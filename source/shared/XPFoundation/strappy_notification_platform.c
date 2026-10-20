/* Platform selection stays outside portable execution code. */
#include "strappy_notification_platform.h"
#include <stddef.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
#include <stdlib.h>
#endif
#include "../../daemon/strappy_notifications.h"

#if defined(__APPLE__) && TARGET_OS_IPHONE && !defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
#include "XPDaemonNotifications.h"

#endif

void strappy_notification_platform_start(void)
{
#if defined(__APPLE__) && TARGET_OS_IPHONE && !defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
  (void)XPDaemonNotificationsStart();
#endif
}
void strappy_notification_platform_stop(void)
{
#if defined(__APPLE__) && TARGET_OS_IPHONE && !defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
  XPDaemonNotificationsStop();
#endif
}
int strappy_notification_platform_modern(void)
{
#if defined(__APPLE__) && TARGET_OS_IPHONE && !defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
  return XPDaemonNotificationsModern();
#else
  return 0;
#endif
}
int strappy_notification_platform_post(long long run, const char *title,
                                       const char *body)
{
#if defined(__APPLE__) && TARGET_OS_IPHONE && !defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
  return XPDaemonNotificationsPost(run, title, body);
#else
  (void)run; (void)title; (void)body; return 0;
#endif
}
int strappy_notification_platform_badge(long long revision, long long count)
{
#if defined(__APPLE__) && TARGET_OS_IPHONE && !defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
  return XPDaemonNotificationsBadge(revision, count);
#else
  (void)revision; (void)count; return 0;
#endif
}

const char *strappy_notification_platform_socket(void)
{
#if defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
  return getenv("STRAPPY_TEST_NOTIFICATION_SOCKET");
#elif defined(__APPLE__) && TARGET_OS_IPHONE
  return XPDaemonNotificationsModern() ? NULL : STRAPPY_NOTIFICATION_SOCKET;
#else
  return NULL;
#endif
}
