#import <Foundation/Foundation.h>

extern NSString * const StrappyAsyncCommandFailedNotification;
extern NSString * const StrappyAsyncActivityChangedNotification;

/* Results carry the actual method value/error. Short command completions run
 * inline on the main thread; background/query completions return to it later. */
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
/* Short control command: invoke and deliver completion synchronously on the
 * GUI main thread. No worker hop or run-loop pumping. Send one message to each
 * proxy and pass NULL for NSError**; inspect StrappyAsyncResult in completion.
 * The immediate object result is nil; BOOL means the invocation was handled,
 * including failures already delivered through completion/the error channel. */
+ (id)commandTarget:(id)target title:(NSString *)title
  receiver:(id)receiver completion:(SEL)completion context:(id)context;
/* Explicitly slow local work (e.g. launchctl startup), separate from control.
 * Captures arguments and queues them; completion is asynchronous. */
+ (id)backgroundTarget:(id)target title:(NSString *)title
  receiver:(id)receiver completion:(SEL)completion context:(id)context;
/* Read-only database queries remain asynchronous and latest-only per callback. */
+ (id)queryTarget:(id)target receiver:(id)receiver completion:(SEL)completion
  context:(id)context;
+ (NSUInteger)pendingCommandCount;
/* For observed terminal job failures after the acceptance command succeeded. */
+ (void)reportError:(NSError *)error title:(NSString *)title;
+ (void)postNotificationName:(NSString *)name object:(id)object;
+ (void)postNotificationName:(NSString *)name object:(id)object userInfo:(NSDictionary *)info;
@end
