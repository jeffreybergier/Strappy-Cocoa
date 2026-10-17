/* SpringBoard-only MRC compatibility boundary. Private selectors stay here.
 * The run-loop source lives until SpringBoard exits. No app-state filtering;
 * the lock-state check selects the appropriate presentation controller. */
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <dispatch/dispatch.h>
#include "../../daemon/strappy_notifications.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>

static id XPStrappyBulletinObject(id target, NSString *selector)
{
  SEL action = NSSelectorFromString(selector);
  if (![target respondsToSelector:action]) return nil;
  return [target performSelector:action];
}

static BOOL XPStrappyBulletinSetObject(id target, NSString *selector, id value)
{
  SEL action = NSSelectorFromString(selector);
  if (![target respondsToSelector:action]) return NO;
  [target performSelector:action withObject:value];
  return YES;
}

static BOOL XPStrappyBulletinBool(id target, NSString *selector, BOOL *value)
{
  SEL action = NSSelectorFromString(selector);
  NSMethodSignature *signature;
  NSInvocation *invocation;
  if (![target respondsToSelector:action]) return NO;
  signature = [target methodSignatureForSelector:action];
  if ([signature numberOfArguments] != 2 ||
      [signature methodReturnLength] != sizeof(BOOL)) return NO;
  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:target];
  [invocation setSelector:action];
  [invocation invoke];
  [invocation getReturnValue:value];
  return YES;
}

static BOOL XPStrappyBulletinSetBool(id target, NSString *selector, BOOL value)
{
  SEL action = NSSelectorFromString(selector);
  NSMethodSignature *signature;
  NSInvocation *invocation;
  NSUInteger size = 0;
  if (![target respondsToSelector:action]) return NO;
  signature = [target methodSignatureForSelector:action];
  if ([signature numberOfArguments] != 3) return NO;
  NSGetSizeAndAlignment([signature getArgumentTypeAtIndex:2], &size, NULL);
  if (size != sizeof(value)) return NO;
  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:target];
  [invocation setSelector:action];
  [invocation setArgument:&value atIndex:2];
  [invocation invoke];
  return YES;
}

static BOOL XPStrappyBulletinDisplay(id controller, id observer, id bulletin,
                           unsigned int feed)
{
  SEL action = NSSelectorFromString(@"observer:addBulletin:forFeed:");
  NSMethodSignature *signature;
  NSInvocation *invocation;
  NSUInteger size = 0;
  unsigned long long wideFeed = feed;
  if (![controller respondsToSelector:action]) return NO;
  signature = [controller methodSignatureForSelector:action];
  if ([signature numberOfArguments] != 5) return NO;
  NSGetSizeAndAlignment([signature getArgumentTypeAtIndex:4], &size, NULL);
  if (size != sizeof(feed) && size != sizeof(wideFeed)) return NO;
  invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:controller];
  [invocation setSelector:action];
  [invocation setArgument:&observer atIndex:2];
  [invocation setArgument:&bulletin atIndex:3];
  if (size == sizeof(feed)) [invocation setArgument:&feed atIndex:4];
  else [invocation setArgument:&wideFeed atIndex:4];
  [invocation invoke];
  return YES;
}

/* Main-thread only. This first backend uses the iOS 6 controller path
 * verified on gomadango. Missing private APIs fail without dispatch. */
static BOOL XPStrappyBulletinPostBulletin(NSString *title, NSString *body)
{
  id away = XPStrappyBulletinObject(NSClassFromString(@"SBAwayController"),
                         @"sharedAwayController");
  BOOL locked = NO;
  id controller;
  id observer;
  id bulletin;
  id action;
  Class actionClass = NSClassFromString(@"BBAction");
  SEL actionSelector = NSSelectorFromString(@"actionWithLaunchBundleID:callblock:");
  NSString *identity = @"com.altivecintelligence.Strappy";
  NSString *identifier = [[NSProcessInfo processInfo] globallyUniqueString];
  NSDate *date = [NSDate date];
  /* The iOS 6 presentation uses a single message. Include the session-name
   * prefix here rather than relying on a separate bulletin title. */
  NSString *message = [NSString stringWithFormat:@"%@: %@", title, body];
  if (!XPStrappyBulletinBool(away, @"isLocked", &locked)) return NO;
  if (locked) {
    id view = XPStrappyBulletinObject(away, @"awayView");
    controller = [view valueForKey:@"bulletinController"];
  } else {
    controller = XPStrappyBulletinObject(NSClassFromString(@"SBBulletinBannerController"),
                               @"sharedInstance");
  }
  observer = [controller valueForKey:@"observer"];
  if (controller == nil || observer == nil) return NO;
  bulletin = [[[NSClassFromString(@"BBBulletin") alloc] init] autorelease];
  if (!XPStrappyBulletinSetObject(bulletin, @"setSection:", identity) ||
      !XPStrappyBulletinSetObject(bulletin, @"setSectionID:", identity) ||
      !XPStrappyBulletinSetObject(bulletin, @"setBulletinID:", identifier) ||
      !XPStrappyBulletinSetObject(bulletin, @"setBulletinVersionID:", identifier) ||
      !XPStrappyBulletinSetObject(bulletin, @"setRecordID:", identifier) ||
      !XPStrappyBulletinSetObject(bulletin, @"setPublisherBulletinID:", identifier) ||
      !XPStrappyBulletinSetObject(bulletin, @"setMessage:",
        message) ||
      !XPStrappyBulletinSetObject(bulletin, @"setDate:", date) ||
      !XPStrappyBulletinSetObject(bulletin, @"setLastInterruptDate:", date) ||
      !XPStrappyBulletinSetBool(bulletin, @"setClearable:", YES)) return NO;
  if (![actionClass respondsToSelector:actionSelector]) return NO;
  action = [actionClass performSelector:actionSelector
                            withObject:identity withObject:nil];
  if (action == nil ||
      !XPStrappyBulletinSetObject(bulletin, @"setDefaultAction:", action)) return NO;
  if (!XPStrappyBulletinDisplay(controller, observer, bulletin, locked ? 8U : 2U))
    return NO;
  syslog(LOG_NOTICE, "StrappyNotifications: %s",
    locked ? "dispatched lock-screen bulletin" : "dispatched banner");
  return YES;
}

static void XPStrappyBulletinReceived(CFSocketRef socket,
  CFSocketCallBackType type, CFDataRef address, const void *data, void *info)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  unsigned int count;
  (void)type; (void)address; (void)data; (void)info;
  /* Bound work per main-loop callback. Each datagram represents one completed
   * prompt; unlike Darwin notifications, concurrent completions do not coalesce. */
  for (count = 0; count < 8; ++count) {
    unsigned char bytes[sizeof(strappy_notification_packet) + 1];
    strappy_notification_packet packet;
    ssize_t size = recv(CFSocketGetNative(socket), bytes, sizeof(bytes), 0);
    NSString *title, *body;
    if (size < 0) break;
    if (size != (ssize_t)sizeof(packet)) continue;
    memcpy(&packet, bytes, sizeof(packet));
    if (memcmp(packet.magic, "STN1", 4) ||
        !memchr(packet.run, 0, sizeof(packet.run)) ||
        !memchr(packet.title, 0, sizeof(packet.title)) ||
        !memchr(packet.body, 0, sizeof(packet.body))) continue;
    title = [NSString stringWithUTF8String:packet.title];
    body = [NSString stringWithUTF8String:packet.body];
    if (!title || !body) continue;
    @try {
      if (!XPStrappyBulletinPostBulletin(title, body))
        syslog(LOG_NOTICE, "StrappyNotifications: unsupported display API");
    } @catch (NSException *exception) {
      (void)exception;
      syslog(LOG_NOTICE, "StrappyNotifications: display API exception");
    }
  }
  [pool drain];
}

static void XPStrappyBulletinListen(void)
{
  struct stat status;
  struct sockaddr_un address;
  int fd, flags;
  int receiveSpace = 128 * 1024;
  CFSocketRef receiver;
  CFRunLoopSourceRef source;
  /* Never follow a substituted directory/socket or expose reply text to other
   * Unix users. Processes running as mobile share this trust boundary. */
  if (mkdir(STRAPPY_NOTIFICATION_DIRECTORY, 0700) != 0 && errno != EEXIST)
    return;
  if (lstat(STRAPPY_NOTIFICATION_DIRECTORY, &status) != 0 ||
      !S_ISDIR(status.st_mode) || status.st_uid != geteuid() ||
      (status.st_mode & 077) != 0) return;
  if (lstat(STRAPPY_NOTIFICATION_SOCKET, &status) == 0) {
    if (!S_ISSOCK(status.st_mode) || status.st_uid != geteuid() ||
        unlink(STRAPPY_NOTIFICATION_SOCKET) != 0) return;
  } else if (errno != ENOENT) return;
  fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0) return;
  /* The default iOS 6 receive buffer is only 4 KiB. Reserve a bounded burst
   * buffer for concurrent sessions; delivery remains best-effort if denied. */
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receiveSpace,
                   sizeof(receiveSpace));
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, STRAPPY_NOTIFICATION_SOCKET);
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0 ||
      fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 ||
      bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
      chmod(STRAPPY_NOTIFICATION_SOCKET, 0600) != 0) {
    close(fd);
    return;
  }
  receiver = CFSocketCreateWithNative(kCFAllocatorDefault, fd,
    kCFSocketReadCallBack, XPStrappyBulletinReceived, NULL);
  if (!receiver) { close(fd); return; }
  source = CFSocketCreateRunLoopSource(kCFAllocatorDefault, receiver, 0);
  if (source) {
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
    CFRelease(source);
    syslog(LOG_NOTICE, "StrappyNotifications: receiver ready");
  } else {
    CFSocketInvalidate(receiver);
  }
  CFRelease(receiver);
}

__attribute__((constructor)) static void XPStrappyBulletinInitialize(void)
{
  dispatch_async(dispatch_get_main_queue(), ^{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    if ([[[NSBundle mainBundle] bundleIdentifier]
          isEqualToString:@"com.apple.springboard"])
      XPStrappyBulletinListen();
    [pool drain];
  });
}
