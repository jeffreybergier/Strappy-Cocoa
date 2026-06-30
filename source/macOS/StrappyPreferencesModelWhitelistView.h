#import "StrappyPreferencesWhitelistView.h"

@interface StrappyPreferencesModelWhitelistView : StrappyPreferencesWhitelistView {
 @private
  NSPopUpButton       *defaultModelPopUpButton_;
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
         dataSource:(id)dataSource
           delegate:(id)delegate;
- (NSPopUpButton *)defaultModelPopUpButton;
- (NSButton *)fetchButton;

@end
