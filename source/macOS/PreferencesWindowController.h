#import <AppKit/AppKit.h>
#import "XPAppKit.h"

@class StrappyPreferencesAuthenticationView;
@class StrappyPreferencesDatabaseWhitelistView;
@class StrappyPreferencesDatabaseStudyView;
@class StrappyPreferencesModelWhitelistView;
@class StrappyPreferencesSystemPromptsView;

@interface PreferencesWindowController : NSWindowController
    <XPTableViewDataSource, XPTableViewDelegate, XPToolbarDelegate> {
 @private
  NSView              *contentPaneView_;
  StrappyPreferencesAuthenticationView *authenticationPaneView_;
  NSTextField         *apiEndpointField_;
  NSSecureTextField   *apiTokenField_;
  NSTextField         *apiTokenStatusLabel_;
  NSSearchField       *modelSearchField_;
  NSPopUpButton       *defaultModelPopUpButton_;
  NSTableView         *modelTableView_;
  StrappyPreferencesModelWhitelistView *modelWhitelistView_;
  NSButton            *fetchModelsButton_;
  NSProgressIndicator *modelProgressIndicator_;
  NSTextField         *modelStatusLabel_;
  StrappyPreferencesSystemPromptsView *systemPromptsPaneView_;
  NSTextView          *systemPromptTextView_;
  NSSearchField       *databaseSearchField_;
  NSTableView         *databaseTableView_;
  StrappyPreferencesDatabaseWhitelistView *databaseWhitelistView_;
  StrappyPreferencesDatabaseStudyView *databaseStudyPaneView_;
  NSTextView          *databaseStudyTextView_;
  NSButton            *scanButton_;
  NSProgressIndicator *scanProgressIndicator_;
  NSTextField         *databaseStatusLabel_;
  NSArray             *allModelRows_;
  NSArray             *modelRows_;
  NSArray             *allDatabaseRows_;
  NSArray             *databaseRows_;
  BOOL                 scanning_;
  BOOL                 refreshingModels_;
}

- (id)init;
- (void)scanDatabases:(id)sender;
- (void)refreshModels:(id)sender;

@end
