#import "StrappyPreferencesWhitelistView.h"

@interface StrappyPreferencesModelWhitelistView : StrappyPreferencesWhitelistView {
 @private
  NSButton            *fetchButton_;
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
         dataSource:(id)dataSource
           delegate:(id)delegate;
- (NSButton *)fetchButton;

@end
