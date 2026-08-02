#import "StrappyAppearance.h"

#import <QuartzCore/QuartzCore.h>

#import "XPUIKit.h"
#import "strappy_palette.h"

static const CGFloat kStrappyLegacyGroupedCellCornerRadius = 10.0f;

@interface StrappyLegacySelectionBackgroundView : UIView {
 @private
  CAShapeLayer *maskLayer_;
  UIRectCorner roundedCorners_;
}

- (void)setRoundedCorners:(UIRectCorner)roundedCorners;

@end

@implementation StrappyLegacySelectionBackgroundView

- (void)setRoundedCorners:(UIRectCorner)roundedCorners
{
  if (roundedCorners_ == roundedCorners) {
    return;
  }
  roundedCorners_ = roundedCorners;
  [self setNeedsLayout];
}

- (void)layoutSubviews
{
  UIBezierPath *path;

  [super layoutSubviews];
  if (roundedCorners_ == 0U) {
    [[self layer] setMask:nil];
    return;
  }

  if (maskLayer_ == nil) {
    maskLayer_ = [CAShapeLayer layer];
  }
  path = [UIBezierPath
    bezierPathWithRoundedRect:[self bounds]
            byRoundingCorners:roundedCorners_
                  cornerRadii:CGSizeMake(
                    kStrappyLegacyGroupedCellCornerRadius,
                    kStrappyLegacyGroupedCellCornerRadius)];
  [maskLayer_ setFrame:[self bounds]];
  [maskLayer_ setPath:[path CGPath]];
  [[self layer] setMask:maskLayer_];
}

@end

static UIRectCorner StrappyLegacySelectionRoundedCorners(
  UITableView *tableView,
  NSIndexPath *indexPath)
{
  UIRectCorner roundedCorners;
  NSInteger rowCount;

  if ((tableView == nil) || (indexPath == nil) ||
      ([tableView style] != UITableViewStyleGrouped)) {
    return 0U;
  }

  roundedCorners = 0U;
  if ([indexPath row] == 0) {
    roundedCorners |= UIRectCornerTopLeft | UIRectCornerTopRight;
  }
  rowCount = [tableView numberOfRowsInSection:[indexPath section]];
  if ((rowCount > 0) && ([indexPath row] == (rowCount - 1))) {
    roundedCorners |= UIRectCornerBottomLeft | UIRectCornerBottomRight;
  }
  return roundedCorners;
}

@implementation StrappyAppearance

+ (UIColor *)primaryTintColor
{
  return [UIColor colorWithRed:(STRAPPY_PRIMARY_TINT_RED / 255.0f)
                         green:(STRAPPY_PRIMARY_TINT_GREEN / 255.0f)
                          blue:(STRAPPY_PRIMARY_TINT_BLUE / 255.0f)
                         alpha:1.0f];
}

+ (UIColor *)highlightedPrimaryTintColor
{
  return [UIColor colorWithRed:(STRAPPY_DARK_TINT_RED / 255.0f)
                         green:(STRAPPY_DARK_TINT_GREEN / 255.0f)
                          blue:(STRAPPY_DARK_TINT_BLUE / 255.0f)
                         alpha:1.0f];
}

+ (UIColor *)legacyBarTintColor
{
  return [UIColor colorWithRed:(STRAPPY_MUTED_PURPLE_RED / 255.0f)
                         green:(STRAPPY_MUTED_PURPLE_GREEN / 255.0f)
                          blue:(STRAPPY_MUTED_PURPLE_BLUE / 255.0f)
                         alpha:1.0f];
}

+ (UIColor *)modernBarBackgroundColor
{
  return [UIColor colorWithRed:(STRAPPY_LIGHT_PURPLE_RED / 255.0f)
                         green:(STRAPPY_LIGHT_PURPLE_GREEN / 255.0f)
                          blue:(STRAPPY_LIGHT_PURPLE_BLUE / 255.0f)
                         alpha:1.0f];
}

+ (void)applyApplicationTintToWindow:(UIWindow *)window
{
  if ((window == nil) ||
      ![[UIDevice currentDevice]
        XP_isOperatingSystemAtLeastMajorVersion:7]) {
    return;
  }
  [window XP_setTintColorIfAvailable:[self primaryTintColor]];
}

+ (void)applyBarAppearanceToNavigationController:
  (UINavigationController *)navigationController
{
  UIColor *barColor;
  BOOL usesModernAppearance;

  if (navigationController == nil) {
    return;
  }

  usesModernAppearance = [[UIDevice currentDevice]
    XP_isOperatingSystemAtLeastMajorVersion:7];
  if (!usesModernAppearance) {
    barColor = [self legacyBarTintColor];
    [[navigationController navigationBar]
      XP_setTintColorIfAvailable:barColor];
    [[navigationController toolbar]
      XP_setTintColorIfAvailable:barColor];
  }
}

+ (void)applyBarAppearanceToSearchBar:(UISearchBar *)searchBar
{
  if ((searchBar == nil) ||
      [[UIDevice currentDevice]
        XP_isOperatingSystemAtLeastMajorVersion:7]) {
    return;
  }
  [searchBar XP_setTintColorIfAvailable:[self legacyBarTintColor]];
}

+ (void)applyPrimaryTintToBarButtonItem:(UIBarButtonItem *)barButtonItem
{
  if ((barButtonItem == nil) ||
      [[UIDevice currentDevice]
        XP_isOperatingSystemAtLeastMajorVersion:7]) {
    return;
  }
  [barButtonItem XP_setTintColorIfAvailable:[self primaryTintColor]];
}

+ (void)applySelectionAppearanceToTableViewCell:(UITableViewCell *)cell
                                    inTableView:(UITableView *)tableView
                                   atIndexPath:(NSIndexPath *)indexPath
{
  StrappyLegacySelectionBackgroundView *selectedBackgroundView;

  if ((cell == nil) ||
      [[UIDevice currentDevice]
        XP_isOperatingSystemAtLeastMajorVersion:7]) {
    return;
  }

  selectedBackgroundView = (StrappyLegacySelectionBackgroundView *)
    [cell selectedBackgroundView];
  if (![selectedBackgroundView
        isKindOfClass:[StrappyLegacySelectionBackgroundView class]]) {
    selectedBackgroundView =
      [[StrappyLegacySelectionBackgroundView alloc] initWithFrame:CGRectZero];
    [cell setSelectedBackgroundView:selectedBackgroundView];
  }
  [selectedBackgroundView
    setBackgroundColor:[self legacyBarTintColor]];
  [selectedBackgroundView setRoundedCorners:
    StrappyLegacySelectionRoundedCorners(tableView, indexPath)];
}

@end
