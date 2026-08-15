#include "strappy_db_internal.h"

#include "strappy_assistant_sets.h"
#include "strappy_core.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

/* Connection lifetime and canonical schema initialization. */

static int strappy_db_enable_write_ahead_log(sqlite3 *db, char **error_out)
{
  sqlite3_stmt *stmt;
  const unsigned char *journal_mode;
  int rc;
  int finalize_rc;
  int ok;

  if (db == NULL) {
    strappy_set_error(error_out, "Database is not open.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, "PRAGMA journal_mode = WAL;", -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not enable database write-ahead log: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  ok = 1;
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    journal_mode = sqlite3_column_text(stmt, 0);
    if ((journal_mode == NULL) ||
        (strcmp((const char *)journal_mode, "wal") != 0)) {
      strappy_set_formatted_error(error_out,
                                  "Could not enable database write-ahead log: "
                                  "journal mode is %s",
                                  (journal_mode != NULL) ?
                                    (const char *)journal_mode : "unknown");
      ok = 0;
    }
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not enable database write-ahead log: %s",
                                sqlite3_errmsg(db));
    ok = 0;
  }

  finalize_rc = sqlite3_finalize(stmt);
  if (ok && (finalize_rc != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not enable database write-ahead log: %s",
                                sqlite3_errmsg(db));
    ok = 0;
  }

  return ok;
}

static pthread_mutex_t strappy_db_connection_mutex =
  PTHREAD_MUTEX_INITIALIZER;
static sqlite3 *strappy_db_connection = NULL;
static char *strappy_db_connection_path = NULL;
static dev_t strappy_db_connection_device = (dev_t)0;
static ino_t strappy_db_connection_inode = (ino_t)0;
static int strappy_db_connection_has_identity = 0;
static int strappy_db_connection_schema_ready = 0;
static int strappy_db_connection_shutdown_registered = 0;

static void strappy_db_close_connection_locked(void)
{
  sqlite3_stmt *stmt;

  if (strappy_db_connection == NULL) {
    return;
  }
  while ((stmt = sqlite3_next_stmt(strappy_db_connection, NULL)) != NULL) {
    sqlite3_finalize(stmt);
  }
  if (!sqlite3_get_autocommit(strappy_db_connection)) {
    sqlite3_exec(strappy_db_connection, "ROLLBACK;", NULL, NULL, NULL);
  }
  sqlite3_exec(strappy_db_connection,
               "PRAGMA wal_checkpoint(PASSIVE);",
               NULL,
               NULL,
               NULL);
  sqlite3_close(strappy_db_connection);
  strappy_db_connection = NULL;
  free(strappy_db_connection_path);
  strappy_db_connection_path = NULL;
  strappy_db_connection_device = (dev_t)0;
  strappy_db_connection_inode = (ino_t)0;
  strappy_db_connection_has_identity = 0;
  strappy_db_connection_schema_ready = 0;
}

static void strappy_db_shutdown_connection(void)
{
  if (pthread_mutex_lock(&strappy_db_connection_mutex) != 0) {
    return;
  }
  strappy_db_close_connection_locked();
  pthread_mutex_unlock(&strappy_db_connection_mutex);
}

void strappy_db_release(sqlite3 *db)
{
  sqlite3_stmt *stmt;

  if (db != strappy_db_connection) {
    if (db != NULL) {
      sqlite3_close(db);
    }
    return;
  }
  while ((stmt = sqlite3_next_stmt(db, NULL)) != NULL) {
    sqlite3_finalize(stmt);
  }
  if (!sqlite3_get_autocommit(db)) {
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
  }
  pthread_mutex_unlock(&strappy_db_connection_mutex);
}

int strappy_db_open(const char *db_path,
                           sqlite3 **db_out,
                           char **error_out)
{
  struct stat path_stat;
  sqlite3 *db;
  char *path_copy;
  int flags;
  int path_has_identity;
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
  if (pthread_mutex_lock(&strappy_db_connection_mutex) != 0) {
    strappy_set_error(error_out, "Could not lock the session database.");
    return 0;
  }

  path_has_identity = (stat(db_path, &path_stat) == 0) ? 1 : 0;
  if ((strappy_db_connection != NULL) &&
      (strappy_db_connection_path != NULL) &&
      (strcmp(strappy_db_connection_path, db_path) == 0) &&
      (((path_has_identity && strappy_db_connection_has_identity) &&
        (path_stat.st_dev == strappy_db_connection_device) &&
        (path_stat.st_ino == strappy_db_connection_inode)) ||
       (!path_has_identity && !strappy_db_connection_has_identity &&
        (db_path[0] == ':')))) {
    *db_out = strappy_db_connection;
    return 1;
  }

  strappy_db_close_connection_locked();
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
    pthread_mutex_unlock(&strappy_db_connection_mutex);
    return 0;
  }

  sqlite3_busy_timeout(db, 5000);
  if (!strappy_db_enable_write_ahead_log(db, error_out)) {
    sqlite3_close(db);
    pthread_mutex_unlock(&strappy_db_connection_mutex);
    return 0;
  }
  path_copy = strappy_string_duplicate(db_path);
  if (path_copy == NULL) {
    strappy_set_error(error_out, "Could not remember session database path.");
    sqlite3_close(db);
    pthread_mutex_unlock(&strappy_db_connection_mutex);
    return 0;
  }

  strappy_db_connection = db;
  strappy_db_connection_path = path_copy;
  strappy_db_connection_schema_ready = 0;
  if (stat(db_path, &path_stat) == 0) {
    strappy_db_connection_device = path_stat.st_dev;
    strappy_db_connection_inode = path_stat.st_ino;
    strappy_db_connection_has_identity = 1;
  }
  if (!strappy_db_connection_shutdown_registered) {
    if (atexit(strappy_db_shutdown_connection) == 0) {
      strappy_db_connection_shutdown_registered = 1;
    }
  }

  *db_out = db;
  return 1;
}

int strappy_db_exec(sqlite3 *db,
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

void strappy_db_sql_buffer_destroy(strappy_db_sql_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }
  free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0U;
}

int strappy_db_sql_buffer_append(strappy_db_sql_buffer *buffer,
                                        const char *text)
{
  size_t text_length;
  char *next;

  if ((buffer == NULL) || (text == NULL)) {
    return 0;
  }
  text_length = strlen(text);
  if (buffer->length > (((size_t)-1) - text_length - 1U)) {
    return 0;
  }
  next = (char *)realloc(buffer->data,
                         buffer->length + text_length + 1U);
  if (next == NULL) {
    return 0;
  }
  buffer->data = next;
  memcpy(buffer->data + buffer->length, text, text_length);
  buffer->length += text_length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

static int strappy_db_ensure_semantic_schema(sqlite3 *db, char **error_out)
{
  static const char schema_models_sql[] =
    "CREATE TABLE IF NOT EXISTS schema_metadata ("
    "id INTEGER PRIMARY KEY CHECK(id = 1),"
    "schema_name TEXT NOT NULL,"
    "created_at_ms INTEGER NOT NULL"
    ");"
    "INSERT OR IGNORE INTO schema_metadata "
    "(id, schema_name, created_at_ms) VALUES "
    "(1, 'semantic-v1', CAST(strftime('%s','now') AS INTEGER) * 1000);"

    "CREATE TABLE IF NOT EXISTS models ("
    "id TEXT PRIMARY KEY,"
    "canonical_slug TEXT,"
    "hugging_face_id TEXT,"
    "name TEXT NOT NULL,"
    "description TEXT,"
    "context_length INTEGER NOT NULL DEFAULT 0 CHECK(context_length >= 0),"
    "created_at_s INTEGER NOT NULL DEFAULT 0,"
    "architecture_modality TEXT,"
    "architecture_tokenizer TEXT,"
    "architecture_instruct_type TEXT,"
    "provider_context_length INTEGER NOT NULL DEFAULT 0 "
      "CHECK(provider_context_length >= 0),"
    "provider_max_completion_tokens INTEGER NOT NULL DEFAULT 0 "
      "CHECK(provider_max_completion_tokens >= 0),"
    "provider_is_moderated INTEGER NOT NULL DEFAULT 0 "
      "CHECK(provider_is_moderated IN (0,1)),"
    "knowledge_cutoff TEXT,"
    "expiration_date TEXT,"
    "details_url TEXT,"
    "catalog_active INTEGER NOT NULL DEFAULT 1 "
      "CHECK(catalog_active IN (0,1)),"
    "last_seen_at_ms INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS models_name_idx ON models(name, id);"
    "CREATE TABLE IF NOT EXISTS model_prices ("
    "model_id TEXT NOT NULL,"
    "price_kind TEXT NOT NULL,"
    "price_decimal TEXT NOT NULL,"
    "PRIMARY KEY(model_id, price_kind),"
    "FOREIGN KEY(model_id) REFERENCES models(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS model_features ("
    "model_id TEXT NOT NULL,"
    "feature_kind TEXT NOT NULL CHECK(feature_kind IN "
      "('input_modality','output_modality','parameter','voice','reasoning')),"
    "feature_value TEXT NOT NULL,"
    "PRIMARY KEY(model_id, feature_kind, feature_value),"
    "FOREIGN KEY(model_id) REFERENCES models(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS model_preferences ("
    "model_id TEXT PRIMARY KEY,"
    "allowed INTEGER NOT NULL DEFAULT 0 CHECK(allowed IN (0,1)),"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(model_id) REFERENCES models(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS app_preferences ("
    "id INTEGER PRIMARY KEY CHECK(id = 1),"
    "default_model_id TEXT,"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(default_model_id) REFERENCES models(id)"
    ");"
    "INSERT OR IGNORE INTO app_preferences (id, updated_at_ms) VALUES "
      "(1, CAST(strftime('%s','now') AS INTEGER) * 1000);";

  static const char schema_sessions_sql[] =
    "CREATE TABLE IF NOT EXISTS default_session_options ("
    "id INTEGER PRIMARY KEY CHECK(id = 1),"
    "assistant_set_id TEXT NOT NULL DEFAULT '"
      STRAPPY_ASSISTANT_SET_DEFAULT "' CHECK(length(assistant_set_id) > 0),"
    "web_provider TEXT NOT NULL DEFAULT '"
      STRAPPY_DB_DEFAULT_SESSION_WEB_PROVIDER "' "
      "CHECK(web_provider IN ('none','auto','native','exa','parallel')),"
    "web_search_enabled INTEGER NOT NULL DEFAULT "
      STRAPPY_DB_DEFAULT_SESSION_WEB_SEARCH_ENABLED " "
      "CHECK(web_search_enabled IN (0,1)),"
    "bash_enabled INTEGER NOT NULL DEFAULT 0 "
      "CHECK(bash_enabled IN (0,1)),"
    "limit_to_one_tool INTEGER NOT NULL DEFAULT 0 "
      "CHECK(limit_to_one_tool IN (0,1)),"
    "answer_quality_enabled INTEGER NOT NULL DEFAULT 0 "
      "CHECK(answer_quality_enabled IN (0,1)),"
    "round_limit INTEGER NOT NULL DEFAULT "
      STRAPPY_DB_STRINGIFY(STRAPPY_SESSION_DEFAULT_ROUND_LIMIT) " "
      "CHECK(round_limit BETWEEN 1 AND "
        STRAPPY_DB_STRINGIFY(STRAPPY_SESSION_MAX_LIMIT) "),"
    "working_directory TEXT "
      "CHECK(working_directory IS NULL OR length(working_directory) > 0),"
    "updated_at_ms INTEGER NOT NULL"
    ");"
    "INSERT OR IGNORE INTO default_session_options (id, updated_at_ms) VALUES "
      "(1, CAST(strftime('%s','now') AS INTEGER) * 1000);"
    "CREATE TABLE IF NOT EXISTS sessions ("
    "id INTEGER PRIMARY KEY,"
    "name TEXT NOT NULL DEFAULT '',"
    "model_id TEXT,"
    "created_at_ms INTEGER NOT NULL,"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(model_id) REFERENCES models(id)"
    ");"
    "CREATE INDEX IF NOT EXISTS sessions_updated_idx "
      "ON sessions(updated_at_ms DESC, id DESC);"
    "CREATE TABLE IF NOT EXISTS session_assistant_sets ("
    "session_id INTEGER PRIMARY KEY,"
    "assistant_set_id TEXT NOT NULL CHECK(length(assistant_set_id) > 0),"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS session_settings ("
    "session_id INTEGER PRIMARY KEY,"
    "web_provider TEXT NOT NULL DEFAULT '"
      STRAPPY_DB_DEFAULT_SESSION_WEB_PROVIDER "' "
      "CHECK(web_provider IN ('none','auto','native','exa','parallel')),"
    "web_search_enabled INTEGER NOT NULL DEFAULT "
      STRAPPY_DB_DEFAULT_SESSION_WEB_SEARCH_ENABLED " "
      "CHECK(web_search_enabled IN (0,1)),"
    "bash_enabled INTEGER NOT NULL DEFAULT 0 "
      "CHECK(bash_enabled IN (0,1)),"
    "limit_to_one_tool INTEGER NOT NULL DEFAULT 0 "
      "CHECK(limit_to_one_tool IN (0,1)),"
    "answer_quality_enabled INTEGER NOT NULL DEFAULT 0 "
      "CHECK(answer_quality_enabled IN (0,1)),"
    "round_limit INTEGER NOT NULL DEFAULT "
      STRAPPY_DB_STRINGIFY(STRAPPY_SESSION_DEFAULT_ROUND_LIMIT) " "
      "CHECK(round_limit BETWEEN 1 AND "
        STRAPPY_DB_STRINGIFY(STRAPPY_SESSION_MAX_LIMIT) "),"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS session_working_directories ("
    "session_id INTEGER PRIMARY KEY,"
    "working_directory TEXT NOT NULL CHECK(length(working_directory) > 0),"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS turns ("
    "id INTEGER PRIMARY KEY,"
    "session_id INTEGER NOT NULL,"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "prompt_group_key TEXT NOT NULL,"
    "state TEXT NOT NULL DEFAULT 'running' CHECK(state IN "
      "('running','completed','error','cancelled')),"
    "created_at_ms INTEGER NOT NULL,"
    "completed_at_ms INTEGER,"
    "UNIQUE(session_id, ordinal),"
    "UNIQUE(session_id, prompt_group_key),"
    "UNIQUE(id, session_id),"
    "FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE"
    ");"

    "CREATE TABLE IF NOT EXISTS instruction_revisions ("
    "id INTEGER PRIMARY KEY,"
    "digest TEXT NOT NULL UNIQUE,"
    "text TEXT NOT NULL,"
    "created_at_ms INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS toolset_revisions ("
    "id INTEGER PRIMARY KEY,"
    "digest TEXT NOT NULL UNIQUE,"
    "resource_version TEXT,"
    "created_at_ms INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS toolset_members ("
    "toolset_revision_id INTEGER NOT NULL,"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "tool_name TEXT NOT NULL,"
    "definition_digest TEXT,"
    "PRIMARY KEY(toolset_revision_id, ordinal),"
    "UNIQUE(toolset_revision_id, tool_name),"
    "FOREIGN KEY(toolset_revision_id) REFERENCES toolset_revisions(id) "
      "ON DELETE CASCADE"
    ");";

  static const char schema_requests_sql[] =
    "CREATE TABLE IF NOT EXISTS model_requests ("
    "id INTEGER PRIMARY KEY,"
    "turn_id INTEGER NOT NULL,"
    "previous_request_id INTEGER,"
    "round_index INTEGER NOT NULL CHECK(round_index >= 0),"
    "request_kind TEXT NOT NULL,"
    "model_id TEXT NOT NULL,"
    "instruction_revision_id INTEGER,"
    "toolset_revision_id INTEGER,"
    "input_from_sequence INTEGER NOT NULL DEFAULT 1 "
      "CHECK(input_from_sequence >= 1),"
    "input_through_sequence INTEGER NOT NULL DEFAULT 0 "
      "CHECK(input_through_sequence >= 0),"
    "new_input_from_sequence INTEGER,"
    "max_output_tokens INTEGER,"
    "temperature_millionths INTEGER,"
    "web_provider TEXT NOT NULL DEFAULT 'none' "
      "CHECK(web_provider IN ('none','auto','native','exa','parallel')),"
    "stream_enabled INTEGER NOT NULL DEFAULT 0 CHECK(stream_enabled IN (0,1)),"
    "reasoning_enabled INTEGER NOT NULL DEFAULT 1 "
      "CHECK(reasoning_enabled IN (0,1)),"
    "reasoning_summary TEXT,"
    "parallel_tool_calls INTEGER NOT NULL DEFAULT 1 "
      "CHECK(parallel_tool_calls IN (0,1)),"
    "tool_calls_enabled INTEGER NOT NULL DEFAULT 1 "
      "CHECK(tool_calls_enabled IN (0,1)),"
    "state TEXT NOT NULL DEFAULT 'pending' CHECK(state IN "
      "('pending','running','completed','error','cancelled')),"
    "created_at_ms INTEGER NOT NULL,"
    "completed_at_ms INTEGER,"
    "wall_duration_ms INTEGER CHECK(wall_duration_ms >= 0),"
    "UNIQUE(turn_id, round_index),"
    "FOREIGN KEY(turn_id) REFERENCES turns(id) ON DELETE CASCADE,"
    "FOREIGN KEY(previous_request_id) REFERENCES model_requests(id),"
    "FOREIGN KEY(model_id) REFERENCES models(id),"
    "FOREIGN KEY(instruction_revision_id) REFERENCES instruction_revisions(id),"
    "FOREIGN KEY(toolset_revision_id) REFERENCES toolset_revisions(id)"
    ");"
    "CREATE INDEX IF NOT EXISTS model_requests_turn_idx "
      "ON model_requests(turn_id, round_index);";

  static const char schema_attempts_sql[] =
    "CREATE TABLE IF NOT EXISTS http_attempts ("
    "id INTEGER PRIMARY KEY,"
    "request_id INTEGER NOT NULL,"
    "previous_attempt_id INTEGER,"
    "attempt_index INTEGER NOT NULL CHECK(attempt_index >= 0),"
    "state TEXT NOT NULL DEFAULT 'pending' CHECK(state IN "
      "('pending','running','completed','http_error','response_error',"
       "'transport_error','parse_error','cancelled')),"
    "method TEXT NOT NULL,"
    "endpoint TEXT NOT NULL,"
    "started_at_ms INTEGER NOT NULL,"
    "completed_at_ms INTEGER,"
    "http_status INTEGER NOT NULL DEFAULT 0,"
    "curl_code INTEGER NOT NULL DEFAULT 0,"
    "retry_after_seconds INTEGER NOT NULL DEFAULT 0,"
    "request_bytes INTEGER NOT NULL DEFAULT 0,"
    "response_bytes INTEGER NOT NULL DEFAULT 0,"
    "name_lookup_us INTEGER NOT NULL DEFAULT 0,"
    "connect_us INTEGER NOT NULL DEFAULT 0,"
    "start_transfer_us INTEGER NOT NULL DEFAULT 0,"
    "total_us INTEGER NOT NULL DEFAULT 0,"
    "effective_url TEXT,"
    "transport_error TEXT,"
    "content_type TEXT,"
    "provider_request_id TEXT,"
    "provider_generation_id TEXT,"
    "rate_limit_limit TEXT,"
    "rate_limit_remaining TEXT,"
    "rate_limit_reset TEXT,"
    "rate_limit_requests_limit TEXT,"
    "rate_limit_requests_remaining TEXT,"
    "rate_limit_requests_reset TEXT,"
    "rate_limit_tokens_limit TEXT,"
    "rate_limit_tokens_remaining TEXT,"
    "rate_limit_tokens_reset TEXT,"
    "UNIQUE(request_id, attempt_index),"
    "FOREIGN KEY(request_id) REFERENCES model_requests(id) ON DELETE CASCADE,"
    "FOREIGN KEY(previous_attempt_id) REFERENCES http_attempts(id)"
    ");"
    "CREATE INDEX IF NOT EXISTS http_attempts_request_idx "
      "ON http_attempts(request_id, attempt_index);"
    "CREATE TABLE IF NOT EXISTS api_results ("
    "attempt_id INTEGER PRIMARY KEY,"
    "provider_response_id TEXT,"
    "provider_model_id TEXT,"
    "provider_status TEXT,"
    "provider_created_at_s INTEGER,"
    "provider_completed_at_s INTEGER,"
    "incomplete_reason TEXT,"
    "error_type TEXT,"
    "error_code TEXT,"
    "error_message TEXT,"
    "error_parameter TEXT,"
    "parse_error TEXT,"
    "FOREIGN KEY(attempt_id) REFERENCES http_attempts(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS api_usage ("
    "attempt_id INTEGER PRIMARY KEY,"
    "input_tokens INTEGER NOT NULL DEFAULT 0,"
    "cached_input_tokens INTEGER NOT NULL DEFAULT 0,"
    "output_tokens INTEGER NOT NULL DEFAULT 0,"
    "reasoning_tokens INTEGER NOT NULL DEFAULT 0,"
    "total_tokens INTEGER NOT NULL DEFAULT 0,"
    "cost_nano_usd INTEGER,"
    "upstream_cost_nano_usd INTEGER,"
    "upstream_input_cost_nano_usd INTEGER,"
    "upstream_output_cost_nano_usd INTEGER,"
    "is_byok INTEGER NOT NULL DEFAULT 0 CHECK(is_byok IN (0,1)),"
    "FOREIGN KEY(attempt_id) REFERENCES api_results(attempt_id) ON DELETE CASCADE"
    ");";

  static const char schema_conversation_sql[] =
    "CREATE TABLE IF NOT EXISTS conversation_items ("
    "id INTEGER PRIMARY KEY,"
    "session_id INTEGER NOT NULL,"
    "turn_id INTEGER NOT NULL,"
    "sequence INTEGER NOT NULL CHECK(sequence >= 1),"
    "kind TEXT NOT NULL CHECK(kind IN "
      "('message','reasoning','function_call','function_call_output',"
       "'openrouter:web_search','openrouter:web_fetch')),"
    "introduced_request_id INTEGER,"
    "source_attempt_id INTEGER,"
    "source_item_index INTEGER NOT NULL DEFAULT 0 CHECK(source_item_index >= 0),"
    "provider_item_id TEXT,"
    "provider_status TEXT,"
    "include_in_context INTEGER NOT NULL DEFAULT 1 "
      "CHECK(include_in_context IN (0,1)),"
    "timeline_visible INTEGER NOT NULL DEFAULT 1 "
      "CHECK(timeline_visible IN (0,1)),"
    "is_error INTEGER NOT NULL DEFAULT 0 CHECK(is_error IN (0,1)),"
    "created_at_ms INTEGER NOT NULL,"
    "UNIQUE(session_id, sequence),"
    "UNIQUE(source_attempt_id, source_item_index),"
    "FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE,"
    "FOREIGN KEY(turn_id, session_id) REFERENCES turns(id, session_id) "
      "ON DELETE CASCADE,"
    "FOREIGN KEY(introduced_request_id) REFERENCES model_requests(id) "
      "ON DELETE CASCADE,"
    "FOREIGN KEY(source_attempt_id) REFERENCES http_attempts(id) "
      "ON DELETE CASCADE"
    ");"
    "CREATE INDEX IF NOT EXISTS conversation_items_context_idx "
      "ON conversation_items(session_id, include_in_context, sequence);"
    "CREATE INDEX IF NOT EXISTS conversation_items_request_idx "
      "ON conversation_items(introduced_request_id, source_item_index);"
    "CREATE INDEX IF NOT EXISTS conversation_items_attempt_idx "
      "ON conversation_items(source_attempt_id, source_item_index);"
    "CREATE TABLE IF NOT EXISTS message_items ("
    "item_id INTEGER PRIMARY KEY,"
    "role TEXT NOT NULL CHECK(role IN ('user','assistant','developer','system')),"
    "phase TEXT,"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS item_text_parts ("
    "id INTEGER PRIMARY KEY,"
    "item_id INTEGER NOT NULL,"
    "collection_name TEXT NOT NULL CHECK(collection_name IN ('content','summary')),"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "part_type TEXT NOT NULL CHECK(part_type IN "
      "('input_text','output_text','refusal','reasoning_text','summary_text')),"
    "text TEXT NOT NULL,"
    "UNIQUE(item_id, collection_name, ordinal),"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS item_citations ("
    "id INTEGER PRIMARY KEY,"
    "text_part_id INTEGER NOT NULL,"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "citation_type TEXT NOT NULL,"
    "start_offset INTEGER,"
    "end_offset INTEGER,"
    "title TEXT,"
    "url TEXT,"
    "excerpt TEXT,"
    "UNIQUE(text_part_id, ordinal),"
    "FOREIGN KEY(text_part_id) REFERENCES item_text_parts(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS reasoning_items ("
    "item_id INTEGER PRIMARY KEY,"
    "encrypted_content TEXT,"
    "provider_format TEXT,"
    "provider_signature TEXT,"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE"
    ");";

  static const char schema_tool_items_sql[] =
    "CREATE TABLE IF NOT EXISTS function_calls ("
    "item_id INTEGER PRIMARY KEY,"
    "provider_call_id TEXT NOT NULL UNIQUE,"
    "tool_name TEXT NOT NULL,"
    "tool_namespace TEXT,"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS function_outputs ("
    "item_id INTEGER PRIMARY KEY,"
    "function_call_item_id INTEGER NOT NULL UNIQUE,"
    "execution_state TEXT NOT NULL DEFAULT 'completed' CHECK(execution_state IN "
      "('pending','running','completed','error','cancelled')),"
    "started_at_ms INTEGER,"
    "completed_at_ms INTEGER,"
    "output_format TEXT NOT NULL DEFAULT 'text' "
      "CHECK(output_format IN ('text','structured')),"
    "text_output TEXT,"
    "error_code TEXT,"
    "error_message TEXT,"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE,"
    "FOREIGN KEY(function_call_item_id) REFERENCES function_calls(item_id)"
    ");"
    "CREATE TABLE IF NOT EXISTS tool_executions ("
    "id INTEGER PRIMARY KEY,"
    "function_call_item_id INTEGER NOT NULL UNIQUE,"
    "response_attempt_id INTEGER NOT NULL,"
    "output_index INTEGER NOT NULL CHECK(output_index >= 0),"
    "state TEXT NOT NULL CHECK(state IN "
      "('pending','running','completed','error','cancelled')),"
    "started_at_ms INTEGER,"
    "completed_at_ms INTEGER,"
    "error_code TEXT,"
    "error_message TEXT,"
    "FOREIGN KEY(function_call_item_id) REFERENCES function_calls(item_id) "
      "ON DELETE CASCADE,"
    "FOREIGN KEY(response_attempt_id) REFERENCES http_attempts(id) "
      "ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS web_searches ("
    "item_id INTEGER PRIMARY KEY,"
    "action_type TEXT NOT NULL,"
    "query TEXT,"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS web_search_sources ("
    "web_search_item_id INTEGER NOT NULL,"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "source_type TEXT NOT NULL,"
    "url TEXT NOT NULL,"
    "PRIMARY KEY(web_search_item_id, ordinal),"
    "FOREIGN KEY(web_search_item_id) REFERENCES web_searches(item_id) "
      "ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS web_fetches ("
    "item_id INTEGER PRIMARY KEY,"
    "url TEXT NOT NULL,"
    "title TEXT,"
    "content TEXT,"
    "http_status INTEGER,"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE"
    ");";

  static const char schema_answer_quality_sql[] =
    "CREATE TABLE IF NOT EXISTS answer_quality_audits ("
    "id INTEGER PRIMARY KEY,"
    "response_attempt_id INTEGER NOT NULL UNIQUE,"
    "outcome TEXT NOT NULL CHECK(outcome IN ('passed','failed','error')),"
    "guidance_version TEXT,"
    "evaluated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(response_attempt_id) REFERENCES http_attempts(id) "
      "ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS answer_quality_checks ("
    "audit_id INTEGER NOT NULL,"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "check_key TEXT NOT NULL,"
    "check_kind TEXT NOT NULL CHECK(check_kind IN "
      "('required_tool','answer_content')),"
    "label TEXT NOT NULL,"
    "status TEXT NOT NULL CHECK(status IN "
      "('passed','failed','not_applicable','error')),"
    "tool_name TEXT,"
    "detail TEXT,"
    "evidence_item_id INTEGER,"
    "PRIMARY KEY(audit_id, ordinal),"
    "UNIQUE(audit_id, check_key),"
    "FOREIGN KEY(audit_id) REFERENCES answer_quality_audits(id) "
      "ON DELETE CASCADE,"
    "FOREIGN KEY(evidence_item_id) REFERENCES conversation_items(id) "
      "ON DELETE SET NULL"
    ");";

  static const char schema_structured_sql[] =
    "CREATE TABLE IF NOT EXISTS structured_documents ("
    "id INTEGER PRIMARY KEY,"
    "owner_item_id INTEGER NOT NULL,"
    "purpose TEXT NOT NULL CHECK(purpose IN ('arguments','output')),"
    "UNIQUE(owner_item_id, purpose),"
    "FOREIGN KEY(owner_item_id) REFERENCES conversation_items(id) "
      "ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS structured_nodes ("
    "document_id INTEGER NOT NULL,"
    "node_id INTEGER NOT NULL CHECK(node_id >= 0),"
    "parent_node_id INTEGER,"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "member_name TEXT,"
    "value_type TEXT NOT NULL CHECK(value_type IN "
      "('object','array','string','number','boolean','null')),"
    "text_value TEXT,"
    "number_value TEXT,"
    "boolean_value INTEGER CHECK(boolean_value IN (0,1)),"
    "PRIMARY KEY(document_id, node_id),"
    "UNIQUE(document_id, parent_node_id, ordinal),"
    "CHECK((node_id = 0 AND parent_node_id IS NULL) OR "
      "(node_id > 0 AND parent_node_id IS NOT NULL)),"
    "FOREIGN KEY(document_id) REFERENCES structured_documents(id) "
      "ON DELETE CASCADE,"
    "FOREIGN KEY(document_id, parent_node_id) "
      "REFERENCES structured_nodes(document_id, node_id) ON DELETE CASCADE"
    ");";

  static const char schema_catalog_sql[] =
    "CREATE TABLE IF NOT EXISTS applications ("
    "id INTEGER PRIMARY KEY,"
    "stable_key TEXT UNIQUE,"
    "name TEXT,"
    "bundle_id TEXT,"
    "container_path TEXT,"
    "bundle_path TEXT,"
    "source TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS scan_roots ("
    "id INTEGER PRIMARY KEY,"
    "path TEXT NOT NULL UNIQUE,"
    "kind TEXT NOT NULL DEFAULT 'filesystem',"
    "last_started_at_ms INTEGER,"
    "last_completed_at_ms INTEGER"
    ");"
    "CREATE TABLE IF NOT EXISTS scan_runs ("
    "id INTEGER PRIMARY KEY,"
    "scan_root_id INTEGER NOT NULL,"
    "state TEXT NOT NULL CHECK(state IN ('running','completed','error','cancelled')),"
    "started_at_ms INTEGER NOT NULL,"
    "completed_at_ms INTEGER,"
    "candidate_count INTEGER NOT NULL DEFAULT 0,"
    "database_count INTEGER NOT NULL DEFAULT 0,"
    "error_message TEXT,"
    "FOREIGN KEY(scan_root_id) REFERENCES scan_roots(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS databases ("
    "id INTEGER PRIMARY KEY,"
    "assistant_database_id TEXT UNIQUE,"
    "stable_key TEXT NOT NULL UNIQUE,"
    "application_id INTEGER,"
    "display_name TEXT,"
    "origin_kind TEXT NOT NULL DEFAULT 'other',"
    "location_tail TEXT,"
    "first_seen_at_ms INTEGER NOT NULL,"
    "last_seen_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(application_id) REFERENCES applications(id)"
    ");"
    "CREATE TABLE IF NOT EXISTS database_locations ("
    "id INTEGER PRIMARY KEY,"
    "database_id INTEGER NOT NULL,"
    "path TEXT NOT NULL UNIQUE,"
    "scan_root_id INTEGER,"
    "last_scan_run_id INTEGER,"
    "device INTEGER NOT NULL DEFAULT 0,"
    "inode INTEGER NOT NULL DEFAULT 0,"
    "size_bytes INTEGER NOT NULL DEFAULT 0,"
    "modified_at_s INTEGER NOT NULL DEFAULT 0,"
    "validation_state TEXT NOT NULL DEFAULT 'candidate' CHECK(validation_state IN "
      "('candidate','valid','invalid')),"
    "validation_error TEXT,"
    "active INTEGER NOT NULL DEFAULT 1 CHECK(active IN (0,1)),"
    "first_seen_at_ms INTEGER NOT NULL,"
    "last_seen_at_ms INTEGER NOT NULL,"
    "last_scanned_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(database_id) REFERENCES databases(id) ON DELETE CASCADE,"
    "FOREIGN KEY(scan_root_id) REFERENCES scan_roots(id),"
    "FOREIGN KEY(last_scan_run_id) REFERENCES scan_runs(id)"
    ");"
    "CREATE INDEX IF NOT EXISTS database_locations_device_inode_idx "
      "ON database_locations(device, inode);"
    "CREATE INDEX IF NOT EXISTS database_locations_database_idx "
      "ON database_locations(database_id, active, path);"
    "CREATE TABLE IF NOT EXISTS database_permissions ("
    "database_id INTEGER PRIMARY KEY,"
    "decision TEXT NOT NULL DEFAULT 'unknown' "
      "CHECK(decision IN ('unknown','allowed','denied')),"
    "hidden INTEGER NOT NULL DEFAULT 0 CHECK(hidden IN (0,1)),"
    "auto_hidden INTEGER NOT NULL DEFAULT 0 CHECK(auto_hidden IN (0,1)),"
    "hidden_override INTEGER CHECK(hidden_override IN (0,1)),"
    "hidden_reason TEXT,"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(database_id) REFERENCES databases(id) ON DELETE CASCADE"
    ");"
    "CREATE INDEX IF NOT EXISTS database_permissions_decision_idx "
      "ON database_permissions(decision, hidden, database_id);";

  static const char schema_memory_sql[] =
    "CREATE TABLE IF NOT EXISTS user_facts ("
    "id INTEGER PRIMARY KEY,"
    "assistant_set_id TEXT NOT NULL CHECK(length(assistant_set_id) > 0),"
    "kind TEXT NOT NULL,"
    "subject TEXT,"
    "predicate TEXT,"
    "value TEXT NOT NULL,"
    "confidence_basis_points INTEGER NOT NULL DEFAULT 10000 "
      "CHECK(confidence_basis_points BETWEEN 0 AND 10000),"
    "source_item_id INTEGER,"
    "created_at_ms INTEGER NOT NULL,"
    "updated_at_ms INTEGER NOT NULL,"
    "last_used_at_ms INTEGER,"
    "FOREIGN KEY(source_item_id) REFERENCES conversation_items(id) "
      "ON DELETE SET NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS user_facts_assistant_set_kind_idx "
      "ON user_facts(assistant_set_id, kind, updated_at_ms DESC, id DESC);"
    "CREATE TABLE IF NOT EXISTS database_hints ("
    "id INTEGER PRIMARY KEY,"
    "database_id INTEGER NOT NULL,"
    "kind TEXT NOT NULL,"
    "content TEXT NOT NULL,"
    "observed_modified_at_s INTEGER NOT NULL DEFAULT 0,"
    "source_item_id INTEGER,"
    "created_at_ms INTEGER NOT NULL,"
    "updated_at_ms INTEGER NOT NULL,"
    "last_used_at_ms INTEGER,"
    "FOREIGN KEY(database_id) REFERENCES databases(id) ON DELETE CASCADE,"
    "FOREIGN KEY(source_item_id) REFERENCES conversation_items(id) "
      "ON DELETE SET NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS database_hints_database_idx "
      "ON database_hints(database_id, kind, updated_at_ms DESC);"

    "PRAGMA user_version = 1;";
  static const char *const schema_sql[] = {
    schema_models_sql,
    schema_sessions_sql,
    schema_requests_sql,
    schema_attempts_sql,
    schema_conversation_sql,
    schema_tool_items_sql,
    schema_answer_quality_sql,
    schema_structured_sql,
    schema_catalog_sql,
    schema_memory_sql
  };
  size_t schema_index;

  for (schema_index = 0U;
       schema_index < (sizeof(schema_sql) / sizeof(schema_sql[0]));
       schema_index++) {
    if (!strappy_db_exec(db,
                         schema_sql[schema_index],
                         "Could not create semantic database schema",
                         error_out)) {
      return 0;
    }
  }
  if (!strappy_db_exec(db,
                       STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL,
                       "Could not create built-in default model",
                       error_out)) {
    return 0;
  }
  return strappy_db_exec(db,
                         "PRAGMA foreign_keys = ON;",
                         "Could not enable foreign keys",
                         error_out);
}

int strappy_db_ensure_schema(sqlite3 *db, char **error_out)
{
  int ok;

  if ((db == strappy_db_connection) &&
      strappy_db_connection_schema_ready) {
    return 1;
  }
  ok = strappy_db_ensure_semantic_schema(db, error_out);
  if (ok && (db == strappy_db_connection)) {
    strappy_db_connection_schema_ready = 1;
  }
  return ok;
}

char *strappy_db_column_string(sqlite3_stmt *stmt, int column)
{
  const unsigned char *value;

  value = sqlite3_column_text(stmt, column);
  if (value == NULL) {
    return NULL;
  }

  return strappy_string_duplicate((const char *)value);
}

int strappy_db_session_exists(sqlite3 *db,
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

long long strappy_db_now_ms(void)
{
  sqlite3_int64 value;

  value = (sqlite3_int64)time(NULL);
  return ((long long)value) * 1000LL;
}

int strappy_db_initialize(const char *db_path, char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  ok = strappy_db_ensure_schema(db, error_out);
  strappy_db_release(db);
  return ok;
}
