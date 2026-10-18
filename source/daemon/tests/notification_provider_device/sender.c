/* Fixed diagnostic messages only; no production prompt or notification socket. */
#include "../../strappy_notifications.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#ifndef STRAPPY_PROVIDER_TEST_SOCKET
#define STRAPPY_PROVIDER_TEST_SOCKET \
  STRAPPY_NOTIFICATION_DIRECTORY "/provider-candidate.sock"
#endif
int main(int argc, char **argv)
{
  strappy_notification_packet packet;
  struct sockaddr_un address;
  char *end;
  long sequence;
  int fd, readiness;
  if (argc != 2) return 2;
  readiness = !strcmp(argv[1], "ready");
  sequence = readiness ? 1 : strtol(argv[1], &end, 10);
  if (!readiness && (*end || sequence < 1 || sequence > 3)) return 2;
  memset(&packet, 0, sizeof(packet));
  memcpy(packet.magic, "STN1", 4);
  snprintf(packet.run, sizeof(packet.run), "900000000000000000%ld", sequence);
  strcpy(packet.title, "Strappy notification test");
  snprintf(packet.body, sizeof(packet.body),
    "Native provider test %ld. Tap to open Strappy.", sequence);
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, STRAPPY_PROVIDER_TEST_SOCKET);
  fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0) { perror("socket"); return 1; }
  if (readiness) {
    /* A stale socket pathname is not readiness. Connect checks for a live
     * receiver without sending a datagram or creating a bulletin. */
    int connected = connect(fd, (struct sockaddr *)&address, sizeof(address));
    close(fd);
    if (connected != 0) return 1;
    puts("Candidate receiver accepts connections.");
    return 0;
  }
  ssize_t sent = sendto(fd, &packet, sizeof(packet), MSG_DONTWAIT,
    (struct sockaddr *)&address, sizeof(address));
  if (sent != sizeof(packet)) { perror("sendto"); close(fd); return 1; }
  close(fd);
  puts("Test datagram sent; confirm receipt in logs and on the phone.");
  return 0;
}
