/* Native iOS 6 provider experiment. Private APIs remain in this XP boundary.
 * The server owns this provider. Owned bulletins are retained under MRC.
 * Darwin callbacks enqueue main-thread work; unregister before teardown.
 * No publication at load time, network, production data, or Substrate hooks.
 */
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include "probe.h"

typedef void (*XPAddBulletin)(id, id);
typedef void (*XPWithdrawBulletin)(id, NSString *);

static void XPLog(const char *event)
{
  int fd = open(XP_PROVIDER_STAGE "/receiver.log",
    O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW, 0600);
  struct stat st;
  syslog(LOG_NOTICE, "StrappyProviderProbe: %s", event);
  if (fd < 0) return;
  if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size < 65536) {
    char line[256];
    int size = snprintf(line, sizeof(line), "%.3f %s\n",
      CFAbsoluteTimeGetCurrent(), event);
    if (size > 0 && size < (int)sizeof(line)) (void)write(fd, line, size);
  }
  close(fd);
}

static BOOL XPSet(id object, NSString *name, id value)
{
  SEL selector = NSSelectorFromString(name);
  if (![object respondsToSelector:selector]) return NO;
  [object performSelector:selector withObject:value];
  return YES;
}

@interface XPStrappyProviderProbe : NSObject {
  id bulletin_;
  BOOL attempted_;
  BOOL observing_;
}
- (void)sendProbe;
- (void)clearProbe;
@end

static void XPReceive(CFNotificationCenterRef center, void *observer,
  CFStringRef name, const void *object, CFDictionaryRef info)
{
  (void)center;
  (void)object;
  (void)info;
  XPStrappyProviderProbe *provider = (XPStrappyProviderProbe *)observer;
  SEL action = CFEqual(name, CFSTR(XP_PROVIDER_SEND)) ?
    @selector(sendProbe) : @selector(clearProbe);
  [provider performSelectorOnMainThread:action withObject:nil
    waitUntilDone:NO];
}

@implementation XPStrappyProviderProbe

+ (void)load;
{
  /* Adopt the system's actual protocol, avoiding an obsolete copied definition.
   * Registration must precede the loader's conformance check. */
  Protocol *protocol = objc_getProtocol("BBDataProvider");
  if (protocol != nil && class_addProtocol(self, protocol))
    XPLog("class loaded; adopted BBDataProvider");
  else
    XPLog("class loaded; protocol unavailable or already adopted");
}

- (id)init;
{
  self = [super init];
  if (self == nil) return nil;
  if (kCFCoreFoundationVersionNumber < 793.0 ||
      kCFCoreFoundationVersionNumber >= 847.0) {
    XPLog("refusing initialization outside iOS 6");
    [self release];
    return nil;
  }
  CFNotificationCenterRef center = CFNotificationCenterGetDarwinNotifyCenter();
  CFNotificationCenterAddObserver(center, self, XPReceive,
    CFSTR(XP_PROVIDER_SEND), NULL, CFNotificationSuspensionBehaviorDeliverImmediately);
  CFNotificationCenterAddObserver(center, self, XPReceive,
    CFSTR(XP_PROVIDER_CLEAR), NULL, CFNotificationSuspensionBehaviorDeliverImmediately);
  observing_ = YES;
  XPLog("provider initialized; idle, awaiting explicit send");
  return self;
}

- (NSString *)sectionIdentifier;
{
  return @XP_PROVIDER_ID;
}

- (NSString *)sectionDisplayName;
{
  return @"Strappy Provider Probe";
}

- (id)defaultSectionInfo;
{
  Class infoClass = NSClassFromString(@"BBSectionInfo");
  SEL selector = NSSelectorFromString(@"defaultSectionInfoForType:");
  NSMethodSignature *signature = [infoClass methodSignatureForSelector:selector];
  NSUInteger size = 0;
  unsigned int type = 0;
  id info = nil;
  if (signature == nil || [signature numberOfArguments] != 3 ||
      [signature methodReturnLength] != sizeof(id)) return nil;
  NSGetSizeAndAlignment([signature getArgumentTypeAtIndex:2], &size, NULL);
  if (size != sizeof(type)) return nil;
  NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:infoClass];
  [invocation setSelector:selector];
  [invocation setArgument:&type atIndex:2];
  [invocation invoke];
  [invocation getReturnValue:&info];
  if (!XPSet(info, @"setSectionID:", [self sectionIdentifier])) return nil;
  XPLog("server requested default section settings");
  return info;
}

- (NSArray *)sortDescriptors;
{
  return [NSArray arrayWithObject:
    [[[NSSortDescriptor alloc] initWithKey:@"date" ascending:NO] autorelease]];
}

- (NSArray *)bulletinsFilteredBy:(unsigned int)filter
  count:(unsigned int)count lastCleared:(id)cleared;
{
  /* Never restore a bulletin after system clearing or a restart. */
  XPLog("server queried bulletin list");
  return [NSArray array];
}

- (void)dataProviderDidLoad;
{
  XPLog("dataProviderDidLoad");
}

- (void)sendProbe;
{
  XPAddBulletin add = (XPAddBulletin)dlsym(RTLD_DEFAULT,
    "BBDataProviderAddBulletin");
  Class requestClass = NSClassFromString(@"BBBulletinRequest");
  int fd;
  if (attempted_) { XPLog("duplicate send ignored"); return; }
  attempted_ = YES;
  if (add == NULL || requestClass == Nil) {
    XPLog("publication API missing"); return;
  }
  /* Persistent one-attempt guard survives a SpringBoard crash/restart.
   * Missing staging directory also disables publication. */
  fd = open(XP_PROVIDER_STAGE "/attempted", O_WRONLY | O_CREAT | O_EXCL |
    O_NOFOLLOW, 0600);
  if (fd < 0) { XPLog("attempt guard unavailable; refusing send"); return; }
  close(fd);
  id request = [[requestClass alloc] init];
  BOOL valid = XPSet(request, @"setSectionID:", [self sectionIdentifier]) &&
    XPSet(request, @"setBulletinID:", @XP_PROVIDER_ID ".fixed") &&
    XPSet(request, @"setPublisherBulletinID:", @XP_PROVIDER_ID ".fixed") &&
    XPSet(request, @"setRecordID:", @XP_PROVIDER_ID ".fixed") &&
    XPSet(request, @"setTitle:", @"Strappy Provider Probe") &&
    XPSet(request, @"setMessage:", @"One isolated native-provider test.") &&
    XPSet(request, @"setDate:", [NSDate date]);
  if (!valid) {
    XPLog("bulletin selectors missing; refusing send");
    [request release]; return;
  }
  bulletin_ = request;
  XPLog("calling BBDataProviderAddBulletin");
  add(self, bulletin_);
  XPLog("publication returned; display not confirmed");
}

- (void)clearProbe;
{
  XPWithdrawBulletin withdraw = (XPWithdrawBulletin)dlsym(RTLD_DEFAULT,
    "BBDataProviderWithdrawBulletinsWithRecordID");
  if (withdraw == NULL) { XPLog("withdraw API missing"); return; }
  withdraw(self, @XP_PROVIDER_ID ".fixed");
  [bulletin_ release];
  bulletin_ = nil;
  XPLog("withdraw returned; inspect phone");
}

- (void)dealloc;
{
  if (observing_) CFNotificationCenterRemoveEveryObserver(
    CFNotificationCenterGetDarwinNotifyCenter(), self);
  [bulletin_ release];
  [super dealloc];
}
@end
