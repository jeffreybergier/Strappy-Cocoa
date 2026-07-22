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
#include "../shared/strappy_assistant_sets.h"
#include "../shared/strappy_config.h"
#include "../shared/strappy_db.h"
#include "../shared/strappy_prompt.h"
#include "../shared/strappy_quality_policy.h"
#include "../shared/strappy_responses.h"
#include "../shared/strappy_session.h"
#include "../shared/strappy_tools.h"

#define HARNESS_MEMORY_USER_FACT_REMEMBER_DESCRIPTION \
  "Call this tool to save a useful durable user fact for future prompts. " \
  "NEVER store secrets or sensitive information."

#define HARNESS_MEMORY_USER_FACT_READ_DESCRIPTION \
  "Call this tool to retrieve durable facts stored about the user."

#define HARNESS_DATABASE_LIST_INFO_DESCRIPTION \
  "Call this tool to view approved databases. Returns an object containing " \
  "a databases array with database_id, app_name, path, size_bytes, and " \
  "modified_at in Unix seconds. When no databases are approved, the array " \
  "is empty and guidance explains why."

#define HARNESS_DATABASE_QUERY_DESCRIPTION \
  "ALWAYS call this tool before finalizing when the request depends on " \
  "personal data. Run one read-only SQLite query against an approved " \
  "database; do not guess the user's data. Returns ordered column names and " \
  "positional rows. At most 100 rows and 64 columns are returned."
#define HARNESS_DATABASE_QUERY_DATABASE_ID_DESCRIPTION \
  "Approved database ID returned by database_list."
#define HARNESS_DATABASE_QUERY_SQL_DESCRIPTION \
  "One read-only SQLite SELECT or EXPLAIN query that returns columns. Select " \
  "only needed columns and use LIMIT when practical. Query sqlite_schema for " \
  "schema details. PRAGMA, writes, ATTACH, multiple statements, and bind " \
  "parameters are not allowed."

#define HARNESS_DATABASE_CONTEXT_READ_DESCRIPTION \
  "ALWAYS call this tool with the relevant approved database_id before " \
  "database_query. Returns remembered hints, table names, view names, and " \
  "guidance for exploring them."

#define HARNESS_SESSION_NAME_WRITE_DESCRIPTION \
  "ALWAYS call this tool before the final answer. Update the session with a " \
  "short, descriptive name for the user's latest prompt. A non-empty name " \
  "is required; null and empty values are invalid."

#define HARNESS_FONTAWESOME_CONFIRM_DESCRIPTION \
  "ALWAYS call this tool before the final answer. Set shortcodes to a " \
  "non-empty array of Font Awesome version 7 Free Markdown shortcodes, such " \
  "as [fa:heart] or [fa:brands:github], to confirm before using them. Returns " \
  "confirmed and not_found arrays. Null and empty arrays are invalid. Render " \
  "confirmed icons in answers with the returned Markdown shortcode syntax. " \
  "NEVER use unicode emoji."

#define HARNESS_MEMORY_DATABASE_HINT_REMEMBER_DESCRIPTION \
  "Call this tool to save useful durable query, schema, or access information " \
  "for an approved database. NEVER store user data, secrets, sensitive " \
  "information, guesses, or one-off query results."

#define HARNESS_DATETIME_FROM_ISO8601_DESCRIPTION \
  "ALWAYS call this tool when converting ISO 8601 datetimes to numeric " \
  "timestamps. Returns a JSON array of timestamp strings in input order."

#define HARNESS_UNICODE_EMOJI_ANSWER \
  "Initial answer. \xF0\x9F\x98\x80"

#define HARNESS_WORLD_PREFLIGHT_ASSISTANT_TEXT \
  "Strappy here! Your favorite world knowledge babe. Let me read any stored " \
  "memories before kicking things off!"
#define HARNESS_PERSONAL_PREFLIGHT_ASSISTANT_TEXT \
  "Strappy here! Your favorite database sleuth! Let me see what I have to " \
  "work with before kicking things off!"
#define HARNESS_CODING_PREFLIGHT_ASSISTANT_TEXT \
  "Strappy here! Your strap-on coding master. Let me check out the " \
  "environment we are working with before kicking things off!"

#define HARNESS_MEMORY_USER_FACT_FORGET_DESCRIPTION \
  "Call this tool to forget durable facts that are no longer correct or useful."

static int harness_fail(const char *message)
{
  fprintf(stderr, "%s\n", message);
  return 0;
}

static int harness_test_unicode_emoji_scan(void)
{
  static const struct {
    const char *text;
    int expected;
  } cases[] = {
    { NULL, 0 },
    { "", 0 },
    { "Plain ASCII [source](https://example.com).", 0 },
    { "# * 0123456789", 0 },
    { "Japanese: \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", 0 },
    { "Invalid UTF-8: \xF0\x28\x8C\x28", 0 },
    { "Copyright: \xC2\xA9", 1 },
    { "Heart: \xE2\x9D\xA4", 1 },
    { "Grinning: \xF0\x9F\x98\x80", 1 },
    { "Flag: \xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", 1 },
    { "Skin tone: \xF0\x9F\x8F\xBD", 1 },
    { "Keycap: 1\xEF\xB8\x8F\xE2\x83\xA3", 1 },
    { "[source](https://example.com) then \xF0\x9F\x98\x80", 1 }
  };
  size_t index;

  for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); index++) {
    int actual;

    actual = strappy_quality_policy_text_has_unicode_emoji(
      cases[index].text);
    if (actual != cases[index].expected) {
      fprintf(stderr,
              "Unicode emoji scan case %lu failed: expected=%d actual=%d\n",
              (unsigned long)index,
              cases[index].expected,
              actual);
      return 0;
    }
  }
  if ((strappy_quality_policy_find(
         STRAPPY_TOOL_MEMORY_SAVE) != NULL) ||
      (strappy_quality_policy_find(
         STRAPPY_TOOL_DATABASE_STUDY) != NULL)) {
    return harness_fail(
      "Optional memory tools unexpectedly remain answer-quality checks.");
  }
  return 1;
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

static int harness_has_tool_name(cJSON *tools, const char *expected)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (expected == NULL)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *name;

    name = cJSON_GetObjectItem(tool, "name");
    if (cJSON_IsString(name) && (name->valuestring != NULL) &&
        (strcmp(name->valuestring, expected) == 0)) {
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

static int harness_database_context_parameters_match_contract(
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
    cJSON *database_id;
    cJSON *type;
    cJSON *description;
    cJSON *min_length;
    cJSON *max_length;
    cJSON *required;
    cJSON *required_item;
    cJSON *additional_properties;

    name = cJSON_GetObjectItem(tool, "name");
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, STRAPPY_TOOL_DATABASE_CONTEXT) != 0)) {
      continue;
    }
    parameters = cJSON_GetObjectItem(tool, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "properties") : NULL;
    database_id = cJSON_IsObject(properties) ?
      cJSON_GetObjectItem(properties, "database_id") : NULL;
    type = cJSON_IsObject(database_id) ?
      cJSON_GetObjectItem(database_id, "type") : NULL;
    description = cJSON_IsObject(database_id) ?
      cJSON_GetObjectItem(database_id, "description") : NULL;
    min_length = cJSON_IsObject(database_id) ?
      cJSON_GetObjectItem(database_id, "minLength") : NULL;
    max_length = cJSON_IsObject(database_id) ?
      cJSON_GetObjectItem(database_id, "maxLength") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "required") : NULL;
    required_item = cJSON_IsArray(required) ?
      cJSON_GetArrayItem(required, 0) : NULL;
    additional_properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "additionalProperties") : NULL;
    return cJSON_IsObject(properties) &&
      (cJSON_GetArraySize(properties) == 1) &&
      cJSON_IsArray(required) && (cJSON_GetArraySize(required) == 1) &&
      cJSON_IsString(required_item) &&
      (required_item->valuestring != NULL) &&
      (strcmp(required_item->valuestring, "database_id") == 0) &&
      (cJSON_GetObjectItem(parameters, "minProperties") == NULL) &&
      cJSON_IsFalse(additional_properties) &&
      cJSON_IsString(type) && (type->valuestring != NULL) &&
      (strcmp(type->valuestring, "string") == 0) &&
      cJSON_IsString(description) && (description->valuestring != NULL) &&
      (strcmp(description->valuestring,
              "Approved database ID returned by database_list.") == 0) &&
      cJSON_IsNumber(min_length) && (min_length->valuedouble == 1.0) &&
      cJSON_IsNumber(max_length) && (max_length->valuedouble == 128.0);
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

static int harness_database_query_parameters_match_contract(cJSON *tools)
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
    cJSON *database_id_description;
    cJSON *database_id_min_length;
    cJSON *database_id_max_length;
    cJSON *sql_type;
    cJSON *sql_description;
    cJSON *sql_min_length;
    cJSON *sql_max_length;
    cJSON *sql_pattern;
    cJSON *required;
    cJSON *additional_properties;

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
    database_id_description = cJSON_IsObject(database_id) ?
      cJSON_GetObjectItem(database_id, "description") : NULL;
    database_id_min_length = cJSON_IsObject(database_id) ?
      cJSON_GetObjectItem(database_id, "minLength") : NULL;
    database_id_max_length = cJSON_IsObject(database_id) ?
      cJSON_GetObjectItem(database_id, "maxLength") : NULL;
    sql_type = cJSON_IsObject(sql) ? cJSON_GetObjectItem(sql, "type") : NULL;
    sql_description = cJSON_IsObject(sql) ?
      cJSON_GetObjectItem(sql, "description") : NULL;
    sql_min_length = cJSON_IsObject(sql) ?
      cJSON_GetObjectItem(sql, "minLength") : NULL;
    sql_max_length = cJSON_IsObject(sql) ?
      cJSON_GetObjectItem(sql, "maxLength") : NULL;
    sql_pattern = cJSON_IsObject(sql) ?
      cJSON_GetObjectItem(sql, "pattern") : NULL;
    required = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "required") : NULL;
    additional_properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "additionalProperties") : NULL;
    return cJSON_IsObject(properties) &&
      (cJSON_GetArraySize(properties) == 2) &&
      cJSON_IsString(database_id_type) &&
      (database_id_type->valuestring != NULL) &&
      (strcmp(database_id_type->valuestring, "string") == 0) &&
      cJSON_IsString(database_id_description) &&
      (database_id_description->valuestring != NULL) &&
      (strcmp(database_id_description->valuestring,
              HARNESS_DATABASE_QUERY_DATABASE_ID_DESCRIPTION) == 0) &&
      cJSON_IsNumber(database_id_min_length) &&
      (database_id_min_length->valuedouble == 1.0) &&
      cJSON_IsNumber(database_id_max_length) &&
      (database_id_max_length->valuedouble == 64.0) &&
      cJSON_IsString(sql_type) && (sql_type->valuestring != NULL) &&
      (strcmp(sql_type->valuestring, "string") == 0) &&
      cJSON_IsString(sql_description) &&
      (sql_description->valuestring != NULL) &&
      (strcmp(sql_description->valuestring,
              HARNESS_DATABASE_QUERY_SQL_DESCRIPTION) == 0) &&
      cJSON_IsNumber(sql_min_length) &&
      (sql_min_length->valuedouble == 1.0) &&
      cJSON_IsNumber(sql_max_length) &&
      (sql_max_length->valuedouble == 8192.0) &&
      cJSON_IsString(sql_pattern) && (sql_pattern->valuestring != NULL) &&
      (strcmp(sql_pattern->valuestring, "\\S") == 0) &&
      cJSON_IsArray(required) && (cJSON_GetArraySize(required) == 2) &&
      harness_array_contains_string(required, "database_id") &&
      harness_array_contains_string(required, "sql") &&
      cJSON_IsFalse(additional_properties);
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

static cJSON *harness_tool_parameter_schema(cJSON *tools,
                                            const char *tool_name,
                                            const char *parameter_name)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (tool_name == NULL) ||
      (parameter_name == NULL)) {
    return NULL;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *name;
    cJSON *parameters;
    cJSON *properties;

    name = cJSON_GetObjectItem(tool, "name");
    if (!cJSON_IsString(name) || (name->valuestring == NULL) ||
        (strcmp(name->valuestring, tool_name) != 0)) {
      continue;
    }
    parameters = cJSON_GetObjectItem(tool, "parameters");
    properties = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItem(parameters, "properties") : NULL;
    return cJSON_IsObject(properties) ?
      cJSON_GetObjectItem(properties, parameter_name) : NULL;
  }
  return NULL;
}

static int harness_fontawesome_parameter_constraints_match(cJSON *tools)
{
  cJSON *query;
  cJSON *shortcodes;
  cJSON *items;
  cJSON *value;

  query = harness_tool_parameter_schema(
    tools,
    STRAPPY_TOOL_FONTAWESOME_SEARCH,
    "query");
  shortcodes = harness_tool_parameter_schema(
    tools,
    STRAPPY_TOOL_FONTAWESOME_CONFIRM,
    "shortcodes");
  items = cJSON_IsObject(shortcodes) ?
    cJSON_GetObjectItem(shortcodes, "items") : NULL;
  if (!cJSON_IsObject(query) || !cJSON_IsObject(shortcodes) ||
      !cJSON_IsObject(items)) {
    return 0;
  }

  value = cJSON_GetObjectItem(query, "minLength");
  if (!cJSON_IsNumber(value) || (value->valueint != 1)) {
    return 0;
  }
  value = cJSON_GetObjectItem(query, "maxLength");
  if (!cJSON_IsNumber(value) || (value->valueint != 128)) {
    return 0;
  }
  value = cJSON_GetObjectItem(query, "pattern");
  if (!cJSON_IsString(value) || (value->valuestring == NULL) ||
      (strcmp(value->valuestring, "\\S") != 0)) {
    return 0;
  }
  value = cJSON_GetObjectItem(shortcodes, "maxItems");
  if (!cJSON_IsNumber(value) || (value->valueint != 32)) {
    return 0;
  }
  value = cJSON_GetObjectItem(items, "maxLength");
  return cJSON_IsNumber(value) && (value->valueint == 96);
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

static int harness_server_tool_has_engine(cJSON *tools,
                                          const char *tool_type,
                                          const char *engine_name)
{
  cJSON *tool;

  if (!cJSON_IsArray(tools) || (tool_type == NULL) ||
      (engine_name == NULL)) {
    return 0;
  }
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    cJSON *type;
    cJSON *parameters;
    cJSON *engine;

    type = cJSON_GetObjectItem(tool, "type");
    if (cJSON_IsString(type) && (type->valuestring != NULL) &&
        (strcmp(type->valuestring, tool_type) == 0)) {
      parameters = cJSON_GetObjectItemCaseSensitive(tool, "parameters");
      engine = cJSON_IsObject(parameters) ?
        cJSON_GetObjectItemCaseSensitive(parameters, "engine") : NULL;
      return (cJSON_GetArraySize(tool) == 2) && cJSON_IsString(engine) &&
        (engine->valuestring != NULL) &&
        (strcmp(engine->valuestring, engine_name) == 0);
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
    STRAPPY_WEB_PROVIDER_AUTO,
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
    (strcmp(name->valuestring, "database_list") == 0) &&
    cJSON_IsString(description) && (description->valuestring != NULL) &&
    (strcmp(description->valuestring,
            HARNESS_DATABASE_LIST_INFO_DESCRIPTION) == 0) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_DATABASE_QUERY,
      HARNESS_DATABASE_QUERY_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_DATETIME_FROM_ISO8601,
      HARNESS_DATETIME_FROM_ISO8601_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_MEMORY_READ,
      HARNESS_MEMORY_USER_FACT_READ_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_MEMORY_SAVE,
      HARNESS_MEMORY_USER_FACT_REMEMBER_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_MEMORY_DELETE,
      HARNESS_MEMORY_USER_FACT_FORGET_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_DATABASE_STUDY,
      HARNESS_MEMORY_DATABASE_HINT_REMEMBER_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_DATABASE_CONTEXT,
      HARNESS_DATABASE_CONTEXT_READ_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_SESSION_RENAME,
      HARNESS_SESSION_NAME_WRITE_DESCRIPTION) &&
    harness_tool_description_equals(
      tools,
      STRAPPY_TOOL_FONTAWESOME_CONFIRM,
      HARNESS_FONTAWESOME_CONFIRM_DESCRIPTION) &&
    harness_database_query_parameters_match_contract(tools) &&
    harness_tool_has_required_nonempty_string_array_parameter(
      tools,
      STRAPPY_TOOL_DATETIME_TO_ISO8601,
      "timestamps") &&
    harness_tool_has_required_string_parameter(
      tools,
      STRAPPY_TOOL_DATETIME_TO_ISO8601,
      "unit") &&
    harness_tool_has_required_nonempty_string_array_parameter(
      tools,
      STRAPPY_TOOL_DATETIME_FROM_ISO8601,
      "datetimes") &&
    harness_tool_has_required_string_parameter(
      tools,
      STRAPPY_TOOL_DATETIME_FROM_ISO8601,
      "unit") &&
    harness_tool_has_required_string_parameter(
      tools,
      STRAPPY_TOOL_SESSION_RENAME,
      "name") &&
    harness_tool_has_required_nonempty_string_array_parameter(
      tools,
      STRAPPY_TOOL_FONTAWESOME_CONFIRM,
      "shortcodes") &&
    harness_tool_has_required_string_parameter(
      tools,
      STRAPPY_TOOL_FONTAWESOME_SEARCH,
      "query") &&
    harness_fontawesome_parameter_constraints_match(tools) &&
    harness_database_context_parameters_match_contract(tools) &&
    harness_tool_has_required_string_parameter(
      tools,
      STRAPPY_TOOL_MEMORY_SAVE,
      "fact") &&
    harness_tool_has_required_string_parameter(
      tools,
      STRAPPY_TOOL_DATABASE_STUDY,
      "database_id") &&
    harness_tool_has_required_string_parameter(
      tools,
      STRAPPY_TOOL_DATABASE_STUDY,
      "hint") &&
    harness_tools_hide_local_display_metadata(tools) &&
    harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
    harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_FETCH) &&
    harness_server_tool_has_engine(
      tools,
      STRAPPY_TOOL_OPENROUTER_WEB_SEARCH,
      "auto") &&
    harness_server_tool_has_engine(
      tools,
      STRAPPY_TOOL_OPENROUTER_WEB_FETCH,
      "auto");
  cJSON_Delete(tools);
  free(error);
  if (!ok) {
    return harness_fail("Responses tool schemas were not flattened.");
  }

  error = NULL;
  tools_json = strappy_tools_responses_request_json(
    "../shared/Resources",
    STRAPPY_WEB_PROVIDER_NONE,
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
  long long answer_quality_count;
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
  long long answer_quality_count;
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
  answer_quality_count = 0LL;
  if ((event->type != STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED) ||
      (event->kind == NULL) ||
      (strcmp(event->kind, "response_api_call") != 0) ||
      (event->message_key == NULL) ||
      (strncmp(event->message_key, "response-call-", 14U) != 0) ||
      (event->status_kind == NULL) ||
      !opened ||
      !harness_query_int(db,
                         "SELECT COUNT(*) FROM http_attempts;",
                         &call_count) ||
      !harness_query_int(db,
                         "SELECT COUNT(*) FROM http_attempts "
                         "WHERE state IN ('pending','running');",
                         &pending_count) ||
      !harness_query_int(db,
                         "SELECT COUNT(*) FROM answer_quality_audits;",
                         &answer_quality_count) ||
      (call_count != (recorder->count + 1LL)) ||
      (pending_count != 0LL)) {
    recorder->valid = 0;
  }
  recorder->answer_quality_count = answer_quality_count;
  if (db != NULL) {
    sqlite3_close(db);
  }
  recorder->count++;
  return 1;
}

#define HARNESS_HTTP_MAX_REQUEST_BYTES (4U * 1024U * 1024U)

typedef enum harness_responses_server_scenario {
  HARNESS_RESPONSES_SERVER_ANSWER_QUALITY = 1,
  HARNESS_RESPONSES_SERVER_SERVER_TOOL = 2,
  HARNESS_RESPONSES_SERVER_FUNCTION_TOOL = 3,
  HARNESS_RESPONSES_SERVER_RETRY = 4,
  HARNESS_RESPONSES_SERVER_RETRY_AFTER = 5,
  HARNESS_RESPONSES_SERVER_SLOW = 6,
  HARNESS_RESPONSES_SERVER_EMPTY_ANSWER = 7,
  HARNESS_RESPONSES_SERVER_EMPTY_ANSWER_AFTER_TOOLS = 8,
  HARNESS_RESPONSES_SERVER_WEB_REFERENCE_VALID = 9,
  HARNESS_RESPONSES_SERVER_WORLD_KNOWLEDGE = 10,
  HARNESS_RESPONSES_SERVER_BASH_CANCELLATION = 11,
  HARNESS_RESPONSES_SERVER_CODING_BASH_DISABLED = 12,
  HARNESS_RESPONSES_SERVER_BASH_OUTPUT = 13,
  HARNESS_RESPONSES_SERVER_FILE_MUTATION = 14,
  HARNESS_RESPONSES_SERVER_PREFLIGHT_FIRST_PROMPT_ONLY = 15
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

static const char *harness_object_string(cJSON *parent, const char *key)
{
  cJSON *value;

  value = cJSON_IsObject(parent) ?
    cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
  return cJSON_IsString(value) && (value->valuestring != NULL) &&
    (value->valuestring[0] != '\0') ? value->valuestring : NULL;
}

static const char *harness_object_text(cJSON *parent, const char *key)
{
  cJSON *value;

  value = cJSON_IsObject(parent) ?
    cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
  return cJSON_IsString(value) && (value->valuestring != NULL) ?
    value->valuestring : NULL;
}

static int harness_instructions_include_resource_sections(
  const char *instructions)
{
  static const char *section_names[] = {
    "tools",
    "audit",
    "goal",
    "invariant"
  };
  char *resource_text;
  char *error;
  cJSON *root;
  cJSON *sections;
  size_t index;
  int result;

  if ((instructions == NULL) || (instructions[0] == '\0')) {
    return 0;
  }
  error = NULL;
  resource_text = strappy_prompt_render_resource(
    "../shared/Resources",
    STRAPPY_SYSTEM_PROMPT_RESOURCE_NAME,
    &error);
  free(error);
  if (resource_text == NULL) {
    return 0;
  }
  root = cJSON_Parse(resource_text);
  free(resource_text);
  sections = cJSON_IsObject(root) ?
    cJSON_GetObjectItemCaseSensitive(root, "sections") : NULL;
  result = cJSON_IsObject(sections);
  for (index = 0U;
       result && (index < (sizeof(section_names) / sizeof(section_names[0])));
       index++) {
    cJSON *section;
    const char *heading;
    const char *instruction;
    const char *footer;

    section = cJSON_GetObjectItemCaseSensitive(sections,
                                               section_names[index]);
    heading = harness_object_string(section, "heading");
    instruction = harness_object_text(section, "instruction");
    footer = harness_object_text(section, "footer");
    result = (heading != NULL) && (instruction != NULL) &&
      (footer != NULL) && (strstr(instructions, heading) != NULL) &&
      ((instruction[0] == '\0') ||
       (strstr(instructions, instruction) != NULL)) &&
      ((footer[0] == '\0') || (strstr(instructions, footer) != NULL));
  }
  cJSON_Delete(root);
  return result;
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
                                           const char *expected_arguments,
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
    ((expected_arguments == NULL) ||
     (strcmp(arguments->valuestring, expected_arguments) == 0));
}

static int harness_coding_preflight_bash_arguments_are_valid(
  const char *arguments_json)
{
  static const char *required_fragments[] = {
    "uname -a",
    "SystemVersion.plist",
    "dpkg --print-architecture",
    "id",
    "PWD=$PWD",
    "HOME=$HOME",
    "SHELL=$SHELL",
    "PATH=$PATH",
    "df -h .",
    "/usr/include",
    "which clang",
    "which gcc",
    "command -v cc",
    "command -v make",
    "command -v ld",
    "command -v ar",
    "command -v git",
    "command -v ldid",
    "command -v dpkg-deb",
    "command -v curl",
    "command -v sqlite3",
    "clang --version",
    "gcc --version",
    "make --version",
    "git --version",
    " && pwd && ls -al"
  };
  cJSON *root;
  cJSON *command;
  size_t index;
  int ok;

  root = cJSON_Parse((arguments_json != NULL) ? arguments_json : "");
  command = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "command") : NULL;
  ok = cJSON_IsString(command) && (command->valuestring != NULL) &&
    (root->child == command) && (command->next == NULL) &&
    (strstr(command->valuestring, "python") == NULL);
  for (index = 0U;
       ok && (index < (sizeof(required_fragments) /
                       sizeof(required_fragments[0])));
       index++) {
    ok = (strstr(command->valuestring, required_fragments[index]) != NULL);
  }
  cJSON_Delete(root);
  return ok;
}

static int harness_preflight_output_matches(cJSON *item,
                                            cJSON *call,
                                            int expect_array)
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
  ok = expect_array ? cJSON_IsArray(result) : cJSON_IsObject(result);
  cJSON_Delete(result);
  return ok;
}

static int harness_preflight_output_equals(cJSON *item,
                                           cJSON *call,
                                           const char *expected_output)
{
  cJSON *output;

  if ((expected_output == NULL) ||
      !harness_preflight_output_matches(item, call, 1)) {
    return 0;
  }
  output = cJSON_GetObjectItem(item, "output");
  return (output != NULL) && cJSON_IsString(output) &&
    (output->valuestring != NULL) &&
    (strcmp(output->valuestring, expected_output) == 0);
}

static int harness_preflight_bash_output_is_valid(cJSON *item, cJSON *call)
{
  cJSON *output;
  cJSON *result;
  cJSON *command_output;
  cJSON *output_truncated;
  int ok;

  if (!harness_preflight_output_matches(item, call, 0)) {
    return 0;
  }
  output = cJSON_GetObjectItem(item, "output");
  result = cJSON_Parse(output->valuestring);
  command_output = cJSON_GetObjectItem(result, "output");
  output_truncated = cJSON_GetObjectItem(result, "output_truncated");
  ok = cJSON_IsString(command_output) &&
    (command_output->valuestring != NULL) &&
    (command_output->valuestring[0] != '\0') &&
    cJSON_IsFalse(output_truncated);
  cJSON_Delete(result);
  return ok;
}

static int harness_preflight_input_is_valid(cJSON *input,
                                            const char *prompt_group)
{
  cJSON *memory_call;
  cJSON *database_call;
  const char *assistant_text;

  assistant_text = harness_message_text(cJSON_GetArrayItem(input, 1));
  memory_call = cJSON_GetArrayItem(input, 2);
  database_call = cJSON_GetArrayItem(input, 3);
  return harness_message_role_is(cJSON_GetArrayItem(input, 1), "assistant") &&
    (assistant_text != NULL) &&
    (strcmp(assistant_text, HARNESS_PERSONAL_PREFLIGHT_ASSISTANT_TEXT) == 0) &&
    harness_preflight_call_is_valid(memory_call,
                                    "memory_read",
                                    "{}",
                                    "fc_pf_0_",
                                    "call_pf_0_",
                                    prompt_group) &&
    harness_preflight_call_is_valid(database_call,
                                    "database_list",
                                    "{}",
                                    "fc_pf_1_",
                                    "call_pf_1_",
                                    prompt_group) &&
    harness_preflight_output_matches(cJSON_GetArrayItem(input, 4),
                                     memory_call,
                                     1) &&
    harness_preflight_output_matches(cJSON_GetArrayItem(input, 5),
                                     database_call,
                                     0);
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
  cJSON *instructions;
  cJSON *session_key;
  cJSON *metadata;
  cJSON *prompt_group;
  cJSON *input;
  cJSON *tools;
  cJSON *first_tool;
  cJSON *function_wrapper;
  int input_count;
  int has_web_search;
  int has_web_fetch;
  int has_web_reference_key;
  int has_web_reference_instruction;
  const char *text;

  stream = cJSON_GetObjectItem(root, "stream");
  store = cJSON_GetObjectItem(root, "store");
  instructions = cJSON_GetObjectItem(root, "instructions");
  session_key = cJSON_GetObjectItem(root, "session_id");
  metadata = cJSON_GetObjectItem(root, "metadata");
  prompt_group = cJSON_GetObjectItem(metadata, "strappy_prompt_group_key");
  input = cJSON_GetObjectItem(root, "input");
  tools = cJSON_GetObjectItem(root, "tools");
  first_tool = cJSON_GetArrayItem(tools, 0);
  function_wrapper = cJSON_GetObjectItem(first_tool, "function");
  input_count = cJSON_IsArray(input) ? cJSON_GetArraySize(input) : 0;
  has_web_search = harness_has_tool_type(
    tools,
    STRAPPY_TOOL_OPENROUTER_WEB_SEARCH);
  has_web_fetch = harness_has_tool_type(
    tools,
    STRAPPY_TOOL_OPENROUTER_WEB_FETCH);
  has_web_reference_key = cJSON_IsString(instructions) &&
    (instructions->valuestring != NULL) &&
    (strstr(instructions->valuestring, "`web_reference`") != NULL);
  has_web_reference_instruction = cJSON_IsString(instructions) &&
    (instructions->valuestring != NULL) &&
    (strstr(instructions->valuestring,
            "titled inline Markdown HTTP or HTTPS link") != NULL);
  text = harness_message_text(cJSON_GetArrayItem(input, 0));
  if (!cJSON_IsFalse(stream) || !cJSON_IsFalse(store) ||
      !cJSON_IsString(instructions) ||
      (instructions->valuestring == NULL) ||
      !harness_instructions_include_resource_sections(
        instructions->valuestring) ||
      (strstr(instructions->valuestring,
              "`unicode_emoji_absent`") == NULL) ||
      (strstr(instructions->valuestring,
              "NEVER use unicode emoji in your answer") == NULL) ||
      (has_web_search != has_web_fetch) ||
      (has_web_search != has_web_reference_key) ||
      (has_web_search != has_web_reference_instruction) ||
      !cJSON_IsString(session_key) || (session_key->valuestring == NULL) ||
      !cJSON_IsString(prompt_group) || (prompt_group->valuestring == NULL) ||
      !cJSON_IsArray(input) || (input_count != 6) ||
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

static int harness_world_knowledge_tools_are_valid(cJSON *tools)
{
  return cJSON_IsArray(tools) && (cJSON_GetArraySize(tools) == 10) &&
    harness_server_tool_has_engine(
      tools,
      STRAPPY_TOOL_OPENROUTER_WEB_SEARCH,
      "parallel") &&
    harness_server_tool_has_engine(
      tools,
      STRAPPY_TOOL_OPENROUTER_WEB_FETCH,
      "parallel") &&
    harness_has_tool_name(tools, STRAPPY_TOOL_DATETIME_TO_ISO8601) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_DATETIME_FROM_ISO8601) &&
    harness_has_tool_name(
      tools,
      STRAPPY_TOOL_FONTAWESOME_SEARCH) &&
    harness_has_tool_name(
      tools,
      STRAPPY_TOOL_FONTAWESOME_CONFIRM) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_MEMORY_READ) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_MEMORY_SAVE) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_MEMORY_DELETE) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_SESSION_RENAME) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_LIST) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_QUERY) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_CONTEXT) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_STUDY) &&
    harness_tools_hide_local_display_metadata(tools);
}

static int harness_world_knowledge_request_is_valid(
  cJSON *root,
  const char *expected_prompt,
  char **session_key_out,
  char **prompt_group_out)
{
  cJSON *stream;
  cJSON *store;
  cJSON *instructions;
  cJSON *session_key;
  cJSON *metadata;
  cJSON *prompt_group;
  cJSON *input;
  cJSON *memory_call;
  cJSON *tools;
  const char *text;

  stream = cJSON_GetObjectItem(root, "stream");
  store = cJSON_GetObjectItem(root, "store");
  instructions = cJSON_GetObjectItem(root, "instructions");
  session_key = cJSON_GetObjectItem(root, "session_id");
  metadata = cJSON_GetObjectItem(root, "metadata");
  prompt_group = cJSON_IsObject(metadata) ?
    cJSON_GetObjectItem(metadata, "strappy_prompt_group_key") : NULL;
  input = cJSON_GetObjectItem(root, "input");
  memory_call = cJSON_GetArrayItem(input, 2);
  tools = cJSON_GetObjectItem(root, "tools");
  text = harness_message_text(cJSON_GetArrayItem(input, 0));
  if (!cJSON_IsFalse(stream) || !cJSON_IsFalse(store) ||
      !cJSON_IsString(instructions) ||
      (instructions->valuestring == NULL) ||
      (strstr(instructions->valuestring,
              "You are an expert world-knowledge assistant.") == NULL) ||
      (strstr(instructions->valuestring,
              "personal databases in this assistant set") == NULL) ||
      (strstr(instructions->valuestring,
              "You are an expert personal assistant.") != NULL) ||
      !cJSON_IsString(session_key) || (session_key->valuestring == NULL) ||
      !cJSON_IsString(prompt_group) || (prompt_group->valuestring == NULL) ||
      !cJSON_IsArray(input) || (cJSON_GetArraySize(input) != 4) ||
      !harness_message_role_is(cJSON_GetArrayItem(input, 0), "user") ||
      !harness_message_role_is(cJSON_GetArrayItem(input, 1), "assistant") ||
      (harness_message_text(cJSON_GetArrayItem(input, 1)) == NULL) ||
      (strcmp(harness_message_text(cJSON_GetArrayItem(input, 1)),
              HARNESS_WORLD_PREFLIGHT_ASSISTANT_TEXT) != 0) ||
      !harness_preflight_call_is_valid(memory_call,
                                       STRAPPY_TOOL_MEMORY_READ,
                                       "{}",
                                       "fc_pf_0_",
                                       "call_pf_0_",
                                       prompt_group->valuestring) ||
      !harness_preflight_output_equals(cJSON_GetArrayItem(input, 3),
                                       memory_call,
                                       "[]") ||
      (text == NULL) || (strcmp(text, expected_prompt) != 0) ||
      !harness_world_knowledge_tools_are_valid(tools)) {
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

static int harness_world_followup_request_is_valid(
  cJSON *root,
  const char *session_key,
  const char *first_prompt_group)
{
  cJSON *request_session;
  cJSON *metadata;
  cJSON *prompt_group;
  cJSON *input;
  cJSON *memory_call;
  const char *first_prompt;
  const char *announcement;
  const char *first_answer;
  const char *second_prompt;

  request_session = cJSON_GetObjectItem(root, "session_id");
  metadata = cJSON_GetObjectItem(root, "metadata");
  prompt_group = cJSON_IsObject(metadata) ?
    cJSON_GetObjectItem(metadata, "strappy_prompt_group_key") : NULL;
  input = cJSON_GetObjectItem(root, "input");
  memory_call = cJSON_GetArrayItem(input, 2);
  first_prompt = harness_message_text(cJSON_GetArrayItem(input, 0));
  announcement = harness_message_text(cJSON_GetArrayItem(input, 1));
  first_answer = harness_message_text(cJSON_GetArrayItem(input, 4));
  second_prompt = harness_message_text(cJSON_GetArrayItem(input, 5));
  return cJSON_IsString(request_session) &&
    (request_session->valuestring != NULL) &&
    (strcmp(request_session->valuestring, session_key) == 0) &&
    cJSON_IsString(prompt_group) && (prompt_group->valuestring != NULL) &&
    (strcmp(prompt_group->valuestring, first_prompt_group) != 0) &&
    cJSON_IsArray(input) && (cJSON_GetArraySize(input) == 6) &&
    harness_message_role_is(cJSON_GetArrayItem(input, 0), "user") &&
    (first_prompt != NULL) && (strcmp(first_prompt, "First prompt") == 0) &&
    harness_message_role_is(cJSON_GetArrayItem(input, 1), "assistant") &&
    (announcement != NULL) &&
    (strcmp(announcement, HARNESS_WORLD_PREFLIGHT_ASSISTANT_TEXT) == 0) &&
    harness_preflight_call_is_valid(memory_call,
                                    STRAPPY_TOOL_MEMORY_READ,
                                    "{}",
                                    "fc_pf_0_",
                                    "call_pf_0_",
                                    first_prompt_group) &&
    harness_preflight_output_equals(cJSON_GetArrayItem(input, 3),
                                    memory_call,
                                    "[]") &&
    harness_message_role_is(cJSON_GetArrayItem(input, 4), "assistant") &&
    (first_answer != NULL) &&
    (strcmp(first_answer, "First round answer.") == 0) &&
    harness_message_role_is(cJSON_GetArrayItem(input, 5), "user") &&
    (second_prompt != NULL) &&
    (strcmp(second_prompt, "Second prompt") == 0) &&
    harness_world_knowledge_tools_are_valid(cJSON_GetObjectItem(root,
                                                                "tools"));
}

static int harness_coding_assistant_request_is_valid(
  cJSON *root,
  const char *expected_prompt,
  int bash_enabled)
{
  cJSON *instructions;
  cJSON *metadata;
  cJSON *prompt_group;
  cJSON *input;
  cJSON *memory_call;
  cJSON *bash_call;
  cJSON *bash_arguments;
  cJSON *tools;
  const char *text;

  instructions = cJSON_GetObjectItem(root, "instructions");
  metadata = cJSON_GetObjectItem(root, "metadata");
  prompt_group = cJSON_IsObject(metadata) ?
    cJSON_GetObjectItem(metadata, "strappy_prompt_group_key") : NULL;
  input = cJSON_GetObjectItem(root, "input");
  memory_call = cJSON_GetArrayItem(input, 2);
  bash_call = cJSON_GetArrayItem(input, 3);
  bash_arguments = cJSON_IsObject(bash_call) ?
    cJSON_GetObjectItem(bash_call, "arguments") : NULL;
  tools = cJSON_GetObjectItem(root, "tools");
  text = harness_message_text(cJSON_GetArrayItem(input, 0));
  return cJSON_IsString(instructions) &&
    (instructions->valuestring != NULL) &&
    (strstr(instructions->valuestring,
            "You are an expert coding assistant.") != NULL) &&
    cJSON_IsString(prompt_group) && (prompt_group->valuestring != NULL) &&
    cJSON_IsArray(input) && (cJSON_GetArraySize(input) == 6) &&
    harness_message_role_is(cJSON_GetArrayItem(input, 0), "user") &&
    harness_message_role_is(cJSON_GetArrayItem(input, 1), "assistant") &&
    (harness_message_text(cJSON_GetArrayItem(input, 1)) != NULL) &&
    (strcmp(harness_message_text(cJSON_GetArrayItem(input, 1)),
            HARNESS_CODING_PREFLIGHT_ASSISTANT_TEXT) == 0) &&
    (text != NULL) && (strcmp(text, expected_prompt) == 0) &&
    harness_preflight_call_is_valid(memory_call,
                                    STRAPPY_TOOL_MEMORY_READ,
                                    "{}",
                                    "fc_pf_0_",
                                    "call_pf_0_",
                                    prompt_group->valuestring) &&
    harness_preflight_call_is_valid(bash_call,
                                    STRAPPY_TOOL_BASH,
                                    NULL,
                                    "fc_pf_1_",
                                    "call_pf_1_",
                                    prompt_group->valuestring) &&
    harness_coding_preflight_bash_arguments_are_valid(
      cJSON_IsString(bash_arguments) ? bash_arguments->valuestring : NULL) &&
    harness_preflight_output_matches(cJSON_GetArrayItem(input, 4),
                                     memory_call,
                                     1) &&
    harness_preflight_bash_output_is_valid(cJSON_GetArrayItem(input, 5),
                                           bash_call) &&
    cJSON_IsArray(tools) &&
    (cJSON_GetArraySize(tools) == (bash_enabled ? 12 : 11)) &&
    (harness_has_tool_name(tools, STRAPPY_TOOL_BASH) ==
      (bash_enabled ? 1 : 0)) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_FILE_READ) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_FILE_WRITE) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_FILE_EDIT) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_QUERY) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_FETCH) &&
    harness_tools_hide_local_display_metadata(tools);
}

static int harness_disabled_web_search_request_is_valid(cJSON *root)
{
  cJSON *tools;
  cJSON *provider;
  cJSON *require_parameters;

  tools = cJSON_GetObjectItem(root, "tools");
  provider = cJSON_GetObjectItem(root, "provider");
  require_parameters = cJSON_IsObject(provider) ?
    cJSON_GetObjectItem(provider, "require_parameters") : NULL;
  return cJSON_IsArray(tools) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_FETCH) &&
    (require_parameters == NULL);
}

static int harness_named_function_output_request_is_valid(
  cJSON *root,
  const char *session_key,
  const char *prompt_group,
  const char *expected_function_name,
  const char *expected_call_id,
  const char *expected_output)
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
    ((expected_output != NULL) ?
      (strcmp(output->valuestring, expected_output) == 0) :
      (output->valuestring[0] != '\0'));
}

static int harness_required_function_outputs_request_is_valid(
  cJSON *root,
  const char *session_key,
  const char *prompt_group)
{
  static const char *names[] = {
    "database_context",
    "session_rename",
    "fontawesome_confirm",
    "memory_save",
    "database_study"
  };
  static const char *call_ids[] = {
    "call-empty-context",
    "call-empty-session",
    "call-empty-icon",
    "call-empty-user-fact",
    "call-empty-database-hint"
  };
  cJSON *request_session;
  cJSON *metadata;
  cJSON *request_group;
  cJSON *input;
  size_t expected_index;

  request_session = cJSON_GetObjectItem(root, "session_id");
  metadata = cJSON_GetObjectItem(root, "metadata");
  request_group = cJSON_GetObjectItem(metadata,
                                      "strappy_prompt_group_key");
  input = cJSON_GetObjectItem(root, "input");
  if (!cJSON_IsString(request_session) ||
      (request_session->valuestring == NULL) ||
      (strcmp(request_session->valuestring, session_key) != 0) ||
      !cJSON_IsString(request_group) ||
      (request_group->valuestring == NULL) ||
      (strcmp(request_group->valuestring, prompt_group) != 0) ||
      !cJSON_IsArray(input) ||
      !harness_message_role_is(cJSON_GetArrayItem(input, 0), "user")) {
    return 0;
  }

  for (expected_index = 0U;
       expected_index < (sizeof(names) / sizeof(names[0]));
       expected_index++) {
    cJSON *item;
    int call_found;
    int output_found;

    call_found = 0;
    output_found = 0;
    for (item = input->child; item != NULL; item = item->next) {
      cJSON *type;
      cJSON *call_id;

      type = cJSON_IsObject(item) ? cJSON_GetObjectItem(item, "type") : NULL;
      call_id = cJSON_IsObject(item) ?
        cJSON_GetObjectItem(item, "call_id") : NULL;
      if (!cJSON_IsString(type) || (type->valuestring == NULL) ||
          !cJSON_IsString(call_id) || (call_id->valuestring == NULL) ||
          (strcmp(call_id->valuestring, call_ids[expected_index]) != 0)) {
        continue;
      }
      if (strcmp(type->valuestring, "function_call") == 0) {
        cJSON *name;

        name = cJSON_GetObjectItem(item, "name");
        call_found = cJSON_IsString(name) &&
          (name->valuestring != NULL) &&
          (strcmp(name->valuestring, names[expected_index]) == 0);
      } else if (strcmp(type->valuestring, "function_call_output") == 0) {
        cJSON *output;

        output = cJSON_GetObjectItem(item, "output");
        output_found = cJSON_IsString(output) &&
          (output->valuestring != NULL) &&
          ((expected_index == 1U) ?
            (strcmp(output->valuestring, "{}") == 0) :
            ((expected_index == 2U) ?
              (strcmp(output->valuestring,
                      "{\"confirmed\":[\"[fa:music]\"],"
                      "\"not_found\":[]}") == 0) :
              (output->valuestring[0] != '\0')));
      }
    }
    if (!call_found || !output_found) {
      return 0;
    }
  }
  return 1;
}

static int harness_function_output_request_is_valid(cJSON *root,
                                                    const char *session_key,
                                                    const char *prompt_group)
{
  return harness_named_function_output_request_is_valid(
    root,
    session_key,
    prompt_group,
    "database_context",
    "call-database-context-error",
    "Error: database_context does not accept argument 'unexpected'.");
}

static void harness_unlink_bash_full_output(const char *output)
{
  static const char marker[] = "Full output: ";
  const char *start;
  const char *end;
  size_t length;
  char *path;

  if (output == NULL) {
    return;
  }
  start = strstr(output, marker);
  if (start == NULL) {
    return;
  }
  start += sizeof(marker) - 1U;
  end = strchr(start, ']');
  if ((end == NULL) || (end <= start)) {
    return;
  }
  length = (size_t)(end - start);
  path = (char *)malloc(length + 1U);
  if (path == NULL) {
    return;
  }
  memcpy(path, start, length);
  path[length] = '\0';
  unlink(path);
  free(path);
}

static int harness_bash_function_pair_is_valid(
  cJSON *input,
  const char *expected_call_id,
  const char *expected_output,
  int exact_output,
  int expected_output_truncated)
{
  cJSON *item;
  int call_found;
  int output_found;

  call_found = 0;
  output_found = 0;
  if (!cJSON_IsArray(input) || (expected_call_id == NULL) ||
      (expected_output == NULL)) {
    return 0;
  }
  for (item = input->child; item != NULL; item = item->next) {
    cJSON *type;
    cJSON *call_id;

    type = cJSON_IsObject(item) ? cJSON_GetObjectItem(item, "type") : NULL;
    call_id = cJSON_IsObject(item) ?
      cJSON_GetObjectItem(item, "call_id") : NULL;
    if (!cJSON_IsString(type) || (type->valuestring == NULL) ||
        !cJSON_IsString(call_id) || (call_id->valuestring == NULL) ||
        (strcmp(call_id->valuestring, expected_call_id) != 0)) {
      continue;
    }
    if (strcmp(type->valuestring, "function_call") == 0) {
      cJSON *name;

      name = cJSON_GetObjectItem(item, "name");
      call_found = cJSON_IsString(name) && (name->valuestring != NULL) &&
        (strcmp(name->valuestring, STRAPPY_TOOL_BASH) == 0);
    } else if (strcmp(type->valuestring, "function_call_output") == 0) {
      cJSON *output;
      cJSON *result;
      cJSON *result_output;
      cJSON *result_truncated;
      const char *text;
      int text_matches;

      output = cJSON_GetObjectItem(item, "output");
      if (!cJSON_IsString(output) || (output->valuestring == NULL) ||
          (cJSON_GetObjectItem(item, "output_truncated") != NULL)) {
        continue;
      }
      result = cJSON_Parse(output->valuestring);
      result_output = cJSON_IsObject(result) ?
        cJSON_GetObjectItem(result, "output") : NULL;
      result_truncated = cJSON_IsObject(result) ?
        cJSON_GetObjectItem(result, "output_truncated") : NULL;
      text = cJSON_IsString(result_output) ? result_output->valuestring : NULL;
      text_matches = (text != NULL) &&
        (exact_output ? (strcmp(text, expected_output) == 0) :
          (strstr(text, expected_output) != NULL));
      output_found = text_matches &&
        (expected_output_truncated ? cJSON_IsTrue(result_truncated) :
          cJSON_IsFalse(result_truncated));
      if (output_found && expected_output_truncated) {
        harness_unlink_bash_full_output(text);
      }
      cJSON_Delete(result);
    }
  }
  return call_found && output_found;
}

static int harness_bash_output_request_is_valid(cJSON *root,
                                                const char *session_key,
                                                const char *prompt_group)
{
  cJSON *request_session;
  cJSON *metadata;
  cJSON *request_group;
  cJSON *input;

  if ((session_key == NULL) || (prompt_group == NULL)) {
    return 0;
  }
  request_session = cJSON_GetObjectItem(root, "session_id");
  metadata = cJSON_GetObjectItem(root, "metadata");
  request_group = cJSON_IsObject(metadata) ?
    cJSON_GetObjectItem(metadata, "strappy_prompt_group_key") : NULL;
  input = cJSON_GetObjectItem(root, "input");
  return cJSON_IsString(request_session) &&
    (request_session->valuestring != NULL) &&
    (strcmp(request_session->valuestring, session_key) == 0) &&
    cJSON_IsString(request_group) && (request_group->valuestring != NULL) &&
    (strcmp(request_group->valuestring, prompt_group) == 0) &&
    cJSON_IsArray(input) && (cJSON_GetArraySize(input) >= 9) &&
    harness_message_role_is(cJSON_GetArrayItem(input, 0), "user") &&
    harness_bash_function_pair_is_valid(input,
                                        "call-bash-complete",
                                        "complete\n",
                                        1,
                                        0) &&
    harness_bash_function_pair_is_valid(input,
                                        "call-bash-truncated",
                                        "2501\n\n\n[Showing lines ",
                                        0,
                                        1) &&
    harness_bash_function_pair_is_valid(
      input,
      "call-bash-error",
      "Error: failed\n\n\nCommand exited with code 7.",
      1,
      0);
}

static int harness_file_mutation_output_request_is_valid(
  cJSON *root,
  const char *session_key,
  const char *prompt_group)
{
  static const char *names[] = {
    STRAPPY_TOOL_FILE_WRITE,
    STRAPPY_TOOL_FILE_EDIT
  };
  static const char *call_ids[] = {
    "call-file-write",
    "call-file-edit"
  };
  cJSON *request_session;
  cJSON *metadata;
  cJSON *request_group;
  cJSON *input;
  size_t expected_index;

  request_session = cJSON_GetObjectItem(root, "session_id");
  metadata = cJSON_GetObjectItem(root, "metadata");
  request_group = cJSON_IsObject(metadata) ?
    cJSON_GetObjectItem(metadata, "strappy_prompt_group_key") : NULL;
  input = cJSON_GetObjectItem(root, "input");
  if (!cJSON_IsString(request_session) ||
      (request_session->valuestring == NULL) ||
      (strcmp(request_session->valuestring, session_key) != 0) ||
      !cJSON_IsString(request_group) ||
      (request_group->valuestring == NULL) ||
      (strcmp(request_group->valuestring, prompt_group) != 0) ||
      !cJSON_IsArray(input) ||
      !harness_message_role_is(cJSON_GetArrayItem(input, 0), "user")) {
    return 0;
  }

  for (expected_index = 0U;
       expected_index < (sizeof(names) / sizeof(names[0]));
       expected_index++) {
    cJSON *item;
    int call_found;
    int output_found;

    call_found = 0;
    output_found = 0;
    for (item = input->child; item != NULL; item = item->next) {
      cJSON *type;
      cJSON *call_id;

      type = cJSON_IsObject(item) ? cJSON_GetObjectItem(item, "type") : NULL;
      call_id = cJSON_IsObject(item) ?
        cJSON_GetObjectItem(item, "call_id") : NULL;
      if (!cJSON_IsString(type) || (type->valuestring == NULL) ||
          !cJSON_IsString(call_id) || (call_id->valuestring == NULL) ||
          (strcmp(call_id->valuestring, call_ids[expected_index]) != 0)) {
        continue;
      }
      if (strcmp(type->valuestring, "function_call") == 0) {
        cJSON *name;

        name = cJSON_GetObjectItem(item, "name");
        call_found = cJSON_IsString(name) &&
          (name->valuestring != NULL) &&
          (strcmp(name->valuestring, names[expected_index]) == 0);
      } else if (strcmp(type->valuestring, "function_call_output") == 0) {
        cJSON *output;

        output = cJSON_GetObjectItem(item, "output");
        output_found = cJSON_IsString(output) &&
          (output->valuestring != NULL) &&
          (strcmp(output->valuestring, "{}") == 0);
      }
    }
    if (!call_found || !output_found) {
      return 0;
    }
  }
  return 1;
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

static int harness_run_answer_quality_server(int listener_fd)
{
  static const char *first_response =
    "{\"id\":\"resp-quality-first\",\"object\":\"response\","
    "\"created_at\":1700000000,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-quality-first\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"" HARNESS_UNICODE_EMOJI_ANSWER
    "\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":3,"
    "\"total_tokens\":7}}";
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
                                  "Report answer quality",
                                  &session_key,
                                  &prompt_group) &&
    harness_request_preflight_contains(
      root,
      "\"path\":\"/tmp/strappy-preflight-db-") &&
    harness_request_preflight_contains(
      root,
      "\"app_name\":null") &&
    harness_request_preflight_contains(
      root,
      "\"date_saved\":\"") &&
    harness_request_preflight_contains(
      root,
      "\"fact\":\"The user's favorite color is purple.\"") &&
    harness_send_json_response(client_fd, 200L, first_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    fprintf(stderr, "Answer quality server rejected the request.\n");
  }
  free(session_key);
  free(prompt_group);
  return ok;
}

static int harness_run_world_knowledge_server(int listener_fd)
{
  static const char *final_response =
    "{\"id\":\"resp-world-final\",\"object\":\"response\","
    "\"created_at\":1700000001,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-world-final\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"World answer.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":4,"
    "\"total_tokens\":8}}";
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
    harness_world_knowledge_request_is_valid(root,
                                             "Use world knowledge",
                                             &session_key,
                                             &prompt_group) &&
    harness_send_json_response(client_fd, 200L, final_response);
  cJSON_Delete(root);
  close(client_fd);
  free(session_key);
  free(prompt_group);
  if (!ok) {
    fprintf(stderr, "World Knowledge server rejected the request.\n");
  }
  return ok;
}

static int harness_run_first_prompt_preflight_server(int listener_fd)
{
  static const char *first_response =
    "{\"id\":\"resp-preflight-first\",\"object\":\"response\","
    "\"created_at\":1700000030,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-preflight-first\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"First round answer.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":4,"
    "\"total_tokens\":8}}";
  static const char *second_response =
    "{\"id\":\"resp-preflight-second\",\"object\":\"response\","
    "\"created_at\":1700000031,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-preflight-second\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Second round answer.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":8,\"output_tokens\":4,"
    "\"total_tokens\":12}}";
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
    harness_world_knowledge_request_is_valid(root,
                                             "First prompt",
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
    harness_world_followup_request_is_valid(root,
                                            session_key,
                                            prompt_group) &&
    harness_send_json_response(client_fd, 200L, second_response);
  cJSON_Delete(root);
  close(client_fd);
  free(session_key);
  free(prompt_group);
  if (!ok) {
    fprintf(stderr, "First-prompt-only preflight server rejected a request.\n");
  }
  return ok;
}

static int harness_run_empty_answer_server(int listener_fd)
{
  static const char *first_response =
    "{\"id\":\"resp-empty-answer\",\"object\":\"response\","
    "\"created_at\":1700000010,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":1,"
    "\"total_tokens\":5}}";
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
                                  "Report empty answer quality",
                                  &session_key,
                                  &prompt_group) &&
    harness_send_json_response(client_fd, 200L, first_response);
  cJSON_Delete(root);
  close(client_fd);
  free(session_key);
  free(prompt_group);
  return ok;
}

static int harness_run_empty_answer_after_tools_server(int listener_fd)
{
  static const char *tool_response =
    "{\"id\":\"resp-empty-after-tools\",\"object\":\"response\","
    "\"created_at\":1700000020,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"function_call\",\"id\":\"fc-empty-context\","
    "\"call_id\":\"call-empty-context\","
    "\"name\":\"database_context\","
    "\"arguments\":\"{\\\"database_id\\\":\\\"db_1\\\"}\","
    "\"status\":\"completed\"},{"
    "\"type\":\"function_call\",\"id\":\"fc-empty-session\","
    "\"call_id\":\"call-empty-session\","
    "\"name\":\"session_rename\","
    "\"arguments\":\"{\\\"name\\\":\\\"Empty Answer Audit\\\"}\","
    "\"status\":\"completed\"},{"
    "\"type\":\"function_call\",\"id\":\"fc-empty-icon\","
    "\"call_id\":\"call-empty-icon\","
    "\"name\":\"fontawesome_confirm\","
    "\"arguments\":\"{\\\"shortcodes\\\":[\\\"[fa:music]\\\"]}\","
    "\"status\":\"completed\"},{"
    "\"type\":\"function_call\",\"id\":\"fc-empty-user-fact\","
    "\"call_id\":\"call-empty-user-fact\","
    "\"name\":\"memory_save\","
    "\"arguments\":\"{\\\"fact\\\":\\\"Provenance fact.\\\"}\","
    "\"status\":\"completed\"},{"
    "\"type\":\"function_call\",\"id\":\"fc-empty-database-hint\","
    "\"call_id\":\"call-empty-database-hint\","
    "\"name\":\"database_study\","
    "\"arguments\":\"{\\\"database_id\\\":\\\"db_1\\\","
    "\\\"hint\\\":\\\"Provenance hint.\\\"}\","
    "\"status\":\"completed\"}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":10,"
    "\"total_tokens\":14}}";
  static const char *reasoning_only_response =
    "{\"id\":\"resp-empty-after-tools-reasoning\","
    "\"object\":\"response\",\"created_at\":1700000021,"
    "\"model\":\"test/model\",\"status\":\"completed\","
    "\"output\":[{\"type\":\"reasoning\","
    "\"id\":\"rs-empty-after-tools\",\"status\":\"completed\","
    "\"content\":[{\"type\":\"reasoning_text\","
    "\"text\":\"I should now provide the final answer.\"}],"
    "\"summary\":[]}],\"usage\":{\"input_tokens\":14,"
    "\"output_tokens\":4,\"total_tokens\":18}}";
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
                                  "Audit empty answer after tools",
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
    harness_required_function_outputs_request_is_valid(root,
                                                       session_key,
                                                       prompt_group) &&
    harness_send_json_response(client_fd, 200L, reasoning_only_response);
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
    "\"text\":\"Plain https://example.com, [FTP](ftp://example.com), "
    "![Image](https://example.com/image.png), and [Empty](https://) are not "
    "references.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":4,"
    "\"total_tokens\":8}}";
  char *body;
  char *session_key;
  char *prompt_group;
  cJSON *root;
  cJSON *tools;
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
  tools = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "tools") : NULL;
  ok = cJSON_IsObject(root) &&
    harness_request_base_is_valid(root,
                                  "Use a server tool",
                                  &session_key,
                                  &prompt_group) &&
    harness_server_tool_has_engine(
      tools,
      STRAPPY_TOOL_OPENROUTER_WEB_SEARCH,
      "auto") &&
    harness_server_tool_has_engine(
      tools,
      STRAPPY_TOOL_OPENROUTER_WEB_FETCH,
      "auto") &&
    harness_send_json_response(client_fd, 200L, first_response);
  cJSON_Delete(root);
  close(client_fd);
  free(session_key);
  free(prompt_group);
  return ok;
}

static int harness_run_valid_web_reference_server(int listener_fd)
{
  static const char *first_response =
    "{\"id\":\"resp-web-reference-valid\",\"object\":\"response\","
    "\"created_at\":1700000004,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"openrouter:web_fetch\",\"id\":\"wf-reference-valid\","
    "\"status\":\"completed\",\"url\":\"http://example.com/article\","
    "\"title\":\"Example Article\",\"content\":\"Fetched page body\","
    "\"httpStatus\":200},{\"type\":\"message\","
    "\"id\":\"msg-web-reference-valid\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Read [Example source](http://example.com/article).\","
    "\"annotations\":[]}]}],\"usage\":{\"input_tokens\":4,"
    "\"output_tokens\":5,\"total_tokens\":9}}";
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
                                  "Use a cited server tool",
                                  &session_key,
                                  &prompt_group) &&
    harness_send_json_response(client_fd, 200L, first_response);
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
    "\"type\":\"function_call\",\"id\":\"fc-database-context-error\","
    "\"call_id\":\"call-database-context-error\","
    "\"name\":\"database_context\","
    "\"arguments\":\"{\\\"unexpected\\\":true}\","
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
    harness_disabled_web_search_request_is_valid(root) &&
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
  free(session_key);
  free(prompt_group);
  return ok;
}

static int harness_run_bash_cancellation_server(int listener_fd)
{
  static const char *tool_response =
    "{\"id\":\"resp-bash-cancel\",\"object\":\"response\","
    "\"created_at\":1700000007,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"function_call\",\"id\":\"fc-bash-cancel\","
    "\"call_id\":\"call-bash-cancel\",\"name\":\"bash\","
    "\"arguments\":\"{\\\"command\\\":\\\"printf 'started\\\\n'; sleep 30\\\"}\","
    "\"status\":\"completed\"}],\"usage\":{"
    "\"input_tokens\":4,\"output_tokens\":4,\"total_tokens\":8}}";
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
    harness_coding_assistant_request_is_valid(root,
                                              "Cancel bash tool",
                                              1) &&
    harness_send_json_response(client_fd, 200L, tool_response);
  cJSON_Delete(root);
  close(client_fd);
  return ok;
}

static int harness_run_bash_output_server(int listener_fd)
{
  static const char *tool_response =
    "{\"id\":\"resp-bash-output\",\"object\":\"response\","
    "\"created_at\":1700000008,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"function_call\",\"id\":\"fc-bash-complete\","
    "\"call_id\":\"call-bash-complete\",\"name\":\"bash\","
    "\"arguments\":\"{\\\"command\\\":\\\"printf 'complete\\\\n'\\\"}\","
    "\"status\":\"completed\"},{"
    "\"type\":\"function_call\",\"id\":\"fc-bash-truncated\","
    "\"call_id\":\"call-bash-truncated\",\"name\":\"bash\","
    "\"arguments\":\"{\\\"command\\\":\\\"seq 1 2501\\\"}\","
    "\"status\":\"completed\"},{"
    "\"type\":\"function_call\",\"id\":\"fc-bash-error\","
    "\"call_id\":\"call-bash-error\",\"name\":\"bash\","
    "\"arguments\":\"{\\\"command\\\":\\\"printf 'failed\\\\n'; exit 7\\\"}\","
    "\"status\":\"completed\"}],\"usage\":{"
    "\"input_tokens\":4,\"output_tokens\":4,\"total_tokens\":8}}";
  static const char *final_response =
    "{\"id\":\"resp-bash-output-final\",\"object\":\"response\","
    "\"created_at\":1700000009,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-bash-output-final\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Bash output flag final answer.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":8,\"output_tokens\":4,"
    "\"total_tokens\":12}}";
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
  if (cJSON_IsObject(root) &&
      harness_coding_assistant_request_is_valid(root,
                                                "Report bash truncation",
                                                1)) {
    cJSON *request_session;
    cJSON *metadata;
    cJSON *request_group;

    request_session = cJSON_GetObjectItem(root, "session_id");
    metadata = cJSON_GetObjectItem(root, "metadata");
    request_group = cJSON_IsObject(metadata) ?
      cJSON_GetObjectItem(metadata, "strappy_prompt_group_key") : NULL;
    if (cJSON_IsString(request_session) &&
        (request_session->valuestring != NULL) &&
        cJSON_IsString(request_group) &&
        (request_group->valuestring != NULL)) {
      session_key = strdup(request_session->valuestring);
      prompt_group = strdup(request_group->valuestring);
    }
  }
  ok = (session_key != NULL) && (prompt_group != NULL) &&
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
  ok = cJSON_IsObject(root) &&
    harness_bash_output_request_is_valid(root,
                                         session_key,
                                         prompt_group) &&
    harness_send_json_response(client_fd, 200L, final_response);
  free(body);
  cJSON_Delete(root);
  close(client_fd);
  free(session_key);
  free(prompt_group);
  return ok;
}

static int harness_run_file_mutation_server(int listener_fd)
{
  static const char *tool_response =
    "{\"id\":\"resp-file-mutation\",\"object\":\"response\","
    "\"created_at\":1700000010,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"function_call\",\"id\":\"fc-file-write\","
    "\"call_id\":\"call-file-write\",\"name\":\"file_write\","
    "\"arguments\":\"{\\\"path\\\":\\\"response_mutation.txt\\\","
    "\\\"content\\\":\\\"first\\\\nsecond\\\\n\\\"}\","
    "\"status\":\"completed\"},{"
    "\"type\":\"function_call\",\"id\":\"fc-file-edit\","
    "\"call_id\":\"call-file-edit\",\"name\":\"file_edit\","
    "\"arguments\":\"{\\\"path\\\":\\\"response_mutation.txt\\\","
    "\\\"old_text\\\":\\\"second\\\","
    "\\\"new_text\\\":\\\"changed\\\"}\","
    "\"status\":\"completed\"}],\"usage\":{"
    "\"input_tokens\":4,\"output_tokens\":4,\"total_tokens\":8}}";
  static const char *final_response =
    "{\"id\":\"resp-file-mutation-final\",\"object\":\"response\","
    "\"created_at\":1700000011,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-file-mutation-final\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"File mutation final answer.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":8,\"output_tokens\":4,"
    "\"total_tokens\":12}}";
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
  if (cJSON_IsObject(root) &&
      harness_coding_assistant_request_is_valid(root,
                                                "Mutate a file",
                                                0)) {
    cJSON *request_session;
    cJSON *metadata;
    cJSON *request_group;

    request_session = cJSON_GetObjectItem(root, "session_id");
    metadata = cJSON_GetObjectItem(root, "metadata");
    request_group = cJSON_IsObject(metadata) ?
      cJSON_GetObjectItem(metadata, "strappy_prompt_group_key") : NULL;
    if (cJSON_IsString(request_session) &&
        (request_session->valuestring != NULL) &&
        cJSON_IsString(request_group) &&
        (request_group->valuestring != NULL)) {
      session_key = strdup(request_session->valuestring);
      prompt_group = strdup(request_group->valuestring);
    }
  }
  ok = (session_key != NULL) && (prompt_group != NULL) &&
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
    harness_file_mutation_output_request_is_valid(root,
                                                  session_key,
                                                  prompt_group) &&
    harness_send_json_response(client_fd, 200L, final_response);
  cJSON_Delete(root);
  close(client_fd);
  free(session_key);
  free(prompt_group);
  return ok;
}

static int harness_run_coding_bash_disabled_server(int listener_fd)
{
  static const char *final_response =
    "{\"id\":\"resp-bash-disabled\",\"object\":\"response\","
    "\"created_at\":1700000008,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-bash-disabled\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Bash disabled.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":4,"
    "\"total_tokens\":8}}";
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
    harness_coding_assistant_request_is_valid(root,
                                              "Keep bash disabled",
                                              0) &&
    harness_send_json_response(client_fd, 200L, final_response);
  cJSON_Delete(root);
  close(client_fd);
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
    "\"name\":\"database_list\",\"arguments\":\"{}\","
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
    harness_named_function_output_request_is_valid(
      root,
      session_key,
      prompt_group,
      "database_list",
      "call-database-list",
      NULL) &&
    harness_send_json_response(client_fd, 200L, final_response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    free(session_key);
    free(prompt_group);
    return 0;
  }

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
    if (scenario == HARNESS_RESPONSES_SERVER_ANSWER_QUALITY) {
      ok = harness_run_answer_quality_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_WORLD_KNOWLEDGE) {
      ok = harness_run_world_knowledge_server(listener_fd);
    } else if (scenario ==
               HARNESS_RESPONSES_SERVER_PREFLIGHT_FIRST_PROMPT_ONLY) {
      ok = harness_run_first_prompt_preflight_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_SERVER_TOOL) {
      ok = harness_run_server_tool_server(listener_fd);
    } else if (scenario ==
               HARNESS_RESPONSES_SERVER_WEB_REFERENCE_VALID) {
      ok = harness_run_valid_web_reference_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_FUNCTION_TOOL) {
      ok = harness_run_function_tool_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_BASH_CANCELLATION) {
      ok = harness_run_bash_cancellation_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_BASH_OUTPUT) {
      ok = harness_run_bash_output_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_FILE_MUTATION) {
      ok = harness_run_file_mutation_server(listener_fd);
    } else if (scenario ==
               HARNESS_RESPONSES_SERVER_CODING_BASH_DISABLED) {
      ok = harness_run_coding_bash_disabled_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_RETRY_AFTER) {
      ok = harness_run_retry_after_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_SLOW) {
      ok = harness_run_slow_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_EMPTY_ANSWER) {
      ok = harness_run_empty_answer_server(listener_fd);
    } else if (scenario ==
               HARNESS_RESPONSES_SERVER_EMPTY_ANSWER_AFTER_TOOLS) {
      ok = harness_run_empty_answer_after_tools_server(listener_fd);
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

static int harness_answer_quality_precedes_assistant(
  const strappy_session_message_record_list *timeline,
  const char *answer_text,
  const char *expected_outcome)
{
  size_t index;

  if ((timeline == NULL) || (answer_text == NULL) ||
      (expected_outcome == NULL)) {
    return 0;
  }
  for (index = 0U; (index + 1U) < timeline->count; index++) {
    const strappy_session_message_record *quality;
    const strappy_session_message_record *answer;

    quality = &timeline->records[index];
    answer = &timeline->records[index + 1U];
    if ((quality->role != NULL) &&
        (strcmp(quality->role, "answer_quality") == 0) &&
        (answer->role != NULL) &&
        (strcmp(answer->role, "assistant") == 0) &&
        (answer->content != NULL) &&
        (strcmp(answer->content, answer_text) == 0) &&
        (quality->http_attempt_id == answer->http_attempt_id) &&
        (quality->metadata_json != NULL) &&
        (strstr(quality->metadata_json, expected_outcome) != NULL) &&
        (strstr(quality->metadata_json, "\"checks\":[") != NULL) &&
        (quality->message_key != NULL) &&
        (strncmp(quality->message_key,
                 "answer-quality-",
                 15U) == 0) &&
        !quality->include_in_context) {
      return 1;
    }
  }
  return 0;
}

static int harness_answer_quality_is_final(
  const strappy_session_message_record_list *timeline,
  const char *expected_outcome,
  const char *expected_check_key)
{
  const strappy_session_message_record *quality;

  if ((timeline == NULL) || (timeline->count == 0U) ||
      (expected_outcome == NULL) || (expected_check_key == NULL)) {
    return 0;
  }
  quality = &timeline->records[timeline->count - 1U];
  return (quality->role != NULL) &&
    (strcmp(quality->role, "answer_quality") == 0) &&
    (quality->metadata_json != NULL) &&
    (strstr(quality->metadata_json, expected_outcome) != NULL) &&
    (strstr(quality->metadata_json, expected_check_key) != NULL) &&
    (quality->message_key != NULL) &&
    (strncmp(quality->message_key, "answer-quality-", 15U) == 0) &&
    !quality->include_in_context;
}

static int harness_test_answer_quality_report(void)
{
  char path[] = "/tmp/strappy-responses-quality-XXXXXX";
  char database_path[] = "/tmp/strappy-preflight-db-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  char *seed_output;
  sqlite3 *db;
  strappy_session_message_record_list timeline;
  strappy_session_message_record_list timeline_tail;
  size_t timeline_total;
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
    return harness_fail("Could not create answer-quality harness database.");
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
  strappy_session_message_record_list_init(&timeline);
  strappy_session_message_record_list_init(&timeline_tail);
  timeline_total = 0U;
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
    STRAPPY_TOOL_MEMORY_SAVE,
    "{\"fact\":\"The user's favorite color is purple.\"}",
    &error);
  if (seed_output == NULL) {
    fprintf(stderr,
            "Could not prepare answer-quality integration test: %s\n",
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
    STRAPPY_TOOL_SESSION_RENAME,
    "{\"name\":\"Earlier Request\"}",
    &error);
  if ((seed_output == NULL) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_ANSWER_QUALITY,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare titled answer-quality integration test: %s\n",
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
    "Report answer quality",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    harness_record_ledger_event,
    &events,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) &&
    (strcmp(result, HARNESS_UNICODE_EMOJI_ANSWER) == 0) &&
    server_ok && events.valid && (events.count == 1LL) &&
    (events.answer_quality_count == 1LL) &&
    events.saw_thinking && !events.saw_tools &&
    (events.processing_count >= 2LL) &&
    (events.clear_count == 1L);
  if (!ok) {
    fprintf(stderr,
            "Answer quality result mismatch: result=%s server=%d valid=%d "
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
                           "SELECT COUNT(*) FROM http_attempts;",
                           &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(DISTINCT t.prompt_group_key) "
                        "FROM http_attempts a JOIN model_requests r "
                        "ON r.id=a.request_id JOIN turns t ON t.id=r.turn_id;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "request_kind='tool_continuation';",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM tool_executions;",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items i "
                        "JOIN message_items m ON m.item_id=i.id WHERE "
                        "m.role='developer' AND i.include_in_context=1;",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_audits WHERE "
                        "outcome='failed' AND guidance_version='6';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks;",
                        &value) && (value == 6LL) &&
      harness_query_int(
        db,
        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
        "(ordinal=0 AND check_key='answer_non_empty' AND "
        "check_kind='answer_content' AND label='Answer provided' AND "
        "tool_name IS NULL) OR "
        "(ordinal=1 AND check_key='unicode_emoji_absent' AND "
        "check_kind='answer_content' AND label='No emoji' AND "
        "tool_name IS NULL) OR "
        "(ordinal=2 AND check_key='web_reference' AND "
        "check_kind='answer_content' AND label='Source link included' AND "
        "tool_name IS NULL) OR "
        "(ordinal=5 AND check_key='database_context' AND "
        "check_kind='required_tool' AND label='Database context checked' AND "
        "tool_name='" STRAPPY_TOOL_DATABASE_CONTEXT "') OR "
        "(ordinal=3 AND check_key='session_rename' AND "
        "check_kind='required_tool' AND label='Session named' AND "
        "tool_name='" STRAPPY_TOOL_SESSION_RENAME "') OR "
        "(ordinal=4 AND check_key='fontawesome_confirm' AND "
        "check_kind='required_tool' AND "
        "label='Font Awesome shortcode confirmed' AND "
        "tool_name='" STRAPPY_TOOL_FONTAWESOME_CONFIRM "');",
        &value) && (value == 6LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "status='failed';",
                        &value) && (value == 4LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "check_key='unicode_emoji_absent' AND "
                        "status='failed' AND "
                        "detail='The response included emoji.';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "status='not_applicable' AND "
                        "check_key='web_reference';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "check_key IN ('memory_save',"
                        "'database_study');",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM item_text_parts p "
                        "JOIN message_items m ON m.item_id=p.item_id WHERE "
                        "m.role='assistant' AND p.text='"
                        HARNESS_UNICODE_EMOJI_ANSWER "';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM sessions WHERE "
                        "name='Earlier Request';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM sqlite_master WHERE "
                        "type='table' AND lower(sql) LIKE '%raw_json%';",
                        &value) && (value == 0LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (ok) {
    ok = strappy_db_list_response_timeline(path,
                                           session_id,
                                           &timeline,
                                           &error) &&
      harness_answer_quality_precedes_assistant(&timeline,
                                                HARNESS_UNICODE_EMOJI_ANSWER,
                                                "\"outcome\":\"failed\"") &&
      (timeline.count >= 2U) &&
      strappy_db_list_response_timeline_range(path,
                                              session_id,
                                              timeline.count - 2U,
                                              &timeline_tail,
                                              &timeline_total,
                                              &error) &&
      (timeline_total == timeline.count) && (timeline_tail.count == 2U) &&
      harness_answer_quality_precedes_assistant(&timeline_tail,
                                                HARNESS_UNICODE_EMOJI_ANSWER,
                                                "\"outcome\":\"failed\"");
  }
  if (!ok) {
    fprintf(stderr,
            "Answer quality report integration failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  strappy_session_message_record_list_destroy(&timeline_tail);
  strappy_session_message_record_list_destroy(&timeline);
  free(error);
  unlink(database_path);
  unlink(path);
  return ok;
}

static int harness_test_world_knowledge_assistant_set(void)
{
  char path[] = "/tmp/strappy-responses-world-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  char *assistant_set_id;
  sqlite3 *db;
  long long session_id;
  long long value;
  pid_t server_pid;
  int fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create World Knowledge harness database.");
  }
  close(fd);
  error = NULL;
  result = NULL;
  assistant_set_id = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error)) {
    fprintf(stderr,
            "Could not create World Knowledge session: %s\n",
            (error != NULL) ? error : "database setup failed");
    free(error);
    unlink(path);
    return 0;
  }
  result = strappy_tools_execute(
    path,
    session_id,
    "../shared/Resources",
    STRAPPY_TOOL_MEMORY_SAVE,
    "{\"fact\":\"Personal-only preflight memory.\"}",
    &error);
  if (result == NULL) {
    fprintf(stderr,
            "Could not seed assistant-scoped preflight memory: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    unlink(path);
    return 0;
  }
  free(result);
  result = NULL;
  if (!strappy_session_update_assistant_set(
        path,
        session_id,
        "../shared/Resources",
        STRAPPY_ASSISTANT_SET_CODING_ASSISTANT,
        &error) || (error != NULL)) {
    fprintf(stderr,
            "Available Coding Assistant set was not accepted: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    unlink(path);
    return 0;
  }
  free(error);
  error = NULL;
  if (!strappy_session_update_assistant_set(
        path,
        session_id,
        "../shared/Resources",
        STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE,
        &error) ||
      !strappy_db_update_session_web_provider(
        path,
        session_id,
        STRAPPY_WEB_PROVIDER_PARALLEL,
        &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_WORLD_KNOWLEDGE,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare World Knowledge integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  result = strappy_responses_send_prompt_for_session_and_store(
    "Use world knowledge",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) && (strcmp(result, "World answer.") == 0) &&
    server_ok && (error == NULL);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM session_assistant_sets "
                           "WHERE assistant_set_id='world_knowledge';",
                           &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM user_facts WHERE "
                        "assistant_set_id='personal_assistant' AND "
                        "value='Personal-only preflight memory.';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_audits WHERE "
                        "outcome='failed' AND guidance_version='6';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks;",
                        &value) && (value == 5LL) &&
      harness_query_int(
        db,
        "SELECT COUNT(*) FROM answer_quality_checks WHERE check_key IN ("
        "'answer_non_empty','unicode_emoji_absent','web_reference',"
        "'session_rename',"
        "'fontawesome_confirm');",
        &value) && (value == 5LL) &&
      harness_query_int(
        db,
        "SELECT COUNT(*) FROM answer_quality_checks WHERE check_key IN ("
        "'database_context','database_study');",
        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM tool_executions;",
                        &value) && (value == 0LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (ok) {
    ok = strappy_session_update_assistant_set(
      path,
      session_id,
      "../shared/Resources",
      STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT,
      &error) &&
      strappy_db_get_session_assistant_set(path,
                                           session_id,
                                           &assistant_set_id,
                                           &error) &&
      (assistant_set_id != NULL) &&
      (strcmp(assistant_set_id,
              STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT) == 0);
  }
  if (!ok) {
    fprintf(stderr,
            "World Knowledge integration failed: %s\n",
            (error != NULL) ? error :
              "request, policy, audit, or set-switch mismatch");
  }
  free(assistant_set_id);
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_preflight_runs_only_on_first_prompt(void)
{
  char path[] = "/tmp/strappy-responses-first-preflight-XXXXXX";
  char endpoint[128];
  char *error;
  char *first_result;
  char *second_result;
  sqlite3 *db;
  strappy_session_record session;
  long long session_id;
  long long value;
  pid_t server_pid;
  int fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create first-preflight harness database.");
  }
  close(fd);
  error = NULL;
  first_result = NULL;
  second_result = NULL;
  session_id = 0LL;
  strappy_session_record_init(&session);
  if (!harness_create_session_database(path, &session_id, &error) ||
      !strappy_session_update_assistant_set(
        path,
        session_id,
        "../shared/Resources",
        STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE,
        &error) ||
      !strappy_db_update_session_web_provider(
        path,
        session_id,
        STRAPPY_WEB_PROVIDER_PARALLEL,
        &error) ||
      !harness_start_server(
        HARNESS_RESPONSES_SERVER_PREFLIGHT_FIRST_PROMPT_ONLY,
        endpoint,
        sizeof(endpoint),
        &server_pid)) {
    fprintf(stderr,
            "Could not prepare first-prompt preflight test: %s\n",
            (error != NULL) ? error : "server setup failed");
    strappy_session_record_destroy(&session);
    free(error);
    unlink(path);
    return 0;
  }

  first_result = strappy_responses_send_prompt_for_session_and_store(
    "First prompt",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    &error);
  if ((first_result != NULL) &&
      (strcmp(first_result, "First round answer.") == 0) &&
      (error == NULL)) {
    second_result = strappy_responses_send_prompt_for_session_and_store(
      "Second prompt",
      "/dev/null",
      endpoint,
      "test-token",
      "../shared/Resources",
      path,
      session_id,
      &error);
  }
  server_ok = harness_wait_for_server(
    server_pid,
    (first_result == NULL) || (second_result == NULL));
  ok = (first_result != NULL) &&
    (strcmp(first_result, "First round answer.") == 0) &&
    (second_result != NULL) &&
    (strcmp(second_result, "Second round answer.") == 0) &&
    server_ok && (error == NULL);
  free(first_result);
  free(second_result);

  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM model_requests WHERE "
                           "request_kind='user';",
                           &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM function_calls f "
                        "JOIN conversation_items i ON i.id=f.item_id WHERE "
                        "f.tool_name='memory_read' AND "
                        "i.introduced_request_id IS NOT NULL AND "
                        "i.source_attempt_id IS NULL;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items i "
                        "JOIN message_items m ON m.item_id=i.id "
                        "JOIN item_text_parts p ON p.item_id=i.id WHERE "
                        "m.role='assistant' AND p.part_type='input_text' AND "
                        "p.text='" HARNESS_WORLD_PREFLIGHT_ASSISTANT_TEXT "' "
                        "AND i.introduced_request_id IS NOT NULL AND "
                        "i.source_attempt_id IS NULL;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM tool_executions;",
                        &value) && (value == 0LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (ok) {
    ok = strappy_db_load_session(path, session_id, &session, &error) &&
      (session.prompt != NULL) &&
      (strcmp(session.prompt, "Second prompt") == 0) &&
      (session.response != NULL) &&
      (strcmp(session.response, "Second round answer.") == 0);
  }
  if (!ok) {
    fprintf(stderr,
            "First-prompt-only preflight integration failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  strappy_session_record_destroy(&session);
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_empty_answer_quality_report(void)
{
  char path[] = "/tmp/strappy-responses-empty-answer-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  strappy_session_message_record_list timeline;
  long long session_id;
  long long value;
  pid_t server_pid;
  int fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create empty-answer harness database.");
  }
  close(fd);
  strappy_session_message_record_list_init(&timeline);
  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !harness_start_server(
        HARNESS_RESPONSES_SERVER_EMPTY_ANSWER,
        endpoint,
        sizeof(endpoint),
        &server_pid)) {
    fprintf(stderr,
            "Could not prepare empty-answer integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    strappy_session_message_record_list_destroy(&timeline);
    free(error);
    unlink(path);
    return 0;
  }

  result = strappy_responses_send_prompt_for_session_and_store(
    "Report empty answer quality",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) && (result[0] == '\0') && server_ok &&
    (error == NULL);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM http_attempts;",
                           &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "request_kind='user';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items i "
                        "JOIN message_items m ON m.item_id=i.id WHERE "
                        "m.role='developer' AND i.include_in_context=1;",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_audits WHERE "
                        "outcome='failed' AND guidance_version='6';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks;",
                        &value) && (value == 6LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "ordinal=0 AND check_key='answer_non_empty' AND "
                        "check_kind='answer_content' AND "
                        "label='Answer provided' AND status='failed' AND "
                        "tool_name IS NULL AND "
                        "instr(detail,'non-empty assistant answer')>0;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "check_kind='required_tool' AND status='failed';",
                        &value) && (value == 3LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "check_key='unicode_emoji_absent' AND "
                        "status='passed';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "check_key='web_reference' AND "
                        "status='not_applicable';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM tool_executions;",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM turns;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items i "
                        "JOIN message_items m ON m.item_id=i.id WHERE "
                        "m.role='assistant' AND "
                        "i.source_attempt_id IS NOT NULL;",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items i "
                        "JOIN message_items m ON m.item_id=i.id "
                        "JOIN item_text_parts p ON p.item_id=i.id WHERE "
                        "m.role='assistant' AND "
                        "i.introduced_request_id IS NOT NULL AND p.text='"
                        HARNESS_PERSONAL_PREFLIGHT_ASSISTANT_TEXT "';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM sqlite_master WHERE "
                        "type='table' AND lower(sql) LIKE '%raw_json%';",
                        &value) && (value == 0LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (ok) {
    ok = strappy_db_list_response_timeline(path,
                                           session_id,
                                           &timeline,
                                           &error) &&
      harness_answer_quality_is_final(&timeline,
                                      "\"outcome\":\"failed\"",
                                      "\"key\":\"answer_non_empty\"");
  }
  if (!ok) {
    fprintf(stderr,
            "Empty answer quality integration failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  strappy_session_message_record_list_destroy(&timeline);
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_empty_answer_after_tools_quality_report(void)
{
  char path[] = "/tmp/strappy-responses-empty-after-tools-XXXXXX";
  char database_path[] = "/tmp/strappy-provenance-db-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  long long session_id;
  long long value;
  pid_t server_pid;
  int fd;
  int database_fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create empty-answer harness database.");
  }
  close(fd);
  database_fd = mkstemp(database_path);
  if (database_fd < 0) {
    unlink(path);
    return harness_fail("Could not create provenance fixture database.");
  }
  close(database_fd);
  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !harness_create_approved_preflight_database(path,
                                                   database_path,
                                                   &error) ||
      !harness_start_server(
        HARNESS_RESPONSES_SERVER_EMPTY_ANSWER_AFTER_TOOLS,
        endpoint,
        sizeof(endpoint),
        &server_pid)) {
    fprintf(stderr,
            "Could not prepare empty-answer-after-tools test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(database_path);
    unlink(path);
    return 0;
  }

  result = strappy_responses_send_prompt_for_session_and_store(
    "Audit empty answer after tools",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) && (result[0] == '\0') &&
    server_ok && (error == NULL);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM http_attempts;",
                           &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests;",
                        &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "request_kind='tool_continuation';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "request_kind NOT IN ('user','tool_continuation');",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM tool_executions WHERE "
                        "state='completed';",
                        &value) && (value == 5LL) &&
      harness_query_int(db,
                        "SELECT COUNT(DISTINCT f.tool_name) FROM "
                        "tool_executions e JOIN function_calls f "
                        "ON f.item_id=e.function_call_item_id "
                        "WHERE f.tool_name IN ("
                        "'database_context',"
                        "'session_rename',"
                        "'fontawesome_confirm',"
                        "'memory_save',"
                        "'database_study');",
                        &value) && (value == 5LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items i "
                        "JOIN message_items m ON m.item_id=i.id WHERE "
                        "m.role='developer' AND i.include_in_context=1;",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_audits WHERE "
                        "outcome='failed' AND guidance_version='6';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "status='passed';",
                        &value) && (value == 4LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "check_key='answer_non_empty' AND status='failed';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "status='not_applicable' AND "
                        "check_key='web_reference';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM sessions WHERE "
                        "name='Empty Answer Audit';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items i "
                        "JOIN message_items m ON m.item_id=i.id WHERE "
                        "m.role='assistant' AND "
                        "i.source_attempt_id IS NOT NULL;",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items i "
                        "JOIN message_items m ON m.item_id=i.id "
                        "JOIN item_text_parts p ON p.item_id=i.id WHERE "
                        "m.role='assistant' AND "
                        "i.introduced_request_id IS NOT NULL AND p.text='"
                        HARNESS_PERSONAL_PREFLIGHT_ASSISTANT_TEXT "';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM user_facts u "
                        "JOIN function_calls f ON f.item_id=u.source_item_id "
                        "JOIN conversation_items i ON i.id=f.item_id WHERE "
                        "u.value='Provenance fact.' AND "
                        "f.provider_call_id='call-empty-user-fact' AND "
                        "f.tool_name='memory_save' AND "
                        "i.session_id=(SELECT id FROM sessions LIMIT 1);",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM database_hints h "
                        "JOIN function_calls f ON f.item_id=h.source_item_id "
                        "JOIN conversation_items i ON i.id=f.item_id WHERE "
                        "h.content='Provenance hint.' AND "
                        "f.provider_call_id='call-empty-database-hint' AND "
                        "f.tool_name='database_study' AND "
                        "i.session_id=(SELECT id FROM sessions LIMIT 1);",
                        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Empty answer after tools quality check failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  free(error);
  unlink(database_path);
  unlink(path);
  return ok;
}

static int harness_test_web_search_requires_markdown_reference(void)
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
      !strappy_db_update_session_web_provider(path,
                                              session_id,
                                              STRAPPY_WEB_PROVIDER_AUTO,
                                              &error) ||
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
    "../shared/Resources",
    path,
    session_id,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) &&
    (strcmp(result,
            "Plain https://example.com, [FTP](ftp://example.com), "
            "![Image](https://example.com/image.png), and "
            "[Empty](https://) are not references.") == 0) &&
    server_ok;
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM http_attempts;",
                           &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "web_provider='auto';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items WHERE "
                        "kind='openrouter:web_search' AND include_in_context=1;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_audits WHERE "
                        "outcome='failed';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "check_key='web_reference' AND status='failed' AND "
                        "instr(detail,'linked HTTP source reference')>0;",
                        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Web-search reference quality check failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_valid_web_reference_passes_content_check(void)
{
  char path[] = "/tmp/strappy-responses-web-reference-XXXXXX";
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
    return harness_fail("Could not create web-reference harness database.");
  }
  close(fd);
  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_WEB_REFERENCE_VALID,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare valid web-reference integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  result = strappy_responses_send_prompt_for_session_and_store(
    "Use a cited server tool",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) &&
    (strcmp(result,
            "Read [Example source](http://example.com/article).") == 0) &&
    server_ok;
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM http_attempts;",
                           &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items WHERE "
                        "kind='openrouter:web_fetch' AND include_in_context=1;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_audits WHERE "
                        "outcome='failed';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "check_key='web_reference' AND status='passed';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "status='failed';",
                        &value) && (value == 3LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Valid web-reference quality check failed: %s\n",
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
  strappy_session_message_record_list timeline;
  harness_ledger_event_recorder events;
  size_t timeline_index;
  pid_t server_pid;
  int saw_named_output;
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
  strappy_session_message_record_list_init(&timeline);
  if (!harness_create_session_database(path, &session_id, &error) ||
      !strappy_db_update_session_web_provider(path,
                                              session_id,
                                              STRAPPY_WEB_PROVIDER_NONE,
                                              &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_FUNCTION_TOOL,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare function-tool integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    strappy_session_message_record_list_destroy(&timeline);
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
    "../shared/Resources",
    path,
    session_id,
    harness_record_ledger_event,
    &events,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) &&
    (strcmp(result, "Function tool final answer.") == 0) &&
    server_ok && events.valid && (events.count == 2LL) &&
    (events.answer_quality_count == 1LL) && events.saw_thinking &&
    events.saw_tools && (events.clear_count == 1L);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM http_attempts;",
                           &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "web_provider!='none';",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "request_kind='tool_continuation';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM tool_executions e "
                        "JOIN function_calls f ON "
                        "f.item_id=e.function_call_item_id WHERE "
                        "f.provider_call_id='call-database-context-error' AND "
                        "f.tool_name='database_context' AND "
                        "e.state='error' AND "
                        "e.error_message='database_context does not "
                        "accept argument ''unexpected''.';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM function_outputs o "
                        "JOIN function_calls f ON "
                        "f.item_id=o.function_call_item_id "
                        "JOIN conversation_items i ON i.id=o.item_id WHERE "
                        "f.provider_call_id='call-database-context-error' AND "
                        "o.execution_state='error' AND "
                        "o.started_at_ms IS NOT NULL AND "
                        "o.completed_at_ms >= o.started_at_ms AND "
                        "o.error_message='database_context does not "
                        "accept argument ''unexpected''.' AND i.is_error=1 AND "
                        "i.include_in_context=1;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items i "
                        "JOIN message_items m ON m.item_id=i.id WHERE "
                        "m.role='developer' AND i.include_in_context=1;",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_audits WHERE "
                        "outcome='failed';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "status='failed';",
                        &value) && (value == 3LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  saw_named_output = 0;
  if (ok) {
    ok = strappy_db_list_response_timeline(path,
                                           session_id,
                                           &timeline,
                                           &error);
  }
  for (timeline_index = 0U;
       ok && (timeline_index < timeline.count);
       timeline_index++) {
    const strappy_session_message_record *record;

    record = &timeline.records[timeline_index];
    if ((record->kind != NULL) &&
        (strcmp(record->kind, "function_call_output") == 0) &&
        (record->tool_call_id != NULL) &&
        (strcmp(record->tool_call_id,
                "call-database-context-error") == 0)) {
      saw_named_output = (record->tool_name != NULL) &&
        (strcmp(record->tool_name, "database_context") == 0);
    }
  }
  if (ok && !saw_named_output) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Function-tool continuation failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  strappy_session_message_record_list_destroy(&timeline);
  free(error);
  unlink(path);
  return ok;
}

static int harness_file_content_equals(const char *path,
                                       const char *expected)
{
  FILE *file;
  char buffer[256];
  size_t expected_length;
  size_t actual_length;
  int ok;

  expected_length = strlen(expected);
  if (expected_length >= sizeof(buffer)) {
    return 0;
  }
  file = fopen(path, "rb");
  if (file == NULL) {
    return 0;
  }
  actual_length = fread(buffer, 1U, sizeof(buffer), file);
  ok = !ferror(file) && (fclose(file) == 0) &&
    (actual_length == expected_length) &&
    (memcmp(buffer, expected, expected_length) == 0);
  return ok;
}

static int harness_test_file_mutation_continuation(void)
{
  char path[] = "/tmp/strappy-responses-file-mutation-XXXXXX";
  char working_directory[] = "/tmp/strappy-file-mutation-work-XXXXXX";
  char file_path[256];
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  long long session_id;
  long long value;
  pid_t server_pid;
  int fd;
  int written;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create file mutation response database.");
  }
  close(fd);
  if (mkdtemp(working_directory) == NULL) {
    unlink(path);
    return harness_fail("Could not create file mutation working directory.");
  }
  written = snprintf(file_path,
                     sizeof(file_path),
                     "%s/response_mutation.txt",
                     working_directory);
  if ((written <= 0) || ((size_t)written >= sizeof(file_path))) {
    rmdir(working_directory);
    unlink(path);
    return harness_fail("Could not build file mutation response path.");
  }

  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !strappy_db_update_session_assistant_set(
        path,
        session_id,
        STRAPPY_ASSISTANT_SET_CODING_ASSISTANT,
        &error) ||
      !strappy_db_update_session_working_directory(path,
                                                   session_id,
                                                   working_directory,
                                                   &error) ||
      !strappy_db_update_session_web_provider(path,
                                              session_id,
                                              STRAPPY_WEB_PROVIDER_NONE,
                                              &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_FILE_MUTATION,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare file mutation continuation test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(file_path);
    rmdir(working_directory);
    unlink(path);
    return 0;
  }

  result = strappy_responses_send_prompt_for_session_and_store(
    "Mutate a file",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) &&
    (strcmp(result, "File mutation final answer.") == 0) &&
    server_ok && (error == NULL) &&
    harness_file_content_equals(file_path, "first\nchanged\n");
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM tool_executions e "
                           "JOIN function_calls f ON "
                           "f.item_id=e.function_call_item_id WHERE "
                           "f.tool_name IN ('file_write','file_edit') AND "
                           "e.state='completed' AND e.error_message IS NULL;",
                           &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM function_outputs o "
                        "JOIN function_calls f ON "
                        "f.item_id=o.function_call_item_id "
                        "JOIN conversation_items i ON i.id=o.item_id WHERE "
                        "f.tool_name IN ('file_write','file_edit') AND "
                        "o.execution_state='completed' AND "
                        "o.output_format='structured' AND i.is_error=0;",
                        &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM structured_documents d "
                        "JOIN function_outputs o ON o.item_id=d.owner_item_id "
                        "JOIN function_calls f ON "
                        "f.item_id=o.function_call_item_id "
                        "JOIN structured_nodes n ON "
                        "n.document_id=d.id AND n.node_id=0 WHERE "
                        "f.tool_name IN ('file_write','file_edit') AND "
                        "d.purpose='output' AND n.value_type='object' AND "
                        "NOT EXISTS (SELECT 1 FROM structured_nodes child "
                        "WHERE child.document_id=d.id AND "
                        "child.parent_node_id=0);",
                        &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM structured_documents d "
                        "JOIN function_calls f ON f.item_id=d.owner_item_id "
                        "WHERE f.tool_name IN ('file_write','file_edit') "
                        "AND d.purpose='arguments';",
                        &value) && (value == 2LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "File mutation continuation failed: %s\n",
            (error != NULL) ? error : "request, file, or ledger mismatch");
  }
  free(error);
  unlink(file_path);
  rmdir(working_directory);
  unlink(path);
  return ok;
}

static int harness_test_bash_disabled_request(void)
{
  char path[] = "/tmp/strappy-responses-bash-disabled-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  long long session_id;
  long long value;
  pid_t server_pid;
  int bash_enabled;
  int fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create Bash-disabled database.");
  }
  close(fd);
  error = NULL;
  session_id = 0LL;
  bash_enabled = 1;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !strappy_db_update_session_assistant_set(
        path,
        session_id,
        STRAPPY_ASSISTANT_SET_CODING_ASSISTANT,
        &error) ||
      !strappy_db_update_session_web_provider(path,
                                              session_id,
                                              STRAPPY_WEB_PROVIDER_NONE,
                                              &error) ||
      !strappy_db_get_session_bash_enabled(path,
                                           session_id,
                                           &bash_enabled,
                                           &error) ||
      bash_enabled ||
      !harness_start_server(
        HARNESS_RESPONSES_SERVER_CODING_BASH_DISABLED,
        endpoint,
        sizeof(endpoint),
        &server_pid)) {
    fprintf(stderr,
            "Could not prepare Bash-disabled integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  result = strappy_responses_send_prompt_for_session_and_store(
    "Keep bash disabled",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) && (strcmp(result, "Bash disabled.") == 0) &&
    server_ok && (error == NULL);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM toolset_members "
                           "WHERE tool_name='bash';",
                           &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM toolset_members "
                        "WHERE tool_name='file_read';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM session_settings WHERE "
                        "session_id > 0 AND bash_enabled=0;",
                        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Disabled Bash leaked into a Coding Assistant request: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_bash_output_truncation_flag(void)
{
  char path[] = "/tmp/strappy-responses-bash-output-XXXXXX";
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
    return harness_fail("Could not create Bash output database.");
  }
  close(fd);
  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !strappy_db_update_session_assistant_set(
        path,
        session_id,
        STRAPPY_ASSISTANT_SET_CODING_ASSISTANT,
        &error) ||
      !strappy_db_update_session_bash_enabled(path,
                                              session_id,
                                              1,
                                              &error) ||
      !strappy_db_update_session_web_provider(path,
                                              session_id,
                                              STRAPPY_WEB_PROVIDER_NONE,
                                              &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_BASH_OUTPUT,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare Bash output integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  memset(&events, 0, sizeof(events));
  events.db_path = path;
  events.valid = 1;
  result = strappy_responses_send_prompt_for_session_and_store_with_events(
    "Report bash truncation",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    harness_record_ledger_event,
    &events,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) &&
    (strcmp(result, "Bash output flag final answer.") == 0) && server_ok &&
    events.valid && (events.count == 2LL) && events.saw_tools &&
    (events.clear_count == 1L);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM tool_executions e "
                           "JOIN function_calls f ON "
                           "f.item_id=e.function_call_item_id WHERE "
                           "f.tool_name='bash' AND e.state='completed';",
                           &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM tool_executions e "
                        "JOIN function_calls f ON "
                        "f.item_id=e.function_call_item_id WHERE "
                        "f.tool_name='bash' AND e.state='error';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM function_outputs o "
                        "JOIN function_calls f ON "
                        "f.item_id=o.function_call_item_id WHERE "
                        "f.tool_name='bash' AND "
                        "o.output_format='structured';",
                        &value) && (value == 4LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Bash output truncation flag failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_bash_tool_cancellation(void)
{
  char path[] = "/tmp/strappy-responses-bash-cancel-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  long long session_id;
  long long started_ms;
  long long elapsed_ms;
  long long value;
  harness_ledger_event_recorder events;
  pid_t server_pid;
  int fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create Bash cancellation database.");
  }
  close(fd);
  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !strappy_db_update_session_assistant_set(
        path,
        session_id,
        STRAPPY_ASSISTANT_SET_CODING_ASSISTANT,
        &error) ||
      !strappy_db_update_session_bash_enabled(path,
                                              session_id,
                                              1,
                                              &error) ||
      !strappy_db_update_session_web_provider(path,
                                              session_id,
                                              STRAPPY_WEB_PROVIDER_NONE,
                                              &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_BASH_CANCELLATION,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare Bash cancellation integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  memset(&events, 0, sizeof(events));
  events.db_path = path;
  events.valid = 1;
  events.cancel_after_ms = 250LL;
  started_ms = harness_now_ms();
  result = strappy_responses_send_prompt_for_session_and_store_with_events(
    "Cancel bash tool",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    harness_record_ledger_event,
    &events,
    &error);
  elapsed_ms = harness_now_ms() - started_ms;
  server_ok = harness_wait_for_server(server_pid, result != NULL);
  ok = (result == NULL) && server_ok && events.valid &&
    events.saw_cancellation_poll && events.saw_tools &&
    (events.count == 1LL) && (events.clear_count == 1L) &&
    (error != NULL) && (strstr(error, "cancelled") != NULL) &&
    (elapsed_ms < 3000LL);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM http_attempts;",
                           &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "request_kind='tool_continuation';",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM tool_executions e "
                        "JOIN function_calls f ON "
                        "f.item_id=e.function_call_item_id WHERE "
                        "f.provider_call_id='call-bash-cancel' AND "
                        "f.tool_name='bash' AND e.state='cancelled' AND "
                        "e.completed_at_ms >= e.started_at_ms AND "
                        "e.error_message LIKE '%Command aborted.%';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM function_outputs o "
                        "JOIN function_calls f ON "
                        "f.item_id=o.function_call_item_id "
                        "JOIN conversation_items i ON i.id=o.item_id WHERE "
                        "f.provider_call_id='call-bash-cancel' AND "
                        "o.execution_state='cancelled' AND "
                        "o.output_format='structured' AND "
                        "i.include_in_context=1 AND i.is_error=1 AND "
                        "i.introduced_request_id IS NULL;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM turns WHERE state='cancelled';",
                        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Bash tool cancellation failed after %lld ms: %s\n",
            elapsed_ms,
            (error != NULL) ? error : "request or ledger mismatch");
  }
  free(error);
  unlink(path);
  return ok;
}

static int harness_verify_timeline_hierarchy(
  const strappy_session_message_record_list *timeline,
  int require_retry);
static int harness_timeline_attempt_metadata_matches(
  const strappy_session_message_record_list *timeline,
  const char *state,
  int expect_metadata,
  const char *expected_text);

static int harness_test_retry_attempt_ledger(void)
{
  char path[] = "/tmp/strappy-responses-retry-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  strappy_session_message_record_list timeline;
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
  strappy_session_message_record_list_init(&timeline);
  if (!harness_create_session_database(path, &session_id, &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_RETRY,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare retry integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    strappy_session_message_record_list_destroy(&timeline);
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
    "../shared/Resources",
    path,
    session_id,
    harness_record_ledger_event,
    &events,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) &&
    (strcmp(result, "Retry final answer.") == 0) &&
    server_ok && events.valid && (events.count == 3LL) &&
    (events.answer_quality_count == 1LL) && events.saw_thinking &&
    events.saw_tools && events.saw_retry_wait && events.saw_retrying &&
    (events.clear_count == 1L);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM http_attempts;",
                           &value) && (value == 3LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM http_attempts a "
                        "JOIN model_requests r ON r.id=a.request_id WHERE "
                        "a.attempt_index=0 AND r.request_kind='user' AND "
                        "a.state='http_error' AND a.http_status=503 AND "
                        "r.new_input_from_sequence IS NOT NULL;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM http_attempts WHERE "
                        "attempt_index=1 AND state='completed' AND "
                        "http_status=200 AND previous_attempt_id IS NOT NULL;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM http_attempts a "
                        "JOIN http_attempts b ON b.previous_attempt_id=a.id "
                        "AND b.request_id=a.request_id;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items WHERE "
                        "include_in_context=1;",
                        &value) && (value == 9LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items WHERE "
                        "introduced_request_id IS NOT NULL AND "
                        "include_in_context=1;",
                        &value) && (value == 7LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_audits WHERE "
                        "outcome='failed';",
                        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (ok) {
    ok = strappy_db_list_response_timeline(path,
                                           session_id,
                                           &timeline,
                                           &error) &&
      harness_verify_timeline_hierarchy(&timeline, 1) &&
      harness_timeline_attempt_metadata_matches(&timeline,
                                                "http_error",
                                                1,
                                                "Provider is busy.");
  }
  if (!ok) {
    fprintf(stderr,
            "Retry attempt ledger failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  strappy_session_message_record_list_destroy(&timeline);
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
  strappy_session_message_record_list timeline;
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
  strappy_session_message_record_list_init(&timeline);
  if (!harness_create_session_database(path, &session_id, &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_SLOW,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare cancellation integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    strappy_session_message_record_list_destroy(&timeline);
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
    "../shared/Resources",
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
                           "SELECT COUNT(*) FROM http_attempts WHERE "
                           "state='cancelled' AND "
                           "transport_error LIKE '%cancelled%';",
                           &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM http_attempts;",
                        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (ok) {
    ok = strappy_db_list_response_timeline(path,
                                           session_id,
                                           &timeline,
                                           &error) &&
      harness_timeline_attempt_metadata_matches(&timeline,
                                                "cancelled",
                                                0,
                                                NULL);
  }
  if (!ok) {
    fprintf(stderr,
            "Active Responses cancellation failed: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
  strappy_session_message_record_list_destroy(&timeline);
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
    "../shared/Resources",
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
                           "SELECT COUNT(*) FROM http_attempts WHERE "
                           "state='http_error' AND http_status=503 AND "
                           "retry_after_seconds=120;",
                           &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM http_attempts;",
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
    "SELECT r.model_id, r.stream_enabled, ir.text, ar.provider_response_id, "
    "ar.provider_status, ar.provider_model_id, u.input_tokens, "
    "u.cached_input_tokens, u.output_tokens, u.reasoning_tokens, "
    "u.total_tokens, u.cost_nano_usd, a.http_status, "
    "a.provider_request_id, a.rate_limit_remaining "
    "FROM http_attempts a JOIN model_requests r ON r.id=a.request_id "
    "LEFT JOIN instruction_revisions ir ON ir.id=r.instruction_revision_id "
    "LEFT JOIN api_results ar ON ar.attempt_id=a.id "
    "LEFT JOIN api_usage u ON u.attempt_id=a.id LIMIT 1;";
  sqlite3_stmt *stmt;
  long long raw_column_count;
  int rc;
  int ok;

  (void)request_json;
  (void)response_json;

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
    strcmp((const char *)sqlite3_column_text(stmt, 2), "System") == 0 &&
    strcmp((const char *)sqlite3_column_text(stmt, 3), "resp-test") == 0 &&
    strcmp((const char *)sqlite3_column_text(stmt, 4), "completed") == 0 &&
    strcmp((const char *)sqlite3_column_text(stmt, 5), "test/provider-model") == 0 &&
    sqlite3_column_int64(stmt, 6) == 11 &&
    sqlite3_column_int64(stmt, 7) == 3 &&
    sqlite3_column_int64(stmt, 8) == 22 &&
    sqlite3_column_int64(stmt, 9) == 7 &&
    sqlite3_column_int64(stmt, 10) == 33 &&
    sqlite3_column_int64(stmt, 11) == 1200000 &&
    sqlite3_column_int64(stmt, 12) == 200 &&
    strcmp((const char *)sqlite3_column_text(stmt, 13), "req-test") == 0 &&
    strcmp((const char *)sqlite3_column_text(stmt, 14), "9") == 0;
  sqlite3_finalize(stmt);
  raw_column_count = 0LL;
  return ok &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM sqlite_master WHERE type='table' "
                      "AND lower(sql) LIKE '%raw_json%';",
                      &raw_column_count) &&
    (raw_column_count == 0LL);
}

static int harness_verify_timeline_hierarchy(
  const strappy_session_message_record_list *timeline,
  int require_retry)
{
  long long model_request_id;
  long long attempt_id;
  long attempt_index;
  int saw_attempt;
  int saw_retry;
  size_t index;

  if (timeline == NULL) {
    return 0;
  }
  model_request_id = 0LL;
  attempt_id = 0LL;
  attempt_index = -1L;
  saw_attempt = 0;
  saw_retry = 0;
  for (index = 0U; index < timeline->count; index++) {
    const strappy_session_message_record *record;

    record = &timeline->records[index];
    if (record->model_request_id != model_request_id) {
      if ((model_request_id > 0LL) &&
          (record->model_request_id <= model_request_id)) {
        return 0;
      }
      model_request_id = record->model_request_id;
      attempt_id = 0LL;
      attempt_index = -1L;
      saw_attempt = 0;
    }
    if ((record->direction != NULL) &&
        (strcmp(record->direction, "request") == 0)) {
      if (saw_attempt || (record->http_attempt_id != 0LL) ||
          (record->attempt_index != -1L)) {
        return 0;
      }
    } else if ((record->kind != NULL) &&
               (strcmp(record->kind, "response_api_call") == 0)) {
      if ((record->http_attempt_id <= 0LL) ||
          (record->attempt_index < attempt_index)) {
        return 0;
      }
      if (record->attempt_index > 0L) {
        saw_retry = 1;
      }
      saw_attempt = 1;
      attempt_id = record->http_attempt_id;
      attempt_index = record->attempt_index;
    } else if ((record->direction != NULL) &&
               (strcmp(record->direction, "response") == 0)) {
      if (!saw_attempt || (record->http_attempt_id != attempt_id) ||
          (record->attempt_index != attempt_index)) {
        return 0;
      }
    } else {
      return 0;
    }
  }
  return !require_retry || saw_retry;
}

static int harness_timeline_attempt_metadata_matches(
  const strappy_session_message_record_list *timeline,
  const char *state,
  int expect_metadata,
  const char *expected_text)
{
  size_t index;

  if ((timeline == NULL) || (state == NULL)) {
    return 0;
  }
  for (index = 0U; index < timeline->count; index++) {
    const strappy_session_message_record *record;
    int has_metadata;

    record = &timeline->records[index];
    if ((record->kind == NULL) ||
        (strcmp(record->kind, "response_api_call") != 0) ||
        (record->attempt_state == NULL) ||
        (strcmp(record->attempt_state, state) != 0)) {
      continue;
    }
    has_metadata = (record->metadata_json != NULL) &&
      (record->metadata_json[0] != '\0');
    if (has_metadata != (expect_metadata ? 1 : 0)) {
      return 0;
    }
    return (expected_text == NULL) ||
      (has_metadata && (strstr(record->metadata_json, expected_text) != NULL));
  }
  return 0;
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
  strappy_session_message_record_list ranged_timeline;
  strappy_session_message_record_list timeline;
  char *error;
  long long session_id;
  long long previous_call_id;
  size_t total_count;
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
  strappy_session_message_record_list_init(&ranged_timeline);
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
    (strcmp(timeline.records[0].attempt_state, "completed") == 0) &&
    harness_double_matches(timeline.records[0].cumulative_usage_cost, 0.001) &&
    timeline.records[1].has_cumulative_usage_cost &&
    timeline.records[1].is_error &&
    (strcmp(timeline.records[1].attempt_state, "response_error") == 0) &&
    harness_double_matches(timeline.records[1].cumulative_usage_cost, 0.0075) &&
    (timeline.records[2].round_index == 1L) &&
    (timeline.records[2].attempt_index == 1L) &&
    (timeline.records[2].model_request_id ==
     timeline.records[1].model_request_id) &&
    (timeline.records[2].http_attempt_id !=
     timeline.records[1].http_attempt_id) &&
    (strcmp(timeline.records[2].attempt_state, "completed") == 0) &&
    harness_double_matches(timeline.records[2].cumulative_usage_cost, 0.0075) &&
    (strcmp(timeline.records[3].prompt_group_key, "group-two") == 0) &&
    (timeline.records[3].round_index == 0L) &&
    harness_double_matches(timeline.records[3].cumulative_usage_cost, 0.0075) &&
    harness_double_matches(timeline.records[4].cumulative_usage_cost, 0.008);

  total_count = 0U;
  if (ok) {
    ok = strappy_db_list_response_timeline_range(path,
                                                 session_id,
                                                 2U,
                                                 &ranged_timeline,
                                                 &total_count,
                                                 &error) &&
      (total_count == 5U) &&
      (ranged_timeline.count == 3U) &&
      harness_double_matches(
        ranged_timeline.records[0].cumulative_usage_cost, 0.0075) &&
      harness_double_matches(
        ranged_timeline.records[1].cumulative_usage_cost, 0.0075) &&
      harness_double_matches(
        ranged_timeline.records[2].cumulative_usage_cost, 0.008);
  }

  if (!ok) {
    fprintf(stderr,
            "Cumulative session usage cost failed: %s\n",
            (error != NULL) ? error : "timeline total mismatch");
  }
  strappy_session_message_record_list_destroy(&ranged_timeline);
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
    "\"call_id\":\"call-test\",\"name\":\"database_list\","
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
                      "SELECT COUNT(*) FROM conversation_items;",
                      &value) && (value == 6LL) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM item_text_parts;",
                      &value) && (value == 3LL) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM function_calls f "
                      "JOIN structured_documents d ON d.owner_item_id=f.item_id "
                      "JOIN structured_nodes n ON n.document_id=d.id "
                      "WHERE f.provider_call_id='call-test' AND "
                      "f.tool_name='database_list' AND "
                      "f.tool_namespace='local' AND n.node_id=0 "
                      "AND n.value_type='object';",
                      &value) && (value == 1LL) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM reasoning_items WHERE "
                      "provider_format='test-v1' AND "
                      "provider_signature='sig-test';",
                      &value) && (value == 1LL) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM web_searches w "
                      "JOIN web_search_sources s ON "
                      "s.web_search_item_id=w.item_id WHERE "
                      "w.action_type='search' AND w.query='Strappy Cocoa' "
                      "AND s.source_type='url' AND "
                      "s.url='https://example.com/search';",
                      &value) && (value == 1LL) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM web_fetches WHERE "
                      "url='https://example.com/article' AND "
                      "title='Example Article' AND http_status=200;",
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
    harness_verify_timeline_hierarchy(&timeline, 0) &&
    (timeline.count == 7U) &&
    (strcmp(timeline.records[0].role, "user") == 0) &&
    (strcmp(timeline.records[0].direction, "request") == 0) &&
    (timeline.records[0].round_index == 0L) &&
    (timeline.records[0].attempt_index == -1L) &&
    (timeline.records[0].model_request_id > 0LL) &&
    (timeline.records[0].http_attempt_id == 0LL) &&
    (strcmp(timeline.records[1].role, "api_call") == 0) &&
    (timeline.records[1].direction == NULL) &&
    (timeline.records[1].model_request_id ==
     timeline.records[0].model_request_id) &&
    (timeline.records[1].http_attempt_id == call_id) &&
    (timeline.records[1].round_index == 0L) &&
    (timeline.records[1].attempt_index == 0L) &&
    (strcmp(timeline.records[1].attempt_state, "completed") == 0) &&
    (strcmp(timeline.records[1].request_method, "POST") == 0) &&
    (strcmp(timeline.records[1].request_endpoint,
            "https://openrouter.ai/api/v1/responses") == 0) &&
    (strstr(timeline.records[1].metadata_json,
            "\"id\":\"resp-test\"") != NULL) &&
    (strstr(timeline.records[1].metadata_json, "\"usage\"") != NULL) &&
    (strstr(timeline.records[1].metadata_json, "http_status") == NULL) &&
    (strstr(timeline.records[1].content, "Model: test/model") != NULL) &&
    (strstr(timeline.records[1].content,
            "Request: POST https://openrouter.ai/api/v1/responses") != NULL) &&
    (strstr(timeline.records[1].content, "HTTP 200") == NULL) &&
    (strstr(timeline.records[1].content, "completed") == NULL) &&
    (strcmp(timeline.records[2].role, "api_reasoning") == 0) &&
    (strcmp(timeline.records[2].direction, "response") == 0) &&
    (timeline.records[2].http_attempt_id == call_id) &&
    (strcmp(timeline.records[3].role, "api_function_call") == 0) &&
    (strcmp(timeline.records[3].direction, "response") == 0) &&
    (strcmp(timeline.records[4].role, "api_item") == 0) &&
    (strcmp(timeline.records[4].kind,
            STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) == 0) &&
    (strcmp(timeline.records[4].response_item_action_json,
            "{\"type\":\"search\",\"query\":\"Strappy Cocoa\","
            "\"sources\":[{\"type\":\"url\","
            "\"url\":\"https://example.com/search\"}]}") == 0) &&
    (strcmp(timeline.records[5].role, "api_item") == 0) &&
    (strcmp(timeline.records[5].kind,
            STRAPPY_TOOL_OPENROUTER_WEB_FETCH) == 0) &&
    (strcmp(timeline.records[5].response_item_url,
            "https://example.com/article") == 0) &&
    (strcmp(timeline.records[5].response_item_title,
            "Example Article") == 0) &&
    (strcmp(timeline.records[5].response_item_status, "completed") == 0) &&
    (strcmp(timeline.records[5].response_item_http_status, "200") == 0) &&
    (strstr(timeline.records[5].response_item_title,
            "Fetched page body") == NULL) &&
    (strcmp(timeline.records[6].role, "assistant") == 0) &&
    (strcmp(timeline.records[6].direction, "response") == 0) &&
    (strcmp(timeline.records[6].content, "Done") == 0);
  if (!ok) {
    size_t timeline_index;

    for (timeline_index = 0U;
         timeline_index < timeline.count;
         timeline_index++) {
      fprintf(stderr,
              "timeline[%lu] role=%s direction=%s turn=%lld request=%lld "
              "attempt=%lld round=%ld attempt_index=%ld state=%s\n",
              (unsigned long)timeline_index,
              (timeline.records[timeline_index].role != NULL) ?
                timeline.records[timeline_index].role : "(null)",
              (timeline.records[timeline_index].direction != NULL) ?
                timeline.records[timeline_index].direction : "(null)",
              timeline.records[timeline_index].turn_id,
              timeline.records[timeline_index].model_request_id,
              timeline.records[timeline_index].http_attempt_id,
              timeline.records[timeline_index].round_index,
              timeline.records[timeline_index].attempt_index,
              (timeline.records[timeline_index].attempt_state != NULL) ?
                timeline.records[timeline_index].attempt_state : "(null)");
    }
    fprintf(stderr, "Responses timeline failed: %s\n", error);
    strappy_session_message_record_list_destroy(&timeline);
    free(error);
    unlink(path);
    return 0;
  }
  strappy_session_message_record_list_destroy(&timeline);

  memset(&execution, 0, sizeof(execution));
  execution.session_id = session_id;
  execution.response_call_id = call_id;
  execution.output_index = 1L;
  execution.call_id = "call-test";
  execution.tool_name = "database_list";
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
                           "SELECT COUNT(*) FROM http_attempts;",
                           &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM tool_executions;",
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

static int harness_test_session_webview_rendering(void)
{
  static const char *first_text = "First stored WebView message";
  static const char *second_text = "Second stored WebView message";
  static const char *request_json =
    "{\"model\":\"test/model\",\"stream\":false,\"store\":false,"
    "\"input\":[{\"type\":\"message\",\"role\":\"user\","
    "\"content\":[{\"type\":\"input_text\","
    "\"text\":\"First stored WebView message\"}]}]}";
  static const char *response_json =
    "{\"id\":\"resp-webview\",\"object\":\"response\","
    "\"created_at\":1700000000,\"completed_at\":1700000001,"
    "\"model\":\"test/model\",\"status\":\"completed\","
    "\"output\":[{\"type\":\"message\",\"id\":\"msg-webview\","
    "\"role\":\"assistant\",\"status\":\"completed\","
    "\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Second stored WebView message\","
    "\"annotations\":[],\"logprobs\":[]}]}],"
    "\"output_text\":\"Second stored WebView message\","
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":5,"
    "\"total_tokens\":9}}";
  const char *path = "/tmp/strappy_session_webview_harness.sqlite";
  strappy_response_call_begin_input begin;
  strappy_response_call_finish_input finish;
  strappy_session_message_record_list range;
  const char *first_position;
  const char *second_position;
  char *append_script;
  char *empty_script;
  char *error;
  char *invalid_script;
  char *page_html;
  long long call_id;
  long long session_id;
  size_t message_count;
  size_t total_count;
  int ok;

  unlink(path);
  append_script = NULL;
  call_id = 0LL;
  empty_script = NULL;
  error = NULL;
  invalid_script = NULL;
  page_html = NULL;
  session_id = 0LL;
  message_count = 0U;
  total_count = 0U;
  strappy_session_message_record_list_init(&range);
  ok = strappy_webview_configure_localized_labels(&error) &&
       strappy_db_create_session(path, &session_id, &error);
  if (!ok) {
    fprintf(stderr,
            "Could not create the session WebView fixture: %s\n",
            (error != NULL) ? error : "unknown error");
    goto cleanup;
  }

  memset(&begin, 0, sizeof(begin));
  begin.session_id = session_id;
  begin.prompt_group_key = "webview-group";
  begin.request_kind = "user";
  begin.round_index = 0L;
  begin.attempt_index = 0L;
  begin.new_input_start_index = 0L;
  begin.request_method = "POST";
  begin.request_url = "https://openrouter.ai/api/v1/responses";
  begin.request_headers_json = "{}";
  begin.request_json = request_json;
  ok = strappy_db_begin_response_call(path, &begin, &call_id, &error);
  if (!ok) {
    fprintf(stderr,
            "Could not begin the session WebView fixture call: %s\n",
            (error != NULL) ? error : "unknown error");
    goto cleanup;
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
  finish.response_headers = "";
  finish.response_json = response_json;
  ok = strappy_db_finish_response_call(path, &finish, &error);
  if (!ok) {
    fprintf(stderr,
            "Could not finish the session WebView fixture call: %s\n",
            (error != NULL) ? error : "unknown error");
    goto cleanup;
  }

  ok = strappy_db_list_response_timeline_range(path,
                                                session_id,
                                                2U,
                                                &range,
                                                &total_count,
                                                &error);
  ok = ok && (total_count == 3U) && (range.count == 1U) &&
       (range.records[0].content != NULL) &&
       (strcmp(range.records[0].content, second_text) == 0);
  strappy_session_message_record_list_destroy(&range);
  if (!ok) {
    fprintf(stderr,
            "Could not read the ranged Responses timeline: %s\n",
            (error != NULL) ? error : "unexpected output");
    goto cleanup;
  }

  page_html = strappy_session_webview_messages_page_html_for_session(
    path,
    session_id,
    "../shared/Resources",
    NULL,
    &message_count,
    &error);
  first_position = (page_html != NULL) ?
    strstr(page_html, first_text) : NULL;
  second_position = (page_html != NULL) ?
    strstr(page_html, second_text) : NULL;
  ok = (page_html != NULL) && (message_count == 3U) &&
       (first_position != NULL) && (second_position != NULL) &&
       (first_position < second_position);
  if (!ok) {
    fprintf(stderr,
            "Could not render the stored WebView page: %s "
            "(count=%lu, first=%d, second=%d)\n",
            (error != NULL) ? error : "unexpected output",
            (unsigned long)message_count,
            (first_position != NULL) ? 1 : 0,
            (second_position != NULL) ? 1 : 0);
    goto cleanup;
  }

  append_script = strappy_session_webview_append_messages_js_for_session(
    path,
    session_id,
    2U,
    &message_count,
    &error);
  ok = (append_script != NULL) && (message_count == 3U) &&
       (strstr(append_script, "appendMessage(") != NULL) &&
       (strstr(append_script, first_text) == NULL) &&
       (strstr(append_script, second_text) != NULL);
  if (!ok) {
    fprintf(stderr,
            "Could not render stored WebView append JavaScript: %s\n",
            (error != NULL) ? error : "unexpected output");
    goto cleanup;
  }

  empty_script = strappy_session_webview_append_messages_js_for_session(
    path,
    session_id,
    3U,
    &message_count,
    &error);
  ok = (empty_script != NULL) && (message_count == 3U) &&
       (strcmp(empty_script, "") == 0);
  if (!ok) {
    fprintf(stderr,
            "Empty stored WebView append range was not empty: %s\n",
            (error != NULL) ? error : "unexpected output");
    goto cleanup;
  }

  strappy_session_free_string(error);
  error = NULL;
  invalid_script = strappy_session_webview_append_messages_js_for_session(
    path,
    session_id,
    4U,
    &message_count,
    &error);
  ok = (invalid_script == NULL) && (message_count == 3U) && (error != NULL);
  if (!ok) {
    fprintf(stderr,
            "Invalid stored WebView append range unexpectedly succeeded.\n");
  }

cleanup:
  strappy_session_message_record_list_destroy(&range);
  strappy_session_free_string(page_html);
  strappy_session_free_string(append_script);
  strappy_session_free_string(empty_script);
  strappy_session_free_string(invalid_script);
  strappy_session_free_string(error);
  unlink(path);
  return ok;
}

int main(void)
{
  if (harness_test_unicode_emoji_scan() &&
      harness_test_request_surfaces() &&
      harness_test_ledger() &&
      harness_test_cumulative_session_usage_cost() &&
      harness_test_answer_quality_report() &&
      harness_test_world_knowledge_assistant_set() &&
      harness_test_preflight_runs_only_on_first_prompt() &&
      harness_test_empty_answer_quality_report() &&
      harness_test_empty_answer_after_tools_quality_report() &&
      harness_test_web_search_requires_markdown_reference() &&
      harness_test_valid_web_reference_passes_content_check() &&
      harness_test_function_tool_continuation() &&
      harness_test_file_mutation_continuation() &&
      harness_test_bash_disabled_request() &&
      harness_test_bash_output_truncation_flag() &&
      harness_test_bash_tool_cancellation() &&
      harness_test_retry_attempt_ledger() &&
      harness_test_active_request_cancellation() &&
      harness_test_retry_after_clamp_and_cancellation() &&
      harness_test_session_webview_rendering()) {
    printf("responses_harness passed.\n");
    return 0;
  }
  fprintf(stderr, "responses_harness failed.\n");
  return 1;
}
