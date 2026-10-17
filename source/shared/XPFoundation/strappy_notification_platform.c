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

const char *strappy_notification_platform_socket(void)
{
#if defined(STRAPPY_ENABLE_LOOPBACK_HTTP_TESTS)
  return getenv("STRAPPY_TEST_NOTIFICATION_SOCKET");
#elif defined(__APPLE__) && TARGET_OS_IPHONE
  return STRAPPY_NOTIFICATION_SOCKET;
#else
  return NULL;
#endif
}
