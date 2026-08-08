#import <UIKit/UIKit.h>

@interface StrappyAppearance : NSObject

+ (UIColor *)primaryTintColor;
+ (UIColor *)highlightedPrimaryTintColor;
+ (UIColor *)legacyBarTintColor;
+ (UIColor *)modernBarBackgroundColor;
+ (void)configureAppearance;
+ (void)applyIOS6TableSectionHeaderTintColor:(UIColor *)tintColor;
+ (void)applyApplicationTintToWindow:(UIWindow *)window;
+ (void)applyLegacyTintToBarButtonItem:
  (UIBarButtonItem *)barButtonItem;

@end

/* UITableViewCell does not expose selectedBackgroundView through
   UIAppearance, so Strappy supplies an appearance selector for its color. */
@interface UITableViewCell (StrappyAppearance)

+ (void)strappy_setAppearanceSelectionBackgroundColorIfAvailable:
  (UIColor *)selectionBackgroundColor;
- (void)setStrappySelectionBackgroundColor:
  (UIColor *)selectionBackgroundColor UI_APPEARANCE_SELECTOR;

@end
