#import "StrappyPreferencesWhitelistView.h"

@interface StrappyPreferencesDatabaseWhitelistView : StrappyPreferencesWhitelistView {
 @private
  NSButton *fullScanButton_;
  NSButton *showHiddenDatabasesButton_;
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
         dataSource:(id)dataSource
           delegate:(id)delegate;
- (NSButton *)scanButton;
- (NSButton *)fullScanButton;
- (NSButton *)showHiddenDatabasesButton;

@end
