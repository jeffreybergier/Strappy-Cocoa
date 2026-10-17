#ifndef STRAPPY_NOTIFICATIONS_H
#define STRAPPY_NOTIFICATIONS_H

/* Fixed-size, versioned local datagram. No pointers or native-width integers.
 * Private mobile-owned socket; no payload is written to disk or logs. */
#define STRAPPY_NOTIFICATION_DIRECTORY "/var/mobile/Library/Application Support/StrappyAlerts"
#define STRAPPY_NOTIFICATION_SOCKET STRAPPY_NOTIFICATION_DIRECTORY "/receiver.sock"
#define STRAPPY_NOTIFICATION_TITLE_CHARACTERS 100
#define STRAPPY_NOTIFICATION_BODY_CHARACTERS 400

typedef struct strappy_notification_packet {
  char magic[4];
  char run[24];
  char title[401];
  char body[1601];
} strappy_notification_packet;

/* Thread-safe, best-effort nonblocking delivery after the run commits.
 * Borrows strings for this call only. A missing/full receiver never retries
 * the prompt or changes its outcome. Unsupported platforms do nothing.
 * state is a STRAPPY_RUN_* value; only completed/failed prompts notify. */
void strappy_notifications_finished(long long run, int state,
                                    const char *title, const char *response,
                                    const char *error);
/* Returns static, code-owned wording for recognized diagnostics. Unknown or
 * provider-supplied diagnostics produce only "Error"; never echo raw errors. */
const char *strappy_notifications_error_body(const char *error);
/* Portable transport entry point used by offline fixtures as well. */
int strappy_notifications_send(const char *socket_path, long long run,
                               const char *title, const char *body);
#endif
