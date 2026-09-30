#import <UIKit/UIKit.h>
#import <CoreBluetooth/CoreBluetooth.h>
#import <TargetConditionals.h>
#include "relay_protocol.h"
#include "relay_batch.h"

static NSString *const ServiceUUID = @"7B61238D-028A-4F65-99D8-7C8F22A11D4E";
static NSString *const DataUUID = @"7B61238D-028A-4F65-99D8-7C8F22A11D4F";
// Public FRLG profile from Decryptu/pokeldn, pinned in relay/README.md.
// Profiles belong to the phone. The Switch executable contains no game keys.
static NSString *const FRLGKey = @"fcb6f6adb9dfea66aca9c326149d2b3b08a781895cbf78f720d78b85a57584a99665d237797b2a41ddef14063ec28d259143af7832fb3cbcf2759cbfbdc81d8c";

@interface RelayController : UIViewController <CBPeripheralManagerDelegate> {
    LrCodec codec;
}
@property(nonatomic, strong) CBPeripheralManager *manager;
@property(nonatomic, strong) CBMutableCharacteristic *dataCharacteristic;
@property(nonatomic, strong) NSData *readSnapshot;
@property(nonatomic, strong) NSUUID *centralID;
@property(nonatomic, strong) UILabel *status;
@property(nonatomic, strong) UILabel *traffic;
@property(nonatomic, strong) UITextView *events;
@property(nonatomic, strong) UITextField *keyField;
@property(nonatomic, strong) UITextField *portField;
@property(nonatomic, strong) UISegmentedControl *protocolControl;
@property(nonatomic, strong) UIStackView *sessionList;
@property(nonatomic, strong) UIButton *scanButton;
@property(nonatomic, strong) UIButton *leaveButton;
@property(nonatomic, strong) UIButton *pingButton;
@property(nonatomic, strong) UIButton *udpButton;
@property(nonatomic, strong) UIButton *statsButton;
@property(nonatomic, strong) UIButton *presetButton;
@property(nonatomic, strong) NSURL *logURL;
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
    if (![[NSFileManager defaultManager] fileExistsAtPath:self.logURL.path]) [[NSData data] writeToURL:self.logURL atomically:YES];
    NSFileHandle *file=[NSFileHandle fileHandleForWritingToURL:self.logURL error:nil];
    [file seekToEndOfFile];[file writeData:[line dataUsingEncoding:NSUTF8StringEncoding]];[file closeFile];
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
    instructions.text=@"Open LDN Relay through Album on your modified Switch, then press A. Keep this app open. Sessions appear automatically after Bluetooth connects; tap your session to join.\n\nTransport preview · game adapters are not installed yet.";
    self.protocolControl=[[UISegmentedControl alloc] initWithItems:@[@"Protocol 1",@"Protocol 3"]];self.protocolControl.selectedSegmentIndex=1;
    self.keyField=[UITextField new];self.keyField.borderStyle=UITextBorderStyleRoundedRect;self.keyField.placeholder=@"Session passphrase (32–128 hex digits)";
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
    UIStackView *stack=[[UIStackView alloc] initWithArrangedSubviews:@[title,self.status,instructions,self.presetButton,self.protocolControl,self.keyField,self.portField,actions,self.sessionList,self.traffic,tests,self.statsButton,self.events]];
    stack.axis=UILayoutConstraintAxisVertical;stack.spacing=10;stack.translatesAutoresizingMaskIntoConstraints=NO;
    UIScrollView *scroll=[UIScrollView new];scroll.translatesAutoresizingMaskIntoConstraints=NO;scroll.keyboardDismissMode=UIScrollViewKeyboardDismissModeOnDrag;
    [self.view addSubview:scroll];[scroll addSubview:stack];UILayoutGuide *safe=self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[[scroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],[scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],[scroll.topAnchor constraintEqualToAnchor:safe.topAnchor],[scroll.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor],[stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:20],[stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-20],[stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:12],[stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-20],[stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-40],[self.events.heightAnchor constraintEqualToConstant:200]]];
    [self loadFRLG];self.nextRequest=1;lr_init(&codec,20);
    [self updateControls];
    [self record:[NSString stringWithFormat:@"%@ %@ started. Transport only; gameplay unverified.", [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleDisplayName"], [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"]]];
    self.manager=[[CBPeripheralManager alloc] initWithDelegate:self queue:dispatch_get_main_queue() options:@{CBPeripheralManagerOptionShowPowerAlertKey:@YES}];
    self.timer=[NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(tick) userInfo:nil repeats:YES];
    UIApplication.sharedApplication.idleTimerDisabled=YES;
}
- (void)loadFRLG { self.protocolControl.selectedSegmentIndex=1;self.keyField.text=FRLGKey;self.portField.text=@"12345"; }
- (void)resetLink {
    self.ready=NO;self.joined=NO;self.nonce=0;self.centralID=nil;self.handshakeReply=NO;
    self.networkInfo=nil;self.pingExpected=nil;self.udpExpected=nil;self.pingRequest=0;self.udpBindRequest=0;
    self.udpReceived=0;self.udpBytes=0;lr_init(&codec,20);[self clearSessions];
    self.sessionBusy=NO;self.udpReady=NO;self.sessionRequest=0;self.traffic.text=@"No session joined";[self updateControls];
}
- (uint16_t)send:(uint8_t)opcode body:(NSData *)body {
    if (!self.ready) { [self record:@"Connect the Switch first."];return 0; }
    uint16_t request=self.nextRequest++;if (!self.nextRequest)self.nextRequest=1;
    uint8_t message[LR_MAX_MESSAGE]={opcode};lr_put16(message+1,request);
    if (body.length>LR_MAX_MESSAGE-3) return 0;
    if (body.length)memcpy(message+3,body.bytes,body.length);
    if (!lr_enqueue(&codec,message,3+body.length)) { [self record:@"Command queue full; wait for delivery."];return 0; }
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
- (void)testPing {
    if (self.pingExpected) { [self record:@"BLE test is already pending."];return; }
    NSMutableData *payload=[NSMutableData dataWithLength:1024];arc4random_buf(payload.mutableBytes,payload.length);
    uint16_t request=[self send:RL_PING body:payload];
    if (request) { self.pingExpected=payload;self.pingRequest=request;self.pingStarted=NSDate.timeIntervalSinceReferenceDate;[self record:@"BLE test: queued 1024 bytes for exact echo."];[self updateControls]; }
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
        self.udpStarted=NSDate.timeIntervalSinceReferenceDate;[self record:@"UDP loopback: binding a test socket on the relay's own LDN address."];
        [self updateControls];
    }
}
- (void)tick {
    NSTimeInterval now=NSDate.timeIntervalSinceReferenceDate;
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
    case RL_CAPS:
        if (n!=9 || p[3]!=LR_VERSION) break;
        if(p[8]&RL_FEATURE_BATCH) {uint8_t flags=RL_FEATURE_BATCH;[self send:RL_CONFIG body:[NSData dataWithBytes:&flags length:1]];}
        self.ready=YES;self.status.text=@"Bluetooth connected · scan for a session";
        [self record:[NSString stringWithFormat:@"Relay ready: frame=%u, UDP limit=%u, sockets=%u",codec.frame_limit,lr_get16(p+5),p[7]]];[self scan];return YES;
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
        self.joined=NO;self.sessionBusy=NO;self.udpReady=NO;self.networkInfo=nil;self.udpExpected=nil;self.status.text=@"Session left · Bluetooth remains connected";
        self.traffic.text=@"No session joined";[self record:@"LDN session closed."];[self updateControls];return YES;
    case RL_BOUND:
        if (n!=6) break;
        [self record:[NSString stringWithFormat:@"UDP bound: slot=%u port=%u",p[3],(p[4]<<8)|p[5]]];
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
            [self record:[NSString stringWithFormat:@"UDP loopback %@: 512 bytes, %.1f ms. Local relay socket only; not a stock-console response.",exact?@"PASS":@"FAIL",(NSDate.timeIntervalSinceReferenceDate-self.udpStarted)*1000]];
            self.udpExpected=nil;[self updateControls];
        } else if (self.udpReceived<=5 || self.udpReceived%100==0) [self record:[NSString stringWithFormat:@"UDP received: slot=%u bytes=%lu total=%lu",p[3],(unsigned long)data.length,(unsigned long)self.udpReceived]];
        // Game adapters subscribe here; payloads remain opaque to the relay.
        [[NSNotificationCenter defaultCenter] postNotificationName:@"LDNRelayDatagram" object:self userInfo:@{@"slot":@(p[3]),@"source":[NSData dataWithBytes:p+4 length:4],@"port":@((p[8]<<8)|p[9]),@"payload":data}];
        return YES;
    }
    case RL_PONG:
        if (self.pingExpected && request==self.pingRequest) {
            NSData *body=[NSData dataWithBytes:p+3 length:n-3];BOOL exact=[body isEqualToData:self.pingExpected];
            [self record:[NSString stringWithFormat:@"BLE echo %@: 1024 bytes, %.1f ms",exact?@"PASS":@"FAIL",(NSDate.timeIntervalSinceReferenceDate-self.pingStarted)*1000]];self.pingExpected=nil;[self updateControls];
        }
        return YES;
    case RL_COUNTERS:
        if (n!=45) break;
        [self record:[NSString stringWithFormat:@"Switch counters: joined=%u queue=%u rx=%llu tx=%llu dropped=%llu bytes_rx=%llu bytes_tx=%llu",p[3],p[4],(unsigned long long)lr_get64(p+5),(unsigned long long)lr_get64(p+13),(unsigned long long)lr_get64(p+21),(unsigned long long)lr_get64(p+29),(unsigned long long)lr_get64(p+37)]];return YES;
    case RL_ERROR:
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
- (void)peripheralManagerDidUpdateState:(CBPeripheralManager *)peripheral {
    [self resetLink];self.serviceAdded=NO;
    if (peripheral.state!=CBManagerStatePoweredOn) { self.status.text=@"Bluetooth unavailable · check permission and Bluetooth settings";return; }
    [peripheral removeAllServices];
    self.dataCharacteristic=[[CBMutableCharacteristic alloc] initWithType:[CBUUID UUIDWithString:DataUUID] properties:CBCharacteristicPropertyRead|CBCharacteristicPropertyWrite value:nil permissions:CBAttributePermissionsReadable|CBAttributePermissionsWriteable];
    CBMutableService *service=[[CBMutableService alloc] initWithType:[CBUUID UUIDWithString:ServiceUUID] primary:YES];service.characteristics=@[self.dataCharacteristic];[peripheral addService:service];
}
- (void)peripheralManager:(CBPeripheralManager *)peripheral didAddService:(CBService *)service error:(NSError *)error {
    if (error) { [self record:error.localizedDescription];return; }
    self.serviceAdded=YES;
    [peripheral startAdvertising:@{CBAdvertisementDataServiceUUIDsKey:@[[CBUUID UUIDWithString:ServiceUUID]],CBAdvertisementDataLocalNameKey:@"LDNRelay"}];
}
- (void)peripheralManagerDidStartAdvertising:(CBPeripheralManager *)peripheral error:(NSError *)error {
    self.status.text=error?@"Bluetooth advertising failed":@"Waiting for the Switch · launch through Album and press A";
    [self record:error?error.localizedDescription:@"Relay Bluetooth service advertising."];
}
- (void)peripheralManager:(CBPeripheralManager *)peripheral didReceiveWriteRequests:(NSArray<CBATTRequest *> *)requests {
    if (!requests.count) return;
    // Writes use response mode, one complete frame per ATT request.
    if (requests.count!=1) { [peripheral respondToRequest:requests.firstObject withResult:CBATTErrorRequestNotSupported];return; }
    CBATTRequest *r=requests.firstObject;NSData *value=r.value;const uint8_t *p=value.bytes;
    if (![r.characteristic.UUID isEqual:self.dataCharacteristic.UUID] || r.offset || value.length>LR_MAX_FRAME) { [peripheral respondToRequest:r withResult:CBATTErrorInvalidAttributeValueLength];return; }
    BOOL hello=value.length==12 && !memcmp(p,"LRH1",4);
    if (self.centralID && ![self.centralID isEqual:r.central.identifier] && NSDate.timeIntervalSinceReferenceDate-self.lastContact<30) { [peripheral respondToRequest:r withResult:CBATTErrorInsufficientAuthorization];return; }
    if (hello) {
        uint32_t nonce=lr_get32(p+4);uint16_t limit=lr_get16(p+8);
        if (!nonce || lr_get16(p+10)!=LR_VERSION || limit<20 || limit>LR_MAX_FRAME) { [peripheral respondToRequest:r withResult:CBATTErrorInvalidAttributeValueLength];return; }
        if (nonce!=self.nonce || ![self.centralID isEqual:r.central.identifier]) {
            [self resetLink];self.nonce=nonce;self.centralID=r.central.identifier;lr_init(&codec,limit);
            [self record:[NSString stringWithFormat:@"Relay handshake: frame limit=%u",limit]];
        } else if (codec.frame_limit!=limit) { [peripheral respondToRequest:r withResult:CBATTErrorInvalidAttributeValueLength];return; }
        uint8_t response[12]={'L','R','A','1'};lr_put32(response+4,nonce);lr_put16(response+8,codec.frame_limit);lr_put16(response+10,LR_VERSION);
        self.readSnapshot=[NSData dataWithBytes:response length:12];self.handshakeReply=YES;
    } else {
        if (!self.nonce || ![self.centralID isEqual:r.central.identifier] || value.length<LR_HEADER_SIZE || value.length>codec.frame_limit) { [peripheral respondToRequest:r withResult:CBATTErrorInvalidAttributeValueLength];return; }
        // CRC/sequence failures are retransmitted by the codec, not delivered.
        lr_ingest(&codec,p,value.length,receive_message,(__bridge void *)self);self.handshakeReply=NO;
    }
    self.lastContact=NSDate.timeIntervalSinceReferenceDate;
    [peripheral respondToRequest:r withResult:CBATTErrorSuccess];
}
- (void)peripheralManager:(CBPeripheralManager *)peripheral didReceiveReadRequest:(CBATTRequest *)request {
    if (![request.characteristic.UUID isEqual:self.dataCharacteristic.UUID] || !self.nonce || ![self.centralID isEqual:request.central.identifier]) { [peripheral respondToRequest:request withResult:CBATTErrorInsufficientAuthorization];return; }
    if (!request.offset && !self.handshakeReply) {
        uint8_t frame[LR_MAX_FRAME];size_t n=lr_frame(&codec,frame,sizeof(frame));self.readSnapshot=[NSData dataWithBytes:frame length:n];
    }
    if (request.offset>self.readSnapshot.length) { [peripheral respondToRequest:request withResult:CBATTErrorInvalidOffset];return; }
    request.value=[self.readSnapshot subdataWithRange:NSMakeRange(request.offset,self.readSnapshot.length-request.offset)];
    self.lastContact=NSDate.timeIntervalSinceReferenceDate;[peripheral respondToRequest:request withResult:CBATTErrorSuccess];
}
@end

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation SceneDelegate
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
