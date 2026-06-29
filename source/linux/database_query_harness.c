#define _POSIX_C_SOURCE 200809L

#include "strappy_core.h"
#include "strappy_config.h"
#include "strappy_db.h"
#include "strappy_tools.h"
#include "cJSON.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HARNESS_RESOURCE_DIR "../shared/Resources"
#define HARNESS_DATABASE_GUIDANCE_RESOURCE "GuidanceDatabase.json"

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
    unlink(context->catalog_path);
  }
  if (context->database_path[0] != '\0') {
    unlink(context->database_path);
  }
  if (context->sms_path[0] != '\0') {
    unlink(context->sms_path);
  }
  if (context->mail_envelope_path[0] != '\0') {
    unlink(context->mail_envelope_path);
  }
  if (context->mail_protected_path[0] != '\0') {
    unlink(context->mail_protected_path);
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

static int harness_run_legacy_default_model_migration_test(
  const harness_context *context)
{
  static const char *legacy_sql =
    "INSERT INTO app_settings (key, value) VALUES "
    "('default_openrouter_model_id', 'gemma-4-31b-it');"
    "INSERT INTO app_settings (key, value) VALUES "
    "('selected_openrouter_model_id', 'gemma-4-31b-it');"
    "INSERT INTO sessions (prompt, response, model, http_status) "
    "VALUES ('', '', 'gemma-4-31b-it', 0);"
    "INSERT INTO openrouter_models (id, name, description) VALUES "
    "('gemma-4-31b-it', 'gemma-4-31b-it', 'Built-in default model.');"
    "INSERT INTO openrouter_model_settings (model_id, allowed) VALUES "
    "('gemma-4-31b-it', 1);";
  sqlite3 *db;
  char *default_model;
  char *error;
  char database_path[1200];
  int rc;
  int ok;

  if (context == NULL) {
    return 0;
  }
  if (!harness_join_path(database_path,
                         sizeof(database_path),
                         context->temp_dir,
                         "legacy-default-model.sqlite")) {
    fprintf(stderr, "Could not build legacy model migration fixture path.\n");
    return 0;
  }
  unlink(database_path);

  error = NULL;
  if (!strappy_db_initialize(database_path, &error)) {
    fprintf(stderr,
            "Could not initialize catalog for legacy model migration: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    unlink(database_path);
    return 0;
  }

  db = NULL;
  rc = sqlite3_open_v2(database_path, &db, SQLITE_OPEN_READWRITE, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not open catalog for legacy model migration: %s\n",
            (db != NULL) ? sqlite3_errmsg(db) : "unknown");
    if (db != NULL) {
      sqlite3_close(db);
    }
    unlink(database_path);
    return 0;
  }
  ok = harness_exec_sql(db, legacy_sql);
  sqlite3_close(db);
  if (!ok) {
    unlink(database_path);
    return 0;
  }

  default_model = NULL;
  error = NULL;
  if (!strappy_db_get_default_openrouter_model(database_path,
                                               &default_model,
                                               &error)) {
    fprintf(stderr,
            "Could not migrate legacy default OpenRouter model: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    unlink(database_path);
    return 0;
  }

  ok = (default_model != NULL) &&
       (strcmp(default_model, STRAPPY_CONFIG_DEFAULT_API_MODEL) == 0);
  strappy_free_string(default_model);
  if (!ok) {
    fprintf(stderr, "Legacy default OpenRouter model was not canonicalized.\n");
    unlink(database_path);
    return 0;
  }

  ok =
    harness_expect_catalog_integer(
      database_path,
      "SELECT COUNT(*) FROM openrouter_models "
      "WHERE id = 'gemma-4-31b-it';",
      0LL,
      "legacy default OpenRouter model row") &&
    harness_expect_catalog_integer(
      database_path,
      "SELECT COUNT(*) FROM openrouter_model_settings "
      "WHERE model_id = 'gemma-4-31b-it';",
      0LL,
      "legacy default OpenRouter model setting") &&
    harness_expect_catalog_integer(
      database_path,
      "SELECT COUNT(*) FROM sessions "
      "WHERE model = '" STRAPPY_CONFIG_DEFAULT_API_MODEL "';",
      1LL,
      "canonicalized legacy session model");
  unlink(database_path);
  return ok;
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
         harness_expect_catalog_sql_ok(
           context->catalog_path,
           "SELECT id, name, prompt, response, model, http_status, "
           "streaming_enabled, created_at FROM sessions LIMIT 0;",
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
           "user_decision, scan_root, first_seen_at, last_seen_at, "
           "last_scanned_at FROM discovered_databases LIMIT 0;",
           "discovered_databases columns") &&
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

static cJSON *harness_database_guidance_rule(cJSON *root,
                                             const char *guidance_id)
{
  cJSON *rules;
  cJSON *rule;

  if ((root == NULL) || (guidance_id == NULL) || (guidance_id[0] == '\0')) {
    return NULL;
  }

  rules = cJSON_GetObjectItem(root, "database_descriptions");
  if (!cJSON_IsArray(rules)) {
    return NULL;
  }

  for (rule = rules->child; rule != NULL; rule = rule->next) {
    cJSON *id;

    id = cJSON_GetObjectItem(rule, "id");
    if (cJSON_IsString(id) &&
        (id->valuestring != NULL) &&
        (strcmp(id->valuestring, guidance_id) == 0)) {
      return rule;
    }
  }

  return NULL;
}

static char *harness_database_guidance_string(const char *guidance_id,
                                              const char *key)
{
  cJSON *root;
  cJSON *rule;
  cJSON *value;
  char *copy;

  root = harness_read_json_resource(HARNESS_DATABASE_GUIDANCE_RESOURCE);
  if (root == NULL) {
    return NULL;
  }

  rule = harness_database_guidance_rule(root, guidance_id);
  value = (rule != NULL) ? cJSON_GetObjectItem(rule, key) : NULL;
  if (!cJSON_IsString(value) || (value->valuestring == NULL)) {
    fprintf(stderr,
            "Database guidance %s is missing %s.\n",
            (guidance_id != NULL) ? guidance_id : "(null)",
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

static int harness_expect_error_contains(const char *catalog_path,
                                         const char *tool_name,
                                         const char *arguments_json,
                                         const char *needle)
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

static int harness_expect_context_description_from_guidance(
  const char *catalog_path,
  const char *arguments_json,
  const char *guidance_id)
{
  char *expected;
  cJSON *root;
  cJSON *description;
  int ok;

  expected = harness_database_guidance_string(guidance_id, "description");
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
            "Database context description did not match %s guidance.\n",
            guidance_id);
  }

  cJSON_Delete(root);
  free(expected);
  return ok;
}

static int harness_expect_list_description_from_guidance(
  const char *catalog_path,
  const char *guidance_id)
{
  char *expected;
  cJSON *root;
  cJSON *databases;
  cJSON *database;
  int ok;

  expected = harness_database_guidance_string(guidance_id, "description_short");
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
            "Database list did not contain %s description_short guidance.\n",
            guidance_id);
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

static int harness_run_tool_registry_tests(void)
{
  char *error;
  char *tools_json;
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

  ok = ((strstr(tools_json, STRAPPY_TOOL_DATABASE_LIST_INFO) != NULL) &&
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
    fprintf(stderr, "Tool registry did not match expected tools: %s\n", tools_json);
  }

  free(error);
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
        "{\"query\":\"warning\",\"limit\":5}",
        "\"shortcode\":\"[fa:triangle-exclamation]\"",
        "\"label\":\"Triangle Exclamation\"",
        "codepoint")) {
    return 0;
  }

  if (!harness_expect_output_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH,
        "{\"query\":\"github\",\"style\":\"brands\",\"limit\":3}",
        "\"shortcode\":\"[fa:brands:github]\"",
        "\"style\":\"brands\"")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_SEARCH,
        "{\"style\":\"duotone\"}",
        "style must be solid, regular, or brands")) {
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
        (strstr(output, "manage_href") != NULL) &&
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

static int harness_run_helper_info_tests(const harness_context *context)
{
  char arguments[4096];
  int written;

  if ((context == NULL) || (context->database_id == NULL)) {
    return 0;
  }

  if (!harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                     NULL,
                                     "requires at least one argument")) {
    return 0;
  }

  if (!harness_expect_error_contains(context->catalog_path,
                                     STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                     "{}",
                                     "requires at least one argument")) {
    return 0;
  }

  if (!harness_expect_output_contains(
        context->catalog_path,
        STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
        "{\"kind\":\"identity\",\"subject\":\"user\","
        "\"predicate\":\"first_name\",\"value\":\"Jeff\","
        "\"confidence\":0.95,\"source\":\"user_explicit\"}",
        "\"ok\":true",
        "\"id\":1")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_MEMORY_USER_FACT_READ,
                                      "{\"query\":\"Jeff\"}",
                                      "\"first_name\"",
                                      "\"Jeff\"")) {
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

  written = snprintf(
    arguments,
    sizeof(arguments),
    "{\"database_id\":\"%s\",\"kind\":\"join_hint\","
    "\"title\":\"messages identifiers join\","
    "\"content\":\"The messages table can be checked against identifiers "
    "when validating exact integer serialization.\","
    "\"evidence\":\"harness fixture query\","
    "\"confidence\":0.9}",
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
                                      "\"join_hint\"",
                                      "\"messages identifiers join\"")) {
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

  if (!harness_expect_context_description_from_guidance(
        context->catalog_path,
        arguments,
        "apple_mail_envelope_index")) {
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

  if (!harness_expect_context_description_from_guidance(
        context->catalog_path,
        arguments,
        "apple_mail_protected_index")) {
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

  if (!harness_expect_context_description_from_guidance(context->catalog_path,
                                                        arguments,
                                                        "apple_messages_sms")) {
    return 0;
  }

  if (!harness_expect_list_description_from_guidance(context->catalog_path,
                                                     "apple_messages_sms")) {
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
  ok = (strstr(output, "\"updated\":false") != NULL);
  free(output);
  if (!ok) {
    fprintf(stderr, "Second session name write unexpectedly updated.\n");
    return 0;
  }

  strappy_session_record_init(&session);
  ok = strappy_db_load_session(context->catalog_path,
                               session_id,
                               &session,
                               &error);
  if (!ok) {
    fprintf(stderr,
            "Could not load unchanged named session: %s\n",
            (error != NULL) ? error : "unknown");
    strappy_free_string(error);
    strappy_session_record_destroy(&session);
    return 0;
  }
  ok = (session.name != NULL) &&
       (strcmp(session.name, "Find Receipts") == 0);
  strappy_session_record_destroy(&session);
  if (!ok) {
    fprintf(stderr, "Session name was unexpectedly changed.\n");
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
       (strcmp(session.name, "Find Receipts") == 0) &&
       (session.prompt != NULL) &&
       (strcmp(session.prompt, "First prompt") == 0) &&
       (session.response != NULL) &&
       (strcmp(session.response, "First answer") == 0) &&
       (session.model != NULL) &&
       (strcmp(session.model, STRAPPY_CONFIG_DEFAULT_API_MODEL) == 0) &&
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
  ok = (list.count == 2U) && found_builtin_default && found_gemma && found_openai;
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

  ok = (list.count == 2U) &&
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
  ok = harness_run_tool_registry_tests() &&
       harness_run_helper_datetime_tests() &&
       harness_run_helper_fontawesome_tests() &&
       harness_make_temp_dir(&context) &&
       harness_run_fresh_catalog_schema_tests(&context) &&
       harness_run_empty_database_list_info_tests(&context) &&
       harness_create_user_database(context.database_path) &&
       harness_register_database(&context) &&
       harness_run_database_list_info_tests(&context) &&
       harness_run_database_query_tests(&context) &&
       harness_run_helper_info_tests(&context) &&
       harness_run_empty_session_storage_tests(&context) &&
       harness_run_session_turn_storage_tests(&context) &&
       harness_run_legacy_default_model_migration_test(&context) &&
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
