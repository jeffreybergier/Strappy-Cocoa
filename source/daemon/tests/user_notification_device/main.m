/* iOS 10+ foreground diagnostic. No daemon, account access, or installation.
 * Runtime invocations allow building with the shared iOS 8.4 SDK.
 * Uses only Strappy's identity and one fixed, removable diagnostic request. */
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#import <objc/runtime.h>

static NSString * const kBundle = @"com.altivecintelligence.Strappy";
static NSString * const kRequest = @"strappy.manual.un-probe";
static NSString * const kToneTopic = @"com.altivecintelligence.Strappy.manual-probe";

/* Object/NSInteger arguments and object results; runtime ABI is checked.
 * Callers own returned init/alloc objects. */
static id XPInvoke(id target, NSString *name, NSArray *arguments)
{
  SEL selector = NSSelectorFromString(name);
  NSMethodSignature *signature = [target methodSignatureForSelector:selector];
  if (!signature || [signature numberOfArguments] != [arguments count] + 2) {
    fprintf(stderr, "Unavailable selector: %s\n", [name UTF8String]);
    exit(2);
  }
  NSInvocation *call = [NSInvocation invocationWithMethodSignature:signature];
  [call setTarget:target];
  [call setSelector:selector];
  for (NSUInteger i = 0; i < [arguments count]; ++i) {
    const char *type = [signature getArgumentTypeAtIndex:i + 2];
    id value = [arguments objectAtIndex:i];
    if (type[0] == '@') {
      if (value == [NSNull null]) value = nil;
      [call setArgument:&value atIndex:i + 2];
    } else if (!strcmp(type, @encode(NSInteger)) &&
        [value isKindOfClass:[NSNumber class]]) {
      NSInteger integer = [value integerValue];
      [call setArgument:&integer atIndex:i + 2];
    } else {
      fprintf(stderr, "Unexpected argument type: %s\n", [name UTF8String]);
      exit(2);
    }
  }
  [call invoke];
  id result = nil;
  if ([signature methodReturnType][0] == '@') [call getReturnValue:&result];
  return result;
}

int main(int argc, char **argv)
{
  if (argc != 2 || (strcmp(argv[1], "settings") &&
      strcmp(argv[1], "send") && strcmp(argv[1], "clear") &&
      strcmp(argv[1], "authorize") && strcmp(argv[1], "badge-one") &&
      strcmp(argv[1], "badge-clear") && strcmp(argv[1], "inspect-sounds") &&
      strcmp(argv[1], "send-glass") && strcmp(argv[1], "send-custom") &&
      strcmp(argv[1], "inspect-tones") && strcmp(argv[1], "send-tone-glass") &&
      strcmp(argv[1], "tone-reset"))) {
    fprintf(stderr, "Usage: strappy-un-probe "
      "settings|send|send-glass|send-custom|send-tone-glass|tone-reset|clear|"
      "authorize|badge-one|badge-clear|inspect-sounds|inspect-tones\n");
    return 2;
  }
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  setbuf(stdout, NULL);
  if (!dlopen("/System/Library/Frameworks/UserNotifications.framework/"
      "UserNotifications", RTLD_NOW | RTLD_GLOBAL)) {
    fprintf(stderr, "UserNotifications load failed: %s\n", dlerror());
    return 2;
  }
  if (!strcmp(argv[1], "inspect-tones")) {
    dlopen("/System/Library/PrivateFrameworks/ToneLibrary.framework/ToneLibrary",
      RTLD_NOW | RTLD_GLOBAL);
    Class cls = NSClassFromString(@"TLToneManager");
    id manager = XPInvoke(cls, @"sharedToneManager", [NSArray array]);
    for (NSString *selector in [NSArray arrayWithObjects:
        @"nameForToneIdentifier:", @"filePathForToneIdentifier:", nil]) {
      id value = XPInvoke(manager, selector,
        [NSArray arrayWithObject:@"texttone:Glass"]);
      printf("Glass %s: %s\n", [selector UTF8String],
        value ? [[value description] UTF8String] : "none");
    }
    for (unsigned int kind = 0; kind < 2; ++kind) {
      unsigned int count = 0;
      Method *methods = class_copyMethodList(kind ? object_getClass(cls) : cls,
        &count);
      for (unsigned int i = 0; i < count; ++i) {
        const char *name = sel_getName(method_getName(methods[i]));
        if (strstr(name, "one") || strstr(name, "lert"))
          printf("%c %s %s\n", kind ? '+' : '-', name,
            method_getTypeEncoding(methods[i]));
      }
      free(methods);
    }
    [pool drain];
    return 0;
  }
  if (!strcmp(argv[1], "inspect-sounds")) {
    dlopen("/System/Library/Frameworks/MobileCoreServices.framework/"
      "MobileCoreServices", RTLD_NOW | RTLD_GLOBAL);
    id proxy = XPInvoke(NSClassFromString(@"LSApplicationProxy"),
      @"applicationProxyForIdentifier:", [NSArray arrayWithObject:kBundle]);
    id container = XPInvoke(proxy, @"dataContainerURL", [NSArray array]);
    printf("Strappy data container: %s\n", container ?
      [[container description] UTF8String] : "none");
    unsigned int count = 0;
    Method *methods = class_copyMethodList(object_getClass(
      NSClassFromString(@"UNNotificationSound")), &count);
    for (unsigned int i = 0; i < count; ++i)
      printf("%s %s\n", sel_getName(method_getName(methods[i])),
        method_getTypeEncoding(methods[i]));
    free(methods);
    [pool drain];
    return 0;
  }
  id center = XPInvoke([NSClassFromString(@"UNUserNotificationCenter") alloc],
    @"initWithBundleIdentifier:", [NSArray arrayWithObject:kBundle]);
  if (!center) return 2;
  BOOL toneGlass = !strcmp(argv[1], "send-tone-glass");
  if (toneGlass || !strcmp(argv[1], "tone-reset")) {
    dlopen("/System/Library/PrivateFrameworks/ToneLibrary.framework/ToneLibrary",
      RTLD_NOW | RTLD_GLOBAL);
    id manager = XPInvoke(NSClassFromString(@"TLToneManager"),
      @"sharedToneManager", [NSArray array]);
    XPInvoke(manager, @"setCurrentToneIdentifier:forAlertType:topic:",
      [NSArray arrayWithObjects:toneGlass ? (id)@"texttone:Glass" : [NSNull null],
        [NSNumber numberWithInteger:5], kToneTopic, nil]);
    id selected = XPInvoke(manager, @"currentToneIdentifierForAlertType:topic:",
      [NSArray arrayWithObjects:[NSNumber numberWithInteger:5], kToneTopic, nil]);
    printf("Diagnostic topic selected tone: %s\n",
      selected ? [[selected description] UTF8String] : "none");
    if (!toneGlass) {
      [center release];
      [pool drain];
      return 0;
    }
  }
  if (!strcmp(argv[1], "clear")) {
    NSArray *ids = [NSArray arrayWithObject:kRequest];
    XPInvoke(center, @"removePendingNotificationRequestsWithIdentifiers:",
      [NSArray arrayWithObject:ids]);
    XPInvoke(center, @"removeDeliveredNotificationsWithIdentifiers:",
      [NSArray arrayWithObject:ids]);
    puts("Requested removal of the diagnostic notification only.");
    [NSThread sleepForTimeInterval:1.0];
    [center release];
    [pool drain];
    return 0;
  }
  NSCondition *condition = [[NSCondition alloc] init];
  __block BOOL done = NO;
  __block int status = 0;
  void (^completion)(id) = [^(id result) {
    [condition lock];
    printf("Callback: %s\n", result ? [[result description] UTF8String] : "OK");
    if ([result isKindOfClass:[NSError class]]) status = 1;
    done = YES;
    [condition signal];
    [condition unlock];
  } copy];
  if (!strcmp(argv[1], "badge-one") || !strcmp(argv[1], "badge-clear")) {
    NSNumber *number = [NSNumber numberWithInt:
      !strcmp(argv[1], "badge-one") ? 1 : 0];
    XPInvoke(center, @"setBadgeNumber:withCompletionHandler:",
      [NSArray arrayWithObjects:number, completion, nil]);
  } else if (!strcmp(argv[1], "authorize")) {
    SEL selector = NSSelectorFromString(
      @"requestAuthorizationWithOptions:completionHandler:");
    NSMethodSignature *signature = [center methodSignatureForSelector:selector];
    if (!signature) return 2;
    NSInvocation *call = [NSInvocation invocationWithMethodSignature:signature];
    [call setTarget:center];
    [call setSelector:selector];
    NSUInteger options = 7; /* Badge (1), sound (2), alert (4). */
    void (^authorized)(BOOL, NSError *) = [^(BOOL granted, NSError *error) {
      printf("Authorization granted: %s\n", granted ? "yes" : "no");
      completion(error ? error : (granted ? nil :
        [NSError errorWithDomain:@"StrappyNotificationProbe" code:1
          userInfo:nil]));
    } copy];
    [call setArgument:&options atIndex:2];
    [call setArgument:&authorized atIndex:3];
    [call invoke];
    [authorized release];
  } else if (!strcmp(argv[1], "settings")) {
    XPInvoke(center, @"getNotificationSettingsWithCompletionHandler:",
      [NSArray arrayWithObject:completion]);
  } else {
    id content = [[NSClassFromString(@"UNMutableNotificationContent") alloc] init];
    XPInvoke(content, @"setTitle:", [NSArray arrayWithObject:@"Strappy manual test"]);
    XPInvoke(content, @"setBody:", [NSArray arrayWithObject:
      @"Standalone daemon-style notification probe. No prompt was sent."]);
    BOOL glass = !strcmp(argv[1], "send-glass");
    BOOL custom = !strcmp(argv[1], "send-custom");
    id sound = XPInvoke(NSClassFromString(@"UNNotificationSound"),
      (glass || custom) ? @"soundNamed:" : @"defaultSound", (glass || custom) ?
      [NSArray arrayWithObject:custom ? @"StrappyManualChime.wav" :
        @"/System/Library/Audio/UISounds/sms-received3.caf"] : [NSArray array]);
    if (toneGlass) {
      sound = [XPInvoke(NSClassFromString(@"UNNotificationSound"),
        @"soundWithAlertType:",
        [NSArray arrayWithObject:[NSNumber numberWithInteger:5]]) mutableCopy];
      XPInvoke(sound, @"setAlertTopic:", [NSArray arrayWithObject:kToneTopic]);
      [sound autorelease];
    }
    if (custom) {
      NSString *path = [[NSBundle mainBundle]
        pathForResource:@"StrappyManualChime" ofType:@"wav"];
      printf("Sender bundle: %s; custom file: %s\n",
        [[[NSBundle mainBundle] bundlePath] UTF8String],
        path ? [path UTF8String] : "none");
    }
    XPInvoke(content, @"setSound:", [NSArray arrayWithObject:sound]);
    id request = XPInvoke(NSClassFromString(@"UNNotificationRequest"),
      @"requestWithIdentifier:content:trigger:",
      [NSArray arrayWithObjects:kRequest, content, [NSNull null], nil]);
    XPInvoke(center, @"addNotificationRequest:withCompletionHandler:",
      [NSArray arrayWithObjects:request, completion, nil]);
    [content release];
  }
  NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:15.0];
  [condition lock];
  while (!done && [condition waitUntilDate:deadline]) {}
  if (!done) {
    fprintf(stderr, "Timed out waiting for completion.\n");
    _Exit(3); /* Outstanding callback owns captured state until process exit. */
  }
  [condition unlock];
  [completion release];
  [condition release];
  [center release];
  [pool drain];
  return status;
}
