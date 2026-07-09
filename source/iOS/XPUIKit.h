#import <UIKit/UIKit.h>

@interface UIColor (XPUIKit)
+ (UIColor *)messagesBackgroundColor;
@end

@interface UIView (XPUIKit)
- (void)XP_setBackgroundTransparent;
- (void)XP_removeShadow;
@end

@interface UIScrollView (XPUIKit)
- (void)XP_setKeyboardDismissModeOnDrag;
@end

@interface UISearchBar (XPUIKit)
- (void)XP_enableSearchReturnKeyWhenEmpty;
@end

@interface UIWebView (XPUIKit)
- (UIScrollView *)XP_scrollView;
@end

@interface UIViewController (XPUIKit)
- (BOOL)XP_isMovingFromParentViewController;
@end

@interface UILabel (XPUIKit)
- (void)XP_setTextAlignmentCenter;
@end
