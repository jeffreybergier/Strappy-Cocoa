#import "StrappyAppearance.h"

#import "XPUIKit.h"
#import "strappy_palette.h"

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
  if (usesModernAppearance) {
    barColor = [self modernBarBackgroundColor];
    [[navigationController navigationBar]
      XP_setBarTintColorIfAvailable:barColor];
    [[navigationController toolbar]
      XP_setBarTintColorIfAvailable:barColor];
  } else {
    barColor = [self legacyBarTintColor];
    [[navigationController navigationBar]
      XP_setTintColorIfAvailable:barColor];
    [[navigationController toolbar]
      XP_setTintColorIfAvailable:barColor];
  }
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

@end
