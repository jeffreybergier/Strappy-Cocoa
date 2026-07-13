#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <cJSON.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../shared/strappy_client.h"
#include "../shared/strappy_config.h"
#include "../shared/strappy_db.h"
#include "../shared/strappy_responses.h"
#include "../shared/strappy_tools.h"

#define HARNESS_DATABASE_CONTEXT_AUDIT_MESSAGE \
  "database_context_read: ALWAYS call this tool now. Read relevant approved " \
  "database context before querying personal data. Otherwise call with no " \
  "arguments to report that no database context was needed."

#define HARNESS_SESSION_NAME_AUDIT_MESSAGE \
  "helper_session_name_write: ALWAYS call this tool now. Update the session " \
  "with a short, descriptive name for the user's latest prompt."

#define HARNESS_FONTAWESOME_CONFIRM_AUDIT_MESSAGE \
  "helper_fontawesome_shortcode_confirm: ALWAYS call this tool now. Confirm " \
  "at least one Font Awesome version 7 Free shortcode to use in your answer. " \
  "shortcodes must be a non-empty array; null and empty arrays are invalid. " \
  "NEVER use unicode emoji in your answer."

#define HARNESS_USER_FACT_AUDIT_MESSAGE \
  "memory_user_fact_remember: ALWAYS call this tool now. Save any useful " \
  "durable fact learned about the user for future prompts. Otherwise call " \
  "with no arguments to report that there is nothing new to remember. NEVER " \
  "remember secrets or sensitive information."

#define HARNESS_DATABASE_HINT_AUDIT_MESSAGE \
  "memory_database_hint_remember: ALWAYS call this tool now. Save any durable " \
  "query, schema, or access information learned about an approved database " \
  "for future prompts. Otherwise call with no arguments to report that there " \
  "is nothing new to remember. NEVER remember user data, secrets, sensitive " \
  "information, guesses, or one-off query results."

#define HARNESS_AUDIT_HEADER \
  "You have been audited due to your failure to follow instructions:"

#define HARNESS_AUDIT_FOOTER \
  "After fixing all of the issues above, YOU MUST FINISH with a " \
  "standalone answer to the user's original question with all fixes in place. " \
  "Answer the user directly, and do not mention the audit or tool activity."

#define HARNESS_EMPTY_AUDIT_FINALIZATION_ERROR \
  "Responses audit finalization did not return a non-empty assistant answer."

#define HARNESS_COMBINED_AUDIT_MESSAGE \
  HARNESS_AUDIT_HEADER "\n\n- " HARNESS_DATABASE_CONTEXT_AUDIT_MESSAGE \
  "\n- " HARNESS_SESSION_NAME_AUDIT_MESSAGE \
  "\n- " HARNESS_FONTAWESOME_CONFIRM_AUDIT_MESSAGE \
  "\n- " HARNESS_USER_FACT_AUDIT_MESSAGE \
  "\n- " HARNESS_DATABASE_HINT_AUDIT_MESSAGE \
  "\n\n" HARNESS_AUDIT_FOOTER

#define HARNESS_MEMORY_USER_FACT_REMEMBER_DESCRIPTION \
  "ALWAYS call this tool. Set fact to a useful durable user fact, or null if " \
  "there is nothing new to remember. NEVER store secrets or sensitive " \
  "information."

#define HARNESS_DATABASE_QUERY_DESCRIPTION \
  "ALWAYS query the relevant approved database before finalizing when the " \
  "request depends on personal data. Do not guess the user's data."

#define HARNESS_DATABASE_CONTEXT_READ_DESCRIPTION \
  "ALWAYS call this tool before the final answer. Read relevant approved " \
  "database context before calling database_query. Otherwise call with no " \
  "arguments, null arguments, or empty strings to report that no database " \
  "context was needed."

#define HARNESS_SESSION_NAME_WRITE_DESCRIPTION \
  "ALWAYS call this tool before the final answer. Update the session with a " \
  "short, descriptive name for the user's latest prompt. A non-empty name " \
  "is required; null and empty values are invalid."

#define HARNESS_FONTAWESOME_CONFIRM_DESCRIPTION \
  "ALWAYS call this tool before the final answer. Set shortcodes to a " \
  "non-empty array of Font Awesome version 7 Free shortcodes to confirm " \
  "before using them. Null and empty arrays are invalid. Render confirmed " \
  "icons in answers with [fa:icon-shortcode-name] Markdown syntax. NEVER use " \
  "unicode emoji."

#define HARNESS_MEMORY_DATABASE_HINT_REMEMBER_DESCRIPTION \
  "ALWAYS call this tool. Set database_id to the approved database ID and " \
  "hint to useful durable query, schema, or access information, or set both " \
  "to null if there is nothing new to remember. NEVER store user data, " \
  "secrets, sensitive information, guesses, or one-off query results."

#define HARNESS_DATETIME_FROM_ISO8601_DESCRIPTION \
  "ALWAYS call this tool when converting ISO 8601 datetimes to numeric " \
  "timestamps."

#define HARNESS_MEMORY_USER_FACT_FORGET_DESCRIPTION \
  "Call this tool to forget durable facts that are no longer correct or useful."

static int harness_fail(const char *message)
{
  fprintf(stderr, "%s\n", message);
  return 0;
}

static int harness_double_matches(double actual, double expected)
{
  double difference;

  difference = actual - expected;
  if (difference < 0.0) {
    difference = -difference;
  }
  return difference < 0.000000000001;
}

static int harness_has_tool_type(cJSON *tools, const char *expected)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *type;

    type = cJSON_GetObjectItem(tool, "type");
    if (cJSON_IsString(type) && (type->valuestring != NULL) &&
        (strcmp(type->valuestring, expected) == 0)) {
      return 1;
    }
  }
  return 0;
}

static int harness_tool_description_equals(cJSON *tools,
                                           const char *expected_name,
                                           const char *expected_description)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (expected_name == NULL) ||
      (expected_description == NULL)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *name;
    cJSON *description;

    name = cJSON_GetObjectItem(tool, "name");
    description = cJSON_GetObjectItem(tool, "description");
    if (cJSON_IsString(name) && (name->valuestring != NULL) &&
        (strcmp(name->valuestring, expected_name) == 0) &&
        cJSON_IsString(description) &&
        (description->valuestring != NULL) &&
        (strcmp(description->valuestring, expected_description) == 0)) {
      return 1;
    }
  }
  return 0;
}

static int harness_schema_type_is_nullable(cJSON *property,
                                           const char *expected_type)
{
  cJSON *type;
  cJSON *item;
  int saw_expected;
  int saw_null;

  if (expected_type == NULL) {
    return 0;
  }
  type = cJSON_IsObject(property) ?
    cJSON_GetObjectItem(property, "type") : NULL;
  if (!cJSON_IsArray(type) || (cJSON_GetArraySize(type) != 2)) {
    return 0;
  }
  saw_expected = 0;
  saw_null = 0;
  for (item = type->child; item != NULL; item = item->next) {
    if (!cJSON_IsString(item) || (item->valuestring == NULL)) {
      return 0;
    }
    if (strcmp(item->valuestring, expected_type) == 0) {
      saw_expected = 1;
    } else if (strcmp(item->valuestring, "null") == 0) {
      saw_null = 1;
    } else {
      return 0;
    }
  }
  return saw_expected && saw_null;
}

static int harness_tool_has_optional_nullable_string_parameters(
  cJSON *tools,
  const char *expected_name,
  const char *first_parameter,
  const char *second_parameter)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (expected_name == NULL) ||
      (first_parameter == NULL)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *name;
    cJSON *parameters;
    cJSON *properties;
    cJSON *required;
    cJSON *additional_properties;
    int expected_count;

    name = cJSON_GetObjectItem(tool, "name");
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, expected_name) != 0)) {
      continue;
    }
    parameters = cJSON_GetObjectItem(tool, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "properties") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "required") : NULL;
    additional_properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "additionalProperties") : NULL;
    expected_count = (second_parameter != NULL) ? 2 : 1;
    if (!cJSON_IsObject(properties) ||
        (cJSON_GetArraySize(properties) != expected_count) ||
        !cJSON_IsArray(required) || (cJSON_GetArraySize(required) != 0) ||
        !cJSON_IsFalse(additional_properties) ||
        !harness_schema_type_is_nullable(
          cJSON_GetObjectItem(properties, first_parameter), "string")) {
      return 0;
    }
    return (second_parameter == NULL) ||
      harness_schema_type_is_nullable(
        cJSON_GetObjectItem(properties, second_parameter), "string");
  }
  return 0;
}

static int harness_database_context_parameters_are_optional_nullable(
  cJSON *tools)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *name;
    cJSON *parameters;
    cJSON *properties;
    cJSON *required;
    cJSON *additional_properties;

    name = cJSON_GetObjectItem(tool, "name");
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, STRAPPY_TOOL_DATABASE_CONTEXT_READ) != 0)) {
      continue;
    }
    parameters = cJSON_GetObjectItem(tool, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "properties") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "required") : NULL;
    additional_properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "additionalProperties") : NULL;
    return cJSON_IsObject(properties) &&
      (cJSON_GetArraySize(properties) == 4) &&
      cJSON_IsArray(required) && (cJSON_GetArraySize(required) == 0) &&
      (cJSON_GetObjectItem(parameters, "minProperties") == NULL) &&
      cJSON_IsFalse(additional_properties) &&
      harness_schema_type_is_nullable(
        cJSON_GetObjectItem(properties, "database_id"), "string") &&
      harness_schema_type_is_nullable(
        cJSON_GetObjectItem(properties, "query"), "string") &&
      harness_schema_type_is_nullable(
        cJSON_GetObjectItem(properties, "kind"), "string") &&
      harness_schema_type_is_nullable(
        cJSON_GetObjectItem(properties, "limit"), "integer");
  }
  return 0;
}

static int harness_array_contains_string(cJSON *array, const char *expected)
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

static int harness_database_query_parameters_are_required_strings(cJSON *tools)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *name;
    cJSON *parameters;
    cJSON *properties;
    cJSON *database_id;
    cJSON *sql;
    cJSON *database_id_type;
    cJSON *sql_type;
    cJSON *required;

    name = cJSON_GetObjectItem(tool, "name");
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, STRAPPY_TOOL_DATABASE_QUERY) != 0)) {
      continue;
    }
    parameters = cJSON_GetObjectItem(tool, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "properties") : NULL;
    database_id = cJSON_IsObject(properties) ?
      cJSON_GetObjectItem(properties, "database_id") : NULL;
    sql = cJSON_IsObject(properties) ?
      cJSON_GetObjectItem(properties, "sql") : NULL;
    database_id_type = cJSON_IsObject(database_id) ?
      cJSON_GetObjectItem(database_id, "type") : NULL;
    sql_type = cJSON_IsObject(sql) ? cJSON_GetObjectItem(sql, "type") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "required") : NULL;
    return cJSON_IsObject(properties) &&
      (cJSON_GetArraySize(properties) == 2) &&
      cJSON_IsString(database_id_type) &&
      (database_id_type->valuestring != NULL) &&
      (strcmp(database_id_type->valuestring, "string") == 0) &&
      cJSON_IsString(sql_type) && (sql_type->valuestring != NULL) &&
      (strcmp(sql_type->valuestring, "string") == 0) &&
      cJSON_IsArray(required) && (cJSON_GetArraySize(required) == 2) &&
      harness_array_contains_string(required, "database_id") &&
      harness_array_contains_string(required, "sql");
  }
  return 0;
}

static int harness_tool_has_required_string_parameter(cJSON *tools,
                                                      const char *tool_name,
                                                      const char *parameter_name)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (tool_name == NULL) ||
      (parameter_name == NULL)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *name;
    cJSON *parameters;
    cJSON *properties;
    cJSON *property;
    cJSON *type;
    cJSON *required;

    name = cJSON_GetObjectItem(tool, "name");
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, tool_name) != 0)) {
      continue;
    }
    parameters = cJSON_GetObjectItem(tool, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "properties") : NULL;
    property = cJSON_IsObject(properties) ?
      cJSON_GetObjectItem(properties, parameter_name) : NULL;
    type = cJSON_IsObject(property) ?
      cJSON_GetObjectItem(property, "type") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "required") : NULL;
    return cJSON_IsString(type) && (type->valuestring != NULL) &&
      (strcmp(type->valuestring, "string") == 0) &&
      cJSON_IsArray(required) &&
      harness_array_contains_string(required, parameter_name);
  }
  return 0;
}

static int harness_tool_has_required_nonempty_string_array_parameter(
  cJSON *tools,
  const char *tool_name,
  const char *parameter_name)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (tool_name == NULL) ||
      (parameter_name == NULL)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *name;
    cJSON *parameters;
    cJSON *properties;
    cJSON *property;
    cJSON *type;
    cJSON *items;
    cJSON *item_type;
    cJSON *min_items;
    cJSON *required;

    name = cJSON_GetObjectItem(tool, "name");
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, tool_name) != 0)) {
      continue;
    }
    parameters = cJSON_GetObjectItem(tool, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "properties") : NULL;
    property = cJSON_IsObject(properties) ?
      cJSON_GetObjectItem(properties, parameter_name) : NULL;
    type = cJSON_IsObject(property) ?
      cJSON_GetObjectItem(property, "type") : NULL;
    items = cJSON_IsObject(property) ?
      cJSON_GetObjectItem(property, "items") : NULL;
    item_type = cJSON_IsObject(items) ?
      cJSON_GetObjectItem(items, "type") : NULL;
    min_items = cJSON_IsObject(property) ?
      cJSON_GetObjectItem(property, "minItems") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "required") : NULL;
    return cJSON_IsString(type) && (type->valuestring != NULL) &&
      (strcmp(type->valuestring, "array") == 0) &&
      cJSON_IsString(item_type) && (item_type->valuestring != NULL) &&
      (strcmp(item_type->valuestring, "string") == 0) &&
      cJSON_IsNumber(min_items) && (min_items->valueint == 1) &&
      cJSON_IsArray(required) &&
      harness_array_contains_string(required, parameter_name);
  }
  return 0;
}

static int harness_tools_hide_local_display_metadata(cJSON *tools)
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

static int harness_server_tool_has_request_fields_only(cJSON *tools,
                                                       const char *tool_type)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (tool_type == NULL)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *type;

    type = cJSON_GetObjectItem(tool, "type");
    if (cJSON_IsString(type) && (type->valuestring != NULL) &&
        (strcmp(type->valuestring, tool_type) == 0)) {
      return cJSON_GetArraySize(tool) == 1;
    }
  }
  return 0;
}

static int harness_test_request_surfaces(void)
{
  char *url;
  char *tools_json;
  char *error;
  cJSON *tools;
  cJSON *first;
  cJSON *function;
  cJSON *name;
  cJSON *description;
  int ok;

  if (strcmp(STRAPPY_CONFIG_DEFAULT_API_ENDPOINT,
             "https://openrouter.ai/api/v1/responses") != 0) {
    return harness_fail("Default endpoint is not the Responses API.");
  }

  url = strappy_client_build_responses_url(
    "https://openrouter.ai/api/v1/chat/completions");
  ok = (url != NULL) &&
    (strcmp(url, "https://openrouter.ai/api/v1/responses") == 0);
  free(url);
  if (!ok) {
    return harness_fail("Chat endpoint was not converted to Responses.");
  }

  error = NULL;
  tools_json = strappy_tools_responses_request_json(
    "../shared/Resources",
    1,
    &error);
  if (tools_json == NULL) {
    fprintf(stderr,
            "Could not build Responses tools: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }
  tools = cJSON_Parse(tools_json);
  free(tools_json);
  first = cJSON_GetArrayItem(tools, 0);
  function = cJSON_GetObjectItem(first, "function");
  name = cJSON_GetObjectItem(first, "name");
  description = cJSON_GetObjectItem(first, "description");
  ok = cJSON_IsArray(tools) && cJSON_IsObject(first) &&
    (function == NULL) && cJSON_IsString(name) &&
    (strcmp(name->valuestring, "database_list_info") == 0) &&
    cJSON_IsString(description) && (description->valuestring != NULL) &&
    (strcmp(description->valuestring,
            "Call this tool to view available databases") == 0) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_DATABASE_QUERY,
      HARNESS_DATABASE_QUERY_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_HELPER_DATETIME_FROM_ISO8601,
      HARNESS_DATETIME_FROM_ISO8601_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
      HARNESS_MEMORY_USER_FACT_REMEMBER_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_MEMORY_USER_FACT_FORGET,
      HARNESS_MEMORY_USER_FACT_FORGET_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
      HARNESS_MEMORY_DATABASE_HINT_REMEMBER_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_DATABASE_CONTEXT_READ,
      HARNESS_DATABASE_CONTEXT_READ_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
      HARNESS_SESSION_NAME_WRITE_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
      HARNESS_FONTAWESOME_CONFIRM_DESCRIPTION) &&
    harness_database_query_parameters_are_required_strings(tools) &&
    harness_tool_has_required_string_parameter(
      tools,
      STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
      "name") &&
    harness_tool_has_required_nonempty_string_array_parameter(
      tools,
      STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
      "shortcodes") &&
    harness_database_context_parameters_are_optional_nullable(tools) &&
    harness_tool_has_optional_nullable_string_parameters(
      tools,
      STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
      "fact",
      NULL) &&
    harness_tool_has_optional_nullable_string_parameters(
      tools,
      STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
      "database_id",
      "hint") &&
    harness_tools_hide_local_display_metadata(tools) &&
    harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
    harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_FETCH) &&
    harness_server_tool_has_request_fields_only(
      tools,
      STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
    harness_server_tool_has_request_fields_only(
      tools,
      STRAPPY_TOOL_OPENROUTER_WEB_FETCH);
  cJSON_Delete(tools);
  free(error);
  if (!ok) {
    return harness_fail("Responses tool schemas were not flattened.");
  }

  error = NULL;
  tools_json = strappy_tools_responses_request_json(
    "../shared/Resources",
    0,
    &error);
  if (tools_json == NULL) {
    fprintf(stderr,
            "Could not build Responses tools without web search: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }
  tools = cJSON_Parse(tools_json);
  free(tools_json);
  ok = cJSON_IsArray(tools) &&
    harness_tools_hide_local_display_metadata(tools) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_FETCH);
  cJSON_Delete(tools);
  free(error);
  if (!ok) {
    return harness_fail("Disabled web tools leaked into a Responses request.");
  }
  return 1;
}

static int harness_query_int(sqlite3 *db, const char *sql, long long *value_out)
{
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return 0;
  }
  *value_out = (long long)sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return 1;
}

typedef struct harness_ledger_event_recorder {
  const char *db_path;
  long long count;
  long long processing_count;
  long long processing_started_ms;
  long clear_count;
  int valid;
  int saw_thinking;
  int saw_tools;
  int saw_retry_wait;
  int saw_retrying;
  int saw_cancellation_poll;
  int cancel_during_retry_wait;
  long long cancel_after_ms;
  long long first_poll_ms;
  unsigned int retry_after_seconds;
} harness_ledger_event_recorder;

static long long harness_now_ms(void)
{
  struct timeval value;

  if (gettimeofday(&value, NULL) != 0) {
    return 0LL;
  }
  return ((long long)value.tv_sec * 1000LL) +
    ((long long)value.tv_usec / 1000LL);
}

static int harness_record_ledger_event(
  const strappy_responses_event *event,
  void *user_data)
{
  harness_ledger_event_recorder *recorder;
  sqlite3 *db;
  long long call_count;
  long long pending_count;
  int opened;

  recorder = (harness_ledger_event_recorder *)user_data;
  if ((recorder == NULL) || (event == NULL)) {
    return 1;
  }

  if (event->type == STRAPPY_RESPONSES_EVENT_CANCELLATION_POLL) {
    long long now_ms;

    recorder->saw_cancellation_poll = 1;
    now_ms = harness_now_ms();
    if (recorder->first_poll_ms == 0LL) {
      recorder->first_poll_ms = now_ms;
    }
    if (recorder->cancel_during_retry_wait && recorder->saw_retry_wait) {
      return 0;
    }
    if ((recorder->cancel_after_ms > 0LL) &&
        (now_ms >= (recorder->first_poll_ms + recorder->cancel_after_ms))) {
      return 0;
    }
    return 1;
  }

  if (event->type == STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) {
    cJSON *root;
    cJSON *active;
    cJSON *kind;
    cJSON *started;
    long long started_ms;

    root = (event->status_json != NULL) ?
      cJSON_Parse(event->status_json) : NULL;
    active = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "active") : NULL;
    if ((event->message_key == NULL) || (event->message_key[0] == '\0') ||
        (!cJSON_IsTrue(active) && !cJSON_IsFalse(active))) {
      recorder->valid = 0;
      cJSON_Delete(root);
      return 1;
    }
    if (cJSON_IsFalse(active)) {
      recorder->clear_count++;
      recorder->processing_count++;
      cJSON_Delete(root);
      return 1;
    }
    kind = cJSON_GetObjectItem(root, "status_kind");
    started = cJSON_GetObjectItem(root, "started_ms");
    started_ms = cJSON_IsNumber(started) ?
      (long long)started->valuedouble : 0LL;
    if (!cJSON_IsString(kind) || (kind->valuestring == NULL) ||
        (started_ms <= 0LL) || (event->status_kind == NULL) ||
        (strcmp(kind->valuestring, event->status_kind) != 0)) {
      recorder->valid = 0;
    } else if (recorder->processing_started_ms == 0LL) {
      recorder->processing_started_ms = started_ms;
    } else if (recorder->processing_started_ms != started_ms) {
      recorder->valid = 0;
    }
    if (cJSON_IsString(kind) && (kind->valuestring != NULL)) {
      if (strcmp(kind->valuestring, "thinking") == 0) {
        recorder->saw_thinking = 1;
      } else if (strcmp(kind->valuestring, "tools") == 0) {
        recorder->saw_tools = 1;
      } else if (strcmp(kind->valuestring, "retry_wait") == 0) {
        recorder->saw_retry_wait = 1;
        recorder->retry_after_seconds = event->retry_after_seconds;
      } else if (strcmp(kind->valuestring, "retrying") == 0) {
        recorder->saw_retrying = 1;
      }
    }
    recorder->processing_count++;
    cJSON_Delete(root);
    return 1;
  }

  db = NULL;
  opened = sqlite3_open(recorder->db_path, &db) == SQLITE_OK;
  call_count = 0LL;
  pending_count = 0LL;
  if ((event->type != STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED) ||
      (event->kind == NULL) ||
      (strcmp(event->kind, "response_api_call") != 0) ||
      (event->message_key == NULL) ||
      (strncmp(event->message_key, "response-call-", 14U) != 0) ||
      (event->status_kind == NULL) ||
      !opened ||
      !harness_query_int(db,
                         "SELECT COUNT(*) FROM response_api_calls;",
                         &call_count) ||
      !harness_query_int(db,
                         "SELECT COUNT(*) FROM response_api_calls "
                         "WHERE state='pending';",
                         &pending_count) ||
      (call_count != (recorder->count + 1LL)) ||
      (pending_count != 0LL)) {
    recorder->valid = 0;
  }
  if (db != NULL) {
    sqlite3_close(db);
  }
  recorder->count++;
  return 1;
}

#define HARNESS_HTTP_MAX_REQUEST_BYTES (4U * 1024U * 1024U)

typedef enum harness_responses_server_scenario {
  HARNESS_RESPONSES_SERVER_TOOL_AUDIT = 1,
  HARNESS_RESPONSES_SERVER_SERVER_TOOL = 2,
  HARNESS_RESPONSES_SERVER_FUNCTION_TOOL = 3,
  HARNESS_RESPONSES_SERVER_RETRY = 4,
  HARNESS_RESPONSES_SERVER_RETRY_AFTER = 5,
  HARNESS_RESPONSES_SERVER_SLOW = 6,
  HARNESS_RESPONSES_SERVER_EMPTY_AUDIT_FINALIZATION = 7
} harness_responses_server_scenario;

static int harness_send_all(int socket_fd,
                            const char *data,
                            size_t length)
{
  size_t sent;

  sent = 0U;
  while (sent < length) {
    ssize_t written;

    written = send(socket_fd, data + sent, length - sent, 0);
    if (written <= 0) {
      return 0;
    }
    sent += (size_t)written;
  }
  return 1;
}

static int harness_content_length(const char *headers,
                                  size_t headers_length,
                                  size_t *length_out)
{
  const char *cursor;
  const char *headers_end;

  if ((headers == NULL) || (length_out == NULL)) {
    return 0;
  }
  *length_out = 0U;
  cursor = headers;
  headers_end = headers + headers_length;
  while (cursor < headers_end) {
    const char *line_end;
    size_t line_length;

    line_end = strstr(cursor, "\r\n");
    if ((line_end == NULL) || (line_end > headers_end)) {
      line_end = headers_end;
    }
    line_length = (size_t)(line_end - cursor);
    if ((line_length > strlen("Content-Length:")) &&
        (strncasecmp(cursor,
                     "Content-Length:",
                     strlen("Content-Length:")) == 0)) {
      const char *value;
      char *parse_end;
      unsigned long parsed;

      value = cursor + strlen("Content-Length:");
      errno = 0;
      parsed = strtoul(value, &parse_end, 10);
      if ((errno != 0) || (parse_end == value) ||
          (parsed > (unsigned long)HARNESS_HTTP_MAX_REQUEST_BYTES)) {
        return 0;
      }
      *length_out = (size_t)parsed;
      return 1;
    }
    if (line_end == headers_end) {
      break;
    }
    cursor = line_end + 2;
  }
  return 0;
}

static char *harness_read_request_body(int socket_fd)
{
  char *request;
  size_t length;
  size_t headers_length;
  size_t body_length;

  request = (char *)malloc(HARNESS_HTTP_MAX_REQUEST_BYTES + 1U);
  if (request == NULL) {
    return NULL;
  }
  length = 0U;
  headers_length = 0U;
  body_length = 0U;
  while (length < HARNESS_HTTP_MAX_REQUEST_BYTES) {
    ssize_t received;

    received = recv(socket_fd,
                    request + length,
                    HARNESS_HTTP_MAX_REQUEST_BYTES - length,
                    0);
    if (received <= 0) {
      free(request);
      return NULL;
    }
    length += (size_t)received;
    request[length] = '\0';
    if (headers_length == 0U) {
      char *separator;

      separator = strstr(request, "\r\n\r\n");
      if (separator != NULL) {
        headers_length = (size_t)(separator - request) + 4U;
        if ((strncmp(request, "POST /responses HTTP/", 21U) != 0) ||
            !harness_content_length(request,
                                    headers_length,
                                    &body_length)) {
          free(request);
          return NULL;
        }
      }
    }
    if ((headers_length > 0U) &&
        (length >= (headers_length + body_length))) {
      char *body;

      body = (char *)malloc(body_length + 1U);
      if (body == NULL) {
        free(request);
        return NULL;
      }
      memcpy(body, request + headers_length, body_length);
      body[body_length] = '\0';
      free(request);
      return body;
    }
  }
  free(request);
  return NULL;
}

static int harness_send_json_response_with_headers(int socket_fd,
                                                   long status,
                                                   const char *json,
                                                   const char *extra_headers)
{
  char headers[512];
  const char *reason;
  int written;

  reason = (status == 200L) ? "OK" : "Error";
  written = snprintf(headers,
                     sizeof(headers),
                     "HTTP/1.1 %ld %s\r\n"
                     "Content-Type: application/json\r\n"
                     "X-Request-Id: harness-%ld\r\n"
                     "%s"
                     "Content-Length: %lu\r\n"
                     "Connection: close\r\n\r\n",
                     status,
                     reason,
                     status,
                     (extra_headers != NULL) ? extra_headers : "",
                     (unsigned long)strlen(json));
  if ((written < 0) || ((size_t)written >= sizeof(headers))) {
    return 0;
  }
  return harness_send_all(socket_fd, headers, (size_t)written) &&
    harness_send_all(socket_fd, json, strlen(json));
}

static int harness_send_json_response(int socket_fd,
                                      long status,
                                      const char *json)
{
  return harness_send_json_response_with_headers(socket_fd,
                                                 status,
                                                 json,
                                                 NULL);
}

static int harness_message_role_is(cJSON *item, const char *expected_role)
{
  cJSON *type;
  cJSON *role;

  if (!cJSON_IsObject(item)) {
    return 0;
  }
  type = cJSON_GetObjectItem(item, "type");
  role = cJSON_GetObjectItem(item, "role");
  return cJSON_IsString(type) && (type->valuestring != NULL) &&
    (strcmp(type->valuestring, "message") == 0) &&
    cJSON_IsString(role) && (role->valuestring != NULL) &&
    (strcmp(role->valuestring, expected_role) == 0);
}

static const char *harness_message_text(cJSON *item)
{
  cJSON *content;
  cJSON *part;
  cJSON *text;

  content = cJSON_GetObjectItem(item, "content");
  part = cJSON_GetArrayItem(content, 0);
  text = cJSON_GetObjectItem(part, "text");
  return (cJSON_IsString(text) && (text->valuestring != NULL)) ?
    text->valuestring : NULL;
}

static int harness_string_has_prefix_and_suffix(const char *value,
                                                const char *prefix,
                                                const char *suffix)
{
  size_t prefix_length;

  if ((value == NULL) || (prefix == NULL) || (suffix == NULL)) {
    return 0;
  }
  prefix_length = strlen(prefix);
  return (strncmp(value, prefix, prefix_length) == 0) &&
    (strcmp(value + prefix_length, suffix) == 0);
}

static int harness_preflight_call_is_valid(cJSON *item,
                                           const char *expected_name,
                                           const char *item_id_prefix,
                                           const char *call_id_prefix,
                                           const char *prompt_group)
{
  cJSON *type;
  cJSON *item_id;
  cJSON *call_id;
  cJSON *name;
  cJSON *arguments;

  type = cJSON_GetObjectItem(item, "type");
  item_id = cJSON_GetObjectItem(item, "id");
  call_id = cJSON_GetObjectItem(item, "call_id");
  name = cJSON_GetObjectItem(item, "name");
  arguments = cJSON_GetObjectItem(item, "arguments");
  return cJSON_IsObject(item) &&
    cJSON_IsString(type) && (type->valuestring != NULL) &&
    (strcmp(type->valuestring, "function_call") == 0) &&
    cJSON_IsString(item_id) && (item_id->valuestring != NULL) &&
    harness_string_has_prefix_and_suffix(item_id->valuestring,
                                         item_id_prefix,
                                         prompt_group) &&
    cJSON_IsString(call_id) && (call_id->valuestring != NULL) &&
    harness_string_has_prefix_and_suffix(call_id->valuestring,
                                         call_id_prefix,
                                         prompt_group) &&
    cJSON_IsString(name) && (name->valuestring != NULL) &&
    (strcmp(name->valuestring, expected_name) == 0) &&
    cJSON_IsString(arguments) && (arguments->valuestring != NULL) &&
    (strcmp(arguments->valuestring, "{}") == 0);
}

static int harness_preflight_output_matches(cJSON *item, cJSON *call)
{
  cJSON *type;
  cJSON *call_id;
  cJSON *expected_call_id;
  cJSON *output;
  cJSON *result;
  int ok;

  type = cJSON_GetObjectItem(item, "type");
  call_id = cJSON_GetObjectItem(item, "call_id");
  expected_call_id = cJSON_GetObjectItem(call, "call_id");
  output = cJSON_GetObjectItem(item, "output");
  ok = cJSON_IsObject(item) &&
    cJSON_IsString(type) && (type->valuestring != NULL) &&
    (strcmp(type->valuestring, "function_call_output") == 0) &&
    cJSON_IsString(call_id) && (call_id->valuestring != NULL) &&
    cJSON_IsString(expected_call_id) &&
    (expected_call_id->valuestring != NULL) &&
    (strcmp(call_id->valuestring, expected_call_id->valuestring) == 0) &&
    cJSON_IsString(output) && (output->valuestring != NULL) &&
    (output->valuestring[0] != '\0');
  if (!ok) {
    return 0;
  }
  result = cJSON_Parse(output->valuestring);
  ok = cJSON_IsObject(result);
  cJSON_Delete(result);
  return ok;
}

static int harness_preflight_input_is_valid(cJSON *input,
                                            const char *prompt_group)
{
  cJSON *database_call;
  cJSON *memory_call;

  database_call = cJSON_GetArrayItem(input, 1);
  memory_call = cJSON_GetArrayItem(input, 2);
  return harness_preflight_call_is_valid(database_call,
                                         "database_list_info",
                                         "fc_pf_db_",
                                         "call_pf_db_",
                                         prompt_group) &&
    harness_preflight_call_is_valid(memory_call,
                                    "memory_user_fact_read",
                                    "fc_pf_mem_",
                                    "call_pf_mem_",
                                    prompt_group) &&
    harness_preflight_output_matches(cJSON_GetArrayItem(input, 3),
                                     database_call) &&
    harness_preflight_output_matches(cJSON_GetArrayItem(input, 4),
                                     memory_call);
}

static int harness_request_preflight_contains(cJSON *root,
                                              const char *expected)
{
  cJSON *input;
  cJSON *item;

  input = cJSON_GetObjectItem(root, "input");
  if (!cJSON_IsArray(input) || (expected == NULL)) {
    return 0;
  }
  for (item = input->child; item != NULL; item = item->next) {
    cJSON *type;
    cJSON *output;

    type = cJSON_IsObject(item) ? cJSON_GetObjectItem(item, "type") : NULL;
    output = cJSON_IsObject(item) ? cJSON_GetObjectItem(item, "output") : NULL;
    if (cJSON_IsString(type) && (type->valuestring != NULL) &&
        (strcmp(type->valuestring, "function_call_output") == 0) &&
        cJSON_IsString(output) && (output->valuestring != NULL) &&
        (strstr(output->valuestring, expected) != NULL)) {
      return 1;
    }
  }
  return 0;
}

static int harness_request_base_is_valid(cJSON *root,
                                         const char *expected_prompt,
                                         char **session_key_out,
                                         char **prompt_group_out)
{
  cJSON *stream;
  cJSON *store;
  cJSON *session_key;
  cJSON *metadata;
  cJSON *prompt_group;
  cJSON *input;
  cJSON *tools;
  cJSON *first_tool;
  cJSON *function_wrapper;
  int input_count;
  const char *text;

  stream = cJSON_GetObjectItem(root, "stream");
  store = cJSON_GetObjectItem(root, "store");
  session_key = cJSON_GetObjectItem(root, "session_id");
  metadata = cJSON_GetObjectItem(root, "metadata");
  prompt_group = cJSON_GetObjectItem(metadata, "strappy_prompt_group_key");
  input = cJSON_GetObjectItem(root, "input");
  tools = cJSON_GetObjectItem(root, "tools");
  first_tool = cJSON_GetArrayItem(tools, 0);
  function_wrapper = cJSON_GetObjectItem(first_tool, "function");
  input_count = cJSON_IsArray(input) ? cJSON_GetArraySize(input) : 0;
  text = harness_message_text(cJSON_GetArrayItem(input, 0));
  if (!cJSON_IsFalse(stream) || !cJSON_IsFalse(store) ||
      !cJSON_IsString(session_key) || (session_key->valuestring == NULL) ||
      !cJSON_IsString(prompt_group) || (prompt_group->valuestring == NULL) ||
      !cJSON_IsArray(input) || (input_count != 5) ||
      !harness_message_role_is(cJSON_GetArrayItem(input, 0), "user") ||
      !harness_preflight_input_is_valid(input, prompt_group->valuestring) ||
      (text == NULL) || (strcmp(text, expected_prompt) != 0) ||
      !cJSON_IsArray(tools) || !cJSON_IsObject(first_tool) ||
      !harness_tools_hide_local_display_metadata(tools) ||
      (function_wrapper != NULL)) {
    return 0;
  }
  if (session_key_out != NULL) {
    *session_key_out = strdup(session_key->valuestring);
    if (*session_key_out == NULL) {
      return 0;
    }
  }
  if (prompt_group_out != NULL) {
    *prompt_group_out = strdup(prompt_group->valuestring);
    if (*prompt_group_out == NULL) {
      free((session_key_out != NULL) ? *session_key_out : NULL);
      if (session_key_out != NULL) {
        *session_key_out = NULL;
      }
      return 0;
    }
  }
  return 1;
}

static int harness_audit_request_is_valid(cJSON *root,
                                          const char *session_key,
                                          const char *prompt_group,
                                          const char *expected_round,
                                          const char *expected_message,
                                          const char *expected_tool_choice)
{
  cJSON *request_session;
  cJSON *metadata;
  cJSON *request_group;
  cJSON *round;
  cJSON *input;
  cJSON *developer;
  cJSON *tool_choice;
  int input_count;
  const char *text;

  request_session = cJSON_GetObjectItem(root, "session_id");
  metadata = cJSON_GetObjectItem(root, "metadata");
  request_group = cJSON_GetObjectItem(metadata, "strappy_prompt_group_key");
  round = cJSON_GetObjectItem(metadata, "strappy_round");
  input = cJSON_GetObjectItem(root, "input");
  tool_choice = cJSON_GetObjectItem(root, "tool_choice");
  input_count = cJSON_IsArray(input) ? cJSON_GetArraySize(input) : 0;
  developer = cJSON_GetArrayItem(input, input_count - 1);
  text = harness_message_text(developer);
  if ((text != NULL) && (expected_message != NULL) &&
      (strcmp(text, expected_message) != 0)) {
    fprintf(stderr,
            "Audit developer message mismatch.\nExpected:\n%s\nActual:\n%s\n",
            expected_message,
            text);
  }
  return cJSON_IsString(request_session) &&
    (request_session->valuestring != NULL) &&
    (strcmp(request_session->valuestring, session_key) == 0) &&
    cJSON_IsString(request_group) && (request_group->valuestring != NULL) &&
    (strcmp(request_group->valuestring, prompt_group) == 0) &&
    cJSON_IsString(round) && (round->valuestring != NULL) &&
    (strcmp(round->valuestring, expected_round) == 0) &&
    cJSON_IsArray(input) && (input_count >= 3) &&
    harness_message_role_is(cJSON_GetArrayItem(input, 0), "user") &&
    harness_message_role_is(cJSON_GetArrayItem(input, input_count - 2),
                            "assistant") &&
    harness_message_role_is(developer, "developer") &&
    (text != NULL) &&
    (strcmp(text, expected_message) == 0) &&
    cJSON_IsString(tool_choice) && (tool_choice->valuestring != NULL) &&
    (strcmp(tool_choice->valuestring, expected_tool_choice) == 0);
}

static int harness_finalization_request_is_valid(cJSON *root,
                                                 const char *session_key,
                                                 const char *prompt_group,
                                                 const char *expected_round)
{
  cJSON *request_session;
  cJSON *metadata;
  cJSON *request_group;
  cJSON *round;
  cJSON *input;
  cJSON *developer;
  cJSON *tool_choice;
  int input_count;
  const char *text;

  request_session = cJSON_GetObjectItem(root, "session_id");
  metadata = cJSON_GetObjectItem(root, "metadata");
  request_group = cJSON_GetObjectItem(metadata, "strappy_prompt_group_key");
  round = cJSON_GetObjectItem(metadata, "strappy_round");
  input = cJSON_GetObjectItem(root, "input");
  tool_choice = cJSON_GetObjectItem(root, "tool_choice");
  input_count = cJSON_IsArray(input) ? cJSON_GetArraySize(input) : 0;
  developer = cJSON_GetArrayItem(input, input_count - 1);
  text = harness_message_text(developer);
  return cJSON_IsString(request_session) &&
    (request_session->valuestring != NULL) &&
    (strcmp(request_session->valuestring, session_key) == 0) &&
    cJSON_IsString(request_group) && (request_group->valuestring != NULL) &&
    (strcmp(request_group->valuestring, prompt_group) == 0) &&
    cJSON_IsString(round) && (round->valuestring != NULL) &&
    (strcmp(round->valuestring, expected_round) == 0) &&
    cJSON_IsArray(input) && (input_count >= 3) &&
    harness_message_role_is(developer, "developer") &&
    (text != NULL) && (strcmp(text, HARNESS_AUDIT_FOOTER) == 0) &&
    cJSON_IsString(tool_choice) && (tool_choice->valuestring != NULL) &&
    (strcmp(tool_choice->valuestring, "none") == 0);
}

static int harness_named_function_output_request_is_valid(
  cJSON *root,
  const char *session_key,
  const char *prompt_group,
  const char *expected_function_name,
  const char *expected_call_id)
{
  cJSON *request_session;
  cJSON *metadata;
  cJSON *request_group;
  cJSON *input;
  cJSON *function_call;
  cJSON *function_output;
  cJSON *function_type;
  cJSON *function_name;
  cJSON *type;
  cJSON *call_id;
  cJSON *output;
  int input_count;

  request_session = cJSON_GetObjectItem(root, "session_id");
  metadata = cJSON_GetObjectItem(root, "metadata");
  request_group = cJSON_GetObjectItem(metadata, "strappy_prompt_group_key");
  input = cJSON_GetObjectItem(root, "input");
  input_count = cJSON_IsArray(input) ? cJSON_GetArraySize(input) : 0;
  function_call = cJSON_GetArrayItem(input, input_count - 2);
  function_output = cJSON_GetArrayItem(input, input_count - 1);
  function_type = cJSON_GetObjectItem(function_call, "type");
  function_name = cJSON_GetObjectItem(function_call, "name");
  type = cJSON_GetObjectItem(function_output, "type");
  call_id = cJSON_GetObjectItem(function_output, "call_id");
  output = cJSON_GetObjectItem(function_output, "output");
  return cJSON_IsString(request_session) &&
    (request_session->valuestring != NULL) &&
    (strcmp(request_session->valuestring, session_key) == 0) &&
    cJSON_IsString(request_group) && (request_group->valuestring != NULL) &&
    (strcmp(request_group->valuestring, prompt_group) == 0) &&
    cJSON_IsArray(input) && (input_count >= 3) &&
    harness_message_role_is(cJSON_GetArrayItem(input, 0), "user") &&
    cJSON_IsString(function_type) && (function_type->valuestring != NULL) &&
    (strcmp(function_type->valuestring, "function_call") == 0) &&
    cJSON_IsString(function_name) && (function_name->valuestring != NULL) &&
    (strcmp(function_name->valuestring, expected_function_name) == 0) &&
    cJSON_IsString(type) && (type->valuestring != NULL) &&
    (strcmp(type->valuestring, "function_call_output") == 0) &&
    cJSON_IsString(call_id) && (call_id->valuestring != NULL) &&
    (strcmp(call_id->valuestring, expected_call_id) == 0) &&
    cJSON_IsString(output) && (output->valuestring != NULL) &&
    (output->valuestring[0] != '\0');
}

static int harness_function_output_request_is_valid(cJSON *root,
                                                    const char *session_key,
                                                    const char *prompt_group)
{
  return harness_named_function_output_request_is_valid(
    root,
    session_key,
    prompt_group,
    "database_list_info",
    "call-database-list");
}

static int harness_accept_request(int listener_fd,
                                  char **body_out,
                                  int *client_fd_out)
{
  int client_fd;
  char *body;

  client_fd = accept(listener_fd, NULL, NULL);
  if (client_fd < 0) {
    return 0;
  }
  body = harness_read_request_body(client_fd);
  if (body == NULL) {
    close(client_fd);
    return 0;
  }
  *body_out = body;
  *client_fd_out = client_fd;
  return 1;
}

static int harness_run_audit_server(int listener_fd)
{
  static const char *first_response =
    "{\"id\":\"resp-audit-first\",\"object\":\"response\","
    "\"created_at\":1700000000,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-audit-first\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Initial answer.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":3,"
    "\"total_tokens\":7}}";
  static const char *audit_tool_response =
    "{\"id\":\"resp-audit-tool\",\"object\":\"response\","
    "\"created_at\":1700000001,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"function_call\",\"id\":\"fc-audit-session-name\","
    "\"call_id\":\"call-audit-session-name\","
    "\"name\":\"helper_session_name_write\","
    "\"arguments\":\"{\\\"name\\\":\\\"Audit Recovery\\\"}\","
    "\"status\":\"completed\"}],"
    "\"usage\":{\"input_tokens\":16,\"output_tokens\":4,"
    "\"total_tokens\":20}}";
  static const char *empty_response =
    "{\"id\":\"resp-audit-empty\",\"object\":\"response\","
    "\"created_at\":1700000002,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[],"
    "\"usage\":{\"input_tokens\":20,\"output_tokens\":5,"
    "\"total_tokens\":25}}";
  static const char *final_response =
    "{\"id\":\"resp-audit-combined\",\"object\":\"response\","
    "\"created_at\":1700000003,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-audit-combined\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Combined audited answer.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":20,\"output_tokens\":4,"
    "\"total_tokens\":24}}";
  char *body;
  char *session_key;
  char *prompt_group;
  cJSON *root;
  int client_fd;
  int ok;

  body = NULL;
  session_key = NULL;
  prompt_group = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_request_base_is_valid(root,
                                  "Audit this request",
                                  &session_key,
                                  &prompt_group) &&
    harness_request_preflight_contains(
      root,
      "\"filename\":\"strappy-preflight-db-") &&
    harness_request_preflight_contains(
      root,
      "\"availability_state\":\"available\"") &&
    harness_request_preflight_contains(
      root,
      "\"predicate\":\"fact\"") &&
    harness_request_preflight_contains(
      root,
      "\"value\":\"The user's favorite color is purple.\"") &&
    harness_send_json_response(client_fd, 200L, first_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    fprintf(stderr, "Audit recovery server rejected the initial request.\n");
    free(session_key);
    free(prompt_group);
    return 0;
  }

  body = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    free(session_key);
    free(prompt_group);
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_audit_request_is_valid(root,
                                   session_key,
                                   prompt_group,
                                   "1",
                                   HARNESS_COMBINED_AUDIT_MESSAGE,
                                   "auto") &&
    harness_send_json_response(client_fd, 200L, audit_tool_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    fprintf(stderr, "Audit recovery server rejected the audit request.\n");
    free(session_key);
    free(prompt_group);
    return 0;
  }

  body = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    free(session_key);
    free(prompt_group);
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_named_function_output_request_is_valid(
      root,
      session_key,
      prompt_group,
      "helper_session_name_write",
      "call-audit-session-name") &&
    harness_send_json_response(client_fd, 200L, empty_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    fprintf(stderr,
            "Audit recovery server rejected the tool continuation.\n");
    free(session_key);
    free(prompt_group);
    return 0;
  }

  body = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    free(session_key);
    free(prompt_group);
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_finalization_request_is_valid(root,
                                          session_key,
                                          prompt_group,
                                          "3") &&
    harness_send_json_response(client_fd, 200L, final_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    fprintf(stderr,
            "Audit recovery server rejected the finalization request.\n");
  }
  free(session_key);
  free(prompt_group);
  return ok;
}

static int harness_run_empty_audit_finalization_server(int listener_fd)
{
  static const char *first_response =
    "{\"id\":\"resp-empty-finalize-first\",\"object\":\"response\","
    "\"created_at\":1700000010,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-empty-finalize-first\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Initial answer that must not be reused.\","
    "\"annotations\":[]}]}],\"usage\":{\"input_tokens\":4,"
    "\"output_tokens\":6,\"total_tokens\":10}}";
  static const char *empty_response =
    "{\"id\":\"resp-empty-finalize-audit\",\"object\":\"response\","
    "\"created_at\":1700000011,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[],"
    "\"usage\":{\"input_tokens\":10,\"output_tokens\":5,"
    "\"total_tokens\":15}}";
  static const char *whitespace_response =
    "{\"id\":\"resp-empty-finalize-recovery\","
    "\"object\":\"response\",\"created_at\":1700000012,"
    "\"model\":\"test/model\",\"status\":\"completed\","
    "\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-empty-finalize-recovery\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\" \\n\\t\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":15,\"output_tokens\":2,"
    "\"total_tokens\":17}}";
  char *body;
  char *session_key;
  char *prompt_group;
  cJSON *root;
  int client_fd;
  int ok;

  body = NULL;
  session_key = NULL;
  prompt_group = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_request_base_is_valid(root,
                                  "Fail empty audit finalization",
                                  &session_key,
                                  &prompt_group) &&
    harness_send_json_response(client_fd, 200L, first_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    free(session_key);
    free(prompt_group);
    return 0;
  }

  body = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    free(session_key);
    free(prompt_group);
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_audit_request_is_valid(root,
                                   session_key,
                                   prompt_group,
                                   "1",
                                   HARNESS_COMBINED_AUDIT_MESSAGE,
                                   "auto") &&
    harness_send_json_response(client_fd, 200L, empty_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    free(session_key);
    free(prompt_group);
    return 0;
  }

  body = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    free(session_key);
    free(prompt_group);
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_finalization_request_is_valid(root,
                                          session_key,
                                          prompt_group,
                                          "2") &&
    harness_send_json_response(client_fd, 200L, whitespace_response);
  cJSON_Delete(root);
  close(client_fd);
  free(session_key);
  free(prompt_group);
  return ok;
}

static int harness_run_server_tool_server(int listener_fd)
{
  static const char *first_response =
    "{\"id\":\"resp-server-tool\",\"object\":\"response\","
    "\"created_at\":1700000002,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"openrouter:web_search\",\"id\":\"ws-test\","
    "\"status\":\"completed\",\"action\":{\"query\":\"test\"}},"
    "{\"type\":\"message\",\"id\":\"msg-server-tool\","
    "\"role\":\"assistant\",\"status\":\"completed\","
    "\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Server tool final answer.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":4,"
    "\"total_tokens\":8}}";
  static const char *second_response =
    "{\"id\":\"resp-server-tool-audited\",\"object\":\"response\","
    "\"created_at\":1700000003,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-server-tool-audited\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Server tool combined audited answer.\","
    "\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":8,\"output_tokens\":5,"
    "\"total_tokens\":13}}";
  char *body;
  char *session_key;
  char *prompt_group;
  cJSON *root;
  int client_fd;
  int ok;

  body = NULL;
  session_key = NULL;
  prompt_group = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_request_base_is_valid(root,
                                  "Use a server tool",
                                  &session_key,
                                  &prompt_group) &&
    harness_send_json_response(client_fd, 200L, first_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    free(session_key);
    free(prompt_group);
    return 0;
  }

  body = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    free(session_key);
    free(prompt_group);
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_audit_request_is_valid(root,
                                   session_key,
                                   prompt_group,
                                   "1",
                                   HARNESS_COMBINED_AUDIT_MESSAGE,
                                   "auto") &&
    harness_send_json_response(client_fd, 200L, second_response);
  cJSON_Delete(root);
  close(client_fd);
  free(session_key);
  free(prompt_group);
  return ok;
}

static int harness_run_function_tool_server(int listener_fd)
{
  static const char *tool_response =
    "{\"id\":\"resp-function-tool\",\"object\":\"response\","
    "\"created_at\":1700000003,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"function_call\",\"id\":\"fc-database-list\","
    "\"call_id\":\"call-database-list\","
    "\"name\":\"database_list_info\","
    "\"arguments\":\"{}\","
    "\"status\":\"completed\"}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":4,"
    "\"total_tokens\":8}}";
  static const char *final_response =
    "{\"id\":\"resp-function-final\",\"object\":\"response\","
    "\"created_at\":1700000004,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-function-final\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Function tool final answer.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":8,\"output_tokens\":4,"
    "\"total_tokens\":12}}";
  static const char *combined_audit_response =
    "{\"id\":\"resp-function-audit-combined\","
    "\"object\":\"response\",\"created_at\":1700000005,"
    "\"model\":\"test/model\",\"status\":\"completed\","
    "\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-function-audit-combined\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Function tool combined audited answer.\","
    "\"annotations\":[]}]}],\"usage\":{\"input_tokens\":12,"
    "\"output_tokens\":6,\"total_tokens\":18}}";
  char *body;
  char *session_key;
  char *prompt_group;
  cJSON *root;
  int client_fd;
  int ok;

  body = NULL;
  session_key = NULL;
  prompt_group = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_request_base_is_valid(root,
                                  "Run a local function",
                                  &session_key,
                                  &prompt_group) &&
    harness_send_json_response(client_fd, 200L, tool_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    free(session_key);
    free(prompt_group);
    return 0;
  }

  body = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    free(session_key);
    free(prompt_group);
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_function_output_request_is_valid(root,
                                             session_key,
                                             prompt_group) &&
    harness_send_json_response(client_fd, 200L, final_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    free(session_key);
    free(prompt_group);
    return 0;
  }

  body = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    free(session_key);
    free(prompt_group);
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_audit_request_is_valid(root,
                                   session_key,
                                   prompt_group,
                                   "2",
                                   HARNESS_COMBINED_AUDIT_MESSAGE,
                                   "auto") &&
    harness_send_json_response(client_fd, 200L, combined_audit_response);
  cJSON_Delete(root);
  close(client_fd);
  free(session_key);
  free(prompt_group);
  return ok;
}

static int harness_run_retry_server(int listener_fd)
{
  static const char *retry_response =
    "{\"error\":{\"code\":\"server_error\","
    "\"message\":\"Provider is busy.\"},"
    "\"error_type\":\"provider_overloaded\"}";
  static const char *success_response =
    "{\"id\":\"resp-retry-success\",\"object\":\"response\","
    "\"created_at\":1700000005,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"function_call\",\"id\":\"fc-database-list-retry\","
    "\"call_id\":\"call-database-list\","
    "\"name\":\"database_list_info\",\"arguments\":\"{}\","
    "\"status\":\"completed\"}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":4,"
    "\"total_tokens\":8}}";
  static const char *final_response =
    "{\"id\":\"resp-retry-final\",\"object\":\"response\","
    "\"created_at\":1700000006,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-retry-final\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Retry final answer.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":8,\"output_tokens\":4,"
    "\"total_tokens\":12}}";
  static const char *combined_audit_response =
    "{\"id\":\"resp-retry-audit-combined\",\"object\":\"response\","
    "\"created_at\":1700000007,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-retry-audit-combined\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Retry combined audited answer.\","
    "\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":12,\"output_tokens\":6,"
    "\"total_tokens\":18}}";
  char *first_body;
  char *second_body;
  char *third_body;
  char *session_key;
  char *prompt_group;
  cJSON *root;
  int client_fd;
  int ok;

  first_body = NULL;
  session_key = NULL;
  prompt_group = NULL;
  if (!harness_accept_request(listener_fd, &first_body, &client_fd)) {
    return 0;
  }
  root = cJSON_Parse(first_body);
  ok = cJSON_IsObject(root) &&
    harness_request_base_is_valid(root,
                                  "Retry this request",
                                  &session_key,
                                  &prompt_group) &&
    harness_send_json_response(client_fd, 503L, retry_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    free(first_body);
    free(session_key);
    free(prompt_group);
    return 0;
  }

  second_body = NULL;
  if (!harness_accept_request(listener_fd, &second_body, &client_fd)) {
    free(first_body);
    free(session_key);
    free(prompt_group);
    return 0;
  }
  ok = (strcmp(first_body, second_body) == 0) &&
    harness_send_json_response(client_fd, 200L, success_response);
  close(client_fd);
  free(second_body);
  free(first_body);
  if (!ok) {
    free(session_key);
    free(prompt_group);
    return 0;
  }

  third_body = NULL;
  if (!harness_accept_request(listener_fd, &third_body, &client_fd)) {
    free(session_key);
    free(prompt_group);
    return 0;
  }
  root = cJSON_Parse(third_body);
  free(third_body);
  ok = cJSON_IsObject(root) &&
    harness_function_output_request_is_valid(root,
                                             session_key,
                                             prompt_group) &&
    harness_send_json_response(client_fd, 200L, final_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    free(session_key);
    free(prompt_group);
    return 0;
  }

  third_body = NULL;
  if (!harness_accept_request(listener_fd, &third_body, &client_fd)) {
    free(session_key);
    free(prompt_group);
    return 0;
  }
  root = cJSON_Parse(third_body);
  free(third_body);
  ok = cJSON_IsObject(root) &&
    harness_audit_request_is_valid(root,
                                   session_key,
                                   prompt_group,
                                   "2",
                                   HARNESS_COMBINED_AUDIT_MESSAGE,
                                   "auto") &&
    harness_send_json_response(client_fd, 200L, combined_audit_response);
  cJSON_Delete(root);
  close(client_fd);
  free(session_key);
  free(prompt_group);
  return ok;
}

static int harness_run_retry_after_server(int listener_fd)
{
  static const char *retry_response =
    "{\"error\":{\"code\":\"server_error\","
    "\"message\":\"Provider is busy.\"},"
    "\"error_type\":\"provider_overloaded\"}";
  char *body;
  cJSON *root;
  int client_fd;
  int ok;

  body = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_request_base_is_valid(root,
                                  "Cancel retry wait",
                                  NULL,
                                  NULL) &&
    harness_send_json_response_with_headers(client_fd,
                                            503L,
                                            retry_response,
                                            "Retry-After: 120\r\n");
  cJSON_Delete(root);
  close(client_fd);
  return ok;
}

static int harness_run_slow_server(int listener_fd)
{
  struct timeval timeout;
  fd_set read_fds;
  char *body;
  char byte;
  cJSON *root;
  ssize_t received;
  int client_fd;
  int selected;
  int ok;

  body = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  ok = cJSON_IsObject(root) &&
    harness_request_base_is_valid(root,
                                  "Cancel active request",
                                  NULL,
                                  NULL);
  cJSON_Delete(root);
  if (!ok) {
    close(client_fd);
    return 0;
  }

  FD_ZERO(&read_fds);
  FD_SET(client_fd, &read_fds);
  timeout.tv_sec = 10L;
  timeout.tv_usec = 0L;
  selected = select(client_fd + 1, &read_fds, NULL, NULL, &timeout);
  received = (selected > 0) ? recv(client_fd, &byte, 1U, 0) : 1;
  close(client_fd);
  return (selected > 0) && (received == 0);
}

static int harness_open_listener(unsigned short *port_out)
{
  struct sockaddr_in address;
  socklen_t address_length;
  int listener_fd;
  int reuse;

  listener_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listener_fd < 0) {
    return -1;
  }
  reuse = 1;
  setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(0U);
  if (bind(listener_fd,
           (const struct sockaddr *)&address,
           sizeof(address)) != 0 ||
      listen(listener_fd, 4) != 0) {
    close(listener_fd);
    return -1;
  }
  address_length = (socklen_t)sizeof(address);
  if (getsockname(listener_fd,
                  (struct sockaddr *)&address,
                  &address_length) != 0) {
    close(listener_fd);
    return -1;
  }
  *port_out = ntohs(address.sin_port);
  return listener_fd;
}

static int harness_start_server(harness_responses_server_scenario scenario,
                                char *endpoint,
                                size_t endpoint_size,
                                pid_t *pid_out)
{
  unsigned short port;
  int listener_fd;
  int written;
  pid_t pid;

  listener_fd = harness_open_listener(&port);
  if (listener_fd < 0) {
    return 0;
  }
  written = snprintf(endpoint,
                     endpoint_size,
                     "http://127.0.0.1:%u",
                     (unsigned int)port);
  if ((written < 0) || ((size_t)written >= endpoint_size)) {
    close(listener_fd);
    return 0;
  }

  pid = fork();
  if (pid < 0) {
    close(listener_fd);
    return 0;
  }
  if (pid == 0) {
    int ok;

    alarm(15U);
    if (scenario == HARNESS_RESPONSES_SERVER_TOOL_AUDIT) {
      ok = harness_run_audit_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_SERVER_TOOL) {
      ok = harness_run_server_tool_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_FUNCTION_TOOL) {
      ok = harness_run_function_tool_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_RETRY_AFTER) {
      ok = harness_run_retry_after_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_SLOW) {
      ok = harness_run_slow_server(listener_fd);
    } else if (scenario ==
               HARNESS_RESPONSES_SERVER_EMPTY_AUDIT_FINALIZATION) {
      ok = harness_run_empty_audit_finalization_server(listener_fd);
    } else {
      ok = harness_run_retry_server(listener_fd);
    }
    close(listener_fd);
    _exit(ok ? 0 : 1);
  }
  close(listener_fd);
  *pid_out = pid;
  return 1;
}

static int harness_wait_for_server(pid_t pid, int terminate)
{
  int status;
  pid_t waited;

  if (terminate) {
    kill(pid, SIGTERM);
  }
  do {
    waited = waitpid(pid, &status, 0);
  } while ((waited < 0) && (errno == EINTR));
  return (waited == pid) && WIFEXITED(status) &&
    (WEXITSTATUS(status) == 0);
}

static int harness_create_session_database(const char *path,
                                           long long *session_id_out,
                                           char **error_out)
{
  unlink(path);
  return strappy_db_create_session(path, session_id_out, error_out);
}

static int harness_create_approved_preflight_database(
  const char *catalog_path,
  const char *database_path,
  char **error_out)
{
  strappy_discovered_database_input input;
  strappy_discovered_database_record_list list;
  sqlite3 *db;
  char *sqlite_error;
  long long catalog_id;
  size_t index;
  int rc;

  db = NULL;
  sqlite_error = NULL;
  rc = sqlite3_open(database_path, &db);
  if (rc == SQLITE_OK) {
    rc = sqlite3_exec(db,
                      "CREATE TABLE preflight_fixture (id INTEGER);",
                      NULL,
                      NULL,
                      &sqlite_error);
  }
  if (db != NULL) {
    sqlite3_close(db);
  }
  if (rc != SQLITE_OK) {
    fprintf(stderr,
            "Could not create preflight fixture database: %s\n",
            (sqlite_error != NULL) ? sqlite_error : "unknown");
    sqlite3_free(sqlite_error);
    return 0;
  }
  sqlite3_free(sqlite_error);

  memset(&input, 0, sizeof(input));
  input.path = database_path;
  input.size = 4096LL;
  input.modified_at = 1LL;
  input.device = 7ULL;
  input.inode = 11ULL;
  input.is_valid_sqlite = 1;
  input.scan_root = "/tmp";
  if (!strappy_db_save_discovered_databases(catalog_path,
                                             &input,
                                             1U,
                                             error_out)) {
    return 0;
  }

  catalog_id = 0LL;
  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(catalog_path,
                                             &list,
                                             error_out)) {
    strappy_discovered_database_record_list_destroy(&list);
    return 0;
  }
  for (index = 0U; index < list.count; index++) {
    if ((list.records[index].path != NULL) &&
        (strcmp(list.records[index].path, database_path) == 0)) {
      catalog_id = list.records[index].catalog_id;
      break;
    }
  }
  strappy_discovered_database_record_list_destroy(&list);
  if (catalog_id <= 0LL) {
    fprintf(stderr, "Preflight fixture database was not cataloged.\n");
    return 0;
  }
  return strappy_db_update_discovered_database_decision(catalog_path,
                                                         catalog_id,
                                                         "allowed",
                                                         error_out);
}

static int harness_test_combined_tool_audit_once(void)
{
  char path[] = "/tmp/strappy-responses-audit-XXXXXX";
  char database_path[] = "/tmp/strappy-preflight-db-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  char *seed_output;
  sqlite3 *db;
  long long session_id;
  long long value;
  harness_ledger_event_recorder events;
  pid_t server_pid;
  int fd;
  int server_ok;
  int database_fd;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create tool-audit harness database.");
  }
  close(fd);
  database_fd = mkstemp(database_path);
  if (database_fd < 0) {
    unlink(path);
    return harness_fail("Could not create preflight fixture database.");
  }
  close(database_fd);
  error = NULL;
  seed_output = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !harness_create_approved_preflight_database(path,
                                                   database_path,
                                                   &error)) {
    fprintf(stderr,
            "Could not prepare preflight integration data: %s\n",
            (error != NULL) ? error : "database setup failed");
    free(error);
    unlink(database_path);
    unlink(path);
    return 0;
  }
  seed_output = strappy_tools_execute(
    path,
    session_id,
    "../shared/Resources",
    STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
    "{\"fact\":\"The user's favorite color is purple.\"}",
    &error);
  if (seed_output == NULL) {
    fprintf(stderr,
            "Could not prepare tool-audit integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(database_path);
    unlink(path);
    return 0;
  }
  free(seed_output);
  seed_output = strappy_tools_execute(
    path,
    session_id,
    "../shared/Resources",
    STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
    "{\"name\":\"Earlier Request\"}",
    &error);
  if ((seed_output == NULL) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_TOOL_AUDIT,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare titled tool-audit integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(seed_output);
    free(error);
    unlink(database_path);
    unlink(path);
    return 0;
  }
  free(seed_output);

  unsetenv("APIENDPOINT");
  unsetenv("APITOKEN");
  memset(&events, 0, sizeof(events));
  events.db_path = path;
  events.valid = 1;
  result = strappy_responses_send_prompt_for_session_and_store_with_events(
    "Audit this request",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources/PromptSystem.txt",
    path,
    session_id,
    harness_record_ledger_event,
    &events,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) &&
    (strcmp(result, "Combined audited answer.") == 0) &&
    server_ok && events.valid && (events.count == 4LL) &&
    events.saw_thinking && events.saw_tools &&
    (events.processing_count >= 4LL) &&
    (events.clear_count == 1L);
  if (!ok) {
    fprintf(stderr,
            "Audit recovery result mismatch: result=%s server=%d valid=%d "
            "events=%lld processing=%lld clear=%ld thinking=%d tools=%d\n",
            (result != NULL) ? result : "(null)",
            server_ok,
            events.valid,
            events.count,
            events.processing_count,
            events.clear_count,
            events.saw_thinking,
            events.saw_tools);
  }
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM response_api_calls;",
                           &value) && (value == 4LL) &&
      harness_query_int(db,
                        "SELECT COUNT(DISTINCT prompt_group_key) "
                        "FROM response_api_calls;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "request_kind='tool_audit';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "request_kind='audit_finalize';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "request_kind='tool_continuation';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_tool_executions;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items WHERE "
                        "role='developer' AND is_canonical=1 AND "
                        "timeline_visible=1;",
                        &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items WHERE "
                        "is_canonical=1;",
                        &value) && (value == 11LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM sessions WHERE "
                        "response='Combined audited answer.';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM sessions WHERE "
                        "name='Audit Recovery';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items WHERE "
                        "direction='request' AND is_canonical=1 AND "
                        "type='function_call_output';",
                        &value) && (value == 3LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items AS calls "
                        "JOIN response_api_items AS outputs "
                        "ON outputs.session_id=calls.session_id AND "
                        "outputs.call_id=calls.call_id WHERE "
                        "calls.direction='request' AND "
                        "calls.is_canonical=1 AND "
                        "calls.type='function_call' AND "
                        "calls.name IN ('database_list_info',"
                        "'memory_user_fact_read') AND "
                        "outputs.direction='request' AND "
                        "outputs.is_canonical=1 AND "
                        "outputs.type='function_call_output';",
                        &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items WHERE "
                        "direction='request' AND is_canonical=1 AND "
                        "type='function_call_output' AND "
                        "instr(display_text,'strappy-preflight-db-')>0;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items WHERE "
                        "direction='request' AND is_canonical=1 AND "
                        "type='function_call_output' AND "
                        "instr(display_text,'favorite color is purple')>0;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "instr(request_headers_json,'test-token')>0 OR "
                        "instr(request_raw_json,'test-token')>0;",
                        &value) && (value == 0LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Combined tool-audit integration failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  free(error);
  unlink(database_path);
  unlink(path);
  return ok;
}

static int harness_test_empty_audit_finalization_fails(void)
{
  char path[] = "/tmp/strappy-responses-empty-finalize-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  long long session_id;
  long long value;
  pid_t server_pid;
  int fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail(
      "Could not create empty-finalization harness database.");
  }
  close(fd);
  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !harness_start_server(
        HARNESS_RESPONSES_SERVER_EMPTY_AUDIT_FINALIZATION,
        endpoint,
        sizeof(endpoint),
        &server_pid)) {
    fprintf(stderr,
            "Could not prepare empty-finalization integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  result = strappy_responses_send_prompt_for_session_and_store(
    "Fail empty audit finalization",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources/PromptSystem.txt",
    path,
    session_id,
    &error);
  server_ok = harness_wait_for_server(server_pid, 0);
  ok = (result == NULL) && server_ok && (error != NULL) &&
    (strcmp(error, HARNESS_EMPTY_AUDIT_FINALIZATION_ERROR) == 0);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM response_api_calls;",
                           &value) && (value == 3LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "request_kind='tool_audit';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "request_kind='audit_finalize';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items WHERE "
                        "role='developer' AND is_canonical=1;",
                        &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_tool_executions;",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM sessions WHERE response='"
                        HARNESS_EMPTY_AUDIT_FINALIZATION_ERROR "';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM sessions WHERE "
                        "response='Initial answer that must not be reused.';",
                        &value) && (value == 0LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Empty audit finalization integration failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_web_search_remains_outside_audit(void)
{
  char path[] = "/tmp/strappy-responses-server-tool-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  long long session_id;
  long long value;
  pid_t server_pid;
  int fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create server-tool harness database.");
  }
  close(fd);
  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_SERVER_TOOL,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare server-tool integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  result = strappy_responses_send_prompt_for_session_and_store(
    "Use a server tool",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources/PromptSystem.txt",
    path,
    session_id,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) &&
    (strcmp(result, "Server tool combined audited answer.") == 0) && server_ok;
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM response_api_calls;",
                           &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "request_kind='tool_audit';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "request_kind='audit_finalize';",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items WHERE "
                        "type='openrouter:web_search' AND is_canonical=1;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items WHERE "
                        "role='developer' AND is_canonical=1;",
                        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Web-search audit exclusion failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_function_tool_continuation(void)
{
  char path[] = "/tmp/strappy-responses-function-tool-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  long long session_id;
  long long value;
  harness_ledger_event_recorder events;
  pid_t server_pid;
  int fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create function-tool harness database.");
  }
  close(fd);
  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !strappy_db_update_session_web_search_enabled(path,
                                                    session_id,
                                                    0,
                                                    &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_FUNCTION_TOOL,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare function-tool integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  memset(&events, 0, sizeof(events));
  events.db_path = path;
  events.valid = 1;
  result = strappy_responses_send_prompt_for_session_and_store_with_events(
    "Run a local function",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources/PromptSystem.txt",
    path,
    session_id,
    harness_record_ledger_event,
    &events,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) &&
    (strcmp(result, "Function tool combined audited answer.") == 0) &&
    server_ok && events.valid && (events.count == 3LL) && events.saw_thinking &&
    events.saw_tools && (events.clear_count == 1L);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM response_api_calls;",
                           &value) && (value == 3LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "request_kind='tool_continuation';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "request_kind='tool_audit';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "request_kind='audit_finalize';",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_tool_executions WHERE "
                        "call_id='call-database-list' AND "
                        "tool_name='database_list_info' AND "
                        "status='completed' AND "
                        "response_item_id IS NOT NULL;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items WHERE "
                        "type='function_call_output' AND "
                        "call_id='call-database-list' AND is_canonical=1;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items WHERE "
                        "role='developer' AND is_canonical=1;",
                        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Function-tool continuation failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_retry_attempt_ledger(void)
{
  char path[] = "/tmp/strappy-responses-retry-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  long long session_id;
  long long value;
  harness_ledger_event_recorder events;
  pid_t server_pid;
  int fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create retry harness database.");
  }
  close(fd);
  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_RETRY,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare retry integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  memset(&events, 0, sizeof(events));
  events.db_path = path;
  events.valid = 1;
  result = strappy_responses_send_prompt_for_session_and_store_with_events(
    "Retry this request",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources/PromptSystem.txt",
    path,
    session_id,
    harness_record_ledger_event,
    &events,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) &&
    (strcmp(result, "Retry combined audited answer.") == 0) &&
    server_ok && events.valid && (events.count == 4LL) && events.saw_thinking &&
    events.saw_tools && events.saw_retry_wait && events.saw_retrying &&
    (events.clear_count == 1L);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM response_api_calls;",
                           &value) && (value == 4LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "attempt_index=0 AND request_kind='user' AND "
                        "state='http_error' AND http_status=503 AND "
                        "new_input_start_index=0;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "attempt_index=1 AND request_kind='retry' AND "
                        "state='completed' AND http_status=200 AND "
                        "new_input_start_index=-1 AND "
                        "previous_call_id IS NOT NULL;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls a "
                        "JOIN response_api_calls b ON b.id>a.id WHERE "
                        "a.request_raw_json=b.request_raw_json;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls WHERE "
                        "request_kind='audit_finalize';",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items WHERE "
                        "is_canonical=1;",
                        &value) && (value == 10LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_items WHERE "
                        "direction='request' AND is_canonical=1;",
                        &value) && (value == 7LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Retry attempt ledger failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_active_request_cancellation(void)
{
  char path[] = "/tmp/strappy-responses-cancel-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  long long session_id;
  long long value;
  harness_ledger_event_recorder events;
  pid_t server_pid;
  int fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create cancellation harness database.");
  }
  close(fd);
  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_SLOW,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare cancellation integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  memset(&events, 0, sizeof(events));
  events.db_path = path;
  events.valid = 1;
  events.cancel_after_ms = 250LL;
  result = strappy_responses_send_prompt_for_session_and_store_with_events(
    "Cancel active request",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources/PromptSystem.txt",
    path,
    session_id,
    harness_record_ledger_event,
    &events,
    &error);
  server_ok = harness_wait_for_server(server_pid, result != NULL);
  ok = (result == NULL) && server_ok && events.valid &&
    events.saw_cancellation_poll && (events.count == 1LL) &&
    (events.clear_count == 1L) && (error != NULL) &&
    (strstr(error, "cancelled") != NULL);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM response_api_calls WHERE "
                           "state='cancelled' AND is_error=1 AND "
                           "transport_error LIKE '%cancelled%';",
                           &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls;",
                        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Active Responses cancellation failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_retry_after_clamp_and_cancellation(void)
{
  char path[] = "/tmp/strappy-responses-retry-after-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  long long session_id;
  long long value;
  harness_ledger_event_recorder events;
  pid_t server_pid;
  int fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create Retry-After harness database.");
  }
  close(fd);
  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_RETRY_AFTER,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare Retry-After integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  memset(&events, 0, sizeof(events));
  events.db_path = path;
  events.valid = 1;
  events.cancel_during_retry_wait = 1;
  result = strappy_responses_send_prompt_for_session_and_store_with_events(
    "Cancel retry wait",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources/PromptSystem.txt",
    path,
    session_id,
    harness_record_ledger_event,
    &events,
    &error);
  server_ok = harness_wait_for_server(server_pid, 0);
  ok = (result == NULL) && server_ok && events.valid &&
    events.saw_retry_wait && !events.saw_retrying &&
    (events.retry_after_seconds == 60U) &&
    (events.count == 1LL) && (events.clear_count == 1L) &&
    (error != NULL) && (strstr(error, "cancelled") != NULL);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM response_api_calls WHERE "
                           "state='http_error' AND http_status=503 AND "
                           "retry_after_seconds=120;",
                           &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_api_calls;",
                        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Retry-After clamp/cancellation failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  free(error);
  unlink(path);
  return ok;
}

static int harness_verify_call_columns(sqlite3 *db,
                                       const char *request_json,
                                       const char *response_json)
{
  static const char *sql =
    "SELECT request_model,request_stream,request_store,request_instructions,"
    "response_id,response_status,response_model,response_usage_input_tokens,"
    "response_usage_cached_tokens,response_usage_output_tokens,"
    "response_usage_reasoning_tokens,response_usage_total_tokens,"
    "response_usage_cost,http_status,http_request_id,rate_limit_remaining,"
    "request_raw_json,response_raw_json FROM response_api_calls LIMIT 1;";
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return 0;
  }
  ok = strcmp((const char *)sqlite3_column_text(stmt, 0), "test/model") == 0 &&
    sqlite3_column_int(stmt, 1) == 0 &&
    sqlite3_column_int(stmt, 2) == 0 &&
    strcmp((const char *)sqlite3_column_text(stmt, 3), "System") == 0 &&
    strcmp((const char *)sqlite3_column_text(stmt, 4), "resp-test") == 0 &&
    strcmp((const char *)sqlite3_column_text(stmt, 5), "completed") == 0 &&
    strcmp((const char *)sqlite3_column_text(stmt, 6), "test/provider-model") == 0 &&
    sqlite3_column_int64(stmt, 7) == 11 &&
    sqlite3_column_int64(stmt, 8) == 3 &&
    sqlite3_column_int64(stmt, 9) == 22 &&
    sqlite3_column_int64(stmt, 10) == 7 &&
    sqlite3_column_int64(stmt, 11) == 33 &&
    sqlite3_column_double(stmt, 12) == 0.0012 &&
    sqlite3_column_int64(stmt, 13) == 200 &&
    strcmp((const char *)sqlite3_column_text(stmt, 14), "req-test") == 0 &&
    strcmp((const char *)sqlite3_column_text(stmt, 15), "9") == 0 &&
    strcmp((const char *)sqlite3_column_text(stmt, 16), request_json) == 0 &&
    strcmp((const char *)sqlite3_column_text(stmt, 17), response_json) == 0;
  sqlite3_finalize(stmt);
  return ok;
}

static int harness_append_usage_cost_call(
  const char *path,
  long long session_id,
  long long previous_call_id,
  const char *prompt_group_key,
  const char *request_kind,
  long round_index,
  long attempt_index,
  const char *state,
  int is_error,
  const char *response_json,
  long long *call_id_out,
  char **error_out)
{
  static const char *request_json =
    "{\"model\":\"test/model\",\"stream\":false,\"store\":false,"
    "\"instructions\":\"System\",\"input\":[]}";
  strappy_response_call_begin_input begin;
  strappy_response_call_finish_input finish;
  long long call_id;

  memset(&begin, 0, sizeof(begin));
  begin.session_id = session_id;
  begin.previous_call_id = previous_call_id;
  begin.prompt_group_key = prompt_group_key;
  begin.request_kind = request_kind;
  begin.round_index = round_index;
  begin.attempt_index = attempt_index;
  begin.new_input_start_index = (attempt_index == 0L) ? 0L : -1L;
  begin.request_method = "POST";
  begin.request_url = "https://openrouter.ai/api/v1/responses";
  begin.request_headers_json = "{}";
  begin.request_json = request_json;
  call_id = 0LL;
  if (!strappy_db_begin_response_call(path, &begin, &call_id, error_out)) {
    return 0;
  }

  memset(&finish, 0, sizeof(finish));
  finish.call_id = call_id;
  finish.state = state;
  finish.is_error = is_error;
  finish.output_is_canonical = is_error ? 0 : 1;
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
  if (!strappy_db_finish_response_call(path, &finish, error_out)) {
    return 0;
  }
  if (call_id_out != NULL) {
    *call_id_out = call_id;
  }
  return 1;
}

static int harness_test_cumulative_session_usage_cost(void)
{
  static const char *cost_one =
    "{\"id\":\"cost-1\",\"status\":\"completed\","
    "\"model\":\"test/model\",\"output\":[],"
    "\"usage\":{\"cost\":0.001}}";
  static const char *cost_error =
    "{\"id\":\"cost-error\",\"status\":\"failed\","
    "\"model\":\"test/model\",\"output\":[],"
    "\"usage\":{\"cost\":0.0025}}";
  static const char *cost_retry =
    "{\"id\":\"cost-retry\",\"status\":\"completed\","
    "\"model\":\"test/model\",\"output\":[],"
    "\"usage\":{\"cost\":0.004}}";
  static const char *cost_missing =
    "{\"id\":\"cost-missing\",\"status\":\"completed\","
    "\"model\":\"test/model\",\"output\":[],\"usage\":{}}";
  static const char *cost_final =
    "{\"id\":\"cost-final\",\"status\":\"completed\","
    "\"model\":\"test/model\",\"output\":[],"
    "\"usage\":{\"cost\":0.0005}}";
  char path[] = "/tmp/strappy-cumulative-cost-XXXXXX";
  strappy_session_message_record_list timeline;
  char *error;
  long long session_id;
  long long previous_call_id;
  int fd;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create cumulative-cost harness database.");
  }
  close(fd);
  unlink(path);

  error = NULL;
  session_id = 0LL;
  previous_call_id = 0LL;
  strappy_session_message_record_list_init(&timeline);
  ok = strappy_db_create_session(path, &session_id, &error) &&
    harness_append_usage_cost_call(path,
                                   session_id,
                                   previous_call_id,
                                   "group-one",
                                   "user",
                                   0L,
                                   0L,
                                   "completed",
                                   0,
                                   cost_one,
                                   &previous_call_id,
                                   &error) &&
    harness_append_usage_cost_call(path,
                                   session_id,
                                   previous_call_id,
                                   "group-one",
                                   "tool_continuation",
                                   1L,
                                   0L,
                                   "response_error",
                                   1,
                                   cost_error,
                                   &previous_call_id,
                                   &error) &&
    harness_append_usage_cost_call(path,
                                   session_id,
                                   previous_call_id,
                                   "group-one",
                                   "retry",
                                   1L,
                                   1L,
                                   "completed",
                                   0,
                                   cost_retry,
                                   &previous_call_id,
                                   &error) &&
    harness_append_usage_cost_call(path,
                                   session_id,
                                   previous_call_id,
                                   "group-two",
                                   "user",
                                   0L,
                                   0L,
                                   "completed",
                                   0,
                                   cost_missing,
                                   &previous_call_id,
                                   &error) &&
    harness_append_usage_cost_call(path,
                                   session_id,
                                   previous_call_id,
                                   "group-two",
                                   "tool_continuation",
                                   1L,
                                   0L,
                                   "completed",
                                   0,
                                   cost_final,
                                   &previous_call_id,
                                   &error) &&
    strappy_db_list_response_timeline(path,
                                      session_id,
                                      &timeline,
                                      &error) &&
    (timeline.count == 5U) &&
    timeline.records[0].has_cumulative_usage_cost &&
    harness_double_matches(timeline.records[0].cumulative_usage_cost, 0.001) &&
    timeline.records[1].has_cumulative_usage_cost &&
    timeline.records[1].is_error &&
    harness_double_matches(timeline.records[1].cumulative_usage_cost, 0.0035) &&
    (timeline.records[2].round_index == 1L) &&
    (timeline.records[2].attempt_index == 1L) &&
    harness_double_matches(timeline.records[2].cumulative_usage_cost, 0.0075) &&
    (strcmp(timeline.records[3].prompt_group_key, "group-two") == 0) &&
    (timeline.records[3].round_index == 0L) &&
    harness_double_matches(timeline.records[3].cumulative_usage_cost, 0.0075) &&
    harness_double_matches(timeline.records[4].cumulative_usage_cost, 0.008);

  if (!ok) {
    fprintf(stderr,
            "Cumulative session usage cost failed: %s\n",
            (error != NULL) ? error : "timeline total mismatch");
  }
  strappy_session_message_record_list_destroy(&timeline);
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_ledger(void)
{
  static const char *request_json =
    "{\"model\":\"test/model\",\"stream\":false,\"store\":false,"
    "\"instructions\":\"System\",\"input\":[{\"type\":\"message\","
    "\"role\":\"user\",\"content\":[{\"type\":\"input_text\","
    "\"text\":\"Hello\"}]}],\"max_output_tokens\":100,"
    "\"parallel_tool_calls\":true,\"reasoning\":{\"enabled\":true},"
    "\"tools\":[],\"tool_choice\":\"auto\","
    "\"provider\":{\"require_parameters\":true},"
    "\"temperature\":0.2,\"top_p\":0.9}";
  static const char *response_json =
    "{\"id\":\"resp-test\",\"object\":\"response\","
    "\"created_at\":1700000000,\"completed_at\":1700000001,"
    "\"model\":\"test/provider-model\",\"status\":\"completed\","
    "\"background\":false,\"error\":null,\"incomplete_details\":null,"
    "\"instructions\":\"System\",\"max_output_tokens\":100,"
    "\"metadata\":{\"trace\":\"one\"},\"parallel_tool_calls\":true,"
    "\"reasoning\":{\"effort\":\"medium\"},\"service_tier\":\"auto\","
    "\"store\":false,\"temperature\":0.2,\"tool_choice\":\"auto\","
    "\"tools\":[],\"top_p\":0.9,"
    "\"openrouter_metadata\":{\"provider\":\"test\"},"
    "\"output\":[{\"type\":\"reasoning\",\"id\":\"rs-test\","
    "\"status\":\"completed\",\"encrypted_content\":\"encrypted\","
    "\"format\":\"test-v1\",\"signature\":\"sig-test\","
    "\"summary\":[{\"type\":\"summary_text\",\"text\":\"Plan\"}]},"
    "{\"type\":\"function_call\",\"id\":\"fc-test\","
    "\"call_id\":\"call-test\",\"name\":\"database_list_info\","
    "\"namespace\":\"local\",\"arguments\":\"{}\","
    "\"status\":\"completed\"},"
    "{\"type\":\"openrouter:web_search\",\"id\":\"ws-test\","
    "\"status\":\"completed\",\"action\":{\"type\":\"search\","
    "\"query\":\"Strappy Cocoa\",\"sources\":[{\"type\":\"url\","
    "\"url\":\"https://example.com/search\"}]}},"
    "{\"type\":\"openrouter:web_fetch\",\"id\":\"wf-test\","
    "\"status\":\"completed\",\"url\":\"https://example.com/article\","
    "\"title\":\"Example Article\",\"content\":\"Fetched page body\","
    "\"httpStatus\":200},{\"type\":\"message\","
    "\"id\":\"msg-test\",\"role\":\"assistant\","
    "\"phase\":\"final_answer\",\"status\":\"completed\","
    "\"content\":[{\"type\":\"output_text\",\"text\":\"Done\","
    "\"annotations\":[],\"logprobs\":[]}]}],\"output_text\":\"Done\","
    "\"usage\":{\"input_tokens\":11,\"input_tokens_details\":{"
    "\"cached_tokens\":3},\"output_tokens\":22,"
    "\"output_tokens_details\":{\"reasoning_tokens\":7},"
    "\"total_tokens\":33,\"cost\":0.0012,\"cost_details\":{"
    "\"upstream_inference_cost\":0.001,"
    "\"upstream_inference_input_cost\":0.0004,"
    "\"upstream_inference_output_cost\":0.0006},\"is_byok\":false}}";
  char path[] = "/tmp/strappy-responses-XXXXXX";
  strappy_response_call_begin_input begin;
  strappy_response_call_finish_input finish;
  strappy_response_tool_execution_input execution;
  strappy_response_item_raw_record_list context;
  strappy_session_message_record_list timeline;
  sqlite3 *db;
  char *error;
  long long session_id;
  long long call_id;
  long long value;
  int fd;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create Responses harness database.");
  }
  close(fd);
  unlink(path);

  error = NULL;
  session_id = 0LL;
  call_id = 0LL;
  strappy_response_item_raw_record_list_init(&context);
  strappy_session_message_record_list_init(&timeline);
  ok = strappy_db_create_session(path, &session_id, &error);
  if (!ok) {
    fprintf(stderr, "Could not create harness session: %s\n", error);
    free(error);
    unlink(path);
    return 0;
  }

  memset(&begin, 0, sizeof(begin));
  begin.session_id = session_id;
  begin.prompt_group_key = "group-test";
  begin.request_kind = "user";
  begin.round_index = 0L;
  begin.attempt_index = 0L;
  begin.new_input_start_index = 0L;
  begin.request_method = "POST";
  begin.request_url = "https://openrouter.ai/api/v1/responses";
  begin.request_headers_json = "{\"Authorization\":\"Bearer [REDACTED]\"}";
  begin.request_json = request_json;
  ok = strappy_db_begin_response_call(path, &begin, &call_id, &error);
  if (!ok) {
    fprintf(stderr, "Could not begin harness call: %s\n", error);
    free(error);
    unlink(path);
    return 0;
  }

  ok = strappy_db_list_response_timeline(path,
                                         session_id,
                                         &timeline,
                                         &error) &&
    (timeline.count == 0U);
  strappy_session_message_record_list_destroy(&timeline);
  if (!ok) {
    fprintf(stderr,
            "Pending Responses call leaked into the UI timeline: %s\n",
            (error != NULL) ? error : "unexpected timeline row");
    free(error);
    unlink(path);
    return 0;
  }

  memset(&finish, 0, sizeof(finish));
  finish.call_id = call_id;
  finish.state = "completed";
  finish.output_is_canonical = 1;
  finish.http_status = 200L;
  finish.started_at_ms = 1000LL;
  finish.completed_at_ms = 1100LL;
  finish.request_bytes = (long long)strlen(request_json);
  finish.response_bytes = (long long)strlen(response_json);
  finish.total_seconds = 0.1;
  finish.effective_url = begin.request_url;
  finish.content_type = "application/json";
  finish.request_id = "req-test";
  finish.generation_id = "gen-test";
  finish.rate_limit_remaining = "9";
  finish.response_headers = "HTTP/2 200\r\nX-Request-Id: req-test\r\n";
  finish.response_json = response_json;
  ok = strappy_db_finish_response_call(path, &finish, &error);
  if (!ok) {
    fprintf(stderr, "Could not finish harness call: %s\n", error);
    free(error);
    unlink(path);
    return 0;
  }

  if (sqlite3_open(path, &db) != SQLITE_OK) {
    unlink(path);
    return harness_fail("Could not inspect Responses harness database.");
  }
  ok = harness_verify_call_columns(db, request_json, response_json) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM response_api_items;",
                      &value) && (value == 6LL) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM response_api_item_parts;",
                      &value) && (value == 3LL) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM response_api_items WHERE "
                      "type='function_call' AND call_id='call-test' AND "
                      "name='database_list_info' AND arguments='{}' AND "
                      "namespace='local';",
                      &value) && (value == 1LL) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM response_api_items WHERE "
                      "type='reasoning' AND format='test-v1' AND "
                      "signature='sig-test';",
                      &value) && (value == 1LL) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM response_api_items WHERE "
                      "type='openrouter:web_search' AND "
                      "action_json='{\"type\":\"search\","
                      "\"query\":\"Strappy Cocoa\",\"sources\":[{"
                      "\"type\":\"url\","
                      "\"url\":\"https://example.com/search\"}]}' ;",
                      &value) && (value == 1LL) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM response_api_items WHERE "
                      "type='openrouter:web_fetch' AND "
                      "url='https://example.com/article' AND "
                      "title='Example Article' AND http_status='200';",
                      &value) && (value == 1LL) &&
    harness_query_int(db, "PRAGMA user_version;", &value) && (value == 1LL);
  sqlite3_close(db);
  if (!ok) {
    unlink(path);
    return harness_fail("Responses ledger columns did not match the wire JSON.");
  }

  ok = strappy_db_list_canonical_response_items(path,
                                                session_id,
                                                &context,
                                                &error) &&
    (context.count == 6U);
  strappy_response_item_raw_record_list_destroy(&context);
  if (!ok) {
    fprintf(stderr, "Canonical Responses items failed: %s\n", error);
    free(error);
    unlink(path);
    return 0;
  }

  ok = strappy_db_list_response_timeline(path,
                                         session_id,
                                         &timeline,
                                         &error) &&
    (timeline.count == 7U) &&
    (strcmp(timeline.records[0].role, "user") == 0) &&
    (strcmp(timeline.records[0].direction, "request") == 0) &&
    (timeline.records[0].round_index == 0L) &&
    (timeline.records[0].attempt_index == 0L) &&
    (strcmp(timeline.records[1].role, "api_reasoning") == 0) &&
    (strcmp(timeline.records[1].direction, "response") == 0) &&
    (strcmp(timeline.records[2].role, "api_function_call") == 0) &&
    (strcmp(timeline.records[2].direction, "response") == 0) &&
    (strcmp(timeline.records[3].role, "api_item") == 0) &&
    (strcmp(timeline.records[3].kind,
            STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) == 0) &&
    (strcmp(timeline.records[3].response_item_action_json,
            "{\"type\":\"search\",\"query\":\"Strappy Cocoa\","
            "\"sources\":[{\"type\":\"url\","
            "\"url\":\"https://example.com/search\"}]}") == 0) &&
    (strcmp(timeline.records[4].role, "api_item") == 0) &&
    (strcmp(timeline.records[4].kind,
            STRAPPY_TOOL_OPENROUTER_WEB_FETCH) == 0) &&
    (strcmp(timeline.records[4].response_item_url,
            "https://example.com/article") == 0) &&
    (strcmp(timeline.records[4].response_item_title,
            "Example Article") == 0) &&
    (strcmp(timeline.records[4].response_item_status, "completed") == 0) &&
    (strcmp(timeline.records[4].response_item_http_status, "200") == 0) &&
    (strstr(timeline.records[4].response_item_title,
            "Fetched page body") == NULL) &&
    (strcmp(timeline.records[5].role, "api_call") == 0) &&
    (timeline.records[5].direction == NULL) &&
    (timeline.records[5].round_index == 0L) &&
    (timeline.records[5].attempt_index == 0L) &&
    (strcmp(timeline.records[6].role, "assistant") == 0) &&
    (strcmp(timeline.records[6].direction, "response") == 0) &&
    (strcmp(timeline.records[6].content, "Done") == 0);
  strappy_session_message_record_list_destroy(&timeline);
  if (!ok) {
    fprintf(stderr, "Responses timeline failed: %s\n", error);
    free(error);
    unlink(path);
    return 0;
  }

  memset(&execution, 0, sizeof(execution));
  execution.session_id = session_id;
  execution.response_call_id = call_id;
  execution.output_index = 1L;
  execution.call_id = "call-test";
  execution.tool_name = "database_list_info";
  execution.arguments_json = "{}";
  execution.status = "completed";
  execution.output_json = "{\"ok\":true}";
  execution.started_at_ms = 1200LL;
  execution.completed_at_ms = 1210LL;
  ok = strappy_db_save_response_tool_execution(path, &execution, &error);
  if (!ok) {
    fprintf(stderr, "Responses tool execution failed: %s\n", error);
    free(error);
    unlink(path);
    return 0;
  }

  ok = strappy_db_delete_session(path, session_id, &error);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM response_api_calls;",
                           &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM response_tool_executions;",
                        &value) && (value == 0LL);
    sqlite3_close(db);
  }
  free(error);
  unlink(path);
  if (!ok) {
    return harness_fail("Responses session delete left ledger rows behind.");
  }
  return 1;
}

int main(void)
{
  if (harness_test_request_surfaces() &&
      harness_test_ledger() &&
      harness_test_cumulative_session_usage_cost() &&
      harness_test_combined_tool_audit_once() &&
      harness_test_empty_audit_finalization_fails() &&
      harness_test_web_search_remains_outside_audit() &&
      harness_test_function_tool_continuation() &&
      harness_test_retry_attempt_ledger() &&
      harness_test_active_request_cancellation() &&
      harness_test_retry_after_clamp_and_cancellation()) {
    printf("responses_harness passed.\n");
    return 0;
  }
  fprintf(stderr, "responses_harness failed.\n");
  return 1;
}
