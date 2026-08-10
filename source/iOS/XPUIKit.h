#import <UIKit/UIKit.h>

@interface NSString (XPUIKit)
- (CGSize)XP_sizeWithFont:(UIFont *)font
        constrainedToSize:(CGSize)size;
@end

@interface UIColor (XPUIKit)
+ (UIColor *)messagesBackgroundColor;
@end

@interface UIView (XPUIKit)
+ (void)XP_setAppearanceBarTintColorIfAvailable:(UIColor *)barTintColor;
+ (void)XP_setAppearanceTintColorIfAvailable:(UIColor *)tintColor;
- (void)XP_setBackgroundTransparent;
- (void)XP_setTintColorIfAvailable:(UIColor *)tintColor;
@end

@interface UIBarButtonItem (XPUIKit)
+ (void)XP_setAppearanceTintColorIfAvailable:(UIColor *)tintColor;
- (void)XP_setTintColorIfAvailable:(UIColor *)tintColor;
@end

@interface UISwitch (XPUIKit)
+ (void)XP_setAppearanceOnTintColorIfAvailable:(UIColor *)onTintColor;
@end

@interface UITableView (XPUIKit)
+ (void)XP_setSectionHeaderFooterAppearanceTintColorIfAvailable:
  (UIColor *)tintColor;
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
- (void)XP_presentViewController:(UIViewController *)viewController
                         animated:(BOOL)animated;
- (void)XP_dismissViewControllerAnimated:(BOOL)animated;
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

/* Instant local notifications backed by UILocalNotification. iOS 8+ requires
 * user-notification authorization; older supported releases have no permission
 * prompt and are treated as authorized. The implementation runtime-dispatches
 * the iOS 8 APIs so Strappy keeps its iOS 4.3 deployment floor. */
typedef enum {
  XPNotificationAuthStatusNotDetermined = 0,
  XPNotificationAuthStatusDenied        = 1,
  XPNotificationAuthStatusAuthorized    = 2
} XPNotificationAuthStatus;

@interface XPUserNotificationCenter : NSObject
+ (XPUserNotificationCenter *)defaultCenter;
- (void)requestAuthorization;
- (XPNotificationAuthStatus)authorizationStatus;
- (void)postNotificationWithTitle:(NSString *)title body:(NSString *)body;
@end
