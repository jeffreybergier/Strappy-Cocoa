#include "strappy_ipc_platform.h"
#include <sys/socket.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

int strappy_ipc_platform_peer_matches(int fd, uid_t expected_uid)
{
#if defined(__APPLE__)
    uid_t uid;
    gid_t gid;
    return getpeereid(fd, &uid, &gid) == 0 && uid == expected_uid;
#elif defined(__linux__)
    struct ucred peer;
    socklen_t size = sizeof(peer);
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 &&
        size == sizeof(peer) && peer.uid == expected_uid;
#else
    (void)fd;
    (void)expected_uid;
    return 0;
#endif
}

ssize_t strappy_ipc_platform_send(int fd, const void *bytes, size_t size)
{
#if defined(__APPLE__)
    int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) return -1;
    return send(fd, bytes, size, 0);
#elif defined(__linux__)
    return send(fd, bytes, size, MSG_NOSIGNAL);
#else
    (void)fd; (void)bytes; (void)size;
    return -1;
#endif
}

int64_t strappy_ipc_platform_milliseconds(void)
{
#if defined(__APPLE__)
    mach_timebase_info_data_t scale;
    uint64_t ticks = mach_absolute_time();
    if (mach_timebase_info(&scale) != KERN_SUCCESS || scale.denom == 0) return -1;
    return (int64_t)((double)ticks * (double)scale.numer / (double)scale.denom / 1000000.0);
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
#endif
}
