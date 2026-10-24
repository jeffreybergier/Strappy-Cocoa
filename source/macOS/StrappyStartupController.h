#import "XPAppKit.h"

@class StrappyStartupController;
@protocol StrappyStartupDelegate
- (void)startupDidFinish:(StrappyStartupController *)controller;
@end

/* Main-thread-only startup alert. The delegate is borrowed and must invalidate
 * this controller before teardown. Start once; failure remains visible until
 * Quit. Invalidation cancels timers and ignores any late installer completion. */
@interface StrappyStartupController : NSObject {
 @private
  id<StrappyStartupDelegate> delegate_;
  NSAlert *alert_;
  NSProgressIndicator *spinner_;
  NSButton *quitButton_;
  NSTimer *pollTimer_;
  NSTimer *timeoutTimer_;
  BOOL started_;
  BOOL prepared_;
  BOOL failed_;
  BOOL invalidated_;
}
- (id)initWithDelegate:(id<StrappyStartupDelegate>)delegate;
- (void)start;
- (void)showWindow;
- (void)invalidate;
@end
