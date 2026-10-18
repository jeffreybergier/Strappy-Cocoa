/* Temporary SpringBoard-only badge experiment. No bulletin publication.
 * Main-thread callbacks own the saved badge; restore before package removal.
 * The receiver is process-lifetime and is unloaded only by restarting SpringBoard.
 */
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <dispatch/dispatch.h>
#include <syslog.h>

static id XPGet(id target, NSString *name)
{
  SEL selector = NSSelectorFromString(name);
  return [target respondsToSelector:selector] ?
    [target performSelector:selector] : nil;
}

@interface XPBadgeProbe : NSObject {
  id original_;
  BOOL captured_;
}
- (void)apply:(NSString *)action;
@end

@implementation XPBadgeProbe
- (void)apply:(NSString *)action;
{
  @try {
    id model = XPGet(XPGet(NSClassFromString(@"SBIconController"),
      @"sharedInstance"), @"model");
    SEL lookup = NSSelectorFromString(@"applicationIconForDisplayIdentifier:");
    id icon = [model respondsToSelector:lookup] ?
      [model performSelector:lookup
        withObject:@"com.altivecintelligence.Strappy"] : nil;
    SEL setter = NSSelectorFromString(@"setBadge:");
    if (![icon respondsToSelector:setter] ||
        ![icon respondsToSelector:NSSelectorFromString(@"badgeNumberOrString")]) {
      syslog(LOG_NOTICE, "StrappyBadgeProbe: badge API unavailable"); return;
    }
    if (!captured_) {
      if (![action isEqualToString:@"set8"]) {
        syslog(LOG_NOTICE, "StrappyBadgeProbe: set8 must capture baseline first");
        return;
      }
      original_ = [XPGet(icon, @"badgeNumberOrString") copy];
      captured_ = YES;
      syslog(LOG_NOTICE, "StrappyBadgeProbe: baseline=%s",
        original_ ? [[original_ description] UTF8String] : "none");
    }
    id value = [action isEqualToString:@"set8"] ? @"8" :
      ([action isEqualToString:@"restore"] ? original_ : nil);
    [icon performSelector:setter withObject:value];
    id actual = XPGet(icon, @"badgeNumberOrString");
    syslog(LOG_NOTICE, "StrappyBadgeProbe: action=%s readback=%s; no bulletin sent",
      [action UTF8String], actual ? [[actual description] UTF8String] : "none");
  } @catch (NSException *exception) {
    (void)exception;
    syslog(LOG_NOTICE, "StrappyBadgeProbe: badge API exception");
  }
}
- (void)dealloc;
{
  [original_ release];
  [super dealloc];
}
@end

static XPBadgeProbe *kReceiver; /* Owned for the SpringBoard process lifetime. */

static void XPReceive(CFNotificationCenterRef center, void *observer,
  CFStringRef name, const void *object, CFDictionaryRef info)
{
  (void)center; (void)object; (void)info;
  NSString *action = [(NSString *)name lastPathComponent];
  [(XPBadgeProbe *)observer performSelectorOnMainThread:@selector(apply:)
    withObject:action waitUntilDone:NO];
}

__attribute__((constructor)) static void XPInitialize(void)
{
  dispatch_async(dispatch_get_main_queue(), ^{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    if (kCFCoreFoundationVersionNumber >= 793.0 &&
        kCFCoreFoundationVersionNumber < 847.0 &&
        [[[NSBundle mainBundle] bundleIdentifier]
          isEqualToString:@"com.apple.springboard"]) {
      kReceiver = [[XPBadgeProbe alloc] init];
      /* Intentional process-lifetime owner; no code/image unload before restart. */
      for (NSString *action in [NSArray arrayWithObjects:@"set8", @"clear", @"restore", nil]) {
        NSString *name = [@"com.altivecintelligence.strappy.badge-probe/"
          stringByAppendingString:action];
        CFNotificationCenterAddObserver(CFNotificationCenterGetDarwinNotifyCenter(),
          kReceiver, XPReceive, (CFStringRef)name, NULL,
          CFNotificationSuspensionBehaviorDeliverImmediately);
      }
      syslog(LOG_NOTICE, "StrappyBadgeProbe: ready; no badge change or bulletin");
    }
    [pool drain];
  });
}
