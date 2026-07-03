#include "strappy_db.h"

#include "strappy_config.h"
#include "strappy_core.h"

#include <cJSON.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_KEY \
  "default_openrouter_model_id"
#define STRAPPY_DB_SELECTED_OPENROUTER_MODEL_KEY \
  "selected_openrouter_model_id"
#define STRAPPY_DB_LEGACY_DEFAULT_API_MODEL "gemma-4-31b-it"
#define STRAPPY_DB_BUILTIN_DEFAULT_MODEL_DESCRIPTION "Built-in default model."
#define STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL \
  "COALESCE((SELECT s.value FROM app_settings s " \
  "WHERE s.key = '" STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_KEY "' " \
  "AND EXISTS (SELECT 1 FROM openrouter_models dm WHERE dm.id = s.value))," \
  "(SELECT s.value FROM app_settings s " \
  "WHERE s.key = '" STRAPPY_DB_SELECTED_OPENROUTER_MODEL_KEY "' " \
  "AND EXISTS (SELECT 1 FROM openrouter_models sm WHERE sm.id = s.value))," \
  "'" STRAPPY_CONFIG_DEFAULT_API_MODEL "')"
#define STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL \
  "INSERT OR IGNORE INTO openrouter_models " \
  "(id, name, description) VALUES ('" STRAPPY_CONFIG_DEFAULT_API_MODEL "', '" \
  STRAPPY_CONFIG_DEFAULT_API_MODEL "', '" \
  STRAPPY_DB_BUILTIN_DEFAULT_MODEL_DESCRIPTION "');"

static int strappy_db_copy_default_openrouter_model(sqlite3 *db,
                                                    char **model_id_out,
                                                    char **error_out);

void strappy_session_record_init(strappy_session_record *record)
{
  if (record == NULL) {
    return;
  }

  record->session_id = 0;
  record->name = NULL;
  record->prompt = NULL;
  record->response = NULL;
  record->model = NULL;
  record->created_at = NULL;
  record->streaming_enabled = 0;
  record->http_status = 0L;
}

void strappy_session_record_destroy(strappy_session_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->prompt);
  free(record->name);
  free(record->response);
  free(record->model);
  free(record->created_at);
  strappy_session_record_init(record);
}

void strappy_session_record_list_init(strappy_session_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
}

void strappy_session_record_list_destroy(strappy_session_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_session_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_session_record_list_init(list);
}

void strappy_session_message_record_init(strappy_session_message_record *record)
{
  if (record == NULL) {
    return;
  }

  record->message_id = 0;
  record->session_id = 0;
  record->turn_id = 0;
  record->turn_key = NULL;
  record->prompt_group_key = NULL;
  record->actor = NULL;
  record->kind = NULL;
  record->api_role = NULL;
  record->render_role = NULL;
  record->role = NULL;
  record->content = NULL;
  record->model = NULL;
  record->metadata_json = NULL;
  record->render_state_json = NULL;
  record->message_json = NULL;
  record->reasoning = NULL;
  record->message_key = NULL;
  record->target_message_key = NULL;
  record->tool_call_id = NULL;
  record->tool_name = NULL;
  record->arguments_json = NULL;
  record->result_json = NULL;
  record->created_at = NULL;
  record->include_in_context = 0;
  record->is_error = 0;
  record->http_status = 0L;
}

void strappy_session_message_record_destroy(strappy_session_message_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->turn_key);
  free(record->prompt_group_key);
  free(record->actor);
  free(record->kind);
  free(record->api_role);
  free(record->render_role);
  free(record->role);
  free(record->content);
  free(record->model);
  free(record->metadata_json);
  free(record->render_state_json);
  free(record->message_json);
  free(record->reasoning);
  free(record->message_key);
  free(record->target_message_key);
  free(record->tool_call_id);
  free(record->tool_name);
  free(record->arguments_json);
  free(record->result_json);
  free(record->created_at);
  strappy_session_message_record_init(record);
}

void strappy_session_message_record_list_init(strappy_session_message_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
}

void strappy_session_message_record_list_destroy(strappy_session_message_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_session_message_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_session_message_record_list_init(list);
}

void strappy_discovered_database_record_init(strappy_discovered_database_record *record)
{
  if (record == NULL) {
    return;
  }

  record->catalog_id = 0;
  record->assistant_database_id = NULL;
  record->path = NULL;
  record->size = 0;
  record->modified_at = 0;
  record->device = 0U;
  record->inode = 0U;
  record->is_valid_sqlite = 0;
  record->validation_error = NULL;
  record->scan_status = NULL;
  record->user_decision = NULL;
  record->scan_root = NULL;
  record->first_seen_at = NULL;
  record->last_seen_at = NULL;
  record->last_scanned_at = NULL;
}

void strappy_discovered_database_record_destroy(strappy_discovered_database_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->assistant_database_id);
  free(record->path);
  free(record->validation_error);
  free(record->scan_status);
  free(record->user_decision);
  free(record->scan_root);
  free(record->first_seen_at);
  free(record->last_seen_at);
  free(record->last_scanned_at);
  strappy_discovered_database_record_init(record);
}

void strappy_discovered_database_record_list_init(
  strappy_discovered_database_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
}

void strappy_discovered_database_record_list_destroy(
  strappy_discovered_database_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_discovered_database_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_discovered_database_record_list_init(list);
}

void strappy_openrouter_model_record_init(strappy_openrouter_model_record *record)
{
  if (record == NULL) {
    return;
  }

  record->model_id = NULL;
  record->canonical_slug = NULL;
  record->hugging_face_id = NULL;
  record->name = NULL;
  record->description = NULL;
  record->context_length = 0;
  record->created = 0;
  record->architecture_modality = NULL;
  record->architecture_tokenizer = NULL;
  record->architecture_instruct_type = NULL;
  record->pricing_prompt = NULL;
  record->pricing_completion = NULL;
  record->pricing_request = NULL;
  record->pricing_image = NULL;
  record->pricing_audio = NULL;
  record->pricing_web_search = NULL;
  record->pricing_internal_reasoning = NULL;
  record->pricing_input_cache_read = NULL;
  record->pricing_input_cache_write = NULL;
  record->top_provider_context_length = 0;
  record->top_provider_max_completion_tokens = 0;
  record->top_provider_is_moderated = 0;
  record->knowledge_cutoff = NULL;
  record->expiration_date = NULL;
  record->links_details = NULL;
  record->links_json = NULL;
  record->reasoning_json = NULL;
  record->benchmarks_json = NULL;
  record->default_parameters_json = NULL;
  record->per_request_limits_json = NULL;
  record->raw_json = NULL;
  record->fetched_at = NULL;
  record->selected = 0;
  record->allowed = 0;
}

void strappy_openrouter_model_record_destroy(strappy_openrouter_model_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->model_id);
  free(record->canonical_slug);
  free(record->hugging_face_id);
  free(record->name);
  free(record->description);
  free(record->architecture_modality);
  free(record->architecture_tokenizer);
  free(record->architecture_instruct_type);
  free(record->pricing_prompt);
  free(record->pricing_completion);
  free(record->pricing_request);
  free(record->pricing_image);
  free(record->pricing_audio);
  free(record->pricing_web_search);
  free(record->pricing_internal_reasoning);
  free(record->pricing_input_cache_read);
  free(record->pricing_input_cache_write);
  free(record->knowledge_cutoff);
  free(record->expiration_date);
  free(record->links_details);
  free(record->links_json);
  free(record->reasoning_json);
  free(record->benchmarks_json);
  free(record->default_parameters_json);
  free(record->per_request_limits_json);
  free(record->raw_json);
  free(record->fetched_at);
  strappy_openrouter_model_record_init(record);
}

void strappy_openrouter_model_record_list_init(
  strappy_openrouter_model_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
}

void strappy_openrouter_model_record_list_destroy(
  strappy_openrouter_model_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_openrouter_model_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_openrouter_model_record_list_init(list);
}

static int strappy_db_open(const char *db_path,
                           sqlite3 **db_out,
                           char **error_out)
{
  sqlite3 *db;
  int flags;
  int rc;

  if (db_out == NULL) {
    strappy_set_error(error_out, "strappy_db_open received no output.");
    return 0;
  }
  *db_out = NULL;

  if ((db_path == NULL) || (db_path[0] == '\0')) {
    strappy_set_error(error_out, "Session database path is not configured.");
    return 0;
  }

  db = NULL;
  flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  rc = sqlite3_open_v2(db_path, &db, flags, NULL);
  if (rc != SQLITE_OK) {
    const char *message = "unknown sqlite error";
    if (db != NULL) {
      message = sqlite3_errmsg(db);
    }
    strappy_set_formatted_error(error_out,
                                "Could not open session database: %s",
                                message);
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  sqlite3_busy_timeout(db, 5000);
  *db_out = db;
  return 1;
}

static int strappy_db_exec(sqlite3 *db,
                           const char *sql,
                           const char *error_prefix,
                           char **error_out)
{
  char *sqlite_error;
  int rc;

  sqlite_error = NULL;
  rc = sqlite3_exec(db, sql, NULL, NULL, &sqlite_error);
  if (rc != SQLITE_OK) {
    if (sqlite_error != NULL) {
      strappy_set_formatted_error(error_out,
                                  "%s: %s",
                                  error_prefix,
                                  sqlite_error);
      sqlite3_free(sqlite_error);
    } else {
      strappy_set_formatted_error(error_out,
                                  "%s: sqlite %d",
                                  error_prefix,
                                  rc);
    }
    return 0;
  }

  return 1;
}

static int strappy_db_migrate_legacy_default_openrouter_model(
  sqlite3 *db,
  char **error_out)
{
  static const char *settings_sql =
    "UPDATE app_settings "
    "SET value = '" STRAPPY_CONFIG_DEFAULT_API_MODEL "', "
    "updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
    "WHERE value = '" STRAPPY_DB_LEGACY_DEFAULT_API_MODEL "' "
    "AND key IN ('" STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_KEY "', "
    "'" STRAPPY_DB_SELECTED_OPENROUTER_MODEL_KEY "');";
  static const char *sessions_sql =
    "UPDATE sessions "
    "SET model = '" STRAPPY_CONFIG_DEFAULT_API_MODEL "' "
    "WHERE model = '" STRAPPY_DB_LEGACY_DEFAULT_API_MODEL "';";
  static const char *preserve_allowed_sql =
    "UPDATE openrouter_model_settings "
    "SET allowed = 1, updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
    "WHERE model_id = '" STRAPPY_CONFIG_DEFAULT_API_MODEL "' "
    "AND EXISTS (SELECT 1 FROM openrouter_model_settings "
    "WHERE model_id = '" STRAPPY_DB_LEGACY_DEFAULT_API_MODEL "' "
    "AND allowed = 1);";
  static const char *move_allowed_sql =
    "UPDATE openrouter_model_settings "
    "SET model_id = '" STRAPPY_CONFIG_DEFAULT_API_MODEL "', "
    "updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
    "WHERE model_id = '" STRAPPY_DB_LEGACY_DEFAULT_API_MODEL "' "
    "AND NOT EXISTS (SELECT 1 FROM openrouter_model_settings "
    "WHERE model_id = '" STRAPPY_CONFIG_DEFAULT_API_MODEL "');";
  static const char *delete_legacy_settings_sql =
    "DELETE FROM openrouter_model_settings "
    "WHERE model_id = '" STRAPPY_DB_LEGACY_DEFAULT_API_MODEL "';";
  static const char *delete_legacy_model_sql =
    "DELETE FROM openrouter_models "
    "WHERE id = '" STRAPPY_DB_LEGACY_DEFAULT_API_MODEL "' "
    "AND name = '" STRAPPY_DB_LEGACY_DEFAULT_API_MODEL "' "
    "AND description = '" STRAPPY_DB_BUILTIN_DEFAULT_MODEL_DESCRIPTION "';";

  if (strcmp(STRAPPY_CONFIG_DEFAULT_API_MODEL,
             STRAPPY_DB_LEGACY_DEFAULT_API_MODEL) == 0) {
    return 1;
  }

  if (!strappy_db_exec(db,
                       settings_sql,
                       "Could not migrate default OpenRouter model settings",
                       error_out)) {
    return 0;
  }
  if (!strappy_db_exec(db,
                       sessions_sql,
                       "Could not migrate session OpenRouter models",
                       error_out)) {
    return 0;
  }
  if (!strappy_db_exec(db,
                       preserve_allowed_sql,
                       "Could not migrate OpenRouter model allowlist",
                       error_out)) {
    return 0;
  }
  if (!strappy_db_exec(db,
                       move_allowed_sql,
                       "Could not migrate OpenRouter model allowlist",
                       error_out)) {
    return 0;
  }
  if (!strappy_db_exec(db,
                       delete_legacy_settings_sql,
                       "Could not remove legacy OpenRouter model setting",
                       error_out)) {
    return 0;
  }
  if (!strappy_db_exec(db,
                       delete_legacy_model_sql,
                       "Could not remove legacy OpenRouter default model",
                       error_out)) {
    return 0;
  }

  return 1;
}

static int strappy_db_ensure_schema(sqlite3 *db, char **error_out)
{
  static const char *sessions_sql =
    "CREATE TABLE IF NOT EXISTS sessions ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "name TEXT NOT NULL DEFAULT '',"
    "prompt TEXT NOT NULL,"
    "response TEXT NOT NULL,"
    "model TEXT,"
    "http_status INTEGER NOT NULL DEFAULT 0,"
    "streaming_enabled INTEGER NOT NULL DEFAULT 0,"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *turns_sql =
    "CREATE TABLE IF NOT EXISTS session_turns ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "session_id INTEGER NOT NULL,"
    "turn_key TEXT NOT NULL,"
    "prompt_group_key TEXT,"
    "actor TEXT NOT NULL,"
    "api_role TEXT NOT NULL DEFAULT 'user',"
    "render_role TEXT NOT NULL,"
    "context_policy TEXT NOT NULL DEFAULT 'full',"
    "prompt TEXT NOT NULL DEFAULT '',"
    "status TEXT NOT NULL DEFAULT 'complete',"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "FOREIGN KEY(session_id) REFERENCES sessions(id),"
    "UNIQUE(session_id, turn_key)"
    ");";
  static const char *messages_sql =
    "CREATE TABLE IF NOT EXISTS session_messages ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "session_id INTEGER NOT NULL,"
    "turn_id INTEGER,"
    "turn_key TEXT,"
    "prompt_group_key TEXT,"
    "actor TEXT,"
    "kind TEXT NOT NULL DEFAULT 'message',"
    "api_role TEXT,"
    "render_role TEXT NOT NULL,"
    "role TEXT NOT NULL,"
    "content TEXT NOT NULL,"
    "model TEXT,"
    "http_status INTEGER NOT NULL DEFAULT 0,"
    "metadata_json TEXT,"
    "render_state_json TEXT,"
    "message_json TEXT,"
    "reasoning TEXT,"
    "message_key TEXT,"
    "target_message_key TEXT,"
    "tool_call_id TEXT,"
    "tool_name TEXT,"
    "arguments_json TEXT,"
    "result_json TEXT,"
    "include_in_context INTEGER NOT NULL DEFAULT 1,"
    "is_error INTEGER NOT NULL DEFAULT 0,"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "FOREIGN KEY(session_id) REFERENCES sessions(id),"
    "FOREIGN KEY(turn_id) REFERENCES session_turns(id)"
    ");";
  static const char *turns_index_sql =
    "CREATE INDEX IF NOT EXISTS session_turns_session_id_id_idx "
    "ON session_turns(session_id, id);";
  static const char *messages_index_sql =
    "CREATE INDEX IF NOT EXISTS session_messages_session_id_id_idx "
    "ON session_messages(session_id, id);";
  static const char *messages_context_index_sql =
    "CREATE INDEX IF NOT EXISTS session_messages_context_idx "
    "ON session_messages(session_id, include_in_context, id);";
  static const char *discovered_databases_sql =
    "CREATE TABLE IF NOT EXISTS discovered_databases ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "assistant_database_id TEXT UNIQUE,"
    "path TEXT NOT NULL UNIQUE,"
    "size INTEGER NOT NULL DEFAULT 0,"
    "modified_at INTEGER NOT NULL DEFAULT 0,"
    "device INTEGER NOT NULL DEFAULT 0,"
    "inode INTEGER NOT NULL DEFAULT 0,"
    "is_valid_sqlite INTEGER NOT NULL DEFAULT 0,"
    "validation_error TEXT,"
    "scan_status TEXT NOT NULL DEFAULT 'candidate',"
    "user_decision TEXT NOT NULL DEFAULT 'unknown',"
    "scan_root TEXT,"
    "first_seen_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "last_seen_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "last_scanned_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *discovered_databases_device_inode_index_sql =
    "CREATE INDEX IF NOT EXISTS discovered_databases_device_inode_idx "
    "ON discovered_databases(device, inode);";
  static const char *discovered_databases_decision_index_sql =
    "CREATE INDEX IF NOT EXISTS discovered_databases_user_decision_idx "
    "ON discovered_databases(user_decision);";
  static const char *app_settings_sql =
    "CREATE TABLE IF NOT EXISTS app_settings ("
    "key TEXT PRIMARY KEY,"
    "value TEXT,"
    "updated_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *openrouter_models_sql =
    "CREATE TABLE IF NOT EXISTS openrouter_models ("
    "id TEXT PRIMARY KEY,"
    "canonical_slug TEXT,"
    "hugging_face_id TEXT,"
    "name TEXT,"
    "description TEXT,"
    "context_length INTEGER NOT NULL DEFAULT 0,"
    "created INTEGER NOT NULL DEFAULT 0,"
    "architecture_modality TEXT,"
    "architecture_tokenizer TEXT,"
    "architecture_instruct_type TEXT,"
    "pricing_prompt TEXT,"
    "pricing_completion TEXT,"
    "pricing_request TEXT,"
    "pricing_image TEXT,"
    "pricing_audio TEXT,"
    "pricing_web_search TEXT,"
    "pricing_internal_reasoning TEXT,"
    "pricing_input_cache_read TEXT,"
    "pricing_input_cache_write TEXT,"
    "top_provider_context_length INTEGER NOT NULL DEFAULT 0,"
    "top_provider_max_completion_tokens INTEGER NOT NULL DEFAULT 0,"
    "top_provider_is_moderated INTEGER NOT NULL DEFAULT 0,"
    "knowledge_cutoff TEXT,"
    "expiration_date TEXT,"
    "links_details TEXT,"
    "links_json TEXT,"
    "reasoning_json TEXT,"
    "benchmarks_json TEXT,"
    "default_parameters_json TEXT,"
    "per_request_limits_json TEXT,"
    "raw_json TEXT,"
    "fetched_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *openrouter_models_name_index_sql =
    "CREATE INDEX IF NOT EXISTS openrouter_models_name_idx "
    "ON openrouter_models(name, id);";
  static const char *openrouter_model_settings_sql =
    "CREATE TABLE IF NOT EXISTS openrouter_model_settings ("
    "model_id TEXT PRIMARY KEY,"
    "allowed INTEGER NOT NULL DEFAULT 0,"
    "updated_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *openrouter_model_input_modalities_sql =
    "CREATE TABLE IF NOT EXISTS openrouter_model_input_modalities ("
    "model_id TEXT NOT NULL,"
    "modality TEXT NOT NULL,"
    "PRIMARY KEY(model_id, modality),"
    "FOREIGN KEY(model_id) REFERENCES openrouter_models(id) ON DELETE CASCADE"
    ");";
  static const char *openrouter_model_output_modalities_sql =
    "CREATE TABLE IF NOT EXISTS openrouter_model_output_modalities ("
    "model_id TEXT NOT NULL,"
    "modality TEXT NOT NULL,"
    "PRIMARY KEY(model_id, modality),"
    "FOREIGN KEY(model_id) REFERENCES openrouter_models(id) ON DELETE CASCADE"
    ");";
  static const char *openrouter_model_supported_parameters_sql =
    "CREATE TABLE IF NOT EXISTS openrouter_model_supported_parameters ("
    "model_id TEXT NOT NULL,"
    "parameter TEXT NOT NULL,"
    "PRIMARY KEY(model_id, parameter),"
    "FOREIGN KEY(model_id) REFERENCES openrouter_models(id) ON DELETE CASCADE"
    ");";
  static const char *openrouter_model_supported_voices_sql =
    "CREATE TABLE IF NOT EXISTS openrouter_model_supported_voices ("
    "model_id TEXT NOT NULL,"
    "voice TEXT NOT NULL,"
    "PRIMARY KEY(model_id, voice),"
    "FOREIGN KEY(model_id) REFERENCES openrouter_models(id) ON DELETE CASCADE"
    ");";
  static const char *user_version_sql = "PRAGMA user_version = 1;";
  if (!strappy_db_exec(db,
                       sessions_sql,
                       "Could not create session schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       turns_sql,
                       "Could not create session turn schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       turns_index_sql,
                       "Could not create session turn index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       messages_sql,
                       "Could not create session message schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       messages_index_sql,
                       "Could not create session message index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       messages_context_index_sql,
                       "Could not create session context message index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       discovered_databases_sql,
                       "Could not create discovered database schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       discovered_databases_device_inode_index_sql,
                       "Could not create discovered database device index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       discovered_databases_decision_index_sql,
                       "Could not create discovered database decision index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       app_settings_sql,
                       "Could not create app settings schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_models_sql,
                       "Could not create OpenRouter model schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_models_name_index_sql,
                       "Could not create OpenRouter model name index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_model_settings_sql,
                       "Could not create OpenRouter model settings schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL,
                       "Could not create built-in OpenRouter default model",
                       error_out)) {
    return 0;
  }
  if (!strappy_db_migrate_legacy_default_openrouter_model(db, error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_model_input_modalities_sql,
                       "Could not create OpenRouter input modality schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_model_output_modalities_sql,
                       "Could not create OpenRouter output modality schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_model_supported_parameters_sql,
                       "Could not create OpenRouter supported parameter schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_model_supported_voices_sql,
                       "Could not create OpenRouter supported voice schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       user_version_sql,
                       "Could not set database user_version",
                       error_out)) {
    return 0;
  }

  return 1;
}

static char *strappy_db_column_string(sqlite3_stmt *stmt, int column)
{
  const unsigned char *value;

  value = sqlite3_column_text(stmt, column);
  if (value == NULL) {
    return NULL;
  }

  return strappy_string_duplicate((const char *)value);
}

static char *strappy_db_like_pattern_for_search(const char *search_text,
                                                char **error_out)
{
  char *pattern;
  size_t index;
  size_t input_length;
  size_t output_index;
  size_t pattern_length;

  if ((search_text == NULL) || (search_text[0] == '\0')) {
    return NULL;
  }

  input_length = strlen(search_text);
  pattern_length = 2U;
  for (index = 0U; index < input_length; index++) {
    size_t character_length;

    character_length = ((search_text[index] == '%') ||
                        (search_text[index] == '_') ||
                        (search_text[index] == '\\')) ? 2U : 1U;
    if (pattern_length > (((size_t)-1) - character_length - 1U)) {
      strappy_set_error(error_out, "OpenRouter model search text is too large.");
      return NULL;
    }
    pattern_length += character_length;
  }

  pattern = (char *)malloc(pattern_length + 1U);
  if (pattern == NULL) {
    strappy_set_error(error_out, "Could not allocate OpenRouter model search.");
    return NULL;
  }

  output_index = 0U;
  pattern[output_index++] = '%';
  for (index = 0U; index < input_length; index++) {
    if ((search_text[index] == '%') ||
        (search_text[index] == '_') ||
        (search_text[index] == '\\')) {
      pattern[output_index++] = '\\';
    }
    pattern[output_index++] = search_text[index];
  }
  pattern[output_index++] = '%';
  pattern[output_index] = '\0';
  return pattern;
}

static int strappy_db_assign_record_from_statement(strappy_session_record *record,
                                                   sqlite3_stmt *stmt,
                                                   char **error_out)
{
  char *name;
  char *prompt;
  char *response;
  char *model;
  char *created_at;

  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Session row request is incomplete.");
    return 0;
  }

  strappy_session_record_destroy(record);
  record->session_id = (long long)sqlite3_column_int64(stmt, 0);
  record->http_status = (long)sqlite3_column_int64(stmt, 5);
  record->streaming_enabled = sqlite3_column_int(stmt, 7) ? 1 : 0;

  name = strappy_db_column_string(stmt, 1);
  prompt = strappy_db_column_string(stmt, 2);
  response = strappy_db_column_string(stmt, 3);
  model = strappy_db_column_string(stmt, 4);
  created_at = strappy_db_column_string(stmt, 6);

  if ((name == NULL) || (prompt == NULL) || (response == NULL) ||
      (created_at == NULL)) {
    free(name);
    free(prompt);
    free(response);
    free(model);
    free(created_at);
    strappy_set_error(error_out, "Could not allocate session row.");
    return 0;
  }

  record->name = name;
  record->prompt = prompt;
  record->response = response;
  record->model = model;
  record->created_at = created_at;
  return 1;
}

static int strappy_db_assign_message_from_statement(
  strappy_session_message_record *record,
  sqlite3_stmt *stmt,
  char **error_out)
{
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

  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Session message row request is incomplete.");
    return 0;
  }

  strappy_session_message_record_destroy(record);
  record->message_id = (long long)sqlite3_column_int64(stmt, 0);
  record->session_id = (long long)sqlite3_column_int64(stmt, 1);
  record->turn_id = (long long)sqlite3_column_int64(stmt, 2);
  record->http_status = (long)sqlite3_column_int64(stmt, 12);
  record->include_in_context = sqlite3_column_int(stmt, 21) ? 1 : 0;
  record->is_error = sqlite3_column_int(stmt, 22) ? 1 : 0;

  turn_key = strappy_db_column_string(stmt, 3);
  prompt_group_key = strappy_db_column_string(stmt, 4);
  actor = strappy_db_column_string(stmt, 5);
  kind = strappy_db_column_string(stmt, 6);
  api_role = strappy_db_column_string(stmt, 7);
  render_role = strappy_db_column_string(stmt, 8);
  role = strappy_db_column_string(stmt, 9);
  content = strappy_db_column_string(stmt, 10);
  model = strappy_db_column_string(stmt, 11);
  metadata_json = strappy_db_column_string(stmt, 13);
  message_json = strappy_db_column_string(stmt, 14);
  reasoning = strappy_db_column_string(stmt, 15);
  message_key = strappy_db_column_string(stmt, 16);
  target_message_key = strappy_db_column_string(stmt, 17);
  tool_call_id = strappy_db_column_string(stmt, 18);
  tool_name = strappy_db_column_string(stmt, 19);
  arguments_json = strappy_db_column_string(stmt, 20);
  result_json = strappy_db_column_string(stmt, 23);
  created_at = strappy_db_column_string(stmt, 24);
  render_state_json = strappy_db_column_string(stmt, 25);

  if ((kind == NULL) || (render_role == NULL) || (role == NULL) ||
      (content == NULL) || (created_at == NULL)) {
    free(turn_key);
    free(prompt_group_key);
    free(actor);
    free(kind);
    free(api_role);
    free(render_role);
    free(role);
    free(content);
    free(model);
    free(metadata_json);
    free(render_state_json);
    free(message_json);
    free(reasoning);
    free(message_key);
    free(target_message_key);
    free(tool_call_id);
    free(tool_name);
    free(arguments_json);
    free(result_json);
    free(created_at);
    strappy_set_error(error_out, "Could not allocate session message row.");
    return 0;
  }

  record->turn_key = turn_key;
  record->prompt_group_key = prompt_group_key;
  record->actor = actor;
  record->kind = kind;
  record->api_role = api_role;
  record->render_role = render_role;
  record->role = role;
  record->content = content;
  record->model = model;
  record->metadata_json = metadata_json;
  record->render_state_json = render_state_json;
  record->message_json = message_json;
  record->reasoning = reasoning;
  record->message_key = message_key;
  record->target_message_key = target_message_key;
  record->tool_call_id = tool_call_id;
  record->tool_name = tool_name;
  record->arguments_json = arguments_json;
  record->result_json = result_json;
  record->created_at = created_at;
  return 1;
}

static char *strappy_db_create_assistant_database_id(long long catalog_id)
{
  char buffer[64];
  int result;

  if (catalog_id <= 0) {
    return NULL;
  }

  result = snprintf(buffer, sizeof(buffer), "db_%lld", catalog_id);
  if ((result <= 0) || ((size_t)result >= sizeof(buffer))) {
    return NULL;
  }

  return strappy_string_duplicate(buffer);
}

static const char *strappy_db_scan_status_for_input(
  const strappy_discovered_database_input *record)
{
  if ((record != NULL) && record->is_valid_sqlite) {
    return "valid";
  }
  if ((record != NULL) &&
      (record->validation_error != NULL) &&
      (record->validation_error[0] != '\0')) {
    return "invalid";
  }
  return "candidate";
}

static int strappy_db_is_valid_user_decision(const char *user_decision)
{
  if (user_decision == NULL) {
    return 0;
  }

  return ((strcmp(user_decision, "unknown") == 0) ||
          (strcmp(user_decision, "allowed") == 0) ||
          (strcmp(user_decision, "denied") == 0));
}

static int strappy_db_bind_optional_text(sqlite3 *db,
                                         sqlite3_stmt *stmt,
                                         int index,
                                         const char *value,
                                         const char *error_prefix,
                                         char **error_out)
{
  int rc;

  if ((value != NULL) && (value[0] != '\0')) {
    rc = sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT);
  } else {
    rc = sqlite3_bind_null(stmt, index);
  }

  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "%s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    return 0;
  }

  return 1;
}

static int strappy_db_set_assistant_database_id(sqlite3 *db,
                                                long long catalog_id,
                                                char **error_out)
{
  static const char *sql =
    "UPDATE discovered_databases "
    "SET assistant_database_id = ? "
    "WHERE id = ? "
    "AND (assistant_database_id IS NULL OR assistant_database_id = '');";
  sqlite3_stmt *stmt;
  char *assistant_database_id;
  int rc;

  assistant_database_id = strappy_db_create_assistant_database_id(catalog_id);
  if (assistant_database_id == NULL) {
    strappy_set_error(error_out, "Could not allocate discovered database id.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database id update: %s",
                                sqlite3_errmsg(db));
    free(assistant_database_id);
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, assistant_database_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)catalog_id);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind discovered database id update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(assistant_database_id);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save discovered database id: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(assistant_database_id);
    return 0;
  }

  sqlite3_finalize(stmt);
  free(assistant_database_id);
  return 1;
}

static int strappy_db_existing_discovered_database_id(sqlite3 *db,
                                                      const char *path,
                                                      long long *catalog_id_out,
                                                      char **error_out)
{
  static const char *sql =
    "SELECT id FROM discovered_databases WHERE path = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if (catalog_id_out == NULL) {
    strappy_set_error(error_out, "Discovered database lookup has no output.");
    return 0;
  }
  *catalog_id_out = 0;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind discovered database lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *catalog_id_out = (long long)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return 1;
  }

  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    return 1;
  }

  strappy_set_formatted_error(error_out,
                              "Could not read discovered database lookup: %s",
                              sqlite3_errmsg(db));
  return 0;
}

static int strappy_db_insert_discovered_database(
  sqlite3 *db,
  const strappy_discovered_database_input *record,
  long long *catalog_id_out,
  char **error_out)
{
  static const char *sql =
    "INSERT INTO discovered_databases "
    "(path, size, modified_at, device, inode, is_valid_sqlite, "
    "validation_error, scan_status, user_decision, scan_root) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'unknown', ?);";
  sqlite3_stmt *stmt;
  const char *scan_status;
  int rc;
  int ok;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  scan_status = strappy_db_scan_status_for_input(record);
  ok = 1;
  if (sqlite3_bind_text(stmt, 1, record->path, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)record->size) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)record->modified_at) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)record->device) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)record->inode) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt, 6, (record->is_valid_sqlite ? 1 : 0)) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           7,
                                           record->validation_error,
                                           "Could not bind discovered database insert",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 8, scan_status, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           9,
                                           record->scan_root,
                                           "Could not bind discovered database insert",
                                           error_out)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind discovered database insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save discovered database: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  if (catalog_id_out != NULL) {
    *catalog_id_out = (long long)sqlite3_last_insert_rowid(db);
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_update_discovered_database(
  sqlite3 *db,
  long long catalog_id,
  const strappy_discovered_database_input *record,
  char **error_out)
{
  static const char *sql =
    "UPDATE discovered_databases "
    "SET size = ?, modified_at = ?, device = ?, inode = ?, "
    "is_valid_sqlite = ?, validation_error = ?, scan_status = ?, "
    "scan_root = ?, "
    "last_seen_at = (strftime('%Y-%m-%dT%H:%M:%fZ','now')), "
    "last_scanned_at = (strftime('%Y-%m-%dT%H:%M:%fZ','now')) "
    "WHERE id = ?;";
  sqlite3_stmt *stmt;
  const char *scan_status;
  int rc;
  int ok;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  scan_status = strappy_db_scan_status_for_input(record);
  ok = 1;
  if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)record->size) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)record->modified_at) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)record->device) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)record->inode) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt, 5, (record->is_valid_sqlite ? 1 : 0)) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           6,
                                           record->validation_error,
                                           "Could not bind discovered database update",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 7, scan_status, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           8,
                                           record->scan_root,
                                           "Could not bind discovered database update",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 9, (sqlite3_int64)catalog_id) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind discovered database update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update discovered database: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_save_discovered_database(
  sqlite3 *db,
  const strappy_discovered_database_input *record,
  char **error_out)
{
  long long catalog_id;

  if ((record == NULL) || (record->path == NULL) || (record->path[0] == '\0')) {
    strappy_set_error(error_out, "Discovered database path is empty.");
    return 0;
  }

  if (!strappy_db_existing_discovered_database_id(db,
                                                  record->path,
                                                  &catalog_id,
                                                  error_out)) {
    return 0;
  }

  if (catalog_id > 0) {
    if (!strappy_db_update_discovered_database(db,
                                               catalog_id,
                                               record,
                                               error_out)) {
      return 0;
    }
  } else {
    if (!strappy_db_insert_discovered_database(db,
                                               record,
                                               &catalog_id,
                                               error_out)) {
      return 0;
    }
  }

  return strappy_db_set_assistant_database_id(db, catalog_id, error_out);
}

static int strappy_db_assign_discovered_database_from_statement(
  strappy_discovered_database_record *record,
  sqlite3_stmt *stmt,
  char **error_out)
{
  char *assistant_database_id;
  char *path;
  char *validation_error;
  char *scan_status;
  char *user_decision;
  char *scan_root;
  char *first_seen_at;
  char *last_seen_at;
  char *last_scanned_at;
  long long catalog_id;

  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Discovered database row request is incomplete.");
    return 0;
  }

  strappy_discovered_database_record_destroy(record);
  catalog_id = (long long)sqlite3_column_int64(stmt, 0);
  record->catalog_id = catalog_id;
  record->size = (long long)sqlite3_column_int64(stmt, 3);
  record->modified_at = (long long)sqlite3_column_int64(stmt, 4);
  record->device = (unsigned long long)sqlite3_column_int64(stmt, 5);
  record->inode = (unsigned long long)sqlite3_column_int64(stmt, 6);
  record->is_valid_sqlite = sqlite3_column_int(stmt, 7) ? 1 : 0;

  assistant_database_id = strappy_db_column_string(stmt, 1);
  if (assistant_database_id == NULL) {
    assistant_database_id = strappy_db_create_assistant_database_id(catalog_id);
  }
  path = strappy_db_column_string(stmt, 2);
  validation_error = strappy_db_column_string(stmt, 8);
  scan_status = strappy_db_column_string(stmt, 9);
  user_decision = strappy_db_column_string(stmt, 10);
  scan_root = strappy_db_column_string(stmt, 11);
  first_seen_at = strappy_db_column_string(stmt, 12);
  last_seen_at = strappy_db_column_string(stmt, 13);
  last_scanned_at = strappy_db_column_string(stmt, 14);

  if ((assistant_database_id == NULL) || (path == NULL) ||
      (scan_status == NULL) || (user_decision == NULL) ||
      (first_seen_at == NULL) || (last_seen_at == NULL) ||
      (last_scanned_at == NULL)) {
    free(assistant_database_id);
    free(path);
    free(validation_error);
    free(scan_status);
    free(user_decision);
    free(scan_root);
    free(first_seen_at);
    free(last_seen_at);
    free(last_scanned_at);
    strappy_set_error(error_out, "Could not allocate discovered database row.");
    return 0;
  }

  record->assistant_database_id = assistant_database_id;
  record->path = path;
  record->validation_error = validation_error;
  record->scan_status = scan_status;
  record->user_decision = user_decision;
  record->scan_root = scan_root;
  record->first_seen_at = first_seen_at;
  record->last_seen_at = last_seen_at;
  record->last_scanned_at = last_scanned_at;
  return 1;
}

static char *strappy_db_create_message_json(const char *role,
                                            const char *content)
{
  cJSON *root;
  char *json;

  if ((role == NULL) || (role[0] == '\0') || (content == NULL)) {
    return NULL;
  }

  root = cJSON_CreateObject();
  if (root == NULL) {
    return NULL;
  }

  if ((cJSON_AddStringToObject(root, "role", role) == NULL) ||
      (cJSON_AddStringToObject(root, "content", content) == NULL)) {
    cJSON_Delete(root);
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return json;
}

static const char *strappy_db_input_render_role(
  const strappy_session_message_input *message)
{
  if ((message != NULL) &&
      (message->render_role != NULL) &&
      (message->render_role[0] != '\0')) {
    return message->render_role;
  }
  if ((message != NULL) &&
      (message->role != NULL) &&
      (message->role[0] != '\0')) {
    return message->role;
  }
  return "assistant";
}

static const char *strappy_db_input_kind(
  const strappy_session_message_input *message)
{
  if ((message != NULL) &&
      (message->kind != NULL) &&
      (message->kind[0] != '\0')) {
    return message->kind;
  }
  return "message";
}

static const char *strappy_db_input_actor(
  const strappy_session_message_input *message)
{
  if ((message != NULL) &&
      (message->actor != NULL) &&
      (message->actor[0] != '\0')) {
    return message->actor;
  }
  return strappy_db_input_render_role(message);
}

static const char *strappy_db_input_context_policy(
  const strappy_session_message_input *message)
{
  if ((message != NULL) &&
      (message->context_policy != NULL) &&
      (message->context_policy[0] != '\0')) {
    return message->context_policy;
  }
  return "full";
}

static const char *strappy_db_input_api_role(
  const strappy_session_message_input *message)
{
  if ((message != NULL) &&
      (message->api_role != NULL) &&
      (message->api_role[0] != '\0')) {
    return message->api_role;
  }
  return NULL;
}

static int strappy_db_insert_or_load_turn(
  sqlite3 *db,
  long long session_id,
  const strappy_session_message_input *message,
  long long *turn_id_out,
  char **error_out)
{
  static const char *insert_sql =
    "INSERT OR IGNORE INTO session_turns "
    "(session_id, turn_key, prompt_group_key, actor, api_role, render_role, "
    "context_policy, prompt, status) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'complete');";
  static const char *select_sql =
    "SELECT id FROM session_turns WHERE session_id = ? AND turn_key = ?;";
  sqlite3_stmt *stmt;
  const char *turn_key;
  const char *api_role;
  const char *prompt;
  int rc;
  int ok;

  if (turn_id_out == NULL) {
    strappy_set_error(error_out, "Session turn insert received no output.");
    return 0;
  }
  *turn_id_out = 0;

  if ((message == NULL) ||
      (message->turn_key == NULL) ||
      (message->turn_key[0] == '\0')) {
    return 1;
  }

  turn_key = message->turn_key;
  api_role = strappy_db_input_api_role(message);
  if (api_role == NULL) {
    api_role = "user";
  }
  prompt = "";
  if (strcmp(strappy_db_input_kind(message), "prompt") == 0) {
    prompt = (message->content != NULL) ? message->content : "";
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session turn insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 2, turn_key, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           3,
                                           message->prompt_group_key,
                                           "Could not bind session turn insert",
                                           error_out)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt,
                               4,
                               strappy_db_input_actor(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 5, api_role, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt,
                               6,
                               strappy_db_input_render_role(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt,
                               7,
                               strappy_db_input_context_policy(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 8, prompt, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session turn insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session turn: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session turn lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, turn_key, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session turn lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *turn_id_out = (long long)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return 1;
  }

  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session turn was not found after insert.");
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read session turn lookup: %s",
                                sqlite3_errmsg(db));
  }
  return 0;
}

static int strappy_db_insert_message(sqlite3 *db,
                                     long long session_id,
                                     const strappy_session_message_input *message,
                                     char **error_out)
{
  static const char *sql =
    "INSERT INTO session_messages "
    "(session_id, turn_id, turn_key, prompt_group_key, actor, kind, api_role, "
    "render_role, role, content, model, http_status, metadata_json, "
    "render_state_json, message_json, reasoning, message_key, "
    "target_message_key, tool_call_id, tool_name, arguments_json, result_json, "
    "include_in_context, is_error) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt *stmt;
  char *generated_message_json;
  const char *message_json_to_store;
  const char *api_role;
  const char *render_role;
  const char *content;
  long long turn_id;
  int rc;
  int ok;

  if ((session_id <= 0) || (message == NULL)) {
    strappy_set_error(error_out, "Session message is incomplete.");
    return 0;
  }

  render_role = strappy_db_input_render_role(message);
  api_role = strappy_db_input_api_role(message);
  content = (message->content != NULL) ? message->content : "";
  if ((render_role == NULL) || (render_role[0] == '\0')) {
    strappy_set_error(error_out, "Session message render role is incomplete.");
    return 0;
  }

  if (!strappy_db_insert_or_load_turn(db,
                                      session_id,
                                      message,
                                      &turn_id,
                                      error_out)) {
    return 0;
  }

  generated_message_json = NULL;
  message_json_to_store = message->message_json;
  if (((message_json_to_store == NULL) || (message_json_to_store[0] == '\0')) &&
      (api_role != NULL) &&
      (api_role[0] != '\0')) {
    generated_message_json = strappy_db_create_message_json(api_role, content);
    message_json_to_store = generated_message_json;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message insert: %s",
                                sqlite3_errmsg(db));
    free(generated_message_json);
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) {
    ok = 0;
  }
  if (ok && (turn_id > 0) &&
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)turn_id) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (turn_id <= 0) && (sqlite3_bind_null(stmt, 2) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           3,
                                           message->turn_key,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           4,
                                           message->prompt_group_key,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           5,
                                           message->actor,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt,
                               6,
                               strappy_db_input_kind(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (api_role != NULL) &&
      (sqlite3_bind_text(stmt, 7, api_role, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (api_role == NULL) && (sqlite3_bind_null(stmt, 7) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 8, render_role, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 9, render_role, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 10, content, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           11,
                                           message->model,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 12, (sqlite3_int64)message->http_status) !=
       SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           13,
                                           message->metadata_json,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           14,
                                           message->render_state_json,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && (message_json_to_store != NULL) &&
      (sqlite3_bind_text(stmt,
                         15,
                         message_json_to_store,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (message_json_to_store == NULL) &&
      (sqlite3_bind_null(stmt, 15) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           16,
                                           message->reasoning,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           17,
                                           message->message_key,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           18,
                                           message->target_message_key,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           19,
                                           message->tool_call_id,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           20,
                                           message->tool_name,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           21,
                                           message->arguments_json,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           22,
                                           message->result_json,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt,
                        23,
                        message->include_in_context ? 1 : 0) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt, 24, message->is_error ? 1 : 0) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(generated_message_json);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session message: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(generated_message_json);
    return 0;
  }

  sqlite3_finalize(stmt);
  free(generated_message_json);
  return 1;
}

static int strappy_db_find_message_id_by_key(sqlite3 *db,
                                             long long session_id,
                                             const char *message_key,
                                             long long *message_id_out,
                                             char **error_out)
{
  static const char *sql =
    "SELECT id FROM session_messages "
    "WHERE session_id = ? AND message_key = ? "
    "ORDER BY id ASC LIMIT 1;";
  sqlite3_stmt *stmt;
  int rc;

  if (message_id_out == NULL) {
    strappy_set_error(error_out, "Session message lookup received no output.");
    return 0;
  }
  *message_id_out = 0LL;

  if ((message_key == NULL) || (message_key[0] == '\0')) {
    return 1;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, message_key, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *message_id_out = (long long)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return 1;
  }

  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    return 1;
  }

  strappy_set_formatted_error(error_out,
                              "Could not read session message lookup: %s",
                              sqlite3_errmsg(db));
  return 0;
}

static int strappy_db_update_message(sqlite3 *db,
                                     long long session_id,
                                     long long message_id,
                                     const strappy_session_message_input *message,
                                     char **error_out)
{
  static const char *sql =
    "UPDATE session_messages "
    "SET turn_id = ?, turn_key = ?, prompt_group_key = ?, actor = ?, "
    "kind = ?, api_role = ?, render_role = ?, role = ?, content = ?, "
    "model = ?, http_status = ?, metadata_json = ?, message_json = ?, "
    "reasoning = COALESCE(NULLIF(?, ''), reasoning), "
    "target_message_key = ?, tool_call_id = ?, "
    "tool_name = ?, arguments_json = ?, result_json = ?, render_state_json = ?, "
    "include_in_context = ?, is_error = ? "
    "WHERE id = ? AND session_id = ?;";
  sqlite3_stmt *stmt;
  char *generated_message_json;
  const char *message_json_to_store;
  const char *api_role;
  const char *render_role;
  const char *content;
  long long turn_id;
  int rc;
  int ok;

  if ((session_id <= 0) || (message_id <= 0) || (message == NULL)) {
    strappy_set_error(error_out, "Session message update is incomplete.");
    return 0;
  }

  render_role = strappy_db_input_render_role(message);
  api_role = strappy_db_input_api_role(message);
  content = (message->content != NULL) ? message->content : "";
  if ((render_role == NULL) || (render_role[0] == '\0')) {
    strappy_set_error(error_out, "Session message render role is incomplete.");
    return 0;
  }

  if (!strappy_db_insert_or_load_turn(db,
                                      session_id,
                                      message,
                                      &turn_id,
                                      error_out)) {
    return 0;
  }

  generated_message_json = NULL;
  message_json_to_store = message->message_json;
  if (((message_json_to_store == NULL) || (message_json_to_store[0] == '\0')) &&
      (api_role != NULL) &&
      (api_role[0] != '\0')) {
    generated_message_json = strappy_db_create_message_json(api_role, content);
    message_json_to_store = generated_message_json;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message update: %s",
                                sqlite3_errmsg(db));
    free(generated_message_json);
    return 0;
  }

  ok = 1;
  if ((turn_id > 0) &&
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)turn_id) != SQLITE_OK)) {
    ok = 0;
  }
  if ((turn_id <= 0) && (sqlite3_bind_null(stmt, 1) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           2,
                                           message->turn_key,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           3,
                                           message->prompt_group_key,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           4,
                                           message->actor,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt,
                               5,
                               strappy_db_input_kind(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (api_role != NULL) &&
      (sqlite3_bind_text(stmt, 6, api_role, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (api_role == NULL) && (sqlite3_bind_null(stmt, 6) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 7, render_role, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 8, render_role, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 9, content, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           10,
                                           message->model,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 11, (sqlite3_int64)message->http_status) !=
       SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           12,
                                           message->metadata_json,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && (message_json_to_store != NULL) &&
      (sqlite3_bind_text(stmt,
                         13,
                         message_json_to_store,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (message_json_to_store == NULL) &&
      (sqlite3_bind_null(stmt, 13) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           14,
                                           message->reasoning,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           15,
                                           message->target_message_key,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           16,
                                           message->tool_call_id,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           17,
                                           message->tool_name,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           18,
                                           message->arguments_json,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           19,
                                           message->result_json,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           20,
                                           message->render_state_json,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt,
                        21,
                        message->include_in_context ? 1 : 0) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt, 22, message->is_error ? 1 : 0) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 23, (sqlite3_int64)message_id) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 24, (sqlite3_int64)session_id) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(generated_message_json);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session message: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(generated_message_json);
    return 0;
  }

  sqlite3_finalize(stmt);
  free(generated_message_json);
  return 1;
}

static int strappy_db_upsert_message(sqlite3 *db,
                                     long long session_id,
                                     const strappy_session_message_input *message,
                                     char **error_out)
{
  long long message_id;

  if ((message != NULL) &&
      (message->message_key != NULL) &&
      (message->message_key[0] != '\0')) {
    if (!strappy_db_find_message_id_by_key(db,
                                           session_id,
                                           message->message_key,
                                           &message_id,
                                           error_out)) {
      return 0;
    }
    if (message_id > 0LL) {
      return strappy_db_update_message(db,
                                       session_id,
                                       message_id,
                                       message,
                                       error_out);
    }
  }

  return strappy_db_insert_message(db, session_id, message, error_out);
}

static int strappy_db_append_message_content(sqlite3 *db,
                                             long long session_id,
                                             const strappy_session_message_input *message,
                                             const char *content_delta,
                                             const char *reasoning_delta,
                                             char **error_out)
{
  static const char *sql =
    "UPDATE session_messages "
    "SET content = content || ?, "
    "reasoning = CASE "
    "WHEN ? = '' THEN reasoning "
    "WHEN reasoning IS NULL THEN ? "
    "ELSE reasoning || ? END "
    ", render_state_json = CASE "
    "WHEN ? IS NULL THEN render_state_json "
    "WHEN render_state_json LIKE '%\"content_started\":true%' "
    "AND ? LIKE '%\"reasoning_collapsed\":false%' THEN render_state_json "
    "ELSE ? END "
    "WHERE id = ? AND session_id = ?;";
  sqlite3_stmt *stmt;
  long long message_id;
  int rc;

  if ((session_id <= 0) || (message == NULL) ||
      (message->message_key == NULL) ||
      (message->message_key[0] == '\0')) {
    strappy_set_error(error_out, "Streamed session message is incomplete.");
    return 0;
  }

  if (!strappy_db_find_message_id_by_key(db,
                                         session_id,
                                         message->message_key,
                                         &message_id,
                                         error_out)) {
    return 0;
  }
  if (message_id <= 0LL) {
    if (!strappy_db_insert_message(db, session_id, message, error_out)) {
      return 0;
    }
    if (!strappy_db_find_message_id_by_key(db,
                                           session_id,
                                           message->message_key,
                                           &message_id,
                                           error_out)) {
      return 0;
    }
  }
  if (message_id <= 0LL) {
    strappy_set_error(error_out, "Streamed session message was not found.");
    return 0;
  }

  if (content_delta == NULL) {
    content_delta = "";
  }
  if (reasoning_delta == NULL) {
    reasoning_delta = "";
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare streamed message update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, content_delta, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, reasoning_delta, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 3, reasoning_delta, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 4, reasoning_delta, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    if (message->render_state_json != NULL) {
      rc = sqlite3_bind_text(stmt,
                             5,
                             message->render_state_json,
                             -1,
                             SQLITE_TRANSIENT);
    } else {
      rc = sqlite3_bind_null(stmt, 5);
    }
  }
  if (rc == SQLITE_OK) {
    if (message->render_state_json != NULL) {
      rc = sqlite3_bind_text(stmt,
                             6,
                             message->render_state_json,
                             -1,
                             SQLITE_TRANSIENT);
    } else {
      rc = sqlite3_bind_null(stmt, 6);
    }
  }
  if (rc == SQLITE_OK) {
    if (message->render_state_json != NULL) {
      rc = sqlite3_bind_text(stmt,
                             7,
                             message->render_state_json,
                             -1,
                             SQLITE_TRANSIENT);
    } else {
      rc = sqlite3_bind_null(stmt, 7);
    }
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 8, (sqlite3_int64)message_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 9, (sqlite3_int64)session_id);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind streamed message update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update streamed message: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_move_message_content_to_reasoning(
  sqlite3 *db,
  long long session_id,
  const strappy_session_message_input *message,
  char **error_out)
{
  static const char *sql =
    "UPDATE session_messages "
    "SET reasoning = CASE "
    "WHEN content = '' THEN reasoning "
    "WHEN reasoning IS NULL OR reasoning = '' THEN content "
    "WHEN substr(content, 1, 1) = ? THEN reasoning || content "
    "ELSE reasoning || ? || content END, "
    "content = '', "
    "render_state_json = CASE WHEN ? IS NULL THEN render_state_json ELSE ? END "
    "WHERE id = ? AND session_id = ?;";
  sqlite3_stmt *stmt;
  long long message_id;
  int rc;

  if ((session_id <= 0) || (message == NULL) ||
      (message->message_key == NULL) ||
      (message->message_key[0] == '\0')) {
    strappy_set_error(error_out, "Streamed session message is incomplete.");
    return 0;
  }

  if (!strappy_db_find_message_id_by_key(db,
                                         session_id,
                                         message->message_key,
                                         &message_id,
                                         error_out)) {
    return 0;
  }
  if (message_id <= 0LL) {
    return 1;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare streamed message move: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, "\n", -1, SQLITE_STATIC);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, "\n", -1, SQLITE_STATIC);
  }
  if (rc == SQLITE_OK) {
    if (message->render_state_json != NULL) {
      rc = sqlite3_bind_text(stmt,
                             3,
                             message->render_state_json,
                             -1,
                             SQLITE_TRANSIENT);
    } else {
      rc = sqlite3_bind_null(stmt, 3);
    }
  }
  if (rc == SQLITE_OK) {
    if (message->render_state_json != NULL) {
      rc = sqlite3_bind_text(stmt,
                             4,
                             message->render_state_json,
                             -1,
                             SQLITE_TRANSIENT);
    } else {
      rc = sqlite3_bind_null(stmt, 4);
    }
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 5, (sqlite3_int64)message_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 6, (sqlite3_int64)session_id);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind streamed message move: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not move streamed message content: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_session_exists(sqlite3 *db,
                                     long long session_id,
                                     char **error_out)
{
  static const char *sql = "SELECT 1 FROM sessions WHERE id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc == SQLITE_ROW) {
    return 1;
  }

  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read session lookup: %s",
                                sqlite3_errmsg(db));
  }
  return 0;
}

static char *strappy_db_json_copy_value_text(cJSON *value)
{
  if ((value == NULL) || cJSON_IsNull(value)) {
    return NULL;
  }

  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return strappy_string_duplicate(value->valuestring);
  }

  return cJSON_PrintUnformatted(value);
}

static char *strappy_db_json_copy_object_text(cJSON *object, const char *key)
{
  if ((object == NULL) || (key == NULL)) {
    return NULL;
  }

  return strappy_db_json_copy_value_text(cJSON_GetObjectItem(object, key));
}

static cJSON *strappy_db_json_object_child(cJSON *object, const char *key)
{
  cJSON *child;

  if ((object == NULL) || (key == NULL)) {
    return NULL;
  }

  child = cJSON_GetObjectItem(object, key);
  return cJSON_IsObject(child) ? child : NULL;
}

static char *strappy_db_json_copy_nested_text(cJSON *object,
                                              const char *parent_key,
                                              const char *child_key)
{
  return strappy_db_json_copy_object_text(
    strappy_db_json_object_child(object, parent_key),
    child_key);
}

static long long strappy_db_json_integer(cJSON *value)
{
  if (cJSON_IsNumber(value)) {
    return (long long)value->valuedouble;
  }

  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return (long long)strtoll(value->valuestring, NULL, 10);
  }

  return 0;
}

static long long strappy_db_json_object_integer(cJSON *object, const char *key)
{
  if ((object == NULL) || (key == NULL)) {
    return 0;
  }

  return strappy_db_json_integer(cJSON_GetObjectItem(object, key));
}

static long long strappy_db_json_nested_integer(cJSON *object,
                                                const char *parent_key,
                                                const char *child_key)
{
  return strappy_db_json_object_integer(
    strappy_db_json_object_child(object, parent_key),
    child_key);
}

static int strappy_db_json_bool(cJSON *value)
{
  if (cJSON_IsTrue(value)) {
    return 1;
  }
  if (cJSON_IsFalse(value) || (value == NULL) || cJSON_IsNull(value)) {
    return 0;
  }
  if (cJSON_IsNumber(value) && (value->valuedouble != 0.0)) {
    return 1;
  }
  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return ((strcmp(value->valuestring, "true") == 0) ||
            (strcmp(value->valuestring, "1") == 0)) ? 1 : 0;
  }
  return 0;
}

static int strappy_db_json_nested_bool(cJSON *object,
                                       const char *parent_key,
                                       const char *child_key)
{
  cJSON *parent;

  parent = strappy_db_json_object_child(object, parent_key);
  return strappy_db_json_bool(cJSON_GetObjectItem(parent, child_key));
}

static int strappy_db_bind_text_or_null(sqlite3 *db,
                                        sqlite3_stmt *stmt,
                                        int index,
                                        const char *value,
                                        const char *error_prefix,
                                        char **error_out)
{
  return strappy_db_bind_optional_text(db,
                                       stmt,
                                       index,
                                       value,
                                       error_prefix,
                                       error_out);
}

static int strappy_db_insert_openrouter_model_string(
  sqlite3 *db,
  const char *sql,
  const char *model_id,
  const char *value,
  const char *error_prefix,
  char **error_out)
{
  sqlite3_stmt *stmt;
  int rc;

  if ((value == NULL) || (value[0] == '\0')) {
    return 1;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "%s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "%s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "%s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_insert_openrouter_model_value_array(
  sqlite3 *db,
  const char *sql,
  const char *model_id,
  cJSON *value,
  const char *error_prefix,
  char **error_out)
{
  int count;
  int index;

  if ((value == NULL) || cJSON_IsNull(value)) {
    return 1;
  }

  if (!cJSON_IsArray(value)) {
    char *text;
    int ok;

    text = strappy_db_json_copy_value_text(value);
    ok = strappy_db_insert_openrouter_model_string(db,
                                                   sql,
                                                   model_id,
                                                   text,
                                                   error_prefix,
                                                   error_out);
    free(text);
    return ok;
  }

  count = cJSON_GetArraySize(value);
  for (index = 0; index < count; index++) {
    cJSON *item;
    char *text;
    int ok;

    item = cJSON_GetArrayItem(value, index);
    text = strappy_db_json_copy_value_text(item);
    ok = strappy_db_insert_openrouter_model_string(db,
                                                   sql,
                                                   model_id,
                                                   text,
                                                   error_prefix,
                                                   error_out);
    free(text);
    if (!ok) {
      return 0;
    }
  }

  return 1;
}

static int strappy_db_insert_openrouter_model_arrays(sqlite3 *db,
                                                     const char *model_id,
                                                     cJSON *model,
                                                     char **error_out)
{
  static const char *input_sql =
    "INSERT OR IGNORE INTO openrouter_model_input_modalities "
    "(model_id, modality) VALUES (?, ?);";
  static const char *output_sql =
    "INSERT OR IGNORE INTO openrouter_model_output_modalities "
    "(model_id, modality) VALUES (?, ?);";
  static const char *parameters_sql =
    "INSERT OR IGNORE INTO openrouter_model_supported_parameters "
    "(model_id, parameter) VALUES (?, ?);";
  static const char *voices_sql =
    "INSERT OR IGNORE INTO openrouter_model_supported_voices "
    "(model_id, voice) VALUES (?, ?);";
  cJSON *architecture;

  architecture = strappy_db_json_object_child(model, "architecture");
  if (!strappy_db_insert_openrouter_model_value_array(
        db,
        input_sql,
        model_id,
        cJSON_GetObjectItem(architecture, "input_modalities"),
        "Could not save OpenRouter input modality",
        error_out)) {
    return 0;
  }
  if (!strappy_db_insert_openrouter_model_value_array(
        db,
        output_sql,
        model_id,
        cJSON_GetObjectItem(architecture, "output_modalities"),
        "Could not save OpenRouter output modality",
        error_out)) {
    return 0;
  }
  if (!strappy_db_insert_openrouter_model_value_array(
        db,
        parameters_sql,
        model_id,
        cJSON_GetObjectItem(model, "supported_parameters"),
        "Could not save OpenRouter supported parameter",
        error_out)) {
    return 0;
  }
  if (!strappy_db_insert_openrouter_model_value_array(
        db,
        voices_sql,
        model_id,
        cJSON_GetObjectItem(model, "supported_voices"),
        "Could not save OpenRouter supported voice",
        error_out)) {
    return 0;
  }

  return 1;
}

static int strappy_db_insert_openrouter_model(sqlite3 *db,
                                              cJSON *model,
                                              char **error_out)
{
  static const char *sql =
    "INSERT OR REPLACE INTO openrouter_models "
    "(id, canonical_slug, hugging_face_id, name, description, "
    "context_length, created, architecture_modality, "
    "architecture_tokenizer, architecture_instruct_type, pricing_prompt, "
    "pricing_completion, pricing_request, pricing_image, pricing_audio, "
    "pricing_web_search, pricing_internal_reasoning, "
    "pricing_input_cache_read, pricing_input_cache_write, "
    "top_provider_context_length, top_provider_max_completion_tokens, "
    "top_provider_is_moderated, knowledge_cutoff, expiration_date, "
    "links_details, links_json, reasoning_json, benchmarks_json, "
    "default_parameters_json, per_request_limits_json, raw_json, fetched_at) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
    "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%fZ','now'));";
  sqlite3_stmt *stmt;
  strappy_openrouter_model_record record;
  int rc;
  int ok;

  if (!cJSON_IsObject(model)) {
    strappy_set_error(error_out, "OpenRouter model entry is not an object.");
    return 0;
  }

  strappy_openrouter_model_record_init(&record);
  record.model_id = strappy_db_json_copy_object_text(model, "id");
  if ((record.model_id == NULL) || (record.model_id[0] == '\0')) {
    strappy_openrouter_model_record_destroy(&record);
    strappy_set_error(error_out, "OpenRouter model entry is missing id.");
    return 0;
  }

  record.canonical_slug =
    strappy_db_json_copy_object_text(model, "canonical_slug");
  record.hugging_face_id =
    strappy_db_json_copy_object_text(model, "hugging_face_id");
  record.name = strappy_db_json_copy_object_text(model, "name");
  record.description = strappy_db_json_copy_object_text(model, "description");
  record.context_length = strappy_db_json_object_integer(model, "context_length");
  record.created = strappy_db_json_object_integer(model, "created");
  record.architecture_modality =
    strappy_db_json_copy_nested_text(model, "architecture", "modality");
  record.architecture_tokenizer =
    strappy_db_json_copy_nested_text(model, "architecture", "tokenizer");
  record.architecture_instruct_type =
    strappy_db_json_copy_nested_text(model, "architecture", "instruct_type");
  record.pricing_prompt =
    strappy_db_json_copy_nested_text(model, "pricing", "prompt");
  record.pricing_completion =
    strappy_db_json_copy_nested_text(model, "pricing", "completion");
  record.pricing_request =
    strappy_db_json_copy_nested_text(model, "pricing", "request");
  record.pricing_image =
    strappy_db_json_copy_nested_text(model, "pricing", "image");
  record.pricing_audio =
    strappy_db_json_copy_nested_text(model, "pricing", "audio");
  record.pricing_web_search =
    strappy_db_json_copy_nested_text(model, "pricing", "web_search");
  record.pricing_internal_reasoning =
    strappy_db_json_copy_nested_text(model, "pricing", "internal_reasoning");
  record.pricing_input_cache_read =
    strappy_db_json_copy_nested_text(model, "pricing", "input_cache_read");
  record.pricing_input_cache_write =
    strappy_db_json_copy_nested_text(model, "pricing", "input_cache_write");
  record.top_provider_context_length =
    strappy_db_json_nested_integer(model, "top_provider", "context_length");
  record.top_provider_max_completion_tokens =
    strappy_db_json_nested_integer(model,
                                   "top_provider",
                                   "max_completion_tokens");
  record.top_provider_is_moderated =
    strappy_db_json_nested_bool(model, "top_provider", "is_moderated");
  record.knowledge_cutoff =
    strappy_db_json_copy_object_text(model, "knowledge_cutoff");
  record.expiration_date =
    strappy_db_json_copy_object_text(model, "expiration_date");
  record.links_details =
    strappy_db_json_copy_nested_text(model, "links", "details");
  record.links_json = strappy_db_json_copy_object_text(model, "links");
  record.reasoning_json = strappy_db_json_copy_object_text(model, "reasoning");
  record.benchmarks_json =
    strappy_db_json_copy_object_text(model, "benchmarks");
  record.default_parameters_json =
    strappy_db_json_copy_object_text(model, "default_parameters");
  record.per_request_limits_json =
    strappy_db_json_copy_object_text(model, "per_request_limits");
  record.raw_json = cJSON_PrintUnformatted(model);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare OpenRouter model insert: %s",
                                sqlite3_errmsg(db));
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, record.model_id, -1, SQLITE_TRANSIENT) !=
      SQLITE_OK) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 2, record.canonical_slug,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 3, record.hugging_face_id,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 4, record.name,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 5, record.description,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)record.context_length) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_int64(stmt, 7, (sqlite3_int64)record.created) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 8,
                                          record.architecture_modality,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 9,
                                          record.architecture_tokenizer,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 10,
                                          record.architecture_instruct_type,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 11, record.pricing_prompt,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 12, record.pricing_completion,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 13, record.pricing_request,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 14, record.pricing_image,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 15, record.pricing_audio,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 16, record.pricing_web_search,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(
              db,
              stmt,
              17,
              record.pricing_internal_reasoning,
              "Could not bind OpenRouter model insert",
              error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(
              db,
              stmt,
              18,
              record.pricing_input_cache_read,
              "Could not bind OpenRouter model insert",
              error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(
              db,
              stmt,
              19,
              record.pricing_input_cache_write,
              "Could not bind OpenRouter model insert",
              error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt,
                          20,
                          (sqlite3_int64)record.top_provider_context_length) !=
       SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(
         stmt,
         21,
         (sqlite3_int64)record.top_provider_max_completion_tokens) !=
       SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt, 22, record.top_provider_is_moderated ? 1 : 0) !=
       SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          23,
                                          record.knowledge_cutoff,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          24,
                                          record.expiration_date,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          25,
                                          record.links_details,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          26,
                                          record.links_json,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          27,
                                          record.reasoning_json,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          28,
                                          record.benchmarks_json,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          29,
                                          record.default_parameters_json,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          30,
                                          record.per_request_limits_json,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 31, record.raw_json,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }

  if (!ok) {
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_formatted_error(error_out,
                                  "Could not bind OpenRouter model insert: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save OpenRouter model: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  sqlite3_finalize(stmt);

  ok = strappy_db_insert_openrouter_model_arrays(db,
                                                 record.model_id,
                                                 model,
                                                 error_out);
  strappy_openrouter_model_record_destroy(&record);
  return ok;
}

static int strappy_db_assign_openrouter_model_from_statement(
  strappy_openrouter_model_record *record,
  sqlite3_stmt *stmt,
  char **error_out)
{
  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "OpenRouter model row request is incomplete.");
    return 0;
  }

  strappy_openrouter_model_record_destroy(record);
  record->context_length = (long long)sqlite3_column_int64(stmt, 5);
  record->created = (long long)sqlite3_column_int64(stmt, 6);
  record->top_provider_context_length = (long long)sqlite3_column_int64(stmt, 19);
  record->top_provider_max_completion_tokens =
    (long long)sqlite3_column_int64(stmt, 20);
  record->top_provider_is_moderated = sqlite3_column_int(stmt, 21) ? 1 : 0;
  record->selected = sqlite3_column_int(stmt, 32) ? 1 : 0;
  record->allowed = sqlite3_column_int(stmt, 33) ? 1 : 0;

  record->model_id = strappy_db_column_string(stmt, 0);
  record->canonical_slug = strappy_db_column_string(stmt, 1);
  record->hugging_face_id = strappy_db_column_string(stmt, 2);
  record->name = strappy_db_column_string(stmt, 3);
  record->description = strappy_db_column_string(stmt, 4);
  record->architecture_modality = strappy_db_column_string(stmt, 7);
  record->architecture_tokenizer = strappy_db_column_string(stmt, 8);
  record->architecture_instruct_type = strappy_db_column_string(stmt, 9);
  record->pricing_prompt = strappy_db_column_string(stmt, 10);
  record->pricing_completion = strappy_db_column_string(stmt, 11);
  record->pricing_request = strappy_db_column_string(stmt, 12);
  record->pricing_image = strappy_db_column_string(stmt, 13);
  record->pricing_audio = strappy_db_column_string(stmt, 14);
  record->pricing_web_search = strappy_db_column_string(stmt, 15);
  record->pricing_internal_reasoning = strappy_db_column_string(stmt, 16);
  record->pricing_input_cache_read = strappy_db_column_string(stmt, 17);
  record->pricing_input_cache_write = strappy_db_column_string(stmt, 18);
  record->knowledge_cutoff = strappy_db_column_string(stmt, 22);
  record->expiration_date = strappy_db_column_string(stmt, 23);
  record->links_details = strappy_db_column_string(stmt, 24);
  record->links_json = strappy_db_column_string(stmt, 25);
  record->reasoning_json = strappy_db_column_string(stmt, 26);
  record->benchmarks_json = strappy_db_column_string(stmt, 27);
  record->default_parameters_json = strappy_db_column_string(stmt, 28);
  record->per_request_limits_json = strappy_db_column_string(stmt, 29);
  record->raw_json = strappy_db_column_string(stmt, 30);
  record->fetched_at = strappy_db_column_string(stmt, 31);

  if ((record->model_id == NULL) || (record->fetched_at == NULL)) {
    strappy_openrouter_model_record_destroy(record);
    strappy_set_error(error_out, "Could not allocate OpenRouter model row.");
    return 0;
  }

  return 1;
}

int strappy_db_initialize(const char *db_path, char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  ok = strappy_db_ensure_schema(db, error_out);
  sqlite3_close(db);
  return ok;
}

int strappy_db_save_discovered_databases(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  char **error_out)
{
  sqlite3 *db;
  size_t index;

  if ((records == NULL) && (count > 0U)) {
    strappy_set_error(error_out, "Discovered database records are missing.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin discovered database save",
                       error_out)) {
    sqlite3_close(db);
    return 0;
  }

  for (index = 0U; index < count; index++) {
    if (!strappy_db_save_discovered_database(db,
                                             &records[index],
                                             error_out)) {
      strappy_db_exec(db,
                      "ROLLBACK;",
                      "Could not roll back discovered database save",
                      NULL);
      sqlite3_close(db);
      return 0;
    }
  }

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit discovered database save",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database save",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_list_discovered_databases(
  const char *db_path,
  strappy_discovered_database_record_list *list,
  char **error_out)
{
  static const char *sql =
    "SELECT id, assistant_database_id, path, size, modified_at, "
    "device, inode, is_valid_sqlite, validation_error, scan_status, "
    "user_decision, scan_root, first_seen_at, last_seen_at, last_scanned_at "
    "FROM discovered_databases "
    "ORDER BY last_seen_at DESC, id DESC;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "strappy_db_list_discovered_databases received no output.");
    return 0;
  }
  strappy_discovered_database_record_list_init(list);

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database list: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_discovered_database_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_discovered_database_record))) {
      strappy_set_error(error_out, "Discovered database list is too large.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_discovered_database_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_discovered_database_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_discovered_database_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate discovered database list.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_discovered_database_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_discovered_database_record_init(&list->records[list->count]);
    if (!strappy_db_assign_discovered_database_from_statement(
          &list->records[list->count],
          stmt,
          error_out)) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_discovered_database_record_list_destroy(list);
      return 0;
    }

    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read discovered database list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_discovered_database_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_update_discovered_database_decision(
  const char *db_path,
  long long catalog_id,
  const char *user_decision,
  char **error_out)
{
  static const char *sql =
    "UPDATE discovered_databases "
    "SET user_decision = ? "
    "WHERE id = ? "
    "AND (? != 'allowed' OR is_valid_sqlite = 1);";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (catalog_id <= 0) {
    strappy_set_error(error_out, "Discovered database id is not valid.");
    return 0;
  }

  if (!strappy_db_is_valid_user_decision(user_decision)) {
    strappy_set_error(error_out, "Discovered database decision is not valid.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database decision update: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, user_decision, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)catalog_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 3, user_decision, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind discovered database decision update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update discovered database decision: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  if (sqlite3_changes(db) < 1) {
    strappy_set_error(error_out,
                      "Discovered database was not found or is not valid SQLite.");
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_save_exchange(const char *db_path,
                             const char *prompt,
                             const char *response,
                             const char *model,
                             long http_status,
                             const char *metadata_json,
                             const char *message_json,
                             const char *reasoning,
                             char **error_out)
{
  return strappy_db_save_exchange_with_id(db_path,
                                          prompt,
                                          response,
                                          model,
                                          http_status,
                                          metadata_json,
                                          message_json,
                                          reasoning,
                                          NULL,
                                          error_out);
}

int strappy_db_save_exchange_with_id(const char *db_path,
                                     const char *prompt,
                                     const char *response,
                                     const char *model,
                                     long http_status,
                                     const char *metadata_json,
                                     const char *message_json,
                                     const char *reasoning,
                                     long long *session_id_out,
                                     char **error_out)
{
  static const char *sql =
    "INSERT INTO sessions "
    "(prompt, response, model, http_status) "
    "VALUES (?, ?, ?, ?);";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;
  long long session_id;

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

  if ((prompt == NULL) || (response == NULL)) {
    strappy_set_error(error_out, "Session exchange is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session insert", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session insert: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, prompt, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 2, response, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model != NULL) &&
      (sqlite3_bind_text(stmt, 3, model, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model == NULL) && (sqlite3_bind_null(stmt, 3) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)http_status) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  session_id = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);

  {
    strappy_session_message_input input;

    memset(&input, 0, sizeof(input));
    input.actor = "user";
    input.context_policy = "full";
    input.kind = "prompt";
    input.api_role = "user";
    input.render_role = "user";
    input.role = "user";
    input.content = prompt;
    input.include_in_context = 1;
    if (!strappy_db_insert_message(db, session_id, &input, error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
      sqlite3_close(db);
      return 0;
    }

    memset(&input, 0, sizeof(input));
    input.actor = "user";
    input.context_policy = "full";
    input.kind = "assistant";
    input.api_role = "assistant";
    input.render_role = "assistant";
    input.role = "assistant";
    input.content = response;
    input.model = model;
    input.http_status = http_status;
    input.metadata_json = metadata_json;
    input.message_json = message_json;
    input.reasoning = reasoning;
    input.include_in_context = 1;
    if (!strappy_db_insert_message(db, session_id, &input, error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
      sqlite3_close(db);
      return 0;
    }
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session insert", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (session_id_out != NULL) {
    *session_id_out = session_id;
  }

  sqlite3_close(db);
  return 1;
}

static int strappy_db_insert_message_sequence(
  sqlite3 *db,
  long long session_id,
  const strappy_session_message_input *messages,
  size_t message_count,
  char **error_out)
{
  size_t index;

  if ((messages == NULL) && (message_count > 0U)) {
    strappy_set_error(error_out, "Session message sequence is missing.");
    return 0;
  }

  for (index = 0U; index < message_count; index++) {
    if (!strappy_db_upsert_message(db,
                                   session_id,
                                   &messages[index],
                                   error_out)) {
      return 0;
    }
  }

  return 1;
}

static int strappy_db_update_session_summary(sqlite3 *db,
                                             long long session_id,
                                             const char *prompt,
                                             const char *response,
                                             const char *model,
                                             long http_status,
                                             char **error_out)
{
  static const char *update_sql =
    "UPDATE sessions "
    "SET prompt = CASE WHEN prompt = '' THEN ? ELSE prompt END, "
    "response = ?, http_status = ? "
    "WHERE id = ?;";
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if ((session_id <= 0) || (prompt == NULL) || (response == NULL)) {
    strappy_set_error(error_out, "Session summary update is incomplete.");
    return 0;
  }
  (void)model;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, prompt, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 2, response, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)http_status) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)session_id) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

int strappy_db_create_session(const char *db_path,
                              long long *session_id_out,
                              char **error_out)
{
  static const char *sql =
    "INSERT INTO sessions "
    "(prompt, response, model, http_status) "
    "VALUES ('', '', ?, 0);";
  char *default_model_id;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  default_model_id = NULL;
  if (!strappy_db_copy_default_openrouter_model(db,
                                                &default_model_id,
                                                error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session insert: %s",
                                sqlite3_errmsg(db));
    free(default_model_id);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, default_model_id, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session insert: %s",
                                sqlite3_errmsg(db));
    free(default_model_id);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not create session: %s",
                                sqlite3_errmsg(db));
    free(default_model_id);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  if (session_id_out != NULL) {
    *session_id_out = (long long)sqlite3_last_insert_rowid(db);
  }

  free(default_model_id);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_update_session_name_if_empty(const char *db_path,
                                            long long session_id,
                                            const char *name,
                                            int *did_update_out,
                                            char **error_out)
{
  static const char *sql =
    "UPDATE sessions "
    "SET name = ? "
    "WHERE id = ? AND name = '';";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int changed;

  if (did_update_out != NULL) {
    *did_update_out = 0;
  }

  if ((name == NULL) || (name[0] == '\0')) {
    strappy_set_error(error_out, "Session name is empty.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session name update: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  if ((sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session name update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session name: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  changed = sqlite3_changes(db);
  if (did_update_out != NULL) {
    *did_update_out = (changed > 0) ? 1 : 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_update_session_streaming_enabled(const char *db_path,
                                                long long session_id,
                                                int streaming_enabled,
                                                char **error_out)
{
  static const char *sql =
    "UPDATE sessions "
    "SET streaming_enabled = ? "
    "WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session streaming update: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  if ((sqlite3_bind_int(stmt, 1, streaming_enabled ? 1 : 0) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session streaming update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session streaming setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_save_message_sequence_with_id(
  const char *db_path,
  const char *prompt,
  const char *response,
  const char *model,
  long http_status,
  const strappy_session_message_input *messages,
  size_t message_count,
  long long *session_id_out,
  char **error_out)
{
  static const char *sql =
    "INSERT INTO sessions "
    "(prompt, response, model, http_status) "
    "VALUES (?, ?, ?, ?);";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;
  long long session_id;

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

  if ((prompt == NULL) || (response == NULL) ||
      ((messages == NULL) && (message_count > 0U))) {
    strappy_set_error(error_out, "Session message sequence is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session insert", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session insert: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, prompt, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 2, response, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model != NULL) &&
      (sqlite3_bind_text(stmt, 3, model, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model == NULL) && (sqlite3_bind_null(stmt, 3) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)http_status) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  session_id = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);

  if (!strappy_db_insert_message_sequence(db,
                                          session_id,
                                          messages,
                                          message_count,
                                          error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session insert", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (session_id_out != NULL) {
    *session_id_out = session_id;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_list_sessions(const char *db_path,
                             strappy_session_record_list *list,
                             char **error_out)
{
  static const char *sql =
    "SELECT id, name, prompt, response, model, http_status, created_at, "
    "streaming_enabled "
    "FROM sessions "
    "ORDER BY id DESC;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "strappy_db_list_sessions received no output.");
    return 0;
  }
  strappy_session_record_list_init(list);

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session list: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_session_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_session_record))) {
      strappy_set_error(error_out, "Session list is too large.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_session_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_session_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate session list.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_session_record_init(&list->records[list->count]);
    if (!strappy_db_assign_record_from_statement(&list->records[list->count],
                                                 stmt,
                                                 error_out)) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read session list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_session_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_load_session(const char *db_path,
                            long long session_id,
                            strappy_session_record *record,
                            char **error_out)
{
  static const char *sql =
    "SELECT id, name, prompt, response, model, http_status, created_at, "
    "streaming_enabled "
    "FROM sessions "
    "WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if (record == NULL) {
    strappy_set_error(error_out, "strappy_db_load_session received no output.");
    return 0;
  }
  strappy_session_record_init(record);

  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session load: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session load: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read session: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  ok = strappy_db_assign_record_from_statement(record, stmt, error_out);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  if (!ok) {
    strappy_session_record_destroy(record);
    return 0;
  }

  return 1;
}

int strappy_db_append_exchange_to_session(const char *db_path,
                                          long long session_id,
                                          const char *prompt,
                                          const char *response,
                                          const char *model,
                                          long http_status,
                                          const char *metadata_json,
                                          const char *message_json,
                                          const char *reasoning,
                                          char **error_out)
{
  static const char *update_sql =
    "UPDATE sessions "
    "SET prompt = CASE WHEN prompt = '' THEN ? ELSE prompt END, "
    "response = ?, http_status = ? "
    "WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if ((prompt == NULL) || (response == NULL)) {
    strappy_set_error(error_out, "Session exchange is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session append", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  {
    strappy_session_message_input input;

    memset(&input, 0, sizeof(input));
    input.actor = "user";
    input.context_policy = "full";
    input.kind = "prompt";
    input.api_role = "user";
    input.render_role = "user";
    input.role = "user";
    input.content = prompt;
    input.include_in_context = 1;
    if (!strappy_db_insert_message(db, session_id, &input, error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
      sqlite3_close(db);
      return 0;
    }

    memset(&input, 0, sizeof(input));
    input.actor = "user";
    input.context_policy = "full";
    input.kind = "assistant";
    input.api_role = "assistant";
    input.render_role = "assistant";
    input.role = "assistant";
    input.content = response;
    input.model = model;
    input.http_status = http_status;
    input.metadata_json = metadata_json;
    input.message_json = message_json;
    input.reasoning = reasoning;
    input.include_in_context = 1;
    if (!strappy_db_insert_message(db, session_id, &input, error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
      sqlite3_close(db);
      return 0;
    }
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session update: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, prompt, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 2, response, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)http_status) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)session_id) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_finalize(stmt);

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session append", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_append_message_sequence_to_session(
  const char *db_path,
  long long session_id,
  const char *prompt,
  const char *response,
  const char *model,
  long http_status,
  const strappy_session_message_input *messages,
  size_t message_count,
  char **error_out)
{
  sqlite3 *db;

  if ((session_id <= 0) || (prompt == NULL) || (response == NULL) ||
      ((messages == NULL) && (message_count > 0U))) {
    strappy_set_error(error_out, "Session message sequence is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session append", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_insert_message_sequence(db,
                                          session_id,
                                          messages,
                                          message_count,
                                          error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_update_session_summary(db,
                                         session_id,
                                         prompt,
                                         response,
                                         model,
                                         http_status,
                                         error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session append", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_upsert_session_message(
  const char *db_path,
  long long session_id,
  const strappy_session_message_input *message,
  char **error_out)
{
  sqlite3 *db;

  if ((session_id <= 0) || (message == NULL)) {
    strappy_set_error(error_out, "Session message upsert is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session message upsert", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_upsert_message(db, session_id, message, error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session message upsert", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session message upsert", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session message upsert", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_append_session_message_content(
  const char *db_path,
  long long session_id,
  const strappy_session_message_input *message,
  const char *content_delta,
  const char *reasoning_delta,
  char **error_out)
{
  sqlite3 *db;

  if ((session_id <= 0) || (message == NULL)) {
    strappy_set_error(error_out, "Streamed session message append is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin streamed message append", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_append_message_content(db,
                                         session_id,
                                         message,
                                         content_delta,
                                         reasoning_delta,
                                         error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back streamed message append", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit streamed message append", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back streamed message append", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_move_session_message_content_to_reasoning(
  const char *db_path,
  long long session_id,
  const strappy_session_message_input *message,
  char **error_out)
{
  sqlite3 *db;

  if ((session_id <= 0) || (message == NULL)) {
    strappy_set_error(error_out, "Streamed session message move is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin streamed message move", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_move_message_content_to_reasoning(db,
                                                    session_id,
                                                    message,
                                                    error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back streamed message move", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit streamed message move", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back streamed message move", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

static const char *strappy_db_session_message_select_clause(void)
{
  return
    "SELECT id, session_id, COALESCE(turn_id, 0), turn_key, "
    "prompt_group_key, actor, kind, api_role, render_role, role, content, "
    "model, http_status, metadata_json, message_json, reasoning, message_key, "
    "target_message_key, tool_call_id, tool_name, arguments_json, "
    "include_in_context, is_error, result_json, created_at, render_state_json "
    "FROM session_messages ";
}

static int strappy_db_list_session_messages_with_filter(
  const char *db_path,
  long long session_id,
  const char *where_suffix,
  strappy_session_message_record_list *list,
  char **error_out)
{
  static const char *all_suffix =
    "FROM session_messages "
    "WHERE session_id = ? "
    "ORDER BY id ASC;";
  char *sql;
  size_t select_length;
  size_t suffix_length;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "strappy_db_list_session_messages received no output.");
    return 0;
  }
  strappy_session_message_record_list_init(list);

  if ((where_suffix == NULL) || (where_suffix[0] == '\0')) {
    where_suffix = all_suffix + strlen("FROM session_messages ");
  }

  select_length = strlen(strappy_db_session_message_select_clause());
  suffix_length = strlen(where_suffix);
  if (select_length > (((size_t)-1) - suffix_length - 1U)) {
    strappy_set_error(error_out, "Session message query is too large.");
    return 0;
  }
  sql = (char *)malloc(select_length + suffix_length + 1U);
  if (sql == NULL) {
    strappy_set_error(error_out, "Could not allocate session message query.");
    return 0;
  }
  memcpy(sql, strappy_db_session_message_select_clause(), select_length);
  memcpy(sql + select_length, where_suffix, suffix_length);
  sql[select_length + suffix_length] = '\0';

  if (!strappy_db_open(db_path, &db, error_out)) {
    free(sql);
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    free(sql);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    free(sql);
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  free(sql);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message list: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_session_message_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_session_message_record))) {
      strappy_set_error(error_out, "Session message list is too large.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_session_message_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_session_message_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate session message list.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_session_message_record_init(&list->records[list->count]);
    if (!strappy_db_assign_message_from_statement(&list->records[list->count],
                                                  stmt,
                                                  error_out)) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }

    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read session message list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_session_message_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_list_session_messages(const char *db_path,
                                     long long session_id,
                                     strappy_session_message_record_list *list,
                                     char **error_out)
{
  return strappy_db_list_session_messages_with_filter(
    db_path,
    session_id,
    "WHERE session_id = ? ORDER BY id ASC;",
    list,
    error_out);
}

int strappy_db_load_session_message_by_key(
  const char *db_path,
  long long session_id,
  const char *message_key,
  strappy_session_message_record *record,
  char **error_out)
{
  static const char *where_suffix =
    "WHERE session_id = ? AND message_key = ? ORDER BY id DESC LIMIT 1;";
  char *sql;
  size_t select_length;
  size_t suffix_length;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (record == NULL) {
    strappy_set_error(error_out, "Session message output is missing.");
    return 0;
  }
  strappy_session_message_record_init(record);

  if ((message_key == NULL) || (message_key[0] == '\0')) {
    strappy_set_error(error_out, "Session message key is missing.");
    return 0;
  }

  select_length = strlen(strappy_db_session_message_select_clause());
  suffix_length = strlen(where_suffix);
  if (select_length > (((size_t)-1) - suffix_length - 1U)) {
    strappy_set_error(error_out, "Session message query is too large.");
    return 0;
  }
  sql = (char *)malloc(select_length + suffix_length + 1U);
  if (sql == NULL) {
    strappy_set_error(error_out, "Could not allocate session message query.");
    return 0;
  }
  memcpy(sql, strappy_db_session_message_select_clause(), select_length);
  memcpy(sql + select_length, where_suffix, suffix_length);
  sql[select_length + suffix_length] = '\0';

  if (!strappy_db_open(db_path, &db, error_out)) {
    free(sql);
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    free(sql);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    free(sql);
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  free(sql);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, message_key, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (!strappy_db_assign_message_from_statement(record, stmt, error_out)) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      return 0;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 1;
  }

  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session message was not found.");
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read session message lookup: %s",
                                sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 0;
}

int strappy_db_list_session_context_messages(
  const char *db_path,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out)
{
  return strappy_db_list_session_messages_with_filter(
    db_path,
    session_id,
    "WHERE session_id = ? "
    "AND include_in_context = 1 "
    "AND api_role IS NOT NULL "
    "AND api_role != '' "
    "ORDER BY id ASC;",
    list,
    error_out);
}

int strappy_db_save_openrouter_models_json(const char *db_path,
                                           const char *json,
                                           char **error_out)
{
  sqlite3 *db;
  cJSON *root;
  cJSON *data;
  int count;
  int index;

  if ((json == NULL) || (json[0] == '\0')) {
    strappy_set_error(error_out, "OpenRouter model JSON is empty.");
    return 0;
  }

  root = cJSON_Parse(json);
  if (root == NULL) {
    strappy_set_error(error_out, "OpenRouter model JSON could not be parsed.");
    return 0;
  }

  data = cJSON_GetObjectItem(root, "data");
  if (!cJSON_IsArray(data) && cJSON_IsArray(root)) {
    data = root;
  }
  if (!cJSON_IsArray(data)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "OpenRouter model JSON is missing data array.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    cJSON_Delete(root);
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    cJSON_Delete(root);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin OpenRouter model catalog save",
                       error_out)) {
    sqlite3_close(db);
    cJSON_Delete(root);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "DELETE FROM openrouter_model_supported_voices;",
                       "Could not clear OpenRouter supported voices",
                       error_out) ||
      !strappy_db_exec(db,
                       "DELETE FROM openrouter_model_supported_parameters;",
                       "Could not clear OpenRouter supported parameters",
                       error_out) ||
      !strappy_db_exec(db,
                       "DELETE FROM openrouter_model_output_modalities;",
                       "Could not clear OpenRouter output modalities",
                       error_out) ||
      !strappy_db_exec(db,
                       "DELETE FROM openrouter_model_input_modalities;",
                       "Could not clear OpenRouter input modalities",
                       error_out) ||
      !strappy_db_exec(db,
                       "DELETE FROM openrouter_models;",
                       "Could not clear OpenRouter model catalog",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back OpenRouter model catalog save",
                    NULL);
    sqlite3_close(db);
    cJSON_Delete(root);
    return 0;
  }

  count = cJSON_GetArraySize(data);
  for (index = 0; index < count; index++) {
    cJSON *model;

    model = cJSON_GetArrayItem(data, index);
    if (!strappy_db_insert_openrouter_model(db, model, error_out)) {
      strappy_db_exec(db,
                      "ROLLBACK;",
                      "Could not roll back OpenRouter model catalog save",
                      NULL);
      sqlite3_close(db);
      cJSON_Delete(root);
      return 0;
    }
  }

  if (!strappy_db_exec(db,
                       STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL,
                       "Could not save built-in OpenRouter default model",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back OpenRouter model catalog save",
                    NULL);
    sqlite3_close(db);
    cJSON_Delete(root);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit OpenRouter model catalog save",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back OpenRouter model catalog save",
                    NULL);
    sqlite3_close(db);
    cJSON_Delete(root);
    return 0;
  }

  sqlite3_close(db);
  cJSON_Delete(root);
  return 1;
}

int strappy_db_list_openrouter_models(
  const char *db_path,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  return strappy_db_list_openrouter_models_matching(db_path,
                                                    NULL,
                                                    list,
                                                    error_out);
}

int strappy_db_list_allowed_openrouter_models(
  const char *db_path,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  size_t read_index;
  size_t write_index;

  if (!strappy_db_list_openrouter_models(db_path, list, error_out)) {
    return 0;
  }

  write_index = 0U;
  for (read_index = 0U; read_index < list->count; read_index++) {
    if (list->records[read_index].allowed) {
      if (write_index != read_index) {
        strappy_openrouter_model_record_destroy(&list->records[write_index]);
        list->records[write_index] = list->records[read_index];
        strappy_openrouter_model_record_init(&list->records[read_index]);
      }
      write_index++;
    } else {
      strappy_openrouter_model_record_destroy(&list->records[read_index]);
    }
  }
  list->count = write_index;
  return 1;
}

int strappy_db_list_openrouter_models_matching(
  const char *db_path,
  const char *search_text,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  static const char *unfiltered_sql =
    "SELECT m.id, m.canonical_slug, m.hugging_face_id, m.name, "
    "m.description, m.context_length, m.created, m.architecture_modality, "
    "m.architecture_tokenizer, m.architecture_instruct_type, "
    "m.pricing_prompt, m.pricing_completion, m.pricing_request, "
    "m.pricing_image, m.pricing_audio, m.pricing_web_search, "
    "m.pricing_internal_reasoning, m.pricing_input_cache_read, "
    "m.pricing_input_cache_write, m.top_provider_context_length, "
    "m.top_provider_max_completion_tokens, m.top_provider_is_moderated, "
    "m.knowledge_cutoff, m.expiration_date, m.links_details, m.links_json, "
    "m.reasoning_json, m.benchmarks_json, m.default_parameters_json, "
    "m.per_request_limits_json, m.raw_json, m.fetched_at, "
    "CASE WHEN m.id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " THEN 1 ELSE 0 END, "
    "CASE WHEN COALESCE(ms.allowed, 0) = 1 OR m.id = "
    STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL " THEN 1 ELSE 0 END "
    "FROM openrouter_models m "
    "LEFT JOIN openrouter_model_settings ms ON ms.model_id = m.id "
    "ORDER BY CASE WHEN m.id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " THEN 0 ELSE 1 END, "
    "LOWER(COALESCE(NULLIF(m.name, ''), m.id)), m.id;";
  static const char *filtered_sql =
    "SELECT m.id, m.canonical_slug, m.hugging_face_id, m.name, "
    "m.description, m.context_length, m.created, m.architecture_modality, "
    "m.architecture_tokenizer, m.architecture_instruct_type, "
    "m.pricing_prompt, m.pricing_completion, m.pricing_request, "
    "m.pricing_image, m.pricing_audio, m.pricing_web_search, "
    "m.pricing_internal_reasoning, m.pricing_input_cache_read, "
    "m.pricing_input_cache_write, m.top_provider_context_length, "
    "m.top_provider_max_completion_tokens, m.top_provider_is_moderated, "
    "m.knowledge_cutoff, m.expiration_date, m.links_details, m.links_json, "
    "m.reasoning_json, m.benchmarks_json, m.default_parameters_json, "
    "m.per_request_limits_json, m.raw_json, m.fetched_at, "
    "CASE WHEN m.id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " THEN 1 ELSE 0 END, "
    "CASE WHEN COALESCE(ms.allowed, 0) = 1 OR m.id = "
    STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL " THEN 1 ELSE 0 END "
    "FROM openrouter_models m "
    "LEFT JOIN openrouter_model_settings ms ON ms.model_id = m.id "
    "WHERE (m.id LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.canonical_slug, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.hugging_face_id, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.name, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.description, '') LIKE ?1 ESCAPE '\\' "
    "OR CAST(m.context_length AS TEXT) LIKE ?1 ESCAPE '\\' "
    "OR CAST(m.created AS TEXT) LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.architecture_modality, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.architecture_tokenizer, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.architecture_instruct_type, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_prompt, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_completion, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_request, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_image, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_audio, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_web_search, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_internal_reasoning, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_input_cache_read, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_input_cache_write, '') LIKE ?1 ESCAPE '\\' "
    "OR CAST(m.top_provider_context_length AS TEXT) LIKE ?1 ESCAPE '\\' "
    "OR CAST(m.top_provider_max_completion_tokens AS TEXT) LIKE ?1 ESCAPE '\\' "
    "OR CAST(m.top_provider_is_moderated AS TEXT) LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.knowledge_cutoff, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.expiration_date, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.links_details, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.fetched_at, '') LIKE ?1 ESCAPE '\\' "
    "OR EXISTS (SELECT 1 FROM openrouter_model_input_modalities im "
    "WHERE im.model_id = m.id AND im.modality LIKE ?1 ESCAPE '\\') "
    "OR EXISTS (SELECT 1 FROM openrouter_model_output_modalities om "
    "WHERE om.model_id = m.id AND om.modality LIKE ?1 ESCAPE '\\') "
    "OR EXISTS (SELECT 1 FROM openrouter_model_supported_parameters sp "
    "WHERE sp.model_id = m.id AND sp.parameter LIKE ?1 ESCAPE '\\') "
    "OR EXISTS (SELECT 1 FROM openrouter_model_supported_voices sv "
    "WHERE sv.model_id = m.id AND sv.voice LIKE ?1 ESCAPE '\\')) "
    "ORDER BY CASE WHEN m.id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " THEN 0 ELSE 1 END, "
    "LOWER(COALESCE(NULLIF(m.name, ''), m.id)), m.id;";
  const char *sql;
  char *search_pattern;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "strappy_db_list_openrouter_models received no output.");
    return 0;
  }
  strappy_openrouter_model_record_list_init(list);

  sql = unfiltered_sql;
  search_pattern = NULL;
  if ((search_text != NULL) && (search_text[0] != '\0')) {
    search_pattern = strappy_db_like_pattern_for_search(search_text, error_out);
    if (search_pattern == NULL) {
      return 0;
    }
    sql = filtered_sql;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    free(search_pattern);
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    free(search_pattern);
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare OpenRouter model list: %s",
                                sqlite3_errmsg(db));
    free(search_pattern);
    sqlite3_close(db);
    return 0;
  }

  if (search_pattern != NULL) {
    rc = sqlite3_bind_text(stmt, 1, search_pattern, -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
      strappy_set_formatted_error(error_out,
                                  "Could not bind OpenRouter model search: %s",
                                  sqlite3_errmsg(db));
      free(search_pattern);
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      return 0;
    }
    free(search_pattern);
    search_pattern = NULL;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_openrouter_model_record *next_records;

    if (list->count >= (((size_t)-1) /
                        sizeof(strappy_openrouter_model_record))) {
      strappy_set_error(error_out, "OpenRouter model list is too large.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_openrouter_model_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_openrouter_model_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate OpenRouter model list.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_openrouter_model_record_init(&list->records[list->count]);
    if (!strappy_db_assign_openrouter_model_from_statement(
          &list->records[list->count],
          stmt,
          error_out)) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }

    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read OpenRouter model list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_openrouter_model_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

static int strappy_db_model_exists(sqlite3 *db,
                                   const char *model_id,
                                   char **error_out)
{
  static const char *exists_sql =
    "SELECT 1 FROM openrouter_models WHERE id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if ((model_id == NULL) || (model_id[0] == '\0')) {
    strappy_set_error(error_out, "OpenRouter model id is empty.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, exists_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare OpenRouter model lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind OpenRouter model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "OpenRouter model was not found.");
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read OpenRouter model lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int strappy_db_upsert_app_setting(sqlite3 *db,
                                         const char *key,
                                         const char *value,
                                         const char *error_prefix,
                                         char **error_out)
{
  static const char *update_sql =
    "UPDATE app_settings "
    "SET value = ?, updated_at = (strftime('%Y-%m-%dT%H:%M:%fZ','now')) "
    "WHERE key = ?;";
  static const char *insert_sql =
    "INSERT INTO app_settings (key, value) VALUES (?, ?);";
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare %s update: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    return 0;
  }
  rc = sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, key, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind %s update: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update %s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  if (sqlite3_changes(db) == 0) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare %s insert: %s",
                                  error_prefix,
                                  sqlite3_errmsg(db));
      return 0;
    }
    rc = sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
      rc = sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    }
    if (rc != SQLITE_OK) {
      strappy_set_formatted_error(error_out,
                                  "Could not bind %s insert: %s",
                                  error_prefix,
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      strappy_set_formatted_error(error_out,
                                  "Could not save %s: %s",
                                  error_prefix,
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
  }

  return 1;
}

static int strappy_db_copy_default_openrouter_model(sqlite3 *db,
                                                    char **model_id_out,
                                                    char **error_out)
{
  static const char *sql =
    "SELECT " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL ";";
  sqlite3_stmt *stmt;
  int rc;

  if (model_id_out == NULL) {
    strappy_set_error(error_out, "Default OpenRouter model output is missing.");
    return 0;
  }
  *model_id_out = NULL;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare default model lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *model_id_out = strappy_db_column_string(stmt, 0);
    if ((*model_id_out == NULL) || ((*model_id_out)[0] == '\0')) {
      sqlite3_finalize(stmt);
      free(*model_id_out);
      *model_id_out = strappy_string_duplicate(STRAPPY_CONFIG_DEFAULT_API_MODEL);
      if (*model_id_out == NULL) {
        strappy_set_error(error_out, "Could not allocate default model id.");
        return 0;
      }
      return 1;
    }
  } else if (rc == SQLITE_DONE) {
    *model_id_out = strappy_string_duplicate(STRAPPY_CONFIG_DEFAULT_API_MODEL);
    if (*model_id_out == NULL) {
      sqlite3_finalize(stmt);
      strappy_set_error(error_out, "Could not allocate default model id.");
      return 0;
    }
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read default model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_model_is_effectively_allowed(sqlite3 *db,
                                                   const char *model_id,
                                                   int *allowed_out,
                                                   char **error_out)
{
  static const char *sql =
    "SELECT CASE WHEN ? = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " OR EXISTS (SELECT 1 FROM openrouter_model_settings "
    "WHERE model_id = ? AND allowed = 1) THEN 1 ELSE 0 END;";
  sqlite3_stmt *stmt;
  int rc;

  if (allowed_out == NULL) {
    strappy_set_error(error_out, "Allowed model output is missing.");
    return 0;
  }
  *allowed_out = 0;
  if ((model_id == NULL) || (model_id[0] == '\0')) {
    strappy_set_error(error_out, "OpenRouter model id is empty.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare allowed model lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, model_id, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind allowed model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *allowed_out = sqlite3_column_int(stmt, 0) ? 1 : 0;
    sqlite3_finalize(stmt);
    return 1;
  }

  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    return 1;
  }

  strappy_set_formatted_error(error_out,
                              "Could not read allowed model lookup: %s",
                              sqlite3_errmsg(db));
  return 0;
}

static int strappy_db_set_openrouter_model_allowed_in_db(sqlite3 *db,
                                                         const char *model_id,
                                                         int allowed,
                                                         char **error_out)
{
  static const char *sql =
    "INSERT OR REPLACE INTO openrouter_model_settings "
    "(model_id, allowed, updated_at) "
    "VALUES (?, ?, strftime('%Y-%m-%dT%H:%M:%fZ','now'));";
  char *default_model_id;
  sqlite3_stmt *stmt;
  int rc;

  if ((model_id == NULL) || (model_id[0] == '\0')) {
    strappy_set_error(error_out, "OpenRouter model id is empty.");
    return 0;
  }

  default_model_id = NULL;
  if (!strappy_db_copy_default_openrouter_model(db, &default_model_id, error_out)) {
    return 0;
  }
  if (!allowed && (default_model_id != NULL) &&
      (strcmp(model_id, default_model_id) == 0)) {
    free(default_model_id);
    strappy_set_error(error_out, "The default model is always allowed.");
    return 0;
  }
  free(default_model_id);

  if (!strappy_db_model_exists(db, model_id, error_out)) {
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare OpenRouter model setting: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 2, allowed ? 1 : 0);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind OpenRouter model setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save OpenRouter model setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

int strappy_db_set_openrouter_model_allowed(const char *db_path,
                                            const char *model_id,
                                            int allowed,
                                            char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  ok = strappy_db_set_openrouter_model_allowed_in_db(db,
                                                     model_id,
                                                     allowed,
                                                     error_out);
  sqlite3_close(db);
  return ok;
}

int strappy_db_set_default_openrouter_model(const char *db_path,
                                            const char *model_id,
                                            char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }
  if (!strappy_db_model_exists(db, model_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  ok = strappy_db_upsert_app_setting(db,
                                     STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_KEY,
                                     model_id,
                                     "default model",
                                     error_out);
  if (ok) {
    ok = strappy_db_set_openrouter_model_allowed_in_db(db,
                                                       model_id,
                                                       1,
                                                       error_out);
  }

  sqlite3_close(db);
  return ok;
}

int strappy_db_get_default_openrouter_model(const char *db_path,
                                            char **model_id_out,
                                            char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  ok = strappy_db_copy_default_openrouter_model(db, model_id_out, error_out);
  sqlite3_close(db);
  return ok;
}

int strappy_db_set_selected_openrouter_model(const char *db_path,
                                             const char *model_id,
                                             char **error_out)
{
  return strappy_db_set_default_openrouter_model(db_path, model_id, error_out);
}

int strappy_db_get_selected_openrouter_model(const char *db_path,
                                             char **model_id_out,
                                             char **error_out)
{
  return strappy_db_get_default_openrouter_model(db_path,
                                                 model_id_out,
                                                 error_out);
}

int strappy_db_update_session_model(const char *db_path,
                                    long long session_id,
                                    const char *model_id,
                                    char **error_out)
{
  static const char *sql =
    "UPDATE sessions SET model = ? WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int allowed;
  int rc;

  if ((session_id <= 0) || (model_id == NULL) || (model_id[0] == '\0')) {
    strappy_set_error(error_out, "Session model update is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }
  if (!strappy_db_session_exists(db, session_id, error_out) ||
      !strappy_db_model_exists(db, model_id, error_out) ||
      !strappy_db_model_is_effectively_allowed(db, model_id, &allowed, error_out)) {
    sqlite3_close(db);
    return 0;
  }
  if (!allowed) {
    sqlite3_close(db);
    strappy_set_error(error_out, "OpenRouter model is not allowed.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session model update: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session model update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session model: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_get_session_model(const char *db_path,
                                 long long session_id,
                                 char **model_id_out,
                                 char **error_out)
{
  static const char *sql =
    "SELECT CASE WHEN NULLIF(s.model, '') IS NOT NULL AND "
    "(s.model = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " OR EXISTS (SELECT 1 FROM openrouter_model_settings ms "
    "WHERE ms.model_id = s.model AND ms.allowed = 1)) "
    "THEN s.model ELSE " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " END FROM sessions s WHERE s.id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (model_id_out == NULL) {
    strappy_set_error(error_out, "Session model output is missing.");
    return 0;
  }
  *model_id_out = NULL;
  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *model_id_out = strappy_db_column_string(stmt, 0);
    if ((*model_id_out == NULL) || ((*model_id_out)[0] == '\0')) {
      free(*model_id_out);
      *model_id_out = strappy_string_duplicate(STRAPPY_CONFIG_DEFAULT_API_MODEL);
    }
    if (*model_id_out == NULL) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_set_error(error_out, "Could not allocate session model id.");
      return 0;
    }
  } else if (rc == SQLITE_DONE) {
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_set_error(error_out, "Session was not found.");
    return 0;
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read session model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}
