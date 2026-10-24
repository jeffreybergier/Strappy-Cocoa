#import "XPAppKit.h"
#import "StrappyStartupController.h"

@class SessionWindowController;
@class PreferencesWindowController;

@interface AppDelegate : NSObject <XPApplicationDelegate, XPMenuDelegate,
    StrappyStartupDelegate> {
 @private
  SessionWindowController *_windowController;
  PreferencesWindowController *_preferencesWindowController;
  NSMutableDictionary *_inFlightSessions;
  NSMenu *_modelMenu;
  BOOL _terminateWhenInFlightSessionsFinish;
  StrappyStartupController *startupController_;
  BOOL startupFinished_;
}

- (void)showAboutWindow:(id)sender;
- (void)showPreferencesWindow:(id)sender;
- (void)showMainWindow:(id)sender;

@end
