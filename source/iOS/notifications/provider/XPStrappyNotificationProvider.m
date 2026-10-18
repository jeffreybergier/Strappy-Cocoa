/* Native iOS 6 BulletinBoard provider, MRC. Server owns the provider.
 * Publication/withdrawal and socket lifecycle run on the main thread. Queries
 * may arrive on other threads: synchronize history; publish outside the lock.
 * History is memory-only, bounded to 32 bulletins and 256 recent run IDs.
 * No user content is logged or persisted. Native lifecycle validation pending.
 */
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include "../../../daemon/strappy_notifications.h"

/* Deliberately cannot take the production receiver's socket in this milestone. */
#ifndef XP_PROVIDER_DIRECTORY
#define XP_PROVIDER_DIRECTORY STRAPPY_NOTIFICATION_DIRECTORY
#define XP_PROVIDER_SOCKET XP_PROVIDER_DIRECTORY "/provider-candidate.sock"
#define XP_PROVIDER_LOCK XP_PROVIDER_SOCKET ".lock"
#endif
static NSString *const kSection = @"com.altivecintelligence.Strappy";
static const NSUInteger kHistoryLimit = 32;
static const NSUInteger kRecentLimit = 256;
typedef void (*XPAddBulletin)(id, id);
typedef void (*XPWithdrawBulletin)(id, NSString *);

static BOOL XPSet(id target, NSString *name, id value)
{
  SEL selector = NSSelectorFromString(name);
  if (![target respondsToSelector:selector]) return NO;
  [target performSelector:selector withObject:value];
  return YES;
}

static BOOL XPSetClearable(id target)
{
  SEL selector = NSSelectorFromString(@"setClearable:");
  NSMethodSignature *signature = [target methodSignatureForSelector:selector];
  NSUInteger size = 0;
  BOOL value = YES;
  if (!signature || [signature numberOfArguments] != 3) return NO;
  NSGetSizeAndAlignment([signature getArgumentTypeAtIndex:2], &size, NULL);
  if (size != sizeof(value)) return NO;
  NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:target];
  [invocation setSelector:selector];
  [invocation setArgument:&value atIndex:2];
  [invocation invoke];
  return YES;
}

static id XPGet(id target, NSString *name)
{
  SEL selector = NSSelectorFromString(name);
  return [target respondsToSelector:selector] ?
    [target performSelector:selector] : nil;
}

/* Main-thread diagnostic only: reserved sender IDs 4..99 set literal badges.
 * This is not unread-count policy and must not migrate to daemon routing. */
static void XPSetTestBadge(const char *run)
{
  if (strlen(run) != 19 || strncmp(run, "90000000000000000", 17)) return;
  int number = (run[17] - '0') * 10 + run[18] - '0';
  if (number < 4 || number > 99) return;
  @try {
    id controller = XPGet(NSClassFromString(@"SBIconController"), @"sharedInstance");
    id model = XPGet(controller, @"model");
    SEL selector = NSSelectorFromString(@"applicationIconForDisplayIdentifier:");
    id icon = [model respondsToSelector:selector] ?
      [model performSelector:selector withObject:kSection] : nil;
    NSString *badge = [NSString stringWithFormat:@"%d", number];
    if (!XPSet(icon, @"setBadge:", badge)) {
      syslog(LOG_NOTICE, "StrappyProvider: test badge API unavailable");
      return;
    }
    BOOL matches = [[XPGet(icon, @"badgeNumberOrString") description]
      isEqualToString:badge];
    syslog(LOG_NOTICE, "StrappyProvider: test badge %d set; readback matches=%d",
      number, matches ? 1 : 0);
  } @catch (NSException *exception) {
    (void)exception;
    syslog(LOG_NOTICE, "StrappyProvider: test badge update raised exception");
  }
}

@interface XPStrappyNotificationProvider : NSObject {
  NSMutableArray *history_;
  NSMutableArray *recent_;
  int lockFD_;
  CFSocketRef receiver_;
  CFRunLoopSourceRef source_;
  dev_t socketDevice_;
  ino_t socketInode_;
  id sound_;
  BOOL soundAttempted_;
  XPAddBulletin add_;
  XPWithdrawBulletin withdraw_;
}
/* Only dataProviderDidLoad activates IPC, after server registration. */
- (void)startReceiving;
- (void)receivePackets;
/* Main-thread teardown: invalidates the borrowed callback target first. */
- (void)stopReceiving;
- (void)acceptPacket:(const strappy_notification_packet *)packet;
/* Main-thread lazy sound creation; returned object is borrowed. */
- (id)notificationSound;
@end

static void XPReceive(CFSocketRef socket, CFSocketCallBackType type,
  CFDataRef address, const void *data, void *info)
{
  (void)socket; (void)type; (void)address; (void)data;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  @try { [(XPStrappyNotificationProvider *)info receivePackets]; }
  @catch (NSException *exception) {
    (void)exception;
    syslog(LOG_NOTICE, "StrappyProvider: private API exception");
  }
  [pool drain];
}

@implementation XPStrappyNotificationProvider
+ (void)load;
{
  Protocol *protocol = objc_getProtocol("BBDataProvider");
  if (protocol) class_addProtocol(self, protocol);
}

- (id)init;
{
  self = [super init];
  if (!self) return nil;
  lockFD_ = -1;
  if (kCFCoreFoundationVersionNumber < 793.0 ||
      kCFCoreFoundationVersionNumber >= 847.0) {
    [self release]; return nil;
  }
  add_ = (XPAddBulletin)dlsym(RTLD_DEFAULT, "BBDataProviderAddBulletin");
  withdraw_ = (XPWithdrawBulletin)dlsym(RTLD_DEFAULT,
    "BBDataProviderWithdrawBulletinsWithRecordID");
  if (!add_ || !withdraw_) { [self release]; return nil; }
  history_ = [[NSMutableArray alloc] init];
  recent_ = [[NSMutableArray alloc] init];
  return self;
}

- (NSString *)sectionIdentifier;
{
  return kSection;
}

- (NSString *)sectionDisplayName;
{
  return @"Strappy";
}

- (id)defaultSectionInfo;
{
  Class cls = NSClassFromString(@"BBSectionInfo");
  SEL selector = NSSelectorFromString(@"defaultSectionInfoForType:");
  NSMethodSignature *signature = [cls methodSignatureForSelector:selector];
  NSUInteger size = 0;
  unsigned int type = 0;
  id info = nil;
  if (!signature || [signature numberOfArguments] != 3 ||
      [signature methodReturnLength] != sizeof(id)) return nil;
  NSGetSizeAndAlignment([signature getArgumentTypeAtIndex:2], &size, NULL);
  if (size != sizeof(type)) return nil;
  NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:cls];
  [invocation setSelector:selector];
  [invocation setArgument:&type atIndex:2];
  [invocation invoke];
  [invocation getReturnValue:&info];
  syslog(LOG_NOTICE, "StrappyProvider: default section requested");
  return XPSet(info, @"setSectionID:", kSection) ? info : nil;
}

- (NSArray *)sortDescriptors;
{
  return [NSArray arrayWithObject:[[[NSSortDescriptor alloc]
    initWithKey:@"date" ascending:NO] autorelease]];
}

/* Cleared info is provider-owned, property-list-safe record IDs. iOS persists
 * this opaque value and passes it back in lastCleared. Support both optional
 * callback forms from the historical BBDataProvider interface. */
- (id)clearedInfoForBulletins:(NSArray *)bulletins lastClearedInfo:(id)info;
{
  syslog(LOG_NOTICE, "StrappyProvider: clear callback count=%lu",
    (unsigned long)[bulletins count]);
  NSMutableArray *cleared = [NSMutableArray array];
  @synchronized (self) {
    if ([info isKindOfClass:[NSArray class]]) {
      for (id identifier in info) {
        if ([identifier isKindOfClass:[NSString class]] &&
            [identifier length] <= 96 && ![cleared containsObject:identifier]) {
          [cleared addObject:identifier];
          if ([cleared count] > kRecentLimit) [cleared removeObjectAtIndex:0];
        }
      }
    }
    for (id bulletin in bulletins) {
      id identifier = XPGet(bulletin, @"recordID");
      if (![identifier isKindOfClass:[NSString class]] ||
          [identifier length] > 96) continue;
      if (![cleared containsObject:identifier]) [cleared addObject:identifier];
      if ([cleared count] > kRecentLimit) [cleared removeObjectAtIndex:0];
      for (NSInteger i = (NSInteger)[history_ count] - 1; i >= 0; --i) {
        if ([[[history_ objectAtIndex:(NSUInteger)i] objectForKey:@"id"]
              isEqual:identifier]) [history_ removeObjectAtIndex:(NSUInteger)i];
      }
    }
  }
  return cleared;
}

- (id)clearedInfoForBulletins:(NSArray *)bulletins;
{
  return [self clearedInfoForBulletins:bulletins lastClearedInfo:nil];
}

- (NSArray *)bulletinsFilteredBy:(unsigned int)filter
  count:(unsigned int)count lastCleared:(id)cleared;
{
  /* All our records are ordinary completion alerts (no custom filter groups).
   * Unknown cleared-info formats fail closed instead of resurrecting alerts. */
  syslog(LOG_NOTICE, "StrappyProvider: list filter=%u count=%u cleared=%s",
    filter, count, !cleared ? "none" :
      [cleared isKindOfClass:[NSArray class]] ? "array" : "unknown");
  NSMutableArray *result = [NSMutableArray array];
  @synchronized (self) {
    if (cleared && ![cleared isKindOfClass:[NSArray class]]) return result;
    for (NSInteger i = (NSInteger)[history_ count] - 1; i >= 0; --i) {
      NSDictionary *entry = [history_ objectAtIndex:(NSUInteger)i];
      if ([cleared containsObject:[entry objectForKey:@"id"]]) {
        [history_ removeObjectAtIndex:(NSUInteger)i];
      } else if ([result count] < count) {
        [result addObject:[entry objectForKey:@"bulletin"]];
      }
    }
  }
  return result;
}

- (void)dataProviderDidLoad;
{
  syslog(LOG_NOTICE, "StrappyProvider: dataProviderDidLoad");
  [self performSelectorOnMainThread:@selector(startReceiving)
    withObject:nil waitUntilDone:NO];
}

- (id)notificationSound;
{
  if (soundAttempted_) return sound_;
  soundAttempted_ = YES;
  /* Built-in iOS Glass alert, used to isolate custom-resource playback.
   * System-owned IDs must not be disposed by this provider. */
  SystemSoundID soundID = 1009;
  Class cls = NSClassFromString(@"BBSound");
  SEL selector = NSSelectorFromString(@"alertSoundWithSystemSoundID:");
  NSMethodSignature *signature = [cls methodSignatureForSelector:selector];
  NSUInteger size = 0;
  if (!signature || [signature numberOfArguments] != 3 ||
      [signature methodReturnLength] != sizeof(id)) return nil;
  NSGetSizeAndAlignment([signature getArgumentTypeAtIndex:2], &size, NULL);
  if (size != sizeof(soundID)) return nil;
  NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:cls];
  [invocation setSelector:selector];
  [invocation setArgument:&soundID atIndex:2];
  [invocation invoke];
  id sound = nil;
  [invocation getReturnValue:&sound];
  sound_ = [sound retain];
  return sound_;
}

- (void)acceptPacket:(const strappy_notification_packet *)packet;
{
  if (memcmp(packet->magic, "STN1", 4) ||
      !memchr(packet->run, 0, sizeof(packet->run)) ||
      !memchr(packet->title, 0, sizeof(packet->title)) ||
      !memchr(packet->body, 0, sizeof(packet->body))) return;
  /* Canonical positive decimal run IDs only. Bound to signed 64-bit range. */
  size_t length = strlen(packet->run);
  if (!length || length > 19 || packet->run[0] == '0') return;
  for (size_t i = 0; i < length; ++i)
    if (packet->run[i] < '0' || packet->run[i] > '9') return;
  if (length == 19 && strcmp(packet->run, "9223372036854775807") > 0) return;
  NSString *title = [NSString stringWithUTF8String:packet->title];
  NSString *body = [NSString stringWithUTF8String:packet->body];
  if (!title || !body) return;
  NSString *identifier = [NSString stringWithFormat:@"%@.run.%s",
    kSection, packet->run];
  @synchronized (self) {
    if ([recent_ containsObject:identifier]) return;
  }
  id request = [[[NSClassFromString(@"BBBulletinRequest") alloc] init] autorelease];
  Class actionClass = NSClassFromString(@"BBAction");
  SEL actionSelector = NSSelectorFromString(@"actionWithLaunchBundleID:callblock:");
  if (![actionClass respondsToSelector:actionSelector]) return;
  id action = [actionClass performSelector:actionSelector
    withObject:kSection withObject:nil];
  if (!action || !XPSet(request, @"setSectionID:", kSection) ||
      !XPSet(request, @"setBulletinID:", identifier) ||
      !XPSet(request, @"setPublisherBulletinID:", identifier) ||
      !XPSet(request, @"setRecordID:", identifier) ||
      !XPSet(request, @"setTitle:", title) ||
      !XPSet(request, @"setMessage:", body) ||
      !XPSet(request, @"setDate:", [NSDate date]) ||
      !XPSet(request, @"setDefaultAction:", action) ||
      !XPSetClearable(request)) return;
  id sound = [self notificationSound];
  if (sound && XPSet(request, @"setSound:", sound))
    syslog(LOG_NOTICE, "StrappyProvider: built-in sound 1009 attached");
  NSString *evicted = nil;
  @synchronized (self) {
    [recent_ addObject:identifier];
    if ([recent_ count] > kRecentLimit) [recent_ removeObjectAtIndex:0];
    [history_ addObject:[NSDictionary dictionaryWithObjectsAndKeys:
      identifier, @"id", request, @"bulletin", nil]];
    if ([history_ count] > kHistoryLimit) {
      evicted = [[[history_ objectAtIndex:0] objectForKey:@"id"] retain];
      [history_ removeObjectAtIndex:0];
    }
  }
  @try {
    if (evicted) withdraw_(self, evicted);
    add_(self, request);
    syslog(LOG_NOTICE, "StrappyProvider: publication returned");
    XPSetTestBadge(packet->run);
  } @catch (NSException *exception) {
    /* Keep the recent ID: an exception may occur after publication. */
    [self clearedInfoForBulletins:[NSArray arrayWithObject:request]];
    @throw;
  } @finally {
    [evicted release];
  }
}

- (void)receivePackets;
{
  for (unsigned int i = 0; receiver_ && i < 8; ++i) {
    unsigned char bytes[sizeof(strappy_notification_packet) + 1];
    ssize_t size = recv(CFSocketGetNative(receiver_), bytes, sizeof(bytes), 0);
    if (size < 0) break;
    if (size != sizeof(strappy_notification_packet)) continue;
    strappy_notification_packet packet;
    memcpy(&packet, bytes, sizeof(packet));
    [self acceptPacket:&packet];
  }
}

- (void)startReceiving;
{
  if (receiver_) return;
  struct stat status;
  struct sockaddr_un address;
  int fd, flags, receiveSpace = 128 * 1024;
  if (mkdir(XP_PROVIDER_DIRECTORY, 0700) && errno != EEXIST) return;
  if (lstat(XP_PROVIDER_DIRECTORY, &status) ||
      !S_ISDIR(status.st_mode) || status.st_uid != geteuid() ||
      (status.st_mode & 077)) return;
  /* A lifetime lock permits recovery after a SpringBoard crash without
   * stealing another provider instance's live socket. Never touch production. */
  int candidateLock = open(XP_PROVIDER_LOCK,
    O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
  if (candidateLock < 0) return;
  if (fstat(candidateLock, &status) || !S_ISREG(status.st_mode) ||
      status.st_uid != geteuid() || (status.st_mode & 077) ||
      fcntl(candidateLock, F_SETFD, FD_CLOEXEC) ||
      flock(candidateLock, LOCK_EX | LOCK_NB)) {
    close(candidateLock); return;
  }
  lockFD_ = candidateLock;
  if (!lstat(XP_PROVIDER_SOCKET, &status)) {
    if (!S_ISSOCK(status.st_mode) || status.st_uid != geteuid() ||
        unlink(XP_PROVIDER_SOCKET)) { [self stopReceiving]; return; }
  } else if (errno != ENOENT) { [self stopReceiving]; return; }
  fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0) { [self stopReceiving]; return; }
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  if (strlen(XP_PROVIDER_SOCKET) >= sizeof(address.sun_path)) {
    close(fd); [self stopReceiving]; return;
  }
  strcpy(address.sun_path, XP_PROVIDER_SOCKET);
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) ||
      fcntl(fd, F_SETFD, FD_CLOEXEC) ||
      bind(fd, (struct sockaddr *)&address, sizeof(address))) {
    close(fd); [self stopReceiving]; return;
  }
  if (lstat(XP_PROVIDER_SOCKET, &status)) {
    close(fd); [self stopReceiving]; return;
  }
  socketDevice_ = status.st_dev;
  socketInode_ = status.st_ino;
  if (chmod(XP_PROVIDER_SOCKET, 0600)) {
    close(fd); [self stopReceiving]; return;
  }
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receiveSpace, sizeof(receiveSpace));
  CFSocketContext context = {0, self, NULL, NULL, NULL};
  receiver_ = CFSocketCreateWithNative(kCFAllocatorDefault, fd,
    kCFSocketReadCallBack, XPReceive, &context);
  if (!receiver_) { close(fd); [self stopReceiving]; return; }
  source_ = CFSocketCreateRunLoopSource(kCFAllocatorDefault, receiver_, 0);
  if (!source_) { [self stopReceiving]; return; }
  CFRunLoopAddSource(CFRunLoopGetMain(), source_, kCFRunLoopCommonModes);
  syslog(LOG_NOTICE, "StrappyProvider: candidate receiver ready");
}

- (void)stopReceiving;
{
  if (source_) {
    CFRunLoopSourceInvalidate(source_);
    CFRelease(source_); source_ = NULL;
  }
  if (receiver_) {
    CFSocketInvalidate(receiver_);
    CFRelease(receiver_); receiver_ = NULL;
  }
  struct stat status;
  if (socketInode_ && !lstat(XP_PROVIDER_SOCKET, &status) &&
      S_ISSOCK(status.st_mode) && status.st_uid == geteuid() &&
      status.st_dev == socketDevice_ && status.st_ino == socketInode_)
    unlink(XP_PROVIDER_SOCKET);
  socketInode_ = 0;
  if (lockFD_ >= 0) { close(lockFD_); lockFD_ = -1; }
}

- (void)dealloc;
{
  [self stopReceiving];
  [history_ release];
  [recent_ release];
  [sound_ release];
  [super dealloc];
}
@end
