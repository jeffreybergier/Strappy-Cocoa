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
@property (nonatomic, assign) UIBackgroundTaskIdentifier promptBackgroundTaskIdentifier;
- (void)observePromptLifecycle;
- (void)promptLifecycleDidChange:(NSNotification *)notification;
- (void)updatePromptBackgroundTaskAssertion;
- (void)beginPromptBackgroundTaskIfNeeded;
- (void)endPromptBackgroundTaskIfNeeded;
- (void)promptBackgroundTaskDidExpire;
@end

@implementation AppDelegate

- (instancetype)init
{
  if ((self = [super init])) {
    _promptBackgroundTaskIdentifier = UIBackgroundTaskInvalid;
  }
  return self;
}

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
  [self observePromptLifecycle];

  self.coordinator = [[StrappyRootCoordinator alloc] initWithWindow:self.window];
  [self.coordinator start];
  [self.window makeKeyAndVisible];

  [self updatePromptBackgroundTaskAssertion];
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
  [self updatePromptBackgroundTaskAssertion];
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationWillEnterForeground", application);
  [self updatePromptBackgroundTaskAssertion];
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationDidBecomeActive", application);
  [self updatePromptBackgroundTaskAssertion];
}

- (void)applicationWillTerminate:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationWillTerminate", application);
  [self endPromptBackgroundTaskIfNeeded];
}

- (void)applicationDidReceiveMemoryWarning:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationDidReceiveMemoryWarning", application);
}

- (void)observePromptLifecycle
{
  NSNotificationCenter *notificationCenter;

  notificationCenter = [NSNotificationCenter defaultCenter];
  [notificationCenter addObserver:self
                         selector:@selector(promptLifecycleDidChange:)
                             name:StrappySessionPromptDidStartNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(promptLifecycleDidChange:)
                             name:StrappySessionPromptDidFinishNotification
                           object:nil];
}

- (void)promptLifecycleDidChange:(NSNotification *)notification
{
  NSString *name;

  name = [notification name];
  NSLog(@"StrappyLifecycle AppDelegate promptLifecycleDidChange name=%@ inFlightSessions=%lu",
        name,
        (unsigned long)[StrappySession inFlightSessionCount]);
  [self updatePromptBackgroundTaskAssertion];
}

- (void)updatePromptBackgroundTaskAssertion
{
  if ([StrappySession hasInFlightSessions]) {
    [self beginPromptBackgroundTaskIfNeeded];
  } else {
    [self endPromptBackgroundTaskIfNeeded];
  }
}

- (void)beginPromptBackgroundTaskIfNeeded
{
  UIApplication *application;
  UIBackgroundTaskIdentifier taskIdentifier;

  @synchronized(self) {
    if ([self promptBackgroundTaskIdentifier] != UIBackgroundTaskInvalid) {
      return;
    }
  }

  application = [UIApplication sharedApplication];
  taskIdentifier =
    [application beginBackgroundTaskWithExpirationHandler:^{
      [self promptBackgroundTaskDidExpire];
    }];
  if (taskIdentifier == UIBackgroundTaskInvalid) {
    NSLog(@"StrappyLifecycle AppDelegate promptBackgroundTaskBeginFailed inFlightSessions=%lu backgroundTimeRemaining=%.3f",
          (unsigned long)[StrappySession inFlightSessionCount],
          [application backgroundTimeRemaining]);
    return;
  }

  @synchronized(self) {
    if ([self promptBackgroundTaskIdentifier] == UIBackgroundTaskInvalid) {
      [self setPromptBackgroundTaskIdentifier:taskIdentifier];
    } else if (taskIdentifier != UIBackgroundTaskInvalid) {
      [application endBackgroundTask:taskIdentifier];
      return;
    }
  }

  NSLog(@"StrappyLifecycle AppDelegate promptBackgroundTaskBegan task=%lu inFlightSessions=%lu backgroundTimeRemaining=%.3f",
        (unsigned long)taskIdentifier,
        (unsigned long)[StrappySession inFlightSessionCount],
        [application backgroundTimeRemaining]);
}

- (void)endPromptBackgroundTaskIfNeeded
{
  UIApplication *application;
  UIBackgroundTaskIdentifier taskIdentifier;

  @synchronized(self) {
    taskIdentifier = [self promptBackgroundTaskIdentifier];
    if (taskIdentifier == UIBackgroundTaskInvalid) {
      return;
    }
    [self setPromptBackgroundTaskIdentifier:UIBackgroundTaskInvalid];
  }

  application = [UIApplication sharedApplication];
  [application endBackgroundTask:taskIdentifier];
  NSLog(@"StrappyLifecycle AppDelegate promptBackgroundTaskEnded task=%lu inFlightSessions=%lu backgroundTimeRemaining=%.3f",
        (unsigned long)taskIdentifier,
        (unsigned long)[StrappySession inFlightSessionCount],
        [application backgroundTimeRemaining]);
}

- (void)promptBackgroundTaskDidExpire
{
  NSLog(@"StrappyLifecycle AppDelegate promptBackgroundTaskExpired inFlightSessions=%lu backgroundTimeRemaining=%.3f",
        (unsigned long)[StrappySession inFlightSessionCount],
        [[UIApplication sharedApplication] backgroundTimeRemaining]);
  [self endPromptBackgroundTaskIfNeeded];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [self endPromptBackgroundTaskIfNeeded];
}

@end
