/* Offline macOS production-delegate regression. Never launches the production
 * delegate, daemon, accounts, or native permission prompt. */
#import <AILocalNotificationCenter.h>
#import "../../macOS/AppDelegate.h"
#include <assert.h>
#ifndef __has_feature
#define __has_feature(x) 0
#endif

static BOOL fixtureBackground;
static AILocalNotificationCenter *fixtureCenter;
@interface NotificationApplication : NSApplication
@end
@implementation NotificationApplication
- (BOOL)isActive;
{ return !fixtureBackground; }
@end

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
  NSString *background = @"NSApplicationDidResignActiveNotification";
  NSString *active = @"NSApplicationDidBecomeActiveNotification";
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
  assert(recorder->submissions == 3);
  unsigned int submitted = recorder->submissions;
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
  /* Tiger must report Unsupported, independently of the recording backend. */
  if (NSClassFromString(@"NSUserNotificationCenter") == Nil) {
    fixtureBackground = YES;
    AILocalNotificationCenter *native = [AILocalNotificationCenter defaultCenter];
    assert([native authorizationStatus] == AINotificationAuthorizationUnsupported);
    assert([native postNotificationWithTitle:@"fixture" body:@"unsupported"
        userInfo:nil] == AINotificationUnsupported);
    fixtureBackground = NO;
  }
  NSLog(@"PASS: production notification delegates, wording, permissions, foreground cleanup; submission does not prove display");
}

int main(void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [NotificationApplication sharedApplication];
  RunNotifications();
  [pool drain];
  return 0;
}
