#define _POSIX_C_SOURCE 200809L

#include "strappy_core.h"
#include "strappy_config.h"
#include "strappy_db.h"
#include "strappy_file_scanner.h"
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
#define HARNESS_DATABASE_GUIDANCE_RESOURCE "GuidanceDatabase.json"
#define HARNESS_AUDIT_GUIDANCE_RESOURCE "GuidanceAudit.json"

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
           "SELECT id, name, prompt, response, model, http_status, "
           "web_search_enabled, streaming_enabled, created_at "
           "FROM sessions LIMIT 0;",
           "sessions columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, session_id, turn_key, prompt_group_key, actor, "
           "api_role, render_role, context_policy, prompt, status, created_at "
           "FROM session_turns LIMIT 0;",
           "session_turns columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, session_id, turn_id, turn_key, prompt_group_key, "
           "actor, kind, api_role, render_role, role, content, model, "
           "http_status, metadata_json, render_state_json, message_json, "
           "reasoning, message_key, target_message_key, tool_call_id, "
           "tool_name, arguments_json, result_json, include_in_context, "
           "is_error, created_at FROM session_messages LIMIT 0;",
           "session_messages columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, assistant_database_id, path, size, modified_at, "
           "device, inode, is_valid_sqlite, validation_error, scan_status, "
           "user_decision, scan_root, app_group_key, app_name, "
           "app_bundle_id, app_container_path, app_bundle_path, app_source, "
           "origin_kind, location_tail, "
           "first_seen_at, last_seen_at, last_scanned_at "
           "FROM discovered_databases LIMIT 0;",
           "discovered_databases columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT path, user_decision, assistant_database_id, hidden, "
           "created_at, updated_at FROM database_access_settings LIMIT 0;",
           "database_access_settings columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT key, value, updated_at FROM app_settings LIMIT 0;",
           "app_settings columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, canonical_slug, hugging_face_id, name, description, "
           "context_length, created, architecture_modality, "
           "architecture_tokenizer, architecture_instruct_type, "
           "pricing_prompt, pricing_completion, pricing_request, "
           "pricing_image, pricing_audio, pricing_web_search, "
           "pricing_internal_reasoning, "
           "pricing_input_cache_read, pricing_input_cache_write, "
           "top_provider_context_length, top_provider_max_completion_tokens, "
           "top_provider_is_moderated, knowledge_cutoff, expiration_date, "
           "links_details, links_json, reasoning_json, benchmarks_json, "
           "default_parameters_json, per_request_limits_json, raw_json, fetched_at "
           "FROM openrouter_models LIMIT 0;",
           "openrouter_models columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT model_id, modality "
           "FROM openrouter_model_input_modalities LIMIT 0;",
           "openrouter_model_input_modalities columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT model_id, modality "
           "FROM openrouter_model_output_modalities LIMIT 0;",
           "openrouter_model_output_modalities columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT model_id, parameter "
           "FROM openrouter_model_supported_parameters LIMIT 0;",
           "openrouter_model_supported_parameters columns") &&
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT model_id, voice "
           "FROM openrouter_model_supported_voices LIMIT 0;",
           "openrouter_model_supported_voices columns");
}

static char *harness_read_file(const char *path)
{
  FILE *file;
  char *buffer;
  long length;
  size_t byte_count;
  size_t read_count;

  if ((path == NULL) || (path[0] == '\0')) {
    fprintf(stderr, "Harness file path is empty.\n");
    return NULL;
  }

  file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open harness resource: %s\n", path);
    return NULL;
  }

  if ((fseek(file, 0L, SEEK_END) != 0) ||
      ((length = ftell(file)) < 0L) ||
      (fseek(file, 0L, SEEK_SET) != 0)) {
    fprintf(stderr, "Could not measure harness resource: %s\n", path);
    fclose(file);
    return NULL;
  }

  byte_count = (size_t)length;
  buffer = (char *)malloc(byte_count + 1U);
  if (buffer == NULL) {
    fprintf(stderr, "Could not allocate harness resource buffer.\n");
    fclose(file);
    return NULL;
  }

  read_count = fread(buffer, 1U, byte_count, file);
  fclose(file);
  if (read_count != byte_count) {
    fprintf(stderr, "Could not read harness resource: %s\n", path);
    free(buffer);
    return NULL;
  }

  buffer[byte_count] = '\0';
  return buffer;
}

static cJSON *harness_read_json_resource(const char *resource_name)
{
  char path[1200];
  char *text;
  cJSON *root;

  if (!harness_join_path(path,
                         sizeof(path),
                         HARNESS_RESOURCE_DIR,
                         resource_name)) {
    fprintf(stderr, "Could not build harness resource path.\n");
    return NULL;
  }

  text = harness_read_file(path);
  if (text == NULL) {
    return NULL;
  }

  root = cJSON_Parse(text);
  free(text);
  if (root == NULL) {
    fprintf(stderr, "Could not parse harness JSON resource: %s\n", path);
    return NULL;
  }

  return root;
}

static int harness_json_string_equals(cJSON *object,
                                      const char *key,
                                      const char *expected)
{
  cJSON *value;

  value = cJSON_IsObject(object) ? cJSON_GetObjectItem(object, key) : NULL;
  return cJSON_IsString(value) && (value->valuestring != NULL) &&
    (strcmp(value->valuestring, expected) == 0);
}

static int harness_run_audit_resource_tests(void)
{
  cJSON *root;
  cJSON *rules;
  cJSON *audit_header;
  cJSON *audit_footer;
  cJSON *empty_answer;
  cJSON *database_context_rule;
  cJSON *session_name_rule;
  cJSON *fontawesome_confirm_rule;
  cJSON *user_fact_rule;
  cJSON *database_hint_rule;
  cJSON *database_context_message;
  cJSON *session_name_message;
  cJSON *fontawesome_confirm_message;
  cJSON *user_fact_message;
  cJSON *database_hint_message;
  cJSON *rule;
  cJSON *message;
  int index;
  int rules_avoid_finalization;
  int ok;

  root = harness_read_json_resource(HARNESS_AUDIT_GUIDANCE_RESOURCE);
  audit_header = cJSON_IsObject(root) ?
    cJSON_GetObjectItem(root, "audit_header") : NULL;
  audit_footer = cJSON_IsObject(root) ?
    cJSON_GetObjectItem(root, "audit_footer") : NULL;
  empty_answer = cJSON_IsObject(root) ?
    cJSON_GetObjectItem(root, "empty_answer") : NULL;
  rules = cJSON_IsObject(root) ?
    cJSON_GetObjectItem(root, "tool_usage_priority") : NULL;
  database_context_rule = cJSON_GetArrayItem(rules, 0);
  session_name_rule = cJSON_GetArrayItem(rules, 1);
  fontawesome_confirm_rule = cJSON_GetArrayItem(rules, 2);
  user_fact_rule = cJSON_GetArrayItem(rules, 3);
  database_hint_rule = cJSON_GetArrayItem(rules, 4);
  database_context_message = cJSON_IsObject(database_context_rule) ?
    cJSON_GetObjectItem(database_context_rule, "if_not_called") : NULL;
  session_name_message = cJSON_IsObject(session_name_rule) ?
    cJSON_GetObjectItem(session_name_rule, "if_not_called") : NULL;
  fontawesome_confirm_message = cJSON_IsObject(fontawesome_confirm_rule) ?
    cJSON_GetObjectItem(fontawesome_confirm_rule, "if_not_called") : NULL;
  user_fact_message = cJSON_IsObject(user_fact_rule) ?
    cJSON_GetObjectItem(user_fact_rule, "if_not_called") : NULL;
  database_hint_message = cJSON_IsObject(database_hint_rule) ?
    cJSON_GetObjectItem(database_hint_rule, "if_not_called") : NULL;
  rules_avoid_finalization = cJSON_IsArray(rules) ? 1 : 0;
  for (index = 0;
       rules_avoid_finalization && (index < cJSON_GetArraySize(rules));
       index++) {
    rule = cJSON_GetArrayItem(rules, index);
    message = cJSON_IsObject(rule) ?
      cJSON_GetObjectItem(rule, "if_not_called") : NULL;
    if (!cJSON_IsString(message) || (message->valuestring == NULL) ||
        (strstr(message->valuestring, "final") != NULL)) {
      rules_avoid_finalization = 0;
    }
  }

  ok = cJSON_IsString(audit_header) &&
    (audit_header->valuestring != NULL) &&
    (strstr(audit_header->valuestring,
            "audited due to your failure to follow instructions") != NULL) &&
    cJSON_IsString(audit_footer) &&
    (audit_footer->valuestring != NULL) &&
    (strstr(audit_footer->valuestring,
            "After fixing all of the issues above") != NULL) &&
    (strstr(audit_footer->valuestring, "standalone answer") != NULL) &&
    (strstr(audit_footer->valuestring, "original question") != NULL) &&
    (strstr(audit_footer->valuestring, "all fixes in place") != NULL) &&
    cJSON_IsString(empty_answer) &&
    (empty_answer->valuestring != NULL) &&
    (strcmp(empty_answer->valuestring,
            "Your answer was empty. Please answer the user's original "
            "question.") == 0) &&
    rules_avoid_finalization &&
    cJSON_IsArray(rules) && (cJSON_GetArraySize(rules) == 5) &&
    harness_json_string_equals(database_context_rule,
                               "tool_name",
                               STRAPPY_TOOL_DATABASE_CONTEXT_READ) &&
    (cJSON_GetObjectItem(database_context_rule, "when") == NULL) &&
    cJSON_IsString(database_context_message) &&
    (database_context_message->valuestring != NULL) &&
    (strstr(database_context_message->valuestring, "ALWAYS call") != NULL) &&
    (strstr(database_context_message->valuestring,
            "no database context was needed") != NULL) &&
    harness_json_string_equals(session_name_rule,
                               "tool_name",
                               STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE) &&
    (cJSON_GetObjectItem(session_name_rule, "when") == NULL) &&
    cJSON_IsString(session_name_message) &&
    (session_name_message->valuestring != NULL) &&
    (strstr(session_name_message->valuestring, "ALWAYS call") != NULL) &&
    (strstr(session_name_message->valuestring,
            "latest prompt") != NULL) &&
    harness_json_string_equals(
      fontawesome_confirm_rule,
      "tool_name",
      STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM) &&
    (cJSON_GetObjectItem(fontawesome_confirm_rule, "when") == NULL) &&
    cJSON_IsString(fontawesome_confirm_message) &&
    (fontawesome_confirm_message->valuestring != NULL) &&
    (strstr(fontawesome_confirm_message->valuestring,
            "ALWAYS call") != NULL) &&
    (strstr(fontawesome_confirm_message->valuestring,
            "non-empty array") != NULL) &&
    (strstr(fontawesome_confirm_message->valuestring,
            "null and empty arrays are invalid") != NULL) &&
    (strstr(fontawesome_confirm_message->valuestring,
            "NEVER use unicode emoji") != NULL) &&
    harness_json_string_equals(user_fact_rule,
                               "tool_name",
                               STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER) &&
    (cJSON_GetObjectItem(user_fact_rule, "when") == NULL) &&
    cJSON_IsString(user_fact_message) &&
    (user_fact_message->valuestring != NULL) &&
    (strstr(user_fact_message->valuestring, "ALWAYS call") != NULL) &&
    (strstr(user_fact_message->valuestring,
            "nothing new to remember") != NULL) &&
    (strstr(user_fact_message->valuestring,
            "NEVER remember secrets or sensitive information") != NULL) &&
    harness_json_string_equals(database_hint_rule,
                               "tool_name",
                               STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER) &&
    (cJSON_GetObjectItem(database_hint_rule, "when") == NULL) &&
    cJSON_IsString(database_hint_message) &&
    (database_hint_message->valuestring != NULL) &&
    (strstr(database_hint_message->valuestring,
            "ALWAYS call") != NULL) &&
    (strstr(database_hint_message->valuestring,
            "query, schema, or access information") != NULL) &&
    (strstr(database_hint_message->valuestring,
            "nothing new to remember") != NULL) &&
    (strstr(database_hint_message->valuestring,
            "NEVER remember user data, secrets, sensitive information") != NULL) &&
    (strstr(database_hint_message->valuestring,
            "guesses, or one-off query results") != NULL);
  cJSON_Delete(root);
  if (!ok) {
    fprintf(stderr, "Audit guidance order or memory safeguards are invalid.\n");
  }
  return ok;
}

static char *harness_database_default_string(const char *key)
{
  cJSON *root;
  cJSON *value;
  char *copy;

  root = harness_read_json_resource(HARNESS_DATABASE_GUIDANCE_RESOURCE);
  if (root == NULL) {
    return NULL;
  }

  value = cJSON_GetObjectItem(root, key);
  if (!cJSON_IsString(value) || (value->valuestring == NULL)) {
    fprintf(stderr,
            "Database defaults are missing %s.\n",
            (key != NULL) ? key : "(null)");
    cJSON_Delete(root);
    return NULL;
  }

  copy = strappy_string_duplicate(value->valuestring);
  cJSON_Delete(root);
  if (copy == NULL) {
    fprintf(stderr, "Could not copy database guidance string.\n");
  }

  return copy;
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
    "(42);");

  sqlite3_close(db);
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

static int harness_expect_context_default_description(
  const char *catalog_path,
  const char *arguments_json)
{
  char *expected;
  cJSON *root;
  cJSON *description;
  int ok;

  expected = harness_database_default_string("default_description");
  if (expected == NULL) {
    return 0;
  }

  root = harness_tool_output_json(catalog_path,
                                  STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                  arguments_json);
  if (root == NULL) {
    free(expected);
    return 0;
  }

  description = cJSON_GetObjectItem(root, "description");
  ok = (cJSON_IsString(description) &&
        (description->valuestring != NULL) &&
        (strcmp(description->valuestring, expected) == 0)) ? 1 : 0;
  if (!ok) {
    fprintf(stderr,
            "Database context description did not match the generic default.\n");
  }

  cJSON_Delete(root);
  free(expected);
  return ok;
}

static int harness_expect_list_default_description(const char *catalog_path)
{
  char *expected;
  cJSON *root;
  cJSON *databases;
  cJSON *database;
  int ok;

  expected = harness_database_default_string("default_description_short");
  if (expected == NULL) {
    return 0;
  }

  root = harness_tool_output_json(catalog_path,
                                  STRAPPY_TOOL_DATABASE_LIST_INFO,
                                  "{}");
  if (root == NULL) {
    free(expected);
    return 0;
  }

  ok = 0;
  databases = cJSON_GetObjectItem(root, "databases");
  if (cJSON_IsArray(databases)) {
    for (database = databases->child; database != NULL; database = database->next) {
      cJSON *description_short;

      description_short = cJSON_GetObjectItem(database, "description_short");
      if (cJSON_IsString(description_short) &&
          (description_short->valuestring != NULL) &&
          (strcmp(description_short->valuestring, expected) == 0)) {
        ok = 1;
        break;
      }
    }
  }

  if (!ok) {
    fprintf(stderr,
            "Database list did not contain the generic description_short.\n");
  }

  cJSON_Delete(root);
  free(expected);
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

static int harness_expect_output_equals(const char *catalog_path,
                                        const char *tool_name,
                                        const char *arguments_json,
                                        const char *expected)
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
        (filtered_json != NULL) && cJSON_IsArray(filtered) &&
        (cJSON_GetArraySize(filtered) == 1) &&
        harness_tool_schemas_hide_display_metadata(filtered) &&
        (registry_json != NULL) && cJSON_IsObject(registry) &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_DATABASE_QUERY,
                                     "database_id",
                                     "database_filename") &&
        harness_tool_display_matches(
          registry,
          STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH,
          "query",
          NULL) &&
        harness_tool_display_matches(
          registry,
          STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
          "shortcodes",
          "comma_separated") &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_MEMORY_USER_FACT_READ,
                                     "query",
                                     NULL) &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
                                     "fact",
                                     NULL) &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_MEMORY_USER_FACT_FORGET,
                                     "id",
                                     "identifier") &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
                                     "name",
                                     NULL) &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                     "database_id",
                                     "database_filename") &&
        harness_tool_display_matches(
          registry,
          STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
          "database_id",
          "database_filename") &&
        harness_tool_display_matches(registry,
                                     STRAPPY_TOOL_MEMORY_DATABASE_HINT_FORGET,
                                     "id",
                                     "database_hint_filename") &&
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
        (cJSON_GetObjectItem(registry,
                             STRAPPY_TOOL_DATABASE_LIST_INFO) == NULL) &&
        (cJSON_GetObjectItem(
           registry,
           STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601) == NULL) &&
        (cJSON_GetObjectItem(
           registry,
           STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601) == NULL) &&
        (strstr(tools_json,
                "Call this tool to view available databases") != NULL) &&
        (strstr(tools_json,
                "ALWAYS query the relevant approved database before "
                "finalizing when the request depends on personal data.") !=
         NULL) &&
        (strstr(tools_json,
                "ALWAYS call this tool when displaying numeric timestamps.") !=
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
                "Free shortcodes to confirm before using them. Null and "
                "empty arrays are invalid.") != NULL) &&
        (strstr(tools_json, "NEVER use unicode emoji.") != NULL) &&
        (strstr(tools_json,
                "ALWAYS call this tool before the final answer. Update the "
                "session with a short, descriptive name for the user's "
                "latest prompt. A non-empty name is required; null and empty "
                "values are invalid.") !=
        NULL) &&
        (strstr(tools_json,
                "ALWAYS call this tool. Set fact to a useful durable user "
                "fact, or null if there is nothing new to remember.") !=
         NULL) &&
        (strstr(tools_json,
                "ALWAYS call this tool. Set database_id to the approved "
                "database ID and hint to useful durable query, schema, or "
                "access information, or set both to null if there is nothing "
                "new to remember.") !=
         NULL) &&
        (strstr(tools_json,
                "ALWAYS call this tool before the final answer. Read relevant "
                "approved database context before calling database_query.") !=
         NULL) &&
        (strstr(tools_json,
                "empty strings to report that no database context was "
                "needed.") != NULL) &&
        (strstr(tools_json,
                "Call this tool to forget durable facts that are no longer "
                "correct or useful.") != NULL) &&
        (strstr(filtered_json,
                "ALWAYS query the relevant approved database before "
                "finalizing when the request depends on personal data.") !=
         NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_DATABASE_LIST_INFO) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_DATABASE_QUERY) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_MEMORY_USER_FACT_READ) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_MEMORY_USER_FACT_FORGET) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_DATABASE_CONTEXT_READ) != NULL) &&
        (strstr(tools_json,
                STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER) != NULL) &&
        (strstr(tools_json,
                STRAPPY_TOOL_MEMORY_DATABASE_HINT_FORGET) != NULL) &&
        (strstr(tools_json,
                STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH) != NULL) &&
        (strstr(tools_json,
                STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM) != NULL) &&
        strappy_tools_is_helper(STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601) &&
        strappy_tools_is_helper(STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601) &&
        strappy_tools_is_helper(STRAPPY_TOOL_MEMORY_USER_FACT_READ) &&
        strappy_tools_is_helper(STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER) &&
        strappy_tools_is_helper(STRAPPY_TOOL_MEMORY_USER_FACT_FORGET) &&
        strappy_tools_is_helper(STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE) &&
        strappy_tools_is_helper(STRAPPY_TOOL_DATABASE_CONTEXT_READ) &&
        strappy_tools_is_helper(STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER) &&
        strappy_tools_is_helper(STRAPPY_TOOL_MEMORY_DATABASE_HINT_FORGET) &&
        strappy_tools_is_helper(STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH) &&
        strappy_tools_is_helper(STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM) &&
        !strappy_tools_is_helper(STRAPPY_TOOL_DATABASE_QUERY) &&
        !strappy_tools_is_helper("helper_convert_dates") &&
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

static int harness_run_helper_datetime_tests(void)
{
  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601,
        "{\"timestamps\":\"0,1,-1,-0.5\"}",
        "1970-01-01T00:00:00Z,1970-01-01T00:00:01Z,"
        "1969-12-31T23:59:59Z,1969-12-31T23:59:59.5Z")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601,
        "{\"timestamps\":\"1700000000123\",\"unit\":\"unix_milliseconds\"}",
        "2023-11-14T22:13:20.123Z")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601,
        "{\"timestamps\":\"0, 1.25\",\"unit\":\"apple_seconds\"}",
        "2001-01-01T00:00:00Z,2001-01-01T00:00:01.25Z")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601,
        "{\"timestamps\":\"1,,2\"}",
        "empty item")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_DATETIME_TO_ISO8601,
        "{\"timestamps\":\"1\",\"unit\":\"banana_seconds\"}",
        "unit is not supported")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601,
        "{\"datetimes\":\"1970-01-01T00:00:00Z,1970-01-01T00:00:01Z\","
        "\"unit\":\"unix_seconds\"}",
        "0,1")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601,
        "{\"datetimes\":\"2001-01-01T00:00:00Z,2026-01-01T00:00:00Z,"
        "2027-01-01T00:00:00Z\",\"unit\":\"apple_seconds\"}",
        "0,788918400,820454400")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601,
        "{\"datetimes\":\"2026-01-01,2027-01-01\"}",
        "788918400,820454400")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601,
        "{\"datetimes\":\"1969-12-31T23:59:59.5Z\","
        "\"unit\":\"unix_seconds\"}",
        "-0.5")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601,
        "{\"datetimes\":\"2026-01-01T05:00:00+05:00\","
        "\"unit\":\"apple_seconds\"}",
        "788918400")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601,
        "{\"datetimes\":\"2026-01-01,,2027-01-01\"}",
        "empty item")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601,
        "{\"datetimes\":\"2026-02-30\"}",
        "date is invalid")) {
    return 0;
  }

  return 1;
}

static int harness_run_helper_fontawesome_tests(void)
{
  if (!harness_expect_output_contains_without(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH,
        "{\"query\":\"warning\"}",
        "\"shortcode\":\"[fa:triangle-exclamation]\"",
        "\"label\":\"Triangle Exclamation\"",
        "codepoint")) {
    return 0;
  }

  if (!harness_expect_output_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH,
        "{\"query\":\"github\"}",
        "\"shortcode\":\"[fa:brands:github]\"",
        "\"style\":\"brands\"")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH,
        "{\"query\":\"github\",\"style\":\"brands\"}",
        "does not accept argument 'style'")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH,
        "{\"query\":\"warning\",\"limit\":5}",
        "does not accept argument 'limit'")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH,
        "{}",
        "requires a non-empty query string")) {
    return 0;
  }

  if (!harness_expect_output_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
        "{\"shortcodes\":[\"[fa:heart]\",\"database\",\"[fa:not-real]\"]}",
        "\"available_count\":2",
        "\"unavailable_count\":1")) {
    return 0;
  }

  if (!harness_expect_output_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
        "{\"shortcodes\":[\"[fa:brands:github]\",\"[fa:regular:heart]\"]}",
        "\"shortcode\":\"[fa:brands:github]\"",
        "\"shortcode\":\"[fa:regular:heart]\"")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
        NULL,
        "requires a shortcodes array") ||
      !harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
        "null",
        "must be a JSON object") ||
      !harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
        "{}",
        "requires a shortcodes array") ||
      !harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
        "{\"shortcodes\":null}",
        "requires a shortcodes array") ||
      !harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
        "{\"shortcodes\":[]}",
        "shortcodes must contain 1 to") ||
      !harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
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

static int harness_run_database_list_info_tests(const harness_context *context)
{
  char *error;
  char *output;
  int ok;

  if (context == NULL) {
    return 0;
  }

  error = NULL;
  output = strappy_tools_execute(context->catalog_path,
                                 0LL,
                                 HARNESS_RESOURCE_DIR,
                                 STRAPPY_TOOL_DATABASE_LIST_INFO,
                                 "{}",
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Expected database_list_info output but got error: %s\n",
            (error != NULL) ? error : "(null)");
    free(error);
    return 0;
  }

  ok = ((strstr(output, "\"database_id\"") != NULL) &&
        (strstr(output, "\"filename\":\"user.sqlite\"") != NULL) &&
        (strstr(output, "\"description_short\"") != NULL) &&
        (strstr(output, "\"description\":") == NULL) &&
        (strstr(output, "\"availability_state\":\"available\"") != NULL) &&
        (strstr(output, "\"schema\"") == NULL) &&
        (strstr(output, "\"database_info\"") == NULL) &&
        (strstr(output, "\"database_hints\"") == NULL) &&
        (strstr(output, "\"remembered_info\"") == NULL) &&
        (strstr(output, "\"user_action\"") == NULL) &&
        (strstr(output, "\"messages\"") == NULL) &&
        (strstr(output, "\"database_query\"") == NULL) &&
        (strstr(output, "learned_info") == NULL) &&
        (strstr(output, "database_learn") == NULL)) ? 1 : 0;
  if (!ok) {
    fprintf(stderr, "database_list_info output was not expected: %s\n", output);
  }

  free(output);
  return ok;
}

static int harness_run_empty_database_list_info_tests(
  const harness_context *context)
{
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
                                 STRAPPY_TOOL_DATABASE_LIST_INFO,
                                 "{}",
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Expected empty database_list_info output but got error: %s\n",
            (error != NULL) ? error : "(null)");
    free(error);
    return 0;
  }

  ok = ((strstr(output, "\"count\":0") != NULL) &&
        (strstr(output, "\"availability_state\":\"no_approved_databases\"") != NULL) &&
        (strstr(output, "\"user_action\"") != NULL) &&
        (strstr(output, "\"href\":\"strappy://database-manage\"") != NULL) &&
        (strstr(output, "scan_needed") == NULL) &&
        (strstr(output, "whitelist_needed") == NULL) &&
        (strstr(output, "possible_scan_needed") == NULL) &&
        (strstr(output, "possible_whitelist_needed") == NULL)) ? 1 : 0;
  if (!ok) {
    fprintf(stderr, "empty database_list_info output was not expected: %s\n", output);
  }

  free(output);
  return ok;
}

typedef struct harness_scanner_batch_context {
  const char *catalog_path;
  const char *scan_root;
} harness_scanner_batch_context;

static int harness_save_scanner_batch(strappy_file_scanner_record_list *list,
                                      void *user_data,
                                      char **error_out)
{
  harness_scanner_batch_context *context;

  context = (harness_scanner_batch_context *)user_data;
  if ((context == NULL) || (context->catalog_path == NULL)) {
    strappy_set_error(error_out, "Harness scanner batch context is missing.");
    return 0;
  }

  return strappy_file_scanner_save_discovered_database_batch(
    context->catalog_path,
    list,
    context->scan_root,
    error_out);
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
  char stale_path[1200];
  strappy_discovered_database_input stale_input;
  strappy_discovered_database_record_list catalog_list;
  strappy_file_scanner_options options;
  strappy_file_scanner_record_list scan_list;
  harness_scanner_batch_context batch_context;
  char *error;
  size_t index;
  int found_first;
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
      !harness_join_path(stale_path,
                         sizeof(stale_path),
                         documents_dir,
                         "stale.sqlite")) {
    fprintf(stderr, "Could not build batch scanner test paths.\n");
    return 0;
  }

  harness_unlink_sqlite_files(catalog_path);
  harness_unlink_sqlite_files(first_path);
  harness_unlink_sqlite_files(stale_path);

  if (!harness_ensure_directory(scan_root, "mkdir batch scan root") ||
      !harness_ensure_directory(documents_dir, "mkdir batch Documents") ||
      !harness_create_user_database(first_path)) {
    harness_unlink_sqlite_files(catalog_path);
    harness_unlink_sqlite_files(first_path);
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
    return 0;
  }

  batch_context.catalog_path = catalog_path;
  batch_context.scan_root = scan_root;
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
    return 0;
  }
  if ((scan_list.records != NULL) || (scan_list.count != 0U)) {
    fprintf(stderr, "Batch catalog scanner left unflushed records.\n");
    strappy_file_scanner_record_list_destroy(&scan_list);
    harness_unlink_sqlite_files(catalog_path);
    harness_unlink_sqlite_files(first_path);
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
    return 0;
  }

  found_first = 0;
  found_stale = 0;
  for (index = 0U; index < catalog_list.count; index++) {
    if ((catalog_list.records[index].path != NULL) &&
        (strcmp(catalog_list.records[index].path, first_path) == 0)) {
      found_first = 1;
    }
    if ((catalog_list.records[index].path != NULL) &&
        (strcmp(catalog_list.records[index].path, stale_path) == 0)) {
      found_stale = 1;
    }
  }

  ok = (found_first && !found_stale) ? 1 : 0;
  if (!ok) {
    fprintf(stderr, "Batch catalog scanner did not replace stale rows correctly.\n");
  }

  strappy_discovered_database_record_list_destroy(&catalog_list);
  strappy_file_scanner_record_list_destroy(&scan_list);
  harness_unlink_sqlite_files(catalog_path);
  harness_unlink_sqlite_files(first_path);
  harness_unlink_sqlite_files(stale_path);
  rmdir(documents_dir);
  rmdir(scan_root);
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
  cache_path[0] = '\0';
  app_library_path[0] = '\0';
  apple_index_path[0] = '\0';
  third_party_index_path[0] = '\0';
  strappy_file_scanner_record_list_init(&scan_list);
  strappy_discovered_database_record_list_init(&catalog_list);
  error = NULL;
  found_normal = 0;
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
  if (!found_normal || !found_cache || !found_app_library ||
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
  if (!found_normal || !found_cache || !found_app_library ||
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
        "requires non-empty database_id and sql strings")) {
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

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_QUERY,
                                      arguments,
                                      "alice",
                                      "\"rows\"")) {
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

  if (!harness_expect_output_contains_without(
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
       harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_QUERY,
                                      arguments,
                                      "\"rows\":[[1]]",
                                      "\"ok\":true");

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
  static const char *noop_result =
    "{\"ok\":true,\"remembered\":false,\"reason\":\"nothing_new\"}";
  static const char *context_noop_result =
    "{\"ok\":true,\"read\":false,\"reason\":\"no_database_needed\"}";
  char arguments[4096];
  int written;

  if ((context == NULL) || (context->database_id == NULL)) {
    return 0;
  }

  if (!harness_expect_output_equals(context->catalog_path,
                                    STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                    NULL,
                                    context_noop_result) ||
      !harness_expect_output_equals(context->catalog_path,
                                    STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                    "",
                                    context_noop_result) ||
      !harness_expect_output_equals(context->catalog_path,
                                    STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                    "null",
                                    context_noop_result) ||
      !harness_expect_output_equals(context->catalog_path,
                                    STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                    "\" NULL \"",
                                    context_noop_result) ||
      !harness_expect_output_equals(context->catalog_path,
                                    STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                    "{}",
                                    context_noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT_READ,
        "{\"database_id\":null,\"query\":null,\"kind\":null,\"limit\":null}",
        context_noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT_READ,
        "{\"database_id\":\"null\",\"query\":\"NULL\","
        "\"kind\":\" Null \",\"limit\":\"null\"}",
        context_noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT_READ,
        "{\"database_id\":\"  \",\"query\":\"\\n\",\"kind\":\"\\t\"}",
        context_noop_result)) {
    return 0;
  }

  if (!harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
        NULL,
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
        "null",
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
        "\"null\"",
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
        "{}",
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
        "{\"fact\":null}",
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
        "{\"fact\":\" NULL \"}",
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
        "{\"fact\":\"  \\n\\t\"}",
        noop_result)) {
    return 0;
  }

  if (!harness_expect_output_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
        "{\"fact\":\"The user's name is Jeff.\"}",
        "\"ok\":true",
        "\"id\":1")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_MEMORY_USER_FACT_READ,
                                      "{\"query\":\"Jeff\"}",
                                      "\"kind\":\"fact\"",
                                      "The user's name is Jeff.")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
        "{\"kind\":\"identity\",\"fact\":\"Jeff\"}",
        "does not accept argument 'kind'")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_MEMORY_USER_FACT_FORGET,
                                      "{\"id\":1}",
                                      "\"forgotten\":true",
                                      "\"ok\":true")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_MEMORY_USER_FACT_READ,
                                      "{\"query\":\"Jeff\"}",
                                      "\"facts\"",
                                      "\"count\":0")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
        NULL,
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
        "null",
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
        "\"null\"",
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
        "{}",
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
        "{\"database_id\":null,\"hint\":null}",
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
        "{\"database_id\":\"null\",\"hint\":\"null\"}",
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
        "{\"database_id\":\"unused\",\"hint\":\" NULL \"}",
        noop_result) ||
      !harness_expect_output_equals(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
        "{\"database_id\":\"unused\",\"hint\":\"\"}",
        noop_result)) {
    return 0;
  }

  if (!harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
        "{\"database_id\":null,\"hint\":\"Useful\"}",
        "requires database_id when hint is provided") ||
      !harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
        "{\"database_id\":\"null\",\"hint\":\"Useful\"}",
        "requires database_id when hint is provided")) {
    return 0;
  }

  written = snprintf(
    arguments,
    sizeof(arguments),
    "{\"database_id\":\"%s\","
    "\"hint\":\"The messages table can be checked against identifiers "
    "when validating exact integer serialization.\"}",
    context->database_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments))) {
    fprintf(stderr, "Could not build database hint remember arguments.\n");
    return 0;
  }

  if (!harness_expect_output_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
        arguments,
        "\"ok\":true",
        "\"id\":1")) {
    return 0;
  }

  written = snprintf(arguments,
                     sizeof(arguments),
                     "{\"database_id\":\"%s\",\"query\":\"identifiers\"}",
                     context->database_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments))) {
    fprintf(stderr, "Could not build database hint read arguments.\n");
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                      arguments,
                                      "\"schema\"",
                                      "\"messages\"")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                      arguments,
                                      "\"kind\":\"hint\"",
                                      "\"title\":\"Database hint\"")) {
    return 0;
  }

  written = snprintf(arguments,
                     sizeof(arguments),
                     "{\"database_id\":\"%s\",\"hint\":\"Useful\","
                     "\"title\":\"Old title\"}",
                     context->database_id);
  if ((written <= 0) || ((size_t)written >= sizeof(arguments))) {
    fprintf(stderr, "Could not build legacy database hint arguments.\n");
    return 0;
  }

  if (!harness_expect_error_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
        arguments,
        "does not accept argument 'title'")) {
    return 0;
  }

  if (!harness_expect_output_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_DATABASE_HINT_FORGET,
        "{\"id\":1}",
        "\"forgotten\":true",
        "\"ok\":true")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                      "{\"query\":\"identifiers\"}",
                                      "\"database_hints\"",
                                      "\"count\":0")) {
    return 0;
  }

  return 1;
}

static int harness_run_mail_guidance_tests(harness_context *context)
{
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

  if (!harness_expect_context_default_description(context->catalog_path,
                                                  arguments)) {
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

  if (!harness_expect_context_default_description(context->catalog_path,
                                                  arguments)) {
    return 0;
  }

  return 1;
}

static int harness_run_sms_guidance_tests(harness_context *context)
{
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

  if (!harness_expect_context_default_description(context->catalog_path,
                                                  arguments)) {
    return 0;
  }

  if (!harness_expect_list_default_description(context->catalog_path)) {
    return 0;
  }

  return 1;
}

static int harness_run_empty_session_storage_tests(const harness_context *context)
{
  strappy_session_record session;
  strappy_session_message_input inputs[2];
  strappy_session_message_record_list messages;
  long long session_id;
  char *error;
  char *output;
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
       (session.web_search_enabled == 1) &&
       (session.streaming_enabled == 0) &&
       (session.http_status == 0L);
  strappy_session_record_destroy(&session);
  if (!ok) {
    fprintf(stderr, "Empty session row did not have the expected shape.\n");
    return 0;
  }

  error = NULL;
  ok = strappy_db_update_session_streaming_enabled(context->catalog_path,
                                                   session_id,
                                                   1,
                                                   &error);
  if (!ok) {
    fprintf(stderr,
            "Could not update session streaming setting: %s\n",
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
            "Could not reload streaming session setting: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_record_destroy(&session);
    return 0;
  }
  ok = (session.streaming_enabled == 1);
  strappy_session_record_destroy(&session);
  if (!ok) {
    fprintf(stderr, "Session streaming setting was not stored.\n");
    return 0;
  }

  error = NULL;
  ok = strappy_db_update_session_web_search_enabled(context->catalog_path,
                                                    session_id,
                                                    0,
                                                    &error);
  if (!ok) {
    fprintf(stderr,
            "Could not update session web search setting: %s\n",
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
            "Could not reload web search session setting: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_record_destroy(&session);
    return 0;
  }
  ok = (session.web_search_enabled == 0);
  strappy_session_record_destroy(&session);
  if (!ok) {
    fprintf(stderr, "Session web search setting was not stored.\n");
    return 0;
  }

  if (!harness_expect_session_error_contains(
        context->catalog_path,
        session_id,
        STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
        NULL,
        "requires a non-empty name string") ||
      !harness_expect_session_error_contains(
        context->catalog_path,
        session_id,
        STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
        "{}",
        "requires a non-empty name string") ||
      !harness_expect_session_error_contains(
        context->catalog_path,
        session_id,
        STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
        "{\"name\":null}",
        "name must be a string") ||
      !harness_expect_session_error_contains(
        context->catalog_path,
        session_id,
        STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
        "{\"name\":\"\"}",
        "requires a non-empty name string")) {
    return 0;
  }

  error = NULL;
  output = strappy_tools_execute(context->catalog_path,
                                 session_id,
                                 HARNESS_RESOURCE_DIR,
                                 STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
                                 "{\"name\":\"  Find   Receipts  \"}",
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Could not write session name: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  ok = (strstr(output, "\"name\":\"Find Receipts\"") != NULL) &&
       (strstr(output, "\"updated\":true") != NULL);
  free(output);
  if (!ok) {
    fprintf(stderr, "Session name tool did not report a write.\n");
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
                                 STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
                                 "{\"name\":\"Different Title\"}",
                                 &error);
  if (output == NULL) {
    fprintf(stderr,
            "Could not run second session name write: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  ok = (strstr(output, "\"name\":\"Different Title\"") != NULL) &&
       (strstr(output, "\"updated\":true") != NULL) &&
       (strstr(output, "\"status\":\"updated\"") != NULL);
  free(output);
  if (!ok) {
    fprintf(stderr, "Second session name write did not report an update.\n");
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

  strappy_session_message_record_list_init(&messages);
  ok = strappy_db_list_session_messages(context->catalog_path,
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

  memset(inputs, 0, sizeof(inputs));
  inputs[0].turn_key = "empty-session-turn";
  inputs[0].prompt_group_key = "empty-session-group";
  inputs[0].actor = "user";
  inputs[0].context_policy = "full";
  inputs[0].kind = "prompt";
  inputs[0].api_role = "user";
  inputs[0].render_role = "user";
  inputs[0].role = "user";
  inputs[0].content = "First prompt";
  inputs[0].message_key = "empty-session-prompt";
  inputs[0].include_in_context = 1;

  inputs[1].turn_key = "empty-session-turn";
  inputs[1].prompt_group_key = "empty-session-group";
  inputs[1].actor = "user";
  inputs[1].context_policy = "full";
  inputs[1].kind = "assistant";
  inputs[1].api_role = "assistant";
  inputs[1].render_role = "assistant";
  inputs[1].role = "assistant";
  inputs[1].content = "First answer";
  inputs[1].message_key = "empty-session-assistant";
  inputs[1].include_in_context = 1;

  ok = strappy_db_append_message_sequence_to_session(context->catalog_path,
                                                     session_id,
                                                     "First prompt",
                                                     "First answer",
                                                     "harness-model",
                                                     200L,
                                                     inputs,
                                                     sizeof(inputs) / sizeof(inputs[0]),
                                                     &error);
  if (!ok) {
    fprintf(stderr,
            "Could not append to empty session: %s\n",
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
            "Could not load appended empty session: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_record_destroy(&session);
    return 0;
  }

  ok = (session.name != NULL) &&
       (strcmp(session.name, "Different Title") == 0) &&
       (session.prompt != NULL) &&
       (strcmp(session.prompt, "First prompt") == 0) &&
       (session.response != NULL) &&
       (strcmp(session.response, "First answer") == 0) &&
       (session.model != NULL) &&
       (strcmp(session.model, STRAPPY_CONFIG_DEFAULT_API_MODEL) == 0) &&
       (session.web_search_enabled == 0) &&
       (session.streaming_enabled == 1) &&
       (session.http_status == 200L);
  strappy_session_record_destroy(&session);
  if (!ok) {
    fprintf(stderr, "Appended empty session summary was not updated.\n");
    return 0;
  }

  strappy_session_message_record_list_init(&messages);
  ok = strappy_db_list_session_messages(context->catalog_path,
                                        session_id,
                                        &messages,
                                        &error);
  if (!ok) {
    fprintf(stderr,
            "Could not list appended empty session messages: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_message_record_list_destroy(&messages);
    return 0;
  }

  ok = (messages.count == 2U) &&
       (messages.records[0].content != NULL) &&
       (strcmp(messages.records[0].content, "First prompt") == 0) &&
       (messages.records[1].content != NULL) &&
       (strcmp(messages.records[1].content, "First answer") == 0);
  if (!ok) {
    fprintf(stderr, "Appended empty session messages were not stored.\n");
  }
  strappy_session_message_record_list_destroy(&messages);
  return ok;
}

static int harness_run_session_turn_storage_tests(const harness_context *context)
{
  strappy_session_message_input messages[5];
  strappy_session_message_record_list all_messages;
  strappy_session_message_record_list context_messages;
  const char *reasoning_streaming_state;
  const char *content_streaming_state;
  const char *processing_streaming_state;
  long long session_id;
  char *error;
  int ok;

  if (context == NULL) {
    return 0;
  }

  reasoning_streaming_state =
    "{\"streaming\":true,\"reasoning_render_when_empty\":true,"
    "\"reasoning_collapsed\":false,\"tool_column_collapsed\":true}";
  content_streaming_state =
    "{\"streaming\":true,\"reasoning_render_when_empty\":true,"
    "\"reasoning_collapsed\":false,\"tool_column_collapsed\":true,"
    "\"content_started\":true}";
  processing_streaming_state =
    "{\"streaming\":true,\"reasoning_render_when_empty\":true,"
    "\"reasoning_collapsed\":false,\"tool_column_collapsed\":true,"
    "\"content_started\":true,\"processing_status\":{\"active\":true,"
    "\"status_kind\":\"thinking\",\"started_ms\":1000}}";

  memset(messages, 0, sizeof(messages));

  messages[0].turn_key = "user-turn-test";
  messages[0].prompt_group_key = "prompt-group-test";
  messages[0].actor = "user";
  messages[0].context_policy = "full";
  messages[0].kind = "prompt";
  messages[0].api_role = "user";
  messages[0].render_role = "user";
  messages[0].role = "user";
  messages[0].content = "What is stored?";
  messages[0].message_key = "user-turn-test-prompt";
  messages[0].include_in_context = 1;

  messages[1].turn_key = "user-turn-test";
  messages[1].prompt_group_key = "prompt-group-test";
  messages[1].actor = "user";
  messages[1].context_policy = "full";
  messages[1].kind = "assistant";
  messages[1].api_role = "assistant";
  messages[1].render_role = "assistant";
  messages[1].role = "assistant";
  messages[1].content = "The answer.";
  messages[1].message_key = "user-turn-test-assistant";
  messages[1].include_in_context = 1;

  messages[2].turn_key = "harness-turn-test";
  messages[2].prompt_group_key = "prompt-group-test";
  messages[2].actor = "harness";
  messages[2].context_policy = "omit";
  messages[2].kind = "prompt";
  messages[2].api_role = "user";
  messages[2].render_role = "harness";
  messages[2].role = "harness";
  messages[2].content = "Learning Summary";
  messages[2].message_key = "harness-turn-test-prompt";
  messages[2].include_in_context = 0;

  messages[3].turn_key = "harness-turn-test";
  messages[3].prompt_group_key = "prompt-group-test";
  messages[3].actor = "harness";
  messages[3].context_policy = "omit";
  messages[3].kind = "tool_result";
  messages[3].api_role = "tool";
  messages[3].render_role = "tool";
  messages[3].role = "tool";
  messages[3].content = "{\"ok\":true}";
  messages[3].message_key = "harness-turn-test-tool-result";
  messages[3].target_message_key = "harness-turn-test-assistant";
  messages[3].tool_call_id = "call-1";
  messages[3].tool_name = STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER;
  messages[3].arguments_json = "{\"key\":\"value\"}";
  messages[3].result_json = "{\"ok\":true}";
  messages[3].include_in_context = 0;

  messages[4].turn_key = "harness-turn-test";
  messages[4].prompt_group_key = "prompt-group-test";
  messages[4].actor = "harness";
  messages[4].context_policy = "omit";
  messages[4].kind = "assistant";
  messages[4].api_role = "assistant";
  messages[4].render_role = "assistant";
  messages[4].role = "assistant";
  messages[4].content = "Learning Summary Complete";
  messages[4].message_key = "harness-turn-test-assistant";
  messages[4].metadata_json =
    "{\"usage\":{\"cost\":\"0.001\",\"prompt_tokens\":1,\"completion_tokens\":2}}";
  messages[4].include_in_context = 0;

  strappy_session_message_record_list_init(&all_messages);
  strappy_session_message_record_list_init(&context_messages);
  error = NULL;
  session_id = 0LL;
  ok = strappy_db_save_message_sequence_with_id(context->catalog_path,
                                                "What is stored?",
                                                "The answer.",
                                                "harness-model",
                                                200L,
                                                messages,
                                                sizeof(messages) / sizeof(messages[0]),
                                                &session_id,
                                                &error);
  if (!ok) {
    fprintf(stderr,
            "Could not save session turn test: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  ok = strappy_db_list_session_messages(context->catalog_path,
                                        session_id,
                                        &all_messages,
                                        &error);
  if (!ok) {
    fprintf(stderr,
            "Could not list session turn test messages: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  ok = (all_messages.count == 5U) &&
       (all_messages.records[0].turn_id > 0LL) &&
       (all_messages.records[2].turn_id > 0LL) &&
       (strcmp(all_messages.records[0].prompt_group_key,
               "prompt-group-test") == 0) &&
       (strcmp(all_messages.records[2].prompt_group_key,
               "prompt-group-test") == 0) &&
       (strcmp(all_messages.records[2].actor, "harness") == 0) &&
       (strcmp(all_messages.records[2].role, "harness") == 0) &&
       (strcmp(all_messages.records[3].kind, "tool_result") == 0) &&
       (strcmp(all_messages.records[3].target_message_key,
               "harness-turn-test-assistant") == 0) &&
       (all_messages.records[3].include_in_context == 0) &&
       (strcmp(all_messages.records[4].metadata_json,
               "{\"usage\":{\"cost\":\"0.001\",\"prompt_tokens\":1,\"completion_tokens\":2}}") == 0);
  if (!ok) {
    fprintf(stderr, "Stored session turn rows did not match expected shape.\n");
    strappy_session_message_record_list_destroy(&all_messages);
    return 0;
  }

  ok = strappy_db_list_session_context_messages(context->catalog_path,
                                                session_id,
                                                &context_messages,
                                                &error);
  if (!ok) {
    fprintf(stderr,
            "Could not list session context messages: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_message_record_list_destroy(&all_messages);
    return 0;
  }

  ok = (context_messages.count == 2U) &&
       (strcmp(context_messages.records[0].api_role, "user") == 0) &&
       (strcmp(context_messages.records[1].api_role, "assistant") == 0) &&
       (strcmp(context_messages.records[1].content, "The answer.") == 0);
  if (!ok) {
    fprintf(stderr, "Session context replay rows did not match expected shape.\n");
    strappy_session_message_record_list_destroy(&context_messages);
    strappy_session_message_record_list_destroy(&all_messages);
    return 0;
  }

  strappy_session_message_record_list_destroy(&context_messages);
  strappy_session_message_record_list_destroy(&all_messages);

  memset(messages, 0, sizeof(messages));
  messages[0].turn_key = "stream-turn-test";
  messages[0].prompt_group_key = "prompt-group-test";
  messages[0].actor = "user";
  messages[0].context_policy = "full";
  messages[0].kind = "assistant";
  messages[0].api_role = "assistant";
  messages[0].render_role = "assistant";
  messages[0].role = "assistant";
  messages[0].content = "";
  messages[0].message_key = "stream-turn-test-assistant";
  messages[0].render_state_json = reasoning_streaming_state;
  messages[0].include_in_context = 1;

  if (!strappy_db_append_session_message_content(context->catalog_path,
                                                 session_id,
                                                 &messages[0],
                                                 "",
                                                 "thinking",
                                                 &error)) {
    fprintf(stderr,
            "Could not append streamed reasoning content: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  messages[0].render_state_json = content_streaming_state;
  if (!strappy_db_append_session_message_content(context->catalog_path,
                                                 session_id,
                                                 &messages[0],
                                                 "Hel",
                                                 NULL,
                                                 &error) ||
      !strappy_db_append_session_message_content(context->catalog_path,
                                                 session_id,
                                                 &messages[0],
                                                 "lo",
                                                 NULL,
                                                 &error)) {
    fprintf(stderr,
            "Could not append streamed message content: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  messages[0].render_state_json = reasoning_streaming_state;
  if (!strappy_db_append_session_message_content(context->catalog_path,
                                                 session_id,
                                                 &messages[0],
                                                 "",
                                                 " late",
                                                 &error)) {
    fprintf(stderr,
            "Could not append streamed message content: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  strappy_session_message_record_list_init(&all_messages);
  ok = strappy_db_list_session_messages(context->catalog_path,
                                        session_id,
                                        &all_messages,
                                        &error);
  if (!ok) {
    fprintf(stderr,
            "Could not list streamed message rows: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_message_record_list_destroy(&all_messages);
    return 0;
  }

  ok = (all_messages.count == 6U) &&
       (strcmp(all_messages.records[5].message_key,
               "stream-turn-test-assistant") == 0) &&
       (strcmp(all_messages.records[5].content, "Hello") == 0) &&
       (strcmp(all_messages.records[5].reasoning, "thinking late") == 0) &&
       (all_messages.records[5].render_state_json != NULL) &&
       (strcmp(all_messages.records[5].render_state_json,
               content_streaming_state) == 0);
  strappy_session_message_record_list_destroy(&all_messages);
  if (!ok) {
    fprintf(stderr, "Streamed message append did not persist expected content.\n");
    return 0;
  }

  if (!strappy_db_update_session_message_render_state(
        context->catalog_path,
        session_id,
        "stream-turn-test-assistant",
        processing_streaming_state,
        &error)) {
    fprintf(stderr,
            "Could not update streamed message render state: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  strappy_session_message_record_list_init(&all_messages);
  ok = strappy_db_list_session_messages(context->catalog_path,
                                        session_id,
                                        &all_messages,
                                        &error);
  if (!ok) {
    fprintf(stderr,
            "Could not list render-state-only update rows: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_message_record_list_destroy(&all_messages);
    return 0;
  }

  ok = (all_messages.count == 6U) &&
       (strcmp(all_messages.records[5].message_key,
               "stream-turn-test-assistant") == 0) &&
       (strcmp(all_messages.records[5].content, "Hello") == 0) &&
       (strcmp(all_messages.records[5].reasoning, "thinking late") == 0) &&
       (all_messages.records[5].render_state_json != NULL) &&
       (strcmp(all_messages.records[5].render_state_json,
               processing_streaming_state) == 0);
  strappy_session_message_record_list_destroy(&all_messages);
  if (!ok) {
    fprintf(stderr, "Render-state-only update changed streamed content.\n");
    return 0;
  }

  messages[0].render_state_json = reasoning_streaming_state;
  if (!strappy_db_move_session_message_content_to_reasoning(context->catalog_path,
                                                            session_id,
                                                            &messages[0],
                                                            &error)) {
    fprintf(stderr,
            "Could not move streamed message content to reasoning: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  strappy_session_message_record_list_init(&all_messages);
  ok = strappy_db_list_session_messages(context->catalog_path,
                                        session_id,
                                        &all_messages,
                                        &error);
  if (!ok) {
    fprintf(stderr,
            "Could not list moved streamed message rows: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_message_record_list_destroy(&all_messages);
    return 0;
  }

  ok = (all_messages.count == 6U) &&
       (strcmp(all_messages.records[5].message_key,
               "stream-turn-test-assistant") == 0) &&
       (strcmp(all_messages.records[5].content, "") == 0) &&
       (strcmp(all_messages.records[5].reasoning,
               "thinking late\nHello") == 0) &&
       (all_messages.records[5].render_state_json != NULL) &&
       (strcmp(all_messages.records[5].render_state_json,
               reasoning_streaming_state) == 0);
  strappy_session_message_record_list_destroy(&all_messages);
  if (!ok) {
    fprintf(stderr, "Streamed message content was not moved into reasoning.\n");
    return 0;
  }

  messages[0].content = "Hello final";
  messages[0].reasoning = "final reasoning";
  messages[0].render_state_json = NULL;
  if (!strappy_db_append_message_sequence_to_session(context->catalog_path,
                                                     session_id,
                                                     "Stream prompt",
                                                     "Hello final",
                                                     "harness-model",
                                                     200L,
                                                     &messages[0],
                                                     1U,
                                                     &error)) {
    fprintf(stderr,
            "Could not upsert final streamed message row: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  strappy_session_message_record_list_init(&all_messages);
  ok = strappy_db_list_session_messages(context->catalog_path,
                                        session_id,
                                        &all_messages,
                                        &error);
  if (!ok) {
    fprintf(stderr,
            "Could not list final streamed message rows: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_message_record_list_destroy(&all_messages);
    return 0;
  }

  ok = (all_messages.count == 6U) &&
       (strcmp(all_messages.records[5].message_key,
               "stream-turn-test-assistant") == 0) &&
       (strcmp(all_messages.records[5].content, "Hello final") == 0) &&
       (strcmp(all_messages.records[5].reasoning, "final reasoning") == 0) &&
       (all_messages.records[5].render_state_json == NULL);
  strappy_session_message_record_list_destroy(&all_messages);
  if (!ok) {
    fprintf(stderr, "Final streamed message upsert duplicated or missed the row.\n");
    return 0;
  }

  messages[0].content = "Hello final without reasoning";
  messages[0].reasoning = NULL;
  if (!strappy_db_append_message_sequence_to_session(context->catalog_path,
                                                     session_id,
                                                     "Stream prompt",
                                                     "Hello final without reasoning",
                                                     "harness-model",
                                                     200L,
                                                     &messages[0],
                                                     1U,
                                                     &error)) {
    fprintf(stderr,
            "Could not upsert final streamed message without reasoning: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  strappy_session_message_record_list_init(&all_messages);
  ok = strappy_db_list_session_messages(context->catalog_path,
                                        session_id,
                                        &all_messages,
                                        &error);
  if (!ok) {
    fprintf(stderr,
            "Could not list preserved reasoning message rows: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_message_record_list_destroy(&all_messages);
    return 0;
  }

  ok = (all_messages.count == 6U) &&
       (strcmp(all_messages.records[5].message_key,
               "stream-turn-test-assistant") == 0) &&
       (strcmp(all_messages.records[5].content,
               "Hello final without reasoning") == 0) &&
       (strcmp(all_messages.records[5].reasoning, "final reasoning") == 0);
  strappy_session_message_record_list_destroy(&all_messages);
  if (!ok) {
    fprintf(stderr, "Final streamed message upsert erased existing reasoning.\n");
    return 0;
  }

  messages[0].content = "Hello final with empty reasoning";
  messages[0].reasoning = "";
  if (!strappy_db_append_message_sequence_to_session(context->catalog_path,
                                                     session_id,
                                                     "Stream prompt",
                                                     "Hello final with empty reasoning",
                                                     "harness-model",
                                                     200L,
                                                     &messages[0],
                                                     1U,
                                                     &error)) {
    fprintf(stderr,
            "Could not upsert final streamed message with empty reasoning: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  strappy_session_message_record_list_init(&all_messages);
  ok = strappy_db_list_session_messages(context->catalog_path,
                                        session_id,
                                        &all_messages,
                                        &error);
  if (!ok) {
    fprintf(stderr,
            "Could not list empty-reasoning message rows: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_message_record_list_destroy(&all_messages);
    return 0;
  }

  ok = (all_messages.count == 6U) &&
       (strcmp(all_messages.records[5].message_key,
               "stream-turn-test-assistant") == 0) &&
       (strcmp(all_messages.records[5].content,
               "Hello final with empty reasoning") == 0) &&
       (strcmp(all_messages.records[5].reasoning, "final reasoning") == 0);
  strappy_session_message_record_list_destroy(&all_messages);
  if (!ok) {
    fprintf(stderr, "Empty final reasoning erased existing reasoning.\n");
    return 0;
  }

  return ok;
}

static int harness_run_openrouter_model_catalog_tests(
  const harness_context *context)
{
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
  strappy_openrouter_model_record_list list;
  char *default_model;
  char *selected_model;
  char *session_model;
  char *error;
  long long session_id;
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

  strappy_openrouter_model_record_list_init(&list);
  if (!strappy_db_list_openrouter_models(context->catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list OpenRouter models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_openrouter_model_record_list_destroy(&list);
    return 0;
  }

  found_builtin_default = 0;
  found_gemma = 0;
  found_openai = 0;
  for (index = 0U; index < list.count; index++) {
    strappy_openrouter_model_record *record;

    record = &list.records[index];
    if ((record->model_id != NULL) &&
        (strcmp(record->model_id, STRAPPY_CONFIG_DEFAULT_API_MODEL) == 0)) {
      found_builtin_default = record->selected && record->allowed;
    }
    if ((record->model_id != NULL) &&
        (strcmp(record->model_id, "google/gemma-4-31b-it") == 0)) {
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
        (record->reasoning_json != NULL) &&
        (strstr(record->reasoning_json, "medium") != NULL) &&
        (record->benchmarks_json != NULL) &&
        (strstr(record->benchmarks_json, "mmlu") != NULL) &&
        (record->default_parameters_json != NULL) &&
        (strstr(record->default_parameters_json, "temperature") != NULL) &&
        (record->raw_json != NULL) &&
        (strstr(record->raw_json, "Harness model") != NULL);
    }
    if ((record->model_id != NULL) &&
        (strcmp(record->model_id, "openai/gpt-4.1-mini") == 0)) {
      found_openai = 1;
    }
  }
  ok = (list.count == 3U) && found_builtin_default && found_gemma && found_openai;
  strappy_openrouter_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "OpenRouter model rows did not match expected values.\n");
    return 0;
  }

  default_model = NULL;
  error = NULL;
  if (!strappy_db_get_default_openrouter_model(context->catalog_path,
                                               &default_model,
                                               &error)) {
    fprintf(stderr,
            "Could not get default OpenRouter model: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  ok = (default_model != NULL) &&
       (strcmp(default_model, STRAPPY_CONFIG_DEFAULT_API_MODEL) == 0);
  strappy_free_string(default_model);
  if (!ok) {
    fprintf(stderr, "Built-in OpenRouter default model did not persist.\n");
    return 0;
  }

  error = NULL;
  strappy_openrouter_model_record_list_init(&list);
  if (!strappy_db_list_allowed_openrouter_models(context->catalog_path,
                                                 &list,
                                                 &error)) {
    fprintf(stderr,
            "Could not list allowed OpenRouter models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_openrouter_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 1U) &&
       (list.records[0].model_id != NULL) &&
       (strcmp(list.records[0].model_id, STRAPPY_CONFIG_DEFAULT_API_MODEL) == 0) &&
       list.records[0].allowed;
  strappy_openrouter_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "Built-in OpenRouter default was not the only allowed model.\n");
    return 0;
  }

  error = NULL;
  strappy_openrouter_model_record_list_init(&list);
  if (!strappy_db_list_openrouter_models_matching(context->catalog_path,
                                                  "alloy",
                                                  &list,
                                                  &error)) {
    fprintf(stderr,
            "Could not filter OpenRouter models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_openrouter_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 1U) &&
       (list.records[0].model_id != NULL) &&
       (strcmp(list.records[0].model_id, "google/gemma-4-31b-it") == 0);
  strappy_openrouter_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "OpenRouter model filtered list did not match expected values.\n");
    return 0;
  }

  error = NULL;
  strappy_openrouter_model_record_list_init(&list);
  if (!strappy_db_list_openrouter_models_matching(context->catalog_path,
                                                  "GPT 4.1 Mini",
                                                  &list,
                                                  &error)) {
    fprintf(stderr,
            "Could not filter OpenRouter models by name: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_openrouter_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 1U) &&
       (list.records[0].model_id != NULL) &&
       (strcmp(list.records[0].model_id, "openai/gpt-4.1-mini") == 0);
  strappy_openrouter_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "OpenRouter model name filter did not match expected values.\n");
    return 0;
  }

  error = NULL;
  strappy_openrouter_model_record_list_init(&list);
  if (!strappy_db_list_openrouter_models_matching(context->catalog_path,
                                                  "openai/gpt-4.1-mini",
                                                  &list,
                                                  &error)) {
    fprintf(stderr,
            "Could not filter OpenRouter models by id: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_openrouter_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 1U) &&
       (list.records[0].model_id != NULL) &&
       (strcmp(list.records[0].model_id, "openai/gpt-4.1-mini") == 0);
  strappy_openrouter_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "OpenRouter model id filter did not match expected values.\n");
    return 0;
  }

  error = NULL;
  strappy_openrouter_model_record_list_init(&list);
  if (!strappy_db_list_openrouter_models_matching(context->catalog_path,
                                                  "not-a-model",
                                                  &list,
                                                  &error)) {
    fprintf(stderr,
            "Could not filter OpenRouter models with no matches: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_openrouter_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 0U);
  strappy_openrouter_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "OpenRouter model filtered list should have been empty.\n");
    return 0;
  }

  if (!harness_expect_catalog_integer(
        context->catalog_path,
        "SELECT COUNT(*) FROM openrouter_model_input_modalities "
        "WHERE model_id = 'openai/gpt-4.1-mini' AND modality = 'image';",
        1LL,
        "OpenRouter input modality count") ||
      !harness_expect_catalog_integer(
        context->catalog_path,
        "SELECT COUNT(*) FROM openrouter_model_supported_parameters "
        "WHERE model_id = 'google/gemma-4-31b-it' AND parameter = 'tools';",
        1LL,
        "OpenRouter supported parameter count") ||
      !harness_expect_catalog_integer(
        context->catalog_path,
        "SELECT COUNT(*) FROM openrouter_model_supported_voices "
        "WHERE model_id = 'google/gemma-4-31b-it' AND voice = 'alloy';",
        1LL,
        "OpenRouter supported voice count")) {
    return 0;
  }

  error = NULL;
  if (!strappy_db_set_default_openrouter_model(context->catalog_path,
                                               "openai/gpt-4.1-mini",
                                               &error)) {
    fprintf(stderr,
            "Could not set default OpenRouter model: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  default_model = NULL;
  if (!strappy_db_get_default_openrouter_model(context->catalog_path,
                                               &default_model,
                                               &error)) {
    fprintf(stderr,
            "Could not get default OpenRouter model after update: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  ok = (default_model != NULL) &&
       (strcmp(default_model, "openai/gpt-4.1-mini") == 0);
  strappy_free_string(default_model);
  if (!ok) {
    fprintf(stderr, "Default OpenRouter model did not persist.\n");
    return 0;
  }

  selected_model = NULL;
  if (!strappy_db_get_selected_openrouter_model(context->catalog_path,
                                                &selected_model,
                                                &error)) {
    fprintf(stderr,
            "Could not get selected OpenRouter model: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  ok = (selected_model != NULL) &&
       (strcmp(selected_model, "openai/gpt-4.1-mini") == 0);
  strappy_free_string(selected_model);
  if (!ok) {
    fprintf(stderr, "Selected OpenRouter compatibility wrapper did not persist.\n");
    return 0;
  }

  error = NULL;
  if (strappy_db_set_openrouter_model_allowed(context->catalog_path,
                                              "openai/gpt-4.1-mini",
                                              0,
                                              &error)) {
    fprintf(stderr, "Default OpenRouter model should not be removable.\n");
    return 0;
  }
  strappy_free_string(error);
  error = NULL;

  strappy_openrouter_model_record_list_init(&list);
  if (!strappy_db_list_openrouter_models(context->catalog_path, &list, &error)) {
    fprintf(stderr,
            "Could not list selected OpenRouter models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_openrouter_model_record_list_destroy(&list);
    return 0;
  }

  ok = (list.count == 3U) &&
       (list.records[0].model_id != NULL) &&
       (strcmp(list.records[0].model_id, "openai/gpt-4.1-mini") == 0) &&
       list.records[0].selected &&
       list.records[0].allowed;
  strappy_openrouter_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "Default OpenRouter model was not listed first.\n");
    return 0;
  }

  error = NULL;
  strappy_openrouter_model_record_list_init(&list);
  if (!strappy_db_list_allowed_openrouter_models(context->catalog_path,
                                                 &list,
                                                 &error)) {
    fprintf(stderr,
            "Could not list updated allowed OpenRouter models: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_openrouter_model_record_list_destroy(&list);
    return 0;
  }
  ok = (list.count == 1U) &&
       (list.records[0].model_id != NULL) &&
       (strcmp(list.records[0].model_id, "openai/gpt-4.1-mini") == 0);
  strappy_openrouter_model_record_list_destroy(&list);
  if (!ok) {
    fprintf(stderr, "Default OpenRouter model was not the only allowed model.\n");
    return 0;
  }

  error = NULL;
  if (!strappy_db_set_openrouter_model_allowed(context->catalog_path,
                                               "google/gemma-4-31b-it",
                                               1,
                                               &error)) {
    fprintf(stderr,
            "Could not allow OpenRouter model: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }

  error = NULL;
  strappy_openrouter_model_record_list_init(&list);
  if (!strappy_db_list_allowed_openrouter_models(context->catalog_path,
                                                 &list,
                                                 &error)) {
    fprintf(stderr,
            "Could not list allowed OpenRouter models after allow: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_openrouter_model_record_list_destroy(&list);
    return 0;
  }
  found_gemma = 0;
  found_openai = 0;
  for (index = 0U; index < list.count; index++) {
    if ((list.records[index].model_id != NULL) &&
        (strcmp(list.records[index].model_id, "google/gemma-4-31b-it") == 0)) {
      found_gemma = 1;
    }
    if ((list.records[index].model_id != NULL) &&
        (strcmp(list.records[index].model_id, "openai/gpt-4.1-mini") == 0)) {
      found_openai = 1;
    }
  }
  ok = (list.count == 2U) && found_gemma && found_openai;
  strappy_openrouter_model_record_list_destroy(&list);
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
       (strcmp(session_model, "openai/gpt-4.1-mini") == 0);
  strappy_free_string(session_model);
  if (!ok) {
    fprintf(stderr, "New session did not copy default OpenRouter model.\n");
    return 0;
  }

  error = NULL;
  if (!strappy_db_update_session_model(context->catalog_path,
                                       session_id,
                                       "google/gemma-4-31b-it",
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
       (strcmp(session_model, "google/gemma-4-31b-it") == 0);
  strappy_free_string(session_model);
  if (!ok) {
    fprintf(stderr, "Session OpenRouter model did not persist.\n");
    return 0;
  }

  error = NULL;
  if (!strappy_db_append_exchange_to_session(context->catalog_path,
                                             session_id,
                                             "summary prompt",
                                             "summary response",
                                             "openai/gpt-4.1-mini",
                                             200L,
                                             NULL,
                                             NULL,
                                             NULL,
                                             &error)) {
    fprintf(stderr,
            "Could not append model selection exchange: %s\n",
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
            "Could not read session model after exchange append: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  ok = (session_model != NULL) &&
       (strcmp(session_model, "google/gemma-4-31b-it") == 0);
  strappy_free_string(session_model);
  if (!ok) {
    fprintf(stderr, "Session exchange append reset OpenRouter model.\n");
    return 0;
  }

  error = NULL;
  if (!strappy_db_append_message_sequence_to_session(context->catalog_path,
                                                     session_id,
                                                     "sequence prompt",
                                                     "sequence response",
                                                     "openai/gpt-4.1-mini",
                                                     200L,
                                                     NULL,
                                                     0U,
                                                     &error)) {
    fprintf(stderr,
            "Could not append model selection sequence: %s\n",
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
            "Could not read session model after sequence append: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  ok = (session_model != NULL) &&
       (strcmp(session_model, "google/gemma-4-31b-it") == 0);
  strappy_free_string(session_model);
  if (!ok) {
    fprintf(stderr, "Session sequence append reset OpenRouter model.\n");
    return 0;
  }

  error = NULL;
  if (!strappy_db_set_openrouter_model_allowed(context->catalog_path,
                                               "google/gemma-4-31b-it",
                                               0,
                                               &error)) {
    fprintf(stderr,
            "Could not disallow non-default OpenRouter model: %s\n",
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
            "Could not read stale session OpenRouter model fallback: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    return 0;
  }
  ok = (session_model != NULL) &&
       (strcmp(session_model, "openai/gpt-4.1-mini") == 0);
  strappy_free_string(session_model);
  if (!ok) {
    fprintf(stderr, "Stale session OpenRouter model did not fall back.\n");
    return 0;
  }

  return 1;
}

int main(void)
{
  harness_context context;
  int ok;

  harness_context_init(&context);
  ok = harness_run_audit_resource_tests() &&
       harness_run_tool_registry_tests() &&
       harness_run_helper_datetime_tests() &&
       harness_run_helper_fontawesome_tests() &&
       harness_make_temp_dir(&context) &&
       harness_run_fresh_catalog_schema_tests(&context) &&
       harness_run_discovered_database_replacement_tests(&context) &&
       harness_run_file_scanner_batch_catalog_tests(&context) &&
       harness_run_file_scanner_hidden_tests(&context) &&
       harness_run_empty_database_list_info_tests(&context) &&
       harness_create_user_database(context.database_path) &&
       harness_register_database(&context) &&
       harness_run_database_list_info_tests(&context) &&
       harness_run_database_query_tests(&context) &&
       harness_run_missing_database_query_guidance_test(&context) &&
       harness_run_readonly_wal_database_query_test(&context) &&
       harness_run_wide_schema_database_query_test(&context) &&
       harness_run_helper_info_tests(&context) &&
       harness_run_empty_session_storage_tests(&context) &&
       harness_run_session_turn_storage_tests(&context) &&
       harness_run_openrouter_model_catalog_tests(&context) &&
       harness_run_sms_guidance_tests(&context) &&
       harness_run_mail_guidance_tests(&context);

  harness_context_destroy(&context);

  if (!ok) {
    fprintf(stderr, "database_query_harness failed.\n");
    return 1;
  }

  printf("database_query_harness passed.\n");
  return 0;
}
