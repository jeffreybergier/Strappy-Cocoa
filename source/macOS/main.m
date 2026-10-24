#import <AppKit/AppKit.h>
#import "AppDelegate.h"
#import "StrappyServiceController.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  if (argc == 2 && strcmp(argv[1], "--install-daemon") == 0) {
    fprintf(stderr, "Start Strappy normally to run its app-owned daemon.\n");
    [pool release];
    return 1;
  }
  if (argc == 2 && strcmp(argv[1], "--uninstall-daemon") == 0) {
    NSString *error = nil;
    BOOL success = [StrappyServiceController uninstallWithError:&error];
    if (!success) fprintf(stderr, "%s\n", [error UTF8String]);
    [pool release];
    return success ? 0 : 1;
  }
  NSApplication *app = [NSApplication sharedApplication];

  AppDelegate *delegate = [[[AppDelegate alloc] init] autorelease];
  [app setDelegate:delegate];
  [app run];

  [pool release];
  return 0;
}
