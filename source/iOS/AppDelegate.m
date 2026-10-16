#import "StrappyAsync.h"
#import <AILocalNotificationCenter.h>
#import "AppDelegate.h"
#import "AIFontAwesome.h"
#import "FileScanner.h"
#import "StrappyAuthentication.h"
#import "StrappyAppearance.h"
#import "StrappyRootCoordinator.h"
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

@interface AppDelegate () <UIAlertViewDelegate>
@property (nonatomic, strong) StrappyRootCoordinator *coordinator;
@property (nonatomic, assign, getter=isLongRunningWorkNetworkActivityIndicatorEnabled)
  BOOL longRunningWorkNetworkActivityIndicatorEnabled;
- (void)showNextAsyncError;
- (void)observeLongRunningWorkLifecycle;
- (void)longRunningWorkLifecycleDidChange:(NSNotification *)notification;
- (void)configurePromptCompletionNotifications;
- (AILocalNotificationCenter *)promptCompletionNotificationCenter;
- (void)postPromptCompletionNotificationIfNeeded:
  (NSNotification *)notification;
- (BOOL)longRunningWorkIsActive;
- (void)updateLongRunningWorkActivityIndicator;
- (void)setLongRunningWorkNetworkActivityIndicatorEnabled:(BOOL)enabled;
@end

static NSMutableArray *StrappyAsyncErrors;
static BOOL StrappyAsyncErrorVisible;

@implementation AppDelegate

- (void)asyncCommandFailed:(NSNotification *)notification
{
  if (!StrappyAsyncErrors) StrappyAsyncErrors = [[NSMutableArray alloc] init];
  [StrappyAsyncErrors addObject:[notification userInfo]];
  [self showNextAsyncError];
}

- (void)showNextAsyncError
{
  NSDictionary *info;
  UIAlertView *alert;
  if (StrappyAsyncErrorVisible || ![StrappyAsyncErrors count] ||
      [[UIApplication sharedApplication] applicationState] != UIApplicationStateActive) return;
  info = [StrappyAsyncErrors objectAtIndex:0];
  StrappyAsyncErrorVisible = YES;
  alert = [[UIAlertView alloc] initWithTitle:[info objectForKey:@"title"]
    message:[[info objectForKey:@"error"] localizedDescription] delegate:self
    cancelButtonTitle:NSLocalizedString(@"OK",nil) otherButtonTitles:nil];
  [alert show];
}

- (void)alertView:(UIAlertView *)alertView didDismissWithButtonIndex:(NSInteger)buttonIndex
{
  (void)alertView; (void)buttonIndex;
  if ([StrappyAsyncErrors count]) [StrappyAsyncErrors removeObjectAtIndex:0];
  StrappyAsyncErrorVisible = NO;
  [self showNextAsyncError];
}

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
  StrappyLogApplicationLifecycle(@"didFinishLaunching begin", application);
  NSLog(@"StrappyLifecycle AppDelegate launchOptions=%@", launchOptions);

  [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(asyncCommandFailed:)
    name:StrappyAsyncCommandFailedNotification object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(longRunningWorkLifecycleDidChange:)
    name:StrappyAsyncActivityChangedNotification object:nil];
  [StrappyAppearance configureAppearance];
  self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
  [StrappyAppearance applyApplicationTintToWindow:[self window]];

  {
    [StrappySession bootstrapProcess];
  }

  [AIFontAwesome registerBundledFonts];
  [self observeLongRunningWorkLifecycle];

  self.coordinator = [[StrappyRootCoordinator alloc] initWithWindow:self.window];
  [self.coordinator start];
  [self.window makeKeyAndVisible];

  [self configurePromptCompletionNotifications];
  [self updateLongRunningWorkActivityIndicator];
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
  [self updateLongRunningWorkActivityIndicator];
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationWillEnterForeground", application);
  [self updateLongRunningWorkActivityIndicator];
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationDidBecomeActive", application);
  [self showNextAsyncError];
  [[StrappyAuthentication sharedAuthentication]
    refreshChatGPTCredentialsIfNeeded];
  [self updateLongRunningWorkActivityIndicator];
}

- (void)applicationWillTerminate:(UIApplication *)application
{
  StrappyLogApplicationLifecycle(@"applicationWillTerminate", application);
  [self setLongRunningWorkNetworkActivityIndicatorEnabled:NO];
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
    [self postPromptCompletionNotificationIfNeeded:notification];
  }
  [self updateLongRunningWorkActivityIndicator];
}

- (void)configurePromptCompletionNotifications
{
  AINotificationAuthorizationStatus status;
  AILocalNotificationCenter *center;

  center = [self promptCompletionNotificationCenter];
  status = [center authorizationStatus];
  NSLog(@"StrappyNotifications authorizationStatus=%d", (int)status);
  if (status == AINotificationAuthorizationUnknown) {
    @try {
      [center requestAuthorization];
    } @catch (NSException *exception) {
      (void)exception;
      NSLog(@"StrappyNotifications authorization request failed");
    }
  }
}

- (AILocalNotificationCenter *)promptCompletionNotificationCenter;
{
  return [AILocalNotificationCenter defaultCenter];
}

- (void)postPromptCompletionNotificationIfNeeded:
  (NSNotification *)notification
{
  UIApplication *application;
  AILocalNotificationCenter *center;
  NSString *body;
  NSString *title;

  application = [UIApplication sharedApplication];
  if ([application applicationState] != UIApplicationStateBackground) {
    return;
  }

  center = [self promptCompletionNotificationCenter];
  if ([center authorizationStatus] != AINotificationAuthorizationAuthorized) {
    return;
  }
  title = StrappyPromptCompletionNotificationTitle(notification);
  body = StrappyPromptCompletionNotificationBody(notification);
  /* Unknown settings are not proof of permission on iOS. Keep the
   * existing authorized-only policy; submission still cannot prove display
   * for Strappy's unsandboxed/system-app installation. */
  [center postNotificationWithTitle:title body:body userInfo:nil];
}

- (BOOL)longRunningWorkIsActive
{
  if ([StrappyAsync pendingCommandCount] > 0) return YES;
  return ([StrappySession hasInFlightSessions] ||
          [StrappySession isModelCatalogRefreshInFlight] ||
          [FileScanner isDatabaseCatalogScanInFlight]) ? YES : NO;
}

- (void)updateLongRunningWorkActivityIndicator
{
  [self setLongRunningWorkNetworkActivityIndicatorEnabled:
    [self longRunningWorkIsActive]];
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

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [self setLongRunningWorkNetworkActivityIndicatorEnabled:NO];
}

@end
