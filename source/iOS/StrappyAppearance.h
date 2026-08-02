#import <UIKit/UIKit.h>

@interface StrappyAppearance : NSObject

+ (UIColor *)primaryTintColor;
+ (UIColor *)highlightedPrimaryTintColor;
+ (UIColor *)legacyBarTintColor;
+ (UIColor *)modernBarBackgroundColor;
+ (void)applyApplicationTintToWindow:(UIWindow *)window;
+ (void)applyBarAppearanceToNavigationController:
  (UINavigationController *)navigationController;
+ (void)applyBarAppearanceToSearchBar:(UISearchBar *)searchBar;
+ (void)applyPrimaryTintToBarButtonItem:(UIBarButtonItem *)barButtonItem;

@end
