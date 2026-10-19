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

/* Object-only arguments/results. Callers own returned init/alloc objects. */
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
    if ([signature getArgumentTypeAtIndex:i + 2][0] != '@') {
      fprintf(stderr, "Unexpected non-object argument: %s\n", [name UTF8String]);
      exit(2);
    }
    id value = [arguments objectAtIndex:i];
    if (value == [NSNull null]) value = nil;
    [call setArgument:&value atIndex:i + 2];
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
      strcmp(argv[1], "send-glass"))) {
    fprintf(stderr, "Usage: strappy-un-probe "
      "settings|send|send-glass|clear|authorize|badge-one|badge-clear|inspect-sounds\n");
    return 2;
  }
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  setbuf(stdout, NULL);
  if (!dlopen("/System/Library/Frameworks/UserNotifications.framework/"
      "UserNotifications", RTLD_NOW | RTLD_GLOBAL)) {
    fprintf(stderr, "UserNotifications load failed: %s\n", dlerror());
    return 2;
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
    id sound = XPInvoke(NSClassFromString(@"UNNotificationSound"),
      glass ? @"soundNamed:" : @"defaultSound", glass ?
      [NSArray arrayWithObject:
        @"/System/Library/Audio/UISounds/sms-received3.caf"] : [NSArray array]);
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
