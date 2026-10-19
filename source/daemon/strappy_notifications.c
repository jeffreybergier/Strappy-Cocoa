#include "strappy_notifications.h"
#include "../shared/XPFoundation/strappy_notification_platform.h"
#include "../shared/strappy_run.h"
#include "../shared/strappy_core.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>


/* Limit Unicode scalars without splitting a UTF-8 sequence. Wire buffers hold
 * four bytes per scalar plus NUL. Invalid input becomes an empty preview. */
static void strappy_notification_preview(char *out, const char *text,
                                         size_t characters)
{
  size_t bytes = 0, count = 0;
  if (!text) text = "";
  while (text[bytes] && count < characters) {
    unsigned char lead = (unsigned char)text[bytes];
    size_t width = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
    size_t i;
    for (i = 1; i < width; ++i) if (!text[bytes + i]) return;
    if (!strappy_utf8_validate(text + bytes, width)) return;
    bytes += width;
    ++count;
  }
  memcpy(out, text, bytes);
  out[bytes] = 0;
}

int strappy_notifications_send(const char *path, long long run,
                               const char *title, const char *body)
{
  strappy_notification_packet packet;
  struct sockaddr_un address;
  int fd, flags, ok = 0;
  if (!path || !*path || strlen(path) >= sizeof(address.sun_path)) return 0;
  memset(&packet, 0, sizeof(packet));
  memcpy(packet.magic, "STN1", 4);
  snprintf(packet.run, sizeof(packet.run), "%lld", run);
  strappy_notification_preview(packet.title, title,
                               STRAPPY_NOTIFICATION_TITLE_CHARACTERS);
  strappy_notification_preview(packet.body, body,
                               STRAPPY_NOTIFICATION_BODY_CHARACTERS);
  if (!packet.title[0]) strcpy(packet.title, "Strappy");
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, path);
  fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0) return 0;
  flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 &&
      fcntl(fd, F_SETFD, FD_CLOEXEC) == 0) {
    ok = sendto(fd, &packet, sizeof(packet), 0,
                (struct sockaddr *)&address, sizeof(address)) ==
         (ssize_t)sizeof(packet);
  }
  close(fd);
  return ok;
}

const char *strappy_notifications_error_body(const char *error)
{
  static const struct { const char *diagnostic; const char *body; } known[] = {
    { "Sign in to ChatGPT before sending a prompt.",
      "Error: Sign in to ChatGPT and try again." },
    { "The selected account has no bearer credential.",
      "Error: Add credentials for the selected account and try again." },
    { "API model is not configured.", "Error: Select a model and try again." },
    { "Responses conversation history is too large.",
      "Error: This conversation is too large. Start a new chat." },
    { "Responses request body is too large.",
      "Error: This request is too large. Try a shorter prompt or a new chat." },
    { "Responses round limit was reached.",
      "Error: The prompt reached its round limit." },
    { "Responses API retry limit was reached.",
      "Error: The provider could not complete the request after retries." }
  };
  size_t i;
  for (i = 0; error && i < sizeof(known) / sizeof(known[0]); ++i)
    if (!strcmp(error, known[i].diagnostic)) return known[i].body;
  return "Error";
}

void strappy_notifications_finished(long long run, int state,
                                    const char *title, const char *response,
                                    const char *error)
{
  const char *path = strappy_notification_platform_socket();
  if ((!path && !strappy_notification_platform_modern()) || (state != STRAPPY_RUN_COMPLETED && state != STRAPPY_RUN_FAILED))
    return;
  if (strappy_notification_platform_modern()) {
    strappy_notification_packet packet;
    memset(&packet, 0, sizeof(packet));
    strappy_notification_preview(packet.title, title,
                                 STRAPPY_NOTIFICATION_TITLE_CHARACTERS);
    strappy_notification_preview(packet.body,
      state == STRAPPY_RUN_FAILED ? strappy_notifications_error_body(error) : response,
      STRAPPY_NOTIFICATION_BODY_CHARACTERS);
    if (!packet.title[0]) strcpy(packet.title, "Strappy");
    if (!strappy_notification_platform_post(run, packet.title, packet.body))
      syslog(LOG_NOTICE, "StrappyNotifications: modern queue unavailable or full");
    return;
  }
  if (!strappy_notifications_send(path, run, title,
        state == STRAPPY_RUN_FAILED ? strappy_notifications_error_body(error) : response))
    syslog(LOG_NOTICE, "StrappyNotifications: receiver unavailable or busy");
}
