/* iOS 6 SpringBoard-only diagnostic. All private selectors stay in this XP
 * boundary. The observer lives for the process lifetime; no borrowed callback
 * target is retained. It accepts only a fixed, payload-free test signal. */
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <dispatch/dispatch.h>
#include <notify.h>
#include <stdio.h>
#include <syslog.h>
#include "probe.h"

static void XPProbeLog(const char *event)
{
  FILE *file;
  syslog(LOG_NOTICE, "StrappyBulletinProbe: %s", event);
  /* The runner creates this mobile-owned staging directory. Never create
   * directories or change permissions from inside SpringBoard. */
  file = fopen(STRAPPY_BULLETIN_LOG, "a");
  if (file != NULL) {
    (void)fprintf(file, "%.3f %s\n", CFAbsoluteTimeGetCurrent(), event);
    (void)fclose(file);
  }
}

static id XPProbeObject(id target, NSString *selector)
{
  SEL action = NSSelectorFromString(selector);
  if (![target respondsToSelector:action]) return nil;
  return [target performSelector:action];
}

static BOOL XPProbeSetObject(id target, NSString *selector, id value)
{
  SEL action = NSSelectorFromString(selector);
  if (![target respondsToSelector:action]) return NO;
  [target performSelector:action withObject:value];
  return YES;
}

static BOOL XPProbeBool(id target, NSString *selector, BOOL *value)
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

static BOOL XPProbeSetBool(id target, NSString *selector, BOOL value)
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

static BOOL XPProbeDisplay(id controller, id observer, id bulletin,
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

/* Main-thread only. Capability checks deliberately limit this initial probe
 * to the iOS 6 controller path verified in gomadango's copied binaries. */
static BOOL XPProbePostBulletin(void)
{
  id away = XPProbeObject(NSClassFromString(@"SBAwayController"),
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
  if (!XPProbeBool(away, @"isLocked", &locked)) return NO;
  if (locked) {
    id view = XPProbeObject(away, @"awayView");
    controller = [view valueForKey:@"bulletinController"];
  } else {
    controller = XPProbeObject(NSClassFromString(@"SBBulletinBannerController"),
                               @"sharedInstance");
  }
  observer = [controller valueForKey:@"observer"];
  if (controller == nil || observer == nil) return NO;
  bulletin = [[[NSClassFromString(@"BBBulletin") alloc] init] autorelease];
  if (!XPProbeSetObject(bulletin, @"setSection:", identity) ||
      !XPProbeSetObject(bulletin, @"setSectionID:", identity) ||
      !XPProbeSetObject(bulletin, @"setBulletinID:", identifier) ||
      !XPProbeSetObject(bulletin, @"setBulletinVersionID:", identifier) ||
      !XPProbeSetObject(bulletin, @"setRecordID:", identifier) ||
      !XPProbeSetObject(bulletin, @"setPublisherBulletinID:", identifier) ||
      !XPProbeSetObject(bulletin, @"setTitle:", @"Strappy daemon test") ||
      !XPProbeSetObject(bulletin, @"setMessage:",
        @"strappyd sent this bulletin five seconds after launch.") ||
      !XPProbeSetObject(bulletin, @"setDate:", date) ||
      !XPProbeSetObject(bulletin, @"setLastInterruptDate:", date) ||
      !XPProbeSetBool(bulletin, @"setClearable:", YES)) return NO;
  if (![actionClass respondsToSelector:actionSelector]) return NO;
  action = [actionClass performSelector:actionSelector
                            withObject:identity withObject:nil];
  if (action == nil ||
      !XPProbeSetObject(bulletin, @"setDefaultAction:", action)) return NO;
  if (!XPProbeDisplay(controller, observer, bulletin, locked ? 8U : 2U))
    return NO;
  XPProbeLog(locked ? "dispatched lock-screen bulletin" : "dispatched banner");
  return YES;
}

static void XPProbeReceived(CFNotificationCenterRef center, void *observer,
                            CFStringRef name, const void *object,
                            CFDictionaryRef userInfo)
{
  (void)center;
  (void)observer;
  (void)name;
  (void)object;
  (void)userInfo;
  dispatch_async(dispatch_get_main_queue(), ^{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    static CFAbsoluteTime lastRequest = 0;
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    /* Diagnostic signals are public. Bound accidental repeated requests. */
    if (now - lastRequest >= 5.0) {
      lastRequest = now;
      XPProbeLog("received request");
      @try {
        if (XPProbePostBulletin()) {
          (void)notify_post(STRAPPY_BULLETIN_ACK);
        } else {
          XPProbeLog("unsupported controller or bulletin API; not dispatched");
        }
      } @catch (NSException *exception) {
        (void)exception;
        XPProbeLog("private API exception; not dispatched");
      }
    }
    [pool drain];
  });
}

__attribute__((constructor)) static void XPProbeInitialize(void)
{
  dispatch_async(dispatch_get_main_queue(), ^{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    if ([[[NSBundle mainBundle] bundleIdentifier]
          isEqualToString:@"com.apple.springboard"]) {
      CFNotificationCenterAddObserver(CFNotificationCenterGetDarwinNotifyCenter(),
        NULL, XPProbeReceived, CFSTR(STRAPPY_BULLETIN_REQUEST), NULL,
        CFNotificationSuspensionBehaviorDeliverImmediately);
      XPProbeLog("receiver ready");
    }
    [pool drain];
  });
}
