#import <AppKit/AppKit.h>
#import "XPAppKit.h"

@interface PreferencesWindowController : NSWindowController
    <XPTableViewDataSource, XPTableViewDelegate> {
 @private
  NSTabView           *tabView_;
  NSTextField         *apiEndpointField_;
  NSSecureTextField   *apiTokenField_;
  NSTextField         *apiTokenStatusLabel_;
  NSTextView          *systemPromptTextView_;
  NSTableView         *databaseTableView_;
  NSButton            *scanButton_;
  NSProgressIndicator *scanProgressIndicator_;
  NSArray             *databaseRows_;
  BOOL                 scanning_;
}

- (id)init;
- (void)scanDatabases:(id)sender;

@end
