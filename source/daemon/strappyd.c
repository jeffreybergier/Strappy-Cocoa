/* Launchd storage owner, service transport, and heartbeat. No provider access.
 * Feature-test macros belong to the build: _POSIX_C_SOURCE would force
 * Leopard-only UNIX2003 symbols even when the Apple SDK targets Tiger. */
#include "../shared/strappy_ipc.h"
#include "../shared/strappy_db.h"
#include "../shared/strappy_model_catalog.h"
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

static int strappy_probe(const char *path)
{
    strappy_ipc_message message;
    unsigned char instance[16];
    int fd = strappy_ipc_connect(path, geteuid(), 3000);
    unsigned int operation;
    if (fd < 0) return EXIT_FAILURE;
    for (operation = STRAPPY_IPC_HELLO; operation <= STRAPPY_IPC_UNSUBSCRIBE; ++operation) {
        memset(&message, 0, sizeof(message));
        message.kind = STRAPPY_IPC_REQUEST;
        message.operation = operation; message.request_id = operation;
        if (operation == STRAPPY_IPC_HELLO) {
            message.length = 8;
            strappy_ipc_put32(message.payload, STRAPPY_IPC_VERSION); strappy_ipc_put32(message.payload + 4, STRAPPY_IPC_VERSION);
        }
        if (strappy_ipc_send(fd, &message, 3000) != 0 ||
            strappy_ipc_receive(fd, &message, 3000) != 0 ||
            message.kind != STRAPPY_IPC_REPLY || message.operation != operation ||
            message.request_id != operation || message.status != STRAPPY_IPC_OK ||
            message.length != 36 || strappy_ipc_get32(message.payload + 24) != 3 ||
            strappy_ipc_get32(message.payload + 32) != STRAPPY_IPC_STORE_ABI ||
            strappy_ipc_get32(message.payload + 28) != STRAPPY_IPC_MAX_PAYLOAD) {
            (void)close(fd); return EXIT_FAILURE;
        }
        if (operation == STRAPPY_IPC_HELLO) memcpy(instance, message.payload, 16);
        else if (memcmp(instance, message.payload, 16) != 0) {
            (void)close(fd); return EXIT_FAILURE;
        }
    }
    (void)close(fd);
    puts("IPC v2: same-user hello, snapshot, subscribe, unsubscribe passed; store ready; execution not ready");
    return EXIT_SUCCESS;
}

static int strappy_store_permissions(const char *path)
{
    static const char *const suffixes[] = {"", "-wal", "-shm"};
    char *file = malloc(strlen(path) + 5);
    size_t i;
    int ok = 1;
    if (file == NULL) return 0;
    for (i = 0; ok && i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        struct stat info;
        sprintf(file, "%s%s", path, suffixes[i]);
        if (lstat(file, &info) != 0) ok = errno == ENOENT;
        else ok = S_ISREG(info.st_mode) && info.st_uid == geteuid() && chmod(file, 0600) == 0;
    }
    free(file);
    return ok;
}

/* Parents are supplied explicitly by the installer. The daemon owns only the
 * final Strappy directory and database files, with its normal user identity. */
static int strappy_prepare_store(const char *path, const char *resources)
{
    char *parent = malloc(strlen(path) + 1);
    char *error = NULL;
    struct stat info;
    int ok;
    if (parent == NULL) return 0;
    strcpy(parent, path);
    *strrchr(parent, '/') = '\0';
    ok = mkdir(parent, 0700) == 0 || errno == EEXIST;
    if (ok) ok = lstat(parent, &info) == 0 && S_ISDIR(info.st_mode) &&
                 info.st_uid == geteuid() && chmod(parent, 0700) == 0;
    free(parent);
    if (!ok || !strappy_store_permissions(path)) return 0;
    ok = strappy_db_initialize(path, &error) &&
         strappy_model_catalog_import_bundled_models(resources, path, &error) &&
         strappy_store_permissions(path);
    if (!ok) fprintf(stderr, "Store initialization failed: %s\n", error != NULL ? error : "unknown error");
    free(error);
    return ok;
}

int main(int argc, char **argv)
{
    struct sigaction action;
    unsigned long long counter = 0;
    char *backup;
    char *directory;
    strappy_ipc_server *server = NULL;
    time_t heartbeat;
    size_t length;
    int result = EXIT_FAILURE;
    (void)signal(SIGPIPE, SIG_IGN);
    if (argc == 3 && strcmp(argv[1], "--ipc-probe") == 0) return strappy_probe(argv[2]);
    if (argc != 4 || argv[1][0] != '/' || argv[2][0] != '/' || argv[3][0] != '/') return EXIT_FAILURE;
    length = strlen(argv[1]);
    backup = malloc(length + 3);
    if (backup == NULL) return EXIT_FAILURE;
    directory = malloc(length + 1);
    if (directory == NULL) { free(backup); return EXIT_FAILURE; }
    memcpy(directory, argv[1], length + 1);
    *strrchr(directory, '/') = '\0';
    memcpy(backup, argv[1], length);
    memcpy(backup + length, ".1", 3);
    (void)umask(077);
    if (pipe(strappy_signal_pipe) != 0) goto done;
    if (fcntl(strappy_signal_pipe[1], F_SETFL, O_NONBLOCK) == -1) goto done;
    memset(&action, 0, sizeof(action));
    action.sa_handler = strappy_stop;
    (void)sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) != 0) goto done;
    server = strappy_ipc_server_open(directory);
    if (server == NULL) goto done;
    if (!strappy_prepare_store(argv[2], argv[3])) goto done;
    strappy_ipc_server_store_ready(server);
    if (strappy_log(argv[1], backup, "startup", counter) != 0) goto done;
    heartbeat = time(NULL);
    while (!strappy_stopping) {
        time_t now;
        if (strappy_ipc_server_step(server, strappy_signal_pipe[0], 1000) != 0) goto done;
        if (strappy_stopping) break;
        now = time(NULL);
        if (now - heartbeat >= 60 || now < heartbeat) {
            if (strappy_log(argv[1], backup, "heartbeat", ++counter) != 0) goto done;
            heartbeat = now;
        }
    }
    if (strappy_log(argv[1], backup, "shutdown", counter) == 0) result = EXIT_SUCCESS;
done:
    strappy_ipc_server_close(server);
    free(directory);
    if (strappy_signal_pipe[0] >= 0) (void)close(strappy_signal_pipe[0]);
    if (strappy_signal_pipe[1] >= 0) (void)close(strappy_signal_pipe[1]);
    free(backup);
    return result;
}
