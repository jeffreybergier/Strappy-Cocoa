#ifndef STRAPPY_STORE_CLIENT_H
#define STRAPPY_STORE_CLIENT_H
/* Starts one process-lifetime service monitor. Callback runs on its worker and
 * is a refresh hint; consumers reload committed data on their own UI thread. */
int strappy_store_client_start(const char *socket_path, const char *database_path,
                               void (*changed)(void));
#endif
