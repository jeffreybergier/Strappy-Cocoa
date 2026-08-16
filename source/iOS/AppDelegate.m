#import "AppDelegate.h"
#import "AIFontAwesome.h"
#import "FileScanner.h"
#import "StrappyAuthentication.h"
#import "StrappyAppearance.h"
#import "StrappyRootCoordinator.h"
#import "StrappyIdleTimerAssertion.h"
#import "StrappySession.h"
#import "XPUIKit.h"
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

static NSString *StrappyPromptCompletionNotificationTitle(
  NSNotification *notification)
{
  NSDictionary *summary;
  StrappySession *session;
  id name;

  summary = [[notification userInfo] objectForKey:@"session"];
  session = [notification object];
  if (![summary isKindOfClass:[NSDictionary class]] &&
      [session isKindOfClass:[StrappySession class]]) {
    summary = [session cachedSummary];
  }
  name = [summary objectForKey:@"name"];
  if ([name isKindOfClass:[NSString class]] && ([name length] > 0U)) {
    return name;
  }
  return NSLocalizedString(@"Strappy", nil);
}

static NSString *StrappyPromptCompletionNotificationBody(
  NSNotification *notification)
{
  id errorMessage;

  errorMessage = [[notification userInfo] objectForKey:@"error"];
  if ([errorMessage isKindOfClass:[NSString class]] &&
      ([errorMessage length] > 0U)) {
    return NSLocalizedString(@"Prompt failed.", nil);
  }
  return NSLocalizedString(@"Prompt completed.", nil);
}

@interface AppDelegate ()
@property (nonatomic, strong) StrappyRootCoordinator *coordinator;
@property (nonatomic, assign) UIBackgroundTaskIdentifier longRunningWorkBackgroundTaskIdentifier;
@property (nonatomic, assign, getter=isLongRunningWorkIdleTimerAssertionEnabled)
  BOOL longRunningWorkIdleTimerAssertionEnabled;
@property (nonatomic, assign, getter=isLongRunningWorkNetworkActivityIndicatorEnabled)
  BOOL longRunningWorkNetworkActivityIndicatorEnabled;
- (void)observeLongRunningWorkLifecycle;
- (void)longRunningWorkLifecycleDidChange:(NSNotification *)notification;
- (void)configurePromptCompletionNotifications;
- (void)postPromptCompletionNotificationIfNeeded:
  (NSNotification *)notification;
- (BOOL)longRunningWorkIsActive;
- (void)updateLongRunningWorkAssertions;
- (void)setLongRunningWorkIdleTimerAssertionEnabled:(BOOL)enabled;
- (void)setLongRunningWorkNetworkActivityIndicatorEnabled:(BOOL)enabled;
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

  [StrappyAppearance configureAppearance];
  self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
  [StrappyAppearance applyApplicationTintToWindow:[self window]];

  {
    NSString *cacert;

    cacert = [AltivecCore certPath];
    NSParameterAssert(cacert);
    [StrappySession bootstrapProcessWithCACertPath:cacert];
    [StrappyAuthentication bootstrapProcessWithCACertPath:cacert];
  }

  [AIFontAwesome registerBundledFonts];
  [self observeLongRunningWorkLifecycle];

  self.coordinator = [[StrappyRootCoordinator alloc] initWithWindow:self.window];
  [self.coordinator start];
  [self.window makeKeyAndVisible];

  [self configurePromptCompletionNotifications];
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
  [[StrappyAuthentication sharedAuthentication]
    refreshChatGPTCredentialsIfNeeded];
  [self updateLongRunningWorkAssertions];
}

- (void)applicationWillTerminate:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationWillTerminate", application);
  [self setLongRunningWorkIdleTimerAssertionEnabled:NO];
  [self setLongRunningWorkNetworkActivityIndicatorEnabled:NO];
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
  if ([name isEqualToString:StrappySessionPromptDidFinishNotification]) {
    /* Deliver before updateLongRunningWorkAssertions can release the final
     * background task and let iOS suspend the process. */
    [self postPromptCompletionNotificationIfNeeded:notification];
  }
  [self updateLongRunningWorkAssertions];
}

- (void)configurePromptCompletionNotifications
{
  XPNotificationAuthStatus status;
  XPUserNotificationCenter *center;

  center = [XPUserNotificationCenter defaultCenter];
  status = [center authorizationStatus];
  NSLog(@"StrappyNotifications authorizationStatus=%d", (int)status);
  if (status == XPNotificationAuthStatusNotDetermined) {
    [center requestAuthorization];
  }
}

- (void)postPromptCompletionNotificationIfNeeded:
  (NSNotification *)notification
{
  UIApplication *application;
  XPUserNotificationCenter *center;
  NSString *body;
  NSString *title;

  application = [UIApplication sharedApplication];
  if ([application applicationState] != UIApplicationStateBackground) {
    return;
  }

  center = [XPUserNotificationCenter defaultCenter];
  if ([center authorizationStatus] != XPNotificationAuthStatusAuthorized) {
    return;
  }
  title = StrappyPromptCompletionNotificationTitle(notification);
  body = StrappyPromptCompletionNotificationBody(notification);
  [center postNotificationWithTitle:title body:body];
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
  [self setLongRunningWorkNetworkActivityIndicatorEnabled:active];
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

- (void)setLongRunningWorkNetworkActivityIndicatorEnabled:(BOOL)enabled
{
  if ([self isLongRunningWorkNetworkActivityIndicatorEnabled] == enabled) {
    return;
  }

  _longRunningWorkNetworkActivityIndicatorEnabled = enabled ? YES : NO;
  [[UIApplication sharedApplication]
    setNetworkActivityIndicatorVisible:enabled];
  NSLog(@"StrappyLifecycle AppDelegate longRunningWorkNetworkActivityIndicator %@",
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
  [self setLongRunningWorkNetworkActivityIndicatorEnabled:NO];
  [self endLongRunningWorkBackgroundTaskIfNeeded];
}

@end
