#import "XPAppKit.h"

@interface AppDelegate : NSObject <XPApplicationDelegate> {
 @private
  NSWindow *_window;
}

- (void)showAboutWindow:(id)sender;

@end
