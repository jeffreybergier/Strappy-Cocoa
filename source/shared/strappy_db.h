#ifndef STRAPPY_DB_H
#define STRAPPY_DB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct strappy_session_record {
  long long session_id;
  char *name;
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
  long long turn_id;
  char *turn_key;
  char *prompt_group_key;
  char *actor;
  char *kind;
  char *api_role;
  char *render_role;
  char *role;
  char *content;
  char *model;
  char *metadata_json;
  char *render_state_json;
  char *message_json;
  char *reasoning;
  char *message_key;
  char *target_message_key;
  char *tool_call_id;
  char *tool_name;
  char *arguments_json;
  char *result_json;
  char *created_at;
  int include_in_context;
  int is_error;
  long http_status;
} strappy_session_message_record;

typedef struct strappy_session_message_record_list {
  strappy_session_message_record *records;
  size_t count;
} strappy_session_message_record_list;

typedef struct strappy_session_message_input {
  const char *turn_key;
  const char *prompt_group_key;
  const char *actor;
  const char *context_policy;
  const char *kind;
  const char *api_role;
  const char *render_role;
  const char *role;
  const char *content;
  const char *model;
  long http_status;
  const char *metadata_json;
  const char *render_state_json;
  const char *message_json;
  const char *reasoning;
  const char *message_key;
  const char *target_message_key;
  const char *tool_call_id;
  const char *tool_name;
  const char *arguments_json;
  const char *result_json;
  int include_in_context;
  int is_error;
} strappy_session_message_input;

typedef struct strappy_discovered_database_input {
  const char *path;
  long long size;
  long long modified_at;
  unsigned long long device;
  unsigned long long inode;
  int is_valid_sqlite;
  const char *validation_error;
  const char *scan_root;
} strappy_discovered_database_input;

typedef struct strappy_discovered_database_record {
  long long catalog_id;
  char *assistant_database_id;
  char *path;
  long long size;
  long long modified_at;
  unsigned long long device;
  unsigned long long inode;
  int is_valid_sqlite;
  char *validation_error;
  char *scan_status;
  char *user_decision;
  char *scan_root;
  char *first_seen_at;
  char *last_seen_at;
  char *last_scanned_at;
} strappy_discovered_database_record;

typedef struct strappy_discovered_database_record_list {
  strappy_discovered_database_record *records;
  size_t count;
} strappy_discovered_database_record_list;

void strappy_session_record_init(strappy_session_record *record);
void strappy_session_record_destroy(strappy_session_record *record);
void strappy_session_record_list_init(strappy_session_record_list *list);
void strappy_session_record_list_destroy(strappy_session_record_list *list);
void strappy_session_message_record_init(strappy_session_message_record *record);
void strappy_session_message_record_destroy(strappy_session_message_record *record);
void strappy_session_message_record_list_init(strappy_session_message_record_list *list);
void strappy_session_message_record_list_destroy(strappy_session_message_record_list *list);
void strappy_discovered_database_record_init(strappy_discovered_database_record *record);
void strappy_discovered_database_record_destroy(strappy_discovered_database_record *record);
void strappy_discovered_database_record_list_init(strappy_discovered_database_record_list *list);
void strappy_discovered_database_record_list_destroy(strappy_discovered_database_record_list *list);

int strappy_db_initialize(const char *db_path, char **error_out);
int strappy_db_save_discovered_databases(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  char **error_out);
int strappy_db_list_discovered_databases(
  const char *db_path,
  strappy_discovered_database_record_list *list,
  char **error_out);
int strappy_db_update_discovered_database_decision(
  const char *db_path,
  long long catalog_id,
  const char *user_decision,
  char **error_out);
int strappy_db_save_exchange(const char *db_path,
                             const char *prompt,
                             const char *response,
                             const char *model,
                             long http_status,
                             const char *metadata_json,
                             const char *message_json,
                             const char *reasoning,
                             char **error_out);
int strappy_db_save_exchange_with_id(const char *db_path,
                                     const char *prompt,
                                     const char *response,
                                     const char *model,
                                     long http_status,
                                     const char *metadata_json,
                                     const char *message_json,
                                     const char *reasoning,
                                     long long *session_id_out,
                                     char **error_out);
int strappy_db_create_session(const char *db_path,
                              long long *session_id_out,
                              char **error_out);
int strappy_db_update_session_name_if_empty(const char *db_path,
                                            long long session_id,
                                            const char *name,
                                            int *did_update_out,
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
                                          const char *metadata_json,
                                          const char *message_json,
                                          const char *reasoning,
                                          char **error_out);
int strappy_db_save_message_sequence_with_id(
  const char *db_path,
  const char *prompt,
  const char *response,
  const char *model,
  long http_status,
  const strappy_session_message_input *messages,
  size_t message_count,
  long long *session_id_out,
  char **error_out);
int strappy_db_append_message_sequence_to_session(
  const char *db_path,
  long long session_id,
  const char *prompt,
  const char *response,
  const char *model,
  long http_status,
  const strappy_session_message_input *messages,
  size_t message_count,
  char **error_out);
int strappy_db_upsert_session_message(
  const char *db_path,
  long long session_id,
  const strappy_session_message_input *message,
  char **error_out);
int strappy_db_append_session_message_content(
  const char *db_path,
  long long session_id,
  const strappy_session_message_input *message,
  const char *content_delta,
  const char *reasoning_delta,
  char **error_out);
int strappy_db_list_session_messages(const char *db_path,
                                     long long session_id,
                                     strappy_session_message_record_list *list,
                                     char **error_out);
int strappy_db_list_session_context_messages(
  const char *db_path,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
