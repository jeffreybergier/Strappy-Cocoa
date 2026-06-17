#import "AppDelegate.h"
#import "XPUIKit.h"
#import "AIFontAwesome.h"
#import "StrappySession.h"
#import <AltivecCore/AltivecCore.h>

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
  (void)application;
  (void)launchOptions;

  {
    NSString *cacert = [AltivecCore certPath];
    NSParameterAssert(cacert);
    [StrappySession bootstrapProcessWithCACertPath:cacert];
  }

  [AIFontAwesome registerBundledFonts];

  self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];

  UIViewController *viewController = [[UIViewController alloc] init];
  UIView *view = [[UIView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
  view.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                          UIViewAutoresizingFlexibleHeight;
  view.backgroundColor = [UIColor messagesBackgroundColor];

  UILabel *iconLabel = [[UILabel alloc] initWithFrame:CGRectMake(0.0f,
                                                                 96.0f,
                                                                 view.bounds.size.width,
                                                                 64.0f)];
  iconLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                               UIViewAutoresizingFlexibleBottomMargin;
  iconLabel.backgroundColor = [UIColor clearColor];
  iconLabel.font = [AIFontAwesome fontForStyle:AIFontAwesomeStyleSolid
                                          size:48.0f];
  iconLabel.text = [AIFontAwesome stringForCodePoint:AIFASeedling];
  [iconLabel XP_setTextAlignmentCenter];
  [view addSubview:iconLabel];

  UILabel *label = [[UILabel alloc] initWithFrame:CGRectInset(view.bounds,
                                                              24.0f,
                                                              24.0f)];
  label.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                           UIViewAutoresizingFlexibleHeight;
  label.backgroundColor = [UIColor clearColor];
  label.font = [UIFont boldSystemFontOfSize:24.0f];
  label.text = NSLocalizedString(@"Ready to build.", nil);
  [label XP_setTextAlignmentCenter];
  label.numberOfLines = 0;
  [view addSubview:label];

  viewController.view = view;
  self.window.rootViewController = viewController;
  [self.window makeKeyAndVisible];

  return YES;
}

@end
