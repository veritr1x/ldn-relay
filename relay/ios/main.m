#include "relay_oneway.h"
#import "LRLog.h"
#import <UIKit/UIKit.h>
#import <CoreBluetooth/CoreBluetooth.h>
#import <TargetConditionals.h>
#include "relay_protocol.h"
#include "relay_batch.h"
#import "LRTransport.h"



// Public FRLG profile from Decryptu/pokeldn, pinned in relay/README.md.
// Profiles belong to the phone. The Switch executable contains no game keys.
static NSString *const FRLGKey = @"fcb6f6adb9dfea66aca9c326149d2b3b08a781895cbf78f720d78b85a57584a99665d237797b2a41ddef14063ec28d259143af7832fb3cbcf2759cbfbdc81d8c";

@interface RelayController : UIViewController
@property(nonatomic, strong) LRTransport *transport;
@property(nonatomic, strong) CBMutableCharacteristic *dataCharacteristic;
@property(nonatomic, strong) NSData *readSnapshot;
@property(nonatomic, strong) NSUUID *centralID;
@property(nonatomic, strong) CBCentral *notifyCentral;
@property(nonatomic) BOOL notificationsActive;
@property(nonatomic, strong) NSData *pendingNotification;
@property(nonatomic, strong) UILabel *status;
@property(nonatomic, strong) UILabel *traffic;
@property(nonatomic, strong) UITextView *events;
@property(nonatomic, strong) UITextField *keyField;
@property(nonatomic, strong) UITextField *portField;
@property(nonatomic, strong) UISegmentedControl *protocolControl;
@property(nonatomic, strong) UISegmentedControl *benchmarkRate;
@property(nonatomic) NSUInteger qualificationEchoes,soakRounds;
@property(nonatomic) BOOL qualificationStopped;
@property(nonatomic) NSUInteger sweepIndex;
@property(nonatomic) BOOL sweepActive;
@property(nonatomic) unsigned sweepRequest,sweepWaits;
@property(nonatomic) unsigned oneWayIndex, duplexReports,duplexRequest;
@property(nonatomic) BOOL oneWayActive,paramRequested;
@property(nonatomic, strong) UIStackView *sessionList;
@property(nonatomic, strong) UIButton *scanButton;
@property(nonatomic, strong) UIButton *leaveButton;
@property(nonatomic, strong) UIButton *pingButton;
@property(nonatomic, strong) UIButton *udpButton;
@property(nonatomic, strong) UIButton *statsButton;
@property(nonatomic, strong) UIButton *presetButton;
@property(nonatomic, strong) NSURL *logURL;
@property(nonatomic,strong) LRLog *logger;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic, strong) NSData *networkInfo;
@property(nonatomic, strong) NSData *pingExpected;
@property(nonatomic, strong) NSData *udpExpected;
@property(nonatomic) uint32_t nonce;
@property(nonatomic) uint16_t nextRequest;
@property(nonatomic) uint16_t generation;
@property(nonatomic) uint16_t pingRequest;
@property(nonatomic) uint16_t udpBindRequest;
@property(nonatomic) uint16_t activePort;
@property(nonatomic) uint16_t sessionRequest;
@property(nonatomic) BOOL handshakeReply;
@property(nonatomic) BOOL serviceAdded;
@property(nonatomic) BOOL ready;
@property(nonatomic) BOOL joined;
@property(nonatomic) BOOL sessionBusy;
@property(nonatomic) BOOL udpReady;
@property(nonatomic) NSTimeInterval lastContact;
@property(nonatomic) NSTimeInterval pingStarted;
@property(nonatomic) NSTimeInterval udpStarted;
@property(nonatomic) NSUInteger udpReceived;
@property(nonatomic) NSUInteger udpBytes;
- (BOOL)receiveMessage:(const uint8_t *)bytes size:(size_t)size;
@end

static bool receive_message(void *ctx, const uint8_t *bytes, size_t size) {
    return [(__bridge RelayController *)ctx receiveMessage:bytes size:size];
}
static NSData *hexData(NSString *text) {
    NSString *s = [[text componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] componentsJoinedByString:@""];
    if (s.length % 2 || s.length < 32 || s.length > 128) return nil;
    NSMutableData *data = [NSMutableData data];
    for (NSUInteger i=0; i<s.length; i+=2) {
        unsigned value=0;
        NSString *part=[s substringWithRange:NSMakeRange(i,2)];
        NSScanner *scanner=[NSScanner scannerWithString:part];
        if (![scanner scanHexInt:&value] || !scanner.isAtEnd) return nil;
        uint8_t byte=(uint8_t)value; [data appendBytes:&byte length:1];
    }
    return data;
}

@implementation RelayController
- (void)record:(NSString *)message {
    NSString *line=[NSString stringWithFormat:@"%@ %@\n", NSDate.date, message];
    NSString *text=[(self.events.text ?: @"") stringByAppendingString:line];
    if (text.length>16000) text=[text substringFromIndex:text.length-16000];
    self.events.text=text;
    [self.events scrollRangeToVisible:NSMakeRange(text.length,0)];
    if(!self.logger)self.logger=[[LRLog alloc] initWithURL:self.logURL];
    [self.logger append:line];
}
- (UIButton *)button:(NSString *)title action:(SEL)action {
    UIButton *button=[UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:title forState:UIControlStateNormal];
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    return button;
}
- (void)clearSessions {
    for (UIView *view in self.sessionList.arrangedSubviews) { [self.sessionList removeArrangedSubview:view];[view removeFromSuperview]; }
}
- (void)updateControls {
    BOOL available=self.ready && !self.sessionBusy;
    self.scanButton.enabled=available && !self.joined;
    self.leaveButton.enabled=available && self.joined;
    self.pingButton.enabled=available && !self.pingExpected;
    self.udpButton.enabled=available && self.joined && self.udpReady && !self.udpExpected;
    self.statsButton.enabled=available;
    self.protocolControl.enabled=!self.joined && !self.sessionBusy;
    self.keyField.enabled=self.portField.enabled=self.presetButton.enabled=!self.joined && !self.sessionBusy;
    for (UIButton *button in self.sessionList.arrangedSubviews)
        button.enabled=available && !self.joined && button.tag==1;
}
- (void)viewDidLoad {
    [super viewDidLoad];self.view.backgroundColor=UIColor.systemBackgroundColor;
#if TARGET_OS_MACCATALYST
    NSURL *logDirectory=[[[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject URLByAppendingPathComponent:@"LDN Relay" isDirectory:YES];
    [[NSFileManager defaultManager] createDirectoryAtURL:logDirectory withIntermediateDirectories:YES attributes:nil error:nil];
    self.logURL=[logDirectory URLByAppendingPathComponent:@"relay.log"];
#else
    self.logURL=[[[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject URLByAppendingPathComponent:@"relay.log"];
#endif
    UILabel *title=[UILabel new];title.text=@"LDN Relay";title.font=[UIFont preferredFontForTextStyle:UIFontTextStyleLargeTitle];
    self.status=[UILabel new];self.status.numberOfLines=0;self.status.text=@"Starting Bluetooth…";
    UILabel *instructions=[UILabel new];instructions.numberOfLines=0;
    instructions.text=@"Open LDN Relay through Album on your modified Switch, press A to find this app, then release and press A again at the approval prompt. Keep this app open. Sessions appear automatically after Bluetooth connects; tap your session to join.\n\nTransport tool · playing or trading requires a separate compatible game companion.";
    self.protocolControl=[[UISegmentedControl alloc] initWithItems:@[@"Protocol 1",@"Protocol 3"]];self.protocolControl.selectedSegmentIndex=1;
    self.keyField=[UITextField new];self.keyField.borderStyle=UITextBorderStyleRoundedRect;self.keyField.placeholder=@"Game session passphrase (not a BLE pairing key)";
    self.keyField.autocapitalizationType=UITextAutocapitalizationTypeNone;self.keyField.autocorrectionType=UITextAutocorrectionTypeNo;
    self.keyField.font=[UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular];
    self.portField=[UITextField new];self.portField.borderStyle=UITextBorderStyleRoundedRect;self.portField.placeholder=@"UDP port";self.portField.keyboardType=UIKeyboardTypeNumberPad;
    self.sessionList=[UIStackView new];self.sessionList.axis=UILayoutConstraintAxisVertical;self.sessionList.spacing=6;
    self.traffic=[UILabel new];self.traffic.numberOfLines=0;self.traffic.text=@"No session joined";
    self.events=[UITextView new];self.events.editable=NO;self.events.font=[UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular];
    self.scanButton=[self button:@"Scan sessions" action:@selector(scan)];self.leaveButton=[self button:@"Leave session" action:@selector(leave)];
    self.pingButton=[self button:@"Test BLE" action:@selector(testPing)];self.udpButton=[self button:@"Test UDP loopback" action:@selector(testUDP)];
    self.statsButton=[self button:@"Read counters" action:@selector(stats)];self.presetButton=[self button:@"Load FireRed / LeafGreen settings" action:@selector(loadFRLG)];
    UIStackView *actions=[[UIStackView alloc] initWithArrangedSubviews:@[self.scanButton,self.leaveButton]];actions.distribution=UIStackViewDistributionFillEqually;
    UIStackView *tests=[[UIStackView alloc] initWithArrangedSubviews:@[self.pingButton,self.udpButton]];tests.distribution=UIStackViewDistributionFillEqually;
    self.benchmarkRate=[[UISegmentedControl alloc] initWithItems:@[@"62/s",@"93/s",@"150/s",@"300/s"]];self.benchmarkRate.selectedSegmentIndex=1;
    UIStackView *stack=[[UIStackView alloc] initWithArrangedSubviews:@[title,self.status,instructions,self.presetButton,self.protocolControl,self.keyField,self.portField,actions,self.sessionList,self.traffic,tests,self.statsButton,self.events]];
    if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayDiagnostics"] boolValue]){
        [stack insertArrangedSubview:self.benchmarkRate atIndex:stack.arrangedSubviews.count-2];
        [stack insertArrangedSubview:[self button:@"Run 60-second benchmark" action:@selector(testBenchmark)] atIndex:stack.arrangedSubviews.count-2];
    }
    stack.axis=UILayoutConstraintAxisVertical;stack.spacing=10;stack.translatesAutoresizingMaskIntoConstraints=NO;
    UIScrollView *scroll=[UIScrollView new];scroll.translatesAutoresizingMaskIntoConstraints=NO;scroll.keyboardDismissMode=UIScrollViewKeyboardDismissModeOnDrag;
    [self.view addSubview:scroll];[scroll addSubview:stack];UILayoutGuide *safe=self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[[scroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],[scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],[scroll.topAnchor constraintEqualToAnchor:safe.topAnchor],[scroll.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor],[stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:20],[stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-20],[stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:12],[stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-20],[stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-40],[self.events.heightAnchor constraintEqualToConstant:200]]];
    [self loadFRLG];self.nextRequest=1;
    [self updateControls];
    [self record:[NSString stringWithFormat:@"%@ %@ started. Transport only; gameplay unverified.", [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleDisplayName"], [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"]]];
    self.transport=[LRTransport new];
    __weak RelayController *weakSelf=self;
    self.transport.messageHandler=^(NSData *d){[weakSelf receiveMessage:d.bytes size:d.length];};
    self.transport.statusHandler=^(NSString *s){[weakSelf record:s];};
    self.transport.resetHandler=^{[weakSelf resetLink];};
    [self.transport start];
    self.timer=[NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(tick) userInfo:nil repeats:YES];
    UIApplication.sharedApplication.idleTimerDisabled=YES;
}
- (void)loadFRLG { self.protocolControl.selectedSegmentIndex=1;self.keyField.text=FRLGKey;self.portField.text=@"12345"; }
- (void)resetLink {
    self.ready=NO;self.joined=NO;self.nonce=0;self.centralID=nil;self.handshakeReply=NO;
    self.notificationsActive=NO;self.notifyCentral=nil;self.pendingNotification=nil;
    self.networkInfo=nil;self.pingExpected=nil;self.udpExpected=nil;self.pingRequest=0;self.udpBindRequest=0;
    self.udpReceived=0;self.udpBytes=0;[self clearSessions];
    self.sessionBusy=NO;self.udpReady=NO;self.sessionRequest=0;self.traffic.text=@"No session joined";[self updateControls];
}
- (uint16_t)send:(uint8_t)opcode body:(NSData *)body {
    if (!self.ready) { [self record:@"Connect the Switch first."];return 0; }
    uint16_t request=self.nextRequest++;if (!self.nextRequest)self.nextRequest=1;
    uint8_t message[LR_MAX_MESSAGE]={opcode};lr_put16(message+1,request);
    if (body.length>LR_MAX_MESSAGE-3) return 0;
    if (body.length)memcpy(message+3,body.bytes,body.length);
    if (![self.transport enqueue:[NSData dataWithBytes:message length:3+body.length]]) { [self record:@"Command queue full; wait for delivery."];return 0; }
    return request;
}
- (void)scan {
    [self.view endEditing:YES];if (!self.ready || self.sessionBusy || self.joined) return;
    uint8_t proto=self.protocolControl.selectedSegmentIndex==0?1:3;
    self.sessionRequest=[self send:RL_SCAN body:[NSData dataWithBytes:&proto length:1]];
    if (self.sessionRequest) { self.sessionBusy=YES;[self clearSessions];self.status.text=@"Scanning nearby LDN sessions…";[self record:@"Scanning sessions after Bluetooth is ready."];[self updateControls]; }
}
- (void)joinIndex:(uint8_t)index generation:(uint16_t)generation {
    if (!self.ready || self.sessionBusy || self.joined) return;
    NSData *key=hexData(self.keyField.text);
    NSScanner *scanner=[NSScanner scannerWithString:self.portField.text ?: @""];int port=0;
    if (!key || ![scanner scanInt:&port] || !scanner.isAtEnd || port<1 || port>65535) { [self record:@"Enter a valid hexadecimal passphrase and UDP port (1–65535)."];return; }
    self.activePort=(uint16_t)port;uint8_t body[70];lr_put16(body,generation);body[2]=index;lr_put16(body+3,UINT16_MAX);body[5]=(uint8_t)key.length;memcpy(body+6,key.bytes,key.length);
    self.sessionRequest=[self send:RL_JOIN body:[NSData dataWithBytes:body length:6+key.length]];
    if (self.sessionRequest) { self.sessionBusy=YES;self.status.text=@"Joining the selected session…";[self updateControls]; }
}
- (void)leave { if (self.sessionBusy) return;self.sessionRequest=[self send:RL_LEAVE body:nil];if(self.sessionRequest) { self.sessionBusy=YES;[self updateControls]; } }
- (void)stats { [self send:RL_STATS body:nil]; }
- (void)runOneWay {
    if(!self.oneWayActive)return;
    if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayLDNTest"] boolValue] && !self.joined){self.oneWayActive=NO;[self record:@"LDN COEXISTENCE STOP: host no longer joined."];return;}
    if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelaySimultaneous"] boolValue]){
        if(self.oneWayIndex>=3){self.oneWayActive=NO;[self record:@"INTERVAL TEST COMPLETE: all scheduled settings finished; actual negotiation unverified."];return;}
        unsigned intervals[3]={12,9,6};
        if(!self.paramRequested){self.paramRequested=YES;uint8_t b[2];lr_put16(b,intervals[self.oneWayIndex]);[self send:OW_PARAM body:[NSData dataWithBytes:b length:2]];return;}
        uint8_t b[5]={2};lr_put16(b+1,62);lr_put16(b+3,60);self.duplexReports=0;
        if(self.transport.queuedMessages){self.oneWayActive=NO;[self record:@"INTERVAL SWEEP STOP: previous traffic did not drain."];return;}
        [self record:[NSString stringWithFormat:@"DUPLEX START trial=%u requested_interval_units=%u rate_each=62 seconds=60 record_bytes=130 LDN=%u",self.oneWayIndex,intervals[self.oneWayIndex],self.joined?1:0]];
        self.duplexRequest=[self send:OW_START body:[NSData dataWithBytes:b length:5]];
        unsigned request=self.duplexRequest;
        if(!request){self.oneWayActive=NO;[self record:@"INTERVAL SWEEP STOP: start enqueue failed."];return;}
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,90*NSEC_PER_SEC),dispatch_get_main_queue(),^{if(self.oneWayActive && self.duplexRequest==request && self.duplexReports!=3){self.oneWayActive=NO;[self record:@"INTERVAL SWEEP STOP: missing completion after 90 seconds."];}});return;
    }
    if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayLDNOneWay"] boolValue]){
        if(self.oneWayIndex>=2){self.oneWayActive=NO;[self record:@"LDN ONEWAY COMPLETE: both directions tested separately; actual interval unverified."];return;}
        if(!self.paramRequested){self.paramRequested=YES;uint8_t b[2];lr_put16(b,6);[self send:OW_PARAM body:[NSData dataWithBytes:b length:2]];return;}
        if(self.transport.queuedMessages){self.oneWayActive=NO;[self record:@"LDN ONEWAY STOP: previous traffic did not drain."];return;}
        uint8_t b[5]={(uint8_t)self.oneWayIndex};lr_put16(b+1,62);lr_put16(b+3,60);
        [self record:[NSString stringWithFormat:@"LDN ONEWAY START trial=%u direction=%@ requested_interval_units=6 rate=62 seconds=60 record_bytes=130 LDN=%u",self.oneWayIndex,b[0]?@"phone-to-Switch":@"Switch-to-phone",self.joined?1:0]];
        self.duplexRequest=[self send:OW_START body:[NSData dataWithBytes:b length:5]];
        unsigned request=self.duplexRequest;
        if(!request){self.oneWayActive=NO;[self record:@"LDN ONEWAY STOP: start enqueue failed."];return;}
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,90*NSEC_PER_SEC),dispatch_get_main_queue(),^{if(self.oneWayActive && self.duplexRequest==request){self.oneWayActive=NO;[self record:@"LDN ONEWAY STOP: missing completion after 90 seconds."];}});return;
    }
    if(self.oneWayIndex==6){self.oneWayActive=NO;[self record:@"ONEWAY COMPLETE; requested intervals are not verified negotiated intervals."];return;}
    if((self.oneWayIndex==2 || self.oneWayIndex==4) && !self.paramRequested){
        self.paramRequested=YES;uint8_t b[2];lr_put16(b,self.oneWayIndex==2?12:24);[self send:OW_PARAM body:[NSData dataWithBytes:b length:2]];return;
    }
    if(self.transport.queuedMessages){self.oneWayActive=NO;[self record:@"ONEWAY STOP: previous trial did not drain."];return;}
    uint8_t b[5]={(uint8_t)(self.oneWayIndex%2)};lr_put16(b+1,62);lr_put16(b+3,8);
    [self record:[NSString stringWithFormat:@"ONEWAY trial=%u direction=%@ interval_request=%@ rate=62 seconds=8 record_bytes=130",self.oneWayIndex,b[0]?@"phone-to-Switch":@"Switch-to-phone",self.oneWayIndex<2?@"unchanged":self.oneWayIndex<4?@"15ms":@"30ms"]];
    [self send:OW_START body:[NSData dataWithBytes:b length:5]];
}
- (void)runSweep {
    if(!self.sweepActive)return;
    BOOL ldnSweep=[[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayLDNSweep"] boolValue];
    if(ldnSweep && !self.joined){self.sweepActive=NO;[self record:@"LDN SWEEP INVALID: LDN disconnected."];return;}
    if(self.sweepIndex>=9){self.sweepActive=NO;[self record:@"SWEEP COMPLETE: nine settings; echo RTT, not one-way latency; inspect trial LDN status."];return;}
    unsigned frames[3]={182,320,500},windows[3]={1,3,6};
    unsigned frame=frames[self.sweepIndex/3],window=windows[self.sweepIndex%3];
    if(![self.transport configureBenchmarkFrame:frame window:window]){
        if(ldnSweep && self.sweepWaits++<10){dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),^{[self runSweep];});return;}
        self.sweepActive=NO;[self record:@"SWEEP STOPPED: transport did not drain before next trial."];return;
    }
    uint8_t b[8];lr_put16(b,62);lr_put16(b+2,ldnSweep?15:8);lr_put16(b+4,frame);lr_put16(b+6,window);
    [self record:[NSString stringWithFormat:@"SWEEP trial=%lu frame=%u window=%u rate=62 duration=%u LDN=%u",(unsigned long)self.sweepIndex,frame,window,ldnSweep?15:8,self.joined?1:0]];
    self.sweepRequest=[self send:RL_BENCH body:[NSData dataWithBytes:b length:8]];
    if(!self.sweepRequest){self.sweepActive=NO;[self record:@"SWEEP STOPPED: benchmark command enqueue failed."];return;}
    unsigned trial=self.sweepIndex;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,35*NSEC_PER_SEC),dispatch_get_main_queue(),^{if(self.sweepActive && self.sweepIndex==trial){self.sweepActive=NO;[self record:@"SWEEP STOPPED: missing result after 35 seconds."];}});
}
- (void)testBenchmark {
    uint16_t rates[4]={62,93,150,300};NSUInteger i=(NSUInteger)self.benchmarkRate.selectedSegmentIndex;if(i>3)i=1;uint8_t b[4];lr_put16(b,rates[i]);lr_put16(b+2,60);
    if([self send:RL_BENCH body:[NSData dataWithBytes:b length:4]])[self record:[NSString stringWithFormat:@"Benchmark: %u packets/s, 120-byte payloads, 60 seconds; no game needed.",rates[i]]];
}
- (void)testPing {
    if (self.pingExpected) { [self record:@"BLE test is already pending."];return; }
    NSMutableData *payload=[NSMutableData dataWithLength:1024];arc4random_buf(payload.mutableBytes,payload.length);
    uint16_t request=[self send:RL_PING body:payload];
    if (request) { self.pingExpected=payload;self.pingRequest=request;self.pingStarted=NSProcessInfo.processInfo.systemUptime;[self record:@"BLE test: queued 1024 bytes for exact echo."];[self updateControls]; }
}
- (uint16_t)sendDatagram:(NSData *)data slot:(uint8_t)slot address:(NSData *)address port:(uint16_t)port {
    if (!self.joined || slot>=RELAY_MAX_SOCKETS || address.length!=4 || !port || data.length>RELAY_MAX_UDP) return 0;
    uint8_t body[7+RELAY_MAX_UDP]={slot};memcpy(body+1,address.bytes,4);body[5]=(uint8_t)(port>>8);body[6]=(uint8_t)port;
    if (data.length)memcpy(body+7,data.bytes,data.length);
    return [self send:RL_SEND body:[NSData dataWithBytes:body length:7+data.length]];
}
- (void)testUDP {
    if (!self.joined) { [self record:@"Join an LDN session first."];return; }
    if (self.udpExpected) { [self record:@"UDP test is already pending."];return; }
    uint8_t body[3]={3,0xc0,0}; // Dedicated test socket 49152; never sends to the stock console.
    self.udpBindRequest=[self send:RL_BIND body:[NSData dataWithBytes:body length:3]];
    if (self.udpBindRequest) {
        NSMutableData *payload=[NSMutableData dataWithLength:512];arc4random_buf(payload.mutableBytes,payload.length);self.udpExpected=payload;
        self.udpStarted=NSProcessInfo.processInfo.systemUptime;[self record:@"UDP loopback: binding a test socket on the relay's own LDN address."];
        [self updateControls];
    }
}
- (void)tick {
    NSTimeInterval now=NSProcessInfo.processInfo.systemUptime;
    if (self.nonce && now-self.lastContact>30) { [self record:@"Bluetooth link idle for 30 seconds. Press A on the Switch to reconnect."];[self resetLink];self.status.text=@"Waiting for the Switch"; }
    if (self.pingExpected && now-self.pingStarted>180) { self.pingExpected=nil;[self record:@"BLE test timed out; delivery not verified."]; }
    if (self.udpExpected && now-self.udpStarted>180) { self.udpExpected=nil;[self record:@"UDP loopback timed out; delivery not verified."]; }
    [self updateControls];
}
- (BOOL)receiveMessage:(const uint8_t *)p size:(size_t)n {
    if (n<3) return YES;
    uint16_t request=lr_get16(p+1);
    switch (p[0]) {
    case RL_BATCH:
        if(!lr_batch_receive(p,n,receive_message,(__bridge void *)self)) [self record:@"Rejected malformed batch envelope."];
        return YES;
    case RL_CONFIGURED:
        if(n!=4)break;
        [self record:(p[3]&RL_FEATURE_BATCH)?@"Relay event batching enabled.":@"Relay event batching disabled."];return YES;
    case RL_CAPS: {
        if (n!=9 || p[3]!=LR_VERSION) break;
        self.ready=YES;
        if(p[8]&RL_FEATURE_BATCH) {uint8_t flags=RL_FEATURE_BATCH;[self send:RL_CONFIG body:[NSData dataWithBytes:&flags length:1]];}
        self.status.text=@"Bluetooth connected · scan for a session";
        [self record:(p[8]&RL_FEATURE_USB)?@"USB relay active over cable.":(p[8]&RL_FEATURE_STREAM)?@"BLE stream-v2 active: eight frames in flight, bidirectional batching.":(p[8]&RL_FEATURE_NOTIFY)?@"BLE notify-v1 active (serial exchange).":@"BLE read-v1 active."];
        [self record:[NSString stringWithFormat:@"Relay ready: frame=%u, UDP limit=%u, sockets=%u",self.transport.frameLimit,lr_get16(p+5),p[7]]];NSNumber *autoRate=[[NSBundle mainBundle] objectForInfoDictionaryKey:@"LDNRelayAutoBenchmarkRate"];
        unsigned cycles=[[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayReconnectCycles"] unsignedIntValue];
        if(cycles && !self.qualificationStopped && self.qualificationEchoes<=cycles){
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),^{[self testPing];});return YES;
        }
        if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayLDNTest"] boolValue]){
            [self record:@"LDN coexistence test: select the stock host; test starts after UDP bind."];[self scan];
        }else if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayOneWay"] boolValue]){
            self.oneWayIndex=0;self.oneWayActive=YES;self.paramRequested=NO;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),^{[self runOneWay];});
        }else if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayAutoSweep"] boolValue]){
            self.sweepIndex=0;self.sweepActive=YES;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),^{[self runSweep];});
        }else if(autoRate.unsignedIntValue && (p[8]&RL_FEATURE_STREAM)){
            uint8_t interval[2];lr_put16(interval,12);[self send:OW_PARAM body:[NSData dataWithBytes:interval length:2]];
            [self record:@"Automatic benchmark waiting for 15 ms connection parameter request result."];
        }else [self scan];return YES;
    }
    case RL_NETWORKS: {
        if (n<7 || p[6]>RELAY_MAX_NETWORKS || n!=7u+p[6]*18u) break;
        [self clearSessions];self.generation=lr_get16(p+3);self.sessionBusy=NO;
        self.status.text=[NSString stringWithFormat:@"Found %u session(s) · tap one to join",p[6]];
        for (unsigned i=0;i<p[6];i++) {
            const uint8_t *row=p+7+i*18;uint8_t index=row[0];uint16_t gen=self.generation;
            uint64_t comm=lr_get64(row+1);
            NSString *name=comm==UINT64_C(0x01006fa0233f8000)?@"FireRed / LeafGreen":[NSString stringWithFormat:@"%016llx",(unsigned long long)comm];
            NSString *label=[NSString stringWithFormat:@"%@ · %u/%u · scene %u · ch %u",name,row[13],row[14],lr_get16(row+9),lr_get16(row+15)];
            __weak RelayController *weakSelf=self;
            UIButton *button=[UIButton buttonWithType:UIButtonTypeSystem];button.titleLabel.numberOfLines=0;
            [button setTitle:label forState:UIControlStateNormal];
            [button addAction:[UIAction actionWithHandler:^(UIAction *action){ [weakSelf joinIndex:index generation:gen]; }] forControlEvents:UIControlEventTouchUpInside];
            button.tag=(row[17]==0 && row[13]<row[14])?1:0;[self.sessionList addArrangedSubview:button];
        }
        [self record:[NSString stringWithFormat:@"Scan returned %u session(s), protocol %u.",p[6],p[5]]];[self updateControls];return YES;
    }
    case RL_CONNECTED: {
        if (n<23 || p[22]>32) break;
        size_t offset=23+p[22];if (n<offset+18) break;
        offset+=16;uint16_t adSize=lr_get16(p+offset);offset+=2;
        if (adSize>384 || n<offset+adSize+1) break;
        offset+=adSize;uint8_t nodes=p[offset++];if (nodes>8 || n!=offset+nodes*15u) break;
        self.networkInfo=[NSData dataWithBytes:p length:n];self.joined=YES;[self clearSessions];
        self.status.text=@"LDN joined · opening UDP relay";
        [self record:[NSString stringWithFormat:@"LDN joined: protocol=%u nodes=%u metadata=%lu bytes",p[3],nodes,(unsigned long)n]];
        uint8_t body[3]={0,(uint8_t)(self.activePort>>8),(uint8_t)self.activePort};self.sessionRequest=[self send:RL_BIND body:[NSData dataWithBytes:body length:3]];
        self.sessionBusy=self.sessionRequest!=0;[self updateControls];return YES;
    }
    case RL_LEFT:
        if (n!=3) break;
        if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayLDNTest"] boolValue]){self.oneWayActive=NO;self.sweepActive=NO;[self record:@"LDN COEXISTENCE INVALID: LDN session closed; any remaining BLE results do not prove coexistence."];}
        self.joined=NO;self.sessionBusy=NO;self.udpReady=NO;self.networkInfo=nil;self.udpExpected=nil;self.status.text=@"Session left · Bluetooth remains connected";
        self.traffic.text=@"No session joined";[self record:@"LDN session closed."];[self updateControls];return YES;
    case RL_BOUND:
        if (n!=6) break;
        [self record:[NSString stringWithFormat:@"UDP bound: slot=%u port=%u",p[3],(p[4]<<8)|p[5]]];
        if(p[3]==0 && [[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayLDNSweep"] boolValue]){
            self.sweepIndex=0;self.sweepWaits=0;self.sweepActive=YES;
            uint8_t interval[2];lr_put16(interval,6);[self send:OW_PARAM body:[NSData dataWithBytes:interval length:2]];
        }
        if(p[3]==0 && ![[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayLDNSweep"] boolValue] && [[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayLDNTest"] boolValue] && !self.oneWayActive){
            self.oneWayIndex=[[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayLDNOneWay"] boolValue]?0:2;self.oneWayActive=YES;self.paramRequested=NO;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{[self runOneWay];});
        }
        if (p[3]==0) { self.sessionBusy=NO;self.udpReady=YES;self.status.text=@"UDP relay ready · run both transport tests below";[self updateControls]; }
        if (p[3]==3 && request==self.udpBindRequest && self.udpExpected) {
            NSData *address=[self.networkInfo subdataWithRange:NSMakeRange(4,4)];
            [self sendDatagram:self.udpExpected slot:3 address:address port:49152];
        }
        return YES;
    case RL_SENT:
        if (n!=6) break;
        [self record:[NSString stringWithFormat:@"UDP send accepted by Switch socket: slot=%u bytes=%u",p[3],lr_get16(p+4)]];return YES;
    case RL_UDP: {
        if (n<10 || n>10+RELAY_MAX_UDP || p[3]>=RELAY_MAX_SOCKETS) break;
        if (!self.joined) return YES;
        self.udpReceived++;self.udpBytes+=n-10;
        self.traffic.text=[NSString stringWithFormat:@"Delivered to iPhone: %lu UDP packets · %lu bytes",(unsigned long)self.udpReceived,(unsigned long)self.udpBytes];
        NSData *data=[NSData dataWithBytes:p+10 length:n-10];
        if (p[3]==3 && self.udpExpected && p[8]==0xc0 && !p[9] && !memcmp(p+4,(const uint8_t *)self.networkInfo.bytes+4,4)) {
            BOOL exact=[data isEqualToData:self.udpExpected];
            [self record:[NSString stringWithFormat:@"UDP loopback %@: 512 bytes, %.1f ms. Local relay socket only; not a stock-console response.",exact?@"PASS":@"FAIL",(NSProcessInfo.processInfo.systemUptime-self.udpStarted)*1000]];
            self.udpExpected=nil;[self updateControls];
        } else if (self.udpReceived<=5 || self.udpReceived%100==0) [self record:[NSString stringWithFormat:@"UDP received: slot=%u bytes=%lu total=%lu",p[3],(unsigned long)data.length,(unsigned long)self.udpReceived]];
        // Game adapters subscribe here; payloads remain opaque to the relay.
        [[NSNotificationCenter defaultCenter] postNotificationName:@"LDNRelayDatagram" object:self userInfo:@{@"slot":@(p[3]),@"source":[NSData dataWithBytes:p+4 length:4],@"port":@((p[8]<<8)|p[9]),@"payload":data}];
        return YES;
    }
    case RL_PONG:
        if (self.pingExpected && request==self.pingRequest) {
            NSData *body=[NSData dataWithBytes:p+3 length:n-3];BOOL exact=[body isEqualToData:self.pingExpected];
            [self record:[NSString stringWithFormat:@"BLE echo %@: 1024 bytes, %.1f ms",exact?@"PASS":@"FAIL",(NSProcessInfo.processInfo.systemUptime-self.pingStarted)*1000]];self.pingExpected=nil;[self updateControls];
            unsigned cycles=[[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayReconnectCycles"] unsignedIntValue];
            if(cycles && !self.qualificationStopped && self.qualificationEchoes<=cycles){
                if(!exact){self.qualificationStopped=YES;[self record:@"QUALIFICATION STOP: echo mismatch."];return YES;}
                self.qualificationEchoes++;[self record:[NSString stringWithFormat:@"QUALIFICATION approved session %lu/%u exact echo PASS",(unsigned long)self.qualificationEchoes,cycles+1]];
                if(self.qualificationEchoes<=cycles)[self send:RL_DIAG_RECONNECT body:nil];
                else{
                    [self record:@"QUALIFICATION reconnect sequence complete."];
                    if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayAutoBenchmarkRate"] unsignedIntValue]){uint8_t interval[2];lr_put16(interval,12);[self send:OW_PARAM body:[NSData dataWithBytes:interval length:2]];}
                    else [self record:@"Load test disabled; no further traffic scheduled."];
                }
            }
        }
        return YES;
    case OW_PARAM_RESULT:
        if(n!=9)break;
        [self record:[NSString stringWithFormat:@"PARAM REQUEST interval_units=%u result=0x%08x; negotiated interval/PHY unavailable via current app API",lr_get16(p+3),lr_get32(p+5)]];
        unsigned autoRate=[[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayAutoBenchmarkRate"] unsignedIntValue];
        if(autoRate){
            if(lr_get32(p+5)){[self record:@"Automatic benchmark stopped: connection parameter request failed."];return YES;}
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{
                uint8_t b[4];lr_put16(b,(uint16_t)autoRate);lr_put16(b+2,60);
                [self send:RL_BENCH body:[NSData dataWithBytes:b length:4]];
                [self record:[NSString stringWithFormat:@"Automatic hardware benchmark: %u/s for 60 seconds (15 ms requested, negotiation unverified)",autoRate]];
            });return YES;
        }
        if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayLDNSweep"] boolValue]){
            if(lr_get32(p+5)){self.sweepActive=NO;[self record:@"LDN SWEEP STOP: interval request failed."];}
            else dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{[self runSweep];});
            return YES;
        }
        if(lr_get32(p+5) && [[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelaySimultaneous"] boolValue]){
            [self record:@"INTERVAL request rejected; skipping setting."];self.oneWayIndex++;self.paramRequested=NO;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{[self runOneWay];});
        }else if(lr_get32(p+5)){self.oneWayActive=NO;[self record:@"ONEWAY parameter sweep stopped: driver request unavailable. Baseline directions retained."];}
        else dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{[self runOneWay];});
        return YES;
    case OW_REPORT:
        if(n!=31)break;
        if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelaySimultaneous"] boolValue]){
            unsigned reportId=lr_get16(p+1),bit=(reportId&0x8000)?2:1;
            if((reportId&0x7fff)!=self.duplexRequest || (self.duplexReports&bit))return YES;
            [self record:[NSString stringWithFormat:@"DUPLEX RESULT direction=%@ offered=%u accepted=%u dropped=%u received=%u corrupt=%u duplicates=%u receiver_elapsed_ms=%u",(lr_get16(p+1)&0x8000)?@"phone-to-Switch":@"Switch-to-phone",lr_get32(p+3),lr_get32(p+7),lr_get32(p+11),lr_get32(p+15),lr_get32(p+19),lr_get32(p+23),lr_get32(p+27)]];
            self.duplexReports|=bit;
            if(lr_get32(p+7)!=lr_get32(p+15) || lr_get32(p+19) || lr_get32(p+23)){self.oneWayActive=NO;[self record:@"INTERVAL SWEEP STOP: missing/corrupt/duplicate accepted packets."];}
            if(self.duplexReports==3 && self.oneWayActive){[self record:@"DUPLEX COMPLETE: both receiver summaries received."];self.oneWayIndex++;self.paramRequested=NO;dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{[self runOneWay];});}return YES;
        }
        if([[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelayLDNOneWay"] boolValue]){
            if(!self.oneWayActive || lr_get16(p+1)!=self.duplexRequest)return YES;
            self.duplexRequest=0; /* Retire the watchdog and reject duplicate summaries. */
        }
        [self record:[NSString stringWithFormat:@"ONEWAY RESULT trial=%u offered=%u accepted=%u dropped=%u received=%u corrupt=%u duplicates=%u receiver_elapsed_ms=%u record_bytes=130",self.oneWayIndex,lr_get32(p+3),lr_get32(p+7),lr_get32(p+11),lr_get32(p+15),lr_get32(p+19),lr_get32(p+23),lr_get32(p+27)]];
        if(self.oneWayActive){
            if(lr_get32(p+7)!=lr_get32(p+15) || lr_get32(p+19) || lr_get32(p+23)){self.oneWayActive=NO;[self record:@"ONEWAY STOP: missing/corrupt/duplicate delivery."];}
            else {self.oneWayIndex++;if(!(self.oneWayIndex%2))self.paramRequested=NO;dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{[self runOneWay];});}
        }return YES;
    case RL_BENCH_DONE:
        if(n!=43)break;
        if(self.sweepActive){
            if(lr_get32(p+11)!=lr_get32(p+15) || lr_get32(p+23)){self.sweepActive=NO;[self record:@"SWEEP STOPPED: missing/corrupt echoes; reconnect before more trials."];}
            else {self.sweepIndex++;self.sweepWaits=0;dispatch_after(dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC),dispatch_get_main_queue(),^{[self runSweep];});}
        }
        [self record:[NSString stringWithFormat:@"BENCH rate=%u/s planned=%u enqueued=%u echoed=%u drops=%u corrupt=%u RTT mean=%u ms p95=%u ms max=%u ms mode=%@",lr_get32(p+3),lr_get32(p+7),lr_get32(p+11),lr_get32(p+15),lr_get32(p+19),lr_get32(p+23),lr_get32(p+27),lr_get32(p+31),lr_get32(p+35),lr_get32(p+39)?@"stream-v2":@"legacy"]];
        unsigned minutes=[[NSBundle.mainBundle objectForInfoDictionaryKey:@"LDNRelaySoakMinutes"] unsignedIntValue];
        if(minutes && !self.qualificationStopped){
            if(lr_get32(p+7)!=lr_get32(p+15) || lr_get32(p+19) || lr_get32(p+23)){self.qualificationStopped=YES;[self record:@"QUALIFICATION STOP: load loss/corruption; no soak pass."];return YES;}
            self.soakRounds++;[self record:[NSString stringWithFormat:@"QUALIFICATION load minute %lu/%u delivered all offered packets",(unsigned long)self.soakRounds,minutes]];
            if(self.soakRounds<minutes){unsigned rate=lr_get32(p+3);dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),^{uint8_t b[4];lr_put16(b,rate);lr_put16(b+2,60);[self send:RL_BENCH body:[NSData dataWithBytes:b length:4]];});}
            else [self record:@"QUALIFICATION LOAD COMPLETE (synthetic echo; not LDN/game or lifecycle coverage)."];
        }return YES;
    case RL_COUNTERS:
        if (n!=45) break;
        [self record:[NSString stringWithFormat:@"Switch counters: joined=%u queue=%u rx=%llu tx=%llu dropped=%llu bytes_rx=%llu bytes_tx=%llu",p[3],p[4],(unsigned long long)lr_get64(p+5),(unsigned long long)lr_get64(p+13),(unsigned long long)lr_get64(p+21),(unsigned long long)lr_get64(p+29),(unsigned long long)lr_get64(p+37)]];return YES;
    case RL_ERROR:
        self.sweepActive=NO;
        if (n!=9) break;
        self.status.text=@"Relay reported an error · see log";
        [self record:[NSString stringWithFormat:@"ERROR command=%u category=%u detail=0x%08x request=%u",p[3],p[4],lr_get32(p+5),request]];
        if (request==self.udpBindRequest)self.udpExpected=nil;
        if (request==self.sessionRequest)self.sessionBusy=NO;
        [self updateControls];
        return YES;
    default: break;
    }
    [self record:[NSString stringWithFormat:@"Rejected malformed/unsupported relay message: opcode=%u bytes=%lu",p[0],(unsigned long)n]];
    return YES;
}

@end

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation SceneDelegate
- (void)sceneDidBecomeActive:(UIScene *)scene { [(RelayController *)self.window.rootViewController record:@"APP STATE active foreground"]; }
- (void)sceneWillResignActive:(UIScene *)scene { [(RelayController *)self.window.rootViewController record:@"APP STATE inactive"]; }
- (void)sceneDidEnterBackground:(UIScene *)scene { [(RelayController *)self.window.rootViewController record:@"APP STATE background: performance run is not a foreground benchmark"]; }
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    self.window=[[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];self.window.rootViewController=[RelayController new];[self.window makeKeyAndVisible];
}
@end
@interface AppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation AppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options { return YES; }
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    UISceneConfiguration *config=[[UISceneConfiguration alloc] initWithName:@"Relay" sessionRole:session.role];config.delegateClass=SceneDelegate.class;return config;
}
@end
int main(int argc,char *argv[]) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(AppDelegate.class)); } }
