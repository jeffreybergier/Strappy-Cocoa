/* Foreground loader diagnostic. Loads a staged bundle in this process only;
 * only initializes the candidate, never registers, publishes or restarts SpringBoard. */
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  if (argc != 2 || strncmp(argv[1], "/var/mobile/tmp_altivec/", 23)) {
    fprintf(stderr, "Pass an absolute staged bundle path.\n");
    [pool drain]; return 2;
  }
  const char *symbols[] = {"objc_terminate", "objc_exception_rethrow",
    "objc_begin_catch", "objc_end_catch", "__objc_personality_v0"};
  for (unsigned int i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i)
    printf("%s: %s\n", symbols[i],
      dlsym(RTLD_DEFAULT, symbols[i]) ? "present" : "absent");
  void *framework = dlopen(
    "/System/Library/PrivateFrameworks/BulletinBoard.framework/BulletinBoard",
    RTLD_NOW | RTLD_GLOBAL);
  printf("BulletinBoard load: %s\n", framework ? "succeeded" : dlerror());
  printf("BBDataProviderAddBulletin: %s\n",
    dlsym(RTLD_DEFAULT, "BBDataProviderAddBulletin") ? "present" : "absent");
  printf("BBDataProviderWithdrawBulletinsWithRecordID: %s\n",
    dlsym(RTLD_DEFAULT, "BBDataProviderWithdrawBulletinsWithRecordID") ?
      "present" : "absent");
  NSBundle *bundle = [NSBundle bundleWithPath:
    [NSString stringWithUTF8String:argv[1]]];
  NSError *error = nil;
  if (![bundle loadAndReturnError:&error]) {
    fprintf(stderr, "NSBundle load failed: %s\n", [[error description] UTF8String]);
    [pool drain]; return 1;
  }
  Class cls = [bundle principalClass];
  Protocol *protocol = objc_getProtocol("BBDataProvider");
  printf("Principal class: %s; BBDataProvider conformance: %s\n",
    cls ? class_getName(cls) : "nil",
    cls && protocol && [cls conformsToProtocol:protocol] ? "yes" : "no");
  if (cls) {
    id provider = [[cls alloc] init];
    printf("Candidate init: %s; CF version: %.2f\n",
      provider ? "succeeded" : "nil", kCFCoreFoundationVersionNumber);
    [provider release];
  }
  puts("No registration callback invoked; no receiver or bulletin started.");
  /* Leave Objective-C image teardown to process exit. */
  [pool drain];
  return 0;
}
