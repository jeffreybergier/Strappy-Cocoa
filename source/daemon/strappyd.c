/* Step-zero launchd heartbeat. No app state or provider access. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define STRAPPY_LOG_LIMIT (64L * 1024L)
static volatile sig_atomic_t strappy_stopping = 0;
static int strappy_signal_pipe[2] = {-1, -1};

static void strappy_stop(int signal_number)
{
    int saved_errno = errno;
    char byte = 1;
    (void)signal_number;
    strappy_stopping = 1;
    /* Async-signal-safe wakeup; a full pipe is already readable. */
    (void)write(strappy_signal_pipe[1], &byte, 1);
    errno = saved_errno;
}

static int strappy_log(const char *path, const char *backup,
                       const char *event, unsigned long long counter)
{
    char timestamp[32];
    char record[192];
    struct tm utc;
    struct stat info;
    time_t now = time(NULL);
    FILE *file;
    int length;
    int failed;
    if (gmtime_r(&now, &utc) == NULL ||
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
        return -1;
    length = snprintf(record, sizeof(record),
                      "%s event=%s pid=%ld euid=%ld heartbeat=%llu\n",
                      timestamp, event, (long)getpid(), (long)geteuid(), counter);
    if (length < 0 || (size_t)length >= sizeof(record)) return -1;
    if (stat(path, &info) == 0) {
        if (info.st_size + length > STRAPPY_LOG_LIMIT && rename(path, backup) != 0)
            return -1;
    } else if (errno != ENOENT) {
        return -1;
    }
    file = fopen(path, "a");
    if (file == NULL) return -1;
    failed = fputs(record, file) == EOF;
    if (fflush(file) != 0) failed = 1;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

int main(int argc, char **argv)
{
    struct sigaction action;
    unsigned long long counter = 0;
    char *backup;
    size_t length;
    int result = EXIT_FAILURE;
    if (argc != 2 || argv[1][0] != '/') return EXIT_FAILURE;
    length = strlen(argv[1]);
    backup = malloc(length + 3);
    if (backup == NULL) return EXIT_FAILURE;
    memcpy(backup, argv[1], length);
    memcpy(backup + length, ".1", 3);
    (void)umask(077);
    if (pipe(strappy_signal_pipe) != 0) goto done;
    if (fcntl(strappy_signal_pipe[1], F_SETFL, O_NONBLOCK) == -1) goto done;
    memset(&action, 0, sizeof(action));
    action.sa_handler = strappy_stop;
    (void)sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) != 0) goto done;
    if (strappy_log(argv[1], backup, "startup", counter) != 0) goto done;
    while (!strappy_stopping) {
        fd_set readers;
        struct timeval timeout;
        int ready;
        FD_ZERO(&readers);
        FD_SET(strappy_signal_pipe[0], &readers);
        timeout.tv_sec = 60;
        timeout.tv_usec = 0;
        ready = select(strappy_signal_pipe[0] + 1, &readers, NULL, NULL, &timeout);
        if (strappy_stopping) break;
        if (ready < 0) {
            if (errno == EINTR) continue;
            goto done;
        }
        if (ready == 0 && strappy_log(argv[1], backup, "heartbeat", ++counter) != 0)
            goto done;
    }
    if (strappy_log(argv[1], backup, "shutdown", counter) == 0) result = EXIT_SUCCESS;
done:
    if (strappy_signal_pipe[0] >= 0) (void)close(strappy_signal_pipe[0]);
    if (strappy_signal_pipe[1] >= 0) (void)close(strappy_signal_pipe[1]);
    free(backup);
    return result;
}
