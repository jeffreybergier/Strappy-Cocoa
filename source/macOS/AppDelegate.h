#import "XPAppKit.h"

@class SessionWindowController;

@interface AppDelegate : NSObject <XPApplicationDelegate> {
 @private
  SessionWindowController *_windowController;
}

- (void)showAboutWindow:(id)sender;

@end
