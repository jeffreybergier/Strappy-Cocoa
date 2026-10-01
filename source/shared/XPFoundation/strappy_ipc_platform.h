#ifndef XP_FOUNDATION_IPC_H
#define XP_FOUNDATION_IPC_H
#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
/* System-only compatibility boundary, usable by the portable C service. */
int strappy_ipc_platform_peer_matches(int fd, uid_t expected_uid);
ssize_t strappy_ipc_platform_send(int fd, const void *bytes, size_t size);
int64_t strappy_ipc_platform_milliseconds(void);
#endif
