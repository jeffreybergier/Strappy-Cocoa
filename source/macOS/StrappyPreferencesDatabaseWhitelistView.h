#import "StrappyPreferencesWhitelistView.h"

@interface StrappyPreferencesDatabaseWhitelistView : StrappyPreferencesWhitelistView

- (id)initWithFrame:(NSRect)frame
             target:(id)target
         dataSource:(id)dataSource
           delegate:(id)delegate;
- (NSButton *)scanButton;

@end
