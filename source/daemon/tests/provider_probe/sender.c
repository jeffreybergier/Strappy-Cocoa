/* One-shot foreground sender. Posting is not proof of provider receipt/display. */
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
#include "probe.h"

int main(int argc, char **argv)
{
  CFStringRef name;
  if (argc != 2 || (strcmp(argv[1], "send") && strcmp(argv[1], "clear"))) {
    fprintf(stderr, "usage: strappy-provider-probe send|clear\n");
    return 2;
  }
  name = !strcmp(argv[1], "send") ? CFSTR(XP_PROVIDER_SEND) :
    CFSTR(XP_PROVIDER_CLEAR);
  CFNotificationCenterPostNotification(
    CFNotificationCenterGetDarwinNotifyCenter(), name, NULL, NULL, true);
  printf("Posted %s; inspect receiver.log and the phone for results.\n", argv[1]);
  return 0;
}
