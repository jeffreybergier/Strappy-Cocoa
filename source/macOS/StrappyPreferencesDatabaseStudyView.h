#import "StrappyPreferencesWhitelistView.h"

@interface StrappyPreferencesDatabaseStudyView : StrappyPreferencesWhitelistView {
 @private
  NSButton *studyButton_;
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
         dataSource:(id)dataSource
           delegate:(id)delegate;
- (NSButton *)studyButton;
- (CGFloat)expandedRowHeightForDescription:(NSString *)description
                                    context:(NSString *)context;

@end
