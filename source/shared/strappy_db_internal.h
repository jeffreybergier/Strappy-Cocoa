#ifndef STRAPPY_DB_INTERNAL_H
#define STRAPPY_DB_INTERNAL_H

#include "strappy_db.h"

#include <sqlite3.h>
#include <stddef.h>

/* Private contracts shared only by the strappy_db implementation units. */

#define STRAPPY_DB_DEFAULT_MODEL_KEY "default_model_id"
#define STRAPPY_DB_BUILTIN_DEFAULT_MODEL_DESCRIPTION "Built-in default model."
#define STRAPPY_DB_DEFAULT_MODEL_SQL \
  "COALESCE((SELECT p.default_model_id FROM app_preferences p " \
  "WHERE p.id = 1 " \
  "AND EXISTS (SELECT 1 FROM models dm " \
  "WHERE dm.id = p.default_model_id))," \
  "'" STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER "')"
#define STRAPPY_DB_DEFAULT_SESSION_WEB_PROVIDER "auto"
#define STRAPPY_DB_DEFAULT_SESSION_WEB_SEARCH_ENABLED "1"
#define STRAPPY_DB_STRINGIFY_INNER(value) #value
#define STRAPPY_DB_STRINGIFY(value) STRAPPY_DB_STRINGIFY_INNER(value)
#define STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL \
  "INSERT OR IGNORE INTO models " \
  "(id, provider_account_id, wire_model_id, name, description, " \
  "catalog_active, last_seen_at_ms) VALUES ('" \
  STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER "', '" \
  STRAPPY_PROVIDER_ACCOUNT_OPENROUTER "', '" \
  STRAPPY_CONFIG_DEFAULT_API_MODEL "', '" \
  STRAPPY_CONFIG_DEFAULT_API_MODEL "', '" \
  STRAPPY_DB_BUILTIN_DEFAULT_MODEL_DESCRIPTION "', 1, " \
  "CAST(strftime('%s','now') AS INTEGER) * 1000);"

typedef struct strappy_db_sql_buffer {
  char *data;
  size_t length;
} strappy_db_sql_buffer;

/* Connection and schema services shared by all database domains. */
int strappy_db_open(const char *db_path,
                    sqlite3 **db_out,
                    char **error_out);
void strappy_db_release(sqlite3 *db);
int strappy_db_exec(sqlite3 *db,
                    const char *sql,
                    const char *error_prefix,
                    char **error_out);
int strappy_db_ensure_schema(sqlite3 *db, char **error_out);
void strappy_db_sql_buffer_destroy(strappy_db_sql_buffer *buffer);
int strappy_db_sql_buffer_append(strappy_db_sql_buffer *buffer,
                                 const char *text);
char *strappy_db_column_string(sqlite3_stmt *stmt, int column);
int strappy_db_session_exists(sqlite3 *db,
                              long long session_id,
                              char **error_out);
long long strappy_db_now_ms(void);

/* Catalog policy services used while validating session options. */
int strappy_db_model_exists(sqlite3 *db,
                            const char *model_id,
                            char **error_out);
int strappy_db_upsert_app_setting(sqlite3 *db,
                                  const char *key,
                                  const char *value,
                                  const char *error_prefix,
                                  char **error_out);
int strappy_db_model_is_effectively_allowed(sqlite3 *db,
                                            const char *model_id,
                                            int *allowed_out,
                                            char **error_out);
int strappy_db_set_model_allowed_in_db(sqlite3 *db,
                                       const char *model_id,
                                       int allowed,
                                       char **error_out);
int strappy_db_model_matches_session_account(sqlite3 *db,
                                             long long session_id,
                                             const char *model_id,
                                             char **error_out);

#endif
