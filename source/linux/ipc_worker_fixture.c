/* Offline fixture: synthetic commits only; never linked into the application. */
#include "../shared/strappy_ipc.h"
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
static strappy_ipc_server *server;
static volatile sig_atomic_t stopping;
static void stop(int sig) { (void)sig; stopping = 1; }
static void *worker(void *unused)
{
    unsigned char topic;
    (void)unused;
    while (read(STDIN_FILENO, &topic, 1) == 1) {
        if (topic < 4) (void)strappy_ipc_server_committed(server, STRAPPY_IPC_SESSION_CHANGED + topic);
    }
    return NULL;
}
int main(int argc, char **argv)
{
    pthread_t thread;
    if (argc != 2) return 1;
    (void)signal(SIGPIPE, SIG_IGN); (void)signal(SIGTERM, stop);
    server = strappy_ipc_server_open(argv[1]);
    if (!server || pthread_create(&thread, NULL, worker, NULL) != 0) return 1;
    while (!stopping) if (strappy_ipc_server_step(server, -1, 100) != 0) return 1;
    /* Test driver closes stdin before termination. */
    (void)pthread_join(thread, NULL);
    strappy_ipc_server_close(server);
    return 0;
}
