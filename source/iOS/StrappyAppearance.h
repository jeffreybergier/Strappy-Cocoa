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
+ (void)applySelectionAppearanceToTableViewCell:(UITableViewCell *)cell
                                    inTableView:(UITableView *)tableView
                                   atIndexPath:(NSIndexPath *)indexPath;

@end
