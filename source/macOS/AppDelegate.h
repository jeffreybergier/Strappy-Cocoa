#import "XPAppKit.h"

@class SessionWindowController;
@class PreferencesWindowController;

@interface AppDelegate : NSObject <XPApplicationDelegate, XPMenuDelegate> {
 @private
  SessionWindowController *_windowController;
  PreferencesWindowController *_preferencesWindowController;
  NSMutableDictionary *_inFlightSessions;
  NSMenu *_modelMenu;
  BOOL _terminateWhenInFlightSessionsFinish;
}

- (void)showAboutWindow:(id)sender;
- (void)showPreferencesWindow:(id)sender;
- (void)showMainWindow:(id)sender;

@end
