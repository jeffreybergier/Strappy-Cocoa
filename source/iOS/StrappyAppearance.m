#import "StrappyAppearance.h"

#import <QuartzCore/QuartzCore.h>

#import "XPUIKit.h"
#import "strappy_palette.h"

static const CGFloat kStrappyLegacyGroupedCellCornerRadius = 10.0f;

@interface StrappyLegacySelectionBackgroundView : UIView {
 @private
  CAShapeLayer *maskLayer_;
}
- (void)updateRoundedCornerMask;
@end

static UIView *StrappyAncestorViewOfClass(UIView *view, Class viewClass)
{
  UIView *ancestorView;

  ancestorView = [view superview];
  while (ancestorView != nil) {
    if ([ancestorView isKindOfClass:viewClass]) {
      return ancestorView;
    }
    ancestorView = [ancestorView superview];
  }
  return nil;
}

static UIRectCorner StrappyLegacySelectionRoundedCornersForView(
  UIView *selectionBackgroundView)
{
  UITableViewCell *cell;
  UITableView *tableView;
  NSIndexPath *indexPath;
  UIRectCorner roundedCorners;
  NSInteger rowCount;

  cell = (UITableViewCell *)StrappyAncestorViewOfClass(
    selectionBackgroundView,
    [UITableViewCell class]);
  tableView = (UITableView *)StrappyAncestorViewOfClass(
    cell,
    [UITableView class]);
  if ((cell == nil) || (tableView == nil) ||
      ([tableView style] != UITableViewStyleGrouped)) {
    return 0U;
  }

  indexPath = [tableView indexPathForCell:cell];
  if (indexPath == nil) {
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

@implementation StrappyLegacySelectionBackgroundView

- (void)setFrame:(CGRect)frame
{
  [super setFrame:frame];
  [self updateRoundedCornerMask];
}

- (void)setBounds:(CGRect)bounds
{
  [super setBounds:bounds];
  [self updateRoundedCornerMask];
}

- (void)didMoveToSuperview
{
  [super didMoveToSuperview];
  [self updateRoundedCornerMask];
}

- (void)didMoveToWindow
{
  [super didMoveToWindow];
  [self updateRoundedCornerMask];
}

- (void)layoutSubviews
{
  [super layoutSubviews];
  [self updateRoundedCornerMask];
}

- (void)updateRoundedCornerMask
{
  UIBezierPath *path;
  UIRectCorner roundedCorners;

  roundedCorners = StrappyLegacySelectionRoundedCornersForView(self);
  if (roundedCorners == 0U) {
    [[self layer] setMask:nil];
    return;
  }

  if (maskLayer_ == nil) {
    maskLayer_ = [CAShapeLayer layer];
  }
  path = [UIBezierPath
    bezierPathWithRoundedRect:[self bounds]
            byRoundingCorners:roundedCorners
                  cornerRadii:CGSizeMake(
                    kStrappyLegacyGroupedCellCornerRadius,
                    kStrappyLegacyGroupedCellCornerRadius)];
  [maskLayer_ setFrame:[self bounds]];
  [maskLayer_ setPath:[path CGPath]];
  [[self layer] setMask:maskLayer_];
}

@end

@implementation UITableViewCell (StrappyAppearance)

+ (void)strappy_setAppearanceSelectionBackgroundColorIfAvailable:
  (UIColor *)selectionBackgroundColor
{
  id appearanceProxy;

  if (![(id)self respondsToSelector:@selector(appearance)]) {
    return;
  }
  appearanceProxy = [(id)self performSelector:@selector(appearance)];
  if ((appearanceProxy == nil) ||
      ([appearanceProxy methodSignatureForSelector:
        @selector(setStrappySelectionBackgroundColor:)] == nil)) {
    return;
  }
  [appearanceProxy
    performSelector:@selector(setStrappySelectionBackgroundColor:)
                        withObject:selectionBackgroundColor];
}

- (void)setStrappySelectionBackgroundColor:
  (UIColor *)selectionBackgroundColor
{
  StrappyLegacySelectionBackgroundView *selectionBackgroundView;

  selectionBackgroundView = (StrappyLegacySelectionBackgroundView *)
    [self selectedBackgroundView];
  if (![selectionBackgroundView
        isKindOfClass:[StrappyLegacySelectionBackgroundView class]]) {
    selectionBackgroundView =
      [[StrappyLegacySelectionBackgroundView alloc]
        initWithFrame:[self bounds]];
    [self setSelectedBackgroundView:selectionBackgroundView];
  }
  [selectionBackgroundView setBackgroundColor:selectionBackgroundColor];
  [selectionBackgroundView setNeedsLayout];
}

@end

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

+ (void)configureAppearance
{
  UIDevice *device;

  device = [UIDevice currentDevice];
  if (![device XP_isOperatingSystemAtLeastMajorVersion:5]) {
    return;
  }

  if ([device XP_isOperatingSystemAtLeastMajorVersion:7]) {
    UIColor *barColor;

    barColor = [self modernBarBackgroundColor];
    [UINavigationBar XP_setAppearanceBarTintColorIfAvailable:barColor];
    [UIToolbar XP_setAppearanceBarTintColorIfAvailable:barColor];
    [UISearchBar XP_setAppearanceBarTintColorIfAvailable:barColor];
    return;
  }

  [UINavigationBar
    XP_setAppearanceTintColorIfAvailable:[self legacyBarTintColor]];
  [UIToolbar
    XP_setAppearanceTintColorIfAvailable:[self legacyBarTintColor]];
  [UISearchBar
    XP_setAppearanceTintColorIfAvailable:[self legacyBarTintColor]];
  [UIBarButtonItem
    XP_setAppearanceTintColorIfAvailable:[self primaryTintColor]];
  [UISwitch
    XP_setAppearanceOnTintColorIfAvailable:[self primaryTintColor]];
  [UITableViewCell
    strappy_setAppearanceSelectionBackgroundColorIfAvailable:
      [self legacyBarTintColor]];
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

+ (void)applyLegacyTintToBarButtonItem:
  (UIBarButtonItem *)barButtonItem
{
  UIDevice *device;

  if (barButtonItem == nil) {
    return;
  }
  device = [UIDevice currentDevice];
  if (![device XP_isOperatingSystemAtLeastMajorVersion:5] ||
      [device XP_isOperatingSystemAtLeastMajorVersion:7]) {
    return;
  }

  [barButtonItem XP_setTintColorIfAvailable:[self primaryTintColor]];
}

@end
