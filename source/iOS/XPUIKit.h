#import <UIKit/UIKit.h>

@interface NSString (XPUIKit)
- (CGSize)XP_sizeWithFont:(UIFont *)font
        constrainedToSize:(CGSize)size;
@end

@interface UIColor (XPUIKit)
+ (UIColor *)messagesBackgroundColor;
@end

@interface UIView (XPUIKit)
- (void)XP_setBackgroundTransparent;
@end

@interface UIScrollView (XPUIKit)
- (void)XP_setKeyboardDismissModeOnDrag;
@end

@interface UISearchBar (XPUIKit)
- (void)XP_enableSearchReturnKeyWhenEmpty;
@end

@interface UIWebView (XPUIKit)
- (UIScrollView *)XP_scrollView;
- (void)XP_setVisibleFrame:(CGRect)frame;
@end

@interface UIViewController (XPUIKit)
- (BOOL)XP_isMovingFromParentViewController;
@end

@interface UIDevice (XPUIKit)
- (BOOL)XP_isOperatingSystemAtLeastMajorVersion:(NSInteger)majorVersion;
@end

@interface UILabel (XPUIKit)
- (void)XP_setTextAlignmentCenter;
- (void)XP_setLineBreakModeWordWrapping;
@end

@interface UITextField (XPUIKit)
- (void)XP_setTextAlignmentRight;
@end
