#include <switch.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "relay_protocol.h"
#include "ble_link.inc"

static LrCodec codec;
static LdnNetworkInfo networks[RELAY_MAX_NETWORKS], connected_info;
static unsigned network_count;
static uint16_t generation;
static uint8_t protocol;
static bool ldn_ready, station_open, joined;
static LdnIpv4Address local_ip;
static LdnSubnetMask subnet;
static int sockets[RELAY_MAX_SOCKETS]={-1,-1,-1,-1};
static uint16_t ports[RELAY_MAX_SOCKETS];
static uint64_t udp_rx,udp_tx,udp_dropped,rx_bytes,tx_bytes;
static uint8_t command[LR_MAX_MESSAGE];
static size_t command_size;

static bool enqueue(const uint8_t *p,size_t n){return lr_enqueue(&codec,p,n);}
static void error_reply(uint16_t id,uint8_t opcode,uint8_t error,uint32_t detail){
    uint8_t p[10]={RL_ERROR};lr_put16(p+1,id);p[3]=opcode;p[4]=error;lr_put32(p+5,detail);
    if(!enqueue(p,9)) note("Control response queue full.");
}
static bool receive_command(void *ctx,const uint8_t *p,size_t n){
    (void)ctx;
    if(command_size)return false;
    memcpy(command,p,n);command_size=n;return true;
}
static bool wait_gatt_value(BleSession *b,uint8_t *out,size_t *size){
    u64 start=armGetSystemTick();
    while(running() && armTicksToNs(armGetSystemTick()-start)<1500000000ULL){
        if(R_FAILED(eventWait(&b->gatt_event,5000000ULL)))continue;
        BtdrvLeEventInfo e={0};BtdrvBleEventType type=0;
        Result rc=btGetLeEventInfo(&e,sizeof(e),&type);
        if(R_FAILED(rc)){result("GATT event",rc);return false;}
        if(type!=8 || !relevant(&e,&b->char_uuid))continue;
        if(!out){if(!e.size)return true;continue;}
        if(e.size>0 && e.size<=LR_MAX_FRAME){memcpy(out,e.data,e.size);*size=e.size;return true;}
    }
    return false;
}
static bool ble_exchange(BleSession *b,const uint8_t *out,size_t out_size,uint8_t *in,size_t *in_size){
    BtdrvGattId service={.instance_id=b->service.instance_id,.uuid=b->service.attr.uuid};
    BtdrvGattId attr={.instance_id=b->characteristic.instance_id,.uuid=b->characteristic.attr.uuid};
    Result rc=btLeClientWriteCharacteristic(b->handle,b->service.primary_service,&service,&attr,out,out_size,0,true);
    if(R_FAILED(rc)){result("BLE write",rc);return false;}
    if(!wait_gatt_value(b,NULL,NULL))return false;
    rc=btLeClientReadCharacteristic(b->handle,b->service.primary_service,&service,&attr,0);
    if(R_FAILED(rc)){result("BLE read",rc);return false;}
    return wait_gatt_value(b,in,in_size);
}
static bool handshake(BleSession *b){
    Event mtu_event={0};bool has_event=R_SUCCEEDED(btdevAcquireBleMtuConfigEvent(&mtu_event));
    Result rc=btdevConfigureBleMtu(b->handle,512);result("request BLE MTU 512",rc);
    if(R_SUCCEEDED(rc) && has_event)eventWait(&mtu_event,1000000000ULL);
    if(has_event)eventClose(&mtu_event);
    uint16_t mtu=23;
    if(R_FAILED(btdevGetBleMtu(b->handle,&mtu)) || mtu<23 || mtu>512)mtu=23;
    uint16_t limit=mtu-3;if(limit>LR_MAX_FRAME)limit=LR_MAX_FRAME;
    uint8_t request[12]={'L','R','H','1'},response[LR_MAX_FRAME];size_t size=0;
    uint32_t nonce=0;randomGet(&nonce,sizeof(nonce));if(!nonce)nonce=1;
    lr_put32(request+4,nonce);lr_put16(request+8,limit);lr_put16(request+10,LR_VERSION);
    for(unsigned retry=0;retry<3 && running();retry++){
        if(!ble_exchange(b,request,sizeof(request),response,&size))continue;
        if(size!=12 || memcmp(response,"LRA1",4) || lr_get32(response+4)!=nonce || lr_get16(response+10)!=LR_VERSION)continue;
        uint16_t peer_limit=lr_get16(response+8);
        if(peer_limit<20 || peer_limit>limit)continue;
        lr_init(&codec,peer_limit);
        note("Relay handshake OK; ATT MTU=%u frame_limit=%u",mtu,peer_limit);
        uint8_t caps[9]={RL_CAPS,0,0,LR_VERSION,0x0a};
        lr_put16(caps+5,RELAY_MAX_UDP);caps[7]=RELAY_MAX_SOCKETS;caps[8]=0;enqueue(caps,sizeof(caps));
        return true;
    }
    note("Relay companion handshake failed.");return false;
}
static void close_sockets(void){
    for(unsigned i=0;i<RELAY_MAX_SOCKETS;i++){if(sockets[i]>=0)close(sockets[i]);sockets[i]=-1;ports[i]=0;}
}
static void leave_ldn(void){
    close_sockets();
    if(ldn_ready){
        LdnState state=LdnState_None;
        if(R_SUCCEEDED(ldnGetState(&state)) && state==LdnState_StationConnected)result("disconnect LDN",ldnDisconnect());
        if(station_open)result("close LDN station",ldnCloseStation());
        ldnExit();
    }
    ldn_ready=station_open=joined=false;network_count=0;generation++;
    memset(&connected_info,0,sizeof(connected_info));local_ip.addr=0;subnet.mask=0;
}
static bool emit_info(uint16_t id){
    if(!joined)return false;
    uint8_t p[LR_MAX_MESSAGE]={RL_CONNECTED};lr_put16(p+1,id);size_t n=3;
    p[n++]=protocol;uint32_t ip=htonl(local_ip.addr),mask=htonl(subnet.mask);
    memcpy(p+n,&ip,4);n+=4;memcpy(p+n,&mask,4);n+=4;
    lr_put64(p+n,(uint64_t)connected_info.network_id.intent_id.local_communication_id);n+=8;
    lr_put16(p+n,connected_info.network_id.intent_id.scene_id);n+=2;
    size_t ssid_size=connected_info.common.ssid.len;
    if(ssid_size>32 || connected_info.advertise_data_size>0x180)return false;
    p[n++]=(uint8_t)ssid_size;memcpy(p+n,connected_info.common.ssid.str,ssid_size);n+=ssid_size;
    memcpy(p+n,&connected_info.network_id.session_id,16);n+=16;
    lr_put16(p+n,connected_info.advertise_data_size);n+=2;
    memcpy(p+n,connected_info.advertise_data,connected_info.advertise_data_size);n+=connected_info.advertise_data_size;
    size_t count_offset=n++;p[count_offset]=0;
    for(unsigned i=0;i<8;i++){
        const LdnNodeInfo *node=&connected_info.nodes[i];if(!node->is_connected)continue;
        ip=htonl(node->ip_addr.addr);memcpy(p+n,&ip,4);n+=4;
        memcpy(p+n,&node->mac_addr,6);n+=6;p[n++]=node->node_id;p[n++]=node->is_connected;
        lr_put16(p+n,(uint16_t)node->local_communication_version);n+=2;p[n++]=node->platform;p[count_offset]++;
    }
    return enqueue(p,n);
}
static void scan_networks(uint16_t id,uint8_t proto){
    if(joined){error_reply(id,RL_SCAN,RL_ERR_STATE,0);return;}
    if(proto!=1 && proto!=3){error_reply(id,RL_SCAN,RL_ERR_UNSUPPORTED,proto);return;}
    leave_ldn();protocol=proto;
    Result rc=ldnInitialize(LdnServiceType_User);
    if(R_FAILED(rc)){error_reply(id,RL_SCAN,RL_ERR_NATIVE,rc);return;}
    ldn_ready=true;rc=ldnSetProtocol((LdnProtocol)proto);
    if(R_SUCCEEDED(rc)){rc=ldnOpenStation();station_open=R_SUCCEEDED(rc);}
    if(R_FAILED(rc)){error_reply(id,RL_SCAN,RL_ERR_NATIVE,rc);leave_ldn();return;}
    LdnScanFilter filter={0};LdnNetworkInfo found[RELAY_MAX_NETWORKS];s32 count=0;
    for(unsigned attempt=0;attempt<3 && running();attempt++){
        rc=ldnScan(0,&filter,found,RELAY_MAX_NETWORKS,&count);
        if(R_FAILED(rc) || count<0 || count>RELAY_MAX_NETWORKS)break;
        network_count=0;
        for(int i=0;i<count;i++){
            if(found[i].network_id.intent_id.local_communication_id==0 || found[i].node_count<1)continue;
            networks[network_count++]=found[i];
        }
        if(network_count)break;
    }
    if(R_FAILED(rc)){error_reply(id,RL_SCAN,RL_ERR_NATIVE,rc);return;}
    if(count<0 || count>RELAY_MAX_NETWORKS){error_reply(id,RL_SCAN,RL_ERR_FORMAT,0);return;}
    uint8_t p[3+4+RELAY_MAX_NETWORKS*18]={RL_NETWORKS};lr_put16(p+1,id);
    lr_put16(p+3,generation);p[5]=protocol;p[6]=(uint8_t)network_count;size_t n=7;
    for(unsigned i=0;i<network_count;i++){
        const LdnNetworkInfo *net=&networks[i];p[n++]=(uint8_t)i;
        lr_put64(p+n,(uint64_t)net->network_id.intent_id.local_communication_id);n+=8;
        lr_put16(p+n,net->network_id.intent_id.scene_id);n+=2;
        lr_put16(p+n,(uint16_t)net->nodes[0].local_communication_version);n+=2;
        p[n++]=net->node_count;p[n++]=(uint8_t)net->node_count_max;
        lr_put16(p+n,(uint16_t)net->common.channel);n+=2;p[n++]=net->station_accept_policy;
    }
    enqueue(p,n);note("Scan protocol=%u: %u LDN session(s). Select one on the phone.",proto,network_count);
}
static void join_network(uint16_t id,const uint8_t *p,size_t n){
    /* generation:u16, index:u8, version:s16 (-1=host), key_size:u8, key */
    if(n<6 || p[5]<16 || p[5]>64 || n!=6u+p[5]){error_reply(id,RL_JOIN,RL_ERR_FORMAT,0);return;}
    if(joined || !station_open){error_reply(id,RL_JOIN,RL_ERR_STATE,0);return;}
    if(lr_get16(p)!=generation || p[2]>=network_count){error_reply(id,RL_JOIN,RL_ERR_STALE,0);return;}
    LdnNetworkInfo *target=&networks[p[2]];
    if(target->node_count>=target->node_count_max || target->station_accept_policy!=LdnAcceptPolicy_AlwaysAccept){error_reply(id,RL_JOIN,RL_ERR_STATE,1);return;}
    int16_t version=(int16_t)lr_get16(p+3);if(version==-1)version=target->nodes[0].local_communication_version;
    if(version<0){error_reply(id,RL_JOIN,RL_ERR_FORMAT,0);return;}
    LdnSecurityConfig security={.security_mode=LdnSecurityMode_Product,.passphrase_size=p[5]};
    memcpy(security.passphrase,p+6,p[5]);LdnUserConfig user={0};strcpy(user.user_name,"LDN Relay");
    Result rc=ldnConnect(&security,&user,version,0,target);memset(&security,0,sizeof(security));
    if(R_FAILED(rc)){error_reply(id,RL_JOIN,RL_ERR_NATIVE,rc);result("join LDN",rc);return;}
    LdnState state=LdnState_None;rc=ldnGetState(&state);
    if(R_SUCCEEDED(rc))rc=ldnGetNetworkInfo(&connected_info);
    bool matches=R_SUCCEEDED(rc) && !memcmp(&connected_info.network_id,&target->network_id,sizeof(target->network_id));
    if(R_SUCCEEDED(rc))rc=ldnGetIpv4Address(&local_ip,&subnet);
    if(R_FAILED(rc)||state!=LdnState_StationConnected||!matches){error_reply(id,RL_JOIN,RL_ERR_NATIVE,rc);leave_ldn();return;}
    joined=true;
    if(!emit_info(id)){error_reply(id,RL_JOIN,RL_ERR_FORMAT,0);leave_ldn();return;}
    note("LDN connected and network verified. Relay active; B disconnects, + exits.");
}
static bool allowed_ip(uint32_t ip,bool broadcast){
    if(!joined)return false;
    if(ip==local_ip.addr)return true;
    if(broadcast && ip==(local_ip.addr|~subnet.mask))return true;
    for(unsigned i=0;i<8;i++)if(connected_info.nodes[i].is_connected && ip==connected_info.nodes[i].ip_addr.addr)return true;
    return false;
}
static void bind_port(uint16_t id,const uint8_t *p,size_t n){
    if(n!=3 || p[0]>=RELAY_MAX_SOCKETS || !(p[1]|p[2])){error_reply(id,RL_BIND,RL_ERR_FORMAT,0);return;}
    if(!joined){error_reply(id,RL_BIND,RL_ERR_STATE,0);return;}
    unsigned slot=p[0];uint16_t port=(uint16_t)((p[1]<<8)|p[2]);
    for(unsigned i=0;i<RELAY_MAX_SOCKETS;i++)if(i!=slot && ports[i]==port){error_reply(id,RL_BIND,RL_ERR_STATE,port);return;}
    if(sockets[slot]>=0){close(sockets[slot]);sockets[slot]=-1;ports[slot]=0;}
    int fd=socket(AF_INET,SOCK_DGRAM,0);int one=1;
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr={.s_addr=INADDR_ANY}};
    if(fd<0 || setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&one,sizeof(one))<0 ||
       fcntl(fd,F_SETFL,O_NONBLOCK)<0 || bind(fd,(struct sockaddr*)&addr,sizeof(addr))<0){
        int e=errno;if(fd>=0)close(fd);error_reply(id,RL_BIND,RL_ERR_SOCKET,(uint32_t)e);return;
    }
    sockets[slot]=fd;ports[slot]=port;uint8_t out[6]={RL_BOUND};lr_put16(out+1,id);memcpy(out+3,p,3);enqueue(out,sizeof(out));
    note("UDP slot %u bound to port %u.",slot,port);
}
static void send_udp(uint16_t id,const uint8_t *p,size_t n){
    if(n<7 || n>7+RELAY_MAX_UDP || p[0]>=RELAY_MAX_SOCKETS){error_reply(id,RL_SEND,RL_ERR_FORMAT,0);return;}
    unsigned slot=p[0];if(!joined || sockets[slot]<0){error_reply(id,RL_SEND,RL_ERR_STATE,0);return;}
    uint32_t ip;memcpy(&ip,p+1,4);if(!allowed_ip(ntohl(ip),true)){error_reply(id,RL_SEND,RL_ERR_DESTINATION,0);return;}
    struct sockaddr_in dest={.sin_family=AF_INET,.sin_addr={.s_addr=ip}};memcpy(&dest.sin_port,p+5,2);
    if(!dest.sin_port){error_reply(id,RL_SEND,RL_ERR_FORMAT,0);return;}
    ssize_t sent=sendto(sockets[slot],p+7,n-7,0,(struct sockaddr*)&dest,sizeof(dest));
    if(sent<0 || (size_t)sent!=n-7){error_reply(id,RL_SEND,RL_ERR_SOCKET,(uint32_t)errno);return;}
    udp_tx++;tx_bytes+=(uint64_t)sent;uint8_t out[6]={RL_SENT};lr_put16(out+1,id);out[3]=(uint8_t)slot;lr_put16(out+4,(uint16_t)sent);enqueue(out,sizeof(out));
}
static void counters(uint16_t id){
    uint8_t p[45]={RL_COUNTERS};lr_put16(p+1,id);p[3]=joined;p[4]=(uint8_t)codec.count;
    lr_put64(p+5,udp_rx);lr_put64(p+13,udp_tx);lr_put64(p+21,udp_dropped);lr_put64(p+29,rx_bytes);lr_put64(p+37,tx_bytes);enqueue(p,sizeof(p));
}
static void handle_command(void){
    if(command_size<3){command_size=0;return;}
    uint8_t op=command[0];uint16_t id=lr_get16(command+1);const uint8_t *p=command+3;size_t n=command_size-3;
    switch(op){
    case RL_SCAN:if(n==1)scan_networks(id,p[0]);else error_reply(id,op,RL_ERR_FORMAT,0);break;
    case RL_JOIN:join_network(id,p,n);break;
    case RL_LEAVE:if(!n){leave_ldn();uint8_t out[3]={RL_LEFT};lr_put16(out+1,id);enqueue(out,3);}else error_reply(id,op,RL_ERR_FORMAT,0);break;
    case RL_BIND:bind_port(id,p,n);break;
    case RL_SEND:send_udp(id,p,n);break;
    case RL_INFO:if(n || !joined)error_reply(id,op,RL_ERR_STATE,0);else emit_info(id);break;
    case RL_PING:command[0]=RL_PONG;enqueue(command,command_size);break;
    case RL_STATS:if(!n)counters(id);else error_reply(id,op,RL_ERR_FORMAT,0);break;
    default:error_reply(id,op,RL_ERR_UNSUPPORTED,0);break;
    }
    memset(command,0,command_size);command_size=0;
}
static void poll_udp(void){
    if(!joined)return;
    for(unsigned slot=0;slot<RELAY_MAX_SOCKETS;slot++){
        if(sockets[slot]<0)continue;
        for(unsigned burst=0;burst<4;burst++){
            uint8_t packet[RELAY_MAX_UDP+1];struct sockaddr_in src={0};socklen_t slen=sizeof(src);
            ssize_t n=recvfrom(sockets[slot],packet,sizeof(packet),MSG_DONTWAIT,(struct sockaddr*)&src,&slen);
            if(n<0){if(errno!=EAGAIN && errno!=EWOULDBLOCK)udp_dropped++;break;}
            if(n>RELAY_MAX_UDP || !allowed_ip(ntohl(src.sin_addr.s_addr),false)){udp_dropped++;continue;}
            udp_rx++;rx_bytes+=(uint64_t)n;
            uint8_t out[RELAY_MAX_UDP+10]={RL_UDP};out[3]=(uint8_t)slot;
            memcpy(out+4,&src.sin_addr.s_addr,4);memcpy(out+8,&src.sin_port,2);memcpy(out+10,packet,(size_t)n);
            /* Reserve queue capacity for control replies; drop whole datagrams. */
            if(codec.count>=LR_QUEUE_DEPTH-4 || !enqueue(out,10+(size_t)n))udp_dropped++;
        }
    }
}
static void relay_loop(BleSession *ble){
    uint8_t out[LR_MAX_FRAME],in[LR_MAX_FRAME];unsigned failures=0;u64 last_check=0,last_progress=armGetSystemTick(),last_received=0;
    while(running()){
        if(leave_requested){leave_requested=false;leave_ldn();uint8_t p[3]={RL_LEFT};enqueue(p,3);}
        poll_udp();
        size_t out_size=lr_frame(&codec,out,sizeof(out)),in_size=0;
        if(!ble_exchange(ble,out,out_size,in,&in_size)){
            if(++failures>=3){note("BLE stopped responding; closing relay.");break;}continue;
        }
        failures=0;uint16_t old_seq=codec.tx_seq;
        if(!lr_ingest(&codec,in,in_size,receive_command,NULL)){
            if(codec.malformed_frames && codec.malformed_frames%20==0)note("Malformed BLE frames=%llu",(unsigned long long)codec.malformed_frames);
        }
        if(codec.tx_seq!=old_seq || !codec.count)last_progress=armGetSystemTick();
        if(armTicksToNs(armGetSystemTick()-last_progress)>10000000000ULL){note("BLE queue stalled for 10 seconds; disconnecting.");break;}
        if(command_size)handle_command();
        u64 now=armGetSystemTick();
        if(armTicksToNs(now-last_check)>1000000000ULL){
            last_check=now;
            if(joined){
                LdnState state=LdnState_None;Result rc=ldnGetState(&state);
                if(R_FAILED(rc)||state!=LdnState_StationConnected){note("LDN disconnected.");leave_ldn();uint8_t p[3]={RL_LEFT};enqueue(p,3);}
                else ldnGetNetworkInfo(&connected_info);
            }
            if(udp_rx!=last_received){last_received=udp_rx;note("UDP rx=%llu tx=%llu dropped=%llu queued=%u",(unsigned long long)udp_rx,(unsigned long long)udp_tx,(unsigned long long)udp_dropped,codec.count);}
        }
        if(!codec.count && !command_size)svcSleepThread(10000000LL);
    }
}
int main(void){
    consoleInit(NULL);padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    mkdir("sdmc:/switch/pokeldn-bridge-probe",0777);log_file=fopen("sdmc:/switch/pokeldn-bridge-probe/relay-v0_1.log","a");
    note("LDN Relay " RELAY_VERSION);note("Open LDN Relay on iPhone. Launch this app through Album.");
    note("A connects to phone. Phone chooses session and UDP ports. B leaves LDN; + exits.");
    note("HOS=0x%08x applet_type=%d",hosversionGet(),(int)appletGetAppletType());
    bool sockets_ready=false;
    if(appletGetAppletType()==AppletType_Application)note("Use Album / Applet Mode to avoid the host game's communication-ID restriction.");
    else if(hosversionBefore(20,0,0))note("This first relay build requires firmware 20 or later.");
    else sockets_ready=result("socketInitializeDefault",socketInitializeDefault());
    while(running()){
        if(sockets_ready && (padGetButtonsDown(&pad)&HidNpadButton_A)){
            BleSession ble={0};leave_requested=false;command_size=0;
            if(prepare_ble(&ble) && handshake(&ble))relay_loop(&ble);
            leave_ldn();release_ble(&ble);memset(&codec,0,sizeof(codec));
            note("Relay stopped. A reconnects; + exits.");
        }
        consoleUpdate(NULL);svcSleepThread(10000000LL);
    }
    leave_ldn();if(sockets_ready)socketExit();note("END LDN Relay " RELAY_VERSION);
    if(log_file)fclose(log_file);
    consoleExit(NULL);return 0;
}
