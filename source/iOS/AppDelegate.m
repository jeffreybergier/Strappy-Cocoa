#import "StrappyAsync.h"
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

@interface AppDelegate () <UIAlertViewDelegate>
@property (nonatomic, strong) StrappyRootCoordinator *coordinator;
@property (nonatomic, assign, getter=isLongRunningWorkNetworkActivityIndicatorEnabled)
  BOOL longRunningWorkNetworkActivityIndicatorEnabled;
- (void)showNextAsyncError;
- (void)observeLongRunningWorkLifecycle;
- (void)longRunningWorkLifecycleDidChange:(NSNotification *)notification;
- (BOOL)longRunningWorkIsActive;
- (void)updateLongRunningWorkActivityIndicator;
- (void)updatePromptBackgroundTask;
- (void)endPromptBackgroundTask;
- (void)setLongRunningWorkNetworkActivityIndicatorEnabled:(BOOL)enabled;
@end

static NSMutableArray *StrappyAsyncErrors;
static BOOL StrappyAsyncErrorVisible;

@implementation AppDelegate

- (instancetype)init;
{
  if ((self = [super init])) {
    promptBackgroundTask_ = UIBackgroundTaskInvalid;
    activePromptIdentifiers_ = [[NSMutableSet alloc] init];
  }
  return self;
}

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
  promptBackgroundTaskExpired_ = NO;
  StrappyLogApplicationLifecycle(@"applicationDidBecomeActive", application);
  [self showNextAsyncError];
  [[StrappyAuthentication sharedAuthentication]
    refreshChatGPTCredentialsIfNeeded];
  [self updateLongRunningWorkActivityIndicator];
}

- (void)applicationWillTerminate:(UIApplication *)application
{
  [self endPromptBackgroundTask];
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
  if ([[notification object] isKindOfClass:[StrappySession class]]) {
    NSNumber *identifier = [[notification object] sessionIdentifier];
    if (identifier) {
      if ([name isEqualToString:StrappySessionPromptDidStartNotification]) {
        /* Submission precedes the daemon's first persisted active run. */
        [activePromptIdentifiers_ addObject:identifier];
      } else if ([name isEqualToString:
                   StrappySessionPromptDidFinishNotification]) {
        [activePromptIdentifiers_ removeObject:identifier];
      }
    }
  }
  NSLog(@"StrappyLifecycle AppDelegate longRunningWorkLifecycleDidChange name=%@ inFlightSessions=%lu modelRefresh=%@ databaseScan=%@",
        name,
        (unsigned long)[StrappySession inFlightSessionCount],
        [StrappySession isModelCatalogRefreshInFlight] ? @"YES" : @"NO",
        [FileScanner isDatabaseCatalogScanInFlight] ? @"YES" : @"NO");
  [self updateLongRunningWorkActivityIndicator];
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
  [self updatePromptBackgroundTask];
  [self setLongRunningWorkNetworkActivityIndicatorEnabled:
    [self longRunningWorkIsActive]];
}

/* Main-thread lifecycle callbacks own one finite task for all active prompts.
 * Expiration releases it without cancelling daemon work or renewing the task
 * until the app becomes active again. */
- (void)updatePromptBackgroundTask;
{
  if (![activePromptIdentifiers_ count] &&
      ![StrappySession hasInFlightSessions]) {
    [self endPromptBackgroundTask];
    return;
  }
  if (promptBackgroundTaskExpired_ ||
      promptBackgroundTask_ != UIBackgroundTaskInvalid) return;

  promptBackgroundTask_ = [[UIApplication sharedApplication]
    beginBackgroundTaskWithExpirationHandler:^{
      promptBackgroundTaskExpired_ = YES;
      NSLog(@"StrappyLifecycle prompt background task expired");
      [self endPromptBackgroundTask];
    }];
  NSLog(@"StrappyLifecycle prompt background task %@",
        promptBackgroundTask_ == UIBackgroundTaskInvalid ?
          @"unavailable" : @"began");
}

- (void)endPromptBackgroundTask;
{
  UIBackgroundTaskIdentifier identifier = promptBackgroundTask_;
  if (identifier == UIBackgroundTaskInvalid) return;
  promptBackgroundTask_ = UIBackgroundTaskInvalid;
  [[UIApplication sharedApplication] endBackgroundTask:identifier];
  NSLog(@"StrappyLifecycle prompt background task ended");
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
  [self endPromptBackgroundTask];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [self setLongRunningWorkNetworkActivityIndicatorEnabled:NO];
}

@end
