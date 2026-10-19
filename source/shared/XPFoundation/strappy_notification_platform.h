#ifndef XP_FOUNDATION_NOTIFICATION_PLATFORM_H
#define XP_FOUNDATION_NOTIFICATION_PLATFORM_H
/* Daemon lifecycle: start before producers, stop after producers/workers join.
 * Start selects modern iOS by class existence; other targets retain their
 * previous transport. No notification failure changes daemon readiness. */
void strappy_notification_platform_start(void);
void strappy_notification_platform_stop(void);
int strappy_notification_platform_modern(void);
/* Thread-safe bounded enqueue; copies strings before returning. */
int strappy_notification_platform_post(long long run, const char *title,
                                       const char *body);
/* Nonblocking latest-value submission. Returns true only when this exact
 * revision/count has completed successfully. Call again while pending. */
int strappy_notification_platform_badge(long long revision, long long count);
/* Borrowed process-lifetime legacy path, or NULL for modern/unsupported.
 * Only offline test builds permit an environment override. */
const char *strappy_notification_platform_socket(void);
#endif
