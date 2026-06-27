#import <AppKit/AppKit.h>
#import "XPAppKit.h"

@interface PreferencesWindowController : NSWindowController
    <XPTableViewDataSource, XPTableViewDelegate> {
 @private
  NSTabView           *tabView_;
  NSTextField         *apiEndpointField_;
  NSSecureTextField   *apiTokenField_;
  NSTextField         *apiTokenStatusLabel_;
  NSSearchField       *modelSearchField_;
  NSTableView         *modelTableView_;
  NSButton            *fetchModelsButton_;
  NSProgressIndicator *modelProgressIndicator_;
  NSTextField         *modelStatusLabel_;
  NSTextView          *systemPromptTextView_;
  NSTableView         *databaseTableView_;
  NSButton            *scanButton_;
  NSProgressIndicator *scanProgressIndicator_;
  NSArray             *modelRows_;
  NSArray             *databaseRows_;
  NSString            *pendingOpenRouterModelIdentifier_;
  BOOL                 scanning_;
  BOOL                 refreshingModels_;
  BOOL                 suppressingModelSelectionChange_;
}

- (id)init;
- (void)scanDatabases:(id)sender;
- (void)refreshModels:(id)sender;

@end
