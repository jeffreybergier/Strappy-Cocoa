#ifndef STRAPPY_DB_H
#define STRAPPY_DB_H

#ifdef __cplusplus
extern "C" {
#endif

int strappy_db_initialize(const char *db_path, char **error_out);
int strappy_db_save_exchange(const char *db_path,
                             const char *prompt,
                             const char *response,
                             const char *model,
                             long http_status,
                             char **error_out);

#ifdef __cplusplus
}
#endif

#endif
