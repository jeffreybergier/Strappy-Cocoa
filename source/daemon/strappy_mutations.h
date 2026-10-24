#ifndef STRAPPY_MUTATIONS_H
#define STRAPPY_MUTATIONS_H
#include "../shared/strappy_ipc.h"
#include "cJSON.h"
#include "../shared/strappy_db.h"
/* Borrowed strings: do not destroy the returned options. */
int strappy_command_options(const cJSON *command, strappy_session_options *options,
    strappy_session_option_mask *mask);
/* Transient command objects contain strings only; never persisted as JSON. */
const char *strappy_command_text(const cJSON *object, const char *key);
long long strappy_command_number(const cJSON *object, const char *key);
/* Returns success; required status receives a safe IPC classification.
 * On success, caller owns *result. Raw diagnostics never cross this boundary. */
int strappy_mutation(const char *db, const char *resources, const cJSON *command,
    char **result, unsigned int *status);
int strappy_service_account_command(const char *db, const cJSON *command, char **result);
int strappy_service_refresh_catalog(const char *db, const char *account);
int strappy_service_scanner_profile(void);
/* Post-commit invalidation topics; bit positions match the IPC event range. */
unsigned int strappy_mutation_topics(const cJSON *command);
#endif
