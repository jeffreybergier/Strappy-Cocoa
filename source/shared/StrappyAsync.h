#import <Foundation/Foundation.h>

extern NSString * const StrappyAsyncCommandFailedNotification;
extern NSString * const StrappyAsyncActivityChangedNotification;

/* A request owns its captured arguments until execution and delivers its result
 * on the main thread. Commands are FIFO and never automatically retried. Query
 * requests are superseded by a newer query for the same receiver/selector. */
@interface StrappyAsyncResult : NSObject {
 @private
  id value_;
  NSError *error_;
  id context_;
  NSString *title_;
  BOOL errorHandled_;
}
- (id)value;
- (NSError *)error;
- (id)context;
- (BOOL)succeeded;
- (void)handleError;
@end

@interface StrappyAsync : NSObject
/* Send exactly one ordinary Objective-C message to the returned proxy. Its
 * arguments are captured, not executed on the caller. Pass NULL for NSError**.
 * Ignore the immediate object result; BOOL only means a completion was scheduled, including queue rejection. Use completion
 * whenever later UI work depends on the actual result. The receiver is retained
 * through completion; callbacks must check whether their view is still relevant.
 * Only bridge/service methods, never view/controller methods, belong here. */
+ (id)commandTarget:(id)target title:(NSString *)title
  receiver:(id)receiver completion:(SEL)completion context:(id)context;
+ (id)queryTarget:(id)target receiver:(id)receiver completion:(SEL)completion
  context:(id)context;
+ (NSUInteger)pendingCommandCount;
/* For observed terminal job failures after the acceptance command succeeded. */
+ (void)reportError:(NSError *)error title:(NSString *)title;
+ (void)postNotificationName:(NSString *)name object:(id)object;
+ (void)postNotificationName:(NSString *)name object:(id)object userInfo:(NSDictionary *)info;
@end
