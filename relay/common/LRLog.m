/* MIT licence. */
#import "LRLog.h"
@implementation LRLog {
    NSURL *url;NSLock *lock;NSMutableData *buffer;NSFileHandle *file;
    dispatch_queue_t queue;dispatch_source_t timer;NSUInteger dropped;
}
- (instancetype)initWithURL:(NSURL *)path {
    if((self=[super init])){url=path;lock=[NSLock new];buffer=[NSMutableData new];queue=dispatch_queue_create("dev.ldn-relay.log",DISPATCH_QUEUE_SERIAL);
        timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,queue);dispatch_source_set_timer(timer,dispatch_time(DISPATCH_TIME_NOW,0),200000000,20000000);
        __weak LRLog *weak=self;dispatch_source_set_event_handler(timer,^{[weak flush];});dispatch_resume(timer);
    }return self;
}
- (void)append:(NSString *)line {
    NSData *data=[line dataUsingEncoding:NSUTF8StringEncoding];[lock lock];
    if(buffer.length+data.length<=1024*1024)[buffer appendData:data];else dropped++;
    [lock unlock];
}
- (void)flush {
    [lock lock];NSData *data=[buffer copy];[buffer setLength:0];NSUInteger lost=dropped;dropped=0;[lock unlock];
    if(!data.length && !lost)return;
    if(!file){if(![NSFileManager.defaultManager fileExistsAtPath:url.path])[NSData.data writeToURL:url atomically:YES];file=[NSFileHandle fileHandleForWritingToURL:url error:nil];[file seekToEndOfFile];}
    @try{[file writeData:data];if(lost)[file writeData:[[NSString stringWithFormat:@"LOGGER: dropped %lu lines at buffer limit\n",(unsigned long)lost] dataUsingEncoding:NSUTF8StringEncoding]];}@catch(NSException *e){NSLog(@"Log write failed: %@",e.reason);file=nil;}
}
- (void)dealloc {if(timer)dispatch_source_cancel(timer);}
@end
