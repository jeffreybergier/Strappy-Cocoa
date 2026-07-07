#import "AppDelegate.h"
#import "AIFontAwesome.h"
#import "StrappyRootCoordinator.h"
#import "StrappySession.h"
#import <AltivecCore/AltivecCore.h>

static NSString *StrappyApplicationStateName(UIApplicationState state)
{
  switch (state) {
    case UIApplicationStateActive:
      return @"active";
    case UIApplicationStateInactive:
      return @"inactive";
    case UIApplicationStateBackground:
      return @"background";
  }
  return @"unknown";
}

static void StrappyLogApplicationLifecycle(NSString *event,
                                           UIApplication *application)
{
  if (application == nil) {
    application = [UIApplication sharedApplication];
  }

  NSLog(@"StrappyLifecycle AppDelegate %@ state=%@ backgroundTimeRemaining=%.3f",
        event,
        StrappyApplicationStateName([application applicationState]),
        [application backgroundTimeRemaining]);
}

@interface AppDelegate ()
@property (nonatomic, strong) StrappyRootCoordinator *coordinator;
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
  StrappyLogApplicationLifecycle(@"didFinishLaunching begin", application);
  NSLog(@"StrappyLifecycle AppDelegate launchOptions=%@", launchOptions);

  self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];

  @try {
    NSString *cacert = [AltivecCore certPath];
    NSParameterAssert(cacert);
    [StrappySession bootstrapProcessWithCACertPath:cacert];
  } @catch (NSException *exception) {
    NSLog(@"AppDelegate.didFinishLaunching bootstrap failed: %@", exception);
  }

  [AIFontAwesome registerBundledFonts];

  self.coordinator = [[StrappyRootCoordinator alloc] initWithWindow:self.window];
  [self.coordinator start];
  [self.window makeKeyAndVisible];

  StrappyLogApplicationLifecycle(@"didFinishLaunching end", application);
  return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationWillResignActive", application);
}

- (void)applicationDidEnterBackground:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationDidEnterBackground", application);
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationWillEnterForeground", application);
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationDidBecomeActive", application);
}

- (void)applicationWillTerminate:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationWillTerminate", application);
}

- (void)applicationDidReceiveMemoryWarning:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationDidReceiveMemoryWarning", application);
}

@end
