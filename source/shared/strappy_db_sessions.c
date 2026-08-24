#include "strappy_db_internal.h"

#include "strappy_assistant_sets.h"
#include "strappy_core.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Session lifecycle, listing, and per-session option persistence. */

#define STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL \
  "COALESCE(s.model_id, " STRAPPY_DB_DEFAULT_MODEL_SQL ")"
#define STRAPPY_DB_SESSION_EFFECTIVE_ACCOUNT_SQL \
  "COALESCE(s.provider_account_id, (SELECT a.id FROM provider_accounts a " \
  "JOIN models m ON m.provider_id=a.provider_id WHERE m.id = " \
  STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL " AND a.lifecycle_state='active' " \
  "ORDER BY CASE WHEN a.id=(SELECT default_provider_account_id FROM " \
  "app_preferences WHERE id=1) THEN 0 ELSE 1 END,a.last_used_at_ms DESC," \
  "a.created_at_ms,a.id LIMIT 1))"
#define STRAPPY_DB_SESSION_ASSISTANT_SET_SQL \
  "COALESCE((SELECT a.assistant_set_id FROM session_assistant_sets a " \
  "WHERE a.session_id = s.id), '" STRAPPY_ASSISTANT_SET_DEFAULT "')"
#define STRAPPY_DB_SESSION_WEB_PROVIDER_SQL \
  "COALESCE((SELECT x.web_provider FROM session_settings x " \
  "WHERE x.session_id = s.id), '" \
  STRAPPY_DB_DEFAULT_SESSION_WEB_PROVIDER "')"
#define STRAPPY_DB_SESSION_WEB_SEARCH_ENABLED_SQL \
  "COALESCE((SELECT x.web_search_enabled FROM session_settings x " \
  "WHERE x.session_id = s.id), " \
  STRAPPY_DB_DEFAULT_SESSION_WEB_SEARCH_ENABLED ")"
#define STRAPPY_DB_SESSION_BASH_ENABLED_SQL \
  "COALESCE((SELECT b.bash_enabled FROM session_settings b " \
  "WHERE b.session_id = s.id), 0)"
#define STRAPPY_DB_SESSION_LIMIT_TO_ONE_TOOL_SQL \
  "COALESCE((SELECT x.limit_to_one_tool FROM session_settings x " \
  "WHERE x.session_id = s.id), 0)"
#define STRAPPY_DB_SESSION_ANSWER_QUALITY_ENABLED_SQL \
  "COALESCE((SELECT x.answer_quality_enabled FROM session_settings x " \
  "WHERE x.session_id = s.id), 0)"
#define STRAPPY_DB_SESSION_ROUND_LIMIT_SQL \
  "COALESCE((SELECT x.round_limit FROM session_settings x " \
  "WHERE x.session_id = s.id), " \
  STRAPPY_DB_STRINGIFY(STRAPPY_SESSION_DEFAULT_ROUND_LIMIT) ")"
#define STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL \
  "COALESCE((SELECT i.created_at_ms FROM conversation_items i " \
  "WHERE i.session_id = s.id ORDER BY i.sequence DESC LIMIT 1), " \
  "s.created_at_ms)"

static int strappy_db_copy_default_session_options(
  sqlite3 *db,
  const char *fallback_working_directory,
  strappy_session_options *options,
  char **error_out);
static int strappy_db_save_session_options_settings(
  sqlite3 *db,
  long long session_id,
  const strappy_session_options *options,
  long long now_ms,
  char **error_out);
static int strappy_db_save_session_options_assistant_set(
  sqlite3 *db,
  long long session_id,
  const char *assistant_set_id,
  long long now_ms,
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
  record->model_name = NULL;
  record->provider_account_id = NULL;
  record->assistant_set_id = NULL;
  record->created_at = NULL;
  record->last_activity_at = NULL;
  record->last_activity_at_ms = 0LL;
  record->web_provider = STRAPPY_WEB_PROVIDER_AUTO;
  record->web_search_enabled = 0;
  record->bash_enabled = 0;
  record->limit_to_one_tool = 0;
  record->answer_quality_enabled = 0;
  record->round_limit = STRAPPY_SESSION_DEFAULT_ROUND_LIMIT;
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
  free(record->model_name);
  free(record->provider_account_id);
  free(record->assistant_set_id);
  free(record->created_at);
  free(record->last_activity_at);
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

void strappy_session_options_init(strappy_session_options *options)
{
  if (options == NULL) {
    return;
  }
  options->model_id = NULL;
  options->provider_account_id = NULL;
  options->assistant_set_id = NULL;
  options->working_directory = NULL;
  options->web_provider = STRAPPY_WEB_PROVIDER_AUTO;
  options->web_search_enabled = 1;
  options->bash_enabled = 0;
  options->limit_to_one_tool = 0;
  options->answer_quality_enabled = 0;
  options->round_limit = STRAPPY_SESSION_DEFAULT_ROUND_LIMIT;
}

void strappy_session_options_destroy(strappy_session_options *options)
{
  if (options == NULL) {
    return;
  }
  free(options->model_id);
  free(options->provider_account_id);
  free(options->assistant_set_id);
  free(options->working_directory);
  strappy_session_options_init(options);
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

static int strappy_db_assign_record_from_statement(strappy_session_record *record,
                                                   sqlite3_stmt *stmt,
                                                   char **error_out)
{
  const unsigned char *web_provider;
  char *name;
  char *prompt;
  char *response;
  char *model;
  char *model_name;
  char *provider_account_id;
  char *assistant_set_id;
  char *created_at;
  char *last_activity_at;

  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Session row request is incomplete.");
    return 0;
  }

  strappy_session_record_destroy(record);
  record->session_id = (long long)sqlite3_column_int64(stmt, 0);
  record->http_status = (long)sqlite3_column_int64(stmt, 6);
  record->last_activity_at_ms = (long long)sqlite3_column_int64(stmt, 9);
  web_provider = sqlite3_column_text(stmt, 10);
  if ((web_provider == NULL) ||
      !strappy_web_provider_parse((const char *)web_provider,
                                  &record->web_provider)) {
    strappy_set_error(error_out, "Session web provider is invalid.");
    return 0;
  }
  record->bash_enabled = sqlite3_column_int(stmt, 11) ? 1 : 0;
  record->limit_to_one_tool = sqlite3_column_int(stmt, 12) ? 1 : 0;
  record->round_limit = (long)sqlite3_column_int64(stmt, 13);
  record->web_search_enabled = sqlite3_column_int(stmt, 15) ? 1 : 0;
  record->answer_quality_enabled = sqlite3_column_int(stmt, 16) ? 1 : 0;
  if ((record->round_limit < 1L) ||
      (record->round_limit > STRAPPY_SESSION_MAX_LIMIT)) {
    strappy_set_error(error_out, "Stored session limits are invalid.");
    return 0;
  }

  name = strappy_db_column_string(stmt, 1);
  prompt = strappy_db_column_string(stmt, 2);
  response = strappy_db_column_string(stmt, 3);
  model = strappy_db_column_string(stmt, 4);
  model_name = strappy_db_column_string(stmt, 5);
  provider_account_id = strappy_db_column_string(stmt, 17);
  assistant_set_id = strappy_db_column_string(stmt, 14);
  created_at = strappy_db_column_string(stmt, 7);
  last_activity_at = strappy_db_column_string(stmt, 8);

  if ((name == NULL) || (prompt == NULL) || (response == NULL) ||
      (model == NULL) || (model_name == NULL) || (created_at == NULL) ||
      (last_activity_at == NULL) || (provider_account_id == NULL) ||
      (assistant_set_id == NULL)) {
    free(name);
    free(prompt);
    free(response);
    free(model);
    free(model_name);
    free(provider_account_id);
    free(assistant_set_id);
    free(created_at);
    free(last_activity_at);
    strappy_set_error(error_out, "Could not allocate session row.");
    return 0;
  }

  record->name = name;
  record->prompt = prompt;
  record->response = response;
  record->model = model;
  record->model_name = model_name;
  record->provider_account_id = provider_account_id;
  record->assistant_set_id = assistant_set_id;
  record->created_at = created_at;
  record->last_activity_at = last_activity_at;
  return 1;
}

static int strappy_db_validate_working_directory(
  const char *working_directory,
  char **error_out)
{
  struct stat directory_stat;

  if ((working_directory == NULL) || (working_directory[0] == '\0')) {
    strappy_set_error(error_out, "Session working directory is empty.");
    return 0;
  }
  if (working_directory[0] != '/') {
    strappy_set_error(error_out,
                      "Session working directory must be an absolute path.");
    return 0;
  }
  errno = 0;
  if (stat(working_directory, &directory_stat) != 0) {
    strappy_set_formatted_error(error_out,
                                "Could not inspect session working directory "
                                "%s: %s.",
                                working_directory,
                                strerror(errno));
    return 0;
  }
  if (!S_ISDIR(directory_stat.st_mode)) {
    strappy_set_formatted_error(error_out,
                                "Session working directory is not a directory: "
                                "%s.",
                                working_directory);
    return 0;
  }
  return 1;
}

static int strappy_db_save_session_settings(sqlite3 *db,
                                            long long session_id,
                                            const strappy_session_options *options,
                                            char **error_out)
{
  return strappy_db_save_session_options_settings(db,
                                                  session_id,
                                                  options,
                                                  strappy_db_now_ms(),
                                                  error_out);
}

static int strappy_db_save_session_working_directory(
  sqlite3 *db,
  long long session_id,
  const char *working_directory,
  char **error_out)
{
  static const char *sql =
    "INSERT OR REPLACE INTO session_working_directories "
    "(session_id, working_directory, updated_at_ms) VALUES (?, ?, ?);";
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt,
                           2,
                           working_directory,
                           -1,
                           SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 3, (sqlite3_int64)strappy_db_now_ms());
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session working directory: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

int strappy_db_create_session_with_working_directory(
  const char *db_path,
  const char *working_directory,
  long long *session_id_out,
  char **error_out)
{
  static const char *sql =
    "INSERT INTO sessions "
    "(model_id, provider_account_id, created_at_ms, updated_at_ms) "
    "VALUES (?, ?, ?, ?);";
  strappy_session_options defaults;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  long long session_id;
  long long now_ms;

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

  if (!strappy_db_validate_working_directory(working_directory, error_out)) {
    return 0;
  }

  strappy_session_options_init(&defaults);
  stmt = NULL;
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin session insert",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_copy_default_session_options(db,
                                               working_directory,
                                               &defaults,
                                               error_out) ||
      !strappy_db_validate_working_directory(defaults.working_directory,
                                             error_out)) {
    goto rollback;
  }
  if ((defaults.model_id[0] == '\0') ||
      (defaults.provider_account_id[0] == '\0')) {
    strappy_set_error(error_out,
                      "Add an account before creating a session.");
    goto rollback;
  }

  now_ms = strappy_db_now_ms();
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session insert: %s",
                                sqlite3_errmsg(db));
    goto rollback;
  }

  rc = sqlite3_bind_text(stmt, 1, defaults.model_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, defaults.provider_account_id, -1,
                           SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ms);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 4, (sqlite3_int64)now_ms);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session insert: %s",
                                sqlite3_errmsg(db));
    goto rollback;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not create session: %s",
                                sqlite3_errmsg(db));
    goto rollback;
  }

  session_id = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);
  stmt = NULL;
  if (!strappy_db_save_session_settings(db,
                                        session_id,
                                        &defaults,
                                        error_out) ||
      !strappy_db_save_session_options_assistant_set(
        db,
        session_id,
        defaults.assistant_set_id,
        now_ms,
        error_out) ||
      !strappy_db_save_session_working_directory(db,
                                                  session_id,
                                                  defaults.working_directory,
                                                  error_out)) {
    goto rollback;
  }
  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit session insert",
                       error_out)) {
    goto rollback;
  }
  if (session_id_out != NULL) {
    *session_id_out = session_id;
  }
  strappy_session_options_destroy(&defaults);
  strappy_db_release(db);
  return 1;

rollback:
  sqlite3_finalize(stmt);
  strappy_db_exec(db,
                  "ROLLBACK;",
                  "Could not roll back session insert",
                  NULL);
  strappy_session_options_destroy(&defaults);
  strappy_db_release(db);
  return 0;
}

int strappy_db_create_session(const char *db_path,
                              long long *session_id_out,
                              char **error_out)
{
  const char *home_directory;

  home_directory = getenv("HOME");
  if ((home_directory == NULL) || (home_directory[0] == '\0')) {
    if (session_id_out != NULL) {
      *session_id_out = 0;
    }
    strappy_set_error(error_out,
                      "Could not determine the session home directory.");
    return 0;
  }
  return strappy_db_create_session_with_working_directory(db_path,
                                                          home_directory,
                                                          session_id_out,
                                                          error_out);
}

static int strappy_db_copy_session_working_directory(
  sqlite3 *db,
  long long session_id,
  char **working_directory_out,
  int *found_out,
  char **error_out)
{
  static const char *sql =
    "SELECT working_directory FROM session_working_directories "
    "WHERE session_id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  *working_directory_out = NULL;
  *found_out = 0;
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc == SQLITE_ROW) {
    *working_directory_out = strappy_db_column_string(stmt, 0);
    *found_out = 1;
    sqlite3_finalize(stmt);
    if (*working_directory_out == NULL) {
      strappy_set_error(error_out,
                        "Could not allocate session working directory.");
      return 0;
    }
    return 1;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read session working directory: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

int strappy_db_get_session_working_directory(
  const char *db_path,
  long long session_id,
  char **working_directory_out,
  char **error_out)
{
  sqlite3 *db;
  const char *home_directory;
  int found;

  if (working_directory_out != NULL) {
    *working_directory_out = NULL;
  }
  if ((working_directory_out == NULL) || (session_id <= 0LL)) {
    strappy_set_error(error_out,
                      "Session working-directory request is incomplete.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_copy_session_working_directory(db,
                                                 session_id,
                                                 working_directory_out,
                                                 &found,
                                                 error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (found) {
    strappy_db_release(db);
    return 1;
  }
  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  home_directory = getenv("HOME");
  if (!strappy_db_validate_working_directory(home_directory, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_save_session_working_directory(db,
                                                 session_id,
                                                 home_directory,
                                                 error_out)) {
    strappy_db_release(db);
    return 0;
  }
  *working_directory_out = strappy_string_duplicate(home_directory);
  if (*working_directory_out == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate session working directory.");
    strappy_db_release(db);
    return 0;
  }
  strappy_db_release(db);
  return 1;
}

int strappy_db_update_session_working_directory(
  const char *db_path,
  long long session_id,
  const char *working_directory,
  char **error_out)
{
  strappy_session_options options;

  strappy_session_options_init(&options);
  options.working_directory = (char *)working_directory;
  return strappy_db_update_session_options(
    db_path,
    session_id,
    &options,
    STRAPPY_SESSION_OPTION_WORKING_DIRECTORY,
    NULL,
    NULL,
    error_out);
}

int strappy_db_delete_session(const char *db_path,
                              long long session_id,
                              char **error_out)
{
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, "DELETE FROM sessions WHERE id = ?;", -1,
                          &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not delete session: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_update_session_name(const char *db_path,
                                   long long session_id,
                                   const char *name,
                                   char **error_out)
{
  static const char *sql =
    "UPDATE sessions "
    "SET name = ?, updated_at_ms = "
      "CAST(strftime('%s','now') AS INTEGER) * 1000 "
    "WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if ((name == NULL) || (name[0] == '\0')) {
    strappy_set_error(error_out, "Session name is empty.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session name update: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  if ((sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session name update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session name: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_update_session_limit_to_one_tool(const char *db_path,
                                                long long session_id,
                                                int limit_to_one_tool,
                                                char **error_out)
{
  strappy_session_options options;

  strappy_session_options_init(&options);
  options.limit_to_one_tool = limit_to_one_tool ? 1 : 0;
  return strappy_db_update_session_options(
    db_path,
    session_id,
    &options,
    STRAPPY_SESSION_OPTION_LIMIT_TO_ONE_TOOL,
    NULL,
    NULL,
    error_out);
}

int strappy_db_update_session_web_provider(
  const char *db_path,
  long long session_id,
  strappy_web_provider web_provider,
  char **error_out)
{
  strappy_session_options options;

  strappy_session_options_init(&options);
  options.web_provider = web_provider;
  return strappy_db_update_session_options(db_path,
                                           session_id,
                                           &options,
                                           STRAPPY_SESSION_OPTION_WEB_PROVIDER,
                                           NULL,
                                           NULL,
                                           error_out);
}

int strappy_db_update_session_web_search_enabled(
  const char *db_path,
  long long session_id,
  int web_search_enabled,
  char **error_out)
{
  strappy_session_options options;

  strappy_session_options_init(&options);
  options.web_search_enabled = web_search_enabled ? 1 : 0;
  return strappy_db_update_session_options(db_path,
                                           session_id,
                                           &options,
                                           STRAPPY_SESSION_OPTION_WEB_SEARCH,
                                           NULL,
                                           NULL,
                                           error_out);
}

static int strappy_db_copy_session_assistant_set(
  sqlite3 *db,
  long long session_id,
  char **assistant_set_id_out,
  char **error_out)
{
  static const char *sql =
    "SELECT COALESCE((SELECT a.assistant_set_id "
      "FROM session_assistant_sets a WHERE a.session_id = ?), '"
      STRAPPY_ASSISTANT_SET_DEFAULT "') "
    "WHERE EXISTS (SELECT 1 FROM sessions s WHERE s.id = ?);";
  sqlite3_stmt *stmt;
  int rc;

  if (assistant_set_id_out != NULL) {
    *assistant_set_id_out = NULL;
  }
  if ((db == NULL) || (assistant_set_id_out == NULL) || (session_id <= 0LL)) {
    strappy_set_error(error_out,
                      "Session assistant-set request is incomplete.");
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session assistant-set read: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  if ((sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session assistant-set read: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
    sqlite3_finalize(stmt);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read session assistant set: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  *assistant_set_id_out = strappy_db_column_string(stmt, 0);
  sqlite3_finalize(stmt);
  if (*assistant_set_id_out == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate session assistant set.");
    return 0;
  }
  return 1;
}

int strappy_db_get_session_assistant_set(const char *db_path,
                                         long long session_id,
                                         char **assistant_set_id_out,
                                         char **error_out)
{
  sqlite3 *db;
  int ok;

  if (assistant_set_id_out != NULL) {
    *assistant_set_id_out = NULL;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  ok = strappy_db_copy_session_assistant_set(db,
                                             session_id,
                                             assistant_set_id_out,
                                             error_out);
  strappy_db_release(db);
  return ok;
}

int strappy_db_get_session_bash_enabled(const char *db_path,
                                        long long session_id,
                                        int *bash_enabled_out,
                                        char **error_out)
{
  static const char *sql =
    "SELECT " STRAPPY_DB_SESSION_BASH_ENABLED_SQL " "
    "FROM sessions s WHERE s.id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (bash_enabled_out == NULL) {
    strappy_set_error(error_out, "Session Bash setting has no output.");
    return 0;
  }
  *bash_enabled_out = 0;
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read session Bash setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  *bash_enabled_out = sqlite3_column_int(stmt, 0) ? 1 : 0;
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_update_session_bash_enabled(const char *db_path,
                                           long long session_id,
                                           int bash_enabled,
                                           char **error_out)
{
  strappy_session_options options;

  strappy_session_options_init(&options);
  options.bash_enabled = bash_enabled ? 1 : 0;
  return strappy_db_update_session_options(db_path,
                                           session_id,
                                           &options,
                                           STRAPPY_SESSION_OPTION_BASH,
                                           NULL,
                                           NULL,
                                           error_out);
}

int strappy_db_update_session_assistant_set(const char *db_path,
                                            long long session_id,
                                            const char *assistant_set_id,
                                            char **error_out)
{
  strappy_session_options options;

  strappy_session_options_init(&options);
  options.assistant_set_id = (char *)assistant_set_id;
  return strappy_db_update_session_options(
    db_path,
    session_id,
    &options,
    STRAPPY_SESSION_OPTION_ASSISTANT_SET,
    NULL,
    NULL,
    error_out);
}

int strappy_db_list_sessions(const char *db_path,
                             strappy_session_record_list *list,
                             char **error_out)
{
  static const char *sql =
    "SELECT s.id, s.name, '', '', "
    STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL ", "
    "COALESCE(m.name, " STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL "), 0, "
    "strftime('%Y-%m-%dT%H:%M:%fZ', s.created_at_ms / 1000.0, 'unixepoch'), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', "
      STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL " / 1000.0, 'unixepoch'), "
    STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL ", "
    STRAPPY_DB_SESSION_WEB_PROVIDER_SQL ", "
    STRAPPY_DB_SESSION_BASH_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_LIMIT_TO_ONE_TOOL_SQL ", "
    STRAPPY_DB_SESSION_ROUND_LIMIT_SQL ", "
    STRAPPY_DB_SESSION_ASSISTANT_SET_SQL ", "
    STRAPPY_DB_SESSION_WEB_SEARCH_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_ANSWER_QUALITY_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_EFFECTIVE_ACCOUNT_SQL " "
    "FROM sessions s LEFT JOIN models m ON m.id = "
      STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL " "
    "ORDER BY " STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL " DESC, s.id DESC;";
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
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session list: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_session_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_session_record))) {
      strappy_set_error(error_out, "Session list is too large.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_session_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_session_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate session list.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_session_record_init(&list->records[list->count]);
    if (!strappy_db_assign_record_from_statement(&list->records[list->count],
                                                 stmt,
                                                 error_out)) {
      sqlite3_finalize(stmt);
      strappy_db_release(db);
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
    strappy_db_release(db);
    strappy_session_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_load_session(const char *db_path,
                            long long session_id,
                            strappy_session_record *record,
                            char **error_out)
{
  static const char *sql =
    "SELECT s.id, s.name, "
    "COALESCE((SELECT p.text FROM conversation_items i "
      "JOIN message_items m ON m.item_id = i.id "
      "JOIN item_text_parts p ON p.item_id = i.id "
      "WHERE i.session_id = s.id AND m.role = 'user' "
      "ORDER BY i.sequence DESC, p.ordinal LIMIT 1), ''), "
    "COALESCE((SELECT p.text FROM conversation_items i "
      "JOIN message_items m ON m.item_id = i.id "
      "JOIN item_text_parts p ON p.item_id = i.id "
      "WHERE i.session_id = s.id AND m.role = 'assistant' "
      "AND i.include_in_context = 1 AND i.source_attempt_id IS NOT NULL "
      "ORDER BY i.sequence DESC, p.ordinal LIMIT 1), ''), "
    STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL ", "
    "COALESCE(m.name, " STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL "), "
    "COALESCE((SELECT a.http_status FROM http_attempts a "
      "JOIN model_requests r ON r.id = a.request_id "
      "JOIN turns t ON t.id = r.turn_id "
      "WHERE t.session_id = s.id ORDER BY a.id DESC LIMIT 1), 0), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', s.created_at_ms / 1000.0, 'unixepoch'), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', "
      STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL " / 1000.0, 'unixepoch'), "
    STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL ", "
    STRAPPY_DB_SESSION_WEB_PROVIDER_SQL ", "
    STRAPPY_DB_SESSION_BASH_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_LIMIT_TO_ONE_TOOL_SQL ", "
    STRAPPY_DB_SESSION_ROUND_LIMIT_SQL ", "
    STRAPPY_DB_SESSION_ASSISTANT_SET_SQL ", "
    STRAPPY_DB_SESSION_WEB_SEARCH_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_ANSWER_QUALITY_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_EFFECTIVE_ACCOUNT_SQL " "
    "FROM sessions s LEFT JOIN models m ON m.id = "
      STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL " WHERE s.id = ?;";
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
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session load: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session load: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read session: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  ok = strappy_db_assign_record_from_statement(record, stmt, error_out);
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  if (!ok) {
    strappy_session_record_destroy(record);
    return 0;
  }

  return 1;
}

int strappy_db_load_session_list_record(const char *db_path,
                                        long long session_id,
                                        strappy_session_record *record,
                                        char **error_out)
{
  static const char *sql =
    "SELECT s.id, s.name, '', '', "
    STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL ", "
    "COALESCE(m.name, " STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL "), 0, "
    "strftime('%Y-%m-%dT%H:%M:%fZ', s.created_at_ms / 1000.0, 'unixepoch'), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', "
      STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL " / 1000.0, 'unixepoch'), "
    STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL ", "
    STRAPPY_DB_SESSION_WEB_PROVIDER_SQL ", "
    STRAPPY_DB_SESSION_BASH_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_LIMIT_TO_ONE_TOOL_SQL ", "
    STRAPPY_DB_SESSION_ROUND_LIMIT_SQL ", "
    STRAPPY_DB_SESSION_ASSISTANT_SET_SQL ", "
    STRAPPY_DB_SESSION_WEB_SEARCH_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_ANSWER_QUALITY_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_EFFECTIVE_ACCOUNT_SQL " "
    "FROM sessions s LEFT JOIN models m ON m.id = "
      STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL " WHERE s.id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if (record == NULL) {
    strappy_set_error(error_out,
                      "strappy_db_load_session_list_record received no output.");
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
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session list row load: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }
  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session list row load: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read session list row: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  ok = strappy_db_assign_record_from_statement(record, stmt, error_out);
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  if (!ok) {
    strappy_session_record_destroy(record);
    return 0;
  }
  return 1;
}

static int strappy_db_copy_default_session_options(
  sqlite3 *db,
  const char *fallback_working_directory,
  strappy_session_options *options,
  char **error_out)
{
  static const char *sql =
    "SELECT CASE WHEN (SELECT default_provider_account_id FROM "
    "app_preferences WHERE id=1) IS NULL THEN NULL ELSE "
    STRAPPY_DB_DEFAULT_MODEL_SQL " END, "
    "(SELECT default_provider_account_id FROM app_preferences WHERE id=1), "
    "assistant_set_id, web_provider, web_search_enabled, bash_enabled, "
    "limit_to_one_tool, round_limit, working_directory, "
    "answer_quality_enabled "
    "FROM default_session_options WHERE id = 1;";
  strappy_session_options loaded;
  const unsigned char *provider_text;
  const unsigned char *working_directory_text;
  sqlite3_stmt *stmt;
  int rc;

  if ((db == NULL) || (options == NULL) ||
      (fallback_working_directory == NULL) ||
      (fallback_working_directory[0] == '\0')) {
    strappy_set_error(error_out, "Default session options request is incomplete.");
    return 0;
  }

  strappy_session_options_init(&loaded);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Default session options were not found.");
    sqlite3_finalize(stmt);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read default session options: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  loaded.model_id = strappy_db_column_string(stmt, 0);
  loaded.provider_account_id = strappy_db_column_string(stmt, 1);
  loaded.assistant_set_id = strappy_db_column_string(stmt, 2);
  provider_text = sqlite3_column_text(stmt, 3);
  loaded.web_search_enabled = sqlite3_column_int(stmt, 4) ? 1 : 0;
  loaded.bash_enabled = sqlite3_column_int(stmt, 5) ? 1 : 0;
  loaded.limit_to_one_tool = sqlite3_column_int(stmt, 6) ? 1 : 0;
  loaded.round_limit = (long)sqlite3_column_int64(stmt, 7);
  working_directory_text = sqlite3_column_text(stmt, 8);
  loaded.answer_quality_enabled = sqlite3_column_int(stmt, 9) ? 1 : 0;
  if (((loaded.model_id == NULL) != (loaded.provider_account_id == NULL)) ||
      (loaded.assistant_set_id == NULL) ||
      (provider_text == NULL) ||
      (loaded.round_limit < 1L) ||
      (loaded.round_limit > STRAPPY_SESSION_MAX_LIMIT) ||
      !strappy_web_provider_parse((const char *)provider_text,
                                  &loaded.web_provider)) {
    strappy_set_error(error_out, "Stored default session options are invalid.");
    sqlite3_finalize(stmt);
    strappy_session_options_destroy(&loaded);
    return 0;
  }
  if (loaded.model_id == NULL) {
    loaded.model_id = strappy_string_duplicate("");
    loaded.provider_account_id = strappy_string_duplicate("");
    if ((loaded.model_id == NULL) || (loaded.provider_account_id == NULL)) {
      strappy_set_error(error_out,
                        "Could not allocate empty default account options.");
      sqlite3_finalize(stmt);
      strappy_session_options_destroy(&loaded);
      return 0;
    }
  }
  loaded.working_directory = strappy_string_duplicate(
    ((working_directory_text != NULL) && (working_directory_text[0] != '\0'))
      ? (const char *)working_directory_text
      : fallback_working_directory);
  sqlite3_finalize(stmt);
  if (loaded.working_directory == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate default session working directory.");
    strappy_session_options_destroy(&loaded);
    return 0;
  }

  strappy_session_options_destroy(options);
  *options = loaded;
  return 1;
}

static int strappy_db_copy_session_options(sqlite3 *db,
                                           long long session_id,
                                           strappy_session_options *options,
                                           char **error_out)
{
  static const char *sql =
    "SELECT " STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL ", "
    STRAPPY_DB_SESSION_EFFECTIVE_ACCOUNT_SQL ", "
    STRAPPY_DB_SESSION_ASSISTANT_SET_SQL ", "
    STRAPPY_DB_SESSION_WEB_PROVIDER_SQL ", "
    STRAPPY_DB_SESSION_WEB_SEARCH_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_BASH_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_LIMIT_TO_ONE_TOOL_SQL ", "
    STRAPPY_DB_SESSION_ROUND_LIMIT_SQL ", "
    STRAPPY_DB_SESSION_ANSWER_QUALITY_ENABLED_SQL " "
    "FROM sessions s WHERE s.id = ?;";
  strappy_session_options loaded;
  const unsigned char *provider_text;
  const char *home_directory;
  sqlite3_stmt *stmt;
  int found_working_directory;
  int rc;

  if ((db == NULL) || (options == NULL) || (session_id <= 0LL)) {
    strappy_set_error(error_out, "Session options request is incomplete.");
    return 0;
  }

  strappy_session_options_init(&loaded);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
    sqlite3_finalize(stmt);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read session options: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  loaded.model_id = strappy_db_column_string(stmt, 0);
  loaded.provider_account_id = strappy_db_column_string(stmt, 1);
  loaded.assistant_set_id = strappy_db_column_string(stmt, 2);
  provider_text = sqlite3_column_text(stmt, 3);
  loaded.web_search_enabled = sqlite3_column_int(stmt, 4) ? 1 : 0;
  loaded.bash_enabled = sqlite3_column_int(stmt, 5) ? 1 : 0;
  loaded.limit_to_one_tool = sqlite3_column_int(stmt, 6) ? 1 : 0;
  loaded.round_limit = (long)sqlite3_column_int64(stmt, 7);
  loaded.answer_quality_enabled = sqlite3_column_int(stmt, 8) ? 1 : 0;
  if ((loaded.model_id == NULL) || (loaded.provider_account_id == NULL) ||
      (loaded.assistant_set_id == NULL) ||
      (provider_text == NULL) ||
      (loaded.round_limit < 1L) ||
      (loaded.round_limit > STRAPPY_SESSION_MAX_LIMIT) ||
      !strappy_web_provider_parse((const char *)provider_text,
                                  &loaded.web_provider)) {
    strappy_set_error(error_out, "Stored session options are invalid.");
    sqlite3_finalize(stmt);
    strappy_session_options_destroy(&loaded);
    return 0;
  }
  sqlite3_finalize(stmt);

  found_working_directory = 0;
  if (!strappy_db_copy_session_working_directory(
        db,
        session_id,
        &loaded.working_directory,
        &found_working_directory,
        error_out)) {
    strappy_session_options_destroy(&loaded);
    return 0;
  }
  if (!found_working_directory) {
    home_directory = getenv("HOME");
    if (!strappy_db_validate_working_directory(home_directory, error_out) ||
        !strappy_db_save_session_working_directory(db,
                                                   session_id,
                                                   home_directory,
                                                   error_out)) {
      strappy_session_options_destroy(&loaded);
      return 0;
    }
    loaded.working_directory = strappy_string_duplicate(home_directory);
    if (loaded.working_directory == NULL) {
      strappy_set_error(error_out,
                        "Could not allocate session working directory.");
      strappy_session_options_destroy(&loaded);
      return 0;
    }
  }

  strappy_session_options_destroy(options);
  *options = loaded;
  return 1;
}

static int strappy_db_copy_options_value(char **destination,
                                         const char *source,
                                         const char *error_message,
                                         char **error_out)
{
  char *copy;

  copy = strappy_string_duplicate(source);
  if (copy == NULL) {
    strappy_set_error(error_out, error_message);
    return 0;
  }
  free(*destination);
  *destination = copy;
  return 1;
}

static int strappy_db_copy_options(const strappy_session_options *source,
                                   strappy_session_options *destination,
                                   char **error_out)
{
  strappy_session_options copy;

  if ((source == NULL) || (destination == NULL)) {
    strappy_set_error(error_out, "Session options copy is incomplete.");
    return 0;
  }
  strappy_session_options_init(&copy);
  if (!strappy_db_copy_options_value(&copy.model_id,
                                     source->model_id,
                                     "Could not allocate session model option.",
                                     error_out) ||
      !strappy_db_copy_options_value(
        &copy.provider_account_id,
        source->provider_account_id,
        "Could not allocate session provider-account option.",
        error_out) ||
      !strappy_db_copy_options_value(
        &copy.assistant_set_id,
        source->assistant_set_id,
        "Could not allocate session assistant-set option.",
        error_out) ||
      !strappy_db_copy_options_value(
        &copy.working_directory,
        source->working_directory,
        "Could not allocate session working-directory option.",
        error_out)) {
    strappy_session_options_destroy(&copy);
    return 0;
  }
  copy.web_provider = source->web_provider;
  copy.web_search_enabled = source->web_search_enabled ? 1 : 0;
  copy.bash_enabled = source->bash_enabled ? 1 : 0;
  copy.limit_to_one_tool = source->limit_to_one_tool ? 1 : 0;
  copy.answer_quality_enabled = source->answer_quality_enabled ? 1 : 0;
  copy.round_limit = source->round_limit;
  strappy_session_options_destroy(destination);
  *destination = copy;
  return 1;
}

static int strappy_db_save_session_options_settings(
  sqlite3 *db,
  long long session_id,
  const strappy_session_options *options,
  long long now_ms,
  char **error_out)
{
  static const char *sql =
    "INSERT OR REPLACE INTO session_settings "
    "(session_id, web_provider, web_search_enabled, bash_enabled, "
     "limit_to_one_tool, round_limit, answer_quality_enabled, updated_at_ms) "
     "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
  const char *provider_name;
  sqlite3_stmt *stmt;
  int rc;

  provider_name = strappy_web_provider_name(options->web_provider);
  if (provider_name == NULL) {
    strappy_set_error(error_out, "Session web provider is invalid.");
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, provider_name, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 3, options->web_search_enabled ? 1 : 0);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 4, options->bash_enabled ? 1 : 0);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 5, options->limit_to_one_tool ? 1 : 0);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt,
                            6,
                            (sqlite3_int64)options->round_limit);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 7, options->answer_quality_enabled ? 1 : 0);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 8, (sqlite3_int64)now_ms);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session options: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int strappy_db_save_default_session_options(
  sqlite3 *db,
  const strappy_session_options *options,
  long long now_ms,
  char **error_out)
{
  static const char *sql =
    "UPDATE default_session_options SET assistant_set_id = ?, "
    "web_provider = ?, web_search_enabled = ?, bash_enabled = ?, "
    "limit_to_one_tool = ?, round_limit = ?, working_directory = ?, "
    "answer_quality_enabled = ?, updated_at_ms = ? WHERE id = 1;";
  const char *provider_name;
  sqlite3_stmt *stmt;
  int rc;

  provider_name = strappy_web_provider_name(options->web_provider);
  if ((options->assistant_set_id == NULL) ||
      (options->assistant_set_id[0] == '\0') ||
      (options->working_directory == NULL) ||
      (options->working_directory[0] == '\0') ||
      (provider_name == NULL)) {
    strappy_set_error(error_out, "Default session options are invalid.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt,
                           1,
                           options->assistant_set_id,
                           -1,
                           SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, provider_name, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 3, options->web_search_enabled ? 1 : 0);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 4, options->bash_enabled ? 1 : 0);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 5, options->limit_to_one_tool ? 1 : 0);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt,
                            6,
                            (sqlite3_int64)options->round_limit);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt,
                           7,
                           options->working_directory,
                           -1,
                           SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 8, options->answer_quality_enabled ? 1 : 0);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 9, (sqlite3_int64)now_ms);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  if ((rc != SQLITE_DONE) || (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not save default session options: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int strappy_db_save_session_options_model(
  sqlite3 *db,
  long long session_id,
  const char *model_id,
  const char *provider_account_id,
  long long now_ms,
  char **error_out)
{
  static const char *sql =
    "UPDATE sessions SET model_id = ?, provider_account_id = ?, "
    "updated_at_ms = ? WHERE id = ? AND (provider_account_id = ? OR "
    "NOT EXISTS (SELECT 1 FROM model_requests r JOIN turns t "
    "ON t.id=r.turn_id WHERE t.session_id=sessions.id));";
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, provider_account_id, -1,
                           SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ms);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 4, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 5, provider_account_id, -1,
                           SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  if ((rc != SQLITE_DONE) || (sqlite3_changes(db) != 1)) {
    if (rc == SQLITE_DONE) {
      strappy_set_error(
        error_out,
        "This conversation is already bound to a different model account. "
        "Create a new session to change accounts.");
    } else {
      strappy_set_formatted_error(
        error_out, "Could not save session account/model options: %s",
        sqlite3_errmsg(db));
    }
    return 0;
  }
  return 1;
}

static int strappy_db_copy_model_account(sqlite3 *db,
                                         const char *model_id,
                                         char **provider_account_id_out,
                                         char **error_out)
{
  sqlite3_stmt *stmt;
  int rc;

  *provider_account_id_out = NULL;
  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "SELECT a.id FROM models m JOIN provider_accounts a "
    "ON a.provider_id=m.provider_id WHERE m.id=? AND m.catalog_active=1 "
    "AND a.lifecycle_state='active' ORDER BY CASE WHEN a.id=(SELECT "
    "default_provider_account_id FROM app_preferences WHERE id=1) THEN 0 "
    "ELSE 1 END,a.last_used_at_ms DESC,a.created_at_ms,a.id LIMIT 1;",
    -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc == SQLITE_ROW) {
    *provider_account_id_out = strappy_db_column_string(stmt, 0);
  }
  sqlite3_finalize(stmt);
  if (*provider_account_id_out == NULL) {
    strappy_set_error(error_out,
                      "The selected model does not belong to an active account.");
    return 0;
  }
  return 1;
}

static int strappy_db_model_matches_account(sqlite3 *db,
                                            const char *model_id,
                                            const char *account_id,
                                            int *matches_out,
                                            char **error_out)
{
  sqlite3_stmt *stmt;
  int rc;

  *matches_out = 0;
  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "SELECT 1 FROM models m JOIN provider_accounts a "
    "ON a.provider_id=m.provider_id WHERE m.id=? AND a.id=? "
    "AND m.catalog_active=1 AND a.lifecycle_state='active';",
    -1, &stmt, NULL);
  if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, account_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) *matches_out = 1;
  sqlite3_finalize(stmt);
  if ((rc != SQLITE_ROW) && (rc != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not validate model provider: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int strappy_db_copy_account_default_model(sqlite3 *db,
                                                 const char *account_id,
                                                 char **model_id_out,
                                                 char **error_out)
{
  sqlite3_stmt *stmt;
  int rc;

  *model_id_out = NULL;
  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "SELECT m.id FROM models m JOIN provider_accounts a "
    "ON a.provider_id=m.provider_id LEFT JOIN model_preferences p "
    "ON p.provider_id=a.provider_id AND p.wire_model_id=m.wire_model_id "
    "WHERE a.id=? "
    "AND a.lifecycle_state='active' AND m.catalog_active=1 "
    "AND (p.allowed=1 OR m.id=(SELECT default_model_id FROM app_preferences "
    "WHERE id=1)) ORDER BY CASE WHEN m.id=(SELECT default_model_id "
    "FROM app_preferences WHERE id=1) THEN 0 ELSE 1 END, m.name, m.id LIMIT 1;",
    -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 1, account_id, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc == SQLITE_ROW) {
    *model_id_out = strappy_db_column_string(stmt, 0);
  }
  sqlite3_finalize(stmt);
  if (*model_id_out == NULL) {
    strappy_set_error(error_out,
                      "The selected account has no active allowed model.");
    return 0;
  }
  return 1;
}

static int strappy_db_save_default_account_model(
  sqlite3 *db,
  const char *account_id,
  const char *model_id,
  long long now_ms,
  char **error_out)
{
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "UPDATE app_preferences SET default_model_id=?, "
    "default_provider_account_id=?, updated_at_ms=? WHERE id=1;",
    -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, account_id, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ms);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if ((rc != SQLITE_DONE) || (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(
      error_out,
      "Could not save the default account and model: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_touch_session_options(sqlite3 *db,
                                            long long session_id,
                                            long long now_ms,
                                            char **error_out)
{
  static const char *sql =
    "UPDATE sessions SET updated_at_ms = ? WHERE id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ms);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  if ((rc != SQLITE_DONE) || (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not touch session options: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int strappy_db_save_session_options_assistant_set(
  sqlite3 *db,
  long long session_id,
  const char *assistant_set_id,
  long long now_ms,
  char **error_out)
{
  static const char *sql =
    "INSERT OR REPLACE INTO session_assistant_sets "
    "(session_id, assistant_set_id, updated_at_ms) VALUES (?, ?, ?);";
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, assistant_set_id, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ms);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session assistant-set option: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

int strappy_db_load_session_options(
  const char *db_path,
  long long session_id,
  strappy_session_options *options,
  char **error_out)
{
  sqlite3 *db;
  int ok;

  if (options == NULL) {
    strappy_set_error(error_out, "Session options output is missing.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  ok = strappy_db_copy_session_options(db, session_id, options, error_out);
  strappy_db_release(db);
  return ok;
}

int strappy_db_load_default_session_options(
  const char *db_path,
  const char *fallback_working_directory,
  strappy_session_options *options,
  char **error_out)
{
  sqlite3 *db;
  int ok;

  if (options == NULL) {
    strappy_set_error(error_out, "Default session options output is missing.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  ok = strappy_db_copy_default_session_options(db,
                                               fallback_working_directory,
                                               options,
                                               error_out);
  strappy_db_release(db);
  return ok;
}

int strappy_db_update_session_options(
  const char *db_path,
  long long session_id,
  const strappy_session_options *options,
  strappy_session_option_mask changed_fields,
  strappy_session_options *saved_options_out,
  strappy_session_option_mask *actual_changed_fields_out,
  char **error_out)
{
  const strappy_session_option_mask settings_fields =
    STRAPPY_SESSION_OPTION_WEB_PROVIDER |
    STRAPPY_SESSION_OPTION_WEB_SEARCH |
    STRAPPY_SESSION_OPTION_BASH |
    STRAPPY_SESSION_OPTION_LIMIT_TO_ONE_TOOL |
    STRAPPY_SESSION_OPTION_ANSWER_QUALITY |
    STRAPPY_SESSION_OPTION_ROUND_LIMIT;
  strappy_session_options current;
  strappy_session_options merged;
  strappy_session_option_mask actual_changed_fields;
  sqlite3 *db;
  long long now_ms;
  int allowed;
  int ok;
  char *selected_account_id;
  char *selected_model_id;

  if (actual_changed_fields_out != NULL) {
    *actual_changed_fields_out = 0U;
  }
  if ((session_id <= 0LL) || (options == NULL) ||
      ((changed_fields &
        ~(strappy_session_option_mask)STRAPPY_SESSION_OPTION_ALL) != 0U)) {
    strappy_set_error(error_out, "Session options update is invalid.");
    return 0;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_MODEL) != 0U) &&
      ((options->model_id == NULL) || (options->model_id[0] == '\0'))) {
    strappy_set_error(error_out, "Session model is not selected.");
    return 0;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT) != 0U) &&
      ((options->provider_account_id == NULL) ||
       (options->provider_account_id[0] == '\0'))) {
    strappy_set_error(error_out, "Session provider account is not selected.");
    return 0;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_ASSISTANT_SET) != 0U) &&
      ((options->assistant_set_id == NULL) ||
       (options->assistant_set_id[0] == '\0'))) {
    strappy_set_error(error_out, "Session assistant set is not selected.");
    return 0;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_WORKING_DIRECTORY) != 0U) &&
      !strappy_db_validate_working_directory(options->working_directory,
                                             error_out)) {
    return 0;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_WEB_PROVIDER) != 0U) &&
      (strappy_web_provider_name(options->web_provider) == NULL)) {
    strappy_set_error(error_out, "Session web provider is invalid.");
    return 0;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_ROUND_LIMIT) != 0U) &&
      ((options->round_limit < 1L) ||
       (options->round_limit > STRAPPY_SESSION_MAX_LIMIT))) {
    strappy_set_error(error_out, "Session round limit is invalid.");
    return 0;
  }

  strappy_session_options_init(&current);
  strappy_session_options_init(&merged);
  selected_account_id = NULL;
  selected_model_id = NULL;
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin session-options update",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_copy_session_options(db,
                                       session_id,
                                       &current,
                                       error_out) ||
      !strappy_db_copy_options(&current, &merged, error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back session-options update",
                    NULL);
    strappy_session_options_destroy(&current);
    strappy_session_options_destroy(&merged);
    strappy_db_release(db);
    return 0;
  }

  if (((changed_fields & STRAPPY_SESSION_OPTION_MODEL) != 0U) &&
      !strappy_db_copy_options_value(
        &merged.model_id,
        options->model_id,
        "Could not allocate session model option.",
        error_out)) {
    goto rollback;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT) != 0U) {
    if (!strappy_db_copy_options_value(
          &merged.provider_account_id, options->provider_account_id,
          "Could not allocate session provider-account option.", error_out)) {
      goto rollback;
    }
    if (((changed_fields & STRAPPY_SESSION_OPTION_MODEL) == 0U) &&
        (strcmp(current.provider_account_id,
                merged.provider_account_id) != 0)) {
      if (!strappy_db_copy_account_default_model(db,
                                                 merged.provider_account_id,
                                                 &selected_model_id,
                                                 error_out) ||
          !strappy_db_copy_options_value(
            &merged.model_id, selected_model_id,
            "Could not allocate the selected account's default model.",
            error_out)) {
        goto rollback;
      }
    }
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_MODEL) != 0U) {
    if (!strappy_db_copy_model_account(db, merged.model_id,
                                       &selected_account_id, error_out)) {
      goto rollback;
    }
    if ((changed_fields & STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT) == 0U) {
      if (!strappy_db_copy_options_value(
            &merged.provider_account_id, selected_account_id,
            "Could not allocate the model's provider account.", error_out)) {
        goto rollback;
      }
    } else {
      int matches;
      if (!strappy_db_model_matches_account(db, merged.model_id,
                                            merged.provider_account_id,
                                            &matches, error_out) || !matches) {
        if ((error_out != NULL) && (*error_out == NULL)) {
          strappy_set_error(error_out,
                            "The selected model and account use different providers.");
        }
        goto rollback;
      }
    }
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_ASSISTANT_SET) != 0U) &&
      !strappy_db_copy_options_value(
        &merged.assistant_set_id,
        options->assistant_set_id,
        "Could not allocate session assistant-set option.",
        error_out)) {
    goto rollback;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_WORKING_DIRECTORY) != 0U) &&
      !strappy_db_copy_options_value(
        &merged.working_directory,
        options->working_directory,
        "Could not allocate session working-directory option.",
        error_out)) {
    goto rollback;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_WEB_PROVIDER) != 0U) {
    merged.web_provider = options->web_provider;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_WEB_SEARCH) != 0U) {
    merged.web_search_enabled = options->web_search_enabled ? 1 : 0;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_BASH) != 0U) {
    merged.bash_enabled = options->bash_enabled ? 1 : 0;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_LIMIT_TO_ONE_TOOL) != 0U) {
    merged.limit_to_one_tool = options->limit_to_one_tool ? 1 : 0;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_ANSWER_QUALITY) != 0U) {
    merged.answer_quality_enabled = options->answer_quality_enabled ? 1 : 0;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_ROUND_LIMIT) != 0U) {
    merged.round_limit = options->round_limit;
  }
  actual_changed_fields = 0U;
  if (((changed_fields & STRAPPY_SESSION_OPTION_MODEL) != 0U) &&
      (strcmp(current.model_id, merged.model_id) != 0)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_MODEL;
  }
  if (strcmp(current.provider_account_id, merged.provider_account_id) != 0) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT;
  }
  if (((actual_changed_fields & STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT) != 0U) &&
      (strcmp(current.model_id, merged.model_id) != 0)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_MODEL;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_ASSISTANT_SET) != 0U) &&
      (strcmp(current.assistant_set_id, merged.assistant_set_id) != 0)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_ASSISTANT_SET;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_WEB_PROVIDER) != 0U) &&
      (current.web_provider != merged.web_provider)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_WEB_PROVIDER;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_WEB_SEARCH) != 0U) &&
      (current.web_search_enabled != merged.web_search_enabled)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_WEB_SEARCH;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_BASH) != 0U) &&
      (current.bash_enabled != merged.bash_enabled)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_BASH;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_LIMIT_TO_ONE_TOOL) != 0U) &&
      (current.limit_to_one_tool != merged.limit_to_one_tool)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_LIMIT_TO_ONE_TOOL;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_ANSWER_QUALITY) != 0U) &&
      (current.answer_quality_enabled != merged.answer_quality_enabled)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_ANSWER_QUALITY;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_ROUND_LIMIT) != 0U) &&
      (current.round_limit != merged.round_limit)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_ROUND_LIMIT;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_WORKING_DIRECTORY) != 0U) &&
      (strcmp(current.working_directory, merged.working_directory) != 0)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_WORKING_DIRECTORY;
  }
  if ((actual_changed_fields & (STRAPPY_SESSION_OPTION_MODEL |
                                STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT)) != 0U) {
    allowed = 0;
    if (!strappy_db_model_exists(db, merged.model_id, error_out) ||
        ((selected_account_id == NULL) &&
         !strappy_db_copy_model_account(db, merged.model_id,
                                        &selected_account_id, error_out))) {
      goto rollback;
    }
    {
      int matches;
      if (!strappy_db_model_matches_account(db, merged.model_id,
                                            merged.provider_account_id,
                                            &matches, error_out) || !matches) {
        if ((error_out != NULL) && (*error_out == NULL)) {
          strappy_set_error(error_out,
                            "The selected model and account use different providers.");
        }
        goto rollback;
      }
    }
    if (
        !strappy_db_model_is_effectively_allowed(db,
                                                 merged.model_id,
                                                 &allowed,
                                                 error_out)) {
      goto rollback;
    }
    if (!allowed) {
      strappy_set_error(error_out, "Model is not allowed.");
      goto rollback;
    }
  }

  now_ms = strappy_db_now_ms();
  ok = 1;
  if ((actual_changed_fields & settings_fields) != 0U) {
    ok = strappy_db_save_session_options_settings(db,
                                                  session_id,
                                                  &merged,
                                                  now_ms,
                                                  error_out);
  }
  if (ok &&
      ((actual_changed_fields & (STRAPPY_SESSION_OPTION_MODEL |
                                 STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT)) != 0U)) {
    ok = strappy_db_save_session_options_model(db,
                                               session_id,
                                               merged.model_id,
                                               merged.provider_account_id,
                                               now_ms,
                                               error_out);
  } else if (ok && (actual_changed_fields != 0U)) {
    ok = strappy_db_touch_session_options(db,
                                          session_id,
                                          now_ms,
                                          error_out);
  }
  if (ok &&
      ((actual_changed_fields & STRAPPY_SESSION_OPTION_ASSISTANT_SET) != 0U)) {
    ok = strappy_db_save_session_options_assistant_set(
      db,
      session_id,
      merged.assistant_set_id,
      now_ms,
      error_out);
  }
  if (ok &&
      ((actual_changed_fields & STRAPPY_SESSION_OPTION_WORKING_DIRECTORY) !=
       0U)) {
    ok = strappy_db_save_session_working_directory(db,
                                                   session_id,
                                                   merged.working_directory,
                                                   error_out);
  }
  if (!ok) {
    goto rollback;
  }

  strappy_session_options_destroy(&current);
  if (!strappy_db_copy_session_options(db,
                                       session_id,
                                       &current,
                                       error_out) ||
      !strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit session-options update",
                       error_out)) {
    goto rollback;
  }
  strappy_db_release(db);
  strappy_session_options_destroy(&merged);
  free(selected_account_id);
  free(selected_model_id);
  if (saved_options_out != NULL) {
    strappy_session_options_destroy(saved_options_out);
    *saved_options_out = current;
    strappy_session_options_init(&current);
  }
  strappy_session_options_destroy(&current);
  if (actual_changed_fields_out != NULL) {
    *actual_changed_fields_out = actual_changed_fields;
  }
  return 1;

rollback:
  strappy_db_exec(db,
                  "ROLLBACK;",
                  "Could not roll back session-options update",
                  NULL);
  strappy_session_options_destroy(&current);
  strappy_session_options_destroy(&merged);
  free(selected_account_id);
  free(selected_model_id);
  strappy_db_release(db);
  return 0;
}

int strappy_db_update_default_session_options(
  const char *db_path,
  const char *fallback_working_directory,
  const strappy_session_options *options,
  strappy_session_option_mask changed_fields,
  strappy_session_options *saved_options_out,
  strappy_session_option_mask *actual_changed_fields_out,
  char **error_out)
{
  const strappy_session_option_mask stored_fields =
    STRAPPY_SESSION_OPTION_ASSISTANT_SET |
    STRAPPY_SESSION_OPTION_WEB_PROVIDER |
    STRAPPY_SESSION_OPTION_WEB_SEARCH |
    STRAPPY_SESSION_OPTION_BASH |
    STRAPPY_SESSION_OPTION_LIMIT_TO_ONE_TOOL |
    STRAPPY_SESSION_OPTION_ANSWER_QUALITY |
    STRAPPY_SESSION_OPTION_WORKING_DIRECTORY |
    STRAPPY_SESSION_OPTION_ROUND_LIMIT;
  strappy_session_options current;
  strappy_session_options merged;
  strappy_session_option_mask actual_changed_fields;
  sqlite3 *db;
  long long now_ms;
  int ok;
  char *selected_account_id;
  char *selected_model_id;

  if (actual_changed_fields_out != NULL) {
    *actual_changed_fields_out = 0U;
  }
  if ((options == NULL) || (fallback_working_directory == NULL) ||
      (fallback_working_directory[0] == '\0') ||
      ((changed_fields &
        ~(strappy_session_option_mask)STRAPPY_SESSION_OPTION_ALL) != 0U)) {
    strappy_set_error(error_out, "Default session options update is invalid.");
    return 0;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_MODEL) != 0U) &&
      ((options->model_id == NULL) || (options->model_id[0] == '\0'))) {
    strappy_set_error(error_out, "Default session model is not selected.");
    return 0;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT) != 0U) &&
      ((options->provider_account_id == NULL) ||
       (options->provider_account_id[0] == '\0'))) {
    strappy_set_error(error_out,
                      "Default session provider account is not selected.");
    return 0;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_ASSISTANT_SET) != 0U) &&
      ((options->assistant_set_id == NULL) ||
       (options->assistant_set_id[0] == '\0'))) {
    strappy_set_error(error_out, "Default assistant set is not selected.");
    return 0;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_WORKING_DIRECTORY) != 0U) &&
      !strappy_db_validate_working_directory(options->working_directory,
                                             error_out)) {
    return 0;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_WEB_PROVIDER) != 0U) &&
      (strappy_web_provider_name(options->web_provider) == NULL)) {
    strappy_set_error(error_out, "Default web provider is invalid.");
    return 0;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_ROUND_LIMIT) != 0U) &&
      ((options->round_limit < 1L) ||
       (options->round_limit > STRAPPY_SESSION_MAX_LIMIT))) {
    strappy_set_error(error_out, "Default round limit is invalid.");
    return 0;
  }

  strappy_session_options_init(&current);
  strappy_session_options_init(&merged);
  selected_account_id = NULL;
  selected_model_id = NULL;
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin default-session-options update",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_copy_default_session_options(db,
                                               fallback_working_directory,
                                               &current,
                                               error_out) ||
      !strappy_db_copy_options(&current, &merged, error_out)) {
    goto default_rollback;
  }

  if (((changed_fields & STRAPPY_SESSION_OPTION_MODEL) != 0U) &&
      !strappy_db_copy_options_value(
        &merged.model_id,
        options->model_id,
        "Could not allocate default session model option.",
        error_out)) {
    goto default_rollback;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT) != 0U) {
    if (!strappy_db_copy_options_value(
          &merged.provider_account_id, options->provider_account_id,
          "Could not allocate default provider-account option.", error_out)) {
      goto default_rollback;
    }
    if (((changed_fields & STRAPPY_SESSION_OPTION_MODEL) == 0U) &&
        (strcmp(current.provider_account_id,
                merged.provider_account_id) != 0)) {
      if (!strappy_db_copy_account_default_model(db,
                                                 merged.provider_account_id,
                                                 &selected_model_id,
                                                 error_out) ||
          !strappy_db_copy_options_value(
            &merged.model_id, selected_model_id,
            "Could not allocate the default account model.", error_out)) {
        goto default_rollback;
      }
    }
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_MODEL) != 0U) {
    if (!strappy_db_copy_model_account(db, merged.model_id,
                                       &selected_account_id, error_out)) {
      goto default_rollback;
    }
    if ((changed_fields & STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT) == 0U) {
      if (!strappy_db_copy_options_value(
            &merged.provider_account_id, selected_account_id,
            "Could not allocate the default model account.", error_out)) {
        goto default_rollback;
      }
    } else {
      int matches;
      if (!strappy_db_model_matches_account(db, merged.model_id,
                                            merged.provider_account_id,
                                            &matches, error_out) || !matches) {
        if ((error_out != NULL) && (*error_out == NULL)) {
          strappy_set_error(error_out,
                            "The default model and account use different providers.");
        }
        goto default_rollback;
      }
    }
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_ASSISTANT_SET) != 0U) &&
      !strappy_db_copy_options_value(
        &merged.assistant_set_id,
        options->assistant_set_id,
        "Could not allocate default assistant-set option.",
        error_out)) {
    goto default_rollback;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_WORKING_DIRECTORY) != 0U) &&
      !strappy_db_copy_options_value(
        &merged.working_directory,
        options->working_directory,
        "Could not allocate default working-directory option.",
        error_out)) {
    goto default_rollback;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_WEB_PROVIDER) != 0U) {
    merged.web_provider = options->web_provider;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_WEB_SEARCH) != 0U) {
    merged.web_search_enabled = options->web_search_enabled ? 1 : 0;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_BASH) != 0U) {
    merged.bash_enabled = options->bash_enabled ? 1 : 0;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_LIMIT_TO_ONE_TOOL) != 0U) {
    merged.limit_to_one_tool = options->limit_to_one_tool ? 1 : 0;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_ANSWER_QUALITY) != 0U) {
    merged.answer_quality_enabled = options->answer_quality_enabled ? 1 : 0;
  }
  if ((changed_fields & STRAPPY_SESSION_OPTION_ROUND_LIMIT) != 0U) {
    merged.round_limit = options->round_limit;
  }

  actual_changed_fields = 0U;
  if (((changed_fields & STRAPPY_SESSION_OPTION_MODEL) != 0U) &&
      (strcmp(current.model_id, merged.model_id) != 0)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_MODEL;
  }
  if (strcmp(current.provider_account_id, merged.provider_account_id) != 0) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT;
  }
  if (((actual_changed_fields & STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT) != 0U) &&
      (strcmp(current.model_id, merged.model_id) != 0)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_MODEL;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_ASSISTANT_SET) != 0U) &&
      (strcmp(current.assistant_set_id, merged.assistant_set_id) != 0)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_ASSISTANT_SET;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_WEB_PROVIDER) != 0U) &&
      (current.web_provider != merged.web_provider)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_WEB_PROVIDER;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_WEB_SEARCH) != 0U) &&
      (current.web_search_enabled != merged.web_search_enabled)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_WEB_SEARCH;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_BASH) != 0U) &&
      (current.bash_enabled != merged.bash_enabled)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_BASH;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_LIMIT_TO_ONE_TOOL) != 0U) &&
      (current.limit_to_one_tool != merged.limit_to_one_tool)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_LIMIT_TO_ONE_TOOL;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_ANSWER_QUALITY) != 0U) &&
      (current.answer_quality_enabled != merged.answer_quality_enabled)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_ANSWER_QUALITY;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_ROUND_LIMIT) != 0U) &&
      (current.round_limit != merged.round_limit)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_ROUND_LIMIT;
  }
  if (((changed_fields & STRAPPY_SESSION_OPTION_WORKING_DIRECTORY) != 0U) &&
      (strcmp(current.working_directory, merged.working_directory) != 0)) {
    actual_changed_fields |= STRAPPY_SESSION_OPTION_WORKING_DIRECTORY;
  }

  if (((actual_changed_fields & (STRAPPY_SESSION_OPTION_MODEL |
                                 STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT)) != 0U) &&
      !strappy_db_model_exists(db, merged.model_id, error_out)) {
    goto default_rollback;
  }

  now_ms = strappy_db_now_ms();
  ok = 1;
  if ((actual_changed_fields & (STRAPPY_SESSION_OPTION_MODEL |
                                STRAPPY_SESSION_OPTION_PROVIDER_ACCOUNT)) != 0U) {
    ok = strappy_db_save_default_account_model(
      db,
      merged.provider_account_id,
      merged.model_id,
      now_ms,
      error_out);
    if (ok) {
      ok = strappy_db_set_model_allowed_in_db(db,
                                              merged.model_id,
                                              1,
                                              error_out);
    }
  }
  if (ok && ((actual_changed_fields & stored_fields) != 0U)) {
    ok = strappy_db_save_default_session_options(db,
                                                &merged,
                                                now_ms,
                                                error_out);
  }
  if (!ok) {
    goto default_rollback;
  }

  strappy_session_options_destroy(&current);
  if (!strappy_db_copy_default_session_options(db,
                                               fallback_working_directory,
                                               &current,
                                               error_out) ||
      !strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit default-session-options update",
                       error_out)) {
    goto default_rollback;
  }
  strappy_db_release(db);
  strappy_session_options_destroy(&merged);
  free(selected_account_id);
  free(selected_model_id);
  if (saved_options_out != NULL) {
    strappy_session_options_destroy(saved_options_out);
    *saved_options_out = current;
    strappy_session_options_init(&current);
  }
  strappy_session_options_destroy(&current);
  if (actual_changed_fields_out != NULL) {
    *actual_changed_fields_out = actual_changed_fields;
  }
  return 1;

default_rollback:
  strappy_db_exec(db,
                  "ROLLBACK;",
                  "Could not roll back default-session-options update",
                  NULL);
  strappy_session_options_destroy(&current);
  strappy_session_options_destroy(&merged);
  free(selected_account_id);
  free(selected_model_id);
  strappy_db_release(db);
  return 0;
}
