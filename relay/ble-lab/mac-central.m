/* MIT. Real CoreBluetooth central using the relay's v2 stream and iOS echo. */
#import <Foundation/Foundation.h>
#import <CoreBluetooth/CoreBluetooth.h>
#include "relay_stream.h"
#include "relay_batch.h"
#include "relay_compact.h"
#include "relay_game_load.h"
#include <time.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
static uint64_t ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static NSString *const serviceID=@"7B61238D-028A-4F65-99D8-7C8F22A11D4E";
static NSString *const charID=@"7B61238D-028A-4F65-99D8-7C8F22A11D4F";
@interface Central:NSObject<CBCentralManagerDelegate,CBPeripheralDelegate>{
 CBCentralManager *manager;CBPeripheral *phone;CBCharacteristic *characteristic;
 dispatch_source_t timer;LrStream stream;uint16_t limit;uint32_t nonce;
 uint64_t previousTick,tickGapMax,tickGapsOver20;
 uint64_t began,started,nextWrite,lastReport;unsigned windowSize;unsigned batchDelay;id activity;unsigned pace;unsigned rate,generated,accepted,dropped,corrupt,duplicates,stage;
 BOOL duplexTest,duplexReady;NSDictionary *duplexReport;uint64_t duplexTxBytes,duplexRxBytes,duplexNextProbe;unsigned duplexOutSize,duplexProbes;NSMutableSet *probeSeen;NSMutableArray *probeRtts;
 BOOL compactTest,compactReady;LrCompact compactTx,compactRx;uint64_t compactSent,compactReceived;
 LrBatch batch;unsigned batchCount;uint64_t batchSince;
 NSUInteger peakQueue,platformBlocked;NSMutableSet *seen;NSMutableArray *latencies,*pending;NSString *output;
}
-(instancetype)initWithRate:(unsigned)value output:(NSString *)path pace:(unsigned)pacing batch:(unsigned)delay window:(unsigned)window;
-(BOOL)receive:(const uint8_t *)p size:(size_t)n;
-(void)finish:(NSString *)reason;
@end
static bool receiveMessage(void *ctx,const uint8_t *p,size_t n){return [(__bridge Central *)ctx receive:p size:n];}
@implementation Central
-(instancetype)initWithRate:(unsigned)value output:(NSString *)path pace:(unsigned)pacing batch:(unsigned)delay window:(unsigned)window {
 if((self=[super init])){probeSeen=[NSMutableSet new];probeRtts=[NSMutableArray new];duplexOutSize=getenv("LDN_DUPLEX_OUT_BYTES")?(unsigned)atoi(getenv("LDN_DUPLEX_OUT_BYTES")):0;duplexTest=getenv("LDN_DUPLEX_TEST")!=NULL;compactTest=getenv("LDN_COMPACT_TEST")!=NULL;rate=value;windowSize=window;pace=pacing;batchDelay=delay;activity=[[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiated|NSActivityLatencyCritical reason:@"Measure foreground BLE latency"];output=path;nonce=arc4random()|1;began=ms();seen=[NSMutableSet new];latencies=[NSMutableArray new];pending=[NSMutableArray new];
 manager=[[CBCentralManager alloc]initWithDelegate:self queue:dispatch_get_main_queue()];
 timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
 dispatch_source_set_timer(timer,DISPATCH_TIME_NOW,1000000,200000);
 __weak Central *weak=self;dispatch_source_set_event_handler(timer,^{[weak tick];});dispatch_resume(timer);}
 return self;
}
-(void)log:(NSString *)text {fprintf(stderr,"%llu %s\n",(unsigned long long)ms(),text.UTF8String);}
-(void)centralManagerDidUpdateState:(CBCentralManager *)m {
 [self log:[NSString stringWithFormat:@"Bluetooth state=%ld",(long)m.state]];
 if(m.state==CBManagerStatePoweredOn)[m scanForPeripheralsWithServices:@[[CBUUID UUIDWithString:serviceID]] options:nil];
 if(m.state==CBManagerStateUnauthorized || m.state==CBManagerStateUnsupported)[self finish:@"Bluetooth unavailable/permission denied"];
}
-(void)centralManager:(CBCentralManager *)m didDiscoverPeripheral:(CBPeripheral *)p advertisementData:(NSDictionary *)data RSSI:(NSNumber *)rssi {
 (void)data;(void)rssi;if(phone)return;phone=p;phone.delegate=self;[m stopScan];[self log:[NSString stringWithFormat:@"Connecting %@ %@",p.name,p.identifier]];[m connectPeripheral:p options:nil];
}
-(void)centralManager:(CBCentralManager *)m didConnectPeripheral:(CBPeripheral *)p {(void)m;[p discoverServices:@[[CBUUID UUIDWithString:serviceID]]];}
-(void)centralManager:(CBCentralManager *)m didFailToConnectPeripheral:(CBPeripheral *)p error:(NSError *)e {(void)m;(void)p;[self finish:e.localizedDescription?:@"Connection failed"];}
-(void)centralManager:(CBCentralManager *)m didDisconnectPeripheral:(CBPeripheral *)p error:(NSError *)e {(void)m;(void)p;[self finish:e.localizedDescription?:@"Disconnected"];}
-(void)peripheral:(CBPeripheral *)p didDiscoverServices:(NSError *)e {
 if(e){[self finish:e.localizedDescription];return;}for(CBService *s in p.services)[p discoverCharacteristics:@[[CBUUID UUIDWithString:charID]] forService:s];
}
-(void)peripheral:(CBPeripheral *)p didDiscoverCharacteristicsForService:(CBService *)s error:(NSError *)e {
 if(e){[self finish:e.localizedDescription];return;}for(CBCharacteristic *c in s.characteristics)if([c.UUID isEqual:[CBUUID UUIDWithString:charID]]){characteristic=c;[p setNotifyValue:YES forCharacteristic:c];}
}
-(void)peripheral:(CBPeripheral *)p didUpdateNotificationStateForCharacteristic:(CBCharacteristic *)c error:(NSError *)e {
 if(e || !c.isNotifying){[self finish:e.localizedDescription?:@"Notifications unavailable"];return;}
 limit=(uint16_t)MIN(500,[p maximumWriteValueLengthForType:CBCharacteristicWriteWithoutResponse]);
 if(limit<64){[self finish:@"Frame limit below 64"];return;}
 ls_init(&stream,limit,nonce);ls_set_window(&stream,windowSize);stream.ack_delay_ms=getenv("LDN_ACK_DELAY_MS")?(unsigned)atoi(getenv("LDN_ACK_DELAY_MS")):0;lr_batch_init(&batch,limit-LS_HEADER);uint8_t h[12]={'L','R','H','2'};lr_put32(h+4,nonce);lr_put16(h+8,limit);lr_put16(h+10,2);stage=1;
 [p writeValue:[NSData dataWithBytes:h length:12] forCharacteristic:c type:CBCharacteristicWriteWithResponse];
}
-(void)peripheral:(CBPeripheral *)p didWriteValueForCharacteristic:(CBCharacteristic *)c error:(NSError *)e {
 if(e){[self finish:e.localizedDescription];return;}if(stage==1){stage=2;[p readValueForCharacteristic:c];}
}
-(void)peripheral:(CBPeripheral *)p didUpdateValueForCharacteristic:(CBCharacteristic *)c error:(NSError *)e {
 if(e){[self finish:e.localizedDescription];return;}const uint8_t *b=c.value.bytes;size_t n=c.value.length;
 if(stage==2 && n==12 && !memcmp(b,"LRA2",4) && lr_get32(b+4)==nonce && lr_get16(b+8)==limit && lr_get16(b+10)==2){
 uint8_t h[12]={'L','R','N','1'};memcpy(h+4,b+4,8);stage=3;[p writeValue:[NSData dataWithBytes:h length:12] forCharacteristic:c type:CBCharacteristicWriteWithResponse];return;}
 if(stage==3 && n==12 && !memcmp(b,"LRNA",4) && lr_get32(b+4)==nonce){stage=4;started=ms();if(duplexTest){uint8_t start[11]={0x70};lr_put32(start+3,rate);lr_put32(start+7,duplexOutSize);ls_enqueue(&stream,start,11);}if(compactTest){uint8_t caps[9]={RL_CAPS,0,0,LR_VERSION,0x0a};lr_put16(caps+5,RELAY_MAX_UDP);caps[7]=4;caps[8]=RL_FEATURE_STREAM|RL_FEATURE_COMPACT;ls_enqueue(&stream,caps,9);}[self log:[NSString stringWithFormat:(duplexTest?@"RUN rate=%u EACH direction frame=%u, 60s independent load + drain, sampled sizes":@"RUN rate=%u frame=%u, 60s load + 15s drain, 130-byte echo records"),rate,limit]];return;}
 if(stage==4 && !ls_ingest(&stream,b,n,receiveMessage,(__bridge void *)self,ms()))corrupt++;
}
-(BOOL)receive:(const uint8_t *)p size:(size_t)n {
 if(n>=3 && p[0]==RL_BATCH)return lr_batch_receive(p,n,receiveMessage,(__bridge void *)self);
 if(duplexTest){
  if(n==15 && p[0]==0x73){unsigned seq=lr_get32(p+3);uint64_t born=lr_get64(p+7);if(seq>=duplexProbes || born>ms() || [probeSeen containsObject:@(seq)]){corrupt++;return YES;}[probeSeen addObject:@(seq)];[probeRtts addObject:@(ms()-born)];return YES;}
  if(n==3 && p[0]==0x74){duplexReady=YES;started=ms();return YES;}
  if(n==39 && p[0]==0x72){duplexReport=@{@"generated":@(lr_get32(p+3)),@"dropped":@(lr_get32(p+7)),@"received":@(lr_get32(p+11)),@"corrupt":@(lr_get32(p+15)),@"duplicates":@(lr_get32(p+19)),@"sent_record_bytes":@(lr_get64(p+23)),@"received_record_bytes":@(lr_get64(p+31))};return YES;}
  if(n<15 || p[0]!=0x71 || p[1]!=1 || p[2]){corrupt++;return YES;}
  unsigned seq=lr_get32(p+3);if(seq>=rate*60 || n!=(duplexOutSize?duplexOutSize:game_size(1,seq))){corrupt++;return YES;}
  for(size_t i=15;i<n;i++)if(p[i]!=(uint8_t)(seq%251)){corrupt++;return YES;}
  if([seen containsObject:@(seq)]){duplicates++;return YES;}[seen addObject:@(seq)];duplexRxBytes+=n;return YES;
 }
 uint8_t decoded[RELAY_MAX_UDP+10];
 if(compactTest){
  if(n==3 && p[0]==RL_COMPACT_ENABLE){uint8_t ready[3]={RL_COMPACT_READY};if(!ls_enqueue(&stream,ready,3))return NO;compactReady=YES;started=ms();return YES;}
  if(p[0]==RL_SCAN || p[0]==RL_CONFIG)return YES;
  if(p[0]==RL_SEND_COMPACT)compactReceived++;
  size_t size=lr_compact_decode(&compactRx,p,n,RL_SEND,RL_SEND_COMPACT,decoded,sizeof(decoded));
  if(size!=130){corrupt++;return YES;}p=decoded;n=size;
 }
 if(n!=130 || p[0]!=(compactTest?RL_SEND:RL_BENCH_DATA)){corrupt++;return YES;}
 unsigned offset=compactTest?10:3;uint32_t sequence=lr_get32(p+offset);uint64_t born=lr_get64(p+offset+4);
 for(size_t i=offset+12;i<n;i++)if(p[i]!=(uint8_t)(sequence%251)){corrupt++;return YES;}
 if(sequence>=generated || born>ms()){corrupt++;return YES;}
 if(compactTest && (p[3]!=sequence%4 || p[4]!=192 || p[5]!=168 || p[6]!=1 || p[7]!=(uint8_t)(1+sequence/100) || p[8]!=0x10 || p[9]!=0x20)){corrupt++;return YES;}
 NSNumber *key=@(sequence);if([seen containsObject:key]){duplicates++;return YES;}
 [seen addObject:key];[latencies addObject:@(ms()-born)];return YES;
}
-(void)fillPending {
 if(!pending.count || stream.count || stream.flying>=stream.send_window)return;
 lr_batch_init(&batch,limit-LS_HEADER);unsigned count=0;
 for(NSData *p in pending){if(!lr_batch_add(&batch,p.bytes,p.length))break;count++;}
 if(count && ls_enqueue(&stream,batch.bytes,batch.size)){accepted+=count;[pending removeObjectsInRange:NSMakeRange(0,count)];}
}
-(void)tick {
 uint64_t now=ms();if(previousTick){uint64_t gap=now-previousTick;if(gap>tickGapMax)tickGapMax=gap;if(gap>20)tickGapsOver20++;}previousTick=now;if(stage!=4){if(now-began>90000)[self finish:@"Handshake timeout; keep iPhone relay foreground"];return;}
 if(((compactTest && !compactReady)||(duplexTest && !duplexReady)) && now-started>10000){[self finish:@"Compact negotiation timeout"];return;}
 uint64_t elapsed=now-started;unsigned target=((compactTest&&!compactReady)||(duplexTest&&!duplexReady))?0:(unsigned)(MIN(elapsed,60000)*rate/1000);
 while(generated<target){uint8_t packet[RELAY_MAX_UDP+10]={RL_BENCH_DATA};unsigned seq=generated++;size_t recordSize=duplexTest?game_size(0,seq):130;if(duplexTest){packet[0]=0x71;packet[1]=0;}unsigned offset=compactTest?10:3;
 if(compactTest){packet[0]=RL_UDP;packet[3]=seq%4;packet[4]=192;packet[5]=168;packet[6]=1;packet[7]=(uint8_t)(1+seq/100);packet[8]=0x10;packet[9]=0x20;}
 lr_put32(packet+offset,seq);lr_put64(packet+offset+4,now);memset(packet+offset+12,seq%251,recordSize-offset-12);
 if(pending.count<72){if(!pending.count)batchSince=now;uint8_t encoded[RELAY_MAX_UDP+10];size_t size=recordSize;
 if(compactTest){size=lr_compact_encode(&compactTx,packet,130,RL_UDP,RL_UDP_COMPACT,encoded,sizeof(encoded));if(size<130)compactSent++;}
 [pending addObject:[NSData dataWithBytes:compactTest?encoded:packet length:size]];if(duplexTest)duplexTxBytes+=size;}else dropped++;}

 if(duplexTest && duplexReady && elapsed<60000 && now>=duplexNextProbe){uint8_t ping[15]={0x73};lr_put32(ping+3,duplexProbes);lr_put64(ping+7,now);if(ls_enqueue(&stream,ping,sizeof(ping))){duplexProbes++;duplexNextProbe=now+1000;}}
 if(pending.count && (now-batchSince>=batchDelay || elapsed>=60000))[self fillPending];
 peakQueue=MAX(peakQueue,stream.count+pending.count);
 for(unsigned burst=0;burst<LS_WINDOW+1 && now>=nextWrite;burst++){if(pending.count && now-batchSince>=batchDelay)[self fillPending];uint8_t frame[LR_MAX_FRAME];size_t n=ls_prepare(&stream,frame,sizeof(frame),now);
 if(n){if(phone.canSendWriteWithoutResponse){[phone writeValue:[NSData dataWithBytes:frame length:n] forCharacteristic:characteristic type:CBCharacteristicWriteWithoutResponse];ls_commit(&stream,frame,n,now);nextWrite=now+pace;}else platformBlocked++;}}
 if(now-lastReport>=5000){lastReport=now;[self log:[NSString stringWithFormat:@"generated=%u echoed=%lu drop=%u q=%u flight=%u retry=%llu",generated,(unsigned long)seen.count,dropped,stream.count,stream.flying,(unsigned long long)stream.retries]];}
 if(duplexTest){if(elapsed>=75000 || (elapsed>=66000 && duplexReport && !pending.count && !stream.flying))[self finish:duplexReport?@"completed":@"Missing duplex report"];return;}
 if((!compactTest || compactReady) && (elapsed>=75000 || (elapsed>=60000 && seen.count==accepted && !pending.count && !stream.flying)))[self finish:@"completed"];
}
-(void)finish:(NSString *)reason {
 [self log:reason];NSArray *sorted=[latencies sortedArrayUsingSelector:@selector(compare:)];NSUInteger n=sorted.count;NSArray *probes=[probeRtts sortedArrayUsingSelector:@selector(compare:)];NSUInteger pn=probes.count;
 NSDictionary *r=@{@"reason":reason,@"transport":duplexTest?@"physical BLE independent bidirectional generators, measured size samples, assumed uniform pacing":@"physical BLE, Mac central to iPhone peripheral; echo workload, not independent bidirectional generators",@"duplex":@(duplexTest),@"outbound_record_override":@(duplexOutSize),@"probe_sent":@(duplexProbes),@"probe_returned":@(pn),@"probe_rtt_p95_ms":pn?probes[(pn*95+99)/100-1]:[NSNull null],@"iphone_report":duplexReport?:@{},@"mac_received":@(seen.count),@"mac_sent_record_bytes":@(duplexTxBytes),@"mac_received_record_bytes":@(duplexRxBytes),@"stream_ack_mean_ms":@(stream.ack_samples?stream.ack_ms_sum/stream.ack_samples:0),@"stream_ack_max_ms":@(stream.ack_ms_max),@"mac_timer_max_gap_ms":@(tickGapMax),@"mac_timer_gaps_over_20ms":@(tickGapsOver20),@"ack_delay_ms":@(stream.ack_delay_ms),@"compact_test":@(compactTest),@"compact_sent":@(compactSent),@"compact_received":@(compactReceived),@"rate":@(rate),@"central_window":@(windowSize),@"central_batch_ms":@(batchDelay),@"central_pace_ms":@(pace),@"record_bytes":duplexTest?[NSNull null]:@130,@"frame_bytes":@(limit),@"generated":@(generated),@"accepted":@(accepted),@"echoed":duplexTest?[NSNull null]:@(n),@"app_dropped":@(dropped),@"pending_not_enqueued":@(pending.count),@"missing":duplexTest?[NSNull null]:@(accepted-n),@"corrupt":@(corrupt),@"duplicates":@(duplicates),@"retries":@(stream.retries),@"gaps":@(stream.gaps),@"peak_queue":@(peakQueue),@"platform_blocked_ticks":@(platformBlocked),@"rtt_p50_ms":n?sorted[(n-1)/2]:[NSNull null],@"rtt_p95_ms":n?sorted[(n*95+99)/100-1]:[NSNull null],@"rtt_max_ms":n?sorted.lastObject:[NSNull null],@"peer":phone.identifier.UUIDString?:@"none"};
 [[NSJSONSerialization dataWithJSONObject:r options:NSJSONWritingPrettyPrinted error:nil] writeToFile:output atomically:YES];exit([reason isEqual:@"completed"]?0:1);
}
@end
int main(int argc,const char **argv){@autoreleasepool {NSString *lockPath=[NSHomeDirectory() stringByAppendingPathComponent:@"Library/Caches/dev.local.ldn-relay-central.lock"];
int guard=open(lockPath.fileSystemRepresentation,O_CREAT|O_RDWR|O_NOFOLLOW,0600);
if(guard<0 || flock(guard,LOCK_EX|LOCK_NB)){fprintf(stderr,"Another BLE benchmark is running; stop it before starting a new one.\n");return 3;}
if(argc<3 || argc>6)return 2;unsigned rate=(unsigned)atoi(argv[1]);if(!rate || rate>1000)return 2;Central *central=[[Central alloc]initWithRate:rate output:@(argv[2]) pace:argc>=4?(unsigned)atoi(argv[3]):15 batch:argc>=5?(unsigned)atoi(argv[4]):20 window:argc>=6?(unsigned)atoi(argv[5]):8];(void)central;[[NSRunLoop mainRunLoop]run];}return 0;}
