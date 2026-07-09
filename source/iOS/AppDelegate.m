#import "AppDelegate.h"
#import "AIFontAwesome.h"
#import "FileScanner.h"
#import "StrappyRootCoordinator.h"
#import "StrappyIdleTimerAssertion.h"
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
@property (nonatomic, assign) UIBackgroundTaskIdentifier longRunningWorkBackgroundTaskIdentifier;
@property (nonatomic, assign, getter=isLongRunningWorkIdleTimerAssertionEnabled)
  BOOL longRunningWorkIdleTimerAssertionEnabled;
- (void)observeLongRunningWorkLifecycle;
- (void)longRunningWorkLifecycleDidChange:(NSNotification *)notification;
- (BOOL)longRunningWorkIsActive;
- (void)updateLongRunningWorkAssertions;
- (void)setLongRunningWorkIdleTimerAssertionEnabled:(BOOL)enabled;
- (void)beginLongRunningWorkBackgroundTaskIfNeeded;
- (void)endLongRunningWorkBackgroundTaskIfNeeded;
- (void)longRunningWorkBackgroundTaskDidExpire;
@end

@implementation AppDelegate

- (instancetype)init
{
  if ((self = [super init])) {
    _longRunningWorkBackgroundTaskIdentifier = UIBackgroundTaskInvalid;
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
  [self observeLongRunningWorkLifecycle];

  self.coordinator = [[StrappyRootCoordinator alloc] initWithWindow:self.window];
  [self.coordinator start];
  [self.window makeKeyAndVisible];

  [self updateLongRunningWorkAssertions];
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
  [self updateLongRunningWorkAssertions];
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationWillEnterForeground", application);
  [self updateLongRunningWorkAssertions];
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationDidBecomeActive", application);
  [self updateLongRunningWorkAssertions];
}

- (void)applicationWillTerminate:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationWillTerminate", application);
  [self setLongRunningWorkIdleTimerAssertionEnabled:NO];
  [self endLongRunningWorkBackgroundTaskIfNeeded];
}

- (void)applicationDidReceiveMemoryWarning:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationDidReceiveMemoryWarning", application);
}

- (void)observeLongRunningWorkLifecycle
{
  NSNotificationCenter *notificationCenter;

  notificationCenter = [NSNotificationCenter defaultCenter];
  [notificationCenter addObserver:self
                         selector:@selector(longRunningWorkLifecycleDidChange:)
                             name:StrappySessionPromptDidStartNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(longRunningWorkLifecycleDidChange:)
                             name:StrappySessionPromptDidFinishNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(longRunningWorkLifecycleDidChange:)
                             name:StrappySessionModelCatalogRefreshDidStartNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(longRunningWorkLifecycleDidChange:)
                             name:StrappySessionModelCatalogRefreshDidFinishNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(longRunningWorkLifecycleDidChange:)
                             name:FileScannerDatabaseCatalogScanDidStartNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(longRunningWorkLifecycleDidChange:)
                             name:FileScannerDatabaseCatalogScanDidFinishNotification
                           object:nil];
}

- (void)longRunningWorkLifecycleDidChange:(NSNotification *)notification
{
  NSString *name;

  name = [notification name];
  NSLog(@"StrappyLifecycle AppDelegate longRunningWorkLifecycleDidChange name=%@ inFlightSessions=%lu modelRefresh=%@ databaseScan=%@",
        name,
        (unsigned long)[StrappySession inFlightSessionCount],
        [StrappySession isModelCatalogRefreshInFlight] ? @"YES" : @"NO",
        [FileScanner isDatabaseCatalogScanInFlight] ? @"YES" : @"NO");
  [self updateLongRunningWorkAssertions];
}

- (BOOL)longRunningWorkIsActive
{
  return ([StrappySession hasInFlightSessions] ||
          [StrappySession isModelCatalogRefreshInFlight] ||
          [FileScanner isDatabaseCatalogScanInFlight]) ? YES : NO;
}

- (void)updateLongRunningWorkAssertions
{
  BOOL active;

  active = [self longRunningWorkIsActive];
  [self setLongRunningWorkIdleTimerAssertionEnabled:active];
  if (active) {
    [self beginLongRunningWorkBackgroundTaskIfNeeded];
  } else {
    [self endLongRunningWorkBackgroundTaskIfNeeded];
  }
}

- (void)setLongRunningWorkIdleTimerAssertionEnabled:(BOOL)enabled
{
  if ([self isLongRunningWorkIdleTimerAssertionEnabled] == enabled) {
    return;
  }

  _longRunningWorkIdleTimerAssertionEnabled = enabled ? YES : NO;
  StrappyIdleTimerAssertionSetEnabled(enabled);
  NSLog(@"StrappyLifecycle AppDelegate longRunningWorkIdleTimerAssertion %@",
        enabled ? @"YES" : @"NO");
}

- (void)beginLongRunningWorkBackgroundTaskIfNeeded
{
  UIApplication *application;
  UIBackgroundTaskIdentifier taskIdentifier;

  @synchronized(self) {
    if ([self longRunningWorkBackgroundTaskIdentifier] !=
        UIBackgroundTaskInvalid) {
      return;
    }
  }

  application = [UIApplication sharedApplication];
  taskIdentifier =
    [application beginBackgroundTaskWithExpirationHandler:^{
      [self longRunningWorkBackgroundTaskDidExpire];
    }];
  if (taskIdentifier == UIBackgroundTaskInvalid) {
    NSLog(@"StrappyLifecycle AppDelegate longRunningWorkBackgroundTaskBeginFailed inFlightSessions=%lu modelRefresh=%@ databaseScan=%@ backgroundTimeRemaining=%.3f",
          (unsigned long)[StrappySession inFlightSessionCount],
          [StrappySession isModelCatalogRefreshInFlight] ? @"YES" : @"NO",
          [FileScanner isDatabaseCatalogScanInFlight] ? @"YES" : @"NO",
          [application backgroundTimeRemaining]);
    return;
  }

  @synchronized(self) {
    if ([self longRunningWorkBackgroundTaskIdentifier] ==
        UIBackgroundTaskInvalid) {
      [self setLongRunningWorkBackgroundTaskIdentifier:taskIdentifier];
    } else if (taskIdentifier != UIBackgroundTaskInvalid) {
      [application endBackgroundTask:taskIdentifier];
      return;
    }
  }

  NSLog(@"StrappyLifecycle AppDelegate longRunningWorkBackgroundTaskBegan task=%lu inFlightSessions=%lu modelRefresh=%@ databaseScan=%@ backgroundTimeRemaining=%.3f",
        (unsigned long)taskIdentifier,
        (unsigned long)[StrappySession inFlightSessionCount],
        [StrappySession isModelCatalogRefreshInFlight] ? @"YES" : @"NO",
        [FileScanner isDatabaseCatalogScanInFlight] ? @"YES" : @"NO",
        [application backgroundTimeRemaining]);
}

- (void)endLongRunningWorkBackgroundTaskIfNeeded
{
  UIApplication *application;
  UIBackgroundTaskIdentifier taskIdentifier;

  @synchronized(self) {
    taskIdentifier = [self longRunningWorkBackgroundTaskIdentifier];
    if (taskIdentifier == UIBackgroundTaskInvalid) {
      return;
    }
    [self setLongRunningWorkBackgroundTaskIdentifier:UIBackgroundTaskInvalid];
  }

  application = [UIApplication sharedApplication];
  [application endBackgroundTask:taskIdentifier];
  NSLog(@"StrappyLifecycle AppDelegate longRunningWorkBackgroundTaskEnded task=%lu inFlightSessions=%lu modelRefresh=%@ databaseScan=%@ backgroundTimeRemaining=%.3f",
        (unsigned long)taskIdentifier,
        (unsigned long)[StrappySession inFlightSessionCount],
        [StrappySession isModelCatalogRefreshInFlight] ? @"YES" : @"NO",
        [FileScanner isDatabaseCatalogScanInFlight] ? @"YES" : @"NO",
        [application backgroundTimeRemaining]);
}

- (void)longRunningWorkBackgroundTaskDidExpire
{
  NSLog(@"StrappyLifecycle AppDelegate longRunningWorkBackgroundTaskExpired inFlightSessions=%lu modelRefresh=%@ databaseScan=%@ backgroundTimeRemaining=%.3f",
        (unsigned long)[StrappySession inFlightSessionCount],
        [StrappySession isModelCatalogRefreshInFlight] ? @"YES" : @"NO",
        [FileScanner isDatabaseCatalogScanInFlight] ? @"YES" : @"NO",
        [[UIApplication sharedApplication] backgroundTimeRemaining]);
  [self endLongRunningWorkBackgroundTaskIfNeeded];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [self setLongRunningWorkIdleTimerAssertionEnabled:NO];
  [self endLongRunningWorkBackgroundTaskIfNeeded];
}

@end
