#import "XPAppKit.h"

@class SessionWindowController;
@class PreferencesWindowController;

@interface AppDelegate : NSObject <XPApplicationDelegate> {
 @private
  SessionWindowController *_windowController;
  PreferencesWindowController *_preferencesWindowController;
}

- (void)showAboutWindow:(id)sender;
- (void)showPreferencesWindow:(id)sender;

@end
