#import "StrappyPreferencesWhitelistView.h"

@interface StrappyPreferencesDatabaseWhitelistView : StrappyPreferencesWhitelistView {
 @private
  NSButton *scanButton_;
  NSButton *showHiddenDatabasesButton_;
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
         dataSource:(id)dataSource
           delegate:(id)delegate;
- (NSButton *)scanButton;
- (NSButton *)showHiddenDatabasesButton;

@end
