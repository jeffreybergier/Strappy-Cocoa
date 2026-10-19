/* Daemon-only modern iOS bridge. No UIKit lifecycle or static UN/Tone linkage. */
#import <Foundation/Foundation.h>
#import "XPDaemonNotifications.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#ifndef XP_NOTIFICATION_REPLY_TIMEOUT
#define XP_NOTIFICATION_REPLY_TIMEOUT 5.0
#endif

static NSString *const kBundle = @"com.altivecintelligence.Strappy";
static NSString *const kToneTopic = @"com.altivecintelligence.Strappy.completion";
static const unsigned int kQueueLimit = 32;
static pthread_mutex_t kLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t kChanged = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static int started, stopping, accepting;
static unsigned int head, queued;
static struct { long long run; char title[401], body[1601]; } alerts[32];
static long long desiredRevision, desiredCount, ackRevision, ackCount;

/* Exact signature validation; object/block parameters use Objective-C's @ ABI.
 * Owned alloc/init/mutableCopy results retain their usual MRC ownership. */
static BOOL XPInvoke(id target, NSString *name, NSArray *arguments,
                     const char *resultType, id *result)
{
  SEL selector = NSSelectorFromString(name);
  if (![target respondsToSelector:selector]) return NO;
  NSMethodSignature *signature = [target methodSignatureForSelector:selector];
  if (!signature || [signature numberOfArguments] != [arguments count] + 2 ||
      strcmp([signature methodReturnType], resultType)) return NO;
  NSInvocation *call = [NSInvocation invocationWithMethodSignature:signature];
  [call setTarget:target];
  [call setSelector:selector];
  for (NSUInteger i = 0; i < [arguments count]; ++i) {
    const char *type = [signature getArgumentTypeAtIndex:i + 2];
    id value = [arguments objectAtIndex:i];
    if (!strcmp(type, "@") || !strcmp(type, "@?")) {
      if (value == [NSNull null]) value = nil;
      [call setArgument:&value atIndex:i + 2];
    } else if (!strcmp(type, @encode(NSInteger)) &&
        [value isKindOfClass:[NSNumber class]]) {
      NSInteger integer = [value integerValue];
      [call setArgument:&integer atIndex:i + 2];
    } else if (!strcmp(type, @encode(NSUInteger)) &&
        [value isKindOfClass:[NSNumber class]]) {
      NSUInteger integer = [value unsignedIntegerValue];
      [call setArgument:&integer atIndex:i + 2];
    } else return NO;
  }
  [call invoke];
  if (result) [call getReturnValue:result];
  return YES;
}
static id XPObject(id target, NSString *name, NSArray *arguments)
{
  id result = nil;
  return XPInvoke(target,name,arguments,"@",&result) ? result : nil;
}
static BOOL XPSet(id target, NSString *name, id value)
{
  return value && XPInvoke(target,name,[NSArray arrayWithObject:value],"v",NULL);
}

/* Copied system callbacks retain this object after timeout/shutdown. They never
 * capture daemon pointers or write global badge acknowledgment state. */
@interface XPNotificationReply : NSObject {
  NSCondition *condition_;
  BOOL done_, success_;
}
- (void)complete:(BOOL)success;
- (BOOL)wait;
@end
@implementation XPNotificationReply
- (id)init;
{
  self = [super init];
  if (self) condition_ = [[NSCondition alloc] init];
  return self;
}
- (void)dealloc;
{
  [condition_ release];
  [super dealloc];
}
- (void)complete:(BOOL)success;
{
  [condition_ lock];
  if (!done_) { done_ = YES; success_ = success; }
  [condition_ signal];
  [condition_ unlock];
}
- (BOOL)wait;
{
  NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:XP_NOTIFICATION_REPLY_TIMEOUT];
  [condition_ lock];
  while (!done_ && [condition_ waitUntilDate:deadline]) {}
  BOOL success = done_ && success_;
  [condition_ unlock];
  return success;
}
@end

static BOOL XPAuthorize(id center)
{
  XPNotificationReply *reply = [[XPNotificationReply alloc] init];
  void (^callback)(BOOL, NSError *) = [^(BOOL granted, NSError *error) {
    [reply complete:granted && !error];
  } copy];
  BOOL ok = NO;
  @try {
    ok = XPInvoke(center,@"requestAuthorizationWithOptions:completionHandler:",
      [NSArray arrayWithObjects:[NSNumber numberWithUnsignedInteger:7],
        callback,nil],"v",NULL);
    if (ok) ok = [reply wait];
  } @finally {
    [callback release];
    [reply release];
  }
  return ok;
}
static BOOL XPSubmit(id center, NSString *selector, id argument)
{
  XPNotificationReply *reply = [[XPNotificationReply alloc] init];
  void (^callback)(NSError *) = [^(NSError *error) {
    [reply complete:error == nil];
  } copy];
  BOOL ok = NO;
  @try {
    ok = XPInvoke(center,selector,
      [NSArray arrayWithObjects:argument,callback,nil],"v",NULL);
    if (ok) ok = [reply wait];
  } @finally {
    [callback release];
    [reply release];
  }
  return ok;
}
static id XPSound(void)
{
  Class cls = NSClassFromString(@"UNNotificationSound");
  dlopen("/System/Library/PrivateFrameworks/ToneLibrary.framework/ToneLibrary",
    RTLD_NOW | RTLD_LOCAL);
  id manager = XPObject(NSClassFromString(@"TLToneManager"),
    @"sharedToneManager",[NSArray array]);
  NSNumber *type = [NSNumber numberWithInteger:5];
  id sound = XPObject(cls,@"soundWithAlertType:",[NSArray arrayWithObject:type]);
  sound = [[sound mutableCopy] autorelease];
  if (sound && XPSet(sound,@"setAlertTopic:",kToneTopic) &&
      XPInvoke(manager,@"setCurrentToneIdentifier:forAlertType:topic:",
        [NSArray arrayWithObjects:@"texttone:Glass",type,kToneTopic,nil],
        "v",NULL) &&
      [XPObject(manager,@"currentToneIdentifierForAlertType:topic:",
        [NSArray arrayWithObjects:type,kToneTopic,nil]) isEqual:@"texttone:Glass"])
    return sound;
  return XPObject(cls,@"defaultSound",[NSArray array]);
}
static BOOL XPPost(id center, id sound, long long run, const char *title,
                   const char *body)
{
  id content = [[NSClassFromString(@"UNMutableNotificationContent") alloc] init];
  BOOL ok = NO;
  @try {
    ok = XPSet(content,@"setTitle:",[NSString stringWithUTF8String:title]) &&
      XPSet(content,@"setBody:",[NSString stringWithUTF8String:body]);
    if (sound) XPSet(content,@"setSound:",sound);
    id request = ok ? XPObject(NSClassFromString(@"UNNotificationRequest"),
      @"requestWithIdentifier:content:trigger:",[NSArray arrayWithObjects:
        [NSString stringWithFormat:@"%@.run.%lld",kBundle,run],content,
        [NSNull null],nil]) : nil;
    ok = request && XPSubmit(center,
      @"addNotificationRequest:withCompletionHandler:",request);
  } @finally { [content release]; }
  return ok;
}
static void *XPWorker(void *unused)
{
  (void)unused;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  id center = nil, sound = nil;
  @try {
    id allocated = [NSClassFromString(@"UNUserNotificationCenter") alloc];
    if (!XPInvoke(allocated,@"initWithBundleIdentifier:",
        [NSArray arrayWithObject:kBundle],"@",&center)) [allocated release];
    if (!center || !XPAuthorize(center)) {
      syslog(LOG_NOTICE,"StrappyNotifications: modern initialization failed");
    } else {
      @try { sound = [XPSound() retain]; }
      @catch (NSException *exception) {
        (void)exception;
        sound = [XPObject(NSClassFromString(@"UNNotificationSound"),
          @"defaultSound",[NSArray array]) retain];
      }
      for (;;) {
        long long revision = 0, count = 0, run = 0;
        char title[401], body[1601];
        pthread_mutex_lock(&kLock);
        while (!stopping && !queued && !desiredRevision)
          pthread_cond_wait(&kChanged,&kLock);
        if (stopping) { pthread_mutex_unlock(&kLock); break; }
        if (desiredRevision) {
          revision = desiredRevision; count = desiredCount;
        } else {
          run = alerts[head].run;
          memcpy(title,alerts[head].title,sizeof(title));
          memcpy(body,alerts[head].body,sizeof(body));
          head = (head + 1) % kQueueLimit; --queued;
        }
        pthread_mutex_unlock(&kLock);
        NSAutoreleasePool *operationPool = [[NSAutoreleasePool alloc] init];
        BOOL ok = NO;
        @try {
          ok = revision ? XPSubmit(center,@"setBadgeNumber:withCompletionHandler:",
            [NSNumber numberWithLongLong:count]) : XPPost(center,sound,run,title,body);
        } @catch (NSException *exception) { (void)exception; }
        if (revision) {
          pthread_mutex_lock(&kLock);
          if (ok) { ackRevision = revision; ackCount = count; }
          if (desiredRevision == revision) desiredRevision = 0;
          pthread_mutex_unlock(&kLock);
        }
        if (!ok) syslog(LOG_NOTICE,
          "StrappyNotifications: modern %s failed or timed out",
          revision ? "badge" : "submission");
        [operationPool drain];
      }
    }
  } @catch (NSException *exception) {
    (void)exception;
    syslog(LOG_NOTICE,"StrappyNotifications: modern initialization exception");
  }
  pthread_mutex_lock(&kLock); accepting = 0; queued = 0;
  pthread_mutex_unlock(&kLock);
  [sound release]; [center release]; [pool drain];
  return NULL;
}
int XPDaemonNotificationsStart(void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  dlopen("/System/Library/Frameworks/UserNotifications.framework/UserNotifications",
    RTLD_NOW | RTLD_LOCAL);
  int modern = NSClassFromString(@"UNUserNotificationCenter") != Nil;
  if (modern && !started) {
    stopping = 0; accepting = 1; head = queued = 0;
    desiredRevision = desiredCount = ackRevision = ackCount = 0;
    started = pthread_create(&worker,NULL,XPWorker,NULL) == 0;
    if (!started) accepting = 0;
  }
  syslog(LOG_NOTICE,"StrappyNotifications: selected %s",modern ? "modern" : "legacy");
  [pool drain];
  return modern;
}
void XPDaemonNotificationsStop(void)
{
  if (!started) return;
  pthread_mutex_lock(&kLock); stopping = 1; accepting = 0;
  pthread_cond_signal(&kChanged); pthread_mutex_unlock(&kLock);
  pthread_join(worker,NULL); started = 0;
}
int XPDaemonNotificationsPost(long long run, const char *title, const char *body)
{
  int ok = 0;
  pthread_mutex_lock(&kLock);
  if (accepting && queued < kQueueLimit) {
    unsigned int slot = (head + queued) % kQueueLimit;
    alerts[slot].run = run;
    snprintf(alerts[slot].title,sizeof(alerts[slot].title),"%s",title);
    snprintf(alerts[slot].body,sizeof(alerts[slot].body),"%s",body);
    ++queued; ok = 1; pthread_cond_signal(&kChanged);
  }
  pthread_mutex_unlock(&kLock);
  return ok;
}
int XPDaemonNotificationsBadge(long long revision, long long count)
{
  pthread_mutex_lock(&kLock);
  int ok = ackRevision == revision && ackCount == count;
  if (!ok && accepting) {
    desiredRevision = revision; desiredCount = count;
    pthread_cond_signal(&kChanged);
  }
  pthread_mutex_unlock(&kLock);
  return ok;
}
