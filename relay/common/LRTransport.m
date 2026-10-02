#include "relay_admission.h"
#include "relay_oneway.h"
#include "relay_notify_gate.h"
#include "relay_approval.h"
/* MIT licence. No UIKit, emulation, decryption, or file I/O on this queue. */
#import "LRTransport.h"
#import <CoreBluetooth/CoreBluetooth.h>
#include "relay_stream.h"
#include "relay_batch.h"
#include "relay_compact.h"
#include "relay_game_load.h"
#include <time.h>
#include "relay_usb.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
static uint64_t clockMs(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static NSString *const serviceUUID=@"7B61238D-028A-4F65-99D8-7C8F22A11D4E";
static NSString *const dataUUID=@"7B61238D-028A-4F65-99D8-7C8F22A11D4F";
@interface LRTransport () <CBPeripheralManagerDelegate> {
    LrApprovedSession session;NSData *sessionReply;NSUUID *sessionOwner;uint64_t sessionStartedAt;
    LrCodec legacy;LrStream stream;LrOneWay oneway,onewayRx;BOOL onewaySend;
    BOOL duplexActive,duplexReported;unsigned duplexOutSize,duplexRate,duplexGenerated,duplexDropped,duplexCorrupt,duplexDuplicates;uint64_t duplexStart,duplexBytes,duplexRxBytes;NSMutableIndexSet *duplexSeen;
    LrCompact compactTx,compactRx;BOOL compactEnabled,compactRequested;
    BOOL usb,usbStopped;int usbSocket;uint64_t usbRetry;NSMutableData *usbInput,*usbOutput;
    dispatch_queue_t queue;dispatch_source_t timer;
    CBPeripheralManager *manager;CBMutableCharacteristic *characteristic;CBCentral *central;
    NSMutableArray<NSData *> *outgoing,*incoming;
    NSData *probe,*snapshot,*readSnapshot;NSUUID *owner;
    unsigned blockedReplySlots;
    uint32_t nonce;uint16_t limit;NSUInteger notificationPace,sendWindow;BOOL streaming,notifying,helloReply,deliveryScheduled;
    LrNotifyGate notificationGate;
    BOOL pullSupported,pulling;uint64_t sessionStarted,readRecoveryFrames,nextRecoveryProbe;
    uint64_t readyCallbacks,watchdogAttempts,watchdogRecoveries;
    uint64_t contact,lastReport,generation,updateBlocked,writeCallbacks,nextNotification,traceCount;
}
- (BOOL)receive:(NSData *)data;
- (void)pump;
- (BOOL)duplexReceive:(NSData *)data;
- (void)duplexTick;
@end
static bool deliver(void *ctx,const uint8_t *p,size_t n){return [(__bridge LRTransport *)ctx receive:[NSData dataWithBytes:p length:n]];}
static bool countBatch(void *ctx,const uint8_t *p,size_t n){(void)n;unsigned *counts=ctx;counts[(p[0]==RL_BENCH_DATA || p[0]==0x73)?1:0]++;return true;}
@implementation LRTransport
- (instancetype)init {if((self=[super init])){queue=dispatch_queue_create("dev.ldn-relay.bluetooth",DISPATCH_QUEUE_SERIAL);NSNumber *pace=[[NSBundle mainBundle] objectForInfoDictionaryKey:@"LDNRelayNotificationPaceMs"];notificationPace=pace?pace.unsignedIntegerValue:15;NSNumber *window=[[NSBundle mainBundle] objectForInfoDictionaryKey:@"LDNRelaySendWindow"];sendWindow=window?window.unsignedIntegerValue:LS_WINDOW;outgoing=[NSMutableArray new];incoming=[NSMutableArray new];limit=20;lr_init(&legacy,20);usbSocket=-1;usb=[[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayTransport"] isEqual:@"usb"];}return self;}
- (void)status:(NSString *)s {dispatch_async(dispatch_get_main_queue(),^{if(self.statusHandler)self.statusHandler(s);});}
- (void)reset {
    memset(&compactTx,0,sizeof(compactTx));memset(&compactRx,0,sizeof(compactRx));compactEnabled=compactRequested=NO;
    duplexActive=NO;duplexSeen=nil;oneway.active=onewayRx.active=false;
    generation++;traceCount=0;blockedReplySlots=0;nonce=0;owner=nil;central=nil;probe=nil;snapshot=nil;streaming=notifying=helloReply=NO;
    pullSupported=pulling=NO;readRecoveryFrames=0;sessionStarted=clockMs();
    lr_notify_ready(&notificationGate);readyCallbacks=watchdogAttempts=watchdogRecoveries=0;
    [outgoing removeAllObjects];[incoming removeAllObjects];limit=20;lr_init(&legacy,20);ls_init(&stream,64,0);
    dispatch_async(dispatch_get_main_queue(),^{if(self.resetHandler)self.resetHandler();});
}
- (void)clearApprovalSession {lp_reset(&session);sessionOwner=nil;sessionReply=nil;sessionStartedAt=0;}
- (NSData *)wireValue:(NSData *)plain notification:(BOOL)notification {
    if(!session.active)return nil;uint8_t wire[LR_MAX_FRAME];size_t n=notification?lp_wrap_notification(&session,plain.bytes,plain.length,wire,sizeof(wire)):lp_wrap(&session,plain.bytes,plain.length,wire,sizeof(wire));return n?[NSData dataWithBytes:wire length:n]:nil;
}
- (void)start {dispatch_async(queue,^{
    if(!self->usb)self->manager=[[CBPeripheralManager alloc] initWithDelegate:self queue:self->queue options:@{CBPeripheralManagerOptionShowPowerAlertKey:@YES}];
    self->timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,self->queue);
    dispatch_source_set_timer(self->timer,dispatch_time(DISPATCH_TIME_NOW,0),5000000,1000000);
    __weak LRTransport *weak=self;dispatch_source_set_event_handler(self->timer,^{[weak tick];});dispatch_resume(self->timer);if(self->usb)[self status:@"USB mode: waiting for local cable bridge (no Bluetooth)."];
});}
- (NSUInteger)queuedMessages {__block NSUInteger n;dispatch_sync(queue,^{n=self->outgoing.count+(self->usb?self->usbOutput.length/LU_PAGE:0)+(self->streaming?self->stream.count+self->stream.flying:self->legacy.count);});return n;}
- (uint16_t)frameLimit {__block uint16_t n;dispatch_sync(queue,^{n=self->limit;});return n;}
- (BOOL)enqueue:(NSData *)data {
    if(!data.length || data.length>LR_MAX_MESSAGE)return NO;
    __block BOOL ok=NO;dispatch_sync(queue,^{if(self->nonce && self->outgoing.count<32){NSData *record=data;LrCompact next=self->compactTx;
        const uint8_t *p=data.bytes;
        if(data.length==8 && p[0]==OW_START && p[3]==1){if(!ow_start(&self->oneway,p,data.length,clockMs()))return;self->onewaySend=YES;}
        if(self->compactEnabled && p[0]==RL_SEND){uint8_t bytes[RELAY_MAX_UDP+10];size_t n=lr_compact_encode(&next,p,data.length,RL_SEND,RL_SEND_COMPACT,bytes,sizeof(bytes));if(!n)return;record=[NSData dataWithBytes:bytes length:n];}
        [self->outgoing addObject:record];self->compactTx=next;ok=YES;}});return ok;
}
- (BOOL)receive:(NSData *)data {
    const uint8_t *p=data.bytes;
    if(data.length==8 && p[0]==OW_START){
        onewaySend=p[3]==2;ow_start(&onewayRx,p,data.length,clockMs());
        if(onewaySend){uint8_t start[8];memcpy(start,p,8);lr_put16(start+1,lr_get16(p+1)|0x8000);ow_start(&oneway,start,8,clockMs());}return YES;
    }
    if(data.length>=3 && p[0]==OW_DATA){ow_receive(&onewayRx,p,data.length);return YES;}
    if(data.length>=3 && p[0]==OW_END){
        if(outgoing.count>=32){blockedReplySlots=1;return NO;}uint8_t report[31];
        if(ow_report(&onewayRx,p,data.length,clockMs(),report)){NSData *d=[NSData dataWithBytes:report length:sizeof(report)];[outgoing addObject:d];return [self receive:d];}return YES;
    }
    if(data.length>=3 && p[0]==RL_BATCH){
        unsigned counts[2]={0};
        if(!lr_batch_receive(p,data.length,countBatch,counts))return YES;
        if([[[NSBundle mainBundle] objectForInfoDictionaryKey:@"LDNRelayCompactLoopback"] boolValue])counts[1]+=counts[0];
        if(incoming.count+counts[0]>64 || outgoing.count+counts[1]>32){blockedReplySlots=counts[1];return NO;}
        return lr_batch_receive(p,data.length,deliver,(__bridge void *)self);
    }
    if([[[NSBundle mainBundle] objectForInfoDictionaryKey:@"LDNRelayDuplex"] boolValue] && data.length>=3 && (p[0]==0x70 || p[0]==0x71 || p[0]==0x73))return [self duplexReceive:data];
    if(data.length>=3 && p[0]==RL_BENCH_DATA){
        if(outgoing.count>=32){blockedReplySlots=1;return NO;}[outgoing addObject:data];return YES;
    }
    if(incoming.count>=64)return NO;
    if(streaming && data.length==9 && p[0]==RL_CAPS && (p[8]&RL_FEATURE_COMPACT) && !compactRequested){
        if(outgoing.count>=32){blockedReplySlots=1;return NO;}
        uint8_t enable[3]={RL_COMPACT_ENABLE};[outgoing addObject:[NSData dataWithBytes:enable length:3]];compactRequested=YES;
    }
    if(data.length==3 && p[0]==RL_COMPACT_READY && compactRequested){compactEnabled=YES;[self status:@"Compact UDP headers negotiated."];return YES;}
    if(compactEnabled && (p[0]==RL_UDP || p[0]==RL_UDP_COMPACT)){
        uint8_t bytes[RELAY_MAX_UDP+10];LrCompact next=compactRx;
        size_t n=lr_compact_decode(&next,p,data.length,RL_UDP,RL_UDP_COMPACT,bytes,sizeof(bytes));
        if(!n){[self status:@"Invalid compact endpoint; reconnect required."];return NO;}
        data=[NSData dataWithBytes:bytes length:n];compactRx=next;
    }
    /* Explicit diagnostic-only loopback exercises the real compact codec over BLE. */
    if(compactEnabled && [[[NSBundle mainBundle] objectForInfoDictionaryKey:@"LDNRelayCompactLoopback"] boolValue] && ((const uint8_t *)data.bytes)[0]==RL_UDP){
        if(outgoing.count>=32){blockedReplySlots=1;return NO;}
        NSMutableData *reply=[data mutableCopy];((uint8_t *)reply.mutableBytes)[0]=RL_SEND;
        uint8_t bytes[RELAY_MAX_UDP+10];LrCompact next=compactTx;
        size_t n=lr_compact_encode(&next,reply.bytes,reply.length,RL_SEND,RL_SEND_COMPACT,bytes,sizeof(bytes));
        if(!n)return NO;[outgoing addObject:[NSData dataWithBytes:bytes length:n]];compactTx=next;return YES;
    }
    [incoming addObject:data];
    if(!deliveryScheduled){
        deliveryScheduled=YES;
        dispatch_async(dispatch_get_main_queue(),^{
            __block NSArray *items;__block uint64_t epoch;
            dispatch_sync(self->queue,^{items=[self->incoming copy];[self->incoming removeAllObjects];self->deliveryScheduled=NO;epoch=self->generation;ls_ready(&self->stream,lr_receive_ready(self->incoming.count,self->outgoing.count,self->blockedReplySlots));});
            for(NSData *item in items){
                __block BOOL current;dispatch_sync(self->queue,^{current=self->generation==epoch;});
                if(!current)break;if(self.messageHandler)self.messageHandler(item);
            }
        });
    }
    return YES;
}
- (BOOL)configureBenchmarkFrame:(unsigned)frame window:(unsigned)window {
    __block BOOL ok=NO;dispatch_sync(queue,^{
        if(self->streaming && frame>=64 && frame<=self->limit && window>=1 && window<=LS_WINDOW &&
           !self->outgoing.count && !self->stream.count && !self->stream.flying){
            ls_set_frame(&self->stream,frame);ls_set_window(&self->stream,window);ok=YES;
        }
    });return ok;
}
- (void)fill {
    /* Form a batch only when the stream can use it. Pre-filling 24 stream
       messages freezes tiny batches while the Bluetooth/window queues are full. */
    while(outgoing.count && (streaming?(stream.count==0 && stream.flying<stream.send_window):legacy.count<LR_QUEUE_DEPTH)){
        NSData *first=outgoing[0];const uint8_t *p=first.bytes;
        if(streaming && first.length>=3 && (p[0]==OW_DATA || p[0]==RL_SEND || p[0]==RL_SEND_COMPACT || p[0]==0x71 || p[0]==RL_BENCH_DATA) && first.length+5<=stream.send_limit-LS_HEADER){
            LrBatch batch;lr_batch_init(&batch,stream.send_limit-LS_HEADER);NSUInteger count=0;
            for(NSData *d in outgoing){const uint8_t *b=d.bytes;if(d.length<3 || (b[0]!=OW_DATA && b[0]!=RL_SEND && b[0]!=RL_SEND_COMPACT && b[0]!=0x71 && b[0]!=RL_BENCH_DATA) || !lr_batch_add(&batch,b,d.length))break;count++;}
            if(!ls_enqueue(&stream,batch.bytes,batch.size))break;
            [outgoing removeObjectsInRange:NSMakeRange(0,count)];
        }else{
            BOOL ok=streaming?ls_enqueue(&stream,first.bytes,first.length):lr_enqueue(&legacy,first.bytes,first.length);
            if(!ok)break;[outgoing removeObjectAtIndex:0];
        }
    }
}
- (void)pump {
    [self fill];if(!notifying || !central || !lr_notify_negotiated(helloReply,probe!=nil))return;
    if(pulling){if(clockMs()<nextRecoveryProbe)return;nextRecoveryProbe=clockMs()+500;}
    BOOL recovering=notificationGate.blocked;
    uint64_t stalledMs=recovering?clockMs()-notificationGate.since:0;
    if(!lr_notify_attempt(&notificationGate,clockMs(),clockMs()-contact<1000))return;
    if(recovering)watchdogAttempts++;
    for(unsigned i=0;i<LS_WINDOW+1;i++){
        [self fill];
        if(streaming && !probe && clockMs()<nextNotification)break;
        uint8_t frame[LR_MAX_FRAME];size_t n=0;NSData *value=probe;
        if(!value){n=streaming?ls_prepare(&stream,frame,sizeof(frame),clockMs()):lr_prepare(&legacy,frame,sizeof(frame));if(!n)break;value=[NSData dataWithBytes:frame length:n];}
        NSData *wire=[self wireValue:value notification:YES];
        if(!wire || wire.length>central.maximumUpdateValueLength){[self status:@"Notification capacity below negotiated frame size; reconnect required."];notifying=NO;break;}
        uint64_t faultAfter=[[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayFaultNotificationAfterMs"] unsignedLongLongValue];
        uint64_t faultDuration=[[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayFaultNotificationDurationMs"] unsignedLongLongValue];
        uint64_t elapsed=clockMs()-sessionStarted;
        BOOL fault=streaming && !probe && faultAfter && elapsed>=faultAfter && (!faultDuration || elapsed-faultAfter<faultDuration);
        BOOL accepted=!fault && [manager updateValue:wire forCharacteristic:characteristic onSubscribedCentrals:@[central]];
        lr_notify_result(&notificationGate,accepted,clockMs());
        if(!accepted){updateBlocked++;break;}
        if(pulling){pulling=NO;[self status:@"Notification delivery accepted again; sending session resume marker with preserved stream."];}
        if(recovering){watchdogRecoveries++;recovering=NO;[self status:[NSString stringWithFormat:@"Notification watchdog resumed after %llu ms; stream sequence preserved.",(unsigned long long)stalledMs]];}
        if(probe){probe=nil;break;}
        if(streaming){if(traceCount<40 && frame[3]){traceCount++;[self status:[NSString stringWithFormat:@"FRAME TX t=%llu seq=%u ack=%u off=%u n=%lu",(unsigned long long)clockMs(),lr_get16(frame+4),lr_get16(frame+6),lr_get16(frame+8),(unsigned long)n]];}ls_commit(&stream,frame,n,clockMs());nextNotification=clockMs()+notificationPace;}else{lr_commit(&legacy,frame,n);break;}
    }
}

#include "LRUSB.inc"
#include "LROneWay.inc"
#include "LRDuplex.inc"
- (void)tick {
    if(usb){[self usbTick];return;}
    if(nonce && clockMs()-contact>30000){[self status:@"Bluetooth idle for 30 seconds; reconnect the Switch."];[self reset];[self clearApprovalSession];return;}
    if(session.active && !nonce && clockMs()-sessionStartedAt>10000)[self clearApprovalSession];
    [self duplexTick];[self onewayTick];
    if(streaming){
        BOOL ready=lr_receive_ready(incoming.count,outgoing.count,blockedReplySlots);
        if(ready)blockedReplySlots=0;
        ls_ready(&stream,ready);[self pump];
    }
    if(nonce && clockMs()-lastReport>=5000){
        lastReport=clockMs();[self status:[NSString stringWithFormat:@"Transport mode=%@ queue=%lu in=%lu flight=%u tx=%llu rx=%llu retry=%llu blocked=%llu callbacks=%llu ack_avg_ms=%llu ack_max_ms=%llu gaps=%llu malformed=%llu peer_credit=%u receive_ready=%u receive_blocked=%llu",streaming?(pulling?@"stream-v2/read-recovery":@"stream-v2/notify"):notifying?@"notify-v1":@"read-v1",(unsigned long)outgoing.count,(unsigned long)incoming.count,streaming?stream.flying:0,(unsigned long long)stream.tx_frames,(unsigned long long)stream.rx_frames,(unsigned long long)stream.retries,(unsigned long long)updateBlocked,(unsigned long long)writeCallbacks,(unsigned long long)(stream.ack_samples?stream.ack_ms_sum/stream.ack_samples:0),(unsigned long long)stream.ack_ms_max,(unsigned long long)stream.gaps,(unsigned long long)stream.malformed,stream.peer_credit,stream.ready?1:0,(unsigned long long)stream.blocked]];
        [self status:[NSString stringWithFormat:@"Notification gate: blocked=%u wait_ms=%llu ready_callbacks=%llu watchdog_attempts=%llu recoveries=%llu subscribed=%u read_frames=%llu",notificationGate.blocked,(unsigned long long)(notificationGate.blocked?clockMs()-notificationGate.since:0),(unsigned long long)readyCallbacks,(unsigned long long)watchdogAttempts,(unsigned long long)watchdogRecoveries,notifying,(unsigned long long)readRecoveryFrames]];
    }
}
- (void)peripheralManagerDidUpdateState:(CBPeripheralManager *)peripheral {
    [self reset];[self clearApprovalSession];if(peripheral.state!=CBManagerStatePoweredOn){[self status:@"Bluetooth unavailable."];return;}
    [peripheral removeAllServices];characteristic=[[CBMutableCharacteristic alloc] initWithType:[CBUUID UUIDWithString:dataUUID] properties:CBCharacteristicPropertyRead|CBCharacteristicPropertyWrite|CBCharacteristicPropertyWriteWithoutResponse|CBCharacteristicPropertyNotify value:nil permissions:CBAttributePermissionsReadable|CBAttributePermissionsWriteable];
    CBMutableService *service=[[CBMutableService alloc] initWithType:[CBUUID UUIDWithString:serviceUUID] primary:YES];service.characteristics=@[characteristic];[peripheral addService:service];
}
- (void)peripheralManager:(CBPeripheralManager *)p didAddService:(CBService *)s error:(NSError *)e {if(e){[self status:e.localizedDescription];return;}[p startAdvertising:@{CBAdvertisementDataServiceUUIDsKey:@[[CBUUID UUIDWithString:serviceUUID]],CBAdvertisementDataLocalNameKey:@"LDNRelay"}];}
- (void)peripheralManagerDidStartAdvertising:(CBPeripheralManager *)p error:(NSError *)e {[self status:e?e.localizedDescription:@"Ready. Press A on the Switch to find this app, then approve the connection there."];}
- (void)peripheralManager:(CBPeripheralManager *)p central:(CBCentral *)c didSubscribeToCharacteristic:(CBCharacteristic *)ch {
    if([ch.UUID isEqual:characteristic.UUID]){[p setDesiredConnectionLatency:CBPeripheralManagerConnectionLatencyLow forCentral:c];[self status:[NSString stringWithFormat:@"Subscribed, max=%lu; requested low connection latency.",(unsigned long)c.maximumUpdateValueLength]];}
}
- (void)peripheralManager:(CBPeripheralManager *)p central:(CBCentral *)c didUnsubscribeFromCharacteristic:(CBCharacteristic *)ch {
    if([c.identifier isEqual:sessionOwner]){[self reset];[self clearApprovalSession];[self status:@"Companion disconnected; ready for a new Switch-approved connection."];}
}
- (void)peripheralManagerIsReadyToUpdateSubscribers:(CBPeripheralManager *)p {readyCallbacks++;lr_notify_ready(&notificationGate);[self pump];}
- (void)peripheralManager:(CBPeripheralManager *)peripheral didReceiveWriteRequests:(NSArray<CBATTRequest *> *)requests {
    if(!requests.count)return;CBATTRequest *first=requests[0];writeCallbacks++;
    /* Local approval happens on the Switch before LRO5. This is not peer
       authentication: central identity and nonce only scope the connection. */
    if(requests.count==1 && first.offset==0 && [first.characteristic.UUID isEqual:characteristic.UUID]){
        NSData *d=first.value;const uint8_t *bytes=d.bytes;
        if(d.length==LP_HELLO && !memcmp(bytes,"LRO5",4)){
            if(nonce || (sessionOwner && ![sessionOwner isEqual:first.central.identifier])){
                [peripheral respondToRequest:first withResult:CBATTErrorInsufficientAuthorization];return;
            }
            uint8_t reply[LP_HELLO];
            if(!lp_server_accept(&session,bytes,d.length,reply)){
                [peripheral respondToRequest:first withResult:CBATTErrorInvalidAttributeValueLength];return;
            }
            sessionOwner=first.central.identifier;sessionStartedAt=clockMs();sessionReply=[NSData dataWithBytes:reply length:sizeof(reply)];
            [self status:@"Connection setup received from Switch; approval mode has no cryptographic authentication or encryption."];
            [peripheral respondToRequest:first withResult:CBATTErrorSuccess];return;
        }
        if(d.length>=4 && (!memcmp(bytes,"LRH4",4)||!memcmp(bytes,"LRF4",4))){
            [self status:@"Incompatible paired relay: update the Switch and companion to approval-mode builds."];
            [peripheral respondToRequest:first withResult:CBATTErrorRequestNotSupported];return;
        }
    }
    if(!session.active || ![sessionOwner isEqual:first.central.identifier]){[peripheral respondToRequest:first withResult:CBATTErrorInsufficientAuthorization];return;}
    /* Decode the entire callback transaction before applying any command. */
    LrApprovedSession candidate=session;NSMutableArray<NSData *> *decoded=[NSMutableArray new];
    for(CBATTRequest *r in requests){
        uint8_t plain[LP_INNER_MAX];size_t n=0;
        if(!r.offset && [r.characteristic.UUID isEqual:characteristic.UUID] && [sessionOwner isEqual:r.central.identifier])n=lp_unwrap(&candidate,r.value.bytes,r.value.length,plain,sizeof(plain));
        if(!n){[peripheral respondToRequest:first withResult:CBATTErrorInsufficientAuthorization];return;}
        [decoded addObject:[NSData dataWithBytes:plain length:n]];
    }
    session=candidate;lp_reset(&candidate);sessionReply=nil;
    for(NSUInteger i=0;i<requests.count;i++)requests[i].value=decoded[i];
    for(CBATTRequest *r in requests){
        NSData *d=r.value;const uint8_t *p=d.bytes;
        if(![r.characteristic.UUID isEqual:characteristic.UUID] || r.offset || d.length>LR_MAX_FRAME || d.length<12){[peripheral respondToRequest:first withResult:CBATTErrorInvalidAttributeValueLength];return;}
        BOOL h=d.length==12 && (!memcmp(p,"LRH1",4)||!memcmp(p,"LRH2",4)||!memcmp(p,"LRH3",4));
        BOOL probePacket=d.length==12 && !memcmp(p,"LRN1",4);
        if(owner && ![owner isEqual:r.central.identifier] && clockMs()-contact<30000){[peripheral respondToRequest:first withResult:CBATTErrorInsufficientAuthorization];return;}
        if(h){BOOL v2=p[3]!='1';if(requests.count!=1 || !lr_get32(p+4) || lr_get16(p+10)!=(v2?LS_VERSION:LR_VERSION) || lr_get16(p+8)<(v2?64:20) || lr_get16(p+8)>LP_INNER_MAX){[peripheral respondToRequest:first withResult:CBATTErrorInvalidAttributeValueLength];return;}}
        else if(!nonce || ![owner isEqual:r.central.identifier] || (probePacket?(requests.count!=1 || lr_get32(p+4)!=nonce || lr_get16(p+8)!=limit || lr_get16(p+10)!=(streaming?LS_VERSION:LR_VERSION)):(d.length<(streaming?LS_HEADER:LR_HEADER_SIZE)||d.length>limit))){[peripheral respondToRequest:first withResult:CBATTErrorInvalidAttributeValueLength];return;}
    }
    for(CBATTRequest *r in requests){NSData *d=r.value;const uint8_t *p=d.bytes;
        if(d.length==12 && (!memcmp(p,"LRH1",4)||!memcmp(p,"LRH2",4)||!memcmp(p,"LRH3",4))){
            BOOL v2=p[3]!='1';uint32_t fresh=lr_get32(p+4);uint16_t cap=lr_get16(p+8);
            if(fresh!=nonce || ![owner isEqual:r.central.identifier] || v2!=streaming || (p[3]=='3')!=pullSupported){[self reset];pullSupported=p[3]=='3';nonce=fresh;limit=cap;owner=r.central.identifier;streaming=v2;lr_init(&legacy,limit);ls_init(&stream,limit,nonce);ls_set_window(&stream,(unsigned)sendWindow);stream.ack_delay_ms=[[[NSBundle mainBundle] objectForInfoDictionaryKey:@"LDNRelayAckDelayMs"] unsignedIntValue];[self status:[NSString stringWithFormat:@"Handshake %@ frame=%u",pullSupported?@"LRH3":v2?@"LRH2":@"LRH1",limit]];}
            uint8_t b[12]={'L','R','A',p[3]};lr_put32(b+4,nonce);lr_put16(b+8,limit);lr_put16(b+10,v2?LS_VERSION:LR_VERSION);snapshot=[NSData dataWithBytes:b length:12];helloReply=YES;
        }else if(d.length==12 && !memcmp(p,"LRN1",4)){
            BOOL subscribed=NO;for(CBCentral *c in characteristic.subscribedCentrals)if([c.identifier isEqual:r.central.identifier])subscribed=YES;
            if(subscribed && r.central.maximumUpdateValueLength>=limit+LP_OVERHEAD){central=r.central;notifying=YES;uint8_t b[12]={'L','R','N','A'};memcpy(b+4,p+4,8);probe=[NSData dataWithBytes:b length:12];}
        }else{
            if(streaming){if(traceCount<40 && p[3]){traceCount++;[self status:[NSString stringWithFormat:@"FRAME RX t=%llu seq=%u ack=%u expected=%u off=%u n=%lu",(unsigned long long)clockMs(),lr_get16(p+4),lr_get16(p+6),stream.received==65535?1:stream.received+1,lr_get16(p+8),(unsigned long)d.length]];}ls_ingest(&stream,p,d.length,deliver,(__bridge void *)self,clockMs());}else lr_ingest(&legacy,p,d.length,deliver,(__bridge void *)self);helloReply=NO;
        }
    }
    contact=clockMs();[peripheral respondToRequest:first withResult:CBATTErrorSuccess];[self pump];
}
- (void)peripheralManager:(CBPeripheralManager *)p didReceiveReadRequest:(CBATTRequest *)r {
    if(sessionReply && [r.characteristic.UUID isEqual:characteristic.UUID] && [sessionOwner isEqual:r.central.identifier]){
        if(r.offset>sessionReply.length){[p respondToRequest:r withResult:CBATTErrorInvalidOffset];return;}
        r.value=[sessionReply subdataWithRange:NSMakeRange(r.offset,sessionReply.length-r.offset)];[p respondToRequest:r withResult:CBATTErrorSuccess];return;
    }
    if(!session.active){[p respondToRequest:r withResult:CBATTErrorInsufficientAuthorization];return;}
    if(![r.characteristic.UUID isEqual:characteristic.UUID] || !nonce || ![owner isEqual:r.central.identifier]){[p respondToRequest:r withResult:CBATTErrorInsufficientAuthorization];return;}
    if(!r.offset && !helloReply){
        if(streaming){
            if(!pullSupported){[p respondToRequest:r withResult:CBATTErrorRequestNotSupported];return;}
            if(!pulling){pulling=YES;probe=nil;stream.replay=stream.flying;
                [self status:@"BLE read recovery active; retaining stream, LDN session and queued packets."];}
            [self fill];uint8_t frame[LR_MAX_FRAME];size_t n=ls_prepare(&stream,frame,sizeof(frame),clockMs());
            /* A read always has a fresh heartbeat response. */
            if(!n){stream.ack_dirty=true;stream.ack_due=clockMs();n=ls_prepare(&stream,frame,sizeof(frame),clockMs());}
            snapshot=[NSData dataWithBytes:frame length:n];
            readSnapshot=[self wireValue:snapshot notification:NO];r.value=readSnapshot;contact=clockMs();[p respondToRequest:r withResult:CBATTErrorSuccess];
            if(n){ls_commit(&stream,frame,n,clockMs());readRecoveryFrames++;}return;
        }
        if(notifying)[self status:@"BLE FALLBACK: notification mode -> legacy read/write."];
        notifying=NO;central=nil;probe=nil;[self fill];uint8_t b[LR_MAX_FRAME];size_t n=lr_frame(&legacy,b,sizeof(b));snapshot=[NSData dataWithBytes:b length:n];
    }
    if(!r.offset)readSnapshot=[self wireValue:snapshot notification:NO];
    if(r.offset>readSnapshot.length){[p respondToRequest:r withResult:CBATTErrorInvalidOffset];return;}
    r.value=[readSnapshot subdataWithRange:NSMakeRange(r.offset,readSnapshot.length-r.offset)];contact=clockMs();[p respondToRequest:r withResult:CBATTErrorSuccess];
}
@end
