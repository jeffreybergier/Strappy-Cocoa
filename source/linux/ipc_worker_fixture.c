/* Offline fixture: synthetic commits only; never linked into the application. */
#include "../shared/strappy_ipc.h"
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/select.h>
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
static void command(const strappy_ipc_message *request, strappy_ipc_message *reply, void *context)
{
    unsigned int topic;
    struct timeval delay;
    (void)request; (void)reply; (void)context;
    for (topic = 0; topic < 4; ++topic)
        strappy_ipc_server_committed(server, STRAPPY_IPC_SESSION_CHANGED + topic);
    /* Let the socket loop collect all topics while this peer is busy. */
    delay.tv_sec = 0; delay.tv_usec = 250000;
    (void)select(0, NULL, NULL, NULL, &delay);
}
int main(int argc, char **argv)
{
    pthread_t thread;
    if (argc != 2) return 1;
    (void)signal(SIGPIPE, SIG_IGN); (void)signal(SIGTERM, stop);
    server = strappy_ipc_server_open(argv[1]);
    if (!server || pthread_create(&thread, NULL, worker, NULL) != 0) return 1;
    strappy_ipc_server_handler(server, command, NULL);
    while (!stopping) if (strappy_ipc_server_step(server, -1, 100) != 0) return 1;
    /* Test driver closes stdin before termination. */
    (void)pthread_join(thread, NULL);
    strappy_ipc_server_close(server);
    return 0;
}
