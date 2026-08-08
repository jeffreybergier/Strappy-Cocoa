#import "StrappyPreferencesWhitelistView.h"

@interface StrappyPreferencesDatabaseWhitelistView : StrappyPreferencesWhitelistView {
 @private
  NSButton *fullScanButton_;
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
         dataSource:(id)dataSource
           delegate:(id)delegate;
- (NSButton *)scanButton;
- (NSButton *)fullScanButton;

@end
