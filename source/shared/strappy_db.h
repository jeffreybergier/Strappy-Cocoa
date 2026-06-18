#ifndef STRAPPY_DB_H
#define STRAPPY_DB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct strappy_session_record {
  long long session_id;
  char *prompt;
  char *response;
  char *model;
  char *created_at;
  long http_status;
} strappy_session_record;

typedef struct strappy_session_record_list {
  strappy_session_record *records;
  size_t count;
} strappy_session_record_list;

typedef struct strappy_session_message_record {
  long long message_id;
  long long session_id;
  char *role;
  char *content;
  char *model;
  char *metadata;
  char *created_at;
  long http_status;
} strappy_session_message_record;

typedef struct strappy_session_message_record_list {
  strappy_session_message_record *records;
  size_t count;
} strappy_session_message_record_list;

void strappy_session_record_init(strappy_session_record *record);
void strappy_session_record_destroy(strappy_session_record *record);
void strappy_session_record_list_init(strappy_session_record_list *list);
void strappy_session_record_list_destroy(strappy_session_record_list *list);
void strappy_session_message_record_init(strappy_session_message_record *record);
void strappy_session_message_record_destroy(strappy_session_message_record *record);
void strappy_session_message_record_list_init(strappy_session_message_record_list *list);
void strappy_session_message_record_list_destroy(strappy_session_message_record_list *list);

int strappy_db_initialize(const char *db_path, char **error_out);
int strappy_db_save_exchange(const char *db_path,
                             const char *prompt,
                             const char *response,
                             const char *model,
                             long http_status,
                             const char *metadata,
                             char **error_out);
int strappy_db_save_exchange_with_id(const char *db_path,
                                     const char *prompt,
                                     const char *response,
                                     const char *model,
                                     long http_status,
                                     const char *metadata,
                                     long long *session_id_out,
                                     char **error_out);
int strappy_db_list_sessions(const char *db_path,
                             strappy_session_record_list *list,
                             char **error_out);
int strappy_db_load_session(const char *db_path,
                            long long session_id,
                            strappy_session_record *record,
                            char **error_out);
int strappy_db_append_exchange_to_session(const char *db_path,
                                          long long session_id,
                                          const char *prompt,
                                          const char *response,
                                          const char *model,
                                          long http_status,
                                          const char *metadata,
                                          char **error_out);
int strappy_db_list_session_messages(const char *db_path,
                                     long long session_id,
                                     strappy_session_message_record_list *list,
                                     char **error_out);

#ifdef __cplusplus
}
#endif

#endif
