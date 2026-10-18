/* Fixed diagnostic commands only. Posting is not proof of visible badge state. */
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
  if (argc != 2 || (strcmp(argv[1], "set8") && strcmp(argv[1], "clear") &&
      strcmp(argv[1], "restore"))) return 2;
  CFStringRef name = CFStringCreateWithFormat(kCFAllocatorDefault, NULL,
    CFSTR("com.altivecintelligence.strappy.badge-probe/%s"), argv[1]);
  if (!name) return 1;
  CFNotificationCenterPostNotification(CFNotificationCenterGetDarwinNotifyCenter(),
    name, NULL, NULL, true);
  CFRelease(name);
  printf("Posted badge-only action %s; check syslog and icon.\n", argv[1]);
  return 0;
}
