#import <AppKit/AppKit.h>
#import "XPAppKit.h"

@interface PreferencesWindowController : NSWindowController
    <XPTableViewDataSource, XPTableViewDelegate> {
 @private
  NSTabView           *tabView_;
  NSTextField         *apiEndpointField_;
  NSSecureTextField   *apiTokenField_;
  NSTextField         *apiTokenStatusLabel_;
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
  BOOL                 scanning_;
  BOOL                 refreshingModels_;
}

- (id)init;
- (void)scanDatabases:(id)sender;
- (void)refreshModels:(id)sender;

@end
