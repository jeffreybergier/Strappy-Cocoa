#define _POSIX_C_SOURCE 200809L

#include "strappy_core.h"
#include "strappy_assistant_sets.h"
#include "strappy_config.h"
#include "strappy_db.h"
#include "strappy_file_scanner.h"
#include "strappy_model_catalog.h"
#include "strappy_skills.h"
#include "strappy_study.h"
#include "strappy_tools.h"
#include "cJSON.h"

#include <sqlite3.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HARNESS_RESOURCE_DIR "../shared/Resources"
#define HARNESS_SKILLS_FIXTURE_RESOURCE_DIR "fixtures/skills"
#define HARNESS_SKILL_INSTRUCTIONS_MAX_BYTES (64U * 1024U)
#define HARNESS_DATABASE_CONTEXT_QUERY_GUIDANCE \
  "Use database_query to inspect columns and view definitions with SELECT " \
  "type, sql FROM sqlite_schema WHERE name = 'table_or_view_name', then use " \
  "targeted SELECT queries to inspect rows."
#define HARNESS_DATABASE_LIST_EMPTY_GUIDANCE \
  "The user has not approved any databases for use. " \
  "You can ask the user to approve databases."

typedef struct harness_context {
  char temp_dir[1024];
  char catalog_path[1200];
  char database_path[1200];
  char sms_path[1200];
  char mail_library_dir[1200];
  char mail_dir[1200];
  char mail_envelope_path[1200];
  char mail_protected_path[1200];
  char *database_id;
  char *sms_id;
  char *mail_envelope_id;
  char *mail_protected_id;
} harness_context;

static int harness_join_path(char *output,
                             size_t output_size,
                             const char *directory,
                             const char *filename)
{
  int written;

  if ((output == NULL) || (output_size == 0U) || (directory == NULL) ||
      (filename == NULL)) {
    return 0;
  }

  written = snprintf(output, output_size, "%s/%s", directory, filename);
  return ((written > 0) && ((size_t)written < output_size)) ? 1 : 0;
}

static void harness_unlink_sqlite_files(const char *database_path)
{
  char sidecar_path[1400];
  int written;

  if ((database_path == NULL) || (database_path[0] == '\0')) {
    return;
  }

  unlink(database_path);
  written = snprintf(sidecar_path, sizeof(sidecar_path), "%s-wal", database_path);
  if ((written > 0) && ((size_t)written < sizeof(sidecar_path))) {
    unlink(sidecar_path);
  }
  written = snprintf(sidecar_path, sizeof(sidecar_path), "%s-shm", database_path);
  if ((written > 0) && ((size_t)written < sizeof(sidecar_path))) {
    unlink(sidecar_path);
  }
}

static int harness_ensure_directory(const char *path, const char *description)
{
  if ((path == NULL) || (path[0] == '\0') || (description == NULL)) {
    fprintf(stderr, "Directory request is incomplete.\n");
    return 0;
  }

  if ((mkdir(path, 0700) != 0) && (access(path, F_OK) != 0)) {
    perror(description);
    return 0;
  }

  return 1;
}

static void harness_context_init(harness_context *context)
{
  if (context == NULL) {
    return;
  }

  context->temp_dir[0] = '\0';
  context->catalog_path[0] = '\0';
  context->database_path[0] = '\0';
  context->sms_path[0] = '\0';
  context->mail_library_dir[0] = '\0';
  context->mail_dir[0] = '\0';
  context->mail_envelope_path[0] = '\0';
  context->mail_protected_path[0] = '\0';
  context->database_id = NULL;
  context->sms_id = NULL;
  context->mail_envelope_id = NULL;
  context->mail_protected_id = NULL;
}

static void harness_context_destroy(harness_context *context)
{
  if (context == NULL) {
    return;
  }

  free(context->database_id);
  context->database_id = NULL;
  free(context->sms_id);
  context->sms_id = NULL;
  free(context->mail_envelope_id);
  context->mail_envelope_id = NULL;
  free(context->mail_protected_id);
  context->mail_protected_id = NULL;

  if (context->catalog_path[0] != '\0') {
    harness_unlink_sqlite_files(context->catalog_path);
  }
  if (context->database_path[0] != '\0') {
    harness_unlink_sqlite_files(context->database_path);
  }
  if (context->sms_path[0] != '\0') {
    harness_unlink_sqlite_files(context->sms_path);
  }
  if (context->mail_envelope_path[0] != '\0') {
    harness_unlink_sqlite_files(context->mail_envelope_path);
  }
  if (context->mail_protected_path[0] != '\0') {
    harness_unlink_sqlite_files(context->mail_protected_path);
  }
  if (context->mail_dir[0] != '\0') {
    rmdir(context->mail_dir);
  }
  if (context->mail_library_dir[0] != '\0') {
    rmdir(context->mail_library_dir);
  }
  if (context->temp_dir[0] != '\0') {
    rmdir(context->temp_dir);
  }
}

static int harness_make_temp_dir(harness_context *context)
{
  const char *base;
  int written;

  if (context == NULL) {
    return 0;
  }

  base = getenv("TMPDIR");
  if ((base == NULL) || (base[0] == '\0')) {
    base = "/tmp";
  }

  written = snprintf(context->temp_dir,
                     sizeof(context->temp_dir),
                     "%s/strappy-linux-harness-XXXXXX",
                     base);
  if ((written <= 0) || ((size_t)written >= sizeof(context->temp_dir))) {
    fprintf(stderr, "Temporary path is too long.\n");
    return 0;
  }

  if (mkdtemp(context->temp_dir) == NULL) {
    perror("mkdtemp");
    return 0;
  }

  if (!harness_join_path(context->catalog_path,
                         sizeof(context->catalog_path),
                         context->temp_dir,
                         "catalog.sqlite") ||
      !harness_join_path(context->database_path,
                         sizeof(context->database_path),
                         context->temp_dir,
                         "user.sqlite") ||
      !harness_join_path(context->sms_path,
                         sizeof(context->sms_path),
                         context->temp_dir,
                         "sms.db")) {
    fprintf(stderr, "Could not build harness fixture paths.\n");
    return 0;
  }

  if (!harness_join_path(context->mail_library_dir,
                         sizeof(context->mail_library_dir),
                         context->temp_dir,
                         "Library") ||
      !harness_join_path(context->mail_dir,
                         sizeof(context->mail_dir),
                         context->mail_library_dir,
                         "Mail") ||
      !harness_join_path(context->mail_envelope_path,
                         sizeof(context->mail_envelope_path),
                         context->mail_dir,
                         "Envelope Index") ||
      !harness_join_path(context->mail_protected_path,
                         sizeof(context->mail_protected_path),
                         context->mail_dir,
                         "Protected Index")) {
    fprintf(stderr, "Could not build harness Mail fixture paths.\n");
    return 0;
  }

  return 1;
}

static int harness_exec_sql(sqlite3 *db, const char *sql)
{
  char *message;
  int rc;

  message = NULL;
  rc = sqlite3_exec(db, sql, NULL, NULL, &message);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "sqlite error: %s\n", (message != NULL) ? message : "unknown");
    sqlite3_free(message);
    return 0;
  }

  return 1;
}

static int harness_append_format(char *output,
                                 size_t output_size,
                                 size_t *used,
                                 const char *format,
                                 ...)
{
  va_list args;
  int written;

  if ((output == NULL) || (output_size == 0U) || (used == NULL) ||
      (*used >= output_size) || (format == NULL)) {
    return 0;
  }

  va_start(args, format);
  written = vsnprintf(output + *used, output_size - *used, format, args);
  va_end(args);

  if ((written < 0) || ((size_t)written >= (output_size - *used))) {
    return 0;
  }

  *used += (size_t)written;
  return 1;
}

static int harness_expect_catalog_sql_ok(const char *database_path,
                                         const char *sql,
                                         const char *description)
{
  sqlite3 *db;
  char *message;
  int rc;

  if ((database_path == NULL) || (database_path[0] == '\0') ||
      (sql == NULL) || (description == NULL)) {
    fprintf(stderr, "Catalog schema check is incomplete.\n");
    return 0;
  }

  db = NULL;
  rc = sqlite3_open_v2(database_path, &db, SQLITE_OPEN_READWRITE, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not open catalog for schema check: %s\n",
            (db != NULL) ? sqlite3_errmsg(db) : "unknown");
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  message = NULL;
  rc = sqlite3_exec(db, sql, NULL, NULL, &message);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Fresh catalog schema missing %s: %s\n",
            description,
            (message != NULL) ? message : "unknown");
    sqlite3_free(message);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

static int harness_expect_catalog_user_version(const char *database_path,
                                               int expected_version)
{
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int version;

  db = NULL;
  rc = sqlite3_open_v2(database_path, &db, SQLITE_OPEN_READWRITE, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not open catalog for user_version check: %s\n",
            (db != NULL) ? sqlite3_errmsg(db) : "unknown");
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "Could not prepare user_version check: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    fprintf(stderr, "Could not read user_version.\n");
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  version = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  if (version != expected_version) {
    fprintf(stderr,
            "Expected user_version %d, got %d.\n",
            expected_version,
            version);
    return 0;
  }

  return 1;
}

static int harness_expect_catalog_journal_mode(const char *database_path,
                                               const char *expected_mode)
{
  sqlite3 *db;
  sqlite3_stmt *stmt;
  const unsigned char *mode;
  int rc;
  int ok;

  if ((expected_mode == NULL) || (expected_mode[0] == '\0')) {
    fprintf(stderr, "Expected journal mode is missing.\n");
    return 0;
  }

  db = NULL;
  rc = sqlite3_open_v2(database_path, &db, SQLITE_OPEN_READWRITE, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not open catalog for journal mode check: %s\n",
            (db != NULL) ? sqlite3_errmsg(db) : "unknown");
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, "PRAGMA journal_mode;", -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "Could not prepare journal mode check: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  ok = 0;
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    mode = sqlite3_column_text(stmt, 0);
    ok = (mode != NULL) && (strcmp((const char *)mode, expected_mode) == 0);
    if (!ok) {
      fprintf(stderr,
              "Expected journal_mode %s, got %s.\n",
              expected_mode,
              (mode != NULL) ? (const char *)mode : "unknown");
    }
  } else {
    fprintf(stderr, "Could not read journal mode check.\n");
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return ok;
}

static int harness_expect_catalog_integer(const char *database_path,
                                          const char *sql,
                                          long long expected,
                                          const char *description)
{
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  long long value;

  db = NULL;
  rc = sqlite3_open_v2(database_path, &db, SQLITE_OPEN_READWRITE, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not open catalog for integer check: %s\n",
            (db != NULL) ? sqlite3_errmsg(db) : "unknown");
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not prepare %s check: %s\n",
            description,
            sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    fprintf(stderr, "Could not read %s check.\n", description);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  value = (long long)sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  if (value != expected) {
    fprintf(stderr,
            "Expected %s to be %lld, got %lld.\n",
            description,
            expected,
            value);
    return 0;
  }

  return 1;
}

static int harness_run_fresh_catalog_schema_tests(
  const harness_context *context)
{
  char *error;

  if (context == NULL) {
    return 0;
  }

  error = NULL;
  if (!strappy_db_initialize(context->catalog_path, &error)) {
    fprintf(stderr,
            "Could not initialize fresh catalog: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  return harness_expect_catalog_user_version(context->catalog_path, 1) &&
         harness_expect_catalog_journal_mode(context->catalog_path, "wal") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, name, model_id, provider_account_id, "
           "created_at_ms, updated_at_ms "
           "FROM sessions LIMIT 0;",
           "sessions columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT session_id, web_provider, web_search_enabled, bash_enabled, "
           "limit_to_one_tool, answer_quality_enabled, round_limit, "
           "updated_at_ms "
         "FROM session_settings LIMIT 0;",
           "session settings columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, assistant_set_id, web_provider, web_search_enabled, "
           "bash_enabled, limit_to_one_tool, answer_quality_enabled, "
           "round_limit, "
           "working_directory, updated_at_ms "
           "FROM default_session_options LIMIT 0;",
           "default session options columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT database_id, decision, hidden, auto_hidden, "
           "hidden_override, hidden_reason, updated_at_ms "
           "FROM database_permissions LIMIT 0;",
           "database permission classification columns") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT COUNT(*) FROM pragma_table_info('session_settings') "
           "WHERE name = 'web_provider' "
           "AND trim(dflt_value, char(39)) = 'auto';",
           1LL,
           "automatic session web-provider default") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT COUNT(*) FROM pragma_table_info('session_settings') "
           "WHERE name = 'web_search_enabled' AND dflt_value = '1';",
           1LL,
           "enabled session web-search default") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT COUNT(*) FROM pragma_table_info('session_settings') "
           "WHERE name = 'answer_quality_enabled' AND dflt_value = '0';",
           1LL,
           "disabled session answer-quality default") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT answer_quality_enabled FROM default_session_options "
           "WHERE id = 1;",
           0LL,
           "disabled default-session answer quality") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT COUNT(*) FROM pragma_table_info('session_settings') "
           "WHERE name = 'tool_call_limit';",
           0LL,
           "removed session tool-call-limit column") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT COUNT(*) FROM pragma_table_info('session_settings') "
           "WHERE name = 'round_limit' AND dflt_value = '50';",
           1LL,
           "session round-limit default") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT COUNT(*) FROM pragma_table_info('sessions') "
           "WHERE name IN ('web_provider','web_search_enabled','bash_enabled',"
           "'limit_to_one_tool','answer_quality_enabled','round_limit');",
           0LL,
           "session toggle columns remaining in sessions") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT session_id, working_directory, updated_at_ms "
           "FROM session_working_directories LIMIT 0;",
           "session working-directory columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, session_id, ordinal, prompt_group_key, state, "
           "created_at_ms, completed_at_ms FROM turns LIMIT 0;",
           "turns columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, turn_id, previous_request_id, round_index, "
           "request_kind, model_id, provider_account_id, "
           "instruction_revision_id, "
           "toolset_revision_id, input_from_sequence, "
           "input_through_sequence, new_input_from_sequence, "
           "parallel_tool_calls, state, wall_duration_ms "
           "FROM model_requests LIMIT 0;",
           "model_requests columns") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT COUNT(*) FROM pragma_table_info('model_requests') "
           "WHERE name = 'wall_duration_ms' AND [notnull] = 0 "
           "AND dflt_value IS NULL;",
           1LL,
           "nullable model request wall duration") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT COUNT(*) FROM pragma_table_info('model_requests') "
           "WHERE name = 'max_tool_calls';",
           0LL,
           "removed model request tool-call limit") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, request_id, previous_attempt_id, attempt_index, "
           "provider_account_id, state, method, endpoint, started_at_ms, "
           "completed_at_ms, "
           "http_status, request_bytes, response_bytes, total_us "
           "FROM http_attempts LIMIT 0;",
           "http_attempts columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT attempt_id, provider_response_id, provider_model_id, "
           "provider_status, incomplete_reason, error_type, error_code, "
           "error_message, parse_error FROM api_results LIMIT 0;",
           "api_results columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT attempt_id, input_tokens, cached_input_tokens, "
           "output_tokens, reasoning_tokens, total_tokens, cost_nano_usd "
           "FROM api_usage LIMIT 0;",
           "api_usage columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, session_id, turn_id, sequence, kind, "
           "introduced_request_id, source_attempt_id, source_item_index, "
           "provider_item_id, provider_status, include_in_context, "
           "timeline_visible, is_error, created_at_ms "
           "FROM conversation_items LIMIT 0;",
           "conversation_items columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT item_id, role, phase FROM message_items LIMIT 0;",
           "message_items columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, item_id, collection_name, ordinal, part_type, text "
           "FROM item_text_parts LIMIT 0;",
           "item_text_parts columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT item_id, provider_call_id, tool_name, tool_namespace "
           "FROM function_calls LIMIT 0;",
           "function_calls columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, response_attempt_id, outcome, guidance_version, "
           "evaluated_at_ms FROM answer_quality_audits LIMIT 0;",
           "answer_quality_audits columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT audit_id, ordinal, check_key, check_kind, label, status, "
           "tool_name, detail, evidence_item_id "
           "FROM answer_quality_checks LIMIT 0;",
           "answer_quality_checks columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT document_id, node_id, parent_node_id, ordinal, "
           "member_name, value_type, text_value, number_value, boolean_value "
           "FROM structured_nodes LIMIT 0;",
           "structured_nodes columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT d.id, d.assistant_database_id, l.path, p.decision, "
           "p.hidden FROM databases d JOIN database_locations l "
           "ON l.database_id = d.id LEFT JOIN database_permissions p "
           "ON p.database_id = d.id LIMIT 0;",
           "database catalog columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, provider_account_id, wire_model_id, canonical_slug, "
           "hugging_face_id, name, description, "
           "context_length, created_at_s, architecture_modality, "
           "architecture_tokenizer, architecture_instruct_type, "
           "catalog_active, last_seen_at_ms FROM models LIMIT 0;",
           "models columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, provider_id, display_name, created_at_ms, updated_at_ms "
           "FROM provider_accounts LIMIT 0;",
           "provider account columns") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT COUNT(*) FROM provider_accounts WHERE "
             "(id = 'openrouter' AND provider_id = 'openrouter') OR "
             "(id = 'openai_chatgpt' AND provider_id = 'openai_chatgpt');",
           2LL,
           "built-in provider account count") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, assistant_set_id, kind, subject, predicate, value, "
           "confidence_basis_points, source_item_id, created_at_ms, "
           "updated_at_ms "
           "FROM user_facts LIMIT 0;",
           "user_facts columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, database_id, kind, content, observed_modified_at_s, "
           "source_item_id FROM database_hints LIMIT 0;",
           "database_hints columns") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT COUNT(*) FROM pragma_table_info('database_hints') "
           "WHERE name IN ('title', 'evidence', "
             "'confidence_basis_points', 'observed_size_bytes');",
           0LL,
           "removed database_hints column count") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' "
           "AND (lower(sql) LIKE '%raw_json%' "
             "OR lower(sql) LIKE '%metadata_json%' "
             "OR lower(sql) LIKE '%headers_json%' "
             "OR lower(sql) LIKE '%body_json%');",
           0LL,
           "raw JSON schema column count") &&
         harness_expect_catalog_integer(
           context->catalog_path,
           "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' "
           "AND name IN ('session_messages','response_api_calls',"
             "'response_api_items','discovered_databases',"
             "'openrouter_models','helper_user_info',"
             "'helper_database_info');",
           0LL,
         "legacy table count");
}

static int harness_run_provider_account_reset_test(
  const harness_context *context)
{
  char path[1200];
  sqlite3 *db;
  char *error;
  int initialized;
  int ok;

  if ((context == NULL) ||
      !harness_join_path(path,
                         sizeof(path),
                         context->temp_dir,
                         "legacy-provider-schema.sqlite")) {
    return 0;
  }
  harness_unlink_sqlite_files(path);
  db = NULL;
  if (sqlite3_open(path, &db) != SQLITE_OK) {
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }
  ok = harness_exec_sql(
    db,
    "CREATE TABLE schema_metadata ("
      "id INTEGER PRIMARY KEY, schema_name TEXT NOT NULL, "
      "created_at_ms INTEGER NOT NULL);"
    "INSERT INTO schema_metadata VALUES (1, 'semantic-v1', 1);"
    "CREATE TABLE models (id TEXT PRIMARY KEY, name TEXT NOT NULL);"
    "PRAGMA user_version = 1;");
  sqlite3_close(db);
  if (!ok) {
    harness_unlink_sqlite_files(path);
    return 0;
  }

  error = NULL;
  initialized = strappy_db_initialize(path, &error);
  ok = !initialized && (error != NULL) &&
    (strstr(error, "no migration is performed") != NULL) &&
    harness_expect_catalog_integer(
      path,
      "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' "
      "AND name = 'provider_accounts';",
      0LL,
      "provider-account reset leaves legacy schema unconverted");
  if (!ok) {
    fprintf(stderr,
            "Legacy provider schema was not rejected cleanly: %s\n",
            (error != NULL) ? error : "unexpected initialization result");
  }
  strappy_free_string(error);
  harness_unlink_sqlite_files(path);
  return ok;
}

static int harness_create_user_database(const char *database_path)
{
  sqlite3 *db;
  int rc;
  int ok;

  db = NULL;
  rc = sqlite3_open_v2(database_path,
                       &db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                       NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not create user database: %s\n",
            (db != NULL) ? sqlite3_errmsg(db) : "unknown");
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  ok = harness_exec_sql(
    db,
    "CREATE TABLE messages ("
    "id INTEGER PRIMARY KEY,"
    "sender TEXT NOT NULL,"
    "body TEXT NOT NULL"
    ");"
    "INSERT INTO messages(sender, body) VALUES "
    "('alice', 'hello'),"
    "('bob', 'hi');"
    "CREATE TABLE identifiers ("
    "value INTEGER NOT NULL"
    ");"
    "INSERT INTO identifiers(value) VALUES "
    "(2556198414531480000),"
    "(-5023472826755880000),"
    "(42);"
    "CREATE TABLE payloads ("
    "large_text TEXT NOT NULL,"
    "data BLOB NOT NULL,"
    "binary_text BLOB NOT NULL"
    ");"
    "INSERT INTO payloads(large_text, data, binary_text) VALUES "
    "(printf('%05000d', 0), zeroblob(20),"
    "X'62706c6973743030D4010203');");

  sqlite3_close(db);
  return ok;
}

static int harness_replace_with_non_database_file(const char *path)
{
  static const char contents[] = "not a sqlite database";
  FILE *file;
  int ok;

  file = fopen(path, "wb");
  if (file == NULL) {
    fprintf(stderr, "Could not replace database fixture: %s\n", path);
    return 0;
  }

  ok = (fwrite(contents, 1U, sizeof(contents) - 1U, file) ==
        (sizeof(contents) - 1U)) ? 1 : 0;
  if (fclose(file) != 0) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr, "Could not write non-database fixture: %s\n", path);
  }
  return ok;
}

static int harness_create_wide_column_database(const char *database_path)
{
  sqlite3 *db;
  char sql[4096];
  size_t used;
  int column;
  int rc;
  int ok;

  db = NULL;
  rc = sqlite3_open_v2(database_path,
                       &db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                       NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not create wide-column database: %s\n",
            (db != NULL) ? sqlite3_errmsg(db) : "unknown");
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  used = 0U;
  ok = harness_append_format(sql,
                             sizeof(sql),
                             &used,
                             "CREATE TABLE wide_table ("
                             "id INTEGER PRIMARY KEY");
  for (column = 1; ok && (column <= 70); column++) {
    ok = harness_append_format(sql,
                               sizeof(sql),
                               &used,
                               ", c%02d TEXT",
                               column);
  }
  ok = ok && harness_append_format(sql,
                                   sizeof(sql),
                                   &used,
                                   ");"
                                   "INSERT INTO wide_table(id, c01) "
                                   "VALUES (1, 'wide');");
  if (!ok) {
    fprintf(stderr, "Could not build wide-column fixture SQL.\n");
    sqlite3_close(db);
    return 0;
  }

  ok = harness_exec_sql(db, sql);
  sqlite3_close(db);
  return ok;
}

static int harness_create_open_wal_database(const char *database_path,
                                            sqlite3 **db_out)
{
  sqlite3 *db;
  char wal_path[1400];
  int written;
  int rc;

  if ((database_path == NULL) || (database_path[0] == '\0') ||
      (db_out == NULL)) {
    fprintf(stderr, "WAL database fixture request is incomplete.\n");
    return 0;
  }
  *db_out = NULL;

  db = NULL;
  rc = sqlite3_open_v2(database_path,
                       &db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                       NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not create WAL user database: %s\n",
            (db != NULL) ? sqlite3_errmsg(db) : "unknown");
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  if (!harness_exec_sql(
        db,
        "PRAGMA journal_mode = WAL;"
        "PRAGMA wal_autocheckpoint = 0;"
        "CREATE TABLE wal_messages ("
        "id INTEGER PRIMARY KEY,"
        "note TEXT NOT NULL"
        ");"
        "INSERT INTO wal_messages(note) VALUES ('from wal');")) {
    sqlite3_close(db);
    return 0;
  }

  written = snprintf(wal_path, sizeof(wal_path), "%s-wal", database_path);
  if ((written <= 0) || ((size_t)written >= sizeof(wal_path))) {
    fprintf(stderr, "Could not build WAL sidecar path.\n");
    sqlite3_close(db);
    return 0;
  }
  if (access(wal_path, F_OK) != 0) {
    fprintf(stderr, "Expected WAL sidecar was not created: %s\n", wal_path);
    sqlite3_close(db);
    return 0;
  }

  *db_out = db;
  return 1;
}

static int harness_create_sms_database(const char *database_path)
{
  sqlite3 *db;
  int rc;
  int ok;

  db = NULL;
  rc = sqlite3_open_v2(database_path,
                       &db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                       NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not create SMS fixture database: %s\n",
            (db != NULL) ? sqlite3_errmsg(db) : "unknown");
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  ok = harness_exec_sql(
    db,
    "CREATE TABLE handle ("
    "ROWID INTEGER PRIMARY KEY,"
    "id TEXT NOT NULL,"
    "country TEXT,"
    "service TEXT NOT NULL,"
    "uncanonicalized_id TEXT"
    ");"
    "CREATE TABLE message ("
    "ROWID INTEGER PRIMARY KEY,"
    "guid TEXT NOT NULL,"
    "text TEXT,"
    "handle_id INTEGER,"
    "attributedBody BLOB,"
    "date INTEGER,"
    "date_read INTEGER,"
    "date_delivered INTEGER,"
    "is_from_me INTEGER DEFAULT 0"
    ");"
    "CREATE TABLE chat ("
    "ROWID INTEGER PRIMARY KEY,"
    "guid TEXT NOT NULL,"
    "chat_identifier TEXT,"
    "service_name TEXT,"
    "room_name TEXT"
    ");"
    "CREATE TABLE chat_handle_join ("
    "chat_id INTEGER,"
    "handle_id INTEGER"
    ");"
    "CREATE TABLE chat_message_join ("
    "chat_id INTEGER,"
    "message_id INTEGER,"
    "PRIMARY KEY(chat_id, message_id)"
    ");"
    "CREATE TABLE attachment ("
    "ROWID INTEGER PRIMARY KEY,"
    "guid TEXT NOT NULL,"
    "created_date INTEGER,"
    "start_date INTEGER,"
    "filename TEXT,"
    "mime_type TEXT"
    ");"
    "CREATE TABLE message_attachment_join ("
    "message_id INTEGER,"
    "attachment_id INTEGER"
    ");"
    "INSERT INTO handle(ROWID, id, service, uncanonicalized_id) "
    "VALUES (1, '+15551234567', 'iMessage', '(555) 123-4567');"
    "INSERT INTO chat(ROWID, guid, chat_identifier, service_name) "
    "VALUES (1, 'chat-guid', '+15551234567', 'iMessage');"
    "INSERT INTO chat_handle_join(chat_id, handle_id) VALUES (1, 1);"
    "INSERT INTO message(ROWID, guid, text, handle_id, date, is_from_me) "
    "VALUES (1, 'message-guid', 'hello', 1, 790735841, 0);"
    "INSERT INTO chat_message_join(chat_id, message_id) VALUES (1, 1);");

  sqlite3_close(db);
  return ok;
}

static int harness_create_mail_database(const harness_context *context,
                                        int protected_index)
{
  const char *database_path;
  sqlite3 *db;
  int rc;
  int ok;

  if (context == NULL) {
    return 0;
  }

  if ((mkdir(context->mail_library_dir, 0700) != 0) &&
      (access(context->mail_library_dir, F_OK) != 0)) {
    perror("mkdir Library");
    return 0;
  }
  if ((mkdir(context->mail_dir, 0700) != 0) &&
      (access(context->mail_dir, F_OK) != 0)) {
    perror("mkdir Mail");
    return 0;
  }

  database_path = protected_index ? context->mail_protected_path :
                                    context->mail_envelope_path;
  db = NULL;
  rc = sqlite3_open_v2(database_path,
                       &db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                       NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not create Mail fixture database: %s\n",
            (db != NULL) ? sqlite3_errmsg(db) : "unknown");
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  if (protected_index) {
    ok = harness_exec_sql(
      db,
      "CREATE TABLE messages ("
      "message_id INTEGER PRIMARY KEY,"
      "sender,"
      "subject,"
      "_to,"
      "cc,"
      "bcc"
      ");"
      "CREATE TABLE message_data ("
      "message_data_id INTEGER PRIMARY KEY,"
      "data"
      ");"
      "INSERT INTO messages(message_id, sender, subject) VALUES "
      "(1, 'sender@example.com', 'hello');");
  } else {
    ok = harness_exec_sql(
      db,
      "CREATE TABLE mailboxes ("
      "ROWID INTEGER PRIMARY KEY,"
      "url UNIQUE"
      ");"
      "CREATE TABLE messages ("
      "ROWID INTEGER PRIMARY KEY AUTOINCREMENT,"
      "remote_id INTEGER,"
      "date_sent INTEGER,"
      "date_received INTEGER,"
      "mailbox INTEGER,"
      "deleted,"
      "visible,"
      "message_id INTEGER"
      ");"
      "INSERT INTO mailboxes(ROWID, url) VALUES "
      "(1, 'imap://user@example.com/INBOX');"
      "INSERT INTO messages(remote_id, date_received, mailbox, deleted, visible, message_id) "
      "VALUES (6547, 1781864527, 1, '0', '1', 2556198414531479370);");
  }

  sqlite3_close(db);
  return ok;
}

static int harness_expect_session_error_contains(const char *catalog_path,
                                                 long long session_id,
                                                 const char *tool_name,
                                                 const char *arguments_json,
                                                 const char *needle)
{
  char *error;
  char *output;
  int ok;

  error = NULL;
  output = strappy_tools_execute(catalog_path,
                                 session_id,
                                 HARNESS_RESOURCE_DIR,
                                 tool_name,
                                 arguments_json,
                                 &error);
  if (output != NULL) {
    fprintf(stderr, "Expected error but got output: %s\n", output);
    free(output);
    return 0;
  }

  ok = ((error != NULL) && (strstr(error, needle) != NULL)) ? 1 : 0;
  if (!ok) {
    fprintf(stderr,
            "Expected error containing '%s' but got: %s\n",
            needle,
            (error != NULL) ? error : "(null)");
  }

  free(error);
  return ok;
}

static int harness_expect_error_contains(const char *catalog_path,
                                         const char *tool_name,
                                         const char *arguments_json,
                                         const char *needle)
{
  return harness_expect_session_error_contains(catalog_path,
                                               0LL,
                                               tool_name,
                                               arguments_json,
                                               needle);
}

static int harness_expect_output_contains(const char *catalog_path,
                                          const char *tool_name,
                                          const char *arguments_json,
                                          const char *first_needle,
                                          const char *second_needle)
{
  char *error;
  char *output;
  int ok;

  error = NULL;
  output = strappy_tools_execute(catalog_path,
                                 0LL,
                                 HARNESS_RESOURCE_DIR,
                                 tool_name,
                                 arguments_json,
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Expected output but got error: %s\n",
            (error != NULL) ? error : "(null)");
    free(error);
    return 0;
  }

  ok = ((strstr(output, first_needle) != NULL) &&
        (strstr(output, second_needle) != NULL)) ? 1 : 0;
  if (!ok) {
    fprintf(stderr,
            "Output did not contain expected values '%s' and '%s': %s\n",
            first_needle,
            second_needle,
            output);
  }

  free(output);
  return ok;
}

static int harness_expect_output_is_valid_utf8(const char *catalog_path,
                                               const char *tool_name,
                                               const char *arguments_json)
{
  char *error;
  char *output;
  int ok;

  error = NULL;
  output = strappy_tools_execute(catalog_path,
                                 0LL,
                                 HARNESS_RESOURCE_DIR,
                                 tool_name,
                                 arguments_json,
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Expected UTF-8 output but got error: %s\n",
            (error != NULL) ? error : "(null)");
    free(error);
    return 0;
  }

  ok = strappy_utf8_validate(output, strlen(output));
  if (!ok) {
    fprintf(stderr, "Tool output was not valid UTF-8.\n");
  }
  free(error);
  free(output);
  return ok;
}

static cJSON *harness_tool_output_json(const char *catalog_path,
                                       const char *tool_name,
                                       const char *arguments_json)
{
  char *error;
  char *output;
  cJSON *root;

  error = NULL;
  output = strappy_tools_execute(catalog_path,
                                 0LL,
                                 HARNESS_RESOURCE_DIR,
                                 tool_name,
                                 arguments_json,
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Expected JSON output but got error: %s\n",
            (error != NULL) ? error : "(null)");
    free(error);
    return NULL;
  }

  root = cJSON_Parse(output);
  if (root == NULL) {
    fprintf(stderr, "Could not parse tool output JSON: %s\n", output);
  }
  free(output);
  return root;
}

static cJSON *harness_session_tool_output_json(const char *catalog_path,
                                               long long session_id,
                                               const char *tool_name,
                                               const char *arguments_json)
{
  char *error;
  char *output;
  cJSON *root;

  error = NULL;
  output = strappy_tools_execute(catalog_path,
                                 session_id,
                                 HARNESS_RESOURCE_DIR,
                                 tool_name,
                                 arguments_json,
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Expected session JSON output but got error: %s\n",
            (error != NULL) ? error : "(null)");
    free(error);
    return NULL;
  }

  root = cJSON_Parse(output);
  if (root == NULL) {
    fprintf(stderr, "Could not parse session tool output JSON: %s\n", output);
  }
  free(output);
  return root;
}

static int harness_expect_user_fact_read_result(const char *catalog_path,
                                                long long session_id,
                                                const char *arguments_json,
                                                long long expected_id,
                                                const char *expected_fact)
{
  cJSON *root;
  cJSON *item;
  cJSON *id;
  cJSON *fact;
  cJSON *date_saved;
  cJSON *property;
  int property_count;
  int ok;

  root = harness_session_tool_output_json(catalog_path,
                                          session_id,
                                          STRAPPY_TOOL_MEMORY_READ,
                                          arguments_json);
  if (root == NULL) {
    return 0;
  }

  item = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 0) : NULL;
  id = cJSON_IsObject(item) ? cJSON_GetObjectItem(item, "id") : NULL;
  fact = cJSON_IsObject(item) ? cJSON_GetObjectItem(item, "fact") : NULL;
  date_saved = cJSON_IsObject(item) ?
    cJSON_GetObjectItem(item, "date_saved") : NULL;
  property_count = 0;
  if (cJSON_IsObject(item)) {
    for (property = item->child; property != NULL; property = property->next) {
      property_count++;
    }
  }

  ok = cJSON_IsArray(root) && (cJSON_GetArraySize(root) == 1) &&
    cJSON_IsNumber(id) && (id->valuedouble == (double)expected_id) &&
    cJSON_IsString(fact) && (fact->valuestring != NULL) &&
    (expected_fact != NULL) && (strcmp(fact->valuestring, expected_fact) == 0) &&
    cJSON_IsString(date_saved) && (date_saved->valuestring != NULL) &&
    (date_saved->valuestring[0] != '\0') && (property_count == 3);
  if (!ok) {
    fprintf(stderr,
            "memory_read result did not match its public shape.\n");
  }

  cJSON_Delete(root);
  return ok;
}

typedef struct harness_database_context_expectation {
  const char *context;
  const char *const *tables;
  size_t table_count;
  int tables_truncated;
  const char *const *views;
  size_t view_count;
  int views_truncated;
} harness_database_context_expectation;

static int harness_object_has_exact_keys(cJSON *object,
                                         const char *const *keys,
                                         size_t key_count)
{
  cJSON *property;
  size_t index;

  if (!cJSON_IsObject(object) || ((key_count > 0U) && (keys == NULL))) {
    return 0;
  }

  property = object->child;
  for (index = 0U; index < key_count; index++) {
    if ((property == NULL) || (property->string == NULL) ||
        (strcmp(property->string, keys[index]) != 0)) {
      return 0;
    }
    property = property->next;
  }
  return (property == NULL) ? 1 : 0;
}

static int harness_string_array_equals(cJSON *array,
                                       const char *const *expected,
                                       size_t expected_count)
{
  cJSON *item;
  size_t index;

  if (!cJSON_IsArray(array) ||
      ((expected_count > 0U) && (expected == NULL))) {
    return 0;
  }

  item = array->child;
  for (index = 0U; index < expected_count; index++) {
    if (!cJSON_IsString(item) || (item->valuestring == NULL) ||
        (strcmp(item->valuestring, expected[index]) != 0)) {
      return 0;
    }
    item = item->next;
  }
  return (item == NULL) ? 1 : 0;
}

static int harness_json_bool_equals(cJSON *value, int expected)
{
  return expected ? cJSON_IsTrue(value) : cJSON_IsFalse(value);
}

static int harness_expect_database_query_shape(
  const char *catalog_path,
  const char *arguments_json,
  const char *const *expected_columns,
  size_t expected_column_count,
  size_t expected_row_count,
  int expected_rows_truncated)
{
  static const char *const keys[] = {
    "columns",
    "rows",
    "rows_truncated"
  };
  cJSON *root;
  cJSON *columns;
  cJSON *rows;
  cJSON *rows_truncated;
  cJSON *row;
  int ok;

  root = harness_tool_output_json(catalog_path,
                                  STRAPPY_TOOL_DATABASE_QUERY,
                                  arguments_json);
  if (root == NULL) {
    return 0;
  }

  columns = cJSON_GetObjectItemCaseSensitive(root, "columns");
  rows = cJSON_GetObjectItemCaseSensitive(root, "rows");
  rows_truncated = cJSON_GetObjectItemCaseSensitive(root,
                                                     "rows_truncated");
  ok = harness_object_has_exact_keys(root,
                                     keys,
                                     sizeof(keys) / sizeof(keys[0])) &&
    harness_string_array_equals(columns,
                                expected_columns,
                                expected_column_count) &&
    cJSON_IsArray(rows) &&
    (cJSON_GetArraySize(rows) == (int)expected_row_count) &&
    harness_json_bool_equals(rows_truncated, expected_rows_truncated);
  for (row = cJSON_IsArray(rows) ? rows->child : NULL;
       ok && (row != NULL);
       row = row->next) {
    ok = cJSON_IsArray(row) &&
      (cJSON_GetArraySize(row) == (int)expected_column_count);
  }

  if (!ok) {
    char *actual;

    actual = cJSON_PrintUnformatted(root);
    fprintf(stderr,
            "database_query result did not match its compact public shape: "
            "%s\n",
            (actual != NULL) ? actual : "(unserializable)");
    free(actual);
  }

  cJSON_Delete(root);
  return ok;
}

static int harness_expect_database_context_result(
  const char *catalog_path,
  const char *arguments_json,
  const harness_database_context_expectation *expected)
{
  static const char *const keys_with_guidance[] = {
    "context",
    "guidance",
    "tables",
    "tables_truncated",
    "views",
    "views_truncated"
  };
  static const char *const keys_without_guidance[] = {
    "context",
    "tables",
    "tables_truncated",
    "views",
    "views_truncated"
  };
  cJSON *root;
  cJSON *context;
  cJSON *guidance;
  cJSON *tables;
  cJSON *tables_truncated;
  cJSON *views;
  cJSON *views_truncated;
  int expects_guidance;
  int ok;

  if (expected == NULL) {
    return 0;
  }

  root = harness_tool_output_json(catalog_path,
                                  STRAPPY_TOOL_DATABASE_CONTEXT,
                                  arguments_json);
  if (root == NULL) {
    return 0;
  }

  context = cJSON_GetObjectItemCaseSensitive(root, "context");
  guidance = cJSON_GetObjectItemCaseSensitive(root, "guidance");
  tables = cJSON_GetObjectItemCaseSensitive(root, "tables");
  tables_truncated = cJSON_GetObjectItemCaseSensitive(root,
                                                       "tables_truncated");
  views = cJSON_GetObjectItemCaseSensitive(root, "views");
  views_truncated = cJSON_GetObjectItemCaseSensitive(root,
                                                      "views_truncated");

  expects_guidance =
    ((expected->table_count > 0U) || (expected->view_count > 0U)) ? 1 : 0;
  ok = harness_object_has_exact_keys(
         root,
         expects_guidance ? keys_with_guidance : keys_without_guidance,
         expects_guidance ?
           (sizeof(keys_with_guidance) / sizeof(keys_with_guidance[0])) :
           (sizeof(keys_without_guidance) /
            sizeof(keys_without_guidance[0]))) &&
    (expects_guidance ?
       (cJSON_IsString(guidance) && (guidance->valuestring != NULL) &&
       (strcmp(guidance->valuestring,
                HARNESS_DATABASE_CONTEXT_QUERY_GUIDANCE) == 0)) :
       (guidance == NULL)) &&
    ((expected->context != NULL) ?
       (cJSON_IsString(context) && (context->valuestring != NULL) &&
        (strcmp(context->valuestring, expected->context) == 0)) :
       cJSON_IsNull(context)) &&
    harness_string_array_equals(tables,
                                expected->tables,
                                expected->table_count) &&
    harness_json_bool_equals(tables_truncated, expected->tables_truncated) &&
    harness_string_array_equals(views,
                                expected->views,
                                expected->view_count) &&
    harness_json_bool_equals(views_truncated, expected->views_truncated);
  if (!ok) {
    char *actual;

    actual = cJSON_PrintUnformatted(root);
    fprintf(stderr,
            "database_context result did not match its compact public "
            "shape: %s\n",
            (actual != NULL) ? actual : "(unserializable)");
    free(actual);
  }

  cJSON_Delete(root);
  return ok;
}

static int harness_expect_output_contains_without(const char *catalog_path,
                                                  const char *tool_name,
                                                  const char *arguments_json,
                                                  const char *first_needle,
                                                  const char *second_needle,
                                                  const char *forbidden_needle)
{
  char *error;
  char *output;
  int ok;

  error = NULL;
  output = strappy_tools_execute(catalog_path,
                                 0LL,
                                 HARNESS_RESOURCE_DIR,
                                 tool_name,
                                 arguments_json,
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Expected output but got error: %s\n",
            (error != NULL) ? error : "(null)");
    free(error);
    return 0;
  }

  ok = ((strstr(output, first_needle) != NULL) &&
        (strstr(output, second_needle) != NULL) &&
        (strstr(output, forbidden_needle) == NULL)) ? 1 : 0;
  if (!ok) {
    fprintf(stderr,
            "Output did not contain '%s' and '%s' while excluding '%s': %s\n",
            first_needle,
            second_needle,
            forbidden_needle,
            output);
  }

  free(output);
  return ok;
}

static int harness_expect_session_output_equals(const char *catalog_path,
                                                long long session_id,
                                                const char *tool_name,
                                                const char *arguments_json,
                                                const char *expected)
{
  char *error;
  char *output;
  int ok;

  error = NULL;
  output = strappy_tools_execute(catalog_path,
                                 session_id,
                                 HARNESS_RESOURCE_DIR,
                                 tool_name,
                                 arguments_json,
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Expected output but got error: %s\n",
            (error != NULL) ? error : "(null)");
    free(error);
    return 0;
  }

  ok = (strcmp(output, expected) == 0) ? 1 : 0;
  if (!ok) {
    fprintf(stderr,
            "Output did not equal expected value.\nExpected: %s\nActual: %s\n",
            expected,
            output);
  }

  free(output);
  return ok;
}

static int harness_expect_output_equals(const char *catalog_path,
                                        const char *tool_name,
                                        const char *arguments_json,
                                        const char *expected)
{
  return harness_expect_session_output_equals(catalog_path,
                                              0LL,
                                              tool_name,
                                              arguments_json,
                                              expected);
}

static int harness_tool_schemas_hide_display_metadata(cJSON *tools)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    if (cJSON_GetObjectItem(tool, "x-strappy-display") != NULL) {
      return 0;
    }
  }
  return 1;
}

static int harness_json_array_contains_string(cJSON *array,
                                              const char *expected)
{
  cJSON *item;

  if (!cJSON_IsArray(array) || (expected == NULL)) {
    return 0;
  }
  for (item = array->child; item != NULL; item = item->next) {
    if (cJSON_IsString(item) && (item->valuestring != NULL) &&
        (strcmp(item->valuestring, expected) == 0)) {
      return 1;
    }
  }
  return 0;
}

static int harness_datetime_tool_schema_matches(cJSON *tools,
                                                const char *tool_name,
                                                const char *items_name)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (tool_name == NULL) ||
      (items_name == NULL)) {
    return 0;
  }

  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *function;
    cJSON *name;
    cJSON *parameters;
    cJSON *properties;
    cJSON *items_property;
    cJSON *item_schema;
    cJSON *unit_property;
    cJSON *required;
    cJSON *additional_properties;
    cJSON *value;

    function = cJSON_GetObjectItemCaseSensitive(tool, "function");
    name = cJSON_IsObject(function) ?
      cJSON_GetObjectItemCaseSensitive(function, "name") : NULL;
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, tool_name) != 0)) {
      continue;
    }

    parameters = cJSON_GetObjectItemCaseSensitive(function, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "properties") : NULL;
    items_property = cJSON_IsObject(properties) ?
      cJSON_GetObjectItemCaseSensitive(properties, items_name) : NULL;
    item_schema = cJSON_IsObject(items_property) ?
      cJSON_GetObjectItemCaseSensitive(items_property, "items") : NULL;
    unit_property = cJSON_IsObject(properties) ?
      cJSON_GetObjectItemCaseSensitive(properties, "unit") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "required") : NULL;
    additional_properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters,
                                       "additionalProperties") : NULL;

    value = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "type") : NULL;
    if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
        (strcmp(value->valuestring, "object") != 0)) {
      return 0;
    }
    value = cJSON_IsObject(items_property) ?
      cJSON_GetObjectItemCaseSensitive(items_property, "type") : NULL;
    if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
        (strcmp(value->valuestring, "array") != 0)) {
      return 0;
    }
    value = cJSON_IsObject(item_schema) ?
      cJSON_GetObjectItemCaseSensitive(item_schema, "type") : NULL;
    if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
        (strcmp(value->valuestring, "string") != 0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(items_property, "minItems");
    if (!cJSON_IsNumber(value) || (value->valuedouble != 1.0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(items_property, "maxItems");
    if (!cJSON_IsNumber(value) || (value->valuedouble != 256.0)) {
      return 0;
    }
    value = cJSON_IsObject(unit_property) ?
      cJSON_GetObjectItemCaseSensitive(unit_property, "type") : NULL;
    if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
        (strcmp(value->valuestring, "string") != 0) ||
        !harness_json_array_contains_string(required, items_name) ||
        !harness_json_array_contains_string(required, "unit") ||
        !cJSON_IsFalse(additional_properties)) {
      return 0;
    }
    return 1;
  }

  return 0;
}

static int harness_tool_schema_has_no_properties(cJSON *tools,
                                                 const char *tool_name)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (tool_name == NULL)) {
    return 0;
  }

  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *function;
    cJSON *name;
    cJSON *parameters;
    cJSON *properties;
    cJSON *required;
    cJSON *additional_properties;

    function = cJSON_GetObjectItemCaseSensitive(tool, "function");
    name = cJSON_IsObject(function) ?
      cJSON_GetObjectItemCaseSensitive(function, "name") : NULL;
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, tool_name) != 0)) {
      continue;
    }

    parameters = cJSON_GetObjectItemCaseSensitive(function, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "properties") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "required") : NULL;
    additional_properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters,
                                       "additionalProperties") : NULL;

    return cJSON_IsObject(properties) && (properties->child == NULL) &&
      cJSON_IsArray(required) &&
      (cJSON_GetArraySize(required) == 0) &&
      cJSON_IsFalse(additional_properties);
  }

  return 0;
}

static int harness_database_context_schema_matches(cJSON *tools)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools)) {
    return 0;
  }

  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *function;
    cJSON *name;
    cJSON *parameters;
    cJSON *properties;
    cJSON *database_id;
    cJSON *type;
    cJSON *description;
    cJSON *min_length;
    cJSON *max_length;
    cJSON *required;
    cJSON *additional_properties;

    function = cJSON_GetObjectItemCaseSensitive(tool, "function");
    name = cJSON_IsObject(function) ?
      cJSON_GetObjectItemCaseSensitive(function, "name") : NULL;
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, STRAPPY_TOOL_DATABASE_CONTEXT) != 0)) {
      continue;
    }

    parameters = cJSON_GetObjectItemCaseSensitive(function, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "properties") : NULL;
    database_id = cJSON_IsObject(properties) ?
      cJSON_GetObjectItemCaseSensitive(properties, "database_id") : NULL;
    type = cJSON_IsObject(database_id) ?
      cJSON_GetObjectItemCaseSensitive(database_id, "type") : NULL;
    description = cJSON_IsObject(database_id) ?
      cJSON_GetObjectItemCaseSensitive(database_id, "description") : NULL;
    min_length = cJSON_IsObject(database_id) ?
      cJSON_GetObjectItemCaseSensitive(database_id, "minLength") : NULL;
    max_length = cJSON_IsObject(database_id) ?
      cJSON_GetObjectItemCaseSensitive(database_id, "maxLength") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "required") : NULL;
    additional_properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters,
                                       "additionalProperties") : NULL;

    return cJSON_IsObject(properties) &&
      (cJSON_GetArraySize(properties) == 1) &&
      cJSON_IsString(type) && (type->valuestring != NULL) &&
      (strcmp(type->valuestring, "string") == 0) &&
      cJSON_IsString(description) && (description->valuestring != NULL) &&
      (strcmp(description->valuestring,
              "Approved database ID returned by database_list.") == 0) &&
      cJSON_IsNumber(min_length) && (min_length->valuedouble == 1.0) &&
      cJSON_IsNumber(max_length) && (max_length->valuedouble == 128.0) &&
      cJSON_IsArray(required) && (cJSON_GetArraySize(required) == 1) &&
      harness_json_array_contains_string(required, "database_id") &&
      cJSON_IsFalse(additional_properties);
  }

  return 0;
}

static int harness_database_study_schema_matches(cJSON *tools)
{
  cJSON *tool;

  for (tool = cJSON_IsArray(tools) ? tools->child : NULL;
       tool != NULL;
       tool = tool->next) {
    cJSON *function;
    cJSON *name;
    cJSON *parameters;
    cJSON *properties;
    cJSON *database_id;
    cJSON *description;
    cJSON *context;
    cJSON *description_type;
    cJSON *description_min_length;
    cJSON *description_max_length;
    cJSON *description_pattern;
    cJSON *context_type;
    cJSON *context_min_length;
    cJSON *context_max_length;
    cJSON *context_pattern;
    cJSON *required;
    cJSON *additional_properties;

    function = cJSON_GetObjectItemCaseSensitive(tool, "function");
    name = cJSON_IsObject(function) ?
      cJSON_GetObjectItemCaseSensitive(function, "name") : NULL;
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, STRAPPY_TOOL_DATABASE_STUDY) != 0)) {
      continue;
    }
    parameters = cJSON_GetObjectItemCaseSensitive(function, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "properties") : NULL;
    database_id = cJSON_IsObject(properties) ?
      cJSON_GetObjectItemCaseSensitive(properties, "database_id") : NULL;
    description = cJSON_IsObject(properties) ?
      cJSON_GetObjectItemCaseSensitive(properties, "description") : NULL;
    context = cJSON_IsObject(properties) ?
      cJSON_GetObjectItemCaseSensitive(properties, "context") : NULL;
    description_type = cJSON_IsObject(description) ?
      cJSON_GetObjectItemCaseSensitive(description, "type") : NULL;
    description_min_length = cJSON_IsObject(description) ?
      cJSON_GetObjectItemCaseSensitive(description, "minLength") : NULL;
    description_max_length = cJSON_IsObject(description) ?
      cJSON_GetObjectItemCaseSensitive(description, "maxLength") : NULL;
    description_pattern = cJSON_IsObject(description) ?
      cJSON_GetObjectItemCaseSensitive(description, "pattern") : NULL;
    context_type = cJSON_IsObject(context) ?
      cJSON_GetObjectItemCaseSensitive(context, "type") : NULL;
    context_min_length = cJSON_IsObject(context) ?
      cJSON_GetObjectItemCaseSensitive(context, "minLength") : NULL;
    context_max_length = cJSON_IsObject(context) ?
      cJSON_GetObjectItemCaseSensitive(context, "maxLength") : NULL;
    context_pattern = cJSON_IsObject(context) ?
      cJSON_GetObjectItemCaseSensitive(context, "pattern") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "required") : NULL;
    additional_properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "additionalProperties") :
      NULL;

    return cJSON_IsObject(properties) &&
      (cJSON_GetArraySize(properties) == 3) &&
      cJSON_IsObject(database_id) &&
      cJSON_IsString(description_type) &&
      (strcmp(description_type->valuestring, "string") == 0) &&
      cJSON_IsNumber(description_min_length) &&
      (description_min_length->valuedouble == 1.0) &&
      cJSON_IsNumber(description_max_length) &&
      (description_max_length->valuedouble == 4096.0) &&
      cJSON_IsString(description_pattern) &&
      (strcmp(description_pattern->valuestring, "\\S") == 0) &&
      cJSON_IsString(context_type) &&
      (strcmp(context_type->valuestring, "string") == 0) &&
      cJSON_IsNumber(context_min_length) &&
      (context_min_length->valuedouble == 1.0) &&
      cJSON_IsNumber(context_max_length) &&
      (context_max_length->valuedouble == 4096.0) &&
      cJSON_IsString(context_pattern) &&
      (strcmp(context_pattern->valuestring, "\\S") == 0) &&
      cJSON_IsArray(required) && (cJSON_GetArraySize(required) == 3) &&
      harness_json_array_contains_string(required, "database_id") &&
      harness_json_array_contains_string(required, "description") &&
      harness_json_array_contains_string(required, "context") &&
      cJSON_IsFalse(additional_properties);
  }
  return 0;
}

static int harness_file_read_schema_matches(cJSON *tools)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *function;
    cJSON *name;
    cJSON *strict;
    cJSON *parameters;
    cJSON *properties;
    cJSON *path;
    cJSON *offset;
    cJSON *limit;
    cJSON *required;
    cJSON *additional_properties;
    cJSON *value;

    function = cJSON_GetObjectItemCaseSensitive(tool, "function");
    name = cJSON_IsObject(function) ?
      cJSON_GetObjectItemCaseSensitive(function, "name") : NULL;
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, STRAPPY_TOOL_FILE_READ) != 0)) {
      continue;
    }

    strict = cJSON_GetObjectItemCaseSensitive(function, "strict");
    parameters = cJSON_GetObjectItemCaseSensitive(function, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "properties") : NULL;
    path = cJSON_IsObject(properties) ?
      cJSON_GetObjectItemCaseSensitive(properties, "path") : NULL;
    offset = cJSON_IsObject(properties) ?
      cJSON_GetObjectItemCaseSensitive(properties, "offset") : NULL;
    limit = cJSON_IsObject(properties) ?
      cJSON_GetObjectItemCaseSensitive(properties, "limit") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "required") : NULL;
    additional_properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters,
                                       "additionalProperties") : NULL;
    if (!cJSON_IsFalse(strict) || !cJSON_IsObject(properties) ||
        (cJSON_GetArraySize(properties) != 3) ||
        !cJSON_IsArray(required) || (cJSON_GetArraySize(required) != 1) ||
        !harness_json_array_contains_string(required, "path") ||
        !cJSON_IsFalse(additional_properties)) {
      return 0;
    }

    value = cJSON_GetObjectItemCaseSensitive(path, "type");
    if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
        (strcmp(value->valuestring, "string") != 0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(path, "minLength");
    if (!cJSON_IsNumber(value) || (value->valuedouble != 1.0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(path, "maxLength");
    if (!cJSON_IsNumber(value) || (value->valuedouble != 4096.0)) {
      return 0;
    }

    value = cJSON_GetObjectItemCaseSensitive(offset, "type");
    if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
        (strcmp(value->valuestring, "integer") != 0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(offset, "minimum");
    if (!cJSON_IsNumber(value) || (value->valuedouble != 1.0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(offset, "maximum");
    if (!cJSON_IsNumber(value) ||
        (value->valuedouble != 2147483647.0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(offset, "default");
    if (!cJSON_IsNumber(value) || (value->valuedouble != 1.0)) {
      return 0;
    }

    value = cJSON_GetObjectItemCaseSensitive(limit, "type");
    if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
        (strcmp(value->valuestring, "integer") != 0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(limit, "minimum");
    if (!cJSON_IsNumber(value) || (value->valuedouble != 1.0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(limit, "maximum");
    if (!cJSON_IsNumber(value) || (value->valuedouble != 500.0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(limit, "default");
    return cJSON_IsNumber(value) && (value->valuedouble == 500.0);
  }
  return 0;
}

static int harness_file_string_property_matches(cJSON *property,
                                                int has_minimum,
                                                double minimum,
                                                double maximum)
{
  cJSON *type;
  cJSON *min_length;
  cJSON *max_length;

  type = cJSON_IsObject(property) ?
    cJSON_GetObjectItemCaseSensitive(property, "type") : NULL;
  min_length = cJSON_IsObject(property) ?
    cJSON_GetObjectItemCaseSensitive(property, "minLength") : NULL;
  max_length = cJSON_IsObject(property) ?
    cJSON_GetObjectItemCaseSensitive(property, "maxLength") : NULL;
  if (!cJSON_IsString(type) || (type->valuestring == NULL) ||
      (strcmp(type->valuestring, "string") != 0) ||
      !cJSON_IsNumber(max_length) ||
      (max_length->valuedouble != maximum)) {
    return 0;
  }
  if (!has_minimum) {
    return (min_length == NULL) ? 1 : 0;
  }
  return cJSON_IsNumber(min_length) &&
    (min_length->valuedouble == minimum);
}

static int harness_file_write_schema_matches(cJSON *tools)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *function;
    cJSON *name;
    cJSON *strict;
    cJSON *parameters;
    cJSON *properties;
    cJSON *required;
    cJSON *additional_properties;

    function = cJSON_GetObjectItemCaseSensitive(tool, "function");
    name = cJSON_IsObject(function) ?
      cJSON_GetObjectItemCaseSensitive(function, "name") : NULL;
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, STRAPPY_TOOL_FILE_WRITE) != 0)) {
      continue;
    }
    strict = cJSON_GetObjectItemCaseSensitive(function, "strict");
    parameters = cJSON_GetObjectItemCaseSensitive(function, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "properties") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "required") : NULL;
    additional_properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters,
                                       "additionalProperties") : NULL;
    return cJSON_IsFalse(strict) && cJSON_IsObject(properties) &&
      (cJSON_GetArraySize(properties) == 2) &&
      harness_file_string_property_matches(
        cJSON_GetObjectItemCaseSensitive(properties, "path"),
        1,
        1.0,
        4096.0) &&
      harness_file_string_property_matches(
        cJSON_GetObjectItemCaseSensitive(properties, "content"),
        0,
        0.0,
        1048576.0) &&
      cJSON_IsArray(required) && (cJSON_GetArraySize(required) == 2) &&
      harness_json_array_contains_string(required, "path") &&
      harness_json_array_contains_string(required, "content") &&
      cJSON_IsFalse(additional_properties);
  }
  return 0;
}

static int harness_file_edit_schema_matches(cJSON *tools)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *function;
    cJSON *name;
    cJSON *strict;
    cJSON *parameters;
    cJSON *properties;
    cJSON *required;
    cJSON *additional_properties;

    function = cJSON_GetObjectItemCaseSensitive(tool, "function");
    name = cJSON_IsObject(function) ?
      cJSON_GetObjectItemCaseSensitive(function, "name") : NULL;
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, STRAPPY_TOOL_FILE_EDIT) != 0)) {
      continue;
    }
    strict = cJSON_GetObjectItemCaseSensitive(function, "strict");
    parameters = cJSON_GetObjectItemCaseSensitive(function, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "properties") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "required") : NULL;
    additional_properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters,
                                       "additionalProperties") : NULL;
    return cJSON_IsFalse(strict) && cJSON_IsObject(properties) &&
      (cJSON_GetArraySize(properties) == 3) &&
      harness_file_string_property_matches(
        cJSON_GetObjectItemCaseSensitive(properties, "path"),
        1,
        1.0,
        4096.0) &&
      harness_file_string_property_matches(
        cJSON_GetObjectItemCaseSensitive(properties, "old_text"),
        1,
        1.0,
        1048576.0) &&
      harness_file_string_property_matches(
        cJSON_GetObjectItemCaseSensitive(properties, "new_text"),
        0,
        0.0,
        1048576.0) &&
      cJSON_IsArray(required) && (cJSON_GetArraySize(required) == 3) &&
      harness_json_array_contains_string(required, "path") &&
      harness_json_array_contains_string(required, "old_text") &&
      harness_json_array_contains_string(required, "new_text") &&
      cJSON_IsFalse(additional_properties);
  }
  return 0;
}

static int harness_bash_schema_matches(cJSON *tools)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *function;
    cJSON *name;
    cJSON *strict;
    cJSON *parameters;
    cJSON *properties;
    cJSON *command;
    cJSON *timeout;
    cJSON *required;
    cJSON *additional_properties;
    cJSON *value;

    function = cJSON_GetObjectItemCaseSensitive(tool, "function");
    name = cJSON_IsObject(function) ?
      cJSON_GetObjectItemCaseSensitive(function, "name") : NULL;
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, STRAPPY_TOOL_BASH) != 0)) {
      continue;
    }

    strict = cJSON_GetObjectItemCaseSensitive(function, "strict");
    parameters = cJSON_GetObjectItemCaseSensitive(function, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "properties") : NULL;
    command = cJSON_IsObject(properties) ?
      cJSON_GetObjectItemCaseSensitive(properties, "command") : NULL;
    timeout = cJSON_IsObject(properties) ?
      cJSON_GetObjectItemCaseSensitive(properties, "timeout") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "required") : NULL;
    additional_properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters,
                                       "additionalProperties") : NULL;
    if (!cJSON_IsFalse(strict) || !cJSON_IsObject(properties) ||
        (cJSON_GetArraySize(properties) != 2) ||
        !cJSON_IsArray(required) || (cJSON_GetArraySize(required) != 1) ||
        !harness_json_array_contains_string(required, "command") ||
        !cJSON_IsFalse(additional_properties)) {
      return 0;
    }

    value = cJSON_GetObjectItemCaseSensitive(command, "type");
    if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
        (strcmp(value->valuestring, "string") != 0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(command, "minLength");
    if (!cJSON_IsNumber(value) || (value->valuedouble != 1.0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(command, "maxLength");
    if (!cJSON_IsNumber(value) || (value->valuedouble != 32768.0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(command, "pattern");
    if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
        (strcmp(value->valuestring, "\\S") != 0)) {
      return 0;
    }

    value = cJSON_GetObjectItemCaseSensitive(timeout, "type");
    if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
        (strcmp(value->valuestring, "number") != 0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(timeout, "exclusiveMinimum");
    if (!cJSON_IsNumber(value) || (value->valuedouble != 0.0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(timeout, "maximum");
    if (!cJSON_IsNumber(value) || (value->valuedouble != 120.0)) {
      return 0;
    }
    value = cJSON_GetObjectItemCaseSensitive(timeout, "default");
    return cJSON_IsNumber(value) && (value->valuedouble == 120.0);
  }
  return 0;
}

static int harness_tool_display_matches(cJSON *registry,
                                        const char *tool_name,
                                        const char *promoted_argument,
                                        const char *transform)
{
  cJSON *display;
  cJSON *argument;
  cJSON *actual_transform;

  display = cJSON_GetObjectItem(registry, tool_name);
  argument = cJSON_IsObject(display) ?
    cJSON_GetObjectItem(display, "promoted_argument") : NULL;
  actual_transform = cJSON_IsObject(display) ?
    cJSON_GetObjectItem(display, "transform") : NULL;
  if (!cJSON_IsString(argument) || (argument->valuestring == NULL) ||
      (strcmp(argument->valuestring, promoted_argument) != 0)) {
    return 0;
  }
  if (transform == NULL) {
    return actual_transform == NULL;
  }
  return cJSON_IsString(actual_transform) &&
    (actual_transform->valuestring != NULL) &&
    (strcmp(actual_transform->valuestring, transform) == 0);
}

static int harness_server_tool_display_matches(cJSON *registry,
                                               const char *tool_type,
                                               const char *label,
                                               const char *path_first,
                                               const char *path_second,
                                               const char *transform)
{
  cJSON *display;
  cJSON *actual_label;
  cJSON *path;
  cJSON *response_item;
  cJSON *actual_transform;
  cJSON *part;
  int expected_path_count;

  display = cJSON_GetObjectItem(registry, tool_type);
  actual_label = cJSON_IsObject(display) ?
    cJSON_GetObjectItem(display, "label") : NULL;
  path = cJSON_IsObject(display) ?
    cJSON_GetObjectItem(display, "promoted_path") : NULL;
  response_item = cJSON_IsObject(display) ?
    cJSON_GetObjectItem(display, "response_item") : NULL;
  actual_transform = cJSON_IsObject(display) ?
    cJSON_GetObjectItem(display, "transform") : NULL;
  expected_path_count = (path_second != NULL) ? 2 : 1;
  if (!cJSON_IsString(actual_label) || (actual_label->valuestring == NULL) ||
      (strcmp(actual_label->valuestring, label) != 0) ||
      !cJSON_IsArray(path) ||
      (cJSON_GetArraySize(path) != expected_path_count) ||
      !cJSON_IsTrue(response_item)) {
    return 0;
  }
  part = cJSON_GetArrayItem(path, 0);
  if (!cJSON_IsString(part) || (part->valuestring == NULL) ||
      (strcmp(part->valuestring, path_first) != 0)) {
    return 0;
  }
  if (path_second != NULL) {
    part = cJSON_GetArrayItem(path, 1);
    if (!cJSON_IsString(part) || (part->valuestring == NULL) ||
        (strcmp(part->valuestring, path_second) != 0)) {
      return 0;
    }
  }
  if (transform == NULL) {
    return actual_transform == NULL;
  }
  return cJSON_IsString(actual_transform) &&
    (actual_transform->valuestring != NULL) &&
    (strcmp(actual_transform->valuestring, transform) == 0);
}

static int harness_run_tool_registry_tests(void)
{
  char *error;
  char *tools_json;
  char *filtered_json;
  char *registry_json;
  const char *allowed_names[1];
  cJSON *tools;
  cJSON *filtered;
  cJSON *registry;
  int ok;

  error = NULL;
  tools_json = strappy_tools_request_json(HARNESS_RESOURCE_DIR, &error);
  if (tools_json == NULL) {
    fprintf(stderr,
            "Expected tool schema but got error: %s\n",
            (error != NULL) ? error : "(null)");
    free(error);
    return 0;
  }

  tools = cJSON_Parse(tools_json);
  allowed_names[0] = STRAPPY_TOOL_DATABASE_QUERY;
  filtered_json = strappy_tools_request_json_filtered(HARNESS_RESOURCE_DIR,
                                                      allowed_names,
                                                      1U,
                                                      &error);
  filtered = (filtered_json != NULL) ? cJSON_Parse(filtered_json) : NULL;
  registry_json = strappy_tools_display_registry_json(HARNESS_RESOURCE_DIR,
                                                      &error);
  registry = (registry_json != NULL) ? cJSON_Parse(registry_json) : NULL;

  ok = (cJSON_IsArray(tools) &&
        harness_tool_schemas_hide_display_metadata(tools) &&
        harness_datetime_tool_schema_matches(
          tools,
          STRAPPY_TOOL_DATETIME_TO_ISO8601,
          "timestamps") &&
        harness_datetime_tool_schema_matches(
          tools,
          STRAPPY_TOOL_DATETIME_FROM_ISO8601,
          "datetimes") &&
        harness_tool_schema_has_no_properties(
          tools,
          STRAPPY_TOOL_MEMORY_READ) &&
        harness_tool_schema_has_no_properties(
          tools,
          STRAPPY_TOOL_SKILLS_LIST) &&
        harness_database_context_schema_matches(tools) &&
        harness_database_study_schema_matches(tools) &&
        harness_file_read_schema_matches(tools) &&
        harness_file_write_schema_matches(tools) &&
        harness_file_edit_schema_matches(tools) &&
        harness_bash_schema_matches(tools) &&
        (filtered_json != NULL) && cJSON_IsArray(filtered) &&
        (cJSON_GetArraySize(filtered) == 1) &&
        harness_tool_schemas_hide_display_metadata(filtered) &&
        (registry_json != NULL) && cJSON_IsObject(registry) &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_DATABASE_QUERY,
                                     "database_id",
                                     "database_filename") &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_FILE_READ,
                                     "path",
                                     NULL) &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_FILE_WRITE,
                                     "path",
                                     NULL) &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_FILE_EDIT,
                                     "path",
                                     NULL) &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_BASH,
                                     "command",
                                     NULL) &&
        harness_tool_display_matches(
          registry,
          STRAPPY_TOOL_FONTAWESOME_SEARCH,
          "query",
          NULL) &&
        harness_tool_display_matches(
          registry,
          STRAPPY_TOOL_FONTAWESOME_CONFIRM,
          "shortcodes",
          "comma_separated") &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_MEMORY_SAVE,
                                     "fact",
                                     NULL) &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_MEMORY_DELETE,
                                     "id",
                                     "identifier") &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_SKILL_READ,
                                     "skill_id",
                                     NULL) &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_SESSION_RENAME,
                                     "name",
                                     NULL) &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_DATABASE_CONTEXT,
                                     "database_id",
                                     "database_filename") &&
        harness_tool_display_matches(
          registry,
          STRAPPY_TOOL_DATABASE_STUDY,
          "database_id",
          "database_filename") &&
        harness_server_tool_display_matches(
          registry,
          STRAPPY_TOOL_OPENROUTER_WEB_SEARCH,
          "Web Search",
          "action",
          "query",
          NULL) &&
        harness_server_tool_display_matches(
          registry,
          STRAPPY_TOOL_OPENROUTER_WEB_FETCH,
          "Web Fetch",
          "url",
          NULL,
          "url") &&
        harness_server_tool_display_matches(
          registry,
          STRAPPY_TOOL_WEB_SEARCH,
          "Web Search",
          "action",
          "query",
          NULL) &&
        harness_server_tool_display_matches(
          registry,
          STRAPPY_RESPONSE_ITEM_WEB_SEARCH_CALL,
          "Web Search",
          "action",
          "query",
          NULL) &&
        (cJSON_GetObjectItem(registry,
                             STRAPPY_TOOL_DATABASE_LIST) == NULL) &&
        (cJSON_GetObjectItem(
           registry,
           STRAPPY_TOOL_DATETIME_TO_ISO8601) == NULL) &&
        (cJSON_GetObjectItem(
           registry,
           STRAPPY_TOOL_DATETIME_FROM_ISO8601) == NULL) &&
        (cJSON_GetObjectItem(registry,
                             STRAPPY_TOOL_MEMORY_READ) == NULL) &&
        (cJSON_GetObjectItem(registry,
                             STRAPPY_TOOL_SKILLS_LIST) == NULL) &&
        (strstr(tools_json,
                "Call this tool to view approved databases. Returns an "
                "object containing a databases array with database_id, "
                "description, app_name, path, size_bytes, and modified_at "
                "in Unix seconds. description is null until the current "
                "database file has been studied. "
                "When no databases are approved, the array is empty and "
                "guidance explains why.") != NULL) &&
        (strstr(tools_json,
                "Run one read-only SQLite query against an approved database. "
                "Use it whenever an answer depends on personal data.") !=
         NULL) &&
        (strstr(tools_json,
                "In Database Study, use schema queries to design access "
                "recipes, then execute every final recipe exactly as it will "
                "be saved. A sqlite_schema query or generic SELECT * sampling "
                "query is not a recipe.") != NULL) &&
        (strstr(tools_json,
                "Convert numeric timestamp strings to ISO 8601 before "
                "displaying them. In Database Study, use a queried timestamp "
                "to verify its unit before recording the unit; never copy the "
                "sampled value into saved context.") !=
         NULL) &&
        (strstr(tools_json,
                "ALWAYS call this tool when converting ISO 8601 datetimes to "
                "numeric timestamps.") != NULL) &&
        (strstr(tools_json,
                "ALWAYS use Font Awesome version 7 Free tools and shortcodes "
                "for visual expression instead of Unicode emoji.") != NULL) &&
        (strstr(tools_json,
                "ALWAYS call this tool before the final answer. Set "
                "shortcodes to a non-empty array of Font Awesome version 7 "
                "Free Markdown shortcodes, such as [fa:heart] or "
                "[fa:brands:github], to confirm before using them. Returns "
                "confirmed and not_found arrays.") != NULL) &&
        (strstr(tools_json, "NEVER use unicode emoji.") != NULL) &&
        (strstr(tools_json,
                "ALWAYS call this tool before the final answer. Update the "
                "session with a short, descriptive name for the user's "
                "latest prompt. A non-empty name is required; null and empty "
                "values are invalid.") !=
        NULL) &&
        (strstr(tools_json,
                "Call this tool to retrieve durable facts stored about the "
                "user.") != NULL) &&
        (strstr(tools_json,
                "Call this tool to save a useful durable user fact for "
                "future prompts. NEVER store secrets or sensitive "
                "information.") !=
         NULL) &&
        (strstr(tools_json,
                "Save one completed Database Study result after "
                "investigation.") !=
         NULL) &&
        (strstr(tools_json,
                "Provide description and context together in the same call.") !=
         NULL) &&
        (strstr(tools_json,
                "description is one sentence explaining what useful user data "
                "exists.") !=
         NULL) &&
        (strstr(tools_json,
                "context is compact how-to retrieval guidance, not a schema "
                "inventory.") !=
         NULL) &&
        (strstr(tools_json,
                "include its purpose and exact read-only SQL copied verbatim "
                "from a successful database_query in the current study "
                "batch.") !=
         NULL) &&
        (strstr(tools_json,
                "SQL must run as written, use literal limits, and contain no "
                "bind placeholders.") !=
         NULL) &&
        (strstr(tools_json,
                "mention only tables and columns used by the patterns.") !=
         NULL) &&
        (strstr(tools_json,
                "If no useful user-facing data exists, say so instead of "
                "listing schema.") !=
         NULL) &&
        (strstr(tools_json,
                "Never include sampled values, secrets, or sensitive "
                "identifiers.") !=
         NULL) &&
        (strstr(tools_json,
                "Call this first for a relevant approved database_id. Returns "
                "the studied context value (or null), bounded table and view "
                "names, and exploration guidance. The table and view names "
                "are generated on demand; do not copy their inventory into "
                "studied context. This result alone is not a completed "
                "Database Study.") !=
         NULL) &&
        (strstr(tools_json,
                "Call this tool to forget durable facts that are no longer "
                "correct or useful.") != NULL) &&
        (strstr(filtered_json,
                "Run one read-only SQLite query against an approved database. "
                "Use it whenever an answer depends on personal data.") !=
         NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_DATABASE_LIST) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_DATABASE_QUERY) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_BASH) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_FILE_READ) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_FILE_WRITE) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_FILE_EDIT) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_DATETIME_TO_ISO8601) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_DATETIME_FROM_ISO8601) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_MEMORY_READ) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_MEMORY_SAVE) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_MEMORY_DELETE) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_SKILLS_LIST) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_SKILL_READ) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_SESSION_RENAME) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_DATABASE_CONTEXT) != NULL) &&
        (strstr(tools_json,
                STRAPPY_TOOL_DATABASE_STUDY) != NULL) &&
        (strstr(tools_json,
                STRAPPY_TOOL_FONTAWESOME_SEARCH) != NULL) &&
        (strstr(tools_json,
                STRAPPY_TOOL_FONTAWESOME_CONFIRM) != NULL) &&
        strappy_tools_is_helper(STRAPPY_TOOL_DATETIME_TO_ISO8601) &&
        strappy_tools_is_helper(STRAPPY_TOOL_DATETIME_FROM_ISO8601) &&
        strappy_tools_is_helper(STRAPPY_TOOL_MEMORY_READ) &&
        strappy_tools_is_helper(STRAPPY_TOOL_MEMORY_SAVE) &&
        strappy_tools_is_helper(STRAPPY_TOOL_MEMORY_DELETE) &&
        strappy_tools_is_helper(STRAPPY_TOOL_SKILLS_LIST) &&
        strappy_tools_is_helper(STRAPPY_TOOL_SKILL_READ) &&
        strappy_tools_is_helper(STRAPPY_TOOL_SESSION_RENAME) &&
        strappy_tools_is_helper(STRAPPY_TOOL_DATABASE_CONTEXT) &&
        strappy_tools_is_helper(STRAPPY_TOOL_DATABASE_STUDY) &&
        strappy_tools_is_helper(STRAPPY_TOOL_FONTAWESOME_SEARCH) &&
        strappy_tools_is_helper(STRAPPY_TOOL_FONTAWESOME_CONFIRM) &&
        strappy_tools_is_registered(STRAPPY_TOOL_FILE_READ) &&
        !strappy_tools_is_helper(STRAPPY_TOOL_FILE_READ) &&
        strappy_tools_is_registered(STRAPPY_TOOL_FILE_WRITE) &&
        !strappy_tools_is_helper(STRAPPY_TOOL_FILE_WRITE) &&
        strappy_tools_is_registered(STRAPPY_TOOL_FILE_EDIT) &&
        !strappy_tools_is_helper(STRAPPY_TOOL_FILE_EDIT) &&
        strappy_tools_is_registered(STRAPPY_TOOL_BASH) &&
        !strappy_tools_is_helper(STRAPPY_TOOL_BASH) &&
        !strappy_tools_is_helper(STRAPPY_TOOL_DATABASE_QUERY) &&
        !strappy_tools_is_helper("helper_convert_dates") &&
        !strappy_tools_is_registered("database_list_info") &&
        !strappy_tools_is_registered("database_context_read") &&
        (strstr(tools_json, "database_list_info") == NULL) &&
        (strstr(tools_json, "database_context_read") == NULL) &&
        !strappy_tools_is_registered("session_name_write") &&
        (strstr(tools_json, "session_name_write") == NULL) &&
        !strappy_tools_is_registered("memory_database_hint_remember") &&
        !strappy_tools_is_registered("memory_database_hint_forget") &&
        (strstr(tools_json, "memory_database_hint_remember") == NULL) &&
        (strstr(tools_json, "memory_database_hint_forget") == NULL) &&
        !strappy_tools_is_registered("memory_user_fact_read") &&
        !strappy_tools_is_registered("memory_user_fact_remember") &&
        !strappy_tools_is_registered("memory_user_fact_forget") &&
        (strstr(tools_json, "memory_user_fact_read") == NULL) &&
        (strstr(tools_json, "memory_user_fact_remember") == NULL) &&
        (strstr(tools_json, "memory_user_fact_forget") == NULL) &&
        (strstr(tools_json, "memory_database_hint_read") == NULL) &&
        (strstr(tools_json, "helper_database_info_read") == NULL) &&
        (strstr(tools_json, "helper_user_info_read") == NULL) &&
        (strstr(tools_json, "helper_database_info_remember") == NULL) &&
        (strstr(tools_json, "helper_convert_dates") == NULL) &&
        (strstr(tools_json, "database_learn") == NULL)) ? 1 : 0;
  if (!ok) {
    fprintf(stderr,
            "Tool schema/display registry did not match expectations.\n"
            "API tools: %s\nDisplay registry: %s\n",
            tools_json,
            (registry_json != NULL) ? registry_json : "(null)");
  }

  cJSON_Delete(registry);
  cJSON_Delete(filtered);
  cJSON_Delete(tools);
  free(error);
  free(registry_json);
  free(filtered_json);
  free(tools_json);
  return ok;
}

static int harness_responses_tools_contains(cJSON *tools,
                                            const char *tool_name)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (tool_name == NULL)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *type;
    cJSON *name;

    type = cJSON_GetObjectItemCaseSensitive(tool, "type");
    name = cJSON_GetObjectItemCaseSensitive(tool, "name");
    if (cJSON_IsString(type) && (type->valuestring != NULL) &&
        (strcmp(type->valuestring, tool_name) == 0)) {
      return 1;
    }
    if (cJSON_IsString(name) && (name->valuestring != NULL) &&
        (strcmp(name->valuestring, tool_name) == 0)) {
      return 1;
    }
  }
  return 0;
}

static int harness_responses_tool_has_engine(cJSON *tools,
                                             const char *tool_name,
                                             const char *engine_name)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (tool_name == NULL) ||
      (engine_name == NULL)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *type;
    cJSON *parameters;
    cJSON *engine;

    type = cJSON_GetObjectItemCaseSensitive(tool, "type");
    if (!cJSON_IsString(type) || (type->valuestring == NULL) ||
        (strcmp(type->valuestring, tool_name) != 0)) {
      continue;
    }
    parameters = cJSON_GetObjectItemCaseSensitive(tool, "parameters");
    engine = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "engine") : NULL;
    return cJSON_IsString(engine) && (engine->valuestring != NULL) &&
      (strcmp(engine->valuestring, engine_name) == 0);
  }
  return 0;
}

static int harness_coding_preflight_bash_arguments_are_valid(
  const char *arguments_json)
{
  static const char *required_fragments[] = {
    "uname -a",
    "command -v id",
    "UID=%s",
    "PWD=%s",
    "PATH=%s",
    "=== Relevant commands ===",
    ("for command_name in clang make git altivec-app altivec-sdk "
     "altivec-lib ldid zip xcrun xcodebuild"),
    "command -v \"$command_name\"",
    "/var/altivec/share/altivec/make/ios-app.mk",
    "command -v altivec-sdk",
    "altivec-sdk list",
    "altivec-sdk list exited",
    "command -v altivec-lib",
    "altivec-lib list",
    "altivec-lib list exited"
  };
  static const char *forbidden_fragments[] = {
    "SystemVersion.plist",
    "dpkg --print-architecture",
    "HOME=$HOME",
    "SHELL=$SHELL",
    "df -h .",
    "/usr/include",
    "PATH directory contents",
    "path_listing_budget",
    "COLUMNS=160",
    "report_version()",
    "clang --version",
    "gcc --version",
    "cc --version",
    "make --version",
    "git --version",
    "curl --version",
    "openssl version",
    "sqlite3 --version",
    "magick -version",
    "=== Working directory ===",
    "ls -al",
    "which clang",
    "which gcc",
    "report_group",
    "dpkg-query -W",
    "com.altivecintelligence.toolchain",
    "altivec-app new",
    "altivec-sdk install",
    "altivec-sdk select",
    "altivec-sdk verify",
    "altivec-lib install",
    "altivec-lib update",
    "altivec-lib select",
    "altivec-lib verify"
  };
  cJSON *root;
  cJSON *command;
  size_t index;
  int ok;

  root = cJSON_Parse((arguments_json != NULL) ? arguments_json : "");
  command = cJSON_IsObject(root) ?
    cJSON_GetObjectItemCaseSensitive(root, "command") : NULL;
  ok = cJSON_IsString(command) && (command->valuestring != NULL) &&
    (root->child == command) && (command->next == NULL) &&
    (strstr(command->valuestring, "python") == NULL);
  for (index = 0U;
       ok && (index < (sizeof(required_fragments) /
                       sizeof(required_fragments[0])));
       index++) {
    ok = (strstr(command->valuestring, required_fragments[index]) != NULL);
  }
  for (index = 0U;
       ok && (index < (sizeof(forbidden_fragments) /
                       sizeof(forbidden_fragments[0])));
       index++) {
    ok = (strstr(command->valuestring, forbidden_fragments[index]) == NULL);
  }
  cJSON_Delete(root);
  return ok;
}

static int harness_run_assistant_set_tests(void)
{
  strappy_assistant_set_record_list list;
  strappy_assistant_set_profile world;
  strappy_assistant_set_profile personal;
  strappy_assistant_set_profile coding;
  strappy_assistant_set_profile study;
  strappy_assistant_set_profile invalid;
  char *world_tools_json;
  char *world_paid_tools_json;
  char *world_tools_without_web_json;
  char *coding_tools_json;
  cJSON *world_tools;
  cJSON *world_paid_tools;
  cJSON *world_tools_without_web;
  cJSON *coding_tools;
  char *error;
  int ok;

  strappy_assistant_set_record_list_init(&list);
  strappy_assistant_set_profile_init(&world);
  strappy_assistant_set_profile_init(&personal);
  strappy_assistant_set_profile_init(&coding);
  strappy_assistant_set_profile_init(&study);
  strappy_assistant_set_profile_init(&invalid);
  world_tools_json = NULL;
  world_paid_tools_json = NULL;
  world_tools_without_web_json = NULL;
  coding_tools_json = NULL;
  world_tools = NULL;
  world_paid_tools = NULL;
  world_tools_without_web = NULL;
  coding_tools = NULL;
  error = NULL;

  ok = strappy_assistant_sets_list(HARNESS_RESOURCE_DIR, &list, &error) &&
    (list.count == 3U) &&
    (strcmp(list.records[0].identifier,
            STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE) == 0) &&
    (strcmp(list.records[1].identifier,
            STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT) == 0) &&
    (strcmp(list.records[2].identifier,
            STRAPPY_ASSISTANT_SET_CODING_ASSISTANT) == 0) &&
    (strcmp(list.records[2].availability,
            STRAPPY_ASSISTANT_SET_AVAILABILITY_AVAILABLE) == 0) &&
    strappy_assistant_sets_load_profile(
      HARNESS_RESOURCE_DIR,
      STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE,
      &world,
      &error) &&
    strappy_assistant_set_profile_is_available(&world) &&
    (world.tool_name_count == 12U) &&
    (world.skill_identifier_count == 0U) &&
    (world.preflight_call_count == 2U) &&
    (strcmp(world.preflight_when,
            STRAPPY_ASSISTANT_SET_PREFLIGHT_FIRST_USER_PROMPT) == 0) &&
    (strcmp(world.preflight_assistant_text,
            "Strappy here, babe. Facts, line up. Let me check my memories and "
            "tools before I put the world in order.") == 0) &&
    (strcmp(world.preflight_calls[0].tool_name,
            STRAPPY_TOOL_MEMORY_READ) == 0) &&
    (strcmp(world.preflight_calls[0].arguments_json, "{}") == 0) &&
    (strcmp(world.preflight_calls[1].tool_name,
            STRAPPY_TOOL_SKILLS_LIST) == 0) &&
    (strcmp(world.preflight_calls[1].arguments_json, "{}") == 0) &&
    (world.quality_check_key_count == 5U) &&
    strappy_assistant_set_profile_allows_tool(
      &world,
      STRAPPY_TOOL_MEMORY_READ) &&
    strappy_assistant_set_profile_allows_tool(
      &world,
      STRAPPY_TOOL_MEMORY_SAVE) &&
    strappy_assistant_set_profile_allows_tool(
      &world,
      STRAPPY_TOOL_SKILLS_LIST) &&
    strappy_assistant_set_profile_allows_tool(
      &world,
      STRAPPY_TOOL_SKILL_READ) &&
    !strappy_assistant_set_profile_has_quality_check(
      &world,
      STRAPPY_TOOL_MEMORY_SAVE) &&
    strappy_assistant_set_profile_allows_tool(
      &world,
      STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
    !strappy_assistant_set_profile_allows_tool(
      &world,
      STRAPPY_TOOL_FILE_READ) &&
    !strappy_assistant_set_profile_allows_tool(
      &world,
      STRAPPY_TOOL_FILE_WRITE) &&
    !strappy_assistant_set_profile_allows_tool(
      &world,
      STRAPPY_TOOL_FILE_EDIT) &&
    !strappy_assistant_set_profile_allows_tool(
      &world,
      STRAPPY_TOOL_BASH) &&
    !strappy_assistant_set_profile_allows_tool(
      &world,
      STRAPPY_TOOL_DATABASE_LIST) &&
    !strappy_assistant_set_profile_has_quality_check(
      &world,
      STRAPPY_TOOL_DATABASE_CONTEXT) &&
    strappy_assistant_sets_load_profile(
      HARNESS_RESOURCE_DIR,
      STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT,
      &personal,
      &error) &&
    strappy_assistant_set_profile_is_available(&personal) &&
    (personal.tool_name_count == 15U) &&
    (personal.skill_identifier_count == 0U) &&
    (personal.preflight_call_count == 3U) &&
    (strcmp(personal.preflight_when,
            STRAPPY_ASSISTANT_SET_PREFLIGHT_FIRST_USER_PROMPT) == 0) &&
    (strcmp(personal.preflight_assistant_text,
            "Strappy here, babe. Databases, behave. Let me inspect my memories, "
            "skills, and approved data before I interrogate the evidence.") ==
     0) &&
    (strcmp(personal.preflight_calls[0].tool_name,
            STRAPPY_TOOL_MEMORY_READ) == 0) &&
    (strcmp(personal.preflight_calls[0].arguments_json, "{}") == 0) &&
    (strcmp(personal.preflight_calls[1].tool_name,
            STRAPPY_TOOL_SKILLS_LIST) == 0) &&
    (strcmp(personal.preflight_calls[1].arguments_json, "{}") == 0) &&
    (strcmp(personal.preflight_calls[2].tool_name,
            STRAPPY_TOOL_DATABASE_LIST) == 0) &&
    (strcmp(personal.preflight_calls[2].arguments_json, "{}") == 0) &&
    (personal.quality_check_key_count == 6U) &&
    strappy_assistant_set_profile_allows_tool(
      &personal,
      STRAPPY_TOOL_DATABASE_QUERY) &&
    !strappy_assistant_set_profile_allows_tool(
      &personal,
      STRAPPY_TOOL_DATABASE_STUDY) &&
    !strappy_assistant_set_profile_allows_tool(
      &personal,
      STRAPPY_TOOL_FILE_READ) &&
    !strappy_assistant_set_profile_allows_tool(
      &personal,
      STRAPPY_TOOL_FILE_WRITE) &&
    !strappy_assistant_set_profile_allows_tool(
      &personal,
      STRAPPY_TOOL_FILE_EDIT) &&
    !strappy_assistant_set_profile_allows_tool(
      &personal,
      STRAPPY_TOOL_BASH) &&
    !strappy_assistant_set_profile_has_quality_check(
      &personal,
      STRAPPY_TOOL_DATABASE_STUDY) &&
    strappy_assistant_sets_load_profile(
      HARNESS_RESOURCE_DIR,
      STRAPPY_ASSISTANT_SET_DATABASE_STUDY,
      &study,
      &error) &&
    strappy_assistant_set_profile_is_available(&study) &&
    (strcmp(study.display_name,
            STRAPPY_ASSISTANT_SET_DATABASE_STUDY_SESSION_NAME) == 0) &&
    (study.tool_name_count == 6U) &&
    (study.skill_identifier_count == 0U) &&
    (study.preflight_call_count == 1U) &&
    (study.quality_check_key_count == 2U) &&
    (strcmp(study.preflight_calls[0].tool_name,
            STRAPPY_TOOL_DATABASE_LIST) == 0) &&
    strappy_assistant_set_profile_allows_tool(
      &study,
      STRAPPY_TOOL_DATABASE_LIST) &&
    strappy_assistant_set_profile_allows_tool(
      &study,
      STRAPPY_TOOL_DATABASE_QUERY) &&
    strappy_assistant_set_profile_allows_tool(
      &study,
      STRAPPY_TOOL_DATABASE_CONTEXT) &&
    strappy_assistant_set_profile_allows_tool(
      &study,
      STRAPPY_TOOL_DATABASE_STUDY) &&
    strappy_assistant_set_profile_allows_tool(
      &study,
      STRAPPY_TOOL_DATETIME_TO_ISO8601) &&
    strappy_assistant_set_profile_allows_tool(
      &study,
      STRAPPY_TOOL_DATETIME_FROM_ISO8601) &&
    !strappy_assistant_set_profile_allows_tool(
      &study,
      STRAPPY_TOOL_MEMORY_READ) &&
    !strappy_assistant_set_profile_allows_tool(
      &study,
      STRAPPY_TOOL_SKILLS_LIST) &&
    !strappy_assistant_set_profile_allows_tool(
      &study,
      STRAPPY_TOOL_SKILL_READ) &&
    !strappy_assistant_set_profile_allows_tool(
      &study,
      STRAPPY_TOOL_SESSION_RENAME) &&
    !strappy_assistant_set_profile_allows_tool(
      &study,
      STRAPPY_TOOL_FONTAWESOME_CONFIRM) &&
    !strappy_assistant_set_profile_allows_tool(
      &study,
      STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
    strappy_assistant_set_profile_has_quality_check(
      &study,
      "answer_non_empty") &&
    !strappy_assistant_set_profile_has_quality_check(
      &study,
      "unicode_emoji_absent") &&
    strappy_assistant_set_profile_has_quality_check(
      &study,
      "database_context") &&
    !strappy_assistant_set_profile_has_quality_check(
      &study,
      "web_reference") &&
    !strappy_assistant_set_profile_has_quality_check(
      &study,
      "session_rename") &&
    !strappy_assistant_set_profile_has_quality_check(
      &study,
      "fontawesome_confirm") &&
    strappy_assistant_sets_load_profile(
      HARNESS_RESOURCE_DIR,
      STRAPPY_ASSISTANT_SET_CODING_ASSISTANT,
      &coding,
      &error) &&
    strappy_assistant_set_profile_is_available(&coding) &&
    (coding.tool_name_count == 16U) &&
    (coding.skill_identifier_count == 2U) &&
    strappy_assistant_set_profile_allows_skill(
      &coding,
      "ios-development") &&
    strappy_assistant_set_profile_allows_skill(
      &coding,
      "macos-development") &&
    strappy_skills_validate_allowed(
      HARNESS_RESOURCE_DIR,
      (const char * const *)coding.skill_identifiers,
      coding.skill_identifier_count,
      &error) &&
    (coding.preflight_call_count == 3U) &&
    (strcmp(coding.preflight_when,
            STRAPPY_ASSISTANT_SET_PREFLIGHT_FIRST_USER_PROMPT) == 0) &&
    (strcmp(coding.preflight_assistant_text,
            "Strappy here, babe. Code, heel. Let me inspect the environment, "
            "review my tools, and find out exactly what needs discipline.") ==
     0) &&
    (strcmp(coding.preflight_calls[0].tool_name,
            STRAPPY_TOOL_MEMORY_READ) == 0) &&
    (strcmp(coding.preflight_calls[0].arguments_json, "{}") == 0) &&
    (strcmp(coding.preflight_calls[1].tool_name,
            STRAPPY_TOOL_SKILLS_LIST) == 0) &&
    (strcmp(coding.preflight_calls[1].arguments_json, "{}") == 0) &&
    (strcmp(coding.preflight_calls[2].tool_name,
            STRAPPY_TOOL_BASH) == 0) &&
    harness_coding_preflight_bash_arguments_are_valid(
      coding.preflight_calls[2].arguments_json) &&
    strappy_assistant_set_profile_allows_tool(
      &coding,
      STRAPPY_TOOL_FILE_READ) &&
    strappy_assistant_set_profile_allows_tool(
      &coding,
      STRAPPY_TOOL_FILE_WRITE) &&
    strappy_assistant_set_profile_allows_tool(
      &coding,
      STRAPPY_TOOL_FILE_EDIT) &&
    strappy_assistant_set_profile_allows_tool(
      &coding,
      STRAPPY_TOOL_BASH);

  if (ok) {
    world_tools_json = strappy_tools_responses_request_json_filtered(
      HARNESS_RESOURCE_DIR,
      (const char * const *)world.tool_names,
      world.tool_name_count,
      STRAPPY_WEB_PROVIDER_NATIVE,
      &error);
    world_paid_tools_json =
      strappy_tools_responses_request_json_filtered(
        HARNESS_RESOURCE_DIR,
        (const char * const *)world.tool_names,
        world.tool_name_count,
        STRAPPY_WEB_PROVIDER_EXA,
        &error);
    world_tools_without_web_json =
      strappy_tools_responses_request_json_filtered(
        HARNESS_RESOURCE_DIR,
        (const char * const *)world.tool_names,
        world.tool_name_count,
        STRAPPY_WEB_PROVIDER_NONE,
        &error);
    coding_tools_json = strappy_tools_responses_request_json_filtered(
      HARNESS_RESOURCE_DIR,
      (const char * const *)coding.tool_names,
      coding.tool_name_count,
      STRAPPY_WEB_PROVIDER_PARALLEL,
      &error);
    world_tools = (world_tools_json != NULL) ?
      cJSON_Parse(world_tools_json) : NULL;
    world_paid_tools = (world_paid_tools_json != NULL) ?
      cJSON_Parse(world_paid_tools_json) : NULL;
    world_tools_without_web = (world_tools_without_web_json != NULL) ?
      cJSON_Parse(world_tools_without_web_json) : NULL;
    coding_tools = (coding_tools_json != NULL) ?
      cJSON_Parse(coding_tools_json) : NULL;
    ok = cJSON_IsArray(world_tools) &&
      (cJSON_GetArraySize(world_tools) == 12) &&
      harness_responses_tools_contains(
        world_tools,
        STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
      harness_responses_tools_contains(
        world_tools,
        STRAPPY_TOOL_OPENROUTER_WEB_FETCH) &&
      harness_responses_tool_has_engine(
        world_tools,
        STRAPPY_TOOL_OPENROUTER_WEB_SEARCH,
        "native") &&
      harness_responses_tool_has_engine(
        world_tools,
        STRAPPY_TOOL_OPENROUTER_WEB_FETCH,
        "native") &&
      harness_responses_tools_contains(
        world_tools,
        STRAPPY_TOOL_DATETIME_TO_ISO8601) &&
      harness_responses_tools_contains(
        world_tools,
        STRAPPY_TOOL_SKILLS_LIST) &&
      harness_responses_tools_contains(
        world_tools,
        STRAPPY_TOOL_SKILL_READ) &&
      !harness_responses_tools_contains(
        world_tools,
        STRAPPY_TOOL_FILE_READ) &&
      !harness_responses_tools_contains(
        world_tools,
        STRAPPY_TOOL_FILE_WRITE) &&
      !harness_responses_tools_contains(
        world_tools,
        STRAPPY_TOOL_FILE_EDIT) &&
      !harness_responses_tools_contains(
        world_tools,
        STRAPPY_TOOL_DATABASE_LIST) &&
      !harness_responses_tools_contains(
        world_tools,
        STRAPPY_TOOL_DATABASE_QUERY) &&
      cJSON_IsArray(world_paid_tools) &&
      (cJSON_GetArraySize(world_paid_tools) == 12) &&
      harness_responses_tools_contains(
        world_paid_tools,
        STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
      harness_responses_tools_contains(
        world_paid_tools,
        STRAPPY_TOOL_OPENROUTER_WEB_FETCH) &&
      harness_responses_tool_has_engine(
        world_paid_tools,
        STRAPPY_TOOL_OPENROUTER_WEB_SEARCH,
        "exa") &&
      harness_responses_tool_has_engine(
        world_paid_tools,
        STRAPPY_TOOL_OPENROUTER_WEB_FETCH,
        "exa") &&
      cJSON_IsArray(world_tools_without_web) &&
      (cJSON_GetArraySize(world_tools_without_web) == 10) &&
      !harness_responses_tools_contains(
        world_tools_without_web,
        STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
      !harness_responses_tools_contains(
        world_tools_without_web,
        STRAPPY_TOOL_OPENROUTER_WEB_FETCH) &&
      cJSON_IsArray(coding_tools) &&
      (cJSON_GetArraySize(coding_tools) == 16) &&
      harness_responses_tools_contains(coding_tools,
                                       STRAPPY_TOOL_FILE_READ) &&
      harness_responses_tools_contains(coding_tools,
                                       STRAPPY_TOOL_FILE_WRITE) &&
      harness_responses_tools_contains(coding_tools,
                                       STRAPPY_TOOL_FILE_EDIT) &&
      harness_responses_tools_contains(coding_tools,
                                       STRAPPY_TOOL_BASH) &&
      harness_responses_tool_has_engine(
        coding_tools,
        STRAPPY_TOOL_OPENROUTER_WEB_SEARCH,
        "parallel") &&
      harness_responses_tool_has_engine(
        coding_tools,
        STRAPPY_TOOL_OPENROUTER_WEB_FETCH,
        "parallel");
  }
  if (ok && strappy_assistant_sets_load_profile(HARNESS_RESOURCE_DIR,
                                                "missing_set",
                                                &invalid,
                                                &error)) {
    ok = 0;
  }
  if (ok && (error == NULL)) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Assistant-set resources did not match expectations: %s\n",
            (error != NULL) ? error : "unknown");
  }

  free(error);
  free(world_tools_json);
  free(world_paid_tools_json);
  free(world_tools_without_web_json);
  free(coding_tools_json);
  cJSON_Delete(world_tools);
  cJSON_Delete(world_paid_tools);
  cJSON_Delete(world_tools_without_web);
  cJSON_Delete(coding_tools);
  strappy_assistant_set_profile_destroy(&invalid);
  strappy_assistant_set_profile_destroy(&study);
  strappy_assistant_set_profile_destroy(&coding);
  strappy_assistant_set_profile_destroy(&personal);
  strappy_assistant_set_profile_destroy(&world);
  strappy_assistant_set_record_list_destroy(&list);
  return ok;
}

static int harness_write_skill_limit_fixture(const char *path,
                                              size_t instruction_bytes)
{
  static const char prefix[] =
    "{\"schema_version\":1,\"skills\":[{\"id\":\"limit-test\","
    "\"title\":\"Limit Test\",\"description\":\"Boundary fixture.\","
    "\"instructions\":\"";
  static const char suffix[] = "\"}]}";
  FILE *file;
  char *instructions;
  int ok;

  if ((path == NULL) || (path[0] == '\0') || (instruction_bytes == 0U)) {
    return 0;
  }
  instructions = (char *)malloc(instruction_bytes);
  if (instructions == NULL) {
    return 0;
  }
  memset(instructions, 'x', instruction_bytes);
  file = fopen(path, "wb");
  if (file == NULL) {
    free(instructions);
    return 0;
  }
  ok = (fwrite(prefix, 1U, sizeof(prefix) - 1U, file) ==
        (sizeof(prefix) - 1U)) &&
    (fwrite(instructions, 1U, instruction_bytes, file) ==
     instruction_bytes) &&
    (fwrite(suffix, 1U, sizeof(suffix) - 1U, file) ==
     (sizeof(suffix) - 1U));
  if (fclose(file) != 0) {
    ok = 0;
  }
  free(instructions);
  return ok;
}

static int harness_run_skills_tests(const harness_context *context)
{
  static const char *const allowed_skills[] = { "concise-writing" };
  static const char *const limit_skill[] = { "limit-test" };
  static const char *const missing_skills[] = { "missing-skill" };
  char catalog_path[1200];
  char limit_resource_path[1200];
  strappy_assistant_set_profile profile;
  strappy_skill_record_list list;
  strappy_skill_record record;
  char *error;
  char *output;
  cJSON *root;
  cJSON *skills;
  cJSON *item;
  long long session_id;
  int ok;

  if ((context == NULL) ||
      !harness_join_path(catalog_path,
                         sizeof(catalog_path),
                         context->temp_dir,
                         "skills.sqlite") ||
      !harness_join_path(limit_resource_path,
                         sizeof(limit_resource_path),
                         context->temp_dir,
                         STRAPPY_SKILLS_RESOURCE_NAME)) {
    return 0;
  }
  error = NULL;
  output = NULL;
  root = NULL;
  session_id = 0LL;
  strappy_assistant_set_profile_init(&profile);
  strappy_skill_record_list_init(&list);
  strappy_skill_record_init(&record);

  ok = strappy_assistant_sets_load_profile(
         HARNESS_SKILLS_FIXTURE_RESOURCE_DIR,
         STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT,
         &profile,
         &error) &&
    strappy_assistant_set_profile_allows_skill(&profile,
                                               "concise-writing") &&
    !strappy_assistant_set_profile_allows_skill(&profile,
                                                "private-notes") &&
    strappy_skills_list_allowed(
      HARNESS_SKILLS_FIXTURE_RESOURCE_DIR,
      allowed_skills,
      sizeof(allowed_skills) / sizeof(allowed_skills[0]),
      &list,
      &error) &&
    (list.count == 1U) &&
    (strcmp(list.records[0].identifier, "concise-writing") == 0) &&
    (strcmp(list.records[0].title, "Concise Writing") == 0) &&
    (strcmp(list.records[0].description,
            "Make a written response direct and compact.") == 0) &&
    (strcmp(list.records[0].instructions,
            "Lead with the answer. Remove repetition and unnecessary "
            "qualifiers.") == 0);
  strappy_skill_record_list_destroy(&list);
  if (ok) {
    ok = strappy_skills_read_allowed(
      HARNESS_SKILLS_FIXTURE_RESOURCE_DIR,
      allowed_skills,
      sizeof(allowed_skills) / sizeof(allowed_skills[0]),
      "concise-writing",
      &record,
      &error) &&
      (strcmp(record.title, "Concise Writing") == 0) &&
      (strstr(record.instructions, "Lead with the answer.") != NULL);
  }
  strappy_skill_record_destroy(&record);
  if (ok) {
    free(error);
    error = NULL;
    ok = !strappy_skills_validate_allowed(
      HARNESS_SKILLS_FIXTURE_RESOURCE_DIR,
      missing_skills,
      sizeof(missing_skills) / sizeof(missing_skills[0]),
      &error) &&
      (error != NULL) &&
      (strstr(error, "Assistant-set skill is not registered") != NULL);
  }
  free(error);
  error = NULL;

  if (ok) {
    ok = harness_write_skill_limit_fixture(
           limit_resource_path,
           HARNESS_SKILL_INSTRUCTIONS_MAX_BYTES) &&
      strappy_skills_read_allowed(
        context->temp_dir,
        limit_skill,
        sizeof(limit_skill) / sizeof(limit_skill[0]),
        "limit-test",
        &record,
        &error) &&
      (strlen(record.instructions) ==
       HARNESS_SKILL_INSTRUCTIONS_MAX_BYTES);
  }
  strappy_skill_record_destroy(&record);
  if (ok) {
    free(error);
    error = NULL;
    ok = harness_write_skill_limit_fixture(
           limit_resource_path,
           HARNESS_SKILL_INSTRUCTIONS_MAX_BYTES + 1U) &&
      !strappy_skills_validate_allowed(
        context->temp_dir,
        limit_skill,
        sizeof(limit_skill) / sizeof(limit_skill[0]),
        &error) &&
      (error != NULL) &&
      (strstr(error, "65536-byte limit") != NULL);
    if (ok) {
      free(error);
      error = NULL;
    }
  }
  unlink(limit_resource_path);

  if (ok) {
    ok = strappy_db_initialize(catalog_path, &error) &&
      strappy_db_create_session(catalog_path, &session_id, &error);
  }
  if (ok) {
    output = strappy_tools_execute(catalog_path,
                                   session_id,
                                   HARNESS_SKILLS_FIXTURE_RESOURCE_DIR,
                                   STRAPPY_TOOL_SKILLS_LIST,
                                   "{}",
                                   &error);
    root = (output != NULL) ? cJSON_Parse(output) : NULL;
    skills = cJSON_IsObject(root) ?
      cJSON_GetObjectItemCaseSensitive(root, "skills") : NULL;
    item = cJSON_IsArray(skills) ? cJSON_GetArrayItem(skills, 0) : NULL;
    ok = cJSON_IsObject(root) && (cJSON_GetArraySize(root) == 1) &&
      cJSON_IsArray(skills) && (cJSON_GetArraySize(skills) == 1) &&
      cJSON_IsObject(item) && (cJSON_GetArraySize(item) == 3) &&
      cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "skill_id")) &&
      (strcmp(cJSON_GetObjectItemCaseSensitive(item,
                                               "skill_id")->valuestring,
              "concise-writing") == 0) &&
      cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "title")) &&
      cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item,
                                                      "description")) &&
      (cJSON_GetObjectItemCaseSensitive(item, "instructions") == NULL);
    cJSON_Delete(root);
    root = NULL;
    free(output);
    output = NULL;
  }
  if (ok) {
    output = strappy_tools_execute(
      catalog_path,
      session_id,
      HARNESS_SKILLS_FIXTURE_RESOURCE_DIR,
      STRAPPY_TOOL_SKILL_READ,
      "{\"skill_id\":\"concise-writing\"}",
      &error);
    root = (output != NULL) ? cJSON_Parse(output) : NULL;
    ok = cJSON_IsObject(root) && (cJSON_GetArraySize(root) == 3) &&
      cJSON_IsString(cJSON_GetObjectItemCaseSensitive(root, "skill_id")) &&
      (strcmp(cJSON_GetObjectItemCaseSensitive(root,
                                               "skill_id")->valuestring,
              "concise-writing") == 0) &&
      cJSON_IsString(cJSON_GetObjectItemCaseSensitive(root, "title")) &&
      cJSON_IsString(cJSON_GetObjectItemCaseSensitive(root,
                                                      "instructions")) &&
      (cJSON_GetObjectItemCaseSensitive(root, "description") == NULL);
    cJSON_Delete(root);
    root = NULL;
    free(output);
    output = NULL;
  }
  if (ok) {
    free(error);
    error = NULL;
    output = strappy_tools_execute(
      catalog_path,
      session_id,
      HARNESS_SKILLS_FIXTURE_RESOURCE_DIR,
      STRAPPY_TOOL_SKILL_READ,
      "{\"skill_id\":\"private-notes\"}",
      &error);
    ok = (output == NULL) && (error != NULL) &&
      (strstr(error, "not allowed by the assistant set") != NULL);
  }
  if (ok) {
    free(error);
    error = NULL;
    output = strappy_tools_execute(
      catalog_path,
      session_id,
      HARNESS_SKILLS_FIXTURE_RESOURCE_DIR,
      STRAPPY_TOOL_SKILL_READ,
      "{\"skill_id\":\"Not Valid\"}",
      &error);
    ok = (output == NULL) && (error != NULL) &&
      (strstr(error, "skill_id is invalid") != NULL);
  }
  if (ok) {
    free(error);
    error = NULL;
    output = strappy_tools_execute(catalog_path,
                                   session_id,
                                   HARNESS_RESOURCE_DIR,
                                   STRAPPY_TOOL_SKILLS_LIST,
                                   "{}",
                                   &error);
    ok = (output != NULL) &&
      (strcmp(output,
              "{\"skills\":[],\"guidance\":\"No skills available\"}") ==
       0);
  }
  if (!ok) {
    fprintf(stderr,
            "Skills catalog/tool tests failed: %s\n",
            (error != NULL) ? error : "unexpected result shape");
  }

  cJSON_Delete(root);
  free(output);
  free(error);
  strappy_skill_record_destroy(&record);
  strappy_skill_record_list_destroy(&list);
  strappy_assistant_set_profile_destroy(&profile);
  harness_unlink_sqlite_files(catalog_path);
  return ok;
}

static int harness_run_helper_datetime_tests(void)
{
  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_DATETIME_TO_ISO8601,
        "{\"timestamps\":[\"0\",\"1\",\"-1\",\"-0.5\"],"
        "\"unit\":\"unix_seconds\"}",
        "[\"1970-01-01T00:00:00Z\",\"1970-01-01T00:00:01Z\","
        "\"1969-12-31T23:59:59Z\",\"1969-12-31T23:59:59.5Z\"]")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_DATETIME_TO_ISO8601,
        "{\"timestamps\":[\"1700000000123\"],"
        "\"unit\":\"unix_milliseconds\"}",
        "[\"2023-11-14T22:13:20.123Z\"]")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_DATETIME_TO_ISO8601,
        "{\"timestamps\":[\"0\",\" 1.25 \"],"
        "\"unit\":\"apple_seconds\"}",
        "[\"2001-01-01T00:00:00Z\",\"2001-01-01T00:00:01.25Z\"]")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_DATETIME_TO_ISO8601,
        "{\"timestamps\":[\"1\"]}",
        "requires a non-empty unit string")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_DATETIME_TO_ISO8601,
        "{\"timestamps\":\"1,2\",\"unit\":\"unix_seconds\"}",
        "must be an array of strings")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_DATETIME_TO_ISO8601,
        "{\"timestamps\":[],\"unit\":\"unix_seconds\"}",
        "must contain between 1 and 256 items")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_DATETIME_TO_ISO8601,
        "{\"timestamps\":[1],\"unit\":\"unix_seconds\"}",
        "items must be strings")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_DATETIME_TO_ISO8601,
        "{\"timestamps\":[\"1\",\" \"],\"unit\":\"unix_seconds\"}",
        "empty item")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_DATETIME_TO_ISO8601,
        "{\"timestamps\":[\"1\"],\"unit\":\"banana_seconds\"}",
        "unit is not supported")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_DATETIME_FROM_ISO8601,
        "{\"datetimes\":[\"1970-01-01T00:00:00Z\","
        "\"1970-01-01T00:00:01Z\"],"
        "\"unit\":\"unix_seconds\"}",
        "[\"0\",\"1\"]")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_DATETIME_FROM_ISO8601,
        "{\"datetimes\":[\"2001-01-01T00:00:00Z\","
        "\"2026-01-01T00:00:00Z\",\"2027-01-01T00:00:00Z\"],"
        "\"unit\":\"apple_seconds\"}",
        "[\"0\",\"788918400\",\"820454400\"]")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_DATETIME_FROM_ISO8601,
        "{\"datetimes\":[\"2026-01-01\",\"2027-01-01\"],"
        "\"unit\":\"apple_seconds\"}",
        "[\"788918400\",\"820454400\"]")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_DATETIME_FROM_ISO8601,
        "{\"datetimes\":[\"1969-12-31T23:59:59.5Z\"],"
        "\"unit\":\"unix_seconds\"}",
        "[\"-0.5\"]")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_DATETIME_FROM_ISO8601,
        "{\"datetimes\":[\"2026-01-01T05:00:00+05:00\"],"
        "\"unit\":\"apple_seconds\"}",
        "[\"788918400\"]")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_DATETIME_FROM_ISO8601,
        "{\"datetimes\":[\"2026-01-01\"]}",
        "requires a non-empty unit string")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_DATETIME_FROM_ISO8601,
        "{\"datetimes\":[\"2026-01-01\",\"\",\"2027-01-01\"],"
        "\"unit\":\"apple_seconds\"}",
        "empty item")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_DATETIME_FROM_ISO8601,
        "{\"datetimes\":[\"2026-02-30\"],"
        "\"unit\":\"apple_seconds\"}",
        "date is invalid")) {
    return 0;
  }

  return 1;
}

static int harness_expect_fontawesome_search_output(
  const char *arguments_json,
  const char *expected_first_shortcode)
{
  cJSON *root;
  cJSON *item;
  int count;
  int ok;

  root = harness_tool_output_json(
    NULL,
    STRAPPY_TOOL_FONTAWESOME_SEARCH,
    arguments_json);
  count = cJSON_IsArray(root) ? cJSON_GetArraySize(root) : 0;
  item = cJSON_GetArrayItem(root, 0);
  ok = cJSON_IsArray(root) && (count > 0) && (count <= 20) &&
    cJSON_IsString(item) && (item->valuestring != NULL) &&
    (strcmp(item->valuestring, expected_first_shortcode) == 0);
  for (item = cJSON_IsArray(root) ? root->child : NULL;
       ok && (item != NULL);
       item = item->next) {
    size_t length;

    if (!cJSON_IsString(item) || (item->valuestring == NULL)) {
      ok = 0;
      break;
    }
    length = strlen(item->valuestring);
    if ((length < 6U) || (strncmp(item->valuestring, "[fa:", 4U) != 0) ||
        (item->valuestring[length - 1U] != ']')) {
      ok = 0;
    }
  }
  if (!ok) {
    fprintf(stderr,
            "Font Awesome search did not return a ranked shortcode array.\n");
  }
  cJSON_Delete(root);
  return ok;
}

static int harness_run_helper_fontawesome_tests(void)
{
  if (!harness_expect_fontawesome_search_output(
        "{\"query\":\"warning\"}",
        "[fa:triangle-exclamation]") ||
      !harness_expect_fontawesome_search_output(
        "{\"query\":\"github\"}",
        "[fa:brands:github]") ||
      !harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_SEARCH,
        "{\"query\":\"strappy-no-such-icon-query\"}",
        "[]")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_SEARCH,
        "{\"query\":\"github\",\"style\":\"brands\"}",
        "does not accept argument 'style'")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_SEARCH,
        "{\"query\":\"warning\",\"limit\":5}",
        "does not accept argument 'limit'")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_SEARCH,
        "{}",
        "requires a non-empty query string") ||
      !harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_SEARCH,
        "{\"query\":\" \\t \\n\"}",
        "query must not be blank")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_CONFIRM,
        "{\"shortcodes\":[\"[fa:heart]\",\"database\",\"[fa:not-real]\"]}",
        "{\"confirmed\":[\"[fa:heart]\"],"
        "\"not_found\":[\"database\",\"[fa:not-real]\"]}")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_CONFIRM,
        "{\"shortcodes\":[\"[fa:brands:github]\",\"[fa:regular:heart]\"]}",
        "{\"confirmed\":[\"[fa:brands:github]\","
        "\"[fa:regular:heart]\"],\"not_found\":[]}")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_CONFIRM,
        "{\"shortcodes\":[\"heart\",\"fa:heart\","
        "\"[FA:heart]\",\" [fa:heart] \"]}",
        "{\"confirmed\":[],\"not_found\":[\"heart\",\"fa:heart\","
        "\"[FA:heart]\",\" [fa:heart] \"]}")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_CONFIRM,
        NULL,
        "requires a shortcodes array") ||
      !harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_CONFIRM,
        "null",
        "must be a JSON object") ||
      !harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_CONFIRM,
        "{}",
        "requires a shortcodes array") ||
      !harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_CONFIRM,
        "{\"shortcodes\":null}",
        "requires a shortcodes array") ||
      !harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_CONFIRM,
        "{\"shortcodes\":[]}",
        "shortcodes must contain 1 to") ||
      !harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_FONTAWESOME_CONFIRM,
        "{\"shortcodes\":\"[fa:heart]\"}",
        "requires a shortcodes array")) {
    return 0;
  }

  return 1;
}

static int harness_register_database_path(harness_context *context,
                                          const char *database_path,
                                          unsigned long long device,
                                          unsigned long long inode,
                                          char **database_id_out)
{
  strappy_discovered_database_input input;
  strappy_discovered_database_record_list list;
  char *error;
  size_t index;
  size_t match_index;
  int found;
  int ok;

  if ((context == NULL) || (database_path == NULL) ||
      (database_id_out == NULL)) {
    return 0;
  }

  error = NULL;
  if (!strappy_db_initialize(context->catalog_path, &error)) {
    fprintf(stderr,
            "Could not initialize catalog: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }

  memset(&input, 0, sizeof(input));
  input.path = database_path;
  input.size = 4096;
  input.modified_at = 1;
  input.device = device;
  input.inode = inode;
  input.is_valid_sqlite = 1;
  input.scan_root = context->temp_dir;
  input.app_name = "Harness App";

  if (!strappy_db_save_discovered_databases(context->catalog_path,
                                            &input,
                                            1U,
                                            &error)) {
    fprintf(stderr,
            "Could not save discovered database: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(context->catalog_path,
                                           &list,
                                           &error)) {
    fprintf(stderr,
            "Could not list discovered database: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    strappy_discovered_database_record_list_destroy(&list);
    return 0;
  }

  found = 0;
  match_index = 0U;
  for (index = 0U; index < list.count; index++) {
    if ((list.records[index].path != NULL) &&
        (strcmp(list.records[index].path, database_path) == 0)) {
      match_index = index;
      found = 1;
      break;
    }
  }

  if (!found) {
    fprintf(stderr, "Could not find registered database path.\n");
    strappy_discovered_database_record_list_destroy(&list);
    return 0;
  }

  free(*database_id_out);
  *database_id_out =
    strappy_string_duplicate(list.records[match_index].assistant_database_id);
  if (*database_id_out == NULL) {
    fprintf(stderr, "Could not copy assistant database id.\n");
    strappy_discovered_database_record_list_destroy(&list);
    return 0;
  }

  ok = strappy_db_update_discovered_database_decision(
    context->catalog_path,
    list.records[match_index].catalog_id,
    "allowed",
    &error);
  if (!ok) {
    fprintf(stderr,
            "Could not approve discovered database: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
  }

  strappy_discovered_database_record_list_destroy(&list);
  return ok;
}

static int harness_register_database(harness_context *context)
{
  if (context == NULL) {
    return 0;
  }

  return harness_register_database_path(context,
                                        context->database_path,
                                        1ULL,
                                        1ULL,
                                        &context->database_id);
}

static int harness_run_database_list_tests(const harness_context *context)
{
  static const char *const root_keys[] = { "databases" };
  cJSON *root;
  cJSON *databases;
  cJSON *database;
  cJSON *database_id;
  cJSON *description;
  cJSON *app_name;
  cJSON *path;
  cJSON *size_bytes;
  cJSON *modified_at;
  cJSON *property;
  char *error;
  char *output;
  int property_count;
  int ok;

  if (context == NULL) {
    return 0;
  }

  error = NULL;
  output = strappy_tools_execute(context->catalog_path,
                                 0LL,
                                 HARNESS_RESOURCE_DIR,
                                 STRAPPY_TOOL_DATABASE_LIST,
                                 "{}",
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Expected database_list output but got error: %s\n",
            (error != NULL) ? error : "(null)");
    free(error);
    return 0;
  }

  root = cJSON_Parse(output);
  databases = cJSON_IsObject(root) ?
    cJSON_GetObjectItemCaseSensitive(root, "databases") : NULL;
  database = cJSON_IsArray(databases) ?
    cJSON_GetArrayItem(databases, 0) : NULL;
  database_id = cJSON_IsObject(database) ?
    cJSON_GetObjectItem(database, "database_id") : NULL;
  description = cJSON_IsObject(database) ?
    cJSON_GetObjectItem(database, "description") : NULL;
  app_name = cJSON_IsObject(database) ?
    cJSON_GetObjectItem(database, "app_name") : NULL;
  path = cJSON_IsObject(database) ?
    cJSON_GetObjectItem(database, "path") : NULL;
  size_bytes = cJSON_IsObject(database) ?
    cJSON_GetObjectItem(database, "size_bytes") : NULL;
  modified_at = cJSON_IsObject(database) ?
    cJSON_GetObjectItem(database, "modified_at") : NULL;
  property_count = 0;
  if (cJSON_IsObject(database)) {
    for (property = database->child;
         property != NULL;
         property = property->next) {
      property_count++;
    }
  }
  ok = harness_object_has_exact_keys(
         root,
         root_keys,
         sizeof(root_keys) / sizeof(root_keys[0])) &&
       cJSON_IsArray(databases) &&
       (cJSON_GetArraySize(databases) == 1) &&
       cJSON_IsString(database_id) &&
       (database_id->valuestring != NULL) &&
       (strcmp(database_id->valuestring, context->database_id) == 0) &&
       cJSON_IsNull(description) &&
       cJSON_IsString(app_name) &&
       (app_name->valuestring != NULL) &&
       (strcmp(app_name->valuestring, "Harness App") == 0) &&
       cJSON_IsString(path) &&
       (path->valuestring != NULL) &&
       (strcmp(path->valuestring, context->database_path) == 0) &&
       cJSON_IsNumber(size_bytes) &&
       (size_bytes->valuedouble == 4096.0) &&
       cJSON_IsNumber(modified_at) &&
       (modified_at->valuedouble == 1.0) &&
       (property_count == 6);
  if (!ok) {
    fprintf(stderr, "database_list output was not expected: %s\n", output);
  }

  cJSON_Delete(root);
  free(output);
  return ok;
}

static int harness_run_empty_database_list_tests(
  const harness_context *context)
{
  static const char *const root_keys[] = { "databases", "guidance" };
  cJSON *root;
  cJSON *databases;
  cJSON *guidance;
  char *error;
  char *output;
  int ok;

  if (context == NULL) {
    return 0;
  }

  error = NULL;
  if (!strappy_db_initialize(context->catalog_path, &error)) {
    fprintf(stderr,
            "Could not initialize empty catalog: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }

  output = strappy_tools_execute(context->catalog_path,
                                 0LL,
                                 HARNESS_RESOURCE_DIR,
                                 STRAPPY_TOOL_DATABASE_LIST,
                                 "{}",
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Expected empty database_list output but got error: %s\n",
            (error != NULL) ? error : "(null)");
    free(error);
    return 0;
  }

  root = cJSON_Parse(output);
  databases = cJSON_IsObject(root) ?
    cJSON_GetObjectItemCaseSensitive(root, "databases") : NULL;
  guidance = cJSON_IsObject(root) ?
    cJSON_GetObjectItemCaseSensitive(root, "guidance") : NULL;
  ok = harness_object_has_exact_keys(
         root,
         root_keys,
         sizeof(root_keys) / sizeof(root_keys[0])) &&
    cJSON_IsArray(databases) && (cJSON_GetArraySize(databases) == 0) &&
    cJSON_IsString(guidance) && (guidance->valuestring != NULL) &&
    (strcmp(guidance->valuestring,
            HARNESS_DATABASE_LIST_EMPTY_GUIDANCE) == 0);
  if (!ok) {
    fprintf(stderr, "empty database_list output was not expected: %s\n", output);
  }

  cJSON_Delete(root);
  free(output);
  return ok;
}

typedef struct harness_scanner_batch_context {
  const char *catalog_path;
  const char *scan_root;
  long long scan_run_id;
  size_t callback_count;
} harness_scanner_batch_context;

static int harness_cancel_file_scanner(
  const strappy_file_scanner_progress *progress,
  void *user_data)
{
  (void)progress;
  (void)user_data;
  return 0;
}

static int harness_save_scanner_batch(strappy_file_scanner_record_list *list,
                                      void *user_data,
                                      char **error_out)
{
  harness_scanner_batch_context *context;
  int ok;

  context = (harness_scanner_batch_context *)user_data;
  if ((context == NULL) || (context->catalog_path == NULL) ||
      (list == NULL) || (list->scan_run_id <= 0LL)) {
    strappy_set_error(error_out, "Harness scanner batch context is missing.");
    return 0;
  }
  if (context->scan_run_id == 0LL) {
    context->scan_run_id = list->scan_run_id;
  } else if (context->scan_run_id != list->scan_run_id) {
    strappy_set_error(error_out, "Scanner batches did not share one scan run.");
    return 0;
  }

  ok = strappy_file_scanner_save_discovered_database_batch(
    context->catalog_path,
    list,
    context->scan_root,
    error_out);
  if (ok) {
    context->callback_count++;
  }
  return ok;
}

static int harness_run_discovered_database_replacement_tests(
  const harness_context *context)
{
  char catalog_path[1200];
  char first_path[1200];
  char second_path[1200];
  strappy_discovered_database_input inputs[2];
  strappy_discovered_database_record_list list;
  char *error;
  char *first_database_id;
  long long first_catalog_id;
  long long restored_catalog_id;
  strappy_file_scanner_record_list scanner_batch;
  size_t index;
  int found_first;
  int found_second;
  int ok;

  if (context == NULL) {
    return 0;
  }

  if (!harness_join_path(catalog_path,
                         sizeof(catalog_path),
                         context->temp_dir,
                         "replacement-catalog.sqlite") ||
      !harness_join_path(first_path,
                         sizeof(first_path),
                         context->temp_dir,
                         "first.sqlite") ||
      !harness_join_path(second_path,
                         sizeof(second_path),
                         context->temp_dir,
                         "second.sqlite")) {
    fprintf(stderr, "Could not build replacement test paths.\n");
    return 0;
  }
  harness_unlink_sqlite_files(catalog_path);

  memset(inputs, 0, sizeof(inputs));
  inputs[0].path = first_path;
  inputs[0].size = 100;
  inputs[0].modified_at = 1;
  inputs[0].device = 1ULL;
  inputs[0].inode = 10ULL;
  inputs[0].is_valid_sqlite = 1;
  inputs[0].scan_root = context->temp_dir;
  inputs[0].hidden = 1;
  inputs[1].path = second_path;
  inputs[1].size = 200;
  inputs[1].modified_at = 2;
  inputs[1].device = 1ULL;
  inputs[1].inode = 20ULL;
  inputs[1].is_valid_sqlite = 1;
  inputs[1].scan_root = context->temp_dir;
  inputs[1].hidden = 0;

  error = NULL;
  if (!strappy_db_replace_discovered_databases_for_scan_root(catalog_path,
                                                             inputs,
                                                             2U,
                                                             context->temp_dir,
                                                             &error)) {
    fprintf(stderr,
            "Could not save initial replacement scan: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  strappy_discovered_database_record_list_init(&list);
  strappy_file_scanner_record_list_init(&scanner_batch);
  if (!strappy_db_list_discovered_databases(catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list replacement scan: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  first_catalog_id = 0;
  first_database_id = NULL;
  for (index = 0U; index < list.count; index++) {
    if ((list.records[index].path != NULL) &&
        (strcmp(list.records[index].path, first_path) == 0)) {
      if (list.records[index].hidden != 1) {
        fprintf(stderr, "Initial replacement scan did not persist hidden state.\n");
        strappy_discovered_database_record_list_destroy(&list);
        harness_unlink_sqlite_files(catalog_path);
        return 0;
      }
      first_catalog_id = list.records[index].catalog_id;
      first_database_id =
        strappy_string_duplicate(list.records[index].assistant_database_id);
      break;
    }
  }
  strappy_discovered_database_record_list_destroy(&list);
  if ((first_catalog_id <= 0) || (first_database_id == NULL)) {
    fprintf(stderr, "Initial replacement scan did not preserve first row.\n");
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  if (!strappy_db_update_discovered_database_decision(catalog_path,
                                                      first_catalog_id,
                                                      "allowed",
                                                      &error)) {
    fprintf(stderr,
            "Could not approve replacement database: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }
  if (!strappy_db_update_discovered_database_hidden(catalog_path,
                                                    first_catalog_id,
                                                    1,
                                                    &error)) {
    fprintf(stderr,
            "Could not set replacement hidden override: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  if (!strappy_db_replace_discovered_databases_for_scan_root(catalog_path,
                                                             &inputs[1],
                                                             1U,
                                                             context->temp_dir,
                                                             &error)) {
    fprintf(stderr,
            "Could not replace scan with second row: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list after replacement delete: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  found_first = 0;
  found_second = 0;
  for (index = 0U; index < list.count; index++) {
    if ((list.records[index].path != NULL) &&
        (strcmp(list.records[index].path, first_path) == 0)) {
      found_first = 1;
    }
    if ((list.records[index].path != NULL) &&
        (strcmp(list.records[index].path, second_path) == 0)) {
      found_second = 1;
    }
  }
  strappy_discovered_database_record_list_destroy(&list);
  if (found_first || !found_second) {
    fprintf(stderr, "Replacement scan did not delete stale database rows.\n");
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  inputs[0].hidden = 0;
  if (!strappy_db_replace_discovered_databases_for_scan_root(catalog_path,
                                                             inputs,
                                                             1U,
                                                             context->temp_dir,
                                                             &error)) {
    fprintf(stderr,
            "Could not re-save first replacement scan: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list replacement state restore: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  ok = 0;
  restored_catalog_id = 0;
  for (index = 0U; index < list.count; index++) {
    if ((list.records[index].path != NULL) &&
        (strcmp(list.records[index].path, first_path) == 0) &&
        (list.records[index].user_decision != NULL) &&
        (strcmp(list.records[index].user_decision, "allowed") == 0) &&
        (list.records[index].assistant_database_id != NULL) &&
        (strcmp(list.records[index].assistant_database_id,
                first_database_id) == 0) &&
        (list.records[index].hidden == 1)) {
      restored_catalog_id = list.records[index].catalog_id;
      ok = 1;
      break;
    }
  }
  strappy_discovered_database_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr,
            "Replacement scan did not restore database access state.\n");
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  if (!strappy_db_update_discovered_database_hidden(catalog_path,
                                                    restored_catalog_id,
                                                    0,
                                                    &error)) {
    fprintf(stderr,
            "Could not update replacement hidden state: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list replacement hidden update: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  ok = 0;
  for (index = 0U; index < list.count; index++) {
    if ((list.records[index].path != NULL) &&
        (strcmp(list.records[index].path, first_path) == 0) &&
        (list.records[index].user_decision != NULL) &&
        (strcmp(list.records[index].user_decision, "allowed") == 0) &&
        (list.records[index].assistant_database_id != NULL) &&
        (strcmp(list.records[index].assistant_database_id,
                first_database_id) == 0) &&
        (list.records[index].hidden == 0)) {
      ok = 1;
      break;
    }
  }
  strappy_discovered_database_record_list_destroy(&list);

  if (!ok) {
    fprintf(stderr,
            "Hidden update did not preserve database access state.\n");
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  if (!strappy_db_replace_discovered_databases_for_scan_root(catalog_path,
                                                             NULL,
                                                             0U,
                                                             context->temp_dir,
                                                             &error)) {
    fprintf(stderr,
            "Could not clear replacement scan before batch save: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  scanner_batch.records =
    (strappy_file_scanner_record *)calloc(1U,
                                          sizeof(strappy_file_scanner_record));
  if (scanner_batch.records == NULL) {
    fprintf(stderr, "Could not allocate scanner batch records.\n");
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }
  scanner_batch.count = 1U;
  strappy_file_scanner_record_init(&scanner_batch.records[0]);
  scanner_batch.records[0].path = strappy_string_duplicate(first_path);
  scanner_batch.records[0].size = 300;
  scanner_batch.records[0].modified_at = 3;
  scanner_batch.records[0].device = 1ULL;
  scanner_batch.records[0].inode = 30ULL;
  scanner_batch.records[0].is_valid_sqlite = 1;
  scanner_batch.records[0].origin_kind = strappy_string_duplicate("documents");
  scanner_batch.records[0].location_tail =
    strappy_string_duplicate("first.sqlite");
  scanner_batch.records[0].hidden = 1;
  if ((scanner_batch.records[0].path == NULL) ||
      (scanner_batch.records[0].origin_kind == NULL) ||
      (scanner_batch.records[0].location_tail == NULL)) {
    fprintf(stderr, "Could not allocate scanner batch record fields.\n");
    strappy_file_scanner_record_list_destroy(&scanner_batch);
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  if (!strappy_file_scanner_save_discovered_database_batch(catalog_path,
                                                           &scanner_batch,
                                                           context->temp_dir,
                                                           &error)) {
    fprintf(stderr,
            "Could not save scanner batch: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    strappy_file_scanner_record_list_destroy(&scanner_batch);
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }
  strappy_file_scanner_record_list_destroy(&scanner_batch);

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list scanner batch state restore: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    free(first_database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  ok = 0;
  for (index = 0U; index < list.count; index++) {
    if ((list.records[index].path != NULL) &&
        (strcmp(list.records[index].path, first_path) == 0) &&
        (list.records[index].user_decision != NULL) &&
        (strcmp(list.records[index].user_decision, "allowed") == 0) &&
        (list.records[index].assistant_database_id != NULL) &&
        (strcmp(list.records[index].assistant_database_id,
                first_database_id) == 0) &&
        (list.records[index].hidden == 0)) {
      ok = 1;
      break;
    }
  }
  strappy_discovered_database_record_list_destroy(&list);
  free(first_database_id);
  harness_unlink_sqlite_files(catalog_path);

  if (!ok) {
    fprintf(stderr,
            "Scanner batch save did not restore database access state.\n");
  }
  return ok;
}

static int harness_run_file_scanner_batch_catalog_tests(
  const harness_context *context)
{
  char catalog_path[1200];
  char scan_root[1200];
  char documents_dir[1200];
  char first_path[1200];
  char second_path[1200];
  char stale_path[1200];
  strappy_discovered_database_input stale_input;
  strappy_discovered_database_record_list catalog_list;
  strappy_file_scanner_options options;
  strappy_file_scanner_record_list scan_list;
  harness_scanner_batch_context batch_context;
  char *error;
  size_t index;
  int found_first;
  int found_second;
  int found_stale;
  int ok;

  if (context == NULL) {
    return 0;
  }

  if (!harness_join_path(catalog_path,
                         sizeof(catalog_path),
                         context->temp_dir,
                         "batch-catalog.sqlite") ||
      !harness_join_path(scan_root,
                         sizeof(scan_root),
                         context->temp_dir,
                         "batch-scan-root") ||
      !harness_join_path(documents_dir,
                         sizeof(documents_dir),
                         scan_root,
                         "Documents") ||
      !harness_join_path(first_path,
                         sizeof(first_path),
                         documents_dir,
                         "first.sqlite") ||
      !harness_join_path(second_path,
                         sizeof(second_path),
                         documents_dir,
                         "second.sqlite") ||
      !harness_join_path(stale_path,
                         sizeof(stale_path),
                         documents_dir,
                         "stale.sqlite")) {
    fprintf(stderr, "Could not build batch scanner test paths.\n");
    return 0;
  }

  harness_unlink_sqlite_files(catalog_path);
  harness_unlink_sqlite_files(first_path);
  harness_unlink_sqlite_files(second_path);
  harness_unlink_sqlite_files(stale_path);

  if (!harness_ensure_directory(scan_root, "mkdir batch scan root") ||
      !harness_ensure_directory(documents_dir, "mkdir batch Documents") ||
      !harness_create_user_database(first_path) ||
      !harness_create_user_database(second_path)) {
    harness_unlink_sqlite_files(catalog_path);
    harness_unlink_sqlite_files(first_path);
    harness_unlink_sqlite_files(second_path);
    return 0;
  }

  strappy_discovered_database_record_list_init(&catalog_list);
  strappy_file_scanner_record_list_init(&scan_list);
  error = NULL;

  strappy_file_scanner_options_init(&options);
  options.root_path = scan_root;
  options.validate_candidates = 1;
  if (strappy_file_scanner_scan_and_save_discovered_databases(catalog_path,
                                                              &options,
                                                              &scan_list,
                                                              &error)) {
    fprintf(stderr, "Batch catalog scan unexpectedly accepted non-batch options.\n");
    strappy_file_scanner_record_list_destroy(&scan_list);
    harness_unlink_sqlite_files(catalog_path);
    harness_unlink_sqlite_files(first_path);
    harness_unlink_sqlite_files(second_path);
    return 0;
  }
  free(error);
  error = NULL;

  memset(&stale_input, 0, sizeof(stale_input));
  stale_input.path = stale_path;
  stale_input.size = 100;
  stale_input.modified_at = 1;
  stale_input.device = 1ULL;
  stale_input.inode = 10ULL;
  stale_input.is_valid_sqlite = 1;
  stale_input.scan_root = scan_root;
  if (!strappy_db_replace_discovered_databases_for_scan_root(catalog_path,
                                                             &stale_input,
                                                             1U,
                                                             scan_root,
                                                             &error)) {
    fprintf(stderr,
            "Could not seed batch scanner stale row: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    strappy_file_scanner_record_list_destroy(&scan_list);
    harness_unlink_sqlite_files(catalog_path);
    harness_unlink_sqlite_files(first_path);
    harness_unlink_sqlite_files(second_path);
    return 0;
  }

  batch_context.catalog_path = catalog_path;
  batch_context.scan_root = scan_root;
  batch_context.scan_run_id = 0LL;
  batch_context.callback_count = 0U;
  options.record_batch_size = 1U;
  options.record_batch_callback = harness_save_scanner_batch;
  options.record_batch_user_data = &batch_context;
  if (!strappy_file_scanner_scan_and_save_discovered_databases(catalog_path,
                                                               &options,
                                                               &scan_list,
                                                               &error)) {
    fprintf(stderr,
            "Could not run batch catalog scanner: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    strappy_file_scanner_record_list_destroy(&scan_list);
    harness_unlink_sqlite_files(catalog_path);
    harness_unlink_sqlite_files(first_path);
    harness_unlink_sqlite_files(second_path);
    return 0;
  }
  if ((batch_context.callback_count != 2U) ||
      (scan_list.records != NULL) || (scan_list.count != 0U) ||
      (scan_list.scan_run_id != 0LL)) {
    fprintf(stderr, "Batch catalog scanner left unflushed records.\n");
    strappy_file_scanner_record_list_destroy(&scan_list);
    harness_unlink_sqlite_files(catalog_path);
    harness_unlink_sqlite_files(first_path);
    harness_unlink_sqlite_files(second_path);
    return 0;
  }

  if (!strappy_db_list_discovered_databases(catalog_path, &catalog_list, &error)) {
    fprintf(stderr,
            "Could not list batch scanner catalog: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    strappy_file_scanner_record_list_destroy(&scan_list);
    harness_unlink_sqlite_files(catalog_path);
    harness_unlink_sqlite_files(first_path);
    harness_unlink_sqlite_files(second_path);
    return 0;
  }

  found_first = 0;
  found_second = 0;
  found_stale = 0;
  for (index = 0U; index < catalog_list.count; index++) {
    if ((catalog_list.records[index].path != NULL) &&
        (strcmp(catalog_list.records[index].path, first_path) == 0)) {
      found_first = 1;
    }
    if ((catalog_list.records[index].path != NULL) &&
        (strcmp(catalog_list.records[index].path, second_path) == 0)) {
      found_second = 1;
    }
    if ((catalog_list.records[index].path != NULL) &&
        (strcmp(catalog_list.records[index].path, stale_path) == 0)) {
      found_stale = 1;
    }
  }

  ok = (found_first && found_second && !found_stale) &&
    harness_expect_catalog_integer(
      catalog_path,
      "SELECT COUNT(*) FROM scan_runs WHERE "
      "id = (SELECT MAX(id) FROM scan_runs) AND state = 'completed' "
      "AND candidate_count = 2 AND database_count = 2 "
      "AND completed_at_ms >= started_at_ms;",
      1LL,
      "completed batch scan run") &&
    harness_expect_catalog_integer(
      catalog_path,
      "SELECT COUNT(*) FROM database_locations WHERE active = 1 AND "
      "last_scan_run_id = (SELECT MAX(id) FROM scan_runs);",
      2LL,
      "batch scan location provenance") &&
    harness_expect_catalog_integer(
      catalog_path,
      "SELECT COUNT(*) FROM scan_roots WHERE id = ("
      "SELECT scan_root_id FROM scan_runs ORDER BY id DESC LIMIT 1) "
      "AND last_started_at_ms IS NOT NULL "
      "AND last_completed_at_ms >= last_started_at_ms;",
      1LL,
      "completed batch scan root timestamps");
  if (!ok) {
    fprintf(stderr, "Batch catalog scanner did not replace stale rows correctly.\n");
  }

  strappy_discovered_database_record_list_destroy(&catalog_list);
  strappy_file_scanner_record_list_destroy(&scan_list);
  harness_unlink_sqlite_files(catalog_path);
  harness_unlink_sqlite_files(first_path);
  harness_unlink_sqlite_files(second_path);
  harness_unlink_sqlite_files(stale_path);
  rmdir(documents_dir);
  rmdir(scan_root);
  return ok;
}

static int harness_run_catalog_reclassification_tests(
  const harness_context *context)
{
  char catalog_path[1200];
  char database_path[1200];
  strappy_discovered_database_input input;
  strappy_discovered_database_record_list list;
  char *error;
  long long catalog_id;
  int ok;

  if ((context == NULL) ||
      !harness_join_path(catalog_path,
                         sizeof(catalog_path),
                         context->temp_dir,
                         "reclassification-catalog.sqlite") ||
      !harness_join_path(database_path,
                         sizeof(database_path),
                         context->temp_dir,
                         "reclassification.sqlite")) {
    return 0;
  }
  harness_unlink_sqlite_files(catalog_path);
  memset(&input, 0, sizeof(input));
  input.path = database_path;
  input.size = 100;
  input.modified_at = 1;
  input.device = 1ULL;
  input.inode = 2ULL;
  input.is_valid_sqlite = 1;
  input.scan_root = context->temp_dir;
  input.app_group_key = "bundle:com.example.Test";
  input.app_name = "Example Test";
  input.app_bundle_id = "com.example.Test";
  input.app_bundle_path = "/Applications/Example Test.app";
  input.app_source = "test";
  input.origin_kind = "application_support";
  input.location_tail = "reclassification.sqlite";
  input.hidden = 1;
  input.hidden_reason = "cache_path";
  error = NULL;
  if (!strappy_db_replace_discovered_databases_for_scan_root(catalog_path,
                                                             &input,
                                                             1U,
                                                             context->temp_dir,
                                                             &error)) {
    fprintf(stderr, "Could not save initial reclassification row: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(catalog_path, &list, &error) ||
      (list.count != 1U)) {
    fprintf(stderr, "Could not read initial reclassification row.\n");
    free(error);
    strappy_discovered_database_record_list_destroy(&list);
    return 0;
  }
  catalog_id = list.records[0].catalog_id;
  ok = (list.records[0].app_bundle_id != NULL) &&
    (strcmp(list.records[0].app_bundle_id, "com.example.Test") == 0) &&
    (list.records[0].hidden == 1) && (list.records[0].auto_hidden == 1) &&
    !list.records[0].has_hidden_override &&
    (list.records[0].hidden_reason != NULL) &&
    (strcmp(list.records[0].hidden_reason, "cache_path") == 0);
  strappy_discovered_database_record_list_destroy(&list);
  if (!ok || !strappy_db_update_discovered_database_hidden(catalog_path,
                                                           catalog_id,
                                                           0,
                                                           &error)) {
    fprintf(stderr, "Could not establish hidden override for reclassification.\n");
    free(error);
    return 0;
  }

  input.app_group_key = NULL;
  input.app_name = NULL;
  input.app_bundle_id = NULL;
  input.app_bundle_path = NULL;
  input.app_source = NULL;
  input.origin_kind = "other";
  if (!strappy_db_replace_discovered_databases_for_scan_root(catalog_path,
                                                             &input,
                                                             1U,
                                                             context->temp_dir,
                                                             &error)) {
    fprintf(stderr, "Could not reclassify catalog row: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }
  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(catalog_path, &list, &error) ||
      (list.count != 1U)) {
    fprintf(stderr, "Could not read reclassified catalog row.\n");
    free(error);
    strappy_discovered_database_record_list_destroy(&list);
    return 0;
  }
  ok = (list.records[0].app_group_key == NULL) &&
    (list.records[0].app_bundle_id == NULL) &&
    (list.records[0].hidden == 0) && (list.records[0].auto_hidden == 1) &&
    list.records[0].has_hidden_override &&
    (list.records[0].hidden_override == 0);
  strappy_discovered_database_record_list_destroy(&list);
  ok = ok && harness_expect_catalog_integer(
    catalog_path,
    "SELECT COUNT(*) FROM applications;",
    0LL,
    "orphan application count");
  harness_unlink_sqlite_files(catalog_path);
  return ok;
}

static int harness_run_file_scanner_filename_filter_tests(
  const harness_context *context)
{
  static const char *const included_names[] = {
    "database",
    ".hidden-database",
    "database.",
    "database.adlib",
    "database.cache",
    "database.db",
    "database.db3",
    "database.gcdata",
    "database.helplib",
    "database.localstorage",
    "database.s3db",
    "database.sl3",
    "database.sql",
    "database.sqlite",
    "database.SQLITE3",
    "database.sqlitedb",
    "database.store"
  };
  static const char *const excluded_names[] = {
    "database.db-wal",
    "database.json",
    "database.plist",
    "database.sqlite.backup",
    "database.txt"
  };
  char catalog_path[1200];
  char scan_root[1200];
  char path[1400];
  strappy_file_scanner_options options;
  strappy_file_scanner_record_list scan_list;
  harness_scanner_batch_context batch_context;
  char *error;
  size_t index;
  size_t record_index;
  int found;
  int ok;

  if (context == NULL) {
    return 0;
  }

  catalog_path[0] = '\0';
  scan_root[0] = '\0';
  path[0] = '\0';
  strappy_file_scanner_record_list_init(&scan_list);
  error = NULL;
  ok = 0;

  if (!harness_join_path(catalog_path,
                         sizeof(catalog_path),
                         context->temp_dir,
                         "scanner-filename-filter-catalog.sqlite") ||
      !harness_join_path(scan_root,
                         sizeof(scan_root),
                         context->temp_dir,
                         "scanner-filename-filter-root")) {
    fprintf(stderr, "Could not build scanner filename filter path.\n");
    goto cleanup;
  }
  harness_unlink_sqlite_files(catalog_path);
  if (!harness_ensure_directory(scan_root,
                                "mkdir scanner filename filter root")) {
    goto cleanup;
  }

  for (index = 0U;
       index < (sizeof(included_names) / sizeof(included_names[0]));
       index++) {
    if (!harness_join_path(path,
                           sizeof(path),
                           scan_root,
                           included_names[index]) ||
        !harness_create_user_database(path)) {
      fprintf(stderr, "Could not create included scanner filename fixture.\n");
      goto cleanup;
    }
  }
  for (index = 0U;
       index < (sizeof(excluded_names) / sizeof(excluded_names[0]));
       index++) {
    if (!harness_join_path(path,
                           sizeof(path),
                           scan_root,
                           excluded_names[index]) ||
        !harness_create_user_database(path)) {
      fprintf(stderr, "Could not create excluded scanner filename fixture.\n");
      goto cleanup;
    }
  }

  strappy_file_scanner_options_init(&options);
  options.root_path = scan_root;
  options.validate_candidates = 1;
  if (!strappy_file_scanner_scan(&options, &scan_list, &error)) {
    fprintf(stderr,
            "Could not run full scanner filename test: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }
  if (scan_list.count !=
      ((sizeof(included_names) / sizeof(included_names[0])) +
       (sizeof(excluded_names) / sizeof(excluded_names[0])))) {
    fprintf(stderr, "Full scanner did not inspect every filename.\n");
    goto cleanup;
  }
  strappy_file_scanner_record_list_destroy(&scan_list);

  options.use_filename_filter = 1;
  if (!strappy_file_scanner_scan(&options, &scan_list, &error)) {
    fprintf(stderr,
            "Could not run scanner filename filter test: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }

  if (scan_list.count !=
      (sizeof(included_names) / sizeof(included_names[0]))) {
    fprintf(stderr,
            "Scanner filename filter returned an unexpected record count.\n");
    goto cleanup;
  }

  for (index = 0U;
       index < (sizeof(included_names) / sizeof(included_names[0]));
       index++) {
    if (!harness_join_path(path,
                           sizeof(path),
                           scan_root,
                           included_names[index])) {
      goto cleanup;
    }
    found = 0;
    for (record_index = 0U;
         record_index < scan_list.count;
         record_index++) {
      if ((scan_list.records[record_index].path != NULL) &&
          (strcmp(scan_list.records[record_index].path, path) == 0) &&
          scan_list.records[record_index].is_valid_sqlite) {
        found = 1;
        break;
      }
    }
    if (!found) {
      fprintf(stderr,
              "Scanner filename filter skipped included fixture %s.\n",
              included_names[index]);
      goto cleanup;
    }
  }

  for (index = 0U;
       index < (sizeof(excluded_names) / sizeof(excluded_names[0]));
       index++) {
    if (!harness_join_path(path,
                           sizeof(path),
                           scan_root,
                           excluded_names[index])) {
      goto cleanup;
    }
    for (record_index = 0U;
         record_index < scan_list.count;
         record_index++) {
      if ((scan_list.records[record_index].path != NULL) &&
          (strcmp(scan_list.records[record_index].path, path) == 0)) {
        fprintf(stderr,
                "Scanner filename filter included excluded fixture %s.\n",
                excluded_names[index]);
        goto cleanup;
      }
    }
  }

  strappy_file_scanner_record_list_destroy(&scan_list);
  strappy_file_scanner_options_init(&options);
  options.root_path = scan_root;
  options.validate_candidates = 1;
  options.record_batch_size = 4U;
  options.record_batch_callback = harness_save_scanner_batch;
  options.record_batch_user_data = &batch_context;
  batch_context.catalog_path = catalog_path;
  batch_context.scan_root = scan_root;
  batch_context.scan_run_id = 0LL;
  batch_context.callback_count = 0U;
  if (!strappy_file_scanner_scan_and_save_discovered_databases(
        catalog_path,
        &options,
        &scan_list,
        &error) ||
      !harness_expect_catalog_integer(
        catalog_path,
        "SELECT COUNT(*) FROM database_locations WHERE active = 1;",
        (long long)((sizeof(included_names) / sizeof(included_names[0])) +
                    (sizeof(excluded_names) / sizeof(excluded_names[0]))),
        "full filename scan catalog")) {
    fprintf(stderr,
            "Full filename scan did not catalog every database: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }

  options.use_filename_filter = 1;
  if (!harness_join_path(path,
                         sizeof(path),
                         scan_root,
                         included_names[0])) {
    goto cleanup;
  }
  harness_unlink_sqlite_files(path);
  options.progress_callback = harness_cancel_file_scanner;
  batch_context.scan_run_id = 0LL;
  batch_context.callback_count = 0U;
  if (strappy_file_scanner_scan_and_save_discovered_databases(
        catalog_path,
        &options,
        &scan_list,
        &error)) {
    fprintf(stderr, "Quick filename scan did not cancel.\n");
    goto cleanup;
  }
  if ((error == NULL) || (strcmp(error, "Scan was cancelled.") != 0)) {
    fprintf(stderr,
            "Quick filename scan returned an unexpected cancellation: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }
  free(error);
  error = NULL;
  options.progress_callback = NULL;
  if (!harness_expect_catalog_integer(
        catalog_path,
        "SELECT COUNT(*) FROM database_locations WHERE active = 1;",
        (long long)((sizeof(included_names) / sizeof(included_names[0])) +
                    (sizeof(excluded_names) / sizeof(excluded_names[0]))),
        "cancelled quick filename scan preserved catalog") ||
      !harness_expect_catalog_integer(
        catalog_path,
        "SELECT COUNT(*) FROM scan_runs WHERE id = "
        "(SELECT MAX(id) FROM scan_runs) AND state = 'cancelled';",
        1LL,
        "cancelled quick filename scan state") ||
      !harness_create_user_database(path)) {
    goto cleanup;
  }

  batch_context.scan_run_id = 0LL;
  batch_context.callback_count = 0U;
  if (!strappy_file_scanner_scan_and_save_discovered_databases(
        catalog_path,
        &options,
        &scan_list,
        &error) ||
      !harness_expect_catalog_integer(
        catalog_path,
        "SELECT COUNT(*) FROM database_locations WHERE active = 1;",
        (long long)((sizeof(included_names) / sizeof(included_names[0])) +
                    (sizeof(excluded_names) / sizeof(excluded_names[0]))),
        "quick filename scan preserved full results") ||
      !harness_expect_catalog_integer(
        catalog_path,
        "SELECT COUNT(*) FROM scan_runs WHERE id = "
        "(SELECT MAX(id) FROM scan_runs) AND candidate_count = 22 "
        "AND database_count = 22;",
        1LL,
        "quick filename scan counts")) {
    fprintf(stderr,
            "Quick filename scan did not preserve full scan results: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }

  if (!harness_join_path(path,
                         sizeof(path),
                         scan_root,
                         included_names[0])) {
    goto cleanup;
  }
  harness_unlink_sqlite_files(path);
  if (!harness_join_path(path,
                         sizeof(path),
                         scan_root,
                         included_names[1]) ||
      !harness_replace_with_non_database_file(path)) {
    goto cleanup;
  }
  if (!harness_join_path(path,
                         sizeof(path),
                         scan_root,
                         excluded_names[0])) {
    goto cleanup;
  }
  harness_unlink_sqlite_files(path);

  batch_context.scan_run_id = 0LL;
  batch_context.callback_count = 0U;
  if (!strappy_file_scanner_scan_and_save_discovered_databases(
        catalog_path,
        &options,
        &scan_list,
        &error) ||
      !harness_expect_catalog_integer(
        catalog_path,
        "SELECT COUNT(*) FROM database_locations WHERE active = 1;",
        (long long)((sizeof(included_names) / sizeof(included_names[0])) +
                    (sizeof(excluded_names) / sizeof(excluded_names[0])) - 3U),
        "quick filename scan reconciled prior results") ||
      !harness_expect_catalog_integer(
        catalog_path,
        "SELECT COUNT(*) FROM database_locations WHERE active = 0;",
        3LL,
        "quick filename scan inactive prior results") ||
      !harness_expect_catalog_integer(
        catalog_path,
        "SELECT COUNT(*) FROM scan_runs WHERE id = "
        "(SELECT MAX(id) FROM scan_runs) AND candidate_count = 19 "
        "AND database_count = 19;",
        1LL,
        "quick filename rescan counts")) {
    fprintf(stderr,
            "Quick filename scan did not reconcile prior results: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }

  for (index = 0U;
       index < (sizeof(excluded_names) / sizeof(excluded_names[0]));
       index++) {
    if (harness_join_path(path,
                          sizeof(path),
                          scan_root,
                          excluded_names[index])) {
      harness_unlink_sqlite_files(path);
    }
  }
  options.use_filename_filter = 0;
  batch_context.scan_run_id = 0LL;
  batch_context.callback_count = 0U;
  if (!strappy_file_scanner_scan_and_save_discovered_databases(
        catalog_path,
        &options,
        &scan_list,
        &error) ||
      !harness_expect_catalog_integer(
        catalog_path,
        "SELECT COUNT(*) FROM database_locations WHERE active = 1;",
        (long long)((sizeof(included_names) / sizeof(included_names[0])) - 2U),
        "full filename rescan replacement")) {
    fprintf(stderr,
            "Full filename rescan did not reconcile deleted databases: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }

  ok = 1;

cleanup:
  free(error);
  strappy_file_scanner_record_list_destroy(&scan_list);
  harness_unlink_sqlite_files(catalog_path);
  if (scan_root[0] != '\0') {
    for (index = 0U;
         index < (sizeof(included_names) / sizeof(included_names[0]));
         index++) {
      if (harness_join_path(path,
                            sizeof(path),
                            scan_root,
                            included_names[index])) {
        harness_unlink_sqlite_files(path);
      }
    }
    for (index = 0U;
         index < (sizeof(excluded_names) / sizeof(excluded_names[0]));
         index++) {
      if (harness_join_path(path,
                            sizeof(path),
                            scan_root,
                            excluded_names[index])) {
        harness_unlink_sqlite_files(path);
      }
    }
    rmdir(scan_root);
  }
  return ok;
}

static int harness_run_file_scanner_hidden_tests(const harness_context *context)
{
  char catalog_path[1200];
  char scan_root[1200];
  char normal_dir[1200];
  char library_dir[1200];
  char caches_dir[1200];
  char cache_owner_dir[1200];
  char app_private_dir[1200];
  char app_var_dir[1200];
  char app_mobile_dir[1200];
  char app_containers_dir[1200];
  char app_data_dir[1200];
  char app_application_dir[1200];
  char app_uuid_dir[1200];
  char app_library_dir[1200];
  char app_preferences_dir[1200];
  char apple_bundle_dir[1200];
  char apple_templates_dir[1200];
  char apple_template_dir[1200];
  char third_party_bundle_dir[1200];
  char third_party_templates_dir[1200];
  char third_party_template_dir[1200];
  char normal_path[1200];
  char named_cache_path[1200];
  char cache_path[1200];
  char app_library_path[1200];
  char apple_index_path[1200];
  char third_party_index_path[1200];
  strappy_file_scanner_options options;
  strappy_file_scanner_record_list scan_list;
  strappy_discovered_database_record_list catalog_list;
  char *error;
  size_t index;
  int found_normal;
  int found_named_cache;
  int found_cache;
  int found_app_library;
  int found_apple_index;
  int found_third_party_index;
  int ok;

  if (context == NULL) {
    return 0;
  }

  catalog_path[0] = '\0';
  scan_root[0] = '\0';
  normal_dir[0] = '\0';
  library_dir[0] = '\0';
  caches_dir[0] = '\0';
  cache_owner_dir[0] = '\0';
  app_private_dir[0] = '\0';
  app_var_dir[0] = '\0';
  app_mobile_dir[0] = '\0';
  app_containers_dir[0] = '\0';
  app_data_dir[0] = '\0';
  app_application_dir[0] = '\0';
  app_uuid_dir[0] = '\0';
  app_library_dir[0] = '\0';
  app_preferences_dir[0] = '\0';
  apple_bundle_dir[0] = '\0';
  apple_templates_dir[0] = '\0';
  apple_template_dir[0] = '\0';
  third_party_bundle_dir[0] = '\0';
  third_party_templates_dir[0] = '\0';
  third_party_template_dir[0] = '\0';
  normal_path[0] = '\0';
  named_cache_path[0] = '\0';
  cache_path[0] = '\0';
  app_library_path[0] = '\0';
  apple_index_path[0] = '\0';
  third_party_index_path[0] = '\0';
  strappy_file_scanner_record_list_init(&scan_list);
  strappy_discovered_database_record_list_init(&catalog_list);
  error = NULL;
  found_normal = 0;
  found_named_cache = 0;
  found_cache = 0;
  found_app_library = 0;
  found_apple_index = 0;
  found_third_party_index = 0;
  ok = 0;

  if (!harness_join_path(catalog_path,
                         sizeof(catalog_path),
                         context->temp_dir,
                         "scanner-hidden-catalog.sqlite") ||
      !harness_join_path(scan_root,
                         sizeof(scan_root),
                         context->temp_dir,
                         "scanner-hidden-root") ||
      !harness_join_path(normal_dir,
                         sizeof(normal_dir),
                         scan_root,
                         "Documents") ||
      !harness_join_path(library_dir,
                         sizeof(library_dir),
                         scan_root,
                         "Library") ||
      !harness_join_path(caches_dir,
                         sizeof(caches_dir),
                         library_dir,
                         "Caches") ||
      !harness_join_path(cache_owner_dir,
                         sizeof(cache_owner_dir),
                         caches_dir,
                         "com.example.CacheOwner") ||
      !harness_join_path(normal_path,
                         sizeof(normal_path),
                         normal_dir,
                         "primary.sqlite") ||
      !harness_join_path(named_cache_path,
                         sizeof(named_cache_path),
                         normal_dir,
                         "SessionCACHEStore.sqlite") ||
      !harness_join_path(cache_path,
                         sizeof(cache_path),
                         cache_owner_dir,
                         "Cache.db") ||
      !harness_join_path(app_private_dir,
                         sizeof(app_private_dir),
                         scan_root,
                         "private") ||
      !harness_join_path(app_var_dir,
                         sizeof(app_var_dir),
                         app_private_dir,
                         "var") ||
      !harness_join_path(app_mobile_dir,
                         sizeof(app_mobile_dir),
                         app_var_dir,
                         "mobile") ||
      !harness_join_path(app_containers_dir,
                         sizeof(app_containers_dir),
                         app_mobile_dir,
                         "Containers") ||
      !harness_join_path(app_data_dir,
                         sizeof(app_data_dir),
                         app_containers_dir,
                         "Data") ||
      !harness_join_path(app_application_dir,
                         sizeof(app_application_dir),
                         app_data_dir,
                         "Application") ||
      !harness_join_path(app_uuid_dir,
                         sizeof(app_uuid_dir),
                         app_application_dir,
                         "00000000-0000-0000-0000-000000000000") ||
      !harness_join_path(app_library_dir,
                         sizeof(app_library_dir),
                         app_uuid_dir,
                         "Library") ||
      !harness_join_path(app_preferences_dir,
                         sizeof(app_preferences_dir),
                         app_library_dir,
                         "Preferences") ||
      !harness_join_path(app_library_path,
                         sizeof(app_library_path),
                         app_preferences_dir,
                         "UserData.sqlite") ||
      !harness_join_path(apple_bundle_dir,
                         sizeof(apple_bundle_dir),
                         scan_root,
                         "Apple.app") ||
      !harness_join_path(apple_templates_dir,
                         sizeof(apple_templates_dir),
                         apple_bundle_dir,
                         "Templates") ||
      !harness_join_path(apple_template_dir,
                         sizeof(apple_template_dir),
                         apple_templates_dir,
                         "Report") ||
      !harness_join_path(apple_index_path,
                         sizeof(apple_index_path),
                         apple_template_dir,
                         "index.db") ||
      !harness_join_path(third_party_bundle_dir,
                         sizeof(third_party_bundle_dir),
                         scan_root,
                         "ThirdParty.app") ||
      !harness_join_path(third_party_templates_dir,
                         sizeof(third_party_templates_dir),
                         third_party_bundle_dir,
                         "Templates") ||
      !harness_join_path(third_party_template_dir,
                         sizeof(third_party_template_dir),
                         third_party_templates_dir,
                         "Report") ||
      !harness_join_path(third_party_index_path,
                         sizeof(third_party_index_path),
                         third_party_template_dir,
                         "index-iso.db")) {
    fprintf(stderr, "Could not build scanner hidden test paths.\n");
    goto cleanup;
  }

  if (!harness_ensure_directory(scan_root, "mkdir scanner-hidden-root") ||
      !harness_ensure_directory(normal_dir, "mkdir scanner Documents") ||
      !harness_ensure_directory(library_dir, "mkdir scanner Library") ||
      !harness_ensure_directory(caches_dir, "mkdir scanner Caches") ||
      !harness_ensure_directory(cache_owner_dir, "mkdir scanner cache owner") ||
      !harness_ensure_directory(app_private_dir, "mkdir scanner private") ||
      !harness_ensure_directory(app_var_dir, "mkdir scanner var") ||
      !harness_ensure_directory(app_mobile_dir, "mkdir scanner mobile") ||
      !harness_ensure_directory(app_containers_dir, "mkdir scanner Containers") ||
      !harness_ensure_directory(app_data_dir, "mkdir scanner Data") ||
      !harness_ensure_directory(app_application_dir, "mkdir scanner Application") ||
      !harness_ensure_directory(app_uuid_dir, "mkdir scanner app uuid") ||
      !harness_ensure_directory(app_library_dir, "mkdir scanner app Library") ||
      !harness_ensure_directory(app_preferences_dir,
                                "mkdir scanner app Preferences") ||
      !harness_ensure_directory(apple_bundle_dir, "mkdir scanner Apple app") ||
      !harness_ensure_directory(apple_templates_dir,
                                "mkdir scanner Apple templates") ||
      !harness_ensure_directory(apple_template_dir,
                                "mkdir scanner Apple template") ||
      !harness_ensure_directory(third_party_bundle_dir,
                                "mkdir scanner third-party app") ||
      !harness_ensure_directory(third_party_templates_dir,
                                "mkdir scanner third-party templates") ||
      !harness_ensure_directory(third_party_template_dir,
                                "mkdir scanner third-party template")) {
    goto cleanup;
  }

  if (!harness_create_user_database(normal_path) ||
      !harness_create_user_database(named_cache_path) ||
      !harness_create_user_database(cache_path) ||
      !harness_create_user_database(app_library_path) ||
      !harness_create_user_database(apple_index_path) ||
      !harness_create_user_database(third_party_index_path)) {
    goto cleanup;
  }

  strappy_file_scanner_options_init(&options);
  options.root_path = scan_root;
  options.validate_candidates = 1;
  if (!strappy_file_scanner_scan(&options, &scan_list, &error)) {
    fprintf(stderr,
            "Could not run scanner hidden test: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    error = NULL;
    goto cleanup;
  }

  for (index = 0U; index < scan_list.count; index++) {
    if ((scan_list.records[index].path != NULL) &&
        (strcmp(scan_list.records[index].path, normal_path) == 0)) {
      found_normal =
        ((scan_list.records[index].hidden == 0) &&
         (scan_list.records[index].origin_kind != NULL) &&
         (strcmp(scan_list.records[index].origin_kind, "documents") == 0) &&
         (scan_list.records[index].location_tail != NULL) &&
         (strcmp(scan_list.records[index].location_tail, "primary.sqlite") == 0)) ?
        1 : 0;
    }
    if ((scan_list.records[index].path != NULL) &&
        (strcmp(scan_list.records[index].path, named_cache_path) == 0)) {
      found_named_cache =
        ((scan_list.records[index].hidden == 1) &&
         (scan_list.records[index].origin_kind != NULL) &&
         (strcmp(scan_list.records[index].origin_kind, "documents") == 0) &&
         (scan_list.records[index].location_tail != NULL) &&
         (strcmp(scan_list.records[index].location_tail,
                 "SessionCACHEStore.sqlite") == 0)) ? 1 : 0;
    }
    if ((scan_list.records[index].path != NULL) &&
        (strcmp(scan_list.records[index].path, cache_path) == 0)) {
      found_cache =
        ((scan_list.records[index].hidden == 1) &&
         (scan_list.records[index].origin_kind != NULL) &&
         (strcmp(scan_list.records[index].origin_kind, "cache") == 0) &&
         (scan_list.records[index].location_tail != NULL) &&
         (strcmp(scan_list.records[index].location_tail,
                 "com.example.CacheOwner/Cache.db") == 0)) ? 1 : 0;
    }
    if ((scan_list.records[index].path != NULL) &&
        (strcmp(scan_list.records[index].path, app_library_path) == 0)) {
      found_app_library =
        ((scan_list.records[index].hidden == 0) &&
         (scan_list.records[index].origin_kind != NULL) &&
         (strcmp(scan_list.records[index].origin_kind, "app_library") == 0) &&
         (scan_list.records[index].location_tail != NULL) &&
         (strcmp(scan_list.records[index].location_tail,
                 "Preferences/UserData.sqlite") == 0)) ? 1 : 0;
    }
    if ((scan_list.records[index].path != NULL) &&
        (strcmp(scan_list.records[index].path, apple_index_path) == 0)) {
      if ((scan_list.records[index].app_group_key == NULL) ||
          (strncmp(scan_list.records[index].app_group_key,
                   "app-path:",
                   strlen("app-path:")) != 0) ||
          (strcmp(scan_list.records[index].app_group_key +
                    strlen("app-path:"),
                  apple_bundle_dir) != 0) ||
          (scan_list.records[index].app_name == NULL) ||
          (strcmp(scan_list.records[index].app_name, "Apple") != 0) ||
          (scan_list.records[index].app_bundle_path == NULL) ||
          (strcmp(scan_list.records[index].app_bundle_path,
                  apple_bundle_dir) != 0) ||
          (scan_list.records[index].app_source == NULL) ||
          (strcmp(scan_list.records[index].app_source,
                  "bundle_plist") != 0)) {
        fprintf(stderr, "C scanner did not infer Apple bundle metadata.\n");
        goto cleanup;
      }
      if (!strappy_file_scanner_record_set_app_metadata(
            &scan_list.records[index],
            "bundle:com.apple.TestApp",
            "Apple Test",
            "com.apple.TestApp",
            NULL,
            apple_bundle_dir,
            "test",
            &error)) {
        fprintf(stderr,
                "Could not set Apple index metadata: %s\n",
                (error != NULL) ? error : "unknown");
        free(error);
        error = NULL;
        goto cleanup;
      }
      found_apple_index =
        ((scan_list.records[index].hidden == 1) &&
         (scan_list.records[index].origin_kind != NULL) &&
         (strcmp(scan_list.records[index].origin_kind, "app_bundle") == 0) &&
         (scan_list.records[index].location_tail != NULL) &&
         (strcmp(scan_list.records[index].location_tail,
                 "Apple.app/Templates/Report/index.db") == 0)) ? 1 : 0;
    }
    if ((scan_list.records[index].path != NULL) &&
        (strcmp(scan_list.records[index].path, third_party_index_path) == 0)) {
      if ((scan_list.records[index].app_group_key == NULL) ||
          (strncmp(scan_list.records[index].app_group_key,
                   "app-path:",
                   strlen("app-path:")) != 0) ||
          (strcmp(scan_list.records[index].app_group_key +
                    strlen("app-path:"),
                  third_party_bundle_dir) != 0) ||
          (scan_list.records[index].app_name == NULL) ||
          (strcmp(scan_list.records[index].app_name, "ThirdParty") != 0) ||
          (scan_list.records[index].app_bundle_path == NULL) ||
          (strcmp(scan_list.records[index].app_bundle_path,
                  third_party_bundle_dir) != 0) ||
          (scan_list.records[index].app_source == NULL) ||
          (strcmp(scan_list.records[index].app_source,
                  "bundle_plist") != 0)) {
        fprintf(stderr,
                "C scanner did not infer third-party bundle metadata.\n");
        goto cleanup;
      }
      if (!strappy_file_scanner_record_set_app_metadata(
            &scan_list.records[index],
            "bundle:com.example.TestApp",
            "Example Test",
            "com.example.TestApp",
            NULL,
            third_party_bundle_dir,
            "test",
            &error)) {
        fprintf(stderr,
                "Could not set third-party index metadata: %s\n",
                (error != NULL) ? error : "unknown");
        free(error);
        error = NULL;
        goto cleanup;
      }
      found_third_party_index =
        ((scan_list.records[index].hidden == 0) &&
         (scan_list.records[index].origin_kind != NULL) &&
         (strcmp(scan_list.records[index].origin_kind, "app_bundle") == 0) &&
         (scan_list.records[index].location_tail != NULL) &&
         (strcmp(scan_list.records[index].location_tail,
                 "ThirdParty.app/Templates/Report/index-iso.db") == 0)) ? 1 : 0;
    }
  }
  if (!found_normal || !found_named_cache || !found_cache ||
      !found_app_library ||
      !found_apple_index || !found_third_party_index) {
    fprintf(stderr, "Scanner display classification did not match expected paths.\n");
    goto cleanup;
  }

  if (!strappy_file_scanner_save_discovered_databases(catalog_path,
                                                      &scan_list,
                                                      scan_root,
                                                      &error)) {
    fprintf(stderr,
            "Could not save scanner hidden catalog: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    error = NULL;
    goto cleanup;
  }

  if (!strappy_db_list_discovered_databases(catalog_path,
                                            &catalog_list,
                                            &error)) {
    fprintf(stderr,
            "Could not list scanner hidden catalog: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    error = NULL;
    goto cleanup;
  }

  found_normal = 0;
  found_named_cache = 0;
  found_cache = 0;
  found_app_library = 0;
  found_apple_index = 0;
  found_third_party_index = 0;
  for (index = 0U; index < catalog_list.count; index++) {
    if ((catalog_list.records[index].path != NULL) &&
        (strcmp(catalog_list.records[index].path, normal_path) == 0)) {
      found_normal =
        ((catalog_list.records[index].hidden == 0) &&
         (catalog_list.records[index].origin_kind != NULL) &&
         (strcmp(catalog_list.records[index].origin_kind, "documents") == 0) &&
         (catalog_list.records[index].location_tail != NULL) &&
         (strcmp(catalog_list.records[index].location_tail,
                 "primary.sqlite") == 0)) ? 1 : 0;
    }
    if ((catalog_list.records[index].path != NULL) &&
        (strcmp(catalog_list.records[index].path, named_cache_path) == 0)) {
      found_named_cache =
        ((catalog_list.records[index].hidden == 1) &&
         (catalog_list.records[index].origin_kind != NULL) &&
         (strcmp(catalog_list.records[index].origin_kind, "documents") == 0) &&
         (catalog_list.records[index].location_tail != NULL) &&
         (strcmp(catalog_list.records[index].location_tail,
                 "SessionCACHEStore.sqlite") == 0)) ? 1 : 0;
    }
    if ((catalog_list.records[index].path != NULL) &&
        (strcmp(catalog_list.records[index].path, cache_path) == 0)) {
      found_cache =
        ((catalog_list.records[index].hidden == 1) &&
         (catalog_list.records[index].origin_kind != NULL) &&
         (strcmp(catalog_list.records[index].origin_kind, "cache") == 0) &&
         (catalog_list.records[index].location_tail != NULL) &&
         (strcmp(catalog_list.records[index].location_tail,
                 "com.example.CacheOwner/Cache.db") == 0)) ? 1 : 0;
    }
    if ((catalog_list.records[index].path != NULL) &&
        (strcmp(catalog_list.records[index].path, app_library_path) == 0)) {
      found_app_library =
        ((catalog_list.records[index].hidden == 0) &&
         (catalog_list.records[index].origin_kind != NULL) &&
         (strcmp(catalog_list.records[index].origin_kind, "app_library") == 0) &&
         (catalog_list.records[index].location_tail != NULL) &&
         (strcmp(catalog_list.records[index].location_tail,
                 "Preferences/UserData.sqlite") == 0)) ? 1 : 0;
    }
    if ((catalog_list.records[index].path != NULL) &&
        (strcmp(catalog_list.records[index].path, apple_index_path) == 0)) {
      found_apple_index =
        ((catalog_list.records[index].hidden == 1) &&
         (catalog_list.records[index].origin_kind != NULL) &&
         (strcmp(catalog_list.records[index].origin_kind, "app_bundle") == 0) &&
         (catalog_list.records[index].location_tail != NULL) &&
         (strcmp(catalog_list.records[index].location_tail,
                 "Apple.app/Templates/Report/index.db") == 0)) ? 1 : 0;
    }
    if ((catalog_list.records[index].path != NULL) &&
        (strcmp(catalog_list.records[index].path, third_party_index_path) == 0)) {
      found_third_party_index =
        ((catalog_list.records[index].hidden == 0) &&
         (catalog_list.records[index].origin_kind != NULL) &&
         (strcmp(catalog_list.records[index].origin_kind, "app_bundle") == 0) &&
         (catalog_list.records[index].location_tail != NULL) &&
         (strcmp(catalog_list.records[index].location_tail,
                 "ThirdParty.app/Templates/Report/index-iso.db") == 0)) ? 1 : 0;
    }
  }
  if (!found_normal || !found_named_cache || !found_cache ||
      !found_app_library ||
      !found_apple_index || !found_third_party_index) {
    fprintf(stderr, "Scanner display metadata was not persisted to the catalog.\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  free(error);
  strappy_file_scanner_record_list_destroy(&scan_list);
  strappy_discovered_database_record_list_destroy(&catalog_list);
  harness_unlink_sqlite_files(normal_path);
  harness_unlink_sqlite_files(named_cache_path);
  harness_unlink_sqlite_files(cache_path);
  harness_unlink_sqlite_files(app_library_path);
  harness_unlink_sqlite_files(apple_index_path);
  harness_unlink_sqlite_files(third_party_index_path);
  harness_unlink_sqlite_files(catalog_path);
  if (third_party_template_dir[0] != '\0') {
    rmdir(third_party_template_dir);
  }
  if (third_party_templates_dir[0] != '\0') {
    rmdir(third_party_templates_dir);
  }
  if (third_party_bundle_dir[0] != '\0') {
    rmdir(third_party_bundle_dir);
  }
  if (apple_template_dir[0] != '\0') {
    rmdir(apple_template_dir);
  }
  if (apple_templates_dir[0] != '\0') {
    rmdir(apple_templates_dir);
  }
  if (apple_bundle_dir[0] != '\0') {
    rmdir(apple_bundle_dir);
  }
  if (app_preferences_dir[0] != '\0') {
    rmdir(app_preferences_dir);
  }
  if (app_library_dir[0] != '\0') {
    rmdir(app_library_dir);
  }
  if (app_uuid_dir[0] != '\0') {
    rmdir(app_uuid_dir);
  }
  if (app_application_dir[0] != '\0') {
    rmdir(app_application_dir);
  }
  if (app_data_dir[0] != '\0') {
    rmdir(app_data_dir);
  }
  if (app_containers_dir[0] != '\0') {
    rmdir(app_containers_dir);
  }
  if (app_mobile_dir[0] != '\0') {
    rmdir(app_mobile_dir);
  }
  if (app_var_dir[0] != '\0') {
    rmdir(app_var_dir);
  }
  if (app_private_dir[0] != '\0') {
    rmdir(app_private_dir);
  }
  if (cache_owner_dir[0] != '\0') {
    rmdir(cache_owner_dir);
  }
  if (caches_dir[0] != '\0') {
    rmdir(caches_dir);
  }
  if (library_dir[0] != '\0') {
    rmdir(library_dir);
  }
  if (normal_dir[0] != '\0') {
    rmdir(normal_dir);
  }
  if (scan_root[0] != '\0') {
    rmdir(scan_root);
  }
  return ok;
}

static int harness_build_query_arguments(char *output,
                                         size_t output_size,
                                         const char *database_id,
                                         const char *sql)
{
  int written;

  written = snprintf(output,
                     output_size,
                     "{\"database_id\":\"%s\",\"sql\":\"%s\"}",
                     database_id,
                     sql);
  return ((written > 0) && ((size_t)written < output_size)) ? 1 : 0;
}

static int harness_run_database_query_tests(const harness_context *context)
{
  static const char *const invalid_text_columns[] = { "invalid_text" };
  static const char *const message_columns[] = { "sender", "body" };
  static const char *const schema_columns[] = { "type", "sql" };
  static const char *const value_columns[] = { "value" };
  static const char *const payload_columns[] = { "large_text", "data" };
  char arguments[2048];

  if ((context == NULL) || (context->database_id == NULL)) {
    return 0;
  }

  if (!harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_QUERY,
        NULL,
        "requires database_id and sql arguments") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_QUERY,
        "null",
        "must be a JSON object") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_QUERY,
        "{}",
        "requires non-empty database_id and sql strings") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_QUERY,
        "{\"database_id\":null,\"sql\":null}",
        "requires non-empty database_id and sql strings") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_QUERY,
        "{\"database_id\":null,\"sql\":\"SELECT 1\"}",
        "requires non-empty database_id and sql strings") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_QUERY,
        "{\"database_id\":\"db_1\",\"sql\":\" \\t\\n\"}",
        "sql must not be blank")) {
    return 0;
  }

  if (!harness_build_query_arguments(
        arguments,
        sizeof(arguments),
        context->database_id,
        "SELECT sender, body FROM messages ORDER BY id")) {
    fprintf(stderr, "Could not build query arguments.\n");
    return 0;
  }

  if (!harness_expect_database_query_shape(
        context->catalog_path,
        arguments,
        message_columns,
        sizeof(message_columns) / sizeof(message_columns[0]),
        2U,
        0) ||
      !harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_QUERY,
                                      arguments,
                                      "alice",
                                      "bob")) {
    return 0;
  }

  if (!harness_build_query_arguments(
        arguments,
        sizeof(arguments),
        context->database_id,
        "SELECT CAST(binary_text AS TEXT) AS invalid_text FROM payloads")) {
    fprintf(stderr, "Could not build invalid UTF-8 query arguments.\n");
    return 0;
  }
  if (!harness_expect_database_query_shape(
        context->catalog_path,
        arguments,
        invalid_text_columns,
        sizeof(invalid_text_columns) / sizeof(invalid_text_columns[0]),
        1U,
        0) ||
      !harness_expect_output_is_valid_utf8(context->catalog_path,
                                           STRAPPY_TOOL_DATABASE_QUERY,
                                           arguments) ||
      !harness_expect_output_contains_without(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_QUERY,
        arguments,
        "\"size_bytes\":12",
        "\"invalid_utf8\":true",
        "bplist00")) {
    return 0;
  }

  if (!harness_build_query_arguments(
        arguments,
        sizeof(arguments),
        context->database_id,
        "SELECT sender, body FROM messages WHERE 0")) {
    fprintf(stderr, "Could not build empty query arguments.\n");
    return 0;
  }
  if (!harness_expect_database_query_shape(
        context->catalog_path,
        arguments,
        message_columns,
        sizeof(message_columns) / sizeof(message_columns[0]),
        0U,
        0)) {
    return 0;
  }

  if (!harness_build_query_arguments(
        arguments,
        sizeof(arguments),
        context->database_id,
        "SELECT type, sql FROM sqlite_schema WHERE name = 'messages'")) {
    fprintf(stderr, "Could not build column exploration query arguments.\n");
    return 0;
  }

  if (!harness_expect_database_query_shape(
        context->catalog_path,
        arguments,
        schema_columns,
        sizeof(schema_columns) / sizeof(schema_columns[0]),
        1U,
        0) ||
      !harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_QUERY,
                                      arguments,
                                      "CREATE TABLE messages",
                                      "sender TEXT NOT NULL")) {
    return 0;
  }

  if (!harness_build_query_arguments(
        arguments,
        sizeof(arguments),
        context->database_id,
        "SELECT value FROM identifiers ORDER BY value DESC")) {
    fprintf(stderr, "Could not build large integer query arguments.\n");
    return 0;
  }

  if (!harness_expect_database_query_shape(
        context->catalog_path,
        arguments,
        value_columns,
        sizeof(value_columns) / sizeof(value_columns[0]),
        3U,
        0) ||
      !harness_expect_output_contains_without(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_QUERY,
        arguments,
        "\"2556198414531480000\"",
        "\"-5023472826755880000\"",
        "e+")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_QUERY,
                                      arguments,
                                      "[42]",
                                      "\"rows\"")) {
    return 0;
  }

  if (!harness_build_query_arguments(
        arguments,
        sizeof(arguments),
        context->database_id,
        "SELECT large_text, data FROM payloads")) {
    fprintf(stderr, "Could not build exceptional cell query arguments.\n");
    return 0;
  }
  if (!harness_expect_database_query_shape(
        context->catalog_path,
        arguments,
        payload_columns,
        sizeof(payload_columns) / sizeof(payload_columns[0]),
        1U,
        0) ||
      !harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_QUERY,
                                      arguments,
                                      "\"type\":\"text\"",
                                      "\"size_bytes\":5000") ||
      !harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_QUERY,
                                      arguments,
                                      "\"type\":\"blob\"",
                                      "\"omitted\":true")) {
    return 0;
  }

  if (!harness_build_query_arguments(
        arguments,
        sizeof(arguments),
        context->database_id,
        "WITH RECURSIVE counter(value) AS (SELECT 1 UNION ALL SELECT "
        "value + 1 FROM counter WHERE value < 101) SELECT value FROM counter")) {
    fprintf(stderr, "Could not build row truncation query arguments.\n");
    return 0;
  }
  if (!harness_expect_database_query_shape(
        context->catalog_path,
        arguments,
        value_columns,
        sizeof(value_columns) / sizeof(value_columns[0]),
        100U,
        1)) {
    return 0;
  }

  if (!harness_build_query_arguments(arguments,
                                     sizeof(arguments),
                                     context->database_id,
                                     "SELECT 1")) {
    fprintf(stderr, "Could not build strict argument test.\n");
    return 0;
  }

  if (strlen(arguments) > 0U) {
    size_t length;

    length = strlen(arguments);
    if (length < (sizeof(arguments) - 16U)) {
      arguments[length - 1U] = '\0';
      strcat(arguments, ",\"path\":\"/tmp/x\"}");
    }
  }

  if (!harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_DATABASE_QUERY,
                                     arguments,
                                     "only accepts database_id and sql")) {
    return 0;
  }

  if (!harness_build_query_arguments(
        arguments,
        sizeof(arguments),
        context->database_id,
        "UPDATE messages SET body = 'x'")) {
    fprintf(stderr, "Could not build write rejection arguments.\n");
    return 0;
  }

  if (!harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_DATABASE_QUERY,
                                     arguments,
                                     "only permits")) {
    return 0;
  }

  if (!harness_build_query_arguments(arguments,
                                     sizeof(arguments),
                                     context->database_id,
                                     "PRAGMA database_list")) {
    fprintf(stderr, "Could not build PRAGMA rejection arguments.\n");
    return 0;
  }

  if (!harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_DATABASE_QUERY,
                                     arguments,
                                     "only permits")) {
    return 0;
  }

  if (!harness_build_query_arguments(arguments,
                                     sizeof(arguments),
                                     context->database_id,
                                     "SELECT 1; SELECT 2")) {
    fprintf(stderr, "Could not build multi-statement rejection arguments.\n");
    return 0;
  }

  if (!harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_DATABASE_QUERY,
                                     arguments,
                                     "exactly one SQL statement")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_QUERY,
        "{\"database_id\":\"db_missing\",\"sql\":\"SELECT 1\"}",
        "database_id was not found")) {
    return 0;
  }

  return 1;
}

static int harness_run_missing_database_query_guidance_test(
  const harness_context *context)
{
  char catalog_path[1200];
  char missing_path[1200];
  char arguments[2048];
  strappy_discovered_database_input input;
  strappy_discovered_database_record_list list;
  char *database_id;
  char *error;
  long long catalog_id;
  int ok;

  if (context == NULL) {
    return 0;
  }

  if (!harness_join_path(catalog_path,
                         sizeof(catalog_path),
                         context->temp_dir,
                         "missing-catalog.sqlite") ||
      !harness_join_path(missing_path,
                         sizeof(missing_path),
                         context->temp_dir,
                         "missing.sqlite")) {
    fprintf(stderr, "Could not build missing database query paths.\n");
    return 0;
  }
  harness_unlink_sqlite_files(catalog_path);
  harness_unlink_sqlite_files(missing_path);

  memset(&input, 0, sizeof(input));
  input.path = missing_path;
  input.size = 4096;
  input.modified_at = 1;
  input.device = 7ULL;
  input.inode = 7ULL;
  input.is_valid_sqlite = 1;
  input.scan_root = context->temp_dir;

  error = NULL;
  if (!strappy_db_save_discovered_databases(catalog_path,
                                            &input,
                                            1U,
                                            &error)) {
    fprintf(stderr,
            "Could not save missing database catalog row: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list missing database catalog row: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  if ((list.count < 1U) || (list.records[0].assistant_database_id == NULL)) {
    fprintf(stderr, "Missing database catalog row was not listed.\n");
    strappy_discovered_database_record_list_destroy(&list);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }
  catalog_id = list.records[0].catalog_id;
  database_id = strappy_string_duplicate(list.records[0].assistant_database_id);
  strappy_discovered_database_record_list_destroy(&list);
  if (database_id == NULL) {
    fprintf(stderr, "Could not copy missing database id.\n");
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  if (!strappy_db_update_discovered_database_decision(catalog_path,
                                                      catalog_id,
                                                      "allowed",
                                                      &error)) {
    fprintf(stderr,
            "Could not approve missing database catalog row: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    free(database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  if (!harness_build_query_arguments(arguments,
                                     sizeof(arguments),
                                     database_id,
                                     "SELECT 1")) {
    fprintf(stderr, "Could not build missing database query arguments.\n");
    free(database_id);
    harness_unlink_sqlite_files(catalog_path);
    return 0;
  }

  ok = harness_expect_error_contains(catalog_path,
                                     STRAPPY_TOOL_DATABASE_QUERY,
                                     arguments,
                                     "approved database file is missing");
  free(database_id);
  harness_unlink_sqlite_files(catalog_path);
  return ok;
}

static int harness_run_readonly_wal_database_query_test(
  harness_context *context)
{
  sqlite3 *db;
  char database_path[1200];
  char arguments[2048];
  char *database_id;
  int ok;

  if (context == NULL) {
    return 0;
  }

  if (!harness_join_path(database_path,
                         sizeof(database_path),
                         context->temp_dir,
                         "wal-user.sqlite")) {
    fprintf(stderr, "Could not build WAL user database path.\n");
    return 0;
  }
  harness_unlink_sqlite_files(database_path);

  db = NULL;
  database_id = NULL;
  ok = harness_create_open_wal_database(database_path, &db) &&
       harness_register_database_path(context,
                                      database_path,
                                      6ULL,
                                      6ULL,
                                      &database_id) &&
       harness_build_query_arguments(
         arguments,
         sizeof(arguments),
         database_id,
         "SELECT note FROM wal_messages ORDER BY id") &&
       harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_QUERY,
                                      arguments,
                                      "from wal",
                                      "\"rows\"");

  free(database_id);
  if (db != NULL) {
    sqlite3_close(db);
  }
  harness_unlink_sqlite_files(database_path);
  return ok;
}

static int harness_run_wide_schema_database_query_test(
  harness_context *context)
{
  static const char *const count_columns[] = { "count(*)" };
  char database_path[1200];
  char arguments[2048];
  char *database_id;
  int ok;

  if (context == NULL) {
    return 0;
  }

  if (!harness_join_path(database_path,
                         sizeof(database_path),
                         context->temp_dir,
                         "wide-user.sqlite")) {
    fprintf(stderr, "Could not build wide-column user database path.\n");
    return 0;
  }
  harness_unlink_sqlite_files(database_path);

  database_id = NULL;
  ok = harness_create_wide_column_database(database_path) &&
       harness_register_database_path(context,
                                      database_path,
                                      8ULL,
                                      8ULL,
                                      &database_id) &&
       harness_build_query_arguments(
         arguments,
         sizeof(arguments),
         database_id,
         "SELECT count(*) FROM sqlite_master WHERE name = 'wide_table'") &&
       harness_expect_database_query_shape(
         context->catalog_path,
         arguments,
         count_columns,
         sizeof(count_columns) / sizeof(count_columns[0]),
         1U,
         0) &&
       harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_QUERY,
                                      arguments,
                                      "\"rows\":[[1]]",
                                      "\"rows_truncated\":false");

  if (ok) {
    ok = harness_build_query_arguments(arguments,
                                       sizeof(arguments),
                                       database_id,
                                       "SELECT * FROM wide_table LIMIT 1") &&
         harness_expect_error_contains(context->catalog_path,
                                       STRAPPY_TOOL_DATABASE_QUERY,
                                       arguments,
                                       "selected too many columns");
  }

  free(database_id);
  harness_unlink_sqlite_files(database_path);
  return ok;
}

static int harness_run_helper_info_tests(const harness_context *context)
{
  static const char *const base_tables[] = {
    "identifiers", "messages", "payloads"
  };
  harness_database_context_expectation expected_context;
  char arguments[4096];
  int written;

  if ((context == NULL) || (context->database_id == NULL)) {
    return 0;
  }

  if (!harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT,
        NULL,
        "requires a non-empty database_id string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT,
        "",
        "requires a non-empty database_id string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT,
        "null",
        "arguments must be a JSON object") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT,
        "\" NULL \"",
        "arguments must be a JSON object") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT,
        "{}",
        "requires a non-empty database_id string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT,
        "{\"database_id\":null}",
        "database_id must be a string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT,
        "{\"database_id\":\"null\"}",
        "database_id must not be blank or null") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT,
        "{\"database_id\":\"  \"}",
        "database_id must not be blank or null")) {
    return 0;
  }

  if (!harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_DATABASE_CONTEXT,
                                     "{\"query\":null}",
                                     "does not accept argument 'query'") ||
      !harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_DATABASE_CONTEXT,
                                     "{\"kind\":null}",
                                     "does not accept argument 'kind'") ||
      !harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_DATABASE_CONTEXT,
                                     "{\"limit\":null}",
                                     "does not accept argument 'limit'")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_SAVE,
        NULL,
        "requires a non-empty fact string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_SAVE,
        "null",
        "arguments must be a JSON object") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_SAVE,
        "\"null\"",
        "arguments must be a JSON object") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_SAVE,
        "{}",
        "requires a non-empty fact string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_SAVE,
        "{\"fact\":null}",
        "fact must be a string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_SAVE,
        "{\"fact\":\" NULL \"}",
        "fact must not be blank or null") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_SAVE,
        "{\"fact\":\"  \\n\\t\"}",
        "fact must not be blank or null") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_SAVE,
        "{\"fact\":\"\"}",
        "requires a non-empty fact string")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_SAVE,
        "{\"kind\":\"identity\",\"fact\":\"Jeff\"}",
        "does not accept argument 'kind'")) {
    return 0;
  }

  if (!harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_MEMORY_READ,
                                     "{\"query\":\"Jeff\"}",
                                     "takes no arguments") ||
      !harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_MEMORY_READ,
                                     "{\"kind\":\"fact\"}",
                                     "takes no arguments") ||
      !harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_MEMORY_READ,
                                     "{\"limit\":1}",
                                     "takes no arguments")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        NULL,
        "requires a non-empty database_id string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "null",
        "arguments must be a JSON object") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "\"null\"",
        "arguments must be a JSON object") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{}",
        "requires a non-empty database_id string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":null,\"description\":\"Useful\","
        "\"context\":\"Useful\"}",
        "database_id must be a string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"null\",\"description\":\"Useful\","
        "\"context\":\"Useful\"}",
        "database_id must not be blank or null") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"\",\"description\":\"Useful\","
        "\"context\":\"Useful\"}",
        "requires a non-empty database_id string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"unused\"}",
        "requires a non-empty description string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"unused\",\"description\":null,"
        "\"context\":\"Useful\"}",
        "description must be a string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"unused\",\"description\":\" NULL \","
        "\"context\":\"Useful\"}",
        "description must not be blank or null") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"unused\",\"description\":\"\","
        "\"context\":\"Useful\"}",
        "requires a non-empty description string")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"unused\",\"description\":\"Useful\"}",
        "requires a non-empty context string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"unused\",\"description\":\"Useful\","
        "\"context\":null}",
        "context must be a string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"unused\",\"description\":\"Useful\","
        "\"context\":\" NULL \"}",
        "context must not be blank or null") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"unused\",\"description\":\"Useful\","
        "\"context\":\"\"}",
        "requires a non-empty context string") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"unused\",\"description\":\"Useful\","
        "\"context\":\"Useful\",\"key\":\"context\"}",
        "does not accept argument 'key'") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"unused\",\"description\":\"Useful\","
        "\"context\":\"Useful\",\"value\":\"Legacy\"}",
        "does not accept argument 'value'") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        "{\"database_id\":\"unused\",\"description\":\"Useful\","
        "\"context\":\"Useful\",\"hint\":\"Legacy\"}",
        "does not accept argument 'hint'")) {
    return 0;
  }

  if (!harness_expect_catalog_sql_ok(
        context->catalog_path,
        "CREATE TRIGGER database_study_context_failure "
        "BEFORE INSERT ON database_hints "
        "WHEN NEW.kind = 'context' "
        "BEGIN SELECT RAISE(ABORT, 'forced context failure'); END;",
        "Database Study atomic-write trigger")) {
    return 0;
  }
  written = snprintf(
    arguments,
    sizeof(arguments),
    "{\"database_id\":\"%s\","
    "\"description\":\"Atomic description.\","
    "\"context\":\"Atomic context.\"}",
    context->database_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments)) ||
      !harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_DATABASE_STUDY,
                                     arguments,
                                     "forced context failure") ||
      !harness_expect_catalog_integer(context->catalog_path,
                                      "SELECT COUNT(*) FROM database_hints;",
                                      0LL,
                                      "atomic Database Study rollback")) {
    (void)harness_expect_catalog_sql_ok(
      context->catalog_path,
      "DROP TRIGGER IF EXISTS database_study_context_failure;",
      "Database Study atomic-write trigger cleanup");
    return 0;
  }
  if (!harness_expect_catalog_sql_ok(
        context->catalog_path,
        "DROP TRIGGER database_study_context_failure;",
        "Database Study atomic-write trigger cleanup")) {
    return 0;
  }

  written = snprintf(
    arguments,
    sizeof(arguments),
    "{\"database_id\":\"%s\","
    "\"description\":\"Messages and identifier serialization test data.\","
    "\"context\":\"Inspect messages and identifiers with targeted SQL.\"}",
    context->database_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments))) {
    fprintf(stderr, "Could not build database study arguments.\n");
    return 0;
  }

  if (!harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        arguments,
        "{}")) {
    return 0;
  }

  written = snprintf(
    arguments,
    sizeof(arguments),
    "{\"database_id\":\"%s\","
    "\"description\":\"Messages and identifier serialization test data.\","
    "\"context\":\"Join messages to identifiers only when needed.\"}",
    context->database_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments)) ||
      !harness_expect_output_equals(context->catalog_path,
                                    STRAPPY_TOOL_DATABASE_STUDY,
                                    arguments,
                                    "{}") ||
      !harness_expect_catalog_integer(
        context->catalog_path,
        "SELECT COUNT(*) FROM database_hints WHERE kind = 'description';",
        1LL,
        "singleton database description") ||
      !harness_expect_catalog_integer(
        context->catalog_path,
        "SELECT COUNT(*) FROM database_hints WHERE kind = 'context';",
        1LL,
        "singleton database context") ||
      !harness_expect_output_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_LIST,
        "{}",
        "\"description\":\"Messages and identifier serialization test "
        "data.\"",
        context->database_id)) {
    return 0;
  }

  written = snprintf(arguments,
                     sizeof(arguments),
                     "{\"database_id\":\"%s\"}",
                     context->database_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments))) {
    fprintf(stderr, "Could not build database hint read arguments.\n");
    return 0;
  }

  memset(&expected_context, 0, sizeof(expected_context));
  expected_context.context =
    "Join messages to identifiers only when needed.";
  expected_context.tables = base_tables;
  expected_context.table_count = sizeof(base_tables) / sizeof(base_tables[0]);
  if (!harness_expect_database_context_result(context->catalog_path,
                                              arguments,
                                              &expected_context)) {
    return 0;
  }

  written = snprintf(arguments,
                     sizeof(arguments),
                     "{\"database_id\":\"%s\","
                     "\"description\":\"Useful description\","
                     "\"context\":\"Useful context\","
                     "\"title\":\"Old title\"}",
                     context->database_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments))) {
    fprintf(stderr, "Could not build legacy database hint arguments.\n");
    return 0;
  }

  if (!harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_STUDY,
        arguments,
        "does not accept argument 'title'")) {
    return 0;
  }

  return 1;
}

static int harness_run_assistant_scoped_memory_tests(
  const harness_context *context)
{
  char study_arguments[1024];
  char *error;
  long long personal_writer_session_id;
  long long personal_reader_session_id;
  long long world_session_id;
  long long coding_session_id;
  int written;
  int ok;

  if (context == NULL) {
    return 0;
  }

  if (!harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_MEMORY_READ,
                                     "{}",
                                     "requires an active session") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_SAVE,
        "{\"fact\":\"Unscoped memory.\"}",
        "requires an active session") ||
      !harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_MEMORY_DELETE,
                                     "{\"id\":1}",
                                     "requires an active session")) {
    return 0;
  }

  error = NULL;
  personal_writer_session_id = 0LL;
  personal_reader_session_id = 0LL;
  world_session_id = 0LL;
  coding_session_id = 0LL;
  ok = strappy_db_create_session(context->catalog_path,
                                 &personal_writer_session_id,
                                 &error) &&
    strappy_db_create_session(context->catalog_path,
                              &personal_reader_session_id,
                              &error) &&
    strappy_db_create_session(context->catalog_path,
                              &world_session_id,
                              &error) &&
    strappy_db_update_session_assistant_set(
      context->catalog_path,
      world_session_id,
      STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE,
      &error) &&
    strappy_db_create_session(context->catalog_path,
                              &coding_session_id,
                              &error) &&
    strappy_db_update_session_assistant_set(
      context->catalog_path,
      coding_session_id,
      STRAPPY_ASSISTANT_SET_CODING_ASSISTANT,
      &error);
  if (!ok) {
    fprintf(stderr,
            "Could not create assistant-scoped memory sessions: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }

  written = snprintf(study_arguments,
                     sizeof(study_arguments),
                     "{\"database_id\":\"%s\","
                     "\"description\":\"Disallowed Personal write.\","
                     "\"context\":\"Disallowed Personal context.\"}",
                     context->database_id);
  if ((written <= 0) || ((size_t)written >= sizeof(study_arguments)) ||
      !harness_expect_session_error_contains(
        context->catalog_path,
        personal_writer_session_id,
        STRAPPY_TOOL_DATABASE_STUDY,
        study_arguments,
        "only available in Database Study sessions")) {
    free(error);
    return 0;
  }

  ok = harness_expect_session_output_equals(
         context->catalog_path,
         personal_writer_session_id,
         STRAPPY_TOOL_MEMORY_SAVE,
         "{\"fact\":\"Personal assistant memory.\"}",
         "{}") &&
    harness_expect_user_fact_read_result(context->catalog_path,
                                         personal_reader_session_id,
                                         "{}",
                                         1LL,
                                         "Personal assistant memory.") &&
    harness_expect_session_output_equals(context->catalog_path,
                                         world_session_id,
                                         STRAPPY_TOOL_MEMORY_READ,
                                         "{}",
                                         "[]") &&
    harness_expect_session_output_equals(
      context->catalog_path,
      world_session_id,
      STRAPPY_TOOL_MEMORY_SAVE,
      "{\"fact\":\"World knowledge memory.\"}",
      "{}") &&
    harness_expect_session_output_equals(context->catalog_path,
                                         coding_session_id,
                                         STRAPPY_TOOL_MEMORY_READ,
                                         "{}",
                                         "[]") &&
    harness_expect_session_output_equals(
      context->catalog_path,
      coding_session_id,
      STRAPPY_TOOL_MEMORY_SAVE,
      "{\"fact\":\"Coding assistant memory.\"}",
      "{}") &&
    harness_expect_user_fact_read_result(context->catalog_path,
                                         personal_writer_session_id,
                                         "{}",
                                         1LL,
                                         "Personal assistant memory.") &&
    harness_expect_user_fact_read_result(context->catalog_path,
                                         world_session_id,
                                         "{}",
                                         2LL,
                                         "World knowledge memory.") &&
    harness_expect_user_fact_read_result(context->catalog_path,
                                         coding_session_id,
                                         "{}",
                                         3LL,
                                         "Coding assistant memory.") &&
    harness_expect_catalog_integer(
      context->catalog_path,
      "SELECT COUNT(*) FROM user_facts WHERE "
      "assistant_set_id='personal_assistant' AND "
      "value='Personal assistant memory.';",
      1LL,
      "personal assistant memory scope") &&
    harness_expect_catalog_integer(
      context->catalog_path,
      "SELECT COUNT(*) FROM user_facts WHERE "
      "assistant_set_id='world_knowledge' AND "
      "value='World knowledge memory.';",
      1LL,
      "world knowledge memory scope") &&
    harness_expect_catalog_integer(
      context->catalog_path,
      "SELECT COUNT(*) FROM user_facts WHERE "
      "assistant_set_id='coding_assistant' AND "
      "value='Coding assistant memory.';",
      1LL,
      "coding assistant memory scope") &&
    strappy_db_update_session_assistant_set(
      context->catalog_path,
      personal_reader_session_id,
      STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE,
      &error) &&
    harness_expect_user_fact_read_result(context->catalog_path,
                                         personal_reader_session_id,
                                         "{}",
                                         2LL,
                                         "World knowledge memory.") &&
    strappy_db_update_session_assistant_set(
      context->catalog_path,
      personal_reader_session_id,
      STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT,
      &error) &&
    harness_expect_user_fact_read_result(context->catalog_path,
                                         personal_reader_session_id,
                                         "{}",
                                         1LL,
                                         "Personal assistant memory.") &&
    harness_expect_session_output_equals(context->catalog_path,
                                         personal_writer_session_id,
                                         STRAPPY_TOOL_MEMORY_DELETE,
                                         "{\"id\":2}",
                                         "{}") &&
    harness_expect_user_fact_read_result(context->catalog_path,
                                         world_session_id,
                                         "{}",
                                         2LL,
                                         "World knowledge memory.") &&
    harness_expect_session_output_equals(context->catalog_path,
                                         personal_writer_session_id,
                                         STRAPPY_TOOL_MEMORY_DELETE,
                                         "{\"id\":1}",
                                         "{}") &&
    harness_expect_session_output_equals(context->catalog_path,
                                         personal_writer_session_id,
                                         STRAPPY_TOOL_MEMORY_DELETE,
                                         "{\"id\":1}",
                                         "{}") &&
    harness_expect_session_output_equals(context->catalog_path,
                                         personal_writer_session_id,
                                         STRAPPY_TOOL_MEMORY_READ,
                                         "{}",
                                         "[]") &&
    harness_expect_session_output_equals(context->catalog_path,
                                         world_session_id,
                                         STRAPPY_TOOL_MEMORY_DELETE,
                                         "{\"id\":2}",
                                         "{}") &&
    harness_expect_session_output_equals(context->catalog_path,
                                         coding_session_id,
                                         STRAPPY_TOOL_MEMORY_DELETE,
                                         "{\"id\":3}",
                                         "{}") &&
    harness_expect_catalog_integer(context->catalog_path,
                                   "SELECT COUNT(*) FROM user_facts;",
                                   0LL,
                                   "assistant-scoped memory cleanup");
  if (!ok) {
    fprintf(stderr,
            "Assistant-scoped memory behavior failed: %s\n",
            (error != NULL) ? error : "tool result mismatch");
  }
  free(error);
  return ok;
}

static int harness_expand_database_context_fixture(const char *database_path)
{
  sqlite3 *db;
  char sql[256];
  int index;
  int rc;
  int written;

  db = NULL;
  rc = sqlite3_open_v2(database_path,
                       &db,
                       SQLITE_OPEN_READWRITE,
                       NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not open database context fixture: %s\n",
            (db != NULL) ? sqlite3_errmsg(db) : "unknown");
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  for (index = 0; index < 19; index++) {
    written = snprintf(sql,
                       sizeof(sql),
                       "CREATE TABLE table_%02d (id INTEGER);",
                       index);
    if ((written <= 0) || ((size_t)written >= sizeof(sql)) ||
        !harness_exec_sql(db, sql)) {
      sqlite3_close(db);
      return 0;
    }
  }

  for (index = 0; index < 21; index++) {
    written = snprintf(sql,
                       sizeof(sql),
                       "CREATE VIEW view_%02d AS SELECT id FROM messages;",
                       index);
    if ((written <= 0) || ((size_t)written >= sizeof(sql)) ||
        !harness_exec_sql(db, sql)) {
      sqlite3_close(db);
      return 0;
    }
  }

  sqlite3_close(db);
  return 1;
}

static int harness_run_database_context_limit_tests(
  const harness_context *context)
{
  static const char *const expected_tables[] = {
    "identifiers", "messages", "payloads",
    "table_00", "table_01", "table_02", "table_03", "table_04",
    "table_05", "table_06", "table_07", "table_08", "table_09",
    "table_10", "table_11", "table_12", "table_13", "table_14",
    "table_15", "table_16"
  };
  static const char *const expected_views[] = {
    "view_00", "view_01", "view_02", "view_03", "view_04",
    "view_05", "view_06", "view_07", "view_08", "view_09",
    "view_10", "view_11", "view_12", "view_13", "view_14",
    "view_15", "view_16", "view_17", "view_18", "view_19"
  };
  harness_database_context_expectation expected_context;
  char arguments[4096];
  int written;

  if ((context == NULL) || (context->database_id == NULL) ||
      !harness_expand_database_context_fixture(context->database_path)) {
    return 0;
  }

  written = snprintf(arguments,
                     sizeof(arguments),
                     "{\"database_id\":\"%s\","
                     "\"description\":\"Expanded context fixture.\","
                     "\"context\":\"Use bounded schema and row queries.\"}",
                     context->database_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments)) ||
      !harness_expect_output_equals(context->catalog_path,
                                    STRAPPY_TOOL_DATABASE_STUDY,
                                    arguments,
                                    "{}")) {
    return 0;
  }

  written = snprintf(arguments,
                     sizeof(arguments),
                     "{\"database_id\":\"%s\"}",
                     context->database_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments))) {
    fprintf(stderr, "Could not build bounded database context arguments.\n");
    return 0;
  }

  memset(&expected_context, 0, sizeof(expected_context));
  expected_context.context = "Use bounded schema and row queries.";
  expected_context.tables = expected_tables;
  expected_context.table_count =
    sizeof(expected_tables) / sizeof(expected_tables[0]);
  expected_context.tables_truncated = 1;
  expected_context.views = expected_views;
  expected_context.view_count =
    sizeof(expected_views) / sizeof(expected_views[0]);
  expected_context.views_truncated = 1;
  return harness_expect_database_context_result(context->catalog_path,
                                                arguments,
                                                &expected_context);
}

static int harness_run_mail_context_tests(harness_context *context)
{
  static const char *const envelope_tables[] = { "mailboxes", "messages" };
  static const char *const protected_tables[] = { "message_data", "messages" };
  harness_database_context_expectation expected_context;
  char arguments[4096];
  int written;

  if (context == NULL) {
    return 0;
  }

  if (!harness_create_mail_database(context, 0) ||
      !harness_register_database_path(context,
                                      context->mail_envelope_path,
                                      2ULL,
                                      2ULL,
                                      &context->mail_envelope_id) ||
      !harness_create_mail_database(context, 1) ||
      !harness_register_database_path(context,
                                      context->mail_protected_path,
                                      3ULL,
                                      3ULL,
                                      &context->mail_protected_id)) {
    return 0;
  }

  written = snprintf(arguments,
                     sizeof(arguments),
                     "{\"database_id\":\"%s\"}",
                     context->mail_envelope_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments))) {
    fprintf(stderr, "Could not build Envelope Index context arguments.\n");
    return 0;
  }

  memset(&expected_context, 0, sizeof(expected_context));
  expected_context.tables = envelope_tables;
  expected_context.table_count =
    sizeof(envelope_tables) / sizeof(envelope_tables[0]);
  if (!harness_expect_database_context_result(context->catalog_path,
                                              arguments,
                                              &expected_context)) {
    return 0;
  }

  written = snprintf(arguments,
                     sizeof(arguments),
                     "{\"database_id\":\"%s\"}",
                     context->mail_protected_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments))) {
    fprintf(stderr, "Could not build Protected Index context arguments.\n");
    return 0;
  }

  memset(&expected_context, 0, sizeof(expected_context));
  expected_context.tables = protected_tables;
  expected_context.table_count =
    sizeof(protected_tables) / sizeof(protected_tables[0]);
  if (!harness_expect_database_context_result(context->catalog_path,
                                              arguments,
                                              &expected_context)) {
    return 0;
  }

  return 1;
}

static int harness_run_sms_context_tests(harness_context *context)
{
  static const char *const tables[] = {
    "attachment",
    "chat",
    "chat_handle_join",
    "chat_message_join",
    "handle",
    "message",
    "message_attachment_join"
  };
  harness_database_context_expectation expected_context;
  char arguments[4096];
  int written;

  if (context == NULL) {
    return 0;
  }

  if (!harness_create_sms_database(context->sms_path) ||
      !harness_register_database_path(context,
                                      context->sms_path,
                                      4ULL,
                                      4ULL,
                                      &context->sms_id)) {
    return 0;
  }

  written = snprintf(arguments,
                     sizeof(arguments),
                     "{\"database_id\":\"%s\"}",
                     context->sms_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments))) {
    fprintf(stderr, "Could not build SMS context arguments.\n");
    return 0;
  }

  memset(&expected_context, 0, sizeof(expected_context));
  expected_context.tables = tables;
  expected_context.table_count = sizeof(tables) / sizeof(tables[0]);
  if (!harness_expect_database_context_result(context->catalog_path,
                                              arguments,
                                              &expected_context)) {
    return 0;
  }

  return 1;
}

static int harness_study_status_matches(
  const char *catalog_path,
  const strappy_study_database_id_list *expected,
  int expect_values,
  const char *replacement_description)
{
  static const char *const root_keys[] = { "databases" };
  static const char *const database_keys[] = {
    "database_id", "description", "context"
  };
  cJSON *root;
  cJSON *databases;
  char *error;
  char *json;
  size_t index;
  int ok;

  if ((catalog_path == NULL) || (expected == NULL)) {
    return 0;
  }
  error = NULL;
  json = strappy_study_status_json(catalog_path, &error);
  root = (json != NULL) ? cJSON_Parse(json) : NULL;
  databases = cJSON_IsObject(root) ?
    cJSON_GetObjectItemCaseSensitive(root, "databases") : NULL;
  ok = (json != NULL) &&
    harness_object_has_exact_keys(root,
                                  root_keys,
                                  sizeof(root_keys) / sizeof(root_keys[0])) &&
    cJSON_IsArray(databases) &&
    (cJSON_GetArraySize(databases) == (int)expected->count);
  for (index = 0U; ok && (index < expected->count); index++) {
    cJSON *database;
    cJSON *database_id;
    cJSON *description;
    cJSON *context;

    database = cJSON_GetArrayItem(databases, (int)index);
    database_id = cJSON_IsObject(database) ?
      cJSON_GetObjectItemCaseSensitive(database, "database_id") : NULL;
    description = cJSON_IsObject(database) ?
      cJSON_GetObjectItemCaseSensitive(database, "description") : NULL;
    context = cJSON_IsObject(database) ?
      cJSON_GetObjectItemCaseSensitive(database, "context") : NULL;
    ok = harness_object_has_exact_keys(
           database,
           database_keys,
           sizeof(database_keys) / sizeof(database_keys[0])) &&
      cJSON_IsString(database_id) && (database_id->valuestring != NULL) &&
      (strcmp(database_id->valuestring, expected->database_ids[index]) == 0);
    if (expect_values) {
      ok = ok && cJSON_IsString(description) &&
        (description->valuestring != NULL) &&
        (description->valuestring[0] != '\0') &&
        cJSON_IsString(context) && (context->valuestring != NULL) &&
        (context->valuestring[0] != '\0');
      if (ok && (index == 0U) && (replacement_description != NULL)) {
        ok = strcmp(description->valuestring,
                    replacement_description) == 0;
      }
    } else {
      ok = ok && cJSON_IsNull(description) && cJSON_IsNull(context);
    }
  }
  if (!ok) {
    fprintf(stderr,
            "Database Study status did not match full coverage: %s (%s)\n",
            (json != NULL) ? json : "(null)",
            (error != NULL) ? error : "no API error");
  }
  cJSON_Delete(root);
  free(json);
  free(error);
  return ok;
}

static int harness_study_status_rows_match(
  const char *catalog_path,
  const strappy_study_database_id_list *expected,
  size_t expected_studied_count)
{
  strappy_study_database_status_record_list list;
  char *error;
  size_t index;
  size_t studied_count;
  int ok;

  if ((catalog_path == NULL) || (expected == NULL)) {
    return 0;
  }
  strappy_study_database_status_record_list_init(&list);
  error = NULL;
  ok = strappy_study_list_database_status_records(catalog_path,
                                                   &list,
                                                   &error) &&
    (list.count == expected->count);
  studied_count = 0U;
  for (index = 0U; ok && (index < list.count); index++) {
    const strappy_study_database_status_record *record;

    record = &list.records[index];
    ok = (record->database_id != NULL) &&
      (strcmp(record->database_id, expected->database_ids[index]) == 0) &&
      (record->path != NULL) && (record->path[0] != '\0') &&
      (record->app_group_key != NULL) && (record->app_name != NULL) &&
      (record->app_bundle_id != NULL) &&
      ((record->studied &&
        (record->description != NULL) &&
        (record->description[0] != '\0') &&
        (record->context != NULL) &&
        (record->context[0] != '\0') &&
        (record->studied_at_ms > 0LL)) ||
       (!record->studied &&
        (record->description == NULL) &&
        (record->context == NULL) &&
        (record->studied_at_ms == 0LL)));
    if (ok && record->studied) {
      studied_count++;
    }
  }
  ok = ok && (studied_count == expected_studied_count);
  if (!ok) {
    fprintf(stderr,
            "Database Study status rows did not match %u of %u studied: %s\n",
            (unsigned int)expected_studied_count,
            (unsigned int)expected->count,
            (error != NULL) ? error : "row mismatch");
  }
  strappy_study_database_status_record_list_destroy(&list);
  free(error);
  return ok;
}

static int harness_write_study_values(const char *catalog_path,
                                      const char *database_id,
                                      const char *description,
                                      const char *context)
{
  cJSON *arguments;
  char *arguments_json;
  int ok;

  arguments = cJSON_CreateObject();
  if ((arguments == NULL) ||
      (cJSON_AddStringToObject(arguments,
                              "database_id",
                              database_id) == NULL) ||
      (cJSON_AddStringToObject(arguments,
                               "description",
                               description) == NULL) ||
      (cJSON_AddStringToObject(arguments, "context", context) == NULL)) {
    cJSON_Delete(arguments);
    return 0;
  }
  arguments_json = cJSON_PrintUnformatted(arguments);
  cJSON_Delete(arguments);
  if (arguments_json == NULL) {
    return 0;
  }
  ok = harness_expect_output_equals(catalog_path,
                                    STRAPPY_TOOL_DATABASE_STUDY,
                                    arguments_json,
                                    "{}");
  free(arguments_json);
  return ok;
}

typedef struct harness_study_app_fixture {
  const char *group_key;
  const char *app_name;
  const char *bundle_id;
  size_t database_count;
} harness_study_app_fixture;

static int harness_prepare_study_batch_catalog(
  const harness_context *context,
  const harness_study_app_fixture *apps,
  size_t app_count,
  char *catalog_path,
  size_t catalog_path_size,
  char **error_out)
{
  strappy_discovered_database_input *inputs;
  strappy_discovered_database_record_list records;
  char **location_tails;
  char **paths;
  size_t app_index;
  size_t database_index;
  size_t index;
  size_t total_count;
  int ok;

  if ((context == NULL) || (apps == NULL) || (app_count == 0U) ||
      (catalog_path == NULL) || (catalog_path_size == 0U) ||
      !harness_join_path(catalog_path,
                         catalog_path_size,
                         context->temp_dir,
                         "study-batch-catalog.sqlite")) {
    strappy_set_error(error_out,
                      "Database Study batch fixture is incomplete.");
    return 0;
  }

  total_count = 0U;
  for (app_index = 0U; app_index < app_count; app_index++) {
    if ((apps[app_index].database_count == 0U) ||
        (apps[app_index].database_count > ((size_t)-1) - total_count)) {
      strappy_set_error(error_out,
                        "Database Study batch fixture count is invalid.");
      return 0;
    }
    total_count += apps[app_index].database_count;
  }

  inputs = (strappy_discovered_database_input *)calloc(
    total_count,
    sizeof(strappy_discovered_database_input));
  paths = (char **)calloc(total_count, sizeof(char *));
  location_tails = (char **)calloc(total_count, sizeof(char *));
  if ((inputs == NULL) || (paths == NULL) || (location_tails == NULL)) {
    free(inputs);
    free(paths);
    free(location_tails);
    strappy_set_error(error_out,
                      "Could not allocate Database Study batch fixture.");
    return 0;
  }

  index = 0U;
  ok = 1;
  for (app_index = 0U; ok && (app_index < app_count); app_index++) {
    for (database_index = 0U;
         ok && (database_index < apps[app_index].database_count);
         database_index++) {
      int path_written;
      int tail_written;

      paths[index] = (char *)calloc(1400U, sizeof(char));
      location_tails[index] = (char *)calloc(128U, sizeof(char));
      if ((paths[index] == NULL) || (location_tails[index] == NULL)) {
        ok = 0;
        break;
      }
      path_written = snprintf(paths[index],
                              1400U,
                              "%s/study-app-%u-database-%u.sqlite",
                              context->temp_dir,
                              (unsigned int)app_index,
                              (unsigned int)database_index);
      tail_written = snprintf(location_tails[index],
                              128U,
                              "database-%u.sqlite",
                              (unsigned int)database_index);
      if ((path_written <= 0) || ((size_t)path_written >= 1400U) ||
          (tail_written <= 0) || ((size_t)tail_written >= 128U)) {
        ok = 0;
        break;
      }

      inputs[index].path = paths[index];
      inputs[index].size = 4096LL + (long long)index;
      inputs[index].modified_at = 1LL;
      inputs[index].device = 100ULL + (unsigned long long)index;
      inputs[index].inode = 1000ULL + (unsigned long long)index;
      inputs[index].is_valid_sqlite = 1;
      inputs[index].scan_root = context->temp_dir;
      inputs[index].app_group_key = apps[app_index].group_key;
      inputs[index].app_name = apps[app_index].app_name;
      inputs[index].app_bundle_id = apps[app_index].bundle_id;
      inputs[index].origin_kind = "application_support";
      inputs[index].location_tail = location_tails[index];
      index++;
    }
  }
  if (!ok) {
    strappy_set_error(error_out,
                      "Could not build Database Study batch fixture paths.");
  }

  harness_unlink_sqlite_files(catalog_path);
  if (ok) {
    ok = strappy_db_save_discovered_databases(catalog_path,
                                               inputs,
                                               total_count,
                                               error_out);
  }
  strappy_discovered_database_record_list_init(&records);
  if (ok) {
    ok = strappy_db_list_discovered_databases(catalog_path,
                                               &records,
                                               error_out) &&
      (records.count == total_count);
  }
  for (index = 0U; ok && (index < records.count); index++) {
    ok = strappy_db_update_discovered_database_decision(
      catalog_path,
      records.records[index].catalog_id,
      "allowed",
      error_out);
  }
  strappy_discovered_database_record_list_destroy(&records);

  for (index = 0U; index < total_count; index++) {
    free(paths[index]);
    free(location_tails[index]);
  }
  free(inputs);
  free(paths);
  free(location_tails);
  return ok;
}

static int harness_study_batch_matches(
  const strappy_study_batch *batch,
  size_t expected_database_count,
  size_t expected_pending_count,
  size_t expected_group_count,
  const char *expected_first_app_name,
  const char *expected_first_bundle_id,
  int check_first_metadata)
{
  static const char *const root_keys[] = {
    "instruction", "application_groups"
  };
  static const char *const group_keys[] = {
    "app_name", "app_bundle_id", "database_ids"
  };
  cJSON *application_groups;
  cJSON *instruction;
  cJSON *root;
  size_t flattened_index;
  size_t group_index;
  int ok;

  if ((batch == NULL) || (batch->prompt == NULL)) {
    return 0;
  }
  root = cJSON_Parse(batch->prompt);
  instruction = cJSON_IsObject(root) ?
    cJSON_GetObjectItemCaseSensitive(root, "instruction") : NULL;
  application_groups = cJSON_IsObject(root) ?
    cJSON_GetObjectItemCaseSensitive(root, "application_groups") : NULL;
  ok = (batch->database_ids.count == expected_database_count) &&
    (batch->pending_database_ids.count == expected_pending_count) &&
    harness_object_has_exact_keys(root,
                                  root_keys,
                                  sizeof(root_keys) / sizeof(root_keys[0])) &&
    cJSON_IsString(instruction) && (instruction->valuestring != NULL) &&
    (strstr(instruction->valuestring, "tightly coupled data") != NULL) &&
    cJSON_IsArray(application_groups) &&
    (cJSON_GetArraySize(application_groups) == (int)expected_group_count);

  flattened_index = 0U;
  for (group_index = 0U;
       ok && (group_index < expected_group_count);
       group_index++) {
    cJSON *app_bundle_id;
    cJSON *app_name;
    cJSON *database_id;
    cJSON *database_ids;
    cJSON *group;

    group = cJSON_GetArrayItem(application_groups, (int)group_index);
    app_name = cJSON_IsObject(group) ?
      cJSON_GetObjectItemCaseSensitive(group, "app_name") : NULL;
    app_bundle_id = cJSON_IsObject(group) ?
      cJSON_GetObjectItemCaseSensitive(group, "app_bundle_id") : NULL;
    database_ids = cJSON_IsObject(group) ?
      cJSON_GetObjectItemCaseSensitive(group, "database_ids") : NULL;
    ok = harness_object_has_exact_keys(
           group,
           group_keys,
           sizeof(group_keys) / sizeof(group_keys[0])) &&
      cJSON_IsArray(database_ids) && (database_ids->child != NULL);
    for (database_id = cJSON_IsArray(database_ids) ?
           database_ids->child : NULL;
         ok && (database_id != NULL);
         database_id = database_id->next) {
      ok = (flattened_index < batch->database_ids.count) &&
        cJSON_IsString(database_id) &&
        (database_id->valuestring != NULL) &&
        (strcmp(database_id->valuestring,
                batch->database_ids.database_ids[flattened_index]) == 0);
      flattened_index++;
    }
    if (ok && check_first_metadata && (group_index == 0U)) {
      ok = cJSON_IsString(app_name) && (app_name->valuestring != NULL) &&
        (expected_first_app_name != NULL) &&
        (strcmp(app_name->valuestring, expected_first_app_name) == 0) &&
        ((expected_first_bundle_id != NULL) ?
          (cJSON_IsString(app_bundle_id) &&
           (app_bundle_id->valuestring != NULL) &&
           (strcmp(app_bundle_id->valuestring,
                   expected_first_bundle_id) == 0)) :
          cJSON_IsNull(app_bundle_id));
    }
  }
  ok = ok && (flattened_index == batch->database_ids.count);
  if (!ok) {
    fprintf(stderr,
            "Database Study batch did not match its grouped public shape: "
            "%s\n",
            batch->prompt);
  }
  cJSON_Delete(root);
  return ok;
}

static int harness_run_database_study_batch_planning_tests(
  const harness_context *context)
{
  static const harness_study_app_fixture large_and_small[] = {
    { "bundle:com.example.small", "Small", "com.example.small", 1U },
    { "bundle:com.example.large", "Large", "com.example.large", 10U }
  };
  static const harness_study_app_fixture three_and_one[] = {
    { "bundle:com.example.one", "One", "com.example.one", 1U },
    { "bundle:com.example.three", "Three", "com.example.three", 3U }
  };
  static const harness_study_app_fixture two_and_one[] = {
    { "bundle:com.example.one", "One", "com.example.one", 1U },
    { "bundle:com.example.two", "Two", "com.example.two", 2U }
  };
  static const harness_study_app_fixture two_and_two[] = {
    { "bundle:com.example.first-two", "First Two",
      "com.example.first-two", 2U },
    { "bundle:com.example.second-two", "Second Two",
      "com.example.second-two", 2U }
  };
  static const harness_study_app_fixture unknown_databases[] = {
    { NULL, NULL, NULL, 4U }
  };
  static const harness_study_app_fixture stale_siblings[] = {
    { "bundle:com.example.stale", "Stale", "com.example.stale", 3U }
  };
  strappy_study_batch batch;
  strappy_study_database_status_record_list status;
  char catalog_path[1200];
  char *error;
  int ok;

  if (context == NULL) {
    return 0;
  }
  catalog_path[0] = '\0';
  error = NULL;
  strappy_study_batch_init(&batch);
  strappy_study_database_status_record_list_init(&status);

  ok = harness_prepare_study_batch_catalog(
         context,
         large_and_small,
         sizeof(large_and_small) / sizeof(large_and_small[0]),
         catalog_path,
         sizeof(catalog_path),
         &error) &&
    strappy_study_next_batch(catalog_path, &batch, &error) &&
    harness_study_batch_matches(&batch,
                                10U,
                                10U,
                                1U,
                                "Large",
                                "com.example.large",
                                1);
  if (!ok) {
    fprintf(stderr,
            "Database Study did not keep a ten-database app together: %s\n",
            (error != NULL) ? error : "batch mismatch");
    goto cleanup;
  }
  strappy_study_batch_destroy(&batch);

  ok = harness_prepare_study_batch_catalog(
         context,
         three_and_one,
         sizeof(three_and_one) / sizeof(three_and_one[0]),
         catalog_path,
         sizeof(catalog_path),
         &error) &&
    strappy_study_next_batch(catalog_path, &batch, &error) &&
    harness_study_batch_matches(&batch,
                                3U,
                                3U,
                                1U,
                                "Three",
                                "com.example.three",
                                1);
  if (!ok) {
    fprintf(stderr,
            "Database Study grouped another app with a three-database app: "
            "%s\n",
            (error != NULL) ? error : "batch mismatch");
    goto cleanup;
  }
  strappy_study_batch_destroy(&batch);

  ok = harness_prepare_study_batch_catalog(
         context,
         two_and_one,
         sizeof(two_and_one) / sizeof(two_and_one[0]),
         catalog_path,
         sizeof(catalog_path),
         &error) &&
    strappy_study_next_batch(catalog_path, &batch, &error) &&
    harness_study_batch_matches(&batch, 3U, 3U, 2U, NULL, NULL, 0);
  if (!ok) {
    fprintf(stderr,
            "Database Study did not pack two small apps into three "
            "databases: %s\n",
            (error != NULL) ? error : "batch mismatch");
    goto cleanup;
  }
  strappy_study_batch_destroy(&batch);

  ok = harness_prepare_study_batch_catalog(
         context,
         two_and_two,
         sizeof(two_and_two) / sizeof(two_and_two[0]),
         catalog_path,
         sizeof(catalog_path),
         &error) &&
    strappy_study_next_batch(catalog_path, &batch, &error) &&
    harness_study_batch_matches(&batch, 2U, 2U, 1U, NULL, NULL, 0);
  if (!ok) {
    fprintf(stderr,
            "Database Study combined small apps above the three-database "
            "limit: %s\n",
            (error != NULL) ? error : "batch mismatch");
    goto cleanup;
  }
  strappy_study_batch_destroy(&batch);

  ok = harness_prepare_study_batch_catalog(
         context,
         unknown_databases,
         sizeof(unknown_databases) / sizeof(unknown_databases[0]),
         catalog_path,
         sizeof(catalog_path),
         &error) &&
    strappy_study_next_batch(catalog_path, &batch, &error) &&
    harness_study_batch_matches(&batch, 3U, 3U, 3U, NULL, NULL, 0);
  if (!ok) {
    fprintf(stderr,
            "Database Study treated unrelated unknown databases as one app: "
            "%s\n",
            (error != NULL) ? error : "batch mismatch");
    goto cleanup;
  }
  strappy_study_batch_destroy(&batch);

  ok = harness_prepare_study_batch_catalog(
         context,
         stale_siblings,
         sizeof(stale_siblings) / sizeof(stale_siblings[0]),
         catalog_path,
         sizeof(catalog_path),
         &error) &&
    strappy_study_list_database_status_records(catalog_path,
                                                &status,
                                                &error) &&
    (status.count == 3U) &&
    harness_write_study_values(catalog_path,
                               status.records[0].database_id,
                               "First fresh sibling",
                               "First fresh sibling context") &&
    harness_write_study_values(catalog_path,
                               status.records[1].database_id,
                               "Second fresh sibling",
                               "Second fresh sibling context") &&
    strappy_study_next_batch(catalog_path, &batch, &error) &&
    harness_study_batch_matches(&batch,
                                3U,
                                1U,
                                1U,
                                "Stale",
                                "com.example.stale",
                                1) &&
    (strcmp(batch.pending_database_ids.database_ids[0],
            status.records[2].database_id) == 0);
  if (!ok) {
    fprintf(stderr,
            "Database Study did not include fresh siblings of a pending "
            "database: %s\n",
            (error != NULL) ? error : "batch mismatch");
  }

cleanup:
  strappy_study_batch_destroy(&batch);
  strappy_study_database_status_record_list_destroy(&status);
  free(error);
  if (catalog_path[0] != '\0') {
    harness_unlink_sqlite_files(catalog_path);
  }
  return ok;
}

typedef struct harness_study_research_call {
  const char *call_id;
  const char *tool_name;
  char *arguments_json;
  char *output_json;
} harness_study_research_call;

static void harness_study_research_calls_destroy(
  harness_study_research_call *calls,
  size_t call_count)
{
  size_t index;

  if (calls == NULL) {
    return;
  }
  for (index = 0U; index < call_count; index++) {
    free(calls[index].arguments_json);
    calls[index].arguments_json = NULL;
    free(calls[index].output_json);
    calls[index].output_json = NULL;
  }
}

static char *harness_study_arguments_json(const char *database_id,
                                          const char *sql,
                                          const char *description,
                                          const char *context)
{
  cJSON *arguments;
  char *json;

  arguments = cJSON_CreateObject();
  if ((arguments == NULL) ||
      (cJSON_AddStringToObject(arguments,
                               "database_id",
                               database_id) == NULL) ||
      ((sql != NULL) &&
       (cJSON_AddStringToObject(arguments, "sql", sql) == NULL)) ||
      ((description != NULL) &&
       (cJSON_AddStringToObject(arguments,
                                "description",
                                description) == NULL)) ||
      ((context != NULL) &&
       (cJSON_AddStringToObject(arguments, "context", context) == NULL))) {
    cJSON_Delete(arguments);
    return NULL;
  }
  json = cJSON_PrintUnformatted(arguments);
  cJSON_Delete(arguments);
  return json;
}

static cJSON *harness_study_user_input(void)
{
  cJSON *items;
  cJSON *message;

  items = cJSON_CreateArray();
  message = cJSON_CreateObject();
  if ((items == NULL) || (message == NULL) ||
      (cJSON_AddStringToObject(message, "type", "message") == NULL) ||
      (cJSON_AddStringToObject(message, "role", "user") == NULL) ||
      (cJSON_AddStringToObject(message,
                               "content",
                               "Study guard harness") == NULL) ||
      !cJSON_AddItemToArray(items, message)) {
    cJSON_Delete(message);
    cJSON_Delete(items);
    return NULL;
  }
  return items;
}

static cJSON *harness_study_function_call_items(
  const harness_study_research_call *calls,
  size_t call_count)
{
  cJSON *items;
  size_t index;

  items = cJSON_CreateArray();
  if (items == NULL) {
    return NULL;
  }
  for (index = 0U; index < call_count; index++) {
    cJSON *item;

    item = cJSON_CreateObject();
    if ((item == NULL) ||
        (cJSON_AddStringToObject(item, "type", "function_call") == NULL) ||
        (cJSON_AddStringToObject(item,
                                 "call_id",
                                 calls[index].call_id) == NULL) ||
        (cJSON_AddStringToObject(item,
                                 "name",
                                 calls[index].tool_name) == NULL) ||
        (cJSON_AddStringToObject(item,
                                 "arguments",
                                 calls[index].arguments_json) == NULL) ||
        (cJSON_AddStringToObject(item, "status", "completed") == NULL) ||
        !cJSON_AddItemToArray(items, item)) {
      cJSON_Delete(item);
      cJSON_Delete(items);
      return NULL;
    }
  }
  return items;
}

static cJSON *harness_study_function_output_items(
  const harness_study_research_call *calls,
  size_t call_count)
{
  cJSON *items;
  size_t index;

  items = cJSON_CreateArray();
  if (items == NULL) {
    return NULL;
  }
  for (index = 0U; index < call_count; index++) {
    cJSON *item;

    item = cJSON_CreateObject();
    if ((item == NULL) ||
        (cJSON_AddStringToObject(item,
                                 "type",
                                 "function_call_output") == NULL) ||
        (cJSON_AddStringToObject(item,
                                 "call_id",
                                 calls[index].call_id) == NULL) ||
        (cJSON_AddStringToObject(item,
                                 "output",
                                 calls[index].output_json) == NULL) ||
        !cJSON_AddItemToArray(items, item)) {
      cJSON_Delete(item);
      cJSON_Delete(items);
      return NULL;
    }
  }
  return items;
}

static int harness_study_append_round(
  const char *catalog_path,
  long long session_id,
  long long previous_call_id,
  const char *prompt_group_key,
  const char *request_kind,
  long round_index,
  cJSON *request_items,
  cJSON *response_items,
  long long *call_id_out,
  char **error_out)
{
  strappy_response_call_begin_input begin;
  strappy_response_call_finish_input finish;
  strappy_model_route_record route;
  cJSON *request;
  cJSON *response;
  cJSON *request_items_copy;
  cJSON *response_items_copy;
  char *request_json;
  char *response_json;
  long long call_id;
  int ok;

  if (call_id_out != NULL) {
    *call_id_out = 0LL;
  }
  if (!cJSON_IsArray(request_items) || !cJSON_IsArray(response_items)) {
    return 0;
  }

  strappy_model_route_record_init(&route);
  if (!strappy_db_get_session_model_route(catalog_path,
                                          session_id,
                                          &route,
                                          error_out)) {
    strappy_model_route_record_destroy(&route);
    return 0;
  }

  request = cJSON_CreateObject();
  response = cJSON_CreateObject();
  request_items_copy = cJSON_Duplicate(request_items, 1);
  response_items_copy = cJSON_Duplicate(response_items, 1);
  if ((request == NULL) || (response == NULL) ||
      (request_items_copy == NULL) || (response_items_copy == NULL) ||
      (cJSON_AddStringToObject(request,
                               "model",
                               route.wire_model_id) == NULL) ||
      (cJSON_AddFalseToObject(request, "stream") == NULL) ||
      (cJSON_AddFalseToObject(request, "store") == NULL) ||
      !cJSON_AddItemToObject(request, "input", request_items_copy)) {
    cJSON_Delete(request_items_copy);
    cJSON_Delete(response_items_copy);
    cJSON_Delete(request);
    cJSON_Delete(response);
    strappy_model_route_record_destroy(&route);
    return 0;
  }
  request_items_copy = NULL;
  if ((cJSON_AddStringToObject(response,
                               "id",
                               "study-guard-response") == NULL) ||
      (cJSON_AddStringToObject(response, "status", "completed") == NULL) ||
      (cJSON_AddStringToObject(response, "model", "test/model") == NULL) ||
      !cJSON_AddItemToObject(response, "output", response_items_copy)) {
    cJSON_Delete(response_items_copy);
    cJSON_Delete(request);
    cJSON_Delete(response);
    strappy_model_route_record_destroy(&route);
    return 0;
  }
  response_items_copy = NULL;
  request_json = cJSON_PrintUnformatted(request);
  response_json = cJSON_PrintUnformatted(response);
  cJSON_Delete(request);
  cJSON_Delete(response);
  if ((request_json == NULL) || (response_json == NULL)) {
    free(request_json);
    free(response_json);
    strappy_model_route_record_destroy(&route);
    return 0;
  }

  memset(&begin, 0, sizeof(begin));
  begin.session_id = session_id;
  begin.previous_call_id = previous_call_id;
  begin.provider_account_id = route.provider_account_id;
  begin.model_id = route.model_id;
  begin.prompt_group_key = prompt_group_key;
  begin.request_kind = request_kind;
  begin.round_index = round_index;
  begin.attempt_index = 0L;
  begin.new_input_start_index = 0L;
  begin.input_from_current_turn = (round_index > 0L) ? 1 : 0;
  begin.request_method = "POST";
  begin.request_url = "https://openrouter.ai/api/v1/responses";
  begin.request_headers_json = "{}";
  begin.request_json = request_json;
  call_id = 0LL;
  ok = strappy_db_begin_response_call(catalog_path,
                                      &begin,
                                      &call_id,
                                      error_out);
  if (ok) {
    memset(&finish, 0, sizeof(finish));
    finish.call_id = call_id;
    finish.state = "completed";
    finish.output_is_canonical = 1;
    finish.http_status = 200L;
    finish.started_at_ms = call_id * 1000LL;
    finish.completed_at_ms = finish.started_at_ms + 100LL;
    finish.request_bytes = (long long)strlen(request_json);
    finish.response_bytes = (long long)strlen(response_json);
    finish.total_seconds = 0.1;
    finish.effective_url = begin.request_url;
    finish.content_type = "application/json";
    finish.response_headers = "";
    finish.response_json = response_json;
    ok = strappy_db_finish_response_call(catalog_path, &finish, error_out);
  }
  free(request_json);
  free(response_json);
  strappy_model_route_record_destroy(&route);
  if (ok && (call_id_out != NULL)) {
    *call_id_out = call_id;
  }
  return ok;
}

static int harness_study_execute_research_calls(
  const harness_context *context,
  long long session_id,
  long long response_call_id,
  harness_study_research_call *calls,
  size_t call_count,
  char **error_out)
{
  strappy_response_tool_execution_input execution;
  size_t index;

  for (index = 0U; index < call_count; index++) {
    calls[index].output_json = strappy_tools_execute_for_function_call(
      context->catalog_path,
      session_id,
      HARNESS_RESOURCE_DIR,
      calls[index].call_id,
      calls[index].tool_name,
      calls[index].arguments_json,
      error_out);
    if (calls[index].output_json == NULL) {
      return 0;
    }

    memset(&execution, 0, sizeof(execution));
    execution.session_id = session_id;
    execution.response_call_id = response_call_id;
    execution.output_index = (long)index;
    execution.call_id = calls[index].call_id;
    execution.tool_name = calls[index].tool_name;
    execution.arguments_json = calls[index].arguments_json;
    execution.status = "completed";
    execution.output_json = calls[index].output_json;
    execution.started_at_ms = 1000LL + (long long)index;
    execution.completed_at_ms = 1100LL + (long long)index;
    if (!strappy_db_save_response_tool_execution(context->catalog_path,
                                                  &execution,
                                                  error_out)) {
      return 0;
    }
  }
  return 1;
}

static int harness_study_record_research(
  const harness_context *context,
  long long session_id,
  long long previous_call_id,
  const char *prompt_group_key,
  const char *context_call_id,
  const char *query_call_id,
  const char *query_sql,
  harness_study_research_call calls[2],
  long long *response_call_id_out,
  char **error_out)
{
  cJSON *request_items;
  cJSON *response_items;
  int ok;

  memset(calls, 0, 2U * sizeof(*calls));
  calls[0].call_id = context_call_id;
  calls[0].tool_name = STRAPPY_TOOL_DATABASE_CONTEXT;
  calls[0].arguments_json = harness_study_arguments_json(
    context->database_id, NULL, NULL, NULL);
  calls[1].call_id = query_call_id;
  calls[1].tool_name = STRAPPY_TOOL_DATABASE_QUERY;
  calls[1].arguments_json = harness_study_arguments_json(
    context->database_id, query_sql, NULL, NULL);
  request_items = harness_study_user_input();
  response_items = harness_study_function_call_items(calls, 2U);
  if ((calls[0].arguments_json == NULL) ||
      (calls[1].arguments_json == NULL) ||
      (request_items == NULL) ||
      (response_items == NULL)) {
    cJSON_Delete(request_items);
    cJSON_Delete(response_items);
    return 0;
  }

  ok = harness_study_append_round(context->catalog_path,
                                  session_id,
                                  previous_call_id,
                                  prompt_group_key,
                                  "user",
                                  0L,
                                  request_items,
                                  response_items,
                                  response_call_id_out,
                                  error_out);
  cJSON_Delete(request_items);
  cJSON_Delete(response_items);
  return ok && harness_study_execute_research_calls(
    context,
    session_id,
    *response_call_id_out,
    calls,
    2U,
    error_out);
}

static int harness_study_append_save_call(
  const harness_context *context,
  long long session_id,
  long long previous_call_id,
  const char *prompt_group_key,
  const harness_study_research_call calls[2],
  const char *study_call_id,
  const char *study_arguments_json,
  long long *response_call_id_out,
  char **error_out)
{
  harness_study_research_call study_call;
  cJSON *request_items;
  cJSON *response_items;
  int ok;

  memset(&study_call, 0, sizeof(study_call));
  study_call.call_id = study_call_id;
  study_call.tool_name = STRAPPY_TOOL_DATABASE_STUDY;
  study_call.arguments_json = (char *)study_arguments_json;
  request_items = harness_study_function_output_items(calls, 2U);
  response_items = harness_study_function_call_items(&study_call, 1U);
  if ((request_items == NULL) || (response_items == NULL)) {
    cJSON_Delete(request_items);
    cJSON_Delete(response_items);
    return 0;
  }
  ok = harness_study_append_round(context->catalog_path,
                                  session_id,
                                  previous_call_id,
                                  prompt_group_key,
                                  "tool_continuation",
                                  1L,
                                  request_items,
                                  response_items,
                                  response_call_id_out,
                                  error_out);
  cJSON_Delete(request_items);
  cJSON_Delete(response_items);
  return ok;
}

static int harness_study_save_result_matches(
  const harness_context *context,
  long long session_id,
  const char *study_call_id,
  const char *study_arguments_json,
  int expect_success,
  const char *expected_error)
{
  char *error;
  char *output;
  int ok;

  error = NULL;
  output = strappy_tools_execute_for_function_call(
    context->catalog_path,
    session_id,
    HARNESS_RESOURCE_DIR,
    study_call_id,
    STRAPPY_TOOL_DATABASE_STUDY,
    study_arguments_json,
    &error);
  if (expect_success) {
    ok = (output != NULL) && (strcmp(output, "{}") == 0) && (error == NULL);
  } else {
    ok = (output == NULL) && (error != NULL) &&
      (expected_error != NULL) && (strstr(error, expected_error) != NULL);
  }
  if (!ok) {
    fprintf(stderr,
            "Database Study guard result did not match: output=%s error=%s\n",
            (output != NULL) ? output : "(null)",
            (error != NULL) ? error : "(null)");
  }
  free(output);
  free(error);
  return ok;
}

static int harness_run_database_study_same_batch_guard_tests(
  const harness_context *context)
{
  static const char schema_sql[] =
    "SELECT name FROM sqlite_schema WHERE type = 'table' ORDER BY name;";
  static const char real_table_sql[] =
    "SELECT id, sender FROM messages ORDER BY id LIMIT 1;";
  harness_study_research_call calls[2];
  cJSON *empty_items;
  cJSON *old_outputs;
  cJSON *prior_study_response;
  harness_study_research_call prior_study_call;
  char *study_arguments;
  char *error;
  long long session_id;
  long long research_call_id;
  long long save_call_id;
  int ok;

  if ((context == NULL) || (context->database_id == NULL)) {
    return 0;
  }

  memset(calls, 0, sizeof(calls));
  memset(&prior_study_call, 0, sizeof(prior_study_call));
  study_arguments = harness_study_arguments_json(
    context->database_id,
    NULL,
    "Messages contain senders and text.",
    "Read messages with a targeted, verified query.");
  error = NULL;
  session_id = 0LL;
  research_call_id = 0LL;
  save_call_id = 0LL;
  ok = (study_arguments != NULL) &&
    strappy_study_reset(context->catalog_path, &error) &&
    strappy_db_create_session(context->catalog_path, &session_id, &error) &&
    strappy_db_update_session_assistant_set(
      context->catalog_path,
      session_id,
      STRAPPY_ASSISTANT_SET_DATABASE_STUDY,
      &error);
  if (!ok) {
    fprintf(stderr,
            "Could not prepare Database Study guard tests: %s\n",
            (error != NULL) ? error : "allocation failed");
    goto cleanup;
  }

  ok = harness_study_record_research(
         context,
         session_id,
         0LL,
         "study-schema-batch",
         "study-schema-context",
         "study-schema-query",
         schema_sql,
         calls,
         &research_call_id,
         &error) &&
    harness_study_append_save_call(
      context,
      session_id,
      research_call_id,
      "study-schema-batch",
      calls,
      "study-schema-save",
      study_arguments,
      &save_call_id,
      &error) &&
    harness_study_save_result_matches(
      context,
      session_id,
      "study-schema-save",
      study_arguments,
      0,
      "real table") &&
    harness_expect_catalog_integer(context->catalog_path,
                                   "SELECT COUNT(*) FROM database_hints;",
                                   0LL,
                                   "schema-only rejected study values");
  harness_study_research_calls_destroy(calls, 2U);
  memset(calls, 0, sizeof(calls));
  if (!ok) {
    fprintf(stderr,
            "Database Study schema-only guard failed: %s\n",
            (error != NULL) ? error : "unexpected result");
    goto cleanup;
  }

  ok = harness_study_record_research(
    context,
    session_id,
    save_call_id,
    "study-prior-research-batch",
    "study-prior-context",
    "study-prior-query",
    real_table_sql,
    calls,
    &research_call_id,
    &error);
  old_outputs = ok ? harness_study_function_output_items(calls, 2U) : NULL;
  empty_items = cJSON_CreateArray();
  if (!ok || (old_outputs == NULL) || (empty_items == NULL) ||
      !harness_study_append_round(
        context->catalog_path,
        session_id,
        research_call_id,
        "study-prior-research-batch",
        "tool_continuation",
        1L,
        old_outputs,
        empty_items,
        &save_call_id,
        &error)) {
    cJSON_Delete(old_outputs);
    cJSON_Delete(empty_items);
    ok = 0;
    goto cleanup;
  }
  cJSON_Delete(old_outputs);
  cJSON_Delete(empty_items);

  prior_study_call.call_id = "study-prior-save";
  prior_study_call.tool_name = STRAPPY_TOOL_DATABASE_STUDY;
  prior_study_call.arguments_json = study_arguments;
  old_outputs = harness_study_user_input();
  prior_study_response =
    harness_study_function_call_items(&prior_study_call, 1U);
  if ((old_outputs == NULL) || (prior_study_response == NULL) ||
      !harness_study_append_round(
        context->catalog_path,
        session_id,
        save_call_id,
        "study-new-batch",
        "user",
        0L,
        old_outputs,
        prior_study_response,
        &save_call_id,
        &error)) {
    cJSON_Delete(old_outputs);
    cJSON_Delete(prior_study_response);
    ok = 0;
    goto cleanup;
  }
  cJSON_Delete(old_outputs);
  cJSON_Delete(prior_study_response);
  ok = harness_study_save_result_matches(
         context,
         session_id,
         "study-prior-save",
         study_arguments,
         0,
         "current study batch") &&
    harness_expect_catalog_integer(context->catalog_path,
                                   "SELECT COUNT(*) FROM database_hints;",
                                   0LL,
                                   "prior-batch rejected study values");
  harness_study_research_calls_destroy(calls, 2U);
  memset(calls, 0, sizeof(calls));
  if (!ok) {
    fprintf(stderr,
            "Database Study prior-batch query guard failed: %s\n",
            (error != NULL) ? error : "unexpected result");
    goto cleanup;
  }

  ok = harness_study_record_research(
         context,
         session_id,
         save_call_id,
         "study-valid-batch",
         "study-valid-context",
         "study-valid-query",
         real_table_sql,
         calls,
         &research_call_id,
         &error) &&
    harness_study_append_save_call(
      context,
      session_id,
      research_call_id,
      "study-valid-batch",
      calls,
      "study-valid-save",
      study_arguments,
      &save_call_id,
      &error) &&
    harness_study_save_result_matches(
      context,
      session_id,
      "study-valid-save",
      study_arguments,
      1,
      NULL) &&
    harness_expect_catalog_integer(context->catalog_path,
                                   "SELECT COUNT(*) FROM database_hints;",
                                   2LL,
                                   "same-batch accepted study values");
  if (!ok) {
    fprintf(stderr,
            "Database Study same-batch query guard failed: %s\n",
            (error != NULL) ? error : "unexpected result");
  }

cleanup:
  harness_study_research_calls_destroy(calls, 2U);
  if (session_id > 0LL) {
    char *delete_error;

    delete_error = NULL;
    if (!strappy_db_delete_session(context->catalog_path,
                                   session_id,
                                   &delete_error) &&
        ok) {
      fprintf(stderr,
              "Could not delete Database Study guard session: %s\n",
              (delete_error != NULL) ? delete_error : "unknown");
      ok = 0;
    }
    free(delete_error);
  }
  if (!strappy_study_reset(context->catalog_path, &error) && ok) {
    ok = 0;
  }
  free(study_arguments);
  free(error);
  return ok;
}

static int harness_run_database_study_coverage_tests(
  const harness_context *context)
{
  static const char *const prompt_keys[] = {
    "instruction", "application_groups"
  };
  static const char replacement_description[] =
    "Replacement coverage description";
  strappy_study_batch batch;
  strappy_study_database_id_list pending;
  strappy_study_database_id_list remaining;
  cJSON *application_groups;
  cJSON *prompt_root;
  char description[128];
  char study_context[128];
  char freshness_sql[512];
  char *error;
  size_t approved_count;
  size_t expected_batch_count;
  size_t index;
  size_t studied_count;
  int written;
  int ok;

  if (context == NULL) {
    return 0;
  }
  strappy_study_batch_init(&batch);
  strappy_study_database_id_list_init(&pending);
  strappy_study_database_id_list_init(&remaining);
  error = NULL;
  application_groups = NULL;
  prompt_root = NULL;
  approved_count = 0U;
  studied_count = 0U;
  ok = strappy_study_reset(context->catalog_path, &error) &&
    strappy_study_list_unstudied_database_ids(context->catalog_path,
                                               &pending,
                                               &error) &&
    strappy_study_progress(context->catalog_path,
                            &studied_count,
                            &approved_count,
                            &error) &&
    (pending.count > 0U) &&
    (studied_count == 0U) &&
    (approved_count == pending.count) &&
    harness_study_status_matches(context->catalog_path, &pending, 0, NULL) &&
    harness_study_status_rows_match(context->catalog_path, &pending, 0U);
  if (!ok) {
    fprintf(stderr,
            "Could not initialize Database Study coverage: %s\n",
            (error != NULL) ? error : "unexpected empty target list");
    goto cleanup;
  }

  if (!strappy_study_next_batch(context->catalog_path, &batch, &error)) {
    ok = 0;
    goto cleanup;
  }
  expected_batch_count = (pending.count < 3U) ? pending.count : 3U;
  prompt_root = (batch.prompt != NULL) ? cJSON_Parse(batch.prompt) : NULL;
  application_groups = cJSON_IsObject(prompt_root) ?
    cJSON_GetObjectItemCaseSensitive(prompt_root, "application_groups") : NULL;
  if ((batch.database_ids.count != expected_batch_count) ||
      (batch.pending_database_ids.count != expected_batch_count) ||
      !harness_object_has_exact_keys(
        prompt_root,
        prompt_keys,
        sizeof(prompt_keys) / sizeof(prompt_keys[0])) ||
      !cJSON_IsArray(application_groups) ||
      (cJSON_GetArraySize(application_groups) !=
       (int)expected_batch_count)) {
    fprintf(stderr,
            "Database Study singleton batch did not contain the next "
            "three-or-fewer application groups: %s\n",
            (batch.prompt != NULL) ? batch.prompt : "(null)");
    ok = 0;
    goto cleanup;
  }
  for (index = 0U; index < expected_batch_count; index++) {
    cJSON *database_ids;
    cJSON *group;

    group = cJSON_GetArrayItem(application_groups, (int)index);
    database_ids = cJSON_IsObject(group) ?
      cJSON_GetObjectItemCaseSensitive(group, "database_ids") : NULL;
    if (!cJSON_IsArray(database_ids) ||
        (cJSON_GetArraySize(database_ids) != 1) ||
        !harness_string_array_equals(
          database_ids,
          (const char * const *)&pending.database_ids[index],
          1U) ||
        (strcmp(batch.database_ids.database_ids[index],
                pending.database_ids[index]) != 0) ||
        (strcmp(batch.pending_database_ids.database_ids[index],
                pending.database_ids[index]) != 0)) {
      fprintf(stderr,
              "Database Study singleton batch IDs did not match pending "
              "database order.\n");
      ok = 0;
      goto cleanup;
    }
  }
  cJSON_Delete(prompt_root);
  prompt_root = NULL;
  application_groups = NULL;
  strappy_study_batch_destroy(&batch);

  for (index = 0U; index < pending.count; index++) {
    written = snprintf(description,
                       sizeof(description),
                       "Coverage description %u",
                       (unsigned int)index);
    if ((written <= 0) || ((size_t)written >= sizeof(description))) {
      ok = 0;
      goto cleanup;
    }
    written = snprintf(study_context,
                       sizeof(study_context),
                       "Coverage access context %u",
                       (unsigned int)index);
    if ((written <= 0) || ((size_t)written >= sizeof(study_context)) ||
        !harness_write_study_values(context->catalog_path,
                                    pending.database_ids[index],
                                    description,
                                    study_context)) {
      ok = 0;
      goto cleanup;
    }
  }

  ok = strappy_study_list_unstudied_database_ids(context->catalog_path,
                                                  &remaining,
                                                  &error) &&
    strappy_study_progress(context->catalog_path,
                            &studied_count,
                            &approved_count,
                            &error) &&
    (remaining.count == 0U) &&
    (studied_count == pending.count) &&
    (approved_count == pending.count) &&
    harness_study_status_matches(context->catalog_path, &pending, 1, NULL) &&
    harness_study_status_rows_match(context->catalog_path,
                                    &pending,
                                    pending.count) &&
    harness_expect_catalog_integer(
      context->catalog_path,
      "SELECT COUNT(*) FROM database_hints;",
      (long long)(pending.count * 2U),
      "two Database Study values per approved database");
  if (!ok) {
    fprintf(stderr,
            "Database Study did not reach 100%% approved-database coverage: "
            "%s\n",
            (error != NULL) ? error : "coverage mismatch");
    goto cleanup;
  }

  ok = strappy_study_delete_database_values(context->catalog_path,
                                             pending.database_ids[0],
                                             &error) &&
    strappy_study_list_unstudied_database_ids(context->catalog_path,
                                               &remaining,
                                               &error) &&
    strappy_study_progress(context->catalog_path,
                            &studied_count,
                            &approved_count,
                            &error) &&
    (remaining.count == 1U) &&
    (strcmp(remaining.database_ids[0], pending.database_ids[0]) == 0) &&
    (studied_count + 1U == approved_count) &&
    (approved_count == pending.count) &&
    harness_study_status_rows_match(context->catalog_path,
                                    &pending,
                                    pending.count - 1U) &&
    harness_expect_catalog_integer(
      context->catalog_path,
      "SELECT COUNT(*) FROM database_hints;",
      (long long)((pending.count * 2U) - 2U),
      "deleted Database Study values for one database");
  if (!ok) {
    fprintf(stderr,
            "Database Study did not delete only the selected database's "
            "values: %s\n",
            (error != NULL) ? error : "delete mismatch");
    goto cleanup;
  }

  strappy_study_database_id_list_destroy(&remaining);
  if (!harness_write_study_values(context->catalog_path,
                                  pending.database_ids[0],
                                  "Restored coverage description",
                                  "Restored coverage context") ||
      !strappy_study_list_unstudied_database_ids(context->catalog_path,
                                                  &remaining,
                                                  &error) ||
      (remaining.count != 0U) ||
      !harness_study_status_matches(context->catalog_path,
                                    &pending,
                                    1,
                                    NULL) ||
      !harness_study_status_rows_match(context->catalog_path,
                                       &pending,
                                       pending.count) ||
      !harness_expect_catalog_integer(
        context->catalog_path,
        "SELECT COUNT(*) FROM database_hints;",
        (long long)(pending.count * 2U),
        "restored Database Study values after deleting one database")) {
    fprintf(stderr,
            "Database Study did not restore the deleted database's values: "
            "%s\n",
            (error != NULL) ? error : "restore mismatch");
    ok = 0;
    goto cleanup;
  }
  strappy_study_database_id_list_destroy(&remaining);

  written = snprintf(
    freshness_sql,
    sizeof(freshness_sql),
    "UPDATE database_locations SET size_bytes = size_bytes + 1 "
    "WHERE active = 1 AND database_id = "
    "(SELECT id FROM databases WHERE assistant_database_id = '%s');",
    pending.database_ids[0]);
  if ((written <= 0) || ((size_t)written >= sizeof(freshness_sql)) ||
      !harness_expect_catalog_sql_ok(context->catalog_path,
                                     freshness_sql,
                                     "size-only Database Study freshness") ||
      !strappy_study_list_unstudied_database_ids(context->catalog_path,
                                                  &remaining,
                                                  &error) ||
      (remaining.count != 0U)) {
    fprintf(stderr,
            "Database Study incorrectly treated a size-only change as stale: "
            "%s\n",
            (error != NULL) ? error : "unexpected pending database");
    ok = 0;
    goto cleanup;
  }

  written = snprintf(
    freshness_sql,
    sizeof(freshness_sql),
    "UPDATE database_locations SET modified_at_s = modified_at_s + 1 "
    "WHERE active = 1 AND database_id = "
    "(SELECT id FROM databases WHERE assistant_database_id = '%s');",
    pending.database_ids[0]);
  if ((written <= 0) || ((size_t)written >= sizeof(freshness_sql)) ||
      !harness_expect_catalog_sql_ok(context->catalog_path,
                                     freshness_sql,
                                     "mtime Database Study freshness") ||
      !strappy_study_list_unstudied_database_ids(context->catalog_path,
                                                  &remaining,
                                                  &error) ||
      !strappy_study_progress(context->catalog_path,
                              &studied_count,
                              &approved_count,
                              &error) ||
      (remaining.count != 1U) ||
      (studied_count + 1U != approved_count) ||
      (approved_count != pending.count) ||
      !harness_study_status_rows_match(context->catalog_path,
                                       &pending,
                                       pending.count - 1U) ||
      (strcmp(remaining.database_ids[0], pending.database_ids[0]) != 0)) {
    fprintf(stderr,
            "Database Study did not treat an mtime change as stale: %s\n",
            (error != NULL) ? error : "pending database mismatch");
    ok = 0;
    goto cleanup;
  }
  strappy_study_database_id_list_destroy(&remaining);
  if (!harness_write_study_values(context->catalog_path,
                                  pending.database_ids[0],
                                  replacement_description,
                                  "Replacement coverage context") ||
      !strappy_study_list_unstudied_database_ids(context->catalog_path,
                                                  &remaining,
                                                  &error) ||
      (remaining.count != 0U) ||
      !harness_study_status_rows_match(context->catalog_path,
                                       &pending,
                                       pending.count)) {
    fprintf(stderr,
            "Database Study did not refresh values after an mtime change: %s\n",
            (error != NULL) ? error : "unexpected pending database");
    ok = 0;
    goto cleanup;
  }

  if (!harness_write_study_values(context->catalog_path,
                                  pending.database_ids[0],
                                  replacement_description,
                                  "Replacement coverage context") ||
      !harness_expect_catalog_integer(
        context->catalog_path,
        "SELECT COUNT(*) FROM database_hints;",
        (long long)(pending.count * 2U),
        "overwritten singleton Database Study values") ||
      !harness_study_status_matches(context->catalog_path,
                                    &pending,
                                    1,
                                    replacement_description)) {
    ok = 0;
    goto cleanup;
  }

  strappy_study_database_id_list_destroy(&remaining);
  ok = strappy_study_reset(context->catalog_path, &error) &&
    harness_expect_catalog_integer(context->catalog_path,
                                   "SELECT COUNT(*) FROM database_hints;",
                                   0LL,
                                   "reset Database Study values") &&
    strappy_study_list_unstudied_database_ids(context->catalog_path,
                                               &remaining,
                                               &error) &&
    strappy_study_progress(context->catalog_path,
                            &studied_count,
                            &approved_count,
                            &error) &&
    (remaining.count == pending.count) &&
    (studied_count == 0U) &&
    (approved_count == pending.count) &&
    harness_study_status_matches(context->catalog_path, &pending, 0, NULL) &&
    harness_study_status_rows_match(context->catalog_path, &pending, 0U);
  if (!ok) {
    fprintf(stderr,
            "Database Study reset did not restore all approved targets: %s\n",
            (error != NULL) ? error : "reset mismatch");
  }

cleanup:
  cJSON_Delete(prompt_root);
  free(error);
  strappy_study_batch_destroy(&batch);
  strappy_study_database_id_list_destroy(&remaining);
  strappy_study_database_id_list_destroy(&pending);
  return ok;
}

static int harness_run_session_options_tests(const harness_context *context)
{
  strappy_session_options default_patch;
  strappy_session_options future_options;
  strappy_session_options original_defaults;
  strappy_session_options saved_defaults;
  strappy_session_options stale;
  strappy_session_options saved;
  strappy_session_options reloaded;
  strappy_session_option_mask actual_changed_fields;
  const strappy_session_option_mask first_update_fields =
    STRAPPY_SESSION_OPTION_WEB_PROVIDER | STRAPPY_SESSION_OPTION_BASH;
  const strappy_session_option_mask final_update_fields =
    STRAPPY_SESSION_OPTION_LIMIT_TO_ONE_TOOL |
    STRAPPY_SESSION_OPTION_ANSWER_QUALITY |
    STRAPPY_SESSION_OPTION_ROUND_LIMIT |
    STRAPPY_SESSION_OPTION_WORKING_DIRECTORY;
  const strappy_session_option_mask default_update_fields =
    STRAPPY_SESSION_OPTION_ASSISTANT_SET |
    STRAPPY_SESSION_OPTION_WEB_PROVIDER |
    STRAPPY_SESSION_OPTION_WEB_SEARCH |
    STRAPPY_SESSION_OPTION_BASH |
    STRAPPY_SESSION_OPTION_LIMIT_TO_ONE_TOOL |
    STRAPPY_SESSION_OPTION_ANSWER_QUALITY |
    STRAPPY_SESSION_OPTION_ROUND_LIMIT |
    STRAPPY_SESSION_OPTION_WORKING_DIRECTORY;
  const char *fallback_working_directory;
  long long future_session_id;
  long long session_id;
  char *error;
  char *working_directory;
  int defaults_changed;
  int ok;

  if (context == NULL) {
    return 0;
  }

  strappy_session_options_init(&default_patch);
  strappy_session_options_init(&future_options);
  strappy_session_options_init(&original_defaults);
  strappy_session_options_init(&saved_defaults);
  strappy_session_options_init(&stale);
  strappy_session_options_init(&saved);
  strappy_session_options_init(&reloaded);
  fallback_working_directory = getenv("HOME");
  future_session_id = 0LL;
  session_id = 0LL;
  error = NULL;
  defaults_changed = 0;
  ok = strappy_db_create_session(context->catalog_path,
                                 &session_id,
                                 &error) &&
    strappy_db_load_session_options(context->catalog_path,
                                    session_id,
                                    &stale,
                                    &error);
  if (!ok) {
    fprintf(stderr,
            "Could not create the session-options fixture: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }
  if ((stale.model_id == NULL) || (stale.assistant_set_id == NULL) ||
      (stale.working_directory == NULL) ||
      (stale.web_provider != STRAPPY_WEB_PROVIDER_AUTO) ||
      !stale.web_search_enabled || stale.bash_enabled ||
      stale.limit_to_one_tool || stale.answer_quality_enabled ||
      (stale.round_limit != STRAPPY_SESSION_DEFAULT_ROUND_LIMIT)) {
    fprintf(stderr, "Initial session-options snapshot did not match.\n");
    ok = 0;
    goto cleanup;
  }

  stale.web_provider = STRAPPY_WEB_PROVIDER_PARALLEL;
  stale.bash_enabled = 1;
  actual_changed_fields = 0U;
  ok = strappy_db_update_session_options(context->catalog_path,
                                         session_id,
                                         &stale,
                                         first_update_fields,
                                         &saved,
                                         &actual_changed_fields,
                                         &error);
  if (!ok || (actual_changed_fields != first_update_fields) ||
      (saved.web_provider != STRAPPY_WEB_PROVIDER_PARALLEL) ||
      !saved.bash_enabled || !saved.web_search_enabled) {
    fprintf(stderr,
            "Could not atomically save the first options patch: %s\n",
            (error != NULL) ? error : "snapshot mismatch");
    ok = 0;
    goto cleanup;
  }

  /* Simulate an older UI snapshot. Only the masked field may be merged. */
  stale.web_provider = STRAPPY_WEB_PROVIDER_AUTO;
  stale.bash_enabled = 0;
  stale.web_search_enabled = 0;
  stale.round_limit = 1L;
  actual_changed_fields = 0U;
  ok = strappy_db_update_session_options(context->catalog_path,
                                         session_id,
                                         &stale,
                                         STRAPPY_SESSION_OPTION_WEB_SEARCH,
                                         &saved,
                                         &actual_changed_fields,
                                         &error);
  if (!ok ||
      (actual_changed_fields != STRAPPY_SESSION_OPTION_WEB_SEARCH) ||
      (saved.web_provider != STRAPPY_WEB_PROVIDER_PARALLEL) ||
      !saved.bash_enabled || saved.web_search_enabled ||
      (saved.round_limit != STRAPPY_SESSION_DEFAULT_ROUND_LIMIT)) {
    fprintf(stderr,
            "A stale options patch overwrote an unmasked field: %s\n",
            (error != NULL) ? error : "snapshot mismatch");
    ok = 0;
    goto cleanup;
  }

  working_directory = strdup(context->temp_dir);
  if (working_directory == NULL) {
    fprintf(stderr, "Could not allocate the options working directory.\n");
    ok = 0;
    goto cleanup;
  }
  free(stale.working_directory);
  stale.working_directory = working_directory;
  stale.limit_to_one_tool = 1;
  stale.answer_quality_enabled = 1;
  stale.round_limit = 9L;
  actual_changed_fields = 0U;
  ok = strappy_db_update_session_options(context->catalog_path,
                                         session_id,
                                         &stale,
                                         final_update_fields,
                                         &saved,
                                         &actual_changed_fields,
                                         &error);
  if (!ok || (actual_changed_fields != final_update_fields) ||
      !saved.limit_to_one_tool || !saved.answer_quality_enabled ||
      (saved.round_limit != 9L) ||
      (saved.working_directory == NULL) ||
      (strcmp(saved.working_directory, context->temp_dir) != 0) ||
      (saved.web_provider != STRAPPY_WEB_PROVIDER_PARALLEL) ||
      !saved.bash_enabled) {
    fprintf(stderr,
            "Could not save the multi-field options patch: %s\n",
            (error != NULL) ? error : "snapshot mismatch");
    ok = 0;
    goto cleanup;
  }

  actual_changed_fields = STRAPPY_SESSION_OPTION_ALL;
  ok = strappy_db_update_session_options(context->catalog_path,
                                         session_id,
                                         &stale,
                                         STRAPPY_SESSION_OPTION_WEB_SEARCH,
                                         &saved,
                                         &actual_changed_fields,
                                         &error);
  if (!ok || (actual_changed_fields != 0U) ||
      (saved.web_provider != STRAPPY_WEB_PROVIDER_PARALLEL) ||
      !saved.bash_enabled || saved.web_search_enabled ||
      !saved.answer_quality_enabled ||
      (saved.round_limit != 9L)) {
    fprintf(stderr,
            "A no-op options patch returned the wrong snapshot: %s\n",
            (error != NULL) ? error : "snapshot mismatch");
    ok = 0;
    goto cleanup;
  }

  stale.web_provider = (strappy_web_provider)99;
  stale.bash_enabled = 0;
  free(error);
  error = NULL;
  if (strappy_db_update_session_options(
        context->catalog_path,
        session_id,
        &stale,
        STRAPPY_SESSION_OPTION_WEB_PROVIDER | STRAPPY_SESSION_OPTION_BASH,
        &saved,
        &actual_changed_fields,
        &error) || (error == NULL)) {
    fprintf(stderr, "An invalid options patch was accepted.\n");
    ok = 0;
    goto cleanup;
  }
  free(error);
  error = NULL;
  stale.round_limit = STRAPPY_SESSION_MAX_LIMIT + 1L;
  if (strappy_db_update_session_options(
        context->catalog_path,
        session_id,
        &stale,
        STRAPPY_SESSION_OPTION_ROUND_LIMIT,
        &saved,
        &actual_changed_fields,
        &error) || (error == NULL)) {
    fprintf(stderr, "An invalid round limit was accepted.\n");
    ok = 0;
    goto cleanup;
  }
  free(error);
  error = NULL;
  ok = strappy_db_load_session_options(context->catalog_path,
                                       session_id,
                                       &reloaded,
                                       &error);
  if (!ok || (reloaded.web_provider != STRAPPY_WEB_PROVIDER_PARALLEL) ||
      !reloaded.bash_enabled || reloaded.web_search_enabled ||
      !reloaded.limit_to_one_tool || !reloaded.answer_quality_enabled ||
      (reloaded.round_limit != 9L) ||
      (reloaded.working_directory == NULL) ||
      (strcmp(reloaded.working_directory, context->temp_dir) != 0)) {
    fprintf(stderr,
            "The rejected options patch changed persisted state: %s\n",
            (error != NULL) ? error : "snapshot mismatch");
    ok = 0;
    goto cleanup;
  }

  if ((fallback_working_directory == NULL) ||
      (fallback_working_directory[0] == '\0') ||
      !strappy_db_load_default_session_options(context->catalog_path,
                                               fallback_working_directory,
                                               &original_defaults,
                                               &error) ||
      !strappy_db_load_default_session_options(context->catalog_path,
                                               fallback_working_directory,
                                               &default_patch,
                                               &error)) {
    fprintf(stderr,
            "Could not load the default session-options fixture: %s\n",
            (error != NULL) ? error : "unknown");
    ok = 0;
    goto cleanup;
  }

  free(default_patch.assistant_set_id);
  default_patch.assistant_set_id =
    strdup(STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE);
  free(default_patch.working_directory);
  default_patch.working_directory = strdup(context->temp_dir);
  if ((default_patch.assistant_set_id == NULL) ||
      (default_patch.working_directory == NULL)) {
    fprintf(stderr, "Could not allocate the default-options patch.\n");
    ok = 0;
    goto cleanup;
  }
  default_patch.web_provider = STRAPPY_WEB_PROVIDER_NATIVE;
  default_patch.web_search_enabled = 0;
  default_patch.bash_enabled = 1;
  default_patch.limit_to_one_tool = 1;
  default_patch.answer_quality_enabled = 1;
  default_patch.round_limit = 13L;
  actual_changed_fields = 0U;
  ok = strappy_db_update_default_session_options(
    context->catalog_path,
    fallback_working_directory,
    &default_patch,
    default_update_fields,
    &saved_defaults,
    &actual_changed_fields,
    &error);
  if (ok) {
    defaults_changed = 1;
  }
  if (!ok || (actual_changed_fields != default_update_fields) ||
      (saved_defaults.model_id == NULL) ||
      (strcmp(saved_defaults.model_id, original_defaults.model_id) != 0) ||
      (saved_defaults.assistant_set_id == NULL) ||
      (strcmp(saved_defaults.assistant_set_id,
              STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE) != 0) ||
      (saved_defaults.web_provider != STRAPPY_WEB_PROVIDER_NATIVE) ||
      saved_defaults.web_search_enabled || !saved_defaults.bash_enabled ||
      !saved_defaults.limit_to_one_tool ||
      !saved_defaults.answer_quality_enabled ||
      (saved_defaults.round_limit != 13L) ||
      (saved_defaults.working_directory == NULL) ||
      (strcmp(saved_defaults.working_directory, context->temp_dir) != 0)) {
    fprintf(stderr,
            "Could not atomically save default session options: %s\n",
            (error != NULL) ? error : "snapshot mismatch");
    ok = 0;
    goto cleanup;
  }

  ok = strappy_db_load_session_options(context->catalog_path,
                                       session_id,
                                       &reloaded,
                                       &error);
  if (!ok || (reloaded.web_provider != STRAPPY_WEB_PROVIDER_PARALLEL) ||
      !reloaded.bash_enabled || reloaded.web_search_enabled ||
      !reloaded.limit_to_one_tool || !reloaded.answer_quality_enabled ||
      (reloaded.round_limit != 9L)) {
    fprintf(stderr,
            "Changing defaults modified an existing session: %s\n",
            (error != NULL) ? error : "snapshot mismatch");
    ok = 0;
    goto cleanup;
  }

  ok = strappy_db_create_session(context->catalog_path,
                                 &future_session_id,
                                 &error) &&
    strappy_db_load_session_options(context->catalog_path,
                                    future_session_id,
                                    &future_options,
                                    &error);
  if (!ok || (future_options.model_id == NULL) ||
      (strcmp(future_options.model_id, original_defaults.model_id) != 0) ||
      (future_options.assistant_set_id == NULL) ||
      (strcmp(future_options.assistant_set_id,
              STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE) != 0) ||
      (future_options.web_provider != STRAPPY_WEB_PROVIDER_NATIVE) ||
      future_options.web_search_enabled || !future_options.bash_enabled ||
      !future_options.limit_to_one_tool ||
      !future_options.answer_quality_enabled ||
      (future_options.round_limit != 13L) ||
      (future_options.working_directory == NULL) ||
      (strcmp(future_options.working_directory, context->temp_dir) != 0)) {
    fprintf(stderr,
            "A new session did not copy the default options snapshot: %s\n",
            (error != NULL) ? error : "snapshot mismatch");
    ok = 0;
  }

cleanup:
  if (defaults_changed) {
    free(error);
    error = NULL;
    if (!strappy_db_update_default_session_options(
          context->catalog_path,
          fallback_working_directory,
          &original_defaults,
          STRAPPY_SESSION_OPTION_ALL,
          &saved_defaults,
          &actual_changed_fields,
          &error)) {
      fprintf(stderr,
              "Could not restore default session options: %s\n",
              (error != NULL) ? error : "unknown");
      ok = 0;
    }
    if (!harness_expect_catalog_sql_ok(
          context->catalog_path,
          "UPDATE default_session_options SET working_directory = NULL "
          "WHERE id = 1;",
          "restored default working-directory fallback")) {
      ok = 0;
    }
  }
  free(error);
  strappy_session_options_destroy(&saved_defaults);
  strappy_session_options_destroy(&original_defaults);
  strappy_session_options_destroy(&future_options);
  strappy_session_options_destroy(&default_patch);
  strappy_session_options_destroy(&reloaded);
  strappy_session_options_destroy(&saved);
  strappy_session_options_destroy(&stale);
  return ok;
}

static int harness_run_empty_session_storage_tests(const harness_context *context)
{
  strappy_session_record session;
  strappy_session_message_record_list messages;
  long long session_id;
  char *error;
  char *output;
  char *assistant_set_id;
  char *working_directory;
  const char *home_directory;
  sqlite3 *db;
  char assistant_set_turn_sql[512];
  int bash_enabled;
  int web_provider_value;
  int written;
  int ok;

  if (context == NULL) {
    return 0;
  }

  error = NULL;
  session_id = 0LL;
  ok = strappy_db_create_session(context->catalog_path, &session_id, &error);
  if (!ok) {
    fprintf(stderr,
            "Could not create empty session: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  home_directory = getenv("HOME");
  working_directory = NULL;
  error = NULL;
  if (!strappy_db_get_session_working_directory(context->catalog_path,
                                                 session_id,
                                                 &working_directory,
                                                 &error) ||
      (home_directory == NULL) || (working_directory == NULL) ||
      (strcmp(working_directory, home_directory) != 0)) {
    fprintf(stderr,
            "New session did not persist its home working directory: %s\n",
            (error != NULL) ? error : "unknown");
    free(working_directory);
    strappy_free_string(error);
    return 0;
  }
  free(working_directory);

  strappy_session_record_init(&session);
  ok = strappy_db_load_session(context->catalog_path,
                               session_id,
                               &session,
                               &error);
  if (!ok) {
    fprintf(stderr,
            "Could not load empty session: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_record_destroy(&session);
    return 0;
  }

  ok = (session.session_id == session_id) &&
       (session.name != NULL) &&
       (strcmp(session.name, "") == 0) &&
       (session.prompt != NULL) &&
       (strcmp(session.prompt, "") == 0) &&
       (session.response != NULL) &&
       (strcmp(session.response, "") == 0) &&
       (session.assistant_set_id != NULL) &&
       (strcmp(session.assistant_set_id,
               STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT) == 0) &&
       (session.web_provider == STRAPPY_WEB_PROVIDER_AUTO) &&
       (session.web_search_enabled == 1) &&
       (session.bash_enabled == 0) &&
       (session.limit_to_one_tool == 0) &&
       (session.round_limit == STRAPPY_SESSION_DEFAULT_ROUND_LIMIT) &&
       (session.http_status == 0L);
  strappy_session_record_destroy(&session);
  if (!ok) {
    fprintf(stderr, "Empty session row did not have the expected shape.\n");
    return 0;
  }

  error = NULL;
  if (!strappy_db_update_session_assistant_set(
        context->catalog_path,
        session_id,
        STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE,
        &error)) {
    fprintf(stderr,
            "Could not select World Knowledge on an empty session: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  assistant_set_id = NULL;
  if (!strappy_db_get_session_assistant_set(context->catalog_path,
                                            session_id,
                                            &assistant_set_id,
                                            &error) ||
      (assistant_set_id == NULL) ||
      (strcmp(assistant_set_id,
              STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE) != 0)) {
    fprintf(stderr,
            "World Knowledge session selection was not stored: %s\n",
            (error != NULL) ? error : "unknown");
    free(assistant_set_id);
    strappy_free_string(error);
    return 0;
  }
  free(assistant_set_id);

  error = NULL;
  if (!strappy_db_update_session_bash_enabled(context->catalog_path,
                                              session_id,
                                              1,
                                              &error) ||
      !strappy_db_get_session_bash_enabled(context->catalog_path,
                                           session_id,
                                           &bash_enabled,
                                           &error) ||
      !bash_enabled) {
    fprintf(stderr,
            "Bash could not be enabled outside the Coding Assistant: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  if (!strappy_db_update_session_assistant_set(
        context->catalog_path,
        session_id,
        STRAPPY_ASSISTANT_SET_CODING_ASSISTANT,
        &error) ||
      !strappy_db_get_session_bash_enabled(context->catalog_path,
                                           session_id,
                                           &bash_enabled,
                                           &error) ||
      !bash_enabled ||
      !strappy_db_update_session_assistant_set(
        context->catalog_path,
        session_id,
        STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE,
        &error) ||
      !strappy_db_get_session_bash_enabled(context->catalog_path,
                                           session_id,
                                           &bash_enabled,
                                           &error) ||
      !bash_enabled ||
      !strappy_db_update_session_bash_enabled(context->catalog_path,
                                              session_id,
                                              0,
                                              &error) ||
      !strappy_db_get_session_bash_enabled(context->catalog_path,
                                           session_id,
                                           &bash_enabled,
                                           &error) ||
      bash_enabled) {
    fprintf(stderr,
            "Session Bash setting did not survive assistant selection: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  error = NULL;
  ok = strappy_db_update_session_limit_to_one_tool(context->catalog_path,
                                                   session_id,
                                                   1,
                                                   &error);
  if (!ok) {
    fprintf(stderr,
            "Could not update session single-tool setting: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  strappy_session_record_init(&session);
  ok = strappy_db_load_session(context->catalog_path,
                               session_id,
                               &session,
                               &error);
  if (!ok) {
    fprintf(stderr,
            "Could not reload session single-tool setting: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_record_destroy(&session);
    return 0;
  }
  ok = (session.limit_to_one_tool == 1) &&
       (session.round_limit == STRAPPY_SESSION_DEFAULT_ROUND_LIMIT);
  strappy_session_record_destroy(&session);
  if (!ok) {
    fprintf(stderr, "Session single-tool setting was not stored.\n");
    return 0;
  }

  if (!strappy_db_update_session_web_search_enabled(context->catalog_path,
                                                     session_id,
                                                     0,
                                                     &error)) {
    fprintf(stderr,
            "Could not disable session web search: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  for (web_provider_value = (int)STRAPPY_WEB_PROVIDER_AUTO;
       web_provider_value <= (int)STRAPPY_WEB_PROVIDER_PARALLEL;
       web_provider_value++) {
    error = NULL;
    ok = strappy_db_update_session_web_provider(
      context->catalog_path,
      session_id,
      (strappy_web_provider)web_provider_value,
      &error);
    if (!ok) {
      fprintf(stderr,
              "Could not update session web provider: %s\n",
              (error != NULL) ? error : "unknown");
      strappy_free_string(error);
      return 0;
    }
  }
  error = NULL;
  if (strappy_db_update_session_web_provider(
        context->catalog_path,
        session_id,
        (strappy_web_provider)99,
        &error) || (error == NULL)) {
    fprintf(stderr, "Invalid session web provider was accepted.\n");
    strappy_free_string(error);
    return 0;
  }
  strappy_free_string(error);
  error = NULL;

  strappy_session_record_init(&session);
  ok = strappy_db_load_session(context->catalog_path,
                               session_id,
                               &session,
                               &error);
  if (!ok) {
    fprintf(stderr,
            "Could not reload web search session setting: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_record_destroy(&session);
    return 0;
  }
  ok = (session.web_provider == STRAPPY_WEB_PROVIDER_PARALLEL) &&
       (session.web_search_enabled == 0);
  strappy_session_record_destroy(&session);
  if (!ok) {
    fprintf(stderr,
            "Provider selection unexpectedly enabled session web search.\n");
    return 0;
  }
  if (!strappy_db_update_session_web_search_enabled(context->catalog_path,
                                                     session_id,
                                                     1,
                                                     &error)) {
    fprintf(stderr,
            "Could not enable session web search: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  if (!harness_expect_session_error_contains(
        context->catalog_path,
        session_id,
        STRAPPY_TOOL_SESSION_RENAME,
        NULL,
        "requires a non-empty name string") ||
      !harness_expect_session_error_contains(
        context->catalog_path,
        session_id,
        STRAPPY_TOOL_SESSION_RENAME,
        "{}",
        "requires a non-empty name string") ||
      !harness_expect_session_error_contains(
        context->catalog_path,
        session_id,
        STRAPPY_TOOL_SESSION_RENAME,
        "{\"name\":null}",
        "name must be a string") ||
      !harness_expect_session_error_contains(
        context->catalog_path,
        session_id,
        STRAPPY_TOOL_SESSION_RENAME,
        "{\"name\":\"\"}",
        "requires a non-empty name string")) {
    return 0;
  }

  error = NULL;
  output = strappy_tools_execute(context->catalog_path,
                                 session_id,
                                 HARNESS_RESOURCE_DIR,
                                 STRAPPY_TOOL_SESSION_RENAME,
                                 "{\"name\":\"  Find   Receipts  \"}",
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Could not write session name: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  ok = (strcmp(output, "{}") == 0);
  free(output);
  if (!ok) {
    fprintf(stderr, "Session name tool did not return an empty result.\n");
    return 0;
  }

  strappy_session_record_init(&session);
  ok = strappy_db_load_session(context->catalog_path,
                               session_id,
                               &session,
                               &error);
  if (!ok) {
    fprintf(stderr,
            "Could not load named empty session: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_record_destroy(&session);
    return 0;
  }
  ok = (session.name != NULL) &&
       (strcmp(session.name, "Find Receipts") == 0);
  strappy_session_record_destroy(&session);
  if (!ok) {
    fprintf(stderr, "Session name was not stored.\n");
    return 0;
  }

  error = NULL;
  output = strappy_tools_execute(context->catalog_path,
                                 session_id,
                                 HARNESS_RESOURCE_DIR,
                                 STRAPPY_TOOL_SESSION_RENAME,
                                 "{\"name\":\"Different Title\"}",
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Could not run second session name write: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  ok = (strcmp(output, "{}") == 0);
  free(output);
  if (!ok) {
    fprintf(stderr,
            "Second session name write did not return an empty result.\n");
    return 0;
  }

  strappy_session_record_init(&session);
  ok = strappy_db_load_session(context->catalog_path,
                               session_id,
                               &session,
                               &error);
  if (!ok) {
    fprintf(stderr,
            "Could not load renamed session: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_record_destroy(&session);
    return 0;
  }
  ok = (session.name != NULL) &&
       (strcmp(session.name, "Different Title") == 0);
  strappy_session_record_destroy(&session);
  if (!ok) {
    fprintf(stderr, "Session name was not updated.\n");
    return 0;
  }

  db = NULL;
  written = snprintf(
    assistant_set_turn_sql,
    sizeof(assistant_set_turn_sql),
    "INSERT INTO turns "
    "(session_id, ordinal, prompt_group_key, state, created_at_ms) "
    "VALUES (%lld, 0, 'assistant-set-switch-test', 'completed', 1);",
    session_id);
  if ((written <= 0) ||
      ((size_t)written >= sizeof(assistant_set_turn_sql)) ||
      (sqlite3_open(context->catalog_path, &db) != SQLITE_OK) ||
      !harness_exec_sql(db, assistant_set_turn_sql)) {
    fprintf(stderr, "Could not create assistant-set switch turn.\n");
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }
  sqlite3_close(db);
  error = NULL;
  if (!strappy_db_update_session_assistant_set(
        context->catalog_path,
        session_id,
        STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT,
        &error)) {
    fprintf(stderr,
            "Could not change assistant set after the first turn: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  assistant_set_id = NULL;
  if (!strappy_db_get_session_assistant_set(context->catalog_path,
                                            session_id,
                                            &assistant_set_id,
                                            &error) ||
      (assistant_set_id == NULL) ||
      (strcmp(assistant_set_id,
              STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT) != 0)) {
    fprintf(stderr,
            "Post-turn assistant-set change was not stored: %s\n",
            (error != NULL) ? error : "unknown");
    free(assistant_set_id);
    strappy_free_string(error);
    return 0;
  }
  free(assistant_set_id);

  strappy_session_message_record_list_init(&messages);
  ok = strappy_db_list_response_timeline(context->catalog_path,
                                         session_id,
                                         &messages,
                                         &error);
  if (!ok) {
    fprintf(stderr,
            "Could not list empty session messages: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_message_record_list_destroy(&messages);
    return 0;
  }

  ok = (messages.count == 0U);
  strappy_session_message_record_list_destroy(&messages);
  if (!ok) {
    fprintf(stderr, "Empty session unexpectedly had messages.\n");
    return 0;
  }

  return 1;
}
static int harness_run_bundled_model_catalog_tests(
  const harness_context *context)
{
  static const char *revision_three_json =
    "{"
      "\"schema_version\":1,"
      "\"catalog_revision\":3,"
      "\"catalog_source\":\"bundled\","
      "\"models\":[{"
        "\"provider_id\":\"openai_chatgpt\","
        "\"provider_account_id\":\"openai_chatgpt\","
        "\"model_id\":\"openai_chatgpt:gpt-5.4\","
        "\"wire_model_id\":\"gpt-5.4\","
        "\"display_name\":\"GPT-5.4 Updated\","
        "\"catalog_active\":true,"
        "\"billing_kind\":\"chatgpt_plan\","
        "\"input_modalities\":[\"text\",\"image\"],"
        "\"context_window_tokens\":272000,"
        "\"max_output_tokens\":128000,"
        "\"capabilities\":{"
          "\"reasoning\":true,"
          "\"local_functions\":true,"
          "\"hosted_tools\":[]"
        "},"
        "\"reasoning_levels\":[\"off\",\"low\",\"high\"],"
        "\"reasoning_level_overrides\":{\"minimal\":\"low\"}"
      "}]"
    "}";
  static const char *invalid_revision_json =
    "{"
      "\"schema_version\":1,"
      "\"catalog_revision\":4,"
      "\"catalog_source\":\"bundled\","
      "\"unexpected\":true,"
      "\"models\":[]"
    "}";
  static const char *selected_model_id = "openai_chatgpt:gpt-5.4";
  char database_path[1400];
  strappy_model_record_list list;
  strappy_model_route_record route;
  char *error;
  long long session_id;
  size_t index;
  int found_model;
  int ok;

  if ((context == NULL) ||
      !harness_join_path(database_path,
                         sizeof(database_path),
                         context->temp_dir,
                         "bundled-models.sqlite")) {
    return 0;
  }
  harness_unlink_sqlite_files(database_path);
  error = NULL;
  ok = strappy_db_initialize(database_path, &error) &&
    strappy_model_catalog_import_bundled_models(HARNESS_RESOURCE_DIR,
                                                 database_path,
                                                 &error);
  if (!ok) {
    fprintf(stderr,
            "Could not import bundled model catalog: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    harness_unlink_sqlite_files(database_path);
    return 0;
  }
  ok = harness_expect_catalog_integer(
         database_path,
         "SELECT COUNT(*) FROM models WHERE provider_account_id = "
           "'openai_chatgpt' AND catalog_active = 1;",
         7LL,
         "initial bundled ChatGPT model count") &&
       harness_expect_catalog_integer(
         database_path,
         "SELECT catalog_revision FROM bundled_model_catalogs WHERE "
           "provider_account_id = 'openai_chatgpt';",
         2LL,
         "initial bundled catalog revision") &&
       harness_expect_catalog_integer(
         database_path,
         "SELECT COUNT(*) FROM model_capabilities WHERE "
           "billing_kind = 'chatgpt_plan' AND hosted_tools_enabled = 1;",
         7LL,
         "bundled plan capability count") &&
       harness_expect_catalog_integer(
         database_path,
         "SELECT COUNT(*) FROM model_features WHERE "
           "model_id = 'openai_chatgpt:gpt-5.4' AND "
           "feature_kind = 'input_modality' AND feature_value = 'image';",
         1LL,
         "bundled image capability");
  if (!ok) {
    harness_unlink_sqlite_files(database_path);
    return 0;
  }

  if (!strappy_db_set_model_allowed(database_path,
                                    selected_model_id,
                                    1,
                                    &error) ||
      !strappy_db_import_bundled_models_json(database_path,
                                             revision_three_json,
                                             &error)) {
    fprintf(stderr,
            "Could not update bundled model catalog: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    harness_unlink_sqlite_files(database_path);
    return 0;
  }
  ok = harness_expect_catalog_integer(
         database_path,
         "SELECT COUNT(*) FROM models WHERE provider_account_id = "
           "'openai_chatgpt' AND catalog_active = 1;",
         1LL,
         "revision-scoped bundled model count") &&
       harness_expect_catalog_integer(
         database_path,
         "SELECT COUNT(*) FROM model_preferences WHERE "
           "model_id = 'openai_chatgpt:gpt-5.4' AND allowed = 1;",
         1LL,
         "preserved bundled model preference") &&
       harness_expect_catalog_integer(
         database_path,
         "SELECT catalog_revision FROM bundled_model_catalogs WHERE "
           "provider_account_id = 'openai_chatgpt';",
         3LL,
         "updated bundled catalog revision");
  if (!ok) {
    harness_unlink_sqlite_files(database_path);
    return 0;
  }

  strappy_free_string(error);
  error = NULL;
  if (strappy_db_import_bundled_models_json(database_path,
                                            invalid_revision_json,
                                            &error) ||
      (error == NULL) || (strstr(error, "unsupported field") == NULL)) {
    fprintf(stderr, "Invalid bundled model catalog was accepted.\n");
    strappy_free_string(error);
    harness_unlink_sqlite_files(database_path);
    return 0;
  }
  strappy_free_string(error);
  error = NULL;
  if (!strappy_model_catalog_import_bundled_models(HARNESS_RESOURCE_DIR,
                                                   database_path,
                                                   &error)) {
    fprintf(stderr,
            "Bundled catalog downgrade guard failed: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    harness_unlink_sqlite_files(database_path);
    return 0;
  }

  strappy_model_record_list_init(&list);
  if (!strappy_db_list_models(database_path, &list, &error)) {
    fprintf(stderr,
            "Could not list bundled models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_record_list_destroy(&list);
    harness_unlink_sqlite_files(database_path);
    return 0;
  }
  found_model = 0;
  for (index = 0U; index < list.count; index++) {
    const strappy_model_record *record;

    record = &list.records[index];
    if ((record->model_id != NULL) &&
        (strcmp(record->model_id, selected_model_id) == 0)) {
      found_model = (record->billing_kind != NULL) &&
        (strcmp(record->billing_kind, "chatgpt_plan") == 0) &&
        record->reasoning_enabled && record->local_functions_enabled &&
        !record->hosted_tools_enabled && record->allowed &&
        (record->name != NULL) &&
        (strcmp(record->name, "GPT-5.4 Updated") == 0);
    }
  }
  ok = found_model;
  strappy_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "Bundled model capability row did not match.\n");
    harness_unlink_sqlite_files(database_path);
    return 0;
  }

  session_id = 0LL;
  strappy_model_route_record_init(&route);
  ok = strappy_db_create_session(database_path, &session_id, &error) &&
    strappy_db_update_session_model(database_path,
                                    session_id,
                                    selected_model_id,
                                    &error) &&
    strappy_db_get_session_model_route(database_path,
                                       session_id,
                                       &route,
                                       &error) &&
    (route.billing_kind != NULL) &&
    (strcmp(route.billing_kind, "chatgpt_plan") == 0) &&
    route.reasoning_enabled && route.local_functions_enabled &&
    !route.hosted_tools_enabled;
  strappy_model_route_record_destroy(&route);
  if (!ok) {
    fprintf(stderr,
            "Bundled model route did not retain capabilities: %s\n",
            (error != NULL) ? error : "unknown");
  }
  strappy_free_string(error);
  harness_unlink_sqlite_files(database_path);
  return ok;
}

static int harness_run_openrouter_model_catalog_tests(
  const harness_context *context)
{
  static const char *gemma_model_id =
    STRAPPY_PROVIDER_ACCOUNT_OPENROUTER ":google/gemma-4-31b-it";
  static const char *openai_model_id =
    STRAPPY_PROVIDER_ACCOUNT_OPENROUTER ":openai/gpt-4.1-mini";
  static const char *models_json =
    "{"
    "\"data\":["
    "{"
    "\"id\":\"google/gemma-4-31b-it\","
    "\"canonical_slug\":\"google/gemma-4-31b-it\","
    "\"hugging_face_id\":\"google/gemma-4-31b-it\","
    "\"name\":\"Gemma 4 31B IT\","
    "\"description\":\"Harness model\","
    "\"knowledge_cutoff\":\"2025-01\","
    "\"expiration_date\":null,"
    "\"context_length\":131072,"
    "\"created\":1760000000,"
    "\"architecture\":{"
    "\"modality\":\"text->text\","
    "\"tokenizer\":\"Gemini\","
    "\"instruct_type\":\"gemma\","
    "\"input_modalities\":[\"text\"],"
    "\"output_modalities\":[\"text\"]"
    "},"
    "\"pricing\":{"
    "\"prompt\":\"0.00000015\","
    "\"completion\":\"0.00000045\","
    "\"request\":\"0\","
    "\"image\":\"0\","
    "\"audio\":\"0\","
    "\"web_search\":\"0\","
    "\"internal_reasoning\":\"0\","
    "\"input_cache_read\":\"0.00000001\","
    "\"input_cache_write\":\"0.00000002\""
    "},"
    "\"top_provider\":{"
    "\"context_length\":131072,"
    "\"max_completion_tokens\":8192,"
    "\"is_moderated\":true"
    "},"
    "\"default_parameters\":{\"temperature\":0.2},"
    "\"per_request_limits\":{\"prompt_tokens\":\"1000\"},"
    "\"links\":{\"details\":\"https://openrouter.ai/models/google/gemma\"},"
    "\"reasoning\":{\"effort\":\"medium\"},"
    "\"benchmarks\":{\"mmlu\":0.91},"
    "\"supported_parameters\":[\"tools\",\"temperature\"],"
    "\"supported_voices\":[\"alloy\"]"
    "},"
    "{"
    "\"id\":\"openai/gpt-4.1-mini\","
    "\"name\":\"GPT 4.1 Mini\","
    "\"context_length\":1048576,"
    "\"architecture\":{\"input_modalities\":[\"text\",\"image\"],"
    "\"output_modalities\":[\"text\"]},"
    "\"pricing\":{\"prompt\":\"0.0000004\",\"completion\":\"0.0000016\"},"
    "\"supported_parameters\":[\"tools\"]"
    "}"
    "]"
    "}";
  strappy_model_record_list list;
  strappy_session_record session_list_record;
  strappy_session_options default_options;
  strappy_session_options saved_default_options;
  strappy_session_option_mask actual_changed_fields;
  const char *fallback_working_directory;
  char *default_model;
  char *session_model;
  char *error;
  long long session_id;
  long long initial_activity_at_ms;
  size_t index;
  int found_builtin_default;
  int found_gemma;
  int found_openai;
  int ok;

  if (context == NULL) {
    return 0;
  }

  error = NULL;
  if (!strappy_db_save_openrouter_models_json(context->catalog_path,
                                              models_json,
                                              &error)) {
    fprintf(stderr,
            "Could not save OpenRouter model JSON: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  strappy_model_record_list_init(&list);
  if (!strappy_db_list_models(context->catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list OpenRouter models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_record_list_destroy(&list);
    return 0;
  }

  found_builtin_default = 0;
  found_gemma = 0;
  found_openai = 0;
  for (index = 0U; index < list.count; index++) {
    strappy_model_record *record;

    record = &list.records[index];
    if ((record->model_id != NULL) &&
        (strcmp(record->model_id,
                STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER) == 0)) {
      found_builtin_default = record->selected && record->allowed &&
        (record->wire_model_id != NULL) &&
        (strcmp(record->wire_model_id, STRAPPY_CONFIG_DEFAULT_API_MODEL) == 0) &&
        (record->provider_account_id != NULL) &&
        (strcmp(record->provider_account_id,
                STRAPPY_PROVIDER_ACCOUNT_OPENROUTER) == 0) &&
        (record->provider_id != NULL) &&
        (strcmp(record->provider_id, STRAPPY_PROVIDER_OPENROUTER) == 0);
    }
    if ((record->model_id != NULL) &&
        (strcmp(record->model_id, gemma_model_id) == 0)) {
      found_gemma =
        (record->name != NULL) &&
        (strcmp(record->name, "Gemma 4 31B IT") == 0) &&
        (record->context_length == 131072LL) &&
        (record->top_provider_max_completion_tokens == 8192LL) &&
        record->top_provider_is_moderated &&
        (record->pricing_prompt != NULL) &&
        (strcmp(record->pricing_prompt, "0.00000015") == 0) &&
        (record->knowledge_cutoff != NULL) &&
        (strcmp(record->knowledge_cutoff, "2025-01") == 0) &&
        (record->links_details != NULL) &&
        (strstr(record->links_details, "openrouter.ai") != NULL) &&
        (record->reasoning_json == NULL) &&
        (record->benchmarks_json == NULL) &&
        (record->default_parameters_json == NULL) &&
        (record->raw_json == NULL);
    }
    if ((record->model_id != NULL) &&
        (strcmp(record->model_id, openai_model_id) == 0)) {
      found_openai = (record->wire_model_id != NULL) &&
        (strcmp(record->wire_model_id, "openai/gpt-4.1-mini") == 0);
    }
  }
  ok = (list.count == 3U) && found_builtin_default && found_gemma && found_openai;
  strappy_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "OpenRouter model rows did not match expected values.\n");
    return 0;
  }

  default_model = NULL;
  error = NULL;
  if (!strappy_db_get_default_model(context->catalog_path,
                                    &default_model,
                                    &error)) {
    fprintf(stderr,
            "Could not get default OpenRouter model: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  ok = (default_model != NULL) &&
       (strcmp(default_model, STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER) == 0);
  strappy_free_string(default_model);
  if (!ok) {
    fprintf(stderr, "Built-in OpenRouter default model did not persist.\n");
    return 0;
  }

  error = NULL;
  strappy_model_record_list_init(&list);
  if (!strappy_db_list_allowed_models(context->catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list allowed OpenRouter models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 1U) &&
       (list.records[0].model_id != NULL) &&
       (strcmp(list.records[0].model_id,
               STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER) == 0) &&
       list.records[0].allowed;
  strappy_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "Built-in OpenRouter default was not the only allowed model.\n");
    return 0;
  }

  error = NULL;
  strappy_model_record_list_init(&list);
  if (!strappy_db_list_models_matching(context->catalog_path,
                                       "alloy",
                                       &list,
                                       &error)) {
    fprintf(stderr,
            "Could not filter OpenRouter models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 1U) &&
       (list.records[0].model_id != NULL) &&
       (strcmp(list.records[0].model_id, gemma_model_id) == 0);
  strappy_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "OpenRouter model filtered list did not match expected values.\n");
    return 0;
  }

  error = NULL;
  strappy_model_record_list_init(&list);
  if (!strappy_db_list_models_matching(context->catalog_path,
                                       "GPT 4.1 Mini",
                                       &list,
                                       &error)) {
    fprintf(stderr,
            "Could not filter OpenRouter models by name: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 1U) &&
       (list.records[0].model_id != NULL) &&
       (strcmp(list.records[0].model_id, openai_model_id) == 0);
  strappy_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "OpenRouter model name filter did not match expected values.\n");
    return 0;
  }

  error = NULL;
  strappy_model_record_list_init(&list);
  if (!strappy_db_list_models_matching(context->catalog_path,
                                       "openai/gpt-4.1-mini",
                                       &list,
                                       &error)) {
    fprintf(stderr,
            "Could not filter OpenRouter models by id: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 1U) &&
       (list.records[0].model_id != NULL) &&
       (strcmp(list.records[0].model_id, openai_model_id) == 0);
  strappy_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "OpenRouter model id filter did not match expected values.\n");
    return 0;
  }

  error = NULL;
  strappy_model_record_list_init(&list);
  if (!strappy_db_list_models_matching(context->catalog_path,
                                       "not-a-model",
                                       &list,
                                       &error)) {
    fprintf(stderr,
            "Could not filter OpenRouter models with no matches: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 0U);
  strappy_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "OpenRouter model filtered list should have been empty.\n");
    return 0;
  }

  if (!harness_expect_catalog_integer(
        context->catalog_path,
        "SELECT COUNT(*) FROM model_features "
        "WHERE model_id = 'openrouter:openai/gpt-4.1-mini' "
        "AND feature_kind = 'input_modality' AND feature_value = 'image';",
        1LL,
        "OpenRouter input modality count") ||
      !harness_expect_catalog_integer(
        context->catalog_path,
        "SELECT COUNT(*) FROM model_features "
        "WHERE model_id = 'openrouter:google/gemma-4-31b-it' "
        "AND feature_kind = 'parameter' AND feature_value = 'tools';",
        1LL,
        "OpenRouter supported parameter count") ||
      !harness_expect_catalog_integer(
        context->catalog_path,
        "SELECT COUNT(*) FROM model_features "
        "WHERE model_id = 'openrouter:google/gemma-4-31b-it' "
        "AND feature_kind = 'voice' AND feature_value = 'alloy';",
        1LL,
        "OpenRouter supported voice count")) {
    return 0;
  }

  fallback_working_directory = getenv("HOME");
  strappy_session_options_init(&default_options);
  strappy_session_options_init(&saved_default_options);
  error = NULL;
  if ((fallback_working_directory == NULL) ||
      !strappy_db_load_default_session_options(
        context->catalog_path,
        fallback_working_directory,
        &default_options,
        &error)) {
    fprintf(stderr,
            "Could not load default options before selecting a model: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_options_destroy(&saved_default_options);
    strappy_session_options_destroy(&default_options);
    return 0;
  }
  free(default_options.model_id);
  default_options.model_id = strdup(openai_model_id);
  actual_changed_fields = 0U;
  if ((default_options.model_id == NULL) ||
      !strappy_db_update_default_session_options(
        context->catalog_path,
        fallback_working_directory,
        &default_options,
        STRAPPY_SESSION_OPTION_MODEL,
        &saved_default_options,
        &actual_changed_fields,
        &error) ||
      (actual_changed_fields != STRAPPY_SESSION_OPTION_MODEL) ||
      (saved_default_options.model_id == NULL) ||
      (strcmp(saved_default_options.model_id, openai_model_id) != 0)) {
    fprintf(stderr,
            "Could not set the model through default session options: %s\n",
            (error != NULL) ? error : "snapshot mismatch");
    strappy_free_string(error);
    strappy_session_options_destroy(&saved_default_options);
    strappy_session_options_destroy(&default_options);
    return 0;
  }
  strappy_session_options_destroy(&saved_default_options);
  strappy_session_options_destroy(&default_options);

  default_model = NULL;
  if (!strappy_db_get_default_model(context->catalog_path,
                                    &default_model,
                                    &error)) {
    fprintf(stderr,
            "Could not get default OpenRouter model after update: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  ok = (default_model != NULL) &&
       (strcmp(default_model, openai_model_id) == 0);
  strappy_free_string(default_model);
  if (!ok) {
    fprintf(stderr, "Default OpenRouter model did not persist.\n");
    return 0;
  }

  error = NULL;
  if (strappy_db_set_model_allowed(context->catalog_path,
                                   openai_model_id,
                                   0,
                                   &error)) {
    fprintf(stderr, "Default OpenRouter model should not be removable.\n");
    return 0;
  }
  strappy_free_string(error);
  error = NULL;

  strappy_model_record_list_init(&list);
  if (!strappy_db_list_models(context->catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list selected OpenRouter models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_record_list_destroy(&list);
    return 0;
  }

  ok = (list.count == 3U) &&
       (list.records[0].model_id != NULL) &&
       (strcmp(list.records[0].model_id, openai_model_id) == 0) &&
       list.records[0].selected &&
       list.records[0].allowed;
  strappy_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "Default OpenRouter model was not listed first.\n");
    return 0;
  }

  error = NULL;
  strappy_model_record_list_init(&list);
  if (!strappy_db_list_allowed_models(context->catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list updated allowed OpenRouter models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 1U) &&
       (list.records[0].model_id != NULL) &&
       (strcmp(list.records[0].model_id, openai_model_id) == 0);
  strappy_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "Default OpenRouter model was not the only allowed model.\n");
    return 0;
  }

  error = NULL;
  if (!strappy_db_set_model_allowed(context->catalog_path,
                                    gemma_model_id,
                                    1,
                                    &error)) {
    fprintf(stderr,
            "Could not allow OpenRouter model: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  error = NULL;
  strappy_model_record_list_init(&list);
  if (!strappy_db_list_allowed_models(context->catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list allowed OpenRouter models after allow: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_record_list_destroy(&list);
    return 0;
  }
  found_gemma = 0;
  found_openai = 0;
  for (index = 0U; index < list.count; index++) {
    if ((list.records[index].model_id != NULL) &&
        (strcmp(list.records[index].model_id, gemma_model_id) == 0)) {
      found_gemma = 1;
    }
    if ((list.records[index].model_id != NULL) &&
        (strcmp(list.records[index].model_id, openai_model_id) == 0)) {
      found_openai = 1;
    }
  }
  ok = (list.count == 2U) && found_gemma && found_openai;
  strappy_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "Allowed OpenRouter models did not match expected values.\n");
    return 0;
  }

  session_id = 0LL;
  error = NULL;
  if (!strappy_db_create_session(context->catalog_path, &session_id, &error)) {
    fprintf(stderr,
            "Could not create model selection session: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  session_model = NULL;
  if (!strappy_db_get_session_model(context->catalog_path,
                                    session_id,
                                    &session_model,
                                    &error)) {
    fprintf(stderr,
            "Could not get model selection session model: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  ok = (session_model != NULL) &&
       (strcmp(session_model, openai_model_id) == 0);
  strappy_free_string(session_model);
  if (!ok) {
    fprintf(stderr, "New session did not copy default OpenRouter model.\n");
    return 0;
  }

  strappy_session_record_init(&session_list_record);
  error = NULL;
  if (!strappy_db_load_session_list_record(context->catalog_path,
                                           session_id,
                                           &session_list_record,
                                           &error)) {
    fprintf(stderr,
            "Could not load lightweight session row: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_record_destroy(&session_list_record);
    return 0;
  }
  initial_activity_at_ms = session_list_record.last_activity_at_ms;
  ok = (session_list_record.prompt != NULL) &&
       (strcmp(session_list_record.prompt, "") == 0) &&
       (session_list_record.response != NULL) &&
       (strcmp(session_list_record.response, "") == 0) &&
       (session_list_record.model != NULL) &&
       (strcmp(session_list_record.model, openai_model_id) == 0) &&
       (session_list_record.model_name != NULL) &&
       (strcmp(session_list_record.model_name, "GPT 4.1 Mini") == 0) &&
       (session_list_record.last_activity_at != NULL) &&
       (session_list_record.last_activity_at[0] != '\0') &&
       (initial_activity_at_ms > 0LL);
  strappy_session_record_destroy(&session_list_record);
  if (!ok) {
    fprintf(stderr, "Lightweight session row did not have the expected metadata.\n");
    return 0;
  }

  error = NULL;
  if (!strappy_db_update_session_model(context->catalog_path,
                                       session_id,
                                       gemma_model_id,
                                       &error)) {
    fprintf(stderr,
            "Could not update session OpenRouter model: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  session_model = NULL;
  if (!strappy_db_get_session_model(context->catalog_path,
                                    session_id,
                                    &session_model,
                                    &error)) {
    fprintf(stderr,
            "Could not read updated session OpenRouter model: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  ok = (session_model != NULL) &&
       (strcmp(session_model, gemma_model_id) == 0);
  strappy_free_string(session_model);
  if (!ok) {
    fprintf(stderr, "Session OpenRouter model did not persist.\n");
    return 0;
  }

  strappy_session_record_init(&session_list_record);
  error = NULL;
  if (!strappy_db_load_session_list_record(context->catalog_path,
                                           session_id,
                                           &session_list_record,
                                           &error)) {
    fprintf(stderr,
            "Could not reload lightweight session row: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_record_destroy(&session_list_record);
    return 0;
  }
  ok = (session_list_record.model != NULL) &&
       (strcmp(session_list_record.model, gemma_model_id) == 0) &&
       (session_list_record.model_name != NULL) &&
       (strcmp(session_list_record.model_name, "Gemma 4 31B IT") == 0) &&
       (session_list_record.last_activity_at_ms == initial_activity_at_ms);
  strappy_session_record_destroy(&session_list_record);
  if (!ok) {
    fprintf(stderr,
            "Model selection changed session activity or display metadata.\n");
    return 0;
  }

  return 1;
}

static int harness_run_provider_account_routing_tests(
  const harness_context *context)
{
  static const char *chatgpt_model_id =
    STRAPPY_PROVIDER_ACCOUNT_OPENAI_CHATGPT ":openai/gpt-4.1-mini";
  static const char *openrouter_model_id =
    STRAPPY_PROVIDER_ACCOUNT_OPENROUTER ":openai/gpt-4.1-mini";
  static const char *wire_model_id = "openai/gpt-4.1-mini";
  static const char *openrouter_refresh_json =
    "{\"data\":["
      "{\"id\":\"google/gemma-4-31b-it\","
       "\"name\":\"Gemma 4 31B IT\"},"
      "{\"id\":\"openai/gpt-4.1-mini\","
       "\"name\":\"GPT 4.1 Mini\"}"
    "]}";
  static const char *request_json =
    "{\"model\":\"openai/gpt-4.1-mini\",\"stream\":true,"
    "\"store\":false,\"input\":[]}";
  static const char *chatgpt_response_json =
    "{\"id\":\"resp-plan\",\"status\":\"completed\",\"output\":[],"
    "\"usage\":{\"input_tokens\":10,\"output_tokens\":4,"
    "\"total_tokens\":14,\"cost\":0,\"cost_details\":{"
    "\"upstream_inference_cost\":0,"
    "\"upstream_inference_input_cost\":0,"
    "\"upstream_inference_output_cost\":0}}}";
  strappy_model_record_list list;
  strappy_model_route_record route;
  strappy_response_call_begin_input begin;
  strappy_response_call_finish_input finish;
  sqlite3 *db;
  char *error;
  char *qualified_model_id;
  char *openrouter_endpoint;
  char *chatgpt_endpoint;
  char sql[1024];
  long long session_id;
  long long call_id;
  size_t index;
  int found_chatgpt;
  int found_openrouter;
  int ok;

  if (context == NULL) {
    return 0;
  }

  db = NULL;
  if (sqlite3_open(context->catalog_path, &db) != SQLITE_OK) {
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }
  ok = harness_exec_sql(
    db,
    "PRAGMA foreign_keys = ON;"
    "INSERT INTO models "
      "(id, provider_account_id, wire_model_id, name, catalog_active, "
       "last_seen_at_ms) VALUES "
      "('openai_chatgpt:openai/gpt-4.1-mini', 'openai_chatgpt', "
       "'openai/gpt-4.1-mini', 'GPT 4.1 Mini (ChatGPT)', 1, 1);");
  sqlite3_close(db);
  if (!ok) {
    return 0;
  }

  error = NULL;
  if (!strappy_db_set_model_allowed(context->catalog_path,
                                    chatgpt_model_id,
                                    1,
                                    &error) ||
      !strappy_db_save_openrouter_models_json(context->catalog_path,
                                              openrouter_refresh_json,
                                              &error)) {
    fprintf(stderr,
            "Could not prepare provider-account catalog rows: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  if (!harness_expect_catalog_integer(
        context->catalog_path,
        "SELECT COUNT(*) FROM models WHERE "
        "id = 'openai_chatgpt:openai/gpt-4.1-mini' "
        "AND provider_account_id = 'openai_chatgpt' "
        "AND catalog_active = 1;",
        1LL,
        "ChatGPT model after OpenRouter refresh")) {
    return 0;
  }

  strappy_model_record_list_init(&list);
  error = NULL;
  if (!strappy_db_list_models_matching(context->catalog_path,
                                       wire_model_id,
                                       &list,
                                       &error)) {
    fprintf(stderr,
            "Could not list duplicate cross-account wire models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_record_list_destroy(&list);
    return 0;
  }
  found_chatgpt = 0;
  found_openrouter = 0;
  for (index = 0U; index < list.count; index++) {
    const strappy_model_record *record;

    record = &list.records[index];
    if ((record->wire_model_id == NULL) ||
        (strcmp(record->wire_model_id, wire_model_id) != 0)) {
      continue;
    }
    if ((record->model_id != NULL) &&
        (strcmp(record->model_id, chatgpt_model_id) == 0) &&
        (record->provider_account_id != NULL) &&
        (strcmp(record->provider_account_id,
                STRAPPY_PROVIDER_ACCOUNT_OPENAI_CHATGPT) == 0) &&
        (record->provider_id != NULL) &&
        (strcmp(record->provider_id,
                STRAPPY_PROVIDER_OPENAI_CHATGPT) == 0) &&
        record->allowed) {
      found_chatgpt = 1;
    }
    if ((record->model_id != NULL) &&
        (strcmp(record->model_id, openrouter_model_id) == 0) &&
        (record->provider_account_id != NULL) &&
        (strcmp(record->provider_account_id,
                STRAPPY_PROVIDER_ACCOUNT_OPENROUTER) == 0)) {
      found_openrouter = 1;
    }
  }
  ok = (list.count == 2U) && found_chatgpt && found_openrouter;
  strappy_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr,
            "Identical wire model IDs were not isolated by account.\n");
    return 0;
  }

  strappy_model_record_list_init(&list);
  if (!strappy_db_list_openrouter_models_matching(context->catalog_path,
                                                  wire_model_id,
                                                  &list,
                                                  &error)) {
    fprintf(stderr,
            "Could not list compatibility OpenRouter models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 1U) &&
    (list.records[0].model_id != NULL) &&
    (strcmp(list.records[0].model_id, openrouter_model_id) == 0);
  strappy_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr,
            "OpenRouter compatibility catalog crossed account boundaries.\n");
    return 0;
  }

  error = NULL;
  qualified_model_id = strappy_provider_model_identifier(
    STRAPPY_PROVIDER_ACCOUNT_OPENAI_CHATGPT,
    wire_model_id,
    &error);
  openrouter_endpoint = strappy_provider_responses_endpoint(
    STRAPPY_PROVIDER_KIND_OPENROUTER,
    "https://router.example/v1/responses",
    &error);
  chatgpt_endpoint = strappy_provider_responses_endpoint(
    STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT,
    "https://must-not-be-used.example/responses",
    &error);
  ok = (qualified_model_id != NULL) &&
    (strcmp(qualified_model_id, chatgpt_model_id) == 0) &&
    (openrouter_endpoint != NULL) &&
    (strcmp(openrouter_endpoint,
            "https://router.example/v1/responses") == 0) &&
    (chatgpt_endpoint != NULL) &&
    (strcmp(chatgpt_endpoint,
            STRAPPY_PROVIDER_OPENAI_CHATGPT_RESPONSES_ENDPOINT) == 0);
  free(qualified_model_id);
  free(openrouter_endpoint);
  free(chatgpt_endpoint);
  if (!ok) {
    fprintf(stderr,
            "Provider-account endpoint policy did not match: %s\n",
            (error != NULL) ? error : "unexpected endpoint");
    strappy_free_string(error);
    return 0;
  }
  strappy_free_string(error);

  session_id = 0LL;
  error = NULL;
  if (!strappy_db_create_session(context->catalog_path, &session_id, &error) ||
      !strappy_db_update_session_model(context->catalog_path,
                                       session_id,
                                       chatgpt_model_id,
                                       &error)) {
    fprintf(stderr,
            "Could not select a ChatGPT-account model: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  strappy_model_route_record_init(&route);
  if (!strappy_db_get_session_model_route(context->catalog_path,
                                          session_id,
                                          &route,
                                          &error)) {
    fprintf(stderr,
            "Could not resolve the session model route: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_model_route_record_destroy(&route);
    return 0;
  }
  ok = (route.model_id != NULL) &&
    (strcmp(route.model_id, chatgpt_model_id) == 0) &&
    (route.provider_account_id != NULL) &&
    (strcmp(route.provider_account_id,
            STRAPPY_PROVIDER_ACCOUNT_OPENAI_CHATGPT) == 0) &&
    (route.provider_id != NULL) &&
    (strcmp(route.provider_id, STRAPPY_PROVIDER_OPENAI_CHATGPT) == 0) &&
    (route.wire_model_id != NULL) &&
    (strcmp(route.wire_model_id, wire_model_id) == 0);
  strappy_model_route_record_destroy(&route);
  if (!ok) {
    fprintf(stderr, "Session route did not retain its model account.\n");
    return 0;
  }

  memset(&begin, 0, sizeof(begin));
  begin.session_id = session_id;
  begin.provider_account_id = STRAPPY_PROVIDER_ACCOUNT_OPENAI_CHATGPT;
  begin.model_id = chatgpt_model_id;
  begin.prompt_group_key = "provider-account-lock";
  begin.request_kind = "user";
  begin.round_index = 0L;
  begin.attempt_index = 0L;
  begin.new_input_start_index = 0L;
  begin.request_method = "POST";
  begin.request_url = STRAPPY_PROVIDER_OPENAI_CHATGPT_RESPONSES_ENDPOINT;
  begin.request_headers_json = "{}";
  begin.request_json = request_json;
  call_id = 0LL;
  if (!strappy_db_begin_response_call(context->catalog_path,
                                      &begin,
                                      &call_id,
                                      &error)) {
    fprintf(stderr,
            "Could not bind a response call to its model account: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  memset(&finish, 0, sizeof(finish));
  finish.call_id = call_id;
  finish.state = "completed";
  finish.http_status = 200L;
  finish.started_at_ms = 2000000000000LL;
  finish.completed_at_ms = 2000000000100LL;
  finish.request_bytes = (long long)strlen(request_json);
  finish.effective_url = begin.request_url;
  finish.response_json = chatgpt_response_json;
  if (!strappy_db_finish_response_call(context->catalog_path,
                                       &finish,
                                       &error)) {
    fprintf(stderr,
            "Could not finish the account-routing response fixture: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  if (snprintf(sql,
               sizeof(sql),
               "SELECT COUNT(*) FROM sessions s JOIN turns t "
               "ON t.session_id = s.id JOIN model_requests r "
               "ON r.turn_id = t.id JOIN http_attempts a "
               "ON a.request_id = r.id WHERE s.id = %lld "
               "AND s.provider_account_id = 'openai_chatgpt' "
               "AND r.model_id = 'openai_chatgpt:openai/gpt-4.1-mini' "
               "AND r.provider_account_id = 'openai_chatgpt' "
               "AND a.provider_account_id = 'openai_chatgpt';",
               session_id) <= 0 ||
      !harness_expect_catalog_integer(context->catalog_path,
                                      sql,
                                      1LL,
                                      "session/request/attempt account route")) {
    return 0;
  }
  if (snprintf(sql,
               sizeof(sql),
               "SELECT COUNT(*) FROM api_usage WHERE attempt_id = %lld "
               "AND input_tokens = 10 AND output_tokens = 4 "
               "AND total_tokens = 14 AND cost_nano_usd IS NULL "
               "AND upstream_cost_nano_usd IS NULL "
               "AND upstream_input_cost_nano_usd IS NULL "
               "AND upstream_output_cost_nano_usd IS NULL;",
               call_id) <= 0 ||
      !harness_expect_catalog_integer(
        context->catalog_path,
        sql,
        1LL,
        "ChatGPT-plan tokens without fabricated monetary cost")) {
    return 0;
  }

  error = NULL;
  if (strappy_db_update_session_model(context->catalog_path,
                                      session_id,
                                      openrouter_model_id,
                                      &error) ||
      (error == NULL) ||
      (strstr(error, "different model account") == NULL)) {
    fprintf(stderr,
            "Established session accepted a cross-account model: %s\n",
            (error != NULL) ? error : "no error");
    strappy_free_string(error);
    return 0;
  }
  strappy_free_string(error);
  error = NULL;

  if (!strappy_db_set_model_allowed(context->catalog_path,
                                    chatgpt_model_id,
                                    0,
                                    &error)) {
    fprintf(stderr,
            "Could not disable the pinned ChatGPT model fixture: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  strappy_model_route_record_init(&route);
  if (strappy_db_get_session_model_route(context->catalog_path,
                                         session_id,
                                         &route,
                                         &error) ||
      (error == NULL) ||
      (strstr(error, "not active and allowed") == NULL)) {
    fprintf(stderr,
            "Disabled pinned model did not fail closed: %s\n",
            (error != NULL) ? error : "route unexpectedly resolved");
    strappy_free_string(error);
    strappy_model_route_record_destroy(&route);
    return 0;
  }
  strappy_free_string(error);
  strappy_model_route_record_destroy(&route);
  if (snprintf(sql,
               sizeof(sql),
               "SELECT COUNT(*) FROM sessions WHERE id = %lld "
               "AND model_id = "
               "'openai_chatgpt:openai/gpt-4.1-mini' "
               "AND provider_account_id = 'openai_chatgpt';",
               session_id) <= 0 ||
      !harness_expect_catalog_integer(
        context->catalog_path,
        sql,
        1LL,
        "disabled pinned model retains its account without fallback")) {
    return 0;
  }

  return harness_expect_catalog_integer(context->catalog_path,
                                        "SELECT COUNT(*) FROM "
                                        "pragma_foreign_key_check;",
                                        0LL,
                                        "provider-account foreign key check");
}

static int harness_write_file_bytes(const char *path,
                                    const unsigned char *bytes,
                                    size_t byte_count)
{
  FILE *file;
  int ok;

  file = fopen(path, "wb");
  if (file == NULL) {
    perror("Could not create file_read fixture");
    return 0;
  }
  ok = ((byte_count == 0U) ||
        (fwrite(bytes, 1U, byte_count, file) == byte_count)) ? 1 : 0;
  if (fclose(file) != 0) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr, "Could not write file_read fixture: %s\n", path);
  }
  return ok;
}

static int harness_create_file_read_paged_fixture(const char *path)
{
  FILE *file;
  int line;
  int ok;

  file = fopen(path, "wb");
  if (file == NULL) {
    perror("Could not create paged file_read fixture");
    return 0;
  }
  ok = 1;
  for (line = 1; line <= 501; line++) {
    if (fprintf(file, "line %03d\n", line) < 0) {
      ok = 0;
      break;
    }
  }
  if (fclose(file) != 0) {
    ok = 0;
  }
  return ok;
}

static int harness_create_file_read_large_fixture(const char *path)
{
  unsigned char line[100];
  FILE *file;
  int line_index;
  int ok;

  memset(line, 'x', sizeof(line));
  line[sizeof(line) - 1U] = (unsigned char)'\n';
  file = fopen(path, "wb");
  if (file == NULL) {
    perror("Could not create large file_read fixture");
    return 0;
  }
  ok = 1;
  for (line_index = 0; line_index < 300; line_index++) {
    if (fwrite(line, 1U, sizeof(line), file) != sizeof(line)) {
      ok = 0;
      break;
    }
  }
  if (fclose(file) != 0) {
    ok = 0;
  }
  return ok;
}

static int harness_file_read_result_matches(cJSON *root,
                                            const char *expected_content,
                                            long long expected_offset,
                                            int expected_line_count,
                                            long long expected_next_offset)
{
  cJSON *content;
  cJSON *offset;
  cJSON *line_count;
  cJSON *next_offset;

  content = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "content") : NULL;
  offset = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "offset") : NULL;
  line_count = cJSON_IsObject(root) ?
    cJSON_GetObjectItem(root, "line_count") : NULL;
  next_offset = cJSON_IsObject(root) ?
    cJSON_GetObjectItem(root, "next_offset") : NULL;

  if (!cJSON_IsObject(root) || (cJSON_GetArraySize(root) != 4) ||
      !cJSON_IsString(content) || (content->valuestring == NULL) ||
      ((expected_content != NULL) &&
       (strcmp(content->valuestring, expected_content) != 0)) ||
      !cJSON_IsNumber(offset) ||
      (offset->valuedouble != (double)expected_offset) ||
      !cJSON_IsNumber(line_count) ||
      (line_count->valuedouble != (double)expected_line_count)) {
    return 0;
  }
  if (expected_next_offset < 0LL) {
    return cJSON_IsNull(next_offset);
  }
  return cJSON_IsNumber(next_offset) &&
    (next_offset->valuedouble == (double)expected_next_offset);
}

static int harness_run_file_read_tests(const harness_context *context)
{
  static const unsigned char utf8_bytes[] =
    "alpha\n\xCE\xB2" "eta\nthird";
  static const unsigned char invalid_utf8_bytes[] = {
    (unsigned char)'o', (unsigned char)'k', (unsigned char)'\n',
    0xC0U, 0xAFU
  };
  char utf8_path[1400];
  char empty_path[1400];
  char paged_path[1400];
  char large_path[1400];
  char invalid_path[1400];
  char absolute_arguments[1600];
  char *working_directory;
  char *error;
  long long session_id;
  cJSON *root;
  cJSON *content;
  int written;
  int ok;

  utf8_path[0] = '\0';
  empty_path[0] = '\0';
  paged_path[0] = '\0';
  large_path[0] = '\0';
  invalid_path[0] = '\0';
  working_directory = NULL;
  error = NULL;
  session_id = 0LL;
  root = NULL;
  ok = (context != NULL) &&
    harness_join_path(utf8_path,
                      sizeof(utf8_path),
                      context->temp_dir,
                      "file_read_utf8.txt") &&
    harness_join_path(empty_path,
                      sizeof(empty_path),
                      context->temp_dir,
                      "file_read_empty.txt") &&
    harness_join_path(paged_path,
                      sizeof(paged_path),
                      context->temp_dir,
                      "file_read_paged.txt") &&
    harness_join_path(large_path,
                      sizeof(large_path),
                      context->temp_dir,
                      "file_read_large.txt") &&
    harness_join_path(invalid_path,
                      sizeof(invalid_path),
                      context->temp_dir,
                      "file_read_invalid.txt");
  if (!ok) {
    fprintf(stderr, "Could not build file_read fixture paths.\n");
    goto cleanup;
  }

  ok = harness_write_file_bytes(utf8_path,
                                utf8_bytes,
                                sizeof(utf8_bytes) - 1U) &&
    harness_write_file_bytes(empty_path, NULL, 0U) &&
    harness_create_file_read_paged_fixture(paged_path) &&
    harness_create_file_read_large_fixture(large_path) &&
    harness_write_file_bytes(invalid_path,
                             invalid_utf8_bytes,
                             sizeof(invalid_utf8_bytes));
  if (!ok) {
    goto cleanup;
  }

  ok = strappy_db_create_session_with_working_directory(
    context->catalog_path,
    context->temp_dir,
    &session_id,
    &error);
  if (!ok) {
    fprintf(stderr,
            "Could not create file_read session: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }
  ok = strappy_db_get_session_working_directory(context->catalog_path,
                                                session_id,
                                                &working_directory,
                                                &error) &&
    (working_directory != NULL) &&
    (strcmp(working_directory, context->temp_dir) == 0);
  if (!ok) {
    fprintf(stderr,
            "File-read working directory was not persisted: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }
  free(working_directory);
  working_directory = NULL;

  ok = harness_expect_session_error_contains(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_READ,
    "{\"path\":\"file_read_utf8.txt\"}",
    "file_read is available only in Coding Assistant sessions") &&
    strappy_db_update_session_assistant_set(
      context->catalog_path,
      session_id,
      STRAPPY_ASSISTANT_SET_CODING_ASSISTANT,
      &error);
  if (!ok) {
    fprintf(stderr,
            "Could not isolate file_read to the Coding Assistant: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }

  root = harness_session_tool_output_json(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_READ,
    "{\"path\":\"file_read_utf8.txt\",\"limit\":2}");
  ok = harness_file_read_result_matches(
    root,
    "alpha\n\xCE\xB2" "eta\n",
    1LL,
    2,
    3LL);
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr, "file_read UTF-8 result did not match.\n");
    goto cleanup;
  }

  root = harness_session_tool_output_json(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_READ,
    "{\"path\":\"file_read_utf8.txt\",\"offset\":3}");
  ok = harness_file_read_result_matches(root, "third", 3LL, 1, -1LL);
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr, "file_read offset result did not match.\n");
    goto cleanup;
  }

  written = snprintf(absolute_arguments,
                     sizeof(absolute_arguments),
                     "{\"path\":\"%s\",\"offset\":2,\"limit\":1}",
                     utf8_path);
  if ((written <= 0) || ((size_t)written >= sizeof(absolute_arguments))) {
    fprintf(stderr, "Could not build absolute file_read arguments.\n");
    ok = 0;
    goto cleanup;
  }
  root = harness_session_tool_output_json(context->catalog_path,
                                          session_id,
                                          STRAPPY_TOOL_FILE_READ,
                                          absolute_arguments);
  ok = harness_file_read_result_matches(root,
                                        "\xCE\xB2" "eta\n",
                                        2LL,
                                        1,
                                        3LL);
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr, "file_read absolute-path result did not match.\n");
    goto cleanup;
  }

  root = harness_session_tool_output_json(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_READ,
    "{\"path\":\"file_read_empty.txt\"}");
  ok = harness_file_read_result_matches(root, "", 1LL, 0, -1LL);
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr, "file_read empty-file result did not match.\n");
    goto cleanup;
  }

  root = harness_session_tool_output_json(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_READ,
    "{\"path\":\"file_read_paged.txt\"}");
  content = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "content") : NULL;
  ok = harness_file_read_result_matches(root, NULL, 1LL, 500, 501LL) &&
    cJSON_IsString(content) && (content->valuestring != NULL) &&
    (strlen(content->valuestring) == 4500U) &&
    (strncmp(content->valuestring, "line 001\n", 9U) == 0) &&
    (strcmp(content->valuestring + 4491U, "line 500\n") == 0);
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr, "file_read 500-line page did not match.\n");
    goto cleanup;
  }

  root = harness_session_tool_output_json(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_READ,
    "{\"path\":\"file_read_paged.txt\",\"offset\":501}");
  ok = harness_file_read_result_matches(root,
                                        "line 501\n",
                                        501LL,
                                        1,
                                        -1LL);
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr, "file_read final page did not match.\n");
    goto cleanup;
  }

  root = harness_session_tool_output_json(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_READ,
    "{\"path\":\"file_read_large.txt\",\"limit\":256}");
  content = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "content") : NULL;
  ok = harness_file_read_result_matches(root, NULL, 1LL, 256, 257LL) &&
    cJSON_IsString(content) && (content->valuestring != NULL) &&
    (strlen(content->valuestring) == (25U * 1024U));
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr, "file_read did not accept the exact 25 KiB boundary.\n");
    goto cleanup;
  }

  ok = harness_expect_session_error_contains(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_READ,
    "{\"path\":\"file_read_large.txt\",\"limit\":257}",
    "Requested 257 lines starting at line 1 exceeds the 25 KiB output "
    "limit. Retry file_read with a smaller limit.") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_READ,
      "{\"path\":\"file_read_invalid.txt\"}",
      "File is not valid UTF-8 text") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_READ,
      "{\"path\":\"file_read_utf8.txt\",\"limit\":501}",
      "file_read limit must be between 1 and 500") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_READ,
      "{\"path\":\"file_read_utf8.txt\",\"offset\":4}",
      "Offset 4 is beyond end of file") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_READ,
      "{\"path\":\".\"}",
      "Path is not a regular file") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_READ,
      "{\"path\":\"file_read_missing.txt\"}",
      "File not found") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_READ,
      "{\"path\":\"file_read_utf8.txt\",\"extra\":true}",
      "file_read accepts only path, offset, and limit") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_READ,
      "{\"path\":\"file_read_utf8.txt\",\"limit\":1,\"limit\":2}",
      "file_read limit may be provided only once");
  if (!ok) {
    goto cleanup;
  }

cleanup:
  cJSON_Delete(root);
  free(working_directory);
  free(error);
  if (utf8_path[0] != '\0') {
    unlink(utf8_path);
  }
  if (empty_path[0] != '\0') {
    unlink(empty_path);
  }
  if (paged_path[0] != '\0') {
    unlink(paged_path);
  }
  if (large_path[0] != '\0') {
    unlink(large_path);
  }
  if (invalid_path[0] != '\0') {
    unlink(invalid_path);
  }
  return ok;
}

static int harness_file_bytes_match(const char *path,
                                    const unsigned char *expected,
                                    size_t expected_length)
{
  FILE *file;
  unsigned char *actual;
  size_t actual_length;
  int ok;

  file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file mutation result: %s\n", path);
    return 0;
  }
  actual = (unsigned char *)malloc(expected_length + 1U);
  if (actual == NULL) {
    fclose(file);
    return 0;
  }
  actual_length = fread(actual, 1U, expected_length + 1U, file);
  ok = !ferror(file) && (fclose(file) == 0) &&
    (actual_length == expected_length) &&
    ((expected_length == 0U) ||
     (memcmp(actual, expected, expected_length) == 0));
  if (!ok) {
    fprintf(stderr, "File mutation result did not match: %s\n", path);
  }
  free(actual);
  return ok;
}

static int harness_empty_object_result(cJSON *root)
{
  return cJSON_IsObject(root) && (cJSON_GetArraySize(root) == 0);
}

static int harness_run_file_mutation_tests(const harness_context *context)
{
  static const unsigned char initial_write[] =
    "alpha\n\xCE\xB2" "eta\n";
  static const unsigned char edit_fixture[] =
    "\xEF\xBB\xBF" "first\r\nsecond \xCE\xB2\r\nthird\r\n";
  static const unsigned char first_edit_result[] =
    "\xEF\xBB\xBF" "first\r\nchanged\r\nline\r\n";
  static const unsigned char deletion_result[] =
    "\xEF\xBB\xBF" "first\r\nline\r\n";
  static const unsigned char duplicate_fixture[] = "same\nsame\n";
  static const unsigned char invalid_utf8_fixture[] = { 0xC0U, 0xAFU };
  char nested_dir[1400];
  char deep_dir[1400];
  char write_path[1400];
  char absolute_path[1400];
  char edit_path[1400];
  char duplicate_path[1400];
  char invalid_path[1400];
  char absolute_arguments[1800];
  struct stat file_stat;
  char *error;
  long long session_id;
  cJSON *root;
  int written;
  int ok;

  nested_dir[0] = '\0';
  deep_dir[0] = '\0';
  write_path[0] = '\0';
  absolute_path[0] = '\0';
  edit_path[0] = '\0';
  duplicate_path[0] = '\0';
  invalid_path[0] = '\0';
  error = NULL;
  session_id = 0LL;
  root = NULL;
  ok = (context != NULL) &&
    harness_join_path(nested_dir,
                      sizeof(nested_dir),
                      context->temp_dir,
                      "file_mutation_nested") &&
    harness_join_path(deep_dir,
                      sizeof(deep_dir),
                      nested_dir,
                      "deep") &&
    harness_join_path(write_path,
                      sizeof(write_path),
                      deep_dir,
                      "created.txt") &&
    harness_join_path(absolute_path,
                      sizeof(absolute_path),
                      context->temp_dir,
                      "file_write_absolute.txt") &&
    harness_join_path(edit_path,
                      sizeof(edit_path),
                      context->temp_dir,
                      "file_edit_crlf.txt") &&
    harness_join_path(duplicate_path,
                      sizeof(duplicate_path),
                      context->temp_dir,
                      "file_edit_duplicate.txt") &&
    harness_join_path(invalid_path,
                      sizeof(invalid_path),
                      context->temp_dir,
                      "file_edit_invalid.txt");
  if (!ok) {
    fprintf(stderr, "Could not build file mutation fixture paths.\n");
    goto cleanup;
  }

  ok = strappy_db_create_session_with_working_directory(
    context->catalog_path,
    context->temp_dir,
    &session_id,
    &error);
  if (!ok) {
    fprintf(stderr,
            "Could not create file mutation session: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }
  ok = harness_expect_session_error_contains(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_WRITE,
    "{\"path\":\"blocked.txt\",\"content\":\"blocked\"}",
    "file_write is available only in Coding Assistant sessions") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_EDIT,
      "{\"path\":\"blocked.txt\",\"old_text\":\"a\","
      "\"new_text\":\"b\"}",
      "file_edit is available only in Coding Assistant sessions") &&
    strappy_db_update_session_assistant_set(
      context->catalog_path,
      session_id,
      STRAPPY_ASSISTANT_SET_CODING_ASSISTANT,
      &error);
  if (!ok) {
    fprintf(stderr,
            "Could not isolate file mutations to Coding Assistant: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }

  root = harness_session_tool_output_json(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_WRITE,
    "{\"path\":\"file_mutation_nested/deep/created.txt\","
    "\"content\":\"alpha\\n\xCE\xB2" "eta\\n\"}");
  ok = harness_empty_object_result(root) &&
    harness_file_bytes_match(write_path,
                             initial_write,
                             sizeof(initial_write) - 1U) &&
    (stat(deep_dir, &file_stat) == 0) && S_ISDIR(file_stat.st_mode);
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr, "file_write did not create the nested UTF-8 file.\n");
    goto cleanup;
  }

  root = harness_session_tool_output_json(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_WRITE,
    "{\"path\":\"file_mutation_nested/deep/created.txt\","
    "\"content\":\"\"}");
  ok = harness_empty_object_result(root) &&
    harness_file_bytes_match(write_path, NULL, 0U);
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr, "file_write did not truncate to an empty file.\n");
    goto cleanup;
  }

  root = harness_session_tool_output_json(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_WRITE,
    "{\"path\":\"file_mutation_nested/deep/created.txt\","
    "\"content\":\"literal\\\\u0000\"}");
  ok = harness_empty_object_result(root) &&
    harness_file_bytes_match(
      write_path,
      (const unsigned char *)"literal\\u0000",
      strlen("literal\\u0000"));
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr, "file_write rejected a literal escaped null sequence.\n");
    goto cleanup;
  }

  written = snprintf(absolute_arguments,
                     sizeof(absolute_arguments),
                     "{\"path\":\"%s\",\"content\":\"absolute\"}",
                     absolute_path);
  if ((written <= 0) || ((size_t)written >= sizeof(absolute_arguments))) {
    fprintf(stderr, "Could not build absolute file_write arguments.\n");
    ok = 0;
    goto cleanup;
  }
  root = harness_session_tool_output_json(context->catalog_path,
                                          session_id,
                                          STRAPPY_TOOL_FILE_WRITE,
                                          absolute_arguments);
  ok = harness_empty_object_result(root) &&
    harness_file_bytes_match(absolute_path,
                             (const unsigned char *)"absolute",
                             8U);
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr, "file_write absolute path result did not match.\n");
    goto cleanup;
  }

  ok = harness_write_file_bytes(edit_path,
                                edit_fixture,
                                sizeof(edit_fixture) - 1U) &&
    (chmod(edit_path, 0755) == 0) &&
    harness_write_file_bytes(duplicate_path,
                             duplicate_fixture,
                             sizeof(duplicate_fixture) - 1U) &&
    harness_write_file_bytes(invalid_path,
                             invalid_utf8_fixture,
                             sizeof(invalid_utf8_fixture));
  if (!ok) {
    goto cleanup;
  }

  root = harness_session_tool_output_json(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_EDIT,
    "{\"path\":\"file_edit_crlf.txt\","
    "\"old_text\":\"second \xCE\xB2\\nthird\","
    "\"new_text\":\"changed\\nline\"}");
  ok = harness_empty_object_result(root) &&
    harness_file_bytes_match(edit_path,
                             first_edit_result,
                             sizeof(first_edit_result) - 1U) &&
    (stat(edit_path, &file_stat) == 0) &&
    ((file_stat.st_mode & 0777) == 0755);
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr,
            "file_edit did not preserve BOM, CRLF, or permissions.\n");
    goto cleanup;
  }

  root = harness_session_tool_output_json(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_EDIT,
    "{\"path\":\"file_edit_crlf.txt\","
    "\"old_text\":\"changed\\n\",\"new_text\":\"\"}");
  ok = harness_empty_object_result(root) &&
    harness_file_bytes_match(edit_path,
                             deletion_result,
                             sizeof(deletion_result) - 1U);
  cJSON_Delete(root);
  root = NULL;
  if (!ok) {
    fprintf(stderr, "file_edit deletion result did not match.\n");
    goto cleanup;
  }

  ok = harness_expect_session_error_contains(
    context->catalog_path,
    session_id,
    STRAPPY_TOOL_FILE_WRITE,
    "{\"path\":\".\",\"content\":\"x\"}",
    "Path is not a regular file") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_WRITE,
      "{\"path\":\"invalid.txt\",\"content\":\"x\","
      "\"extra\":true}",
      "file_write accepts only path and content") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_WRITE,
      "{\"path\":\"invalid.txt\"}",
      "file_write requires path and content strings") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_WRITE,
      "{\"path\":\"invalid.txt\",\"content\":\"a\\u0000b\"}",
      "file_write arguments cannot contain U+0000") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_EDIT,
      "{\"path\":\"file_edit_duplicate.txt\","
      "\"old_text\":\"same\",\"new_text\":\"changed\"}",
      "old_text matches more than once") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_EDIT,
      "{\"path\":\"file_edit_crlf.txt\","
      "\"old_text\":\"missing\",\"new_text\":\"changed\"}",
      "Could not find file_edit old_text") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_EDIT,
      "{\"path\":\"file_edit_crlf.txt\","
      "\"old_text\":\"line\",\"new_text\":\"line\"}",
      "file_edit would not change") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_EDIT,
      "{\"path\":\"file_edit_crlf.txt\","
      "\"old_text\":\"\",\"new_text\":\"x\"}",
      "old_text must be a valid UTF-8 string that is not empty") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_EDIT,
      "{\"path\":\"file_edit_crlf.txt\","
      "\"old_text\":\"line\",\"new_text\":\"a\\u0000b\"}",
      "file_edit arguments cannot contain U+0000") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_EDIT,
      "{\"path\":\"file_edit_invalid.txt\","
      "\"old_text\":\"a\",\"new_text\":\"b\"}",
      "File is not valid UTF-8 text") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_EDIT,
      "{\"path\":\"file_edit_missing.txt\","
      "\"old_text\":\"a\",\"new_text\":\"b\"}",
      "File not found") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_EDIT,
      "{\"path\":\".\",\"old_text\":\"a\","
      "\"new_text\":\"b\"}",
      "Path is not a regular file") &&
    harness_expect_session_error_contains(
      context->catalog_path,
      session_id,
      STRAPPY_TOOL_FILE_EDIT,
      "{\"path\":\"file_edit_crlf.txt\","
      "\"old_text\":\"line\",\"new_text\":\"x\","
      "\"edits\":[]}",
      "file_edit accepts only path, old_text, and new_text");

cleanup:
  cJSON_Delete(root);
  free(error);
  if (write_path[0] != '\0') {
    unlink(write_path);
  }
  if (absolute_path[0] != '\0') {
    unlink(absolute_path);
  }
  if (edit_path[0] != '\0') {
    unlink(edit_path);
  }
  if (duplicate_path[0] != '\0') {
    unlink(duplicate_path);
  }
  if (invalid_path[0] != '\0') {
    unlink(invalid_path);
  }
  if (deep_dir[0] != '\0') {
    rmdir(deep_dir);
  }
  if (nested_dir[0] != '\0') {
    rmdir(nested_dir);
  }
  return ok;
}

int main(void)
{
  harness_context context;
  int ok;

  harness_context_init(&context);
  ok = harness_run_tool_registry_tests() &&
       harness_run_assistant_set_tests() &&
       harness_run_helper_datetime_tests() &&
       harness_run_helper_fontawesome_tests() &&
       harness_make_temp_dir(&context) &&
       harness_run_fresh_catalog_schema_tests(&context) &&
       harness_run_provider_account_reset_test(&context) &&
       harness_run_discovered_database_replacement_tests(&context) &&
       harness_run_catalog_reclassification_tests(&context) &&
       harness_run_file_scanner_filename_filter_tests(&context) &&
       harness_run_file_scanner_batch_catalog_tests(&context) &&
       harness_run_file_scanner_hidden_tests(&context) &&
       harness_run_empty_database_list_tests(&context) &&
       harness_create_user_database(context.database_path) &&
       harness_register_database(&context) &&
       harness_run_database_list_tests(&context) &&
       harness_run_database_query_tests(&context) &&
       harness_run_missing_database_query_guidance_test(&context) &&
       harness_run_readonly_wal_database_query_test(&context) &&
       harness_run_wide_schema_database_query_test(&context) &&
       harness_run_helper_info_tests(&context) &&
       harness_run_assistant_scoped_memory_tests(&context) &&
       harness_run_database_context_limit_tests(&context) &&
       harness_run_session_options_tests(&context) &&
       harness_run_empty_session_storage_tests(&context) &&
       harness_run_bundled_model_catalog_tests(&context) &&
       harness_run_openrouter_model_catalog_tests(&context) &&
       harness_run_provider_account_routing_tests(&context) &&
       harness_run_sms_context_tests(&context) &&
       harness_run_mail_context_tests(&context) &&
       harness_run_database_study_same_batch_guard_tests(&context) &&
       harness_run_database_study_batch_planning_tests(&context) &&
       harness_run_database_study_coverage_tests(&context) &&
       harness_run_file_read_tests(&context) &&
       harness_run_file_mutation_tests(&context) &&
       harness_run_skills_tests(&context);

  harness_context_destroy(&context);

  if (!ok) {
    fprintf(stderr, "database_query_harness failed.\n");
    return 1;
  }

  printf("database_query_harness passed.\n");
  return 0;
}
