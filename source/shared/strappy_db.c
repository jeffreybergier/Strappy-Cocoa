#include "strappy_db.h"

#include "strappy_core.h"

#include <cJSON.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void strappy_session_record_init(strappy_session_record *record)
{
  if (record == NULL) {
    return;
  }

  record->session_id = 0;
  record->prompt = NULL;
  record->response = NULL;
  record->model = NULL;
  record->created_at = NULL;
  record->http_status = 0L;
}

void strappy_session_record_destroy(strappy_session_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->prompt);
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
  record->actor = NULL;
  record->kind = NULL;
  record->api_role = NULL;
  record->render_role = NULL;
  record->role = NULL;
  record->content = NULL;
  record->model = NULL;
  record->metadata_json = NULL;
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
  free(record->actor);
  free(record->kind);
  free(record->api_role);
  free(record->render_role);
  free(record->role);
  free(record->content);
  free(record->model);
  free(record->metadata_json);
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

static int strappy_db_ensure_schema(sqlite3 *db, char **error_out)
{
  static const char *sessions_sql =
    "CREATE TABLE IF NOT EXISTS sessions ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "prompt TEXT NOT NULL,"
    "response TEXT NOT NULL,"
    "model TEXT,"
    "http_status INTEGER NOT NULL DEFAULT 0,"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *turns_sql =
    "CREATE TABLE IF NOT EXISTS session_turns ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "session_id INTEGER NOT NULL,"
    "turn_key TEXT NOT NULL,"
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
    "actor TEXT,"
    "kind TEXT NOT NULL DEFAULT 'message',"
    "api_role TEXT,"
    "render_role TEXT NOT NULL,"
    "role TEXT NOT NULL,"
    "content TEXT NOT NULL,"
    "model TEXT,"
    "http_status INTEGER NOT NULL DEFAULT 0,"
    "metadata_json TEXT,"
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

static int strappy_db_assign_record_from_statement(strappy_session_record *record,
                                                   sqlite3_stmt *stmt,
                                                   char **error_out)
{
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
  record->http_status = (long)sqlite3_column_int64(stmt, 4);

  prompt = strappy_db_column_string(stmt, 1);
  response = strappy_db_column_string(stmt, 2);
  model = strappy_db_column_string(stmt, 3);
  created_at = strappy_db_column_string(stmt, 5);

  if ((prompt == NULL) || (response == NULL) || (created_at == NULL)) {
    free(prompt);
    free(response);
    free(model);
    free(created_at);
    strappy_set_error(error_out, "Could not allocate session row.");
    return 0;
  }

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
  char *actor;
  char *kind;
  char *api_role;
  char *render_role;
  char *role;
  char *content;
  char *model;
  char *metadata_json;
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
  record->http_status = (long)sqlite3_column_int64(stmt, 11);
  record->include_in_context = sqlite3_column_int(stmt, 20) ? 1 : 0;
  record->is_error = sqlite3_column_int(stmt, 21) ? 1 : 0;

  turn_key = strappy_db_column_string(stmt, 3);
  actor = strappy_db_column_string(stmt, 4);
  kind = strappy_db_column_string(stmt, 5);
  api_role = strappy_db_column_string(stmt, 6);
  render_role = strappy_db_column_string(stmt, 7);
  role = strappy_db_column_string(stmt, 8);
  content = strappy_db_column_string(stmt, 9);
  model = strappy_db_column_string(stmt, 10);
  metadata_json = strappy_db_column_string(stmt, 12);
  message_json = strappy_db_column_string(stmt, 13);
  reasoning = strappy_db_column_string(stmt, 14);
  message_key = strappy_db_column_string(stmt, 15);
  target_message_key = strappy_db_column_string(stmt, 16);
  tool_call_id = strappy_db_column_string(stmt, 17);
  tool_name = strappy_db_column_string(stmt, 18);
  arguments_json = strappy_db_column_string(stmt, 19);
  result_json = strappy_db_column_string(stmt, 22);
  created_at = strappy_db_column_string(stmt, 23);

  if ((kind == NULL) || (render_role == NULL) || (role == NULL) ||
      (content == NULL) || (created_at == NULL)) {
    free(turn_key);
    free(actor);
    free(kind);
    free(api_role);
    free(render_role);
    free(role);
    free(content);
    free(model);
    free(metadata_json);
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
  record->actor = actor;
  record->kind = kind;
  record->api_role = api_role;
  record->render_role = render_role;
  record->role = role;
  record->content = content;
  record->model = model;
  record->metadata_json = metadata_json;
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
    "(session_id, turn_key, actor, api_role, render_role, context_policy, "
    "prompt, status) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, 'complete');";
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
  if (ok && (sqlite3_bind_text(stmt,
                               3,
                               strappy_db_input_actor(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 4, api_role, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt,
                               5,
                               strappy_db_input_render_role(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt,
                               6,
                               strappy_db_input_context_policy(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 7, prompt, -1, SQLITE_TRANSIENT) !=
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
    "(session_id, turn_id, turn_key, actor, kind, api_role, render_role, role, "
    "content, model, http_status, metadata_json, message_json, reasoning, "
    "message_key, target_message_key, tool_call_id, tool_name, arguments_json, "
    "result_json, include_in_context, is_error) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
                                           message->actor,
                                           "Could not bind session message",
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
                                           "Could not bind session message",
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
                                           "Could not bind session message",
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
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           15,
                                           message->message_key,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           16,
                                           message->target_message_key,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           17,
                                           message->tool_call_id,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           18,
                                           message->tool_name,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           19,
                                           message->arguments_json,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           20,
                                           message->result_json,
                                           "Could not bind session message",
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
    if (!strappy_db_insert_message(db,
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
                                             const char *response,
                                             const char *model,
                                             long http_status,
                                             char **error_out)
{
  static const char *update_sql =
    "UPDATE sessions "
    "SET response = ?, model = ?, http_status = ? "
    "WHERE id = ?;";
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if ((session_id <= 0) || (response == NULL)) {
    strappy_set_error(error_out, "Session summary update is incomplete.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, response, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok && (model != NULL) &&
      (sqlite3_bind_text(stmt, 2, model, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model == NULL) && (sqlite3_bind_null(stmt, 2) != SQLITE_OK)) {
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
    "SELECT id, prompt, response, model, http_status, created_at "
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
    "SELECT id, prompt, response, model, http_status, created_at "
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
    "SET response = ?, model = ?, http_status = ? "
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
  if (sqlite3_bind_text(stmt, 1, response, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok && (model != NULL) &&
      (sqlite3_bind_text(stmt, 2, model, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model == NULL) && (sqlite3_bind_null(stmt, 2) != SQLITE_OK)) {
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

static const char *strappy_db_session_message_select_clause(void)
{
  return
    "SELECT id, session_id, COALESCE(turn_id, 0), turn_key, actor, kind, "
    "api_role, render_role, role, content, model, http_status, metadata_json, "
    "message_json, reasoning, message_key, target_message_key, tool_call_id, "
    "tool_name, arguments_json, include_in_context, is_error, result_json, "
    "created_at "
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
