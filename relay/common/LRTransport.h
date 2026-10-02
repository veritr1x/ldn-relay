/* MIT licence. CoreBluetooth owns a dedicated serial queue. UI callbacks use main. */
#import <Foundation/Foundation.h>
@interface LRTransport : NSObject
@property(nonatomic,copy) void (^messageHandler)(NSData *);
@property(nonatomic,copy) void (^statusHandler)(NSString *);
@property(nonatomic,copy) void (^resetHandler)(void);
@property(nonatomic,readonly) NSUInteger queuedMessages;
@property(nonatomic,readonly) uint16_t frameLimit;
- (BOOL)configureBenchmarkFrame:(unsigned)frame window:(unsigned)window;
- (void)start;
- (BOOL)enqueue:(NSData *)message;
@end
