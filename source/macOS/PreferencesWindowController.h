#import <AppKit/AppKit.h>
#import "XPAppKit.h"

@class StrappyPreferencesAuthenticationView;
@class StrappyPreferencesDatabaseWhitelistView;
@class StrappyPreferencesDatabaseStudyView;
@class StrappyPreferencesModelWhitelistView;
@class StrappyPreferencesSystemPromptsView;
@class StrappySessionOptionsViewController;

@interface PreferencesWindowController : NSWindowController
    <XPTableViewDataSource, XPTableViewDelegate, XPToolbarDelegate> {
 @private
  NSView              *contentPaneView_;
  StrappyPreferencesAuthenticationView *authenticationPaneView_;
  StrappySessionOptionsViewController *sessionDefaultsController_;
  NSTextField         *apiEndpointField_;
  NSSecureTextField   *apiTokenField_;
  NSTextField         *apiTokenStatusLabel_;
  NSSearchField       *modelSearchField_;
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
  NSSearchField       *databaseStudySearchField_;
  NSTableView         *databaseStudyTableView_;
  NSButton            *databaseStudyActionButton_;
  NSTextField         *databaseStudyStatusLabel_;
  NSButton            *scanButton_;
  NSButton            *showHiddenDatabasesButton_;
  NSProgressIndicator *scanProgressIndicator_;
  NSTextField         *databaseStatusLabel_;
  NSArray             *allModelRows_;
  NSArray             *modelRows_;
  NSArray             *allDatabaseRows_;
  NSArray             *databaseRows_;
  NSArray             *allDatabaseStudyRows_;
  NSArray             *databaseStudyRows_;
  NSDateFormatter      *databaseStudyDateFormatter_;
  NSString            *expandedDatabaseStudyIdentifier_;
  BOOL                 scanning_;
  BOOL                 refreshingModels_;
}

- (id)init;
- (void)scanDatabases:(id)sender;
- (void)refreshModels:(id)sender;

@end
