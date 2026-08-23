#import "StrappyAppearance.h"

#import <QuartzCore/QuartzCore.h>

#import "XPUIKit.h"

static const CGFloat kStrappyLegacyGroupedCellCornerRadius = 10.0f;
static const CGFloat kStrappyPrimaryTintRed = 142.0f;
static const CGFloat kStrappyPrimaryTintGreen = 27.0f;
static const CGFloat kStrappyPrimaryTintBlue = 207.0f;
static const CGFloat kStrappyDarkTintRed = 114.0f;
static const CGFloat kStrappyDarkTintGreen = 22.0f;
static const CGFloat kStrappyDarkTintBlue = 166.0f;
static const CGFloat kStrappyMutedPurpleRed = 137.0f;
static const CGFloat kStrappyMutedPurpleGreen = 102.0f;
static const CGFloat kStrappyMutedPurpleBlue = 154.0f;
static const CGFloat kStrappyLightPurpleRed = 216.0f;
static const CGFloat kStrappyLightPurpleGreen = 194.0f;
static const CGFloat kStrappyLightPurpleBlue = 229.0f;
static const CGFloat kStrappySectionHeaderTintRed = 149.4166667f;
static const CGFloat kStrappySectionHeaderTintGreen = 138.8583333f;
static const CGFloat kStrappySectionHeaderTintBlue = 153.9416667f;

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
  return [UIColor colorWithRed:(kStrappyPrimaryTintRed / 255.0f)
                         green:(kStrappyPrimaryTintGreen / 255.0f)
                          blue:(kStrappyPrimaryTintBlue / 255.0f)
                         alpha:1.0f];
}

+ (UIColor *)highlightedPrimaryTintColor
{
  return [UIColor colorWithRed:(kStrappyDarkTintRed / 255.0f)
                         green:(kStrappyDarkTintGreen / 255.0f)
                          blue:(kStrappyDarkTintBlue / 255.0f)
                         alpha:1.0f];
}

+ (UIColor *)legacyBarTintColor
{
  return [UIColor colorWithRed:(kStrappyMutedPurpleRed / 255.0f)
                         green:(kStrappyMutedPurpleGreen / 255.0f)
                          blue:(kStrappyMutedPurpleBlue / 255.0f)
                         alpha:1.0f];
}

+ (UIColor *)modernBarBackgroundColor
{
  return [UIColor colorWithRed:(kStrappyLightPurpleRed / 255.0f)
                         green:(kStrappyLightPurpleGreen / 255.0f)
                          blue:(kStrappyLightPurpleBlue / 255.0f)
                         alpha:1.0f];
}

+ (UIColor *)tableSectionHeaderTintColor
{
  return [UIColor colorWithRed:(kStrappySectionHeaderTintRed / 255.0f)
                         green:(kStrappySectionHeaderTintGreen / 255.0f)
                          blue:(kStrappySectionHeaderTintBlue / 255.0f)
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
  [self applyIOS6TableSectionHeaderTintColor:
    [self tableSectionHeaderTintColor]];
}

+ (void)applyIOS6TableSectionHeaderTintColor:(UIColor *)tintColor
{
  UIDevice *device;

  if (tintColor == nil) {
    return;
  }
  device = [UIDevice currentDevice];
  if (![device XP_isOperatingSystemAtLeastMajorVersion:6] ||
      [device XP_isOperatingSystemAtLeastMajorVersion:7]) {
    return;
  }

  [UITableView
    XP_setSectionHeaderFooterAppearanceTintColorIfAvailable:tintColor];
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
