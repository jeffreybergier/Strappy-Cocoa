/* Recording-only bridge test. Never loads Apple notification frameworks,
 * requests system permissions, changes tones, or submits real notifications. */
#import <Foundation/Foundation.h>
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
static int modernAvailable = 1, denyAuthorization, holdAuthorization, holdBadge;
static unsigned int posts, badges;
static NSString *lastIdentifier, *lastTitle, *lastBody;
static long long lastCount;
static void (^authorizationReply)(BOOL, NSError *);
static void (^badgeReply)(NSError *);
static pthread_mutex_t testLock = PTHREAD_MUTEX_INITIALIZER;

@interface XPTestContent : NSObject {
  NSString *title_, *body_;
}
- (void)setTitle:(NSString *)value;
- (void)setBody:(NSString *)value;
- (NSString *)title;
- (NSString *)body;
@end
@implementation XPTestContent
- (void)setTitle:(NSString *)value { [title_ release]; title_ = [value copy]; }
- (void)setBody:(NSString *)value { [body_ release]; body_ = [value copy]; }
- (NSString *)title { return title_; }
- (NSString *)body { return body_; }
- (void)dealloc { [title_ release]; [body_ release]; [super dealloc]; }
@end
@interface XPTestRequest : NSObject
+ (id)requestWithIdentifier:(NSString *)identifier content:(id)content trigger:(id)trigger;
@end
@implementation XPTestRequest
+ (id)requestWithIdentifier:(NSString *)identifier content:(id)content trigger:(id)trigger
{
  assert(trigger == nil);
  return [NSDictionary dictionaryWithObjectsAndKeys:identifier,@"id",content,@"content",nil];
}
@end
@interface XPTestCenter : NSObject
- (id)initWithBundleIdentifier:(NSString *)identifier;
- (void)requestAuthorizationWithOptions:(NSUInteger)options
                    completionHandler:(void (^)(BOOL, NSError *))callback;
- (void)addNotificationRequest:(id)request
        withCompletionHandler:(void (^)(NSError *))callback;
- (void)setBadgeNumber:(NSNumber *)number
withCompletionHandler:(void (^)(NSError *))callback;
@end
@implementation XPTestCenter
- (id)initWithBundleIdentifier:(NSString *)identifier
{
  assert([identifier isEqual:@"com.altivecintelligence.Strappy"]);
  return [super init];
}
- (void)requestAuthorizationWithOptions:(NSUInteger)options
                    completionHandler:(void (^)(BOOL, NSError *))callback
{
  assert(options == 7);
  pthread_mutex_lock(&testLock);
  if (holdAuthorization) authorizationReply = [callback copy];
  else callback(!denyAuthorization,nil);
  pthread_mutex_unlock(&testLock);
}
- (void)addNotificationRequest:(id)request
        withCompletionHandler:(void (^)(NSError *))callback
{
  pthread_mutex_lock(&testLock);
  ++posts;
  [lastIdentifier release]; lastIdentifier = [[request objectForKey:@"id"] copy];
  id content = [request objectForKey:@"content"];
  [lastTitle release]; lastTitle = [[content title] copy];
  [lastBody release]; lastBody = [[content body] copy];
  pthread_mutex_unlock(&testLock);
  callback(nil);
}
- (void)setBadgeNumber:(NSNumber *)number
withCompletionHandler:(void (^)(NSError *))callback
{
  pthread_mutex_lock(&testLock);
  ++badges; lastCount = [number longLongValue];
  if (holdBadge) { [badgeReply release]; badgeReply = [callback copy]; }
  else callback(nil);
  pthread_mutex_unlock(&testLock);
}
@end
static Class XPTestClass(NSString *name)
{
  if ([name isEqual:@"UNUserNotificationCenter"])
    return modernAvailable ? [XPTestCenter class] : Nil;
  if ([name isEqual:@"UNMutableNotificationContent"]) return [XPTestContent class];
  if ([name isEqual:@"UNNotificationRequest"]) return [XPTestRequest class];
  return Nil; /* Sound unavailable must not prevent delivery. */
}
static void *XPTestLoad(const char *path, int flags)
{ (void)path; (void)flags; return NULL; }
#define NSClassFromString XPTestClass
#define dlopen XPTestLoad
#define XP_NOTIFICATION_REPLY_TIMEOUT 0.1
#include "../../shared/XPFoundation/XPDaemonNotifications.m"
#undef NSClassFromString
#undef dlopen

static void waitForPosts(unsigned int count)
{
  for (unsigned int i = 0; i < 2000; ++i) {
    pthread_mutex_lock(&testLock); int done = posts >= count;
    pthread_mutex_unlock(&testLock);
    if (done) return;
    usleep(1000);
  }
  assert(0 && "post timed out");
}
static void waitForBadge(long long revision, long long count)
{
  for (unsigned int i = 0; i < 2000; ++i) {
    if (XPDaemonNotificationsBadge(revision,count)) return;
    usleep(1000);
  }
  assert(0 && "badge timed out");
}
int main(void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  modernAvailable = 0;
  assert(!XPDaemonNotificationsStart());
  XPDaemonNotificationsStop();
  modernAvailable = 1;
  assert(XPDaemonNotificationsStart());
  assert(XPDaemonNotificationsPost(42,"Title","Body"));
  waitForPosts(1);
  pthread_mutex_lock(&testLock);
  assert([lastIdentifier isEqual:@"com.altivecintelligence.Strappy.run.42"]);
  assert([lastTitle isEqual:@"Title"] && [lastBody isEqual:@"Body"]);
  pthread_mutex_unlock(&testLock);
  waitForBadge(1,2); waitForBadge(2,1); waitForBadge(3,0);
  pthread_mutex_lock(&testLock); assert(lastCount == 0);
  pthread_mutex_unlock(&testLock);
  /* Missing selector and wrong argument ABI must fail without invocation. */
  assert(!XPInvoke([XPTestRequest class],@"missing",[NSArray array],"v",NULL));
  assert(!XPInvoke([NSNumber numberWithInt:1],@"intValue",[NSArray array],"@",NULL));
  XPDaemonNotificationsStop();

  /* Bound the queue while authorization is outstanding, then time out. */
  holdAuthorization = 1;
  assert(XPDaemonNotificationsStart());
  for (int i = 0; i < 32; ++i)
    assert(XPDaemonNotificationsPost(100+i,"Queued","Body"));
  assert(!XPDaemonNotificationsPost(200,"Full","Body"));
  usleep(200000);
  XPDaemonNotificationsStop();
  assert(authorizationReply);
  authorizationReply(YES,nil); /* Safe after timeout and teardown. */
  [authorizationReply release]; authorizationReply = nil;
  holdAuthorization = 0;

  /* A timed-out badge callback must not acknowledge a later revision. */
  holdBadge = 1;
  assert(XPDaemonNotificationsStart());
  assert(!XPDaemonNotificationsBadge(10,3));
  usleep(200000);
  pthread_mutex_lock(&testLock);
  assert(badgeReply); holdBadge = 0;
  void (^late)(NSError *) = badgeReply; badgeReply = nil;
  pthread_mutex_unlock(&testLock);
  waitForBadge(11,0);
  late(nil); [late release];
  assert(XPDaemonNotificationsBadge(11,0));
  XPDaemonNotificationsStop();
  denyAuthorization = 1;
  assert(XPDaemonNotificationsStart());
  usleep(200000);
  assert(!XPDaemonNotificationsPost(300,"Denied","Body"));
  XPDaemonNotificationsStop();
  [lastIdentifier release]; [lastTitle release]; [lastBody release];
  puts("PASS: class selection, authorization, content, missing API, bounded queue, badge revisions, zero, late callbacks, shutdown");
  [pool drain];
  return 0;
}
