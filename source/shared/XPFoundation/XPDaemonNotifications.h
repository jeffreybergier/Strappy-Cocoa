#ifndef XP_DAEMON_NOTIFICATIONS_H
#define XP_DAEMON_NOTIFICATIONS_H
/* iOS-only MRC bridge. Lifecycle calls are serialized by the daemon; stop only
 * after producers join. Start returns class existence, not permission/display
 * success. No modern failure falls back to a second delivery route. */
int XPDaemonNotificationsStart(void);
void XPDaemonNotificationsStop(void);
/* Thread-safe; copies bounded UTF-8 previews supplied by the portable layer. */
int XPDaemonNotificationsPost(long long run, const char *title, const char *body);
/* Thread-safe latest-value queue; true means exact revision/count acknowledged.
 * Neither submission nor acknowledgment establishes visual presentation. */
int XPDaemonNotificationsBadge(long long revision, long long count);
#endif
