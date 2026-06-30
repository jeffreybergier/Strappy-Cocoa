#import <AppKit/AppKit.h>
#import "XPAppKit.h"

@class StrappyPreferencesDatabaseWhitelistView;
@class StrappyPreferencesModelWhitelistView;

@interface PreferencesWindowController : NSWindowController
    <XPTableViewDataSource, XPTableViewDelegate> {
 @private
  NSTabView           *tabView_;
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
  NSTextView          *systemPromptTextView_;
  NSSearchField       *databaseSearchField_;
  NSTableView         *databaseTableView_;
  StrappyPreferencesDatabaseWhitelistView *databaseWhitelistView_;
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
