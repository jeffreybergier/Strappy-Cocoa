#import <UIKit/UIKit.h>

#import "AIFontAwesome.h"

@interface StrappyPreferencesStatusToolbarView : UIView

@property (nonatomic, copy) NSString *text;
@property (nonatomic, copy) NSString *actionAccessibilityLabel;
@property (nonatomic, assign, getter=isWorking) BOOL working;

- (instancetype)initWithActionIcon:(AIFontAwesomeIcon)actionIcon
                            target:(id)target
                            action:(SEL)action;
- (void)layoutForToolbar:(UIToolbar *)toolbar
          containingItem:(UIBarButtonItem *)toolbarItem
           fallbackWidth:(CGFloat)fallbackWidth;
- (void)refreshAppearanceForToolbar:(UIToolbar *)toolbar;

@end
