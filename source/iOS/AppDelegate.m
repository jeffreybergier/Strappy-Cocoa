#import "AppDelegate.h"
#import "AIFontAwesome.h"
#import "StrappyRootCoordinator.h"
#import "StrappySession.h"
#import <AltivecCore/AltivecCore.h>

@interface AppDelegate ()
@property (nonatomic, strong) StrappyRootCoordinator *coordinator;
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
  (void)application;
  (void)launchOptions;

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

  return YES;
}

@end
