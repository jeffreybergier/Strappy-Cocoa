#ifndef XP_FOUNDATION_NOTIFICATION_PLATFORM_H
#define XP_FOUNDATION_NOTIFICATION_PLATFORM_H
/* Borrowed process-lifetime path, or NULL on unsupported platforms.
 * Only offline test builds permit an environment override. */
const char *strappy_notification_platform_socket(void);
#endif
