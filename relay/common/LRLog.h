/* MIT. Bounded buffered logging, file operations on a utility queue. */
#import <Foundation/Foundation.h>
@interface LRLog : NSObject
- (instancetype)initWithURL:(NSURL *)url;
- (void)append:(NSString *)line;
@end
