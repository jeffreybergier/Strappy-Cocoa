/* Fixed diagnostic messages only; no production prompt or notification socket. */
#include "../../strappy_notifications.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
int main(int argc, char **argv)
{
  strappy_notification_packet packet;
  struct sockaddr_un address;
  char *end;
  long sequence;
  int fd;
  if (argc != 2) return 2;
  sequence = strtol(argv[1], &end, 10);
  if (*end || sequence < 1 || sequence > 3) return 2;
  memset(&packet, 0, sizeof(packet));
  memcpy(packet.magic, "STN1", 4);
  snprintf(packet.run, sizeof(packet.run), "900000000000000000%ld", sequence);
  strcpy(packet.title, "Strappy notification test");
  snprintf(packet.body, sizeof(packet.body),
    "Native provider test %ld. Tap to open Strappy.", sequence);
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, STRAPPY_NOTIFICATION_DIRECTORY "/provider-candidate.sock");
  fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0) { perror("socket"); return 1; }
  ssize_t sent = sendto(fd, &packet, sizeof(packet), MSG_DONTWAIT,
    (struct sockaddr *)&address, sizeof(address));
  if (sent != sizeof(packet)) { perror("sendto"); close(fd); return 1; }
  close(fd);
  puts("Test datagram sent; confirm receipt in logs and on the phone.");
  return 0;
}
