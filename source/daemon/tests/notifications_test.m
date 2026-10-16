/* Offline production-delegate regression. Never launches the production
 * delegate, daemon, accounts, or native permission prompt. */
#import <TargetConditionals.h>
#import <AILocalNotificationCenter.h>
#if TARGET_OS_IPHONE
#import "../../iOS/AppDelegate.h"
#else
#import "../../macOS/AppDelegate.h"
#endif
#include <assert.h>
#ifndef __has_feature
#define __has_feature(x) 0
#endif

static BOOL fixtureBackground;
#if TARGET_OS_IPHONE
static BOOL fixtureInactive;
#endif
static AILocalNotificationCenter *fixtureCenter;
#if TARGET_OS_IPHONE
@interface NotificationApplication : UIApplication
@end
@implementation NotificationApplication
- (UIApplicationState)applicationState;
{
  if (fixtureInactive) return UIApplicationStateInactive;
  return fixtureBackground ? UIApplicationStateBackground : UIApplicationStateActive;
}
@end
#else
@interface NotificationApplication : NSApplication
@end
@implementation NotificationApplication
- (BOOL)isActive;
{ return !fixtureBackground; }
@end
#endif

@interface AppDelegate (NotificationFixture)
- (AILocalNotificationCenter *)promptCompletionNotificationCenter;
- (void)configurePromptCompletionNotifications;
- (void)postPromptCompletionNotificationIfNeeded:(NSNotification *)notification;
@end
@interface NotificationDelegate : AppDelegate
@end
@implementation NotificationDelegate
- (AILocalNotificationCenter *)promptCompletionNotificationCenter;
{ return fixtureCenter; }
@end

@interface NotificationRecorder : NSObject <AILocalNotificationBackend> {
@public
  AINotificationAuthorizationStatus status;
  unsigned int requests, submissions, clears;
  BOOL rejectRequest;
  int badge;
  NSString *lastTitle, *lastBody;
}
@end
@implementation NotificationRecorder
- (AINotificationAuthorizationStatus)authorizationStatus;
{ return status; }
- (void)requestAuthorization;
{
  ++requests;
  if (rejectRequest) [NSException raise:@"fixture" format:@"request failed"];
}
- (BOOL)isApplicationInBackground;
{ return fixtureBackground; }
- (BOOL)submitTitle:(NSString *)title body:(NSString *)body
          userInfo:(NSDictionary *)info;
{
  assert(info == nil);
  ++submissions;
#if !__has_feature(objc_arc)
  [lastTitle release]; [lastBody release];
#endif
  lastTitle = [title copy]; lastBody = [body copy];
  return YES;
}
- (void)clearNotifications;
{ ++clears; }
- (void)setBadgeCount:(int)value;
{ badge = value; }
#if !__has_feature(objc_arc)
- (void)dealloc;
{ [lastTitle release]; [lastBody release]; [super dealloc]; }
#endif
@end

static void RunNotifications(void)
{
  NotificationRecorder *recorder = [[NotificationRecorder alloc] init];
  NotificationDelegate *delegate = [[NotificationDelegate alloc] init];
  fixtureBackground = NO;
  fixtureCenter = [[AILocalNotificationCenter alloc] initWithBackend:recorder];
  NSNotification *success = [NSNotification notificationWithName:@"fixture"
      object:nil userInfo:[NSDictionary dictionaryWithObject:
        [NSDictionary dictionaryWithObject:@"日本語 session" forKey:@"name"]
        forKey:@"session"]];
  recorder->status = AINotificationAuthorizationAuthorized;
  [delegate postPromptCompletionNotificationIfNeeded:success];
  assert(recorder->submissions == 0);
#if TARGET_OS_IPHONE
  NSString *background = @"UIApplicationDidEnterBackgroundNotification";
  NSString *active = @"UIApplicationDidBecomeActiveNotification";
  [delegate configurePromptCompletionNotifications];
  assert(recorder->requests == 0);
  recorder->status = AINotificationAuthorizationUnknown;
  [delegate configurePromptCompletionNotifications];
  assert(recorder->requests == 1);
  recorder->rejectRequest = YES;
  [delegate configurePromptCompletionNotifications];
  assert(recorder->requests == 2);
  recorder->rejectRequest = NO;
  recorder->status = AINotificationAuthorizationDenied;
  [delegate configurePromptCompletionNotifications];
  assert(recorder->requests == 2);
#else
  NSString *background = @"NSApplicationDidResignActiveNotification";
  NSString *active = @"NSApplicationDidBecomeActiveNotification";
#endif
  fixtureBackground = YES;
  [[NSNotificationCenter defaultCenter] postNotificationName:background object:nil];
  recorder->status = AINotificationAuthorizationAuthorized;
  [delegate postPromptCompletionNotificationIfNeeded:success];
  assert(recorder->submissions == 1);
  assert([recorder->lastTitle isEqual:@"日本語 session"]);
  assert([recorder->lastBody isEqual:NSLocalizedString(@"Prompt completed.", nil)]);
  NSNotification *failure = [NSNotification notificationWithName:@"fixture"
      object:nil userInfo:[NSDictionary dictionaryWithObject:@"private error"
      forKey:@"error"]];
  [delegate postPromptCompletionNotificationIfNeeded:failure];
  assert(recorder->submissions == 2);
  assert([recorder->lastTitle isEqual:NSLocalizedString(@"Strappy", nil)]);
  assert([recorder->lastBody isEqual:NSLocalizedString(@"Prompt failed.", nil)]);
  recorder->status = AINotificationAuthorizationDenied;
  [delegate postPromptCompletionNotificationIfNeeded:success];
  recorder->status = AINotificationAuthorizationUnsupported;
  [delegate postPromptCompletionNotificationIfNeeded:success];
  assert(recorder->submissions == 2);
  recorder->status = AINotificationAuthorizationUnknown;
  [delegate postPromptCompletionNotificationIfNeeded:success];
#if TARGET_OS_IPHONE
  assert(recorder->submissions == 2);
#else
  assert(recorder->submissions == 3);
#endif
  unsigned int submitted = recorder->submissions;
#if TARGET_OS_IPHONE
  fixtureInactive = YES;
  recorder->status = AINotificationAuthorizationAuthorized;
  [delegate postPromptCompletionNotificationIfNeeded:success];
  assert(recorder->submissions == submitted);
  fixtureInactive = NO;
#endif
  fixtureBackground = NO;
  [[NSNotificationCenter defaultCenter] postNotificationName:active object:nil];
  assert(recorder->clears == 1 && recorder->badge == 0);
  [delegate postPromptCompletionNotificationIfNeeded:success];
  assert(recorder->submissions == submitted);
  [fixtureCenter invalidate];
#if !__has_feature(objc_arc)
  [fixtureCenter release]; [delegate release]; [recorder release];
#endif
  fixtureCenter = nil;
#if !TARGET_OS_IPHONE
  /* Tiger must report Unsupported, independently of the recording backend. */
  if (NSClassFromString(@"NSUserNotificationCenter") == Nil) {
    fixtureBackground = YES;
    AILocalNotificationCenter *native = [AILocalNotificationCenter defaultCenter];
    assert([native authorizationStatus] == AINotificationAuthorizationUnsupported);
    assert([native postNotificationWithTitle:@"fixture" body:@"unsupported"
        userInfo:nil] == AINotificationUnsupported);
    fixtureBackground = NO;
  }
#endif
  NSLog(@"PASS: production notification delegates, wording, permissions, foreground cleanup; submission does not prove display");
}

#if TARGET_OS_IPHONE
@interface NotificationRunner : UIResponder <UIApplicationDelegate>
@end
@implementation NotificationRunner
- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)options;
{
  (void)application; (void)options;
  RunNotifications();
  NSString *path = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
      NSUserDomainMask, YES) objectAtIndex:0];
  [@"PASS\n" writeToFile:[path stringByAppendingPathComponent:@"notifications-result.txt"]
      atomically:YES encoding:NSUTF8StringEncoding error:NULL];
  return YES;
}
@end
int main(int argc, char **argv)
{
  @autoreleasepool {
    return UIApplicationMain(argc, argv, @"NotificationApplication", @"NotificationRunner");
  }
}
#else
int main(void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NotificationApplication sharedApplication];
  RunNotifications();
  [pool drain];
  return 0;
}
#endif
