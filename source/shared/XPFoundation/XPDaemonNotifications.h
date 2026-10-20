#ifndef XP_DAEMON_NOTIFICATIONS_H
#define XP_DAEMON_NOTIFICATIONS_H
/* iOS-only MRC bridge. Lifecycle calls are serialized by the daemon; stop only
 * after producers join. Start begins asynchronous capability validation. Missing
 * capabilities/failed initialization select legacy before authorization. Denial,
 * timeout or submission failure never switches delivery routes. */
int XPDaemonNotificationsStart(void);
void XPDaemonNotificationsStop(void);
/* Thread-safe: includes initialization pending and permission-disabled modern.
 * False selects legacy; only its registered, capable provider opens the socket. */
int XPDaemonNotificationsModern(void);
/* Thread-safe; copies bounded UTF-8 previews supplied by the portable layer. */
int XPDaemonNotificationsPost(long long run, const char *title, const char *body);
/* Thread-safe latest-value queue; true means exact revision/count acknowledged.
 * Neither submission nor acknowledgment establishes visual presentation. */
int XPDaemonNotificationsBadge(long long revision, long long count);
#endif
