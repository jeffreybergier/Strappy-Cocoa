#define _POSIX_C_SOURCE 200809L

#include "strappy_core.h"
#include "strappy_db.h"
#include "strappy_tools.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HARNESS_RESOURCE_DIR "../shared/Resources"

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
        (strstr(tools_json, STRAPPY_TOOL_HELPER_CONVERT_DATES) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_HELPER_USER_INFO_READ) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_HELPER_USER_INFO_FORGET) != NULL) &&
        (strstr(tools_json, STRAPPY_TOOL_DATABASE_CONTEXT_READ) != NULL) &&
        (strstr(tools_json,
                STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER) != NULL) &&
        (strstr(tools_json,
                STRAPPY_TOOL_HELPER_DATABASE_INFO_FORGET) != NULL) &&
        strappy_tools_is_helper(STRAPPY_TOOL_HELPER_CONVERT_DATES) &&
        strappy_tools_is_helper(STRAPPY_TOOL_HELPER_USER_INFO_READ) &&
        strappy_tools_is_helper(STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER) &&
        strappy_tools_is_helper(STRAPPY_TOOL_HELPER_USER_INFO_FORGET) &&
        strappy_tools_is_helper(STRAPPY_TOOL_DATABASE_CONTEXT_READ) &&
        strappy_tools_is_helper(STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER) &&
        strappy_tools_is_helper(STRAPPY_TOOL_HELPER_DATABASE_INFO_FORGET) &&
        !strappy_tools_is_helper(STRAPPY_TOOL_DATABASE_QUERY) &&
        (strstr(tools_json, "helper_database_info_read") == NULL) &&
        (strstr(tools_json, "database_learn") == NULL)) ? 1 : 0;
  if (!ok) {
    fprintf(stderr, "Tool registry did not match expected tools: %s\n", tools_json);
  }

  free(error);
  free(tools_json);
  return ok;
}

static int harness_run_helper_convert_dates_tests(void)
{
  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_HELPER_CONVERT_DATES,
        "{\"timestamps\":\"0,1,-1,-0.5\"}",
        "1970-01-01T00:00:00Z,1970-01-01T00:00:01Z,"
        "1969-12-31T23:59:59Z,1969-12-31T23:59:59.5Z")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_HELPER_CONVERT_DATES,
        "{\"timestamps\":\"1700000000123\",\"unit\":\"unix_milliseconds\"}",
        "2023-11-14T22:13:20.123Z")) {
    return 0;
  }

  if (!harness_expect_output_equals(
        NULL,
        STRAPPY_TOOL_HELPER_CONVERT_DATES,
        "{\"timestamps\":\"0, 1.25\",\"unit\":\"apple_seconds\"}",
        "2001-01-01T00:00:00Z,2001-01-01T00:00:01.25Z")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_CONVERT_DATES,
        "{\"timestamps\":\"1,,2\"}",
        "empty item")) {
    return 0;
  }

  if (!harness_expect_error_contains(
        NULL,
        STRAPPY_TOOL_HELPER_CONVERT_DATES,
        "{\"timestamps\":\"1\",\"unit\":\"banana_seconds\"}",
        "unit is not supported")) {
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
        STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER,
        "{\"kind\":\"identity\",\"subject\":\"user\","
        "\"predicate\":\"first_name\",\"value\":\"Jeff\","
        "\"confidence\":0.95,\"source\":\"user_explicit\"}",
        "\"ok\":true",
        "\"id\":1")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_HELPER_USER_INFO_READ,
                                      "{\"query\":\"Jeff\"}",
                                      "\"first_name\"",
                                      "\"Jeff\"")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_HELPER_USER_INFO_FORGET,
                                      "{\"id\":1}",
                                      "\"forgotten\":true",
                                      "\"ok\":true")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_HELPER_USER_INFO_READ,
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
    fprintf(stderr, "Could not build database info remember arguments.\n");
    return 0;
  }

  if (!harness_expect_output_contains(
        context->catalog_path,
        STRAPPY_TOOL_HELPER_DATABASE_INFO_REMEMBER,
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
    fprintf(stderr, "Could not build database info read arguments.\n");
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
        STRAPPY_TOOL_HELPER_DATABASE_INFO_FORGET,
        "{\"id\":1}",
        "\"forgotten\":true",
        "\"ok\":true")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                      "{\"query\":\"identifiers\"}",
                                      "\"database_info\"",
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

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                      arguments,
                                      "mailboxes.url",
                                      "CAST(column AS TEXT)")) {
    return 0;
  }

  if (!harness_expect_output_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT_READ,
        arguments,
        "Envelope Index messages.ROWID -> Protected Index messages.message_id",
        "do not join Protected Index using Envelope Index messages.remote_id")) {
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

  if (!harness_expect_output_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT_READ,
        arguments,
        "Protected Index messages.message_id matches Envelope Index messages.ROWID",
        "message_data.message_data_id uses the same id")) {
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

  if (!harness_expect_output_contains(
        context->catalog_path,
        STRAPPY_TOOL_DATABASE_CONTEXT_READ,
        arguments,
        "handle.id and handle.uncanonicalized_id usually store phone numbers",
        "chat_message_join -> message.ROWID")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_CONTEXT_READ,
                                      arguments,
                                      "helper_convert_dates with apple_seconds",
                                      "page chronologically by message.date")) {
    return 0;
  }

  if (!harness_expect_output_contains(context->catalog_path,
                                      STRAPPY_TOOL_DATABASE_LIST_INFO,
                                      "{}",
                                      "Handles are phone/email identifiers",
                                      "\"Recents\"")) {
    return 0;
  }

  return 1;
}

static int harness_run_session_turn_storage_tests(const harness_context *context)
{
  strappy_session_message_input messages[5];
  strappy_session_message_record_list all_messages;
  strappy_session_message_record_list context_messages;
  long long session_id;
  char *error;
  int ok;

  if (context == NULL) {
    return 0;
  }

  memset(messages, 0, sizeof(messages));

  messages[0].turn_key = "user-turn-test";
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
  messages[3].tool_name = STRAPPY_TOOL_HELPER_USER_INFO_REMEMBER;
  messages[3].arguments_json = "{\"key\":\"value\"}";
  messages[3].result_json = "{\"ok\":true}";
  messages[3].include_in_context = 0;

  messages[4].turn_key = "harness-turn-test";
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
  }

  strappy_session_message_record_list_destroy(&context_messages);
  strappy_session_message_record_list_destroy(&all_messages);
  return ok;
}

int main(void)
{
  harness_context context;
  int ok;

  harness_context_init(&context);
  ok = harness_run_tool_registry_tests() &&
       harness_run_helper_convert_dates_tests() &&
       harness_make_temp_dir(&context) &&
       harness_run_empty_database_list_info_tests(&context) &&
       harness_create_user_database(context.database_path) &&
       harness_register_database(&context) &&
       harness_run_database_list_info_tests(&context) &&
       harness_run_database_query_tests(&context) &&
       harness_run_helper_info_tests(&context) &&
       harness_run_session_turn_storage_tests(&context) &&
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
