#include <switch.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdatomic.h>
#include "relay_stream.h"
#include "relay_approval.h"
#include "relay_queue.h"
#include "relay_usb.h"
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "relay_protocol.h"
#include "relay_host.h"
#include "relay_batch.h"
#include "relay_compact.h"
#include "relay_oneway.h"
#include "ble_link.inc"

static bool diagnostics_enabled,diagnostic_restart;
static unsigned diagnostic_cycles;
static uint64_t diagnostic_reconnect_at;
static LrCodec codec;
static LdnNetworkInfo networks[RELAY_MAX_NETWORKS], connected_info;
static unsigned network_count;
static uint16_t generation;
static uint8_t protocol;
static bool ldn_ready, station_open, ap_open, hosting, joined;
static LdnIpv4Address local_ip;
static LdnSubnetMask subnet;
static int sockets[RELAY_MAX_SOCKETS]={-1,-1,-1,-1};
static uint16_t ports[RELAY_MAX_SOCKETS];
static uint64_t udp_rx,udp_tx,udp_dropped,rx_bytes,tx_bytes;
static uint8_t command[LR_MAX_MESSAGE];
static size_t command_size;

static bool batch_enabled,compact_enabled;
static LrCompact compact_tx,compact_rx;
#include "stream_link.inc"
static bool usb_mode;
#include "usb_link.inc"
static LrBatch batch;
static uint64_t batch_started;
static bool flush_batch(void){
    if(batch.size<=3)return true;
    if(!transport_enqueue(batch.bytes,batch.size))return false;
    lr_batch_init(&batch,usb_mode?LR_MAX_MESSAGE:codec.frame_limit-(stream_mode?LS_HEADER:LR_HEADER_SIZE));return true;
}
static bool enqueue(const uint8_t *p,size_t n){
    if(!batch_enabled)return transport_enqueue(p,n);
    if(n+5>batch.limit){if(!flush_batch())return false;return transport_enqueue(p,n);}
    if(batch.size<=3)batch_started=now_ms();
    if(lr_batch_add(&batch,p,n))return true;
    if(!flush_batch())return false;
    batch_started=now_ms();return lr_batch_add(&batch,p,n);
}
static void error_reply(uint16_t id,uint8_t opcode,uint8_t error,uint32_t detail){
    uint8_t p[10]={RL_ERROR};lr_put16(p+1,id);p[3]=opcode;p[4]=error;lr_put32(p+5,detail);
    if(!enqueue(p,9)) note("Control response queue full.");
}
static bool receive_command(void *ctx,const uint8_t *p,size_t n){
    (void)ctx;
    if(command_size)return false;
    memcpy(command,p,n);command_size=n;return true;
}
static bool wait_gatt_value_for(BleSession *b,uint8_t *out,size_t *size,u64 timeout){
    u64 start=armGetSystemTick();
    while(running() && armTicksToNs(armGetSystemTick()-start)<timeout){
        if(R_FAILED(eventWait(&b->gatt_event,5000000ULL)))continue;
        BtdrvLeEventInfo e={0};BtdrvBleEventType type=0;
        Result rc=btGetLeEventInfo(&e,sizeof(e),&type);
        if(R_FAILED(rc)){result("GATT event",rc);return false;}
        if(type!=8 || !relevant(&e,&b->char_uuid))continue;
        if(!out){if(!e.size)return true;continue;}
        if(e.size>0 && e.size<=LR_MAX_FRAME){
            if(b->session.active){size_t n=lp_unwrap(&b->session,e.data,e.size,out,LR_MAX_FRAME);if(!n)continue;*size=n;}
            else{memcpy(out,e.data,e.size);*size=e.size;}return true;
        }
    }
    return false;
}
static bool wait_gatt_value(BleSession *b,uint8_t *out,size_t *size){
    return wait_gatt_value_for(b,out,size,1500000000ULL);
}
static bool ble_exchange(BleSession *b,const uint8_t *out,size_t out_size,uint8_t *in,size_t *in_size){
    BtdrvGattId service={.instance_id=b->service.instance_id,.uuid=b->service.attr.uuid};
    BtdrvGattId attr={.instance_id=b->characteristic.instance_id,.uuid=b->characteristic.attr.uuid};
    uint8_t wire[LR_MAX_FRAME];const uint8_t *value=out;size_t length=out_size;
    if(b->session.active){length=lp_wrap(&b->session,out,out_size,wire,sizeof(wire));if(!length)return false;value=wire;}
    Result rc=btLeClientWriteCharacteristic(b->handle,b->service.primary_service,&service,&attr,value,length,0,!b->notify_active);
    if(R_FAILED(rc)){result("BLE write",rc);return false;}
    if(b->notify_active){
        u64 start=armGetSystemTick();
        while(true){
            u64 elapsed=armTicksToNs(armGetSystemTick()-start);
            if(elapsed>=500000000ULL)break;
            u64 left=500000000ULL-elapsed;
            if(!wait_gatt_value_for(b,in,in_size,left))return false;
            /* A delayed response must not become the next exchange's reply. */
            if(out_size>=LR_HEADER_SIZE && out[0]=='L' && out[1]=='R' && out[2]==LR_VERSION && out[3] &&
               *in_size>=LR_HEADER_SIZE && lr_get16(in+6)!=lr_get16(out+4)){
                lr_ingest(&codec,in,*in_size,receive_command,NULL);continue;
            }
            return true;
        }
        return false;
    }
    if(!wait_gatt_value(b,NULL,NULL))return false;
    rc=btLeClientReadCharacteristic(b->handle,b->service.primary_service,&service,&attr,0);
    if(R_FAILED(rc)){result("BLE read",rc);return false;}
    return wait_gatt_value(b,in,in_size);
}
static void disable_notifications(BleSession *b){
    if(b->notify_registered){
        BtdrvGattId service={.instance_id=b->service.instance_id,.uuid=b->service.attr.uuid};
        BtdrvGattId attr={.instance_id=b->characteristic.instance_id,.uuid=b->characteristic.attr.uuid};
        result("deregister BLE notifications",btLeClientDeregisterNotification(b->handle,b->service.primary_service,&service,&attr));
    }
    b->notify_registered=b->notify_active=false;
}
static void probe_notifications(BleSession *b,uint32_t nonce){
    BtdrvGattId service={.instance_id=b->service.instance_id,.uuid=b->service.attr.uuid};
    BtdrvGattId attr={.instance_id=b->characteristic.instance_id,.uuid=b->characteristic.attr.uuid};
    BtdrvGattAttributeUuid cccd={.size=2,.uuid={0x02,0x29}};
    BtdevGattDescriptor descriptor={0};bool found=false;
    Result rc=btdevGattCharacteristicGetDescriptor(&b->characteristic,&cccd,&descriptor,&found);
    if(R_FAILED(rc)||!found){note("Notification descriptor unavailable; using read/write transport.");return;}
    rc=btLeClientRegisterNotification(b->handle,b->service.primary_service,&service,&attr);
    if(!result("register BLE notifications",rc))return;
    b->notify_registered=true;
    const uint8_t enabled[2]={1,0};btdevGattDescriptorSetValue(&descriptor,enabled,sizeof(enabled));
    rc=btdevWriteGattDescriptor(&descriptor);
    if(R_FAILED(rc)){result("enable notification descriptor",rc);disable_notifications(b);return;}
    /* Descriptor completion has its own UUID; consume it before the probe.
       Subscription and actual delivery are verified by the nonce echo below. */
    u64 start=armGetSystemTick();
    while(running() && armTicksToNs(armGetSystemTick()-start)<500000000ULL){
        if(R_FAILED(eventWait(&b->gatt_event,5000000ULL)))continue;
        BtdrvLeEventInfo e={0};BtdrvBleEventType type=0;
        if(R_SUCCEEDED(btGetLeEventInfo(&e,sizeof(e),&type)) && relevant(&e,&cccd))break;
    }
    uint8_t request[12]={'L','R','N','1'},response[LR_MAX_FRAME];size_t size=0;
    lr_put32(request+4,nonce);lr_put16(request+8,codec.frame_limit);lr_put16(request+10,b->streaming?LS_VERSION:LR_VERSION);
    for(unsigned attempt=0;attempt<3 && running();attempt++){
        uint8_t wire[LR_MAX_FRAME];size_t wn=lp_wrap(&b->session,request,sizeof(request),wire,sizeof(wire));
        rc=wn?btLeClientWriteCharacteristic(b->handle,b->service.primary_service,&service,&attr,wire,wn,0,false):MAKERESULT(Module_Libnx,LibnxError_BadInput);
        if(R_SUCCEEDED(rc)){
            u64 sent=armGetSystemTick();
            while(running()){
                u64 elapsed=armTicksToNs(armGetSystemTick()-sent);if(elapsed>=1500000000ULL)break;
                if(!wait_gatt_value_for(b,response,&size,1500000000ULL-elapsed))break;
                if(size==12 && !memcmp(response,"LRNA",4) && lr_get32(response+4)==nonce &&
                   lr_get16(response+8)==codec.frame_limit && lr_get16(response+10)==(b->streaming?LS_VERSION:LR_VERSION)){
                    b->notify_active=true;note("BLE fast transport verified: notifications + writes without response.");return;
                }
                note("Notification probe ignored unrelated value (%u bytes)",(unsigned)size);
            }
        }else result("notification probe write",rc);
        note("Notification probe attempt %u/3 received no matching response",attempt+1);
        svcSleepThread(100000000LL);
    }
    note("BLE notification probe failed; approved stream setup stopped.");disable_notifications(b);
}
static bool handshake(BleSession *b){
    Event mtu_event={0};bool has_event=R_SUCCEEDED(btdevAcquireBleMtuConfigEvent(&mtu_event));
    Result rc=btdevConfigureBleMtu(b->handle,512);result("request BLE MTU 512",rc);
    if(R_SUCCEEDED(rc) && has_event)eventWait(&mtu_event,1000000000ULL);
    if(has_event)eventClose(&mtu_event);
    uint16_t mtu=23;
    if(R_FAILED(btdevGetBleMtu(b->handle,&mtu)) || mtu<23 || mtu>512)mtu=23;
    uint16_t limit=mtu-3;if(limit>LR_MAX_FRAME)limit=LR_MAX_FRAME;
    if(limit<LP_OVERHEAD+64){note("BLE capacity too small; reconnect with a compatible companion.");return false;}
    limit-=LP_OVERHEAD;
    uint8_t random[16],hello[LP_HELLO],reply[LR_MAX_FRAME];size_t reply_n=0;
    randomGet(random,sizeof(random));
    if(!lp_client_start(&b->session,random,limit,hello) ||
       !ble_exchange(b,hello,sizeof(hello),reply,&reply_n) || !lp_client_confirm(&b->session,reply,reply_n)){
        lp_reset(&b->session);note("Companion setup failed. Use matching approval-mode app versions.");return false;
    }
    note("Switch-approved companion connected; no cryptographic authentication or encryption.");
    compact_enabled=false;memset(&compact_tx,0,sizeof(compact_tx));memset(&compact_rx,0,sizeof(compact_rx));
    stream_mode=false;memset(&stream_in,0,sizeof(stream_in));memset(&stream_out,0,sizeof(stream_out));
    atomic_store(&stream_pending,0);atomic_store(&stream_tuning,0);
    for(unsigned variant=0;variant<1;variant++){
        b->streaming=variant<2 && limit>=64;b->read_recovery=variant==0 && b->streaming;
        uint8_t handshake_version=b->read_recovery?'3':b->streaming?'2':'1';
        uint8_t request[12]={'L','R','H',handshake_version},response[LR_MAX_FRAME];size_t size=0;
        uint32_t nonce=0;randomGet(&nonce,sizeof(nonce));if(!nonce)nonce=1;
        lr_put32(request+4,nonce);lr_put16(request+8,limit);lr_put16(request+10,b->streaming?LS_VERSION:LR_VERSION);
        for(unsigned retry=0;retry<2 && running();retry++){
            if(!ble_exchange(b,request,sizeof(request),response,&size))continue;
            const char *magic=b->read_recovery?"LRA3":b->streaming?"LRA2":"LRA1";
            if(size!=12 || memcmp(response,magic,4) || lr_get32(response+4)!=nonce || lr_get16(response+10)!=(b->streaming?LS_VERSION:LR_VERSION))continue;
            uint16_t peer_limit=lr_get16(response+8);if(peer_limit<(b->streaming?64:20) || peer_limit>limit)continue;
            lr_init(&codec,peer_limit);ls_init(&stream,peer_limit,nonce);ls_set_window(&stream,3);stream.ack_delay_ms=5;
            batch_enabled=b->streaming;lr_batch_init(&batch,peer_limit-(b->streaming?LS_HEADER:LR_HEADER_SIZE));
            note("Relay handshake OK; ATT MTU=%u frame_limit=%u version=%u",mtu,peer_limit,handshake_version-'0');
            probe_notifications(b,nonce);
            if(b->streaming && !b->notify_active)break;
            stream_mode=b->streaming;
            uint8_t caps[9]={RL_CAPS,0,0,LR_VERSION,0x0a};
            lr_put16(caps+5,RELAY_MAX_UDP);caps[7]=RELAY_MAX_SOCKETS;
            caps[8]=RL_FEATURE_HOST|RL_FEATURE_BATCH|(b->notify_active?RL_FEATURE_NOTIFY:0)|(stream_mode?(RL_FEATURE_COMPACT|RL_FEATURE_STREAM|RL_FEATURE_SEND_BATCH|RL_FEATURE_QUIET_SEND):0);
            enqueue(caps,sizeof(caps));return true;
        }
        disable_notifications(b);note("Transport negotiation failed; update both apps.");
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
        if(state==LdnState_AccessPointCreated)result("destroy LDN network",ldnDestroyNetwork());
        if(ap_open)result("close LDN access point",ldnCloseAccessPoint());
        if(station_open)result("close LDN station",ldnCloseStation());
        ldnExit();
    }
    ldn_ready=station_open=ap_open=hosting=joined=false;network_count=0;generation++;
    memset(&connected_info,0,sizeof(connected_info));local_ip.addr=0;subnet.mask=0;
}
static bool emit_metadata(uint16_t id,uint8_t opcode){
    if(!joined)return false;
    uint8_t p[LR_MAX_MESSAGE]={0};p[0]=opcode;lr_put16(p+1,id);size_t n=3;
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
static bool emit_info(uint16_t id){return emit_metadata(id,hosting?RL_HOSTED:RL_CONNECTED);}
static void host_network(uint16_t id,const uint8_t *p,size_t n){
    LrHostRequest request;
    if(!lr_host_parse(p,n,&request)){error_reply(id,RL_HOST,RL_ERR_FORMAT,0);return;}
    if(joined){error_reply(id,RL_HOST,RL_ERR_STATE,0);return;}
    leave_ldn();protocol=request.protocol;
    Result rc=ldnInitialize(LdnServiceType_User);
    if(R_SUCCEEDED(rc)){ldn_ready=true;rc=ldnSetProtocol((LdnProtocol)protocol);}
    if(R_SUCCEEDED(rc)){rc=ldnOpenAccessPoint();ap_open=R_SUCCEEDED(rc);}
    if(R_SUCCEEDED(rc))rc=ldnSetAdvertiseData(request.advertisement,request.advertisement_size);
    LdnSecurityConfig security={.security_mode=LdnSecurityMode_Product,.passphrase_size=request.key_size};
    memcpy(security.passphrase,request.key,request.key_size);
    LdnUserConfig user={0};strcpy(user.user_name,"LDN Relay");
    LdnNetworkConfig config={.intent_id={.local_communication_id=(s64)request.communication_id,.scene_id=request.scene},
        .node_count_max=request.max_nodes,.local_communication_version=(s16)request.version};
    if(R_SUCCEEDED(rc))rc=ldnCreateNetwork(&security,&user,&config);
    memset(&security,0,sizeof(security));
    if(R_SUCCEEDED(rc))rc=ldnGetNetworkInfo(&connected_info);
    if(R_SUCCEEDED(rc))rc=ldnGetIpv4Address(&local_ip,&subnet);
    if(R_FAILED(rc)){result("host LDN",rc);error_reply(id,RL_HOST,RL_ERR_NATIVE,rc);leave_ldn();return;}
    hosting=joined=true;
    if(!emit_info(id)){error_reply(id,RL_HOST,RL_ERR_QUEUE,0);leave_ldn();return;}
    note("LDN HOST created; protocol=%u scene=%u version=%u max_nodes=%u",protocol,request.scene,request.version,request.max_nodes);
}
static void advertise(uint16_t id,const uint8_t *p,size_t n){
    if(n>384){error_reply(id,RL_ADVERTISE,RL_ERR_FORMAT,0);return;}
    if(!hosting || !joined){error_reply(id,RL_ADVERTISE,RL_ERR_STATE,0);return;}
    Result rc=ldnSetAdvertiseData(p,n);
    if(R_FAILED(rc)){error_reply(id,RL_ADVERTISE,RL_ERR_NATIVE,rc);return;}
    uint8_t reply[3]={RL_ADVERTISED};lr_put16(reply+1,id);enqueue(reply,sizeof(reply));
}
static void refresh_host(void){
    static uint64_t last;static bool dirty;
    if(!hosting || !joined){last=0;dirty=false;return;}
    if(now_ms()-last<50)return;
    last=now_ms();LdnNetworkInfo next;
    if(R_FAILED(ldnGetNetworkInfo(&next)))return;
    for(unsigned i=0;i<8;i++){
        const LdnNodeInfo *a=&connected_info.nodes[i],*b=&next.nodes[i];
        if(a->is_connected!=b->is_connected || (b->is_connected &&
           (a->ip_addr.addr!=b->ip_addr.addr || memcmp(&a->mac_addr,&b->mac_addr,6))))dirty=true;
    }
    connected_info=next; /* Update destination allowlist before polling new peer packets. */
    if(dirty && emit_metadata(0,RL_MEMBERS)){dirty=false;note("LDN HOST membership: %u nodes",connected_info.node_count);}
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
    enqueue(p,n);note("Scan protocol=%u: %u LDN session(s). Select one in the companion.",proto,network_count);
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
    /* The host's advertised address is also the source used by encrypted game
     * protocols. Bind it explicitly rather than relying on route selection. */
    if(hosting)addr.sin_addr.s_addr=htonl(local_ip.addr);
    if(fd<0 || setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&one,sizeof(one))<0 ||
       fcntl(fd,F_SETFL,O_NONBLOCK)<0 || bind(fd,(struct sockaddr*)&addr,sizeof(addr))<0){
        int e=errno;if(fd>=0)close(fd);error_reply(id,RL_BIND,RL_ERR_SOCKET,(uint32_t)e);return;
    }
    sockets[slot]=fd;ports[slot]=port;uint8_t out[6]={RL_BOUND};lr_put16(out+1,id);memcpy(out+3,p,3);enqueue(out,sizeof(out));
    note("UDP slot %u bound to %s:%u (host=%u).",slot,inet_ntoa(addr.sin_addr),port,hosting);
}
static void send_udp(uint16_t id,const uint8_t *p,size_t n){
    if(n<7 || n>7+RELAY_MAX_UDP || p[0]>=RELAY_MAX_SOCKETS){error_reply(id,RL_SEND,RL_ERR_FORMAT,0);return;}
    unsigned slot=p[0];if(!joined || sockets[slot]<0){error_reply(id,RL_SEND,RL_ERR_STATE,0);return;}
    uint32_t ip;memcpy(&ip,p+1,4);if(!allowed_ip(ntohl(ip),true)){error_reply(id,RL_SEND,RL_ERR_DESTINATION,0);return;}
    struct sockaddr_in dest={.sin_family=AF_INET,.sin_addr={.s_addr=ip}};memcpy(&dest.sin_port,p+5,2);
    if(!dest.sin_port){error_reply(id,RL_SEND,RL_ERR_FORMAT,0);return;}
    ssize_t sent=sendto(sockets[slot],p+7,n-7,0,(struct sockaddr*)&dest,sizeof(dest));
    if(sent<0 || (size_t)sent!=n-7){error_reply(id,RL_SEND,RL_ERR_SOCKET,(uint32_t)errno);return;}
    if(hosting && udp_tx<4){
        struct sockaddr_in source={0};socklen_t size=sizeof(source);
        int rc=getsockname(sockets[slot],(struct sockaddr*)&source,&size);
        note("HOST UDP tx dst=%s:%u bytes=%u",inet_ntoa(dest.sin_addr),ntohs(dest.sin_port),(unsigned)sent);
        if(!rc)note("HOST UDP source=%s:%u",inet_ntoa(source.sin_addr),ntohs(source.sin_port));
    }
    udp_tx++;tx_bytes+=(uint64_t)sent;uint8_t out[6]={RL_SENT};lr_put16(out+1,id);out[3]=(uint8_t)slot;lr_put16(out+4,(uint16_t)sent);if(!stream_mode)enqueue(out,sizeof(out));
}
static void counters(uint16_t id){
    uint8_t p[45]={RL_COUNTERS};lr_put16(p+1,id);p[3]=joined;p[4]=(uint8_t)transport_depth();
    lr_put64(p+5,udp_rx);lr_put64(p+13,udp_tx);lr_put64(p+21,udp_dropped);lr_put64(p+29,rx_bytes);lr_put64(p+37,tx_bytes);enqueue(p,sizeof(p));
}
#include "benchmark.inc"
#include "oneway.inc"
static void handle_command(void);
static bool decode_send(LrCompact *cache,const uint8_t *p,size_t n,uint8_t *out,size_t *size){
    if(!compact_enabled){if(p[0]!=RL_SEND || n<10 || n>10+RELAY_MAX_UDP)return false;memcpy(out,p,n);*size=n;return true;}
    *size=lr_compact_decode(cache,p,n,RL_SEND,RL_SEND_COMPACT,out,10+RELAY_MAX_UDP);return *size!=0;
}
static bool valid_send_batch(void *ctx,const uint8_t *p,size_t n){
    if(p[0]==OW_DATA)return n==OW_SIZE;
    if(p[0]==RL_BENCH_DATA)return n==130;
    uint8_t out[10+RELAY_MAX_UDP];size_t size;return decode_send(ctx,p,n,out,&size);
}
static bool apply_send_batch(void *ctx,const uint8_t *p,size_t n){
    (void)ctx;if(p[0]>=OW_START && p[0]<=OW_PARAM_RESULT){oneway_command(p,n);return true;}if(p[0]==RL_BENCH_DATA){bench_echo(p,n);return true;}
    uint8_t out[10+RELAY_MAX_UDP];size_t size;
    if(!decode_send(&compact_rx,p,n,out,&size))return false;
    send_udp(lr_get16(out+1),out+3,size-3);return true;
}
static void handle_command(void){
    if(command_size<3){command_size=0;return;}
    uint8_t op=command[0];uint16_t id=lr_get16(command+1);const uint8_t *p=command+3;size_t n=command_size-3;
    if(!diagnostics_enabled && (op==RL_BENCH || op==RL_DIAG_RECONNECT || (op>=OW_START && op<=OW_PARAM_RESULT))){error_reply(id,op,RL_ERR_UNSUPPORTED,0);command_size=0;return;}
    LrCompact validate_cache=compact_rx;
    switch(op){
    case OW_START:case OW_DATA:case OW_END:case OW_REPORT:case OW_PARAM:oneway_command(command,command_size);break;
    case RL_BATCH:
        if(!stream_mode || !lr_batch_receive(command,command_size,valid_send_batch,&validate_cache))error_reply(id,op,RL_ERR_FORMAT,0);
        else lr_batch_receive(command,command_size,apply_send_batch,NULL);
        break;
    case RL_DIAG_RECONNECT:
        if(n || joined || usb_mode || diagnostic_cycles>=20)error_reply(id,op,RL_ERR_STATE,0);
        else{diagnostic_cycles++;diagnostic_restart=true;note("QUALIFICATION reconnect %u/20 requested by approved companion",diagnostic_cycles);}break;
    case RL_BENCH:bench_begin(p,n,id);break;
    case RL_BENCH_DATA:bench_echo(command,command_size);break;
    case RL_SCAN:if(n==1)scan_networks(id,p[0]);else error_reply(id,op,RL_ERR_FORMAT,0);break;
    case RL_JOIN:join_network(id,p,n);break;
    case RL_HOST:host_network(id,p,n);break;
    case RL_ADVERTISE:advertise(id,p,n);break;
    case RL_LEAVE:if(!n){leave_ldn();uint8_t out[3]={RL_LEFT};lr_put16(out+1,id);enqueue(out,3);}else error_reply(id,op,RL_ERR_FORMAT,0);break;
    case RL_BIND:bind_port(id,p,n);break;
    case RL_SEND:case RL_SEND_COMPACT:
        if(!apply_send_batch(NULL,command,command_size))error_reply(id,op,RL_ERR_FORMAT,0);
        break;
    case RL_COMPACT_ENABLE:
        if(n || !stream_mode || usb_mode)error_reply(id,op,RL_ERR_UNSUPPORTED,0);
        else {uint8_t ready[3]={RL_COMPACT_READY};lr_put16(ready+1,id);
            if(enqueue(ready,sizeof(ready)))compact_enabled=true;
        }
        break;
    case RL_INFO:if(n || !joined)error_reply(id,op,RL_ERR_STATE,0);else emit_info(id);break;
    case RL_PING:command[0]=RL_PONG;enqueue(command,command_size);break;
    case RL_CONFIG:
        if(n!=1 || (p[0]&~RL_FEATURE_BATCH))error_reply(id,op,RL_ERR_FORMAT,0);
        else if(!flush_batch())error_reply(id,op,RL_ERR_QUEUE,0);
        else {batch_enabled=(p[0]&RL_FEATURE_BATCH)!=0;uint8_t out[4]={RL_CONFIGURED};lr_put16(out+1,id);out[3]=p[0];enqueue(out,sizeof(out));note("Event batching %s",batch_enabled?"enabled":"disabled");}
        break;
    case RL_STATS:if(!n)counters(id);else error_reply(id,op,RL_ERR_FORMAT,0);break;
    default:error_reply(id,op,RL_ERR_UNSUPPORTED,0);break;
    }
    memset(command,0,command_size);command_size=0;
}
static void poll_udp(void){
    if(!joined)return;
    for(unsigned slot=0;slot<RELAY_MAX_SOCKETS;slot++){
        if(sockets[slot]<0)continue;
        for(unsigned burst=0;burst<(batch_enabled?32u:4u);burst++){
            uint8_t packet[RELAY_MAX_UDP+1];struct sockaddr_in src={0};socklen_t slen=sizeof(src);
            ssize_t n=recvfrom(sockets[slot],packet,sizeof(packet),MSG_DONTWAIT,(struct sockaddr*)&src,&slen);
            if(n<0){if(errno!=EAGAIN && errno!=EWOULDBLOCK)udp_dropped++;break;}
            if(n>RELAY_MAX_UDP || !allowed_ip(ntohl(src.sin_addr.s_addr),false)){udp_dropped++;continue;}
            udp_rx++;rx_bytes+=(uint64_t)n;
            uint8_t out[RELAY_MAX_UDP+10]={RL_UDP};out[3]=(uint8_t)slot;
            memcpy(out+4,&src.sin_addr.s_addr,4);memcpy(out+8,&src.sin_port,2);memcpy(out+10,packet,(size_t)n);
            /* Reserve queue capacity for control replies; drop whole datagrams. */
            uint8_t packed[RELAY_MAX_UDP+10];size_t size=10+(size_t)n;LrCompact next=compact_tx;
            if(compact_enabled)size=lr_compact_encode(&next,out,size,RL_UDP,RL_UDP_COMPACT,packed,sizeof(packed));
            if(transport_depth()>=LR_QUEUE_DEPTH-4 || !size || !enqueue(compact_enabled?packed:out,size))udp_dropped++;
            else if(compact_enabled)compact_tx=next;
        }
    }
}
static void streaming_loop(BleSession *ble){
    oneway.active=oneway_rx.active=false;if(ble)oneway_peer=ble->address;
    atomic_store(&stream_stop,false);atomic_store(&stream_failed,false);bench.active=false;
    Result rc=threadCreate(&stream_thread,usb_mode?usb_worker:stream_worker,ble,NULL,0x10000,0x2c,-2);
    if(R_FAILED(rc)){result("create BLE worker",rc);return;}
    rc=threadStart(&stream_thread);if(R_FAILED(rc)){result("start BLE worker",rc);threadClose(&stream_thread);return;}
    uint64_t last=now_ms(),last_diag=last;
    while(running() && !atomic_load(&stream_failed)){
        if(leave_requested){leave_requested=false;leave_ldn();uint8_t p[3]={RL_LEFT};enqueue(p,3);}
        for(unsigned i=0;i<32;i++){
            LrMessage m;mutexLock(&stream_mutex);bool ok=lq_pop(&stream_in,&m,NULL);mutexUnlock(&stream_mutex);if(!ok)break;
            memcpy(command,m.bytes,m.size);command_size=m.size;handle_command();
        }
        if(diagnostic_restart)break;
        if(!usb_mode && now_ms()-last_diag>=5000){
            last_diag=now_ms();mutexLock(&stream_mutex);
            unsigned recovery_reads=stream_recovery_reads,recovery_timeouts=stream_recovery_timeouts;bool pulling=stream_pulling;
            uint64_t data_frames=stream_data_frames,ack_frames=stream_ack_frames,bytes=stream_tx_bytes;unsigned unsent=stream_unsent,flying=stream_flying;
            uint64_t poll=stream_poll_max_us,read=stream_read_max_us,write=stream_write_max_us,re=stream_read_errors,we=stream_write_errors,empty=stream_empty_events;
            mutexUnlock(&stream_mutex);
            if(pulling)note("BLE read recovery: reads=%u timeouts=%u",recovery_reads,recovery_timeouts);
            note("BLE BYTES data_frames=%llu ack_frames=%llu bytes=%llu unsent=%u flying=%u",(unsigned long long)data_frames,(unsigned long long)ack_frames,(unsigned long long)bytes,unsent,flying);
            note("BLE DIAG pace_ms=%u poll_max_us=%llu read_max_us=%llu write_max_us=%llu read_errors=%llu write_errors=%llu empty_events=%llu",LR_SWITCH_WRITE_PACE_MS,(unsigned long long)poll,(unsigned long long)read,(unsigned long long)write,(unsigned long long)re,(unsigned long long)we,(unsigned long long)empty);
        }
        refresh_host();poll_udp();bench_tick();oneway_tick();if(batch.size>3 && now_ms()-batch_started>=(usb_mode?1:10))flush_batch();
        if(now_ms()-last>=1000){
            last=now_ms();
            if(joined){LdnState state=LdnState_None;rc=ldnGetState(&state);if(R_FAILED(rc)||state!=(hosting?LdnState_AccessPointCreated:LdnState_StationConnected)){leave_ldn();uint8_t p[3]={RL_LEFT};enqueue(p,3);}}
            mutexLock(&stream_mutex);uint64_t tx=stream_tx,rx=stream_rx,retry=stream_retry,avg=stream_ack_avg,max=stream_ack_max,age=stream_queue_age,wakes=stream_wakes,events=stream_events,drain=stream_max_drain;mutexUnlock(&stream_mutex);
            note("%s ms=%llu LDN=%u UDP rx=%llu tx=%llu drop=%llu queued=%u transport_frames_tx=%llu frames_rx=%llu retries=%llu ack_avg_ms=%llu ack_max_ms=%llu queue_max_ms=%llu wakes=%llu events=%llu max_drain=%llu",usb_mode?"USB":"STREAM",(unsigned long long)last,joined,(unsigned long long)udp_rx,(unsigned long long)udp_tx,(unsigned long long)udp_dropped,transport_depth(),(unsigned long long)tx,(unsigned long long)rx,(unsigned long long)retry,(unsigned long long)avg,(unsigned long long)max,(unsigned long long)age,(unsigned long long)wakes,(unsigned long long)events,(unsigned long long)drain);
        }
        svcSleepThread(1000000LL);
    }
    atomic_store(&stream_stop,true);threadWaitForExit(&stream_thread);threadClose(&stream_thread);
    if(atomic_load(&stream_failed))note("Transport stopped: peer timeout or transfer failure; reconnect required.");
}
static void relay_loop(BleSession *ble){
    if(stream_mode){streaming_loop(ble);return;}
    uint8_t out[LR_MAX_FRAME],in[LR_MAX_FRAME];unsigned failures=0;u64 last_check=0,last_progress=armGetSystemTick(),last_received=0;
    unsigned exchanges=0;u64 exchange_ns=0;
    while(running()){
        if(leave_requested){leave_requested=false;leave_ldn();uint8_t p[3]={RL_LEFT};enqueue(p,3);}
        refresh_host();poll_udp();
        flush_batch();
        size_t out_size=lr_frame(&codec,out,sizeof(out)),in_size=0;
        u64 exchange_start=armGetSystemTick();
        if(!ble_exchange(ble,out,out_size,in,&in_size)){
            if(++failures>=3){
                if(ble->notify_active){note("Notifications stalled; falling back to read/write transport.");disable_notifications(ble);failures=0;continue;}
                note("BLE stopped responding; closing relay.");break;
            }continue;
        }
        ++exchanges;exchange_ns+=armTicksToNs(armGetSystemTick()-exchange_start);
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
                if(R_FAILED(rc)||state!=(hosting?LdnState_AccessPointCreated:LdnState_StationConnected)){note("LDN disconnected.");leave_ldn();uint8_t p[3]={RL_LEFT};enqueue(p,3);}
                else ldnGetNetworkInfo(&connected_info);
            }
            if(udp_rx!=last_received){last_received=udp_rx;note("UDP rx=%llu tx=%llu dropped=%llu queued=%u BLE=%s exchanges=%u avg_us=%llu",(unsigned long long)udp_rx,(unsigned long long)udp_tx,(unsigned long long)udp_dropped,codec.count,ble->notify_active?"notify":"read",exchanges,(unsigned long long)(exchanges?exchange_ns/exchanges/1000:0));}
            exchanges=0;exchange_ns=0;
        }
        if(!ble->notify_active && !codec.count && !command_size)svcSleepThread(10000000LL);
    }
}
int main(void){
    consoleInit(NULL);padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    /* Persistent diagnostics are opt-in. Never create a log during normal use.
     * Keep one previous session, and cap each file at 256 KiB in note(). */
    diagnostics_enabled=access("sdmc:/switch/ldn-relay/diagnostics.enabled",F_OK)==0;
    if(diagnostics_enabled){
        rename("sdmc:/switch/ldn-relay/relay.log","sdmc:/switch/ldn-relay/relay.previous.log");
        log_file=fopen("sdmc:/switch/ldn-relay/relay.log","w");
    }
    note("LDN Relay " RELAY_VERSION " by veritrix");note("Open your companion app. Launch this relay through Album.");
    note("Persistent diagnostics: %s",log_file?"enabled (256 KiB limit)":"off");
    note("A connects over Bluetooth; X starts USB to Mac. B leaves LDN; + exits.");
    note("HOS=0x%08x applet_type=%d",hosversionGet(),(int)appletGetAppletType());
    bool sockets_ready=false;
    if(appletGetAppletType()==AppletType_Application)note("Use Album / Applet Mode to avoid the host game's communication-ID restriction.");
    else if(hosversionBefore(20,0,0))note("LDN Relay requires firmware 20 or later.");
    else sockets_ready=result("socketInitializeDefault",socketInitializeDefault());
    while(running()){
        if(sockets_ready && (padGetButtonsDown(&pad)&HidNpadButton_X)){
            compact_enabled=false;memset(&compact_tx,0,sizeof(compact_tx));memset(&compact_rx,0,sizeof(compact_rx));
            usb_mode=true;stream_mode=true;leave_requested=false;command_size=0;
            memset(&stream_in,0,sizeof(stream_in));memset(&stream_out,0,sizeof(stream_out));atomic_store(&stream_pending,0);atomic_store(&stream_tuning,0);
            stream_tx=stream_rx=stream_retry=stream_ack_avg=stream_ack_max=stream_queue_age=stream_wakes=stream_events=stream_max_drain=0;
            lr_init(&codec,LR_MAX_FRAME);batch_enabled=true;lr_batch_init(&batch,LR_MAX_MESSAGE);
            if(result("usbCommsInitialize",usbCommsInitialize())){
                uint8_t caps[9]={RL_CAPS,0,0,LR_VERSION,0x0a};lr_put16(caps+5,RELAY_MAX_UDP);caps[7]=RELAY_MAX_SOCKETS;caps[8]=RL_FEATURE_HOST|RL_FEATURE_BATCH|RL_FEATURE_SEND_BATCH|RL_FEATURE_QUIET_SEND|RL_FEATURE_USB;
                enqueue(caps,sizeof(caps));note("USB ready. Start the Mac USB bridge and game companion.");streaming_loop(NULL);leave_ldn();usbCommsExit();
            }
            usb_mode=false;note("USB stopped. X restarts USB; A starts Bluetooth; + exits.");
        }
        if(sockets_ready && ((padGetButtonsDown(&pad)&HidNpadButton_A) || (diagnostic_reconnect_at && now_ms()>=diagnostic_reconnect_at))){
            diagnostic_reconnect_at=0;diagnostic_restart=false;
            BleSession ble={0};leave_requested=false;command_size=0;
            if(prepare_ble(&ble) && approve_companion() && handshake(&ble))relay_loop(&ble);
            leave_ldn();release_ble(&ble);lp_reset(&ble.session);memset(&codec,0,sizeof(codec));
            if(diagnostic_restart){diagnostic_restart=false;diagnostic_reconnect_at=now_ms()+3000;}
            note("Relay stopped. A reconnects; + exits.");
        }
        consoleUpdate(NULL);svcSleepThread(10000000LL);
    }
    leave_ldn();if(sockets_ready)socketExit();note("END LDN Relay " RELAY_VERSION);
    if(log_file)fclose(log_file);
    consoleExit(NULL);return 0;
}
