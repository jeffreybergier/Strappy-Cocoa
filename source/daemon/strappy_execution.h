#ifndef STRAPPY_EXECUTION_H
#define STRAPPY_EXECUTION_H
#include "../shared/strappy_ipc.h"
typedef struct strappy_execution strappy_execution;
strappy_execution *strappy_execution_open(strappy_ipc_server *server, const char *db, const char *resources);
void strappy_execution_stop(strappy_execution *execution);
void strappy_execution_close(strappy_execution *execution);
/* Cocoa credential boundary, with portable offline implementation. */
int strappy_service_credentials_configure(void);
int strappy_service_credentials_probe(const char *database);
void *strappy_service_worker_enter(void);
void strappy_service_worker_leave(void *pool);
int strappy_service_should_cancel(void);
#endif
