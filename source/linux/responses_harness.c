#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <cJSON.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../shared/strappy_client.h"
#include "../shared/strappy_assistant_sets.h"
#include "../shared/strappy_config.h"
#include "../shared/strappy_core.h"
#include "../shared/strappy_db.h"
#include "../shared/strappy_identity.h"
#include "../shared/strappy_prompt.h"
#include "../shared/strappy_quality_policy.h"
#include "../shared/strappy_responses.h"
#include "../shared/strappy_session.h"
#include "../shared/strappy_tools.h"

#define HARNESS_SKILLS_LIST_EMPTY_RESULT \
  "{\"skills\":[],\"guidance\":\"No skills available\"}"

#define HARNESS_IOS_DEVELOPMENT_SKILL_DESCRIPTION \
  "Create, build, inspect, repair, analyze, package, and optionally run " \
  "native Objective-C and C iOS applications on-device with " \
  "AltivecToolchain."

#define HARNESS_MACOS_DEVELOPMENT_SKILL_DESCRIPTION \
  "Build, inspect, repair, analyze, package, and optionally run native " \
  "Objective-C and C applications locally on Mac OS X or macOS using only " \
  "the SDKs, compilers, libraries, and package-manager tools already " \
  "installed by the user. Use this skill for AppKit applications, legacy " \
  "Cocoa compatibility, native-architecture Makefiles, `.app` bundles, and " \
  "portable C shared with Cocoa code."

#define HARNESS_MEMORY_USER_FACT_REMEMBER_DESCRIPTION \
  "Call this tool to save a useful durable user fact for future prompts. " \
  "NEVER store secrets or sensitive information."

#define HARNESS_MEMORY_USER_FACT_READ_DESCRIPTION \
  "Call this tool to retrieve durable facts stored about the user."

#define HARNESS_DATABASE_LIST_INFO_DESCRIPTION \
  "Call this tool to view approved databases. Returns an object containing " \
  "a databases array with database_id, description, app_name, path, " \
  "size_bytes, and modified_at in Unix seconds. description is null until " \
  "the current database file has been studied. When no databases are " \
  "approved, the array is empty and guidance explains why."

#define HARNESS_DATABASE_QUERY_DESCRIPTION \
  "Run one read-only SQLite query against an approved database. Use it " \
  "whenever an answer depends on personal data. In Database Study, use " \
  "schema queries to design access recipes, then execute every final recipe " \
  "exactly as it will be saved. A sqlite_schema query or generic SELECT * " \
  "sampling query is not a recipe. Returns ordered column names and positional " \
  "rows, limited to 100 rows and 64 columns."
#define HARNESS_DATABASE_QUERY_DATABASE_ID_DESCRIPTION \
  "Approved database ID returned by database_list."
#define HARNESS_DATABASE_QUERY_SQL_DESCRIPTION \
  "One read-only SQLite SELECT or EXPLAIN query that returns columns. Select " \
  "only needed columns and use LIMIT when practical. Query sqlite_schema for " \
  "schema details. PRAGMA, writes, ATTACH, multiple statements, and bind " \
  "parameters are not allowed."

#define HARNESS_DATABASE_CONTEXT_READ_DESCRIPTION \
  "Call this first for a relevant approved database_id. Returns the studied " \
  "context value (or null), bounded table and view names, and exploration " \
  "guidance. The table and view names are generated on demand; do not copy " \
  "their inventory into studied context. This result alone is not a completed " \
  "Database Study."

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
  "Save one completed Database Study result after investigation. Provide " \
  "description and context together in the same call. description is one " \
  "sentence explaining what useful user data exists. context is compact how-to " \
  "retrieval guidance, not a schema inventory. For each useful access pattern, " \
  "include its purpose and exact read-only SQL copied verbatim from a successful " \
  "database_query in the current study batch. SQL must run as written, use " \
  "literal limits, and contain no bind placeholders. Include required joins, " \
  "filters, ordering, non-obvious field semantics, verified timestamp units, " \
  "and caveats; mention only tables and columns used by the patterns. If no " \
  "useful user-facing data exists, say so instead of listing schema. Never " \
  "include sampled values, secrets, or sensitive identifiers."

#define HARNESS_DATETIME_FROM_ISO8601_DESCRIPTION \
  "ALWAYS call this tool when converting ISO 8601 datetimes to numeric " \
  "timestamps. Returns a JSON array of timestamp strings in input order."

#define HARNESS_BASH_DESCRIPTION \
  "Bash tool for executing commands on the device. Each tool call is always " \
  "a fresh session in the user's selected working directory. The shell state " \
  "does not persist between calls. stdin is closed, and stdout and stderr are " \
  "returned together in arrival order. NEVER delete user data or installed " \
  "applications. NEVER use rm, rm -r, rm -rf, find -delete, unlink, rmdir, " \
  "or an equivalent destructive operation on a target outside the " \
  "current working directory reported by pwd. A deletion target inside the " \
  "current working directory must resolve to a strict descendant of that " \
  "directory. Never target the working directory itself, a parent directory, " \
  "~, /Applications, /var/mobile/Applications, or a path that escapes through " \
  "a symlink. If the target or authorization is unclear, do not execute the " \
  "command and ask the user."

#define HARNESS_UNICODE_EMOJI_ANSWER \
  "Initial answer. \xF0\x9F\x98\x80"

#define HARNESS_WORLD_PREFLIGHT_ASSISTANT_TEXT \
  "Strappy here, babe. Facts, line up. Let me check my memories and tools " \
  "before I put the world in order."
#define HARNESS_PERSONAL_PREFLIGHT_ASSISTANT_TEXT \
  "Strappy here, babe. Databases, behave. Let me inspect my memories, skills, " \
  "and approved data before I interrogate the evidence."
#define HARNESS_CODING_PREFLIGHT_ASSISTANT_TEXT \
  "Strappy here, babe. Code, heel. Let me inspect the environment, review my " \
  "tools, and find out exactly what needs discipline."
#define HARNESS_DATABASE_STUDY_PREFLIGHT_ASSISTANT_TEXT \
  "Starting the internal database study workflow."

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

static int harness_tool_has_required_string_parameter(
  cJSON *tools,
  const char *tool_name,
  const char *parameter_name);
static cJSON *harness_tool_parameter_schema(cJSON *tools,
                                            const char *tool_name,
                                            const char *parameter_name);

static int harness_database_study_parameters_match_contract(cJSON *tools)
{
  cJSON *database_id;
  cJSON *description;
  cJSON *context;

  database_id = harness_tool_parameter_schema(tools,
                                              STRAPPY_TOOL_DATABASE_STUDY,
                                              "database_id");
  description = harness_tool_parameter_schema(tools,
                                              STRAPPY_TOOL_DATABASE_STUDY,
                                              "description");
  context = harness_tool_parameter_schema(tools,
                                          STRAPPY_TOOL_DATABASE_STUDY,
                                          "context");
  return cJSON_IsObject(database_id) && cJSON_IsObject(description) &&
    cJSON_IsObject(context) &&
    harness_tool_has_required_string_parameter(tools,
                                               STRAPPY_TOOL_DATABASE_STUDY,
                                               "database_id") &&
    harness_tool_has_required_string_parameter(tools,
                                               STRAPPY_TOOL_DATABASE_STUDY,
                                               "description") &&
    harness_tool_has_required_string_parameter(tools,
                                               STRAPPY_TOOL_DATABASE_STUDY,
                                               "context") &&
    (harness_tool_parameter_schema(tools,
                                   STRAPPY_TOOL_DATABASE_STUDY,
                                   "key") == NULL) &&
    (harness_tool_parameter_schema(tools,
                                   STRAPPY_TOOL_DATABASE_STUDY,
                                   "value") == NULL) &&
    (harness_tool_parameter_schema(tools,
                                   STRAPPY_TOOL_DATABASE_STUDY,
                                   "hint") == NULL);
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
    harness_has_tool_name(tools, STRAPPY_TOOL_SKILLS_LIST) &&
    harness_tool_has_required_string_parameter(
      tools,
      STRAPPY_TOOL_SKILL_READ,
      "skill_id") &&
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
    harness_tool_description_equals(tools,
                                    STRAPPY_TOOL_BASH,
                                    HARNESS_BASH_DESCRIPTION) &&
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
    harness_database_study_parameters_match_contract(tools) &&
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

  error = NULL;
  tools_json = strappy_tools_responses_request_json_filtered_for_provider(
    "../shared/Resources",
    NULL,
    0U,
    STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT,
    STRAPPY_WEB_PROVIDER_AUTO,
    &error);
  if (tools_json == NULL) {
    fprintf(stderr,
            "Could not build ChatGPT Responses tools: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }
  tools = cJSON_Parse(tools_json);
  free(tools_json);
  ok = cJSON_IsArray(tools) &&
    harness_has_tool_type(tools, STRAPPY_TOOL_WEB_SEARCH) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_FETCH);
  cJSON_Delete(tools);
  free(error);
  if (!ok) {
    return harness_fail("ChatGPT web-search schema was not provider-native.");
  }

  error = NULL;
  tools_json = strappy_tools_responses_request_json_filtered_for_provider(
    "../shared/Resources",
    NULL,
    0U,
    STRAPPY_PROVIDER_KIND_OTHER,
    STRAPPY_WEB_PROVIDER_AUTO,
    &error);
  if (tools_json == NULL) {
    fprintf(stderr,
            "Could not build generic Responses tools: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }
  tools = cJSON_Parse(tools_json);
  free(tools_json);
  ok = cJSON_IsArray(tools) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_DATETIME_TO_ISO8601) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_SESSION_RENAME) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_WEB_SEARCH) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_FETCH) &&
    harness_tools_hide_local_display_metadata(tools);
  cJSON_Delete(tools);
  free(error);
  if (!ok) {
    return harness_fail(
      "Generic Responses tools included unsupported hosted extras.");
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
  long long started_count;
  long long completion_event_count;
  long long coalesced_transition_count;
  long long coalescible_completion_count;
  long long tool_outputs_event_count;
  long long pending_presented_request_id;
  long long last_started_request_id;
  long long processing_count;
  long long processing_started_ms;
  long long answer_quality_count;
  long long wall_duration_update_count;
  long long message_update_required_count;
  long long message_update_skipped_count;
  long long terminal_count;
  long long event_sequence;
  long long terminal_call_id;
  long long terminal_ledger_sequence;
  long long terminal_wall_duration_sequence;
  long long terminal_clear_sequence;
  size_t timeline_count;
  long clear_count;
  int valid;
  int saw_timeline;
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

static int harness_verify_timeline_hierarchy(
  const strappy_session_message_record_list *timeline,
  int require_retry);

static int harness_ledger_timeline_matches(
  harness_ledger_event_recorder *recorder,
  long long session_id,
  long long request_id,
  long long call_id,
  long long attempt_index,
  int running,
  long long coalesced_previous_request_id)
{
  strappy_session_message_record_list timeline;
  char *error;
  size_t current_request_count;
  size_t current_attempt_count;
  size_t suffix_start;
  size_t index;
  int saw_coalesced_current_request;
  int saw_coalesced_previous_response;
  int ok;

  strappy_session_message_record_list_init(&timeline);
  error = NULL;
  current_request_count = 0U;
  current_attempt_count = 0U;
  saw_coalesced_current_request = 0;
  saw_coalesced_previous_response = 0;
  suffix_start = recorder->timeline_count;
  ok = strappy_db_list_response_timeline(recorder->db_path,
                                         session_id,
                                         &timeline,
                                         &error) &&
    harness_verify_timeline_hierarchy(&timeline, 0);
  if (!ok) {
    strappy_session_free_string(error);
    strappy_session_message_record_list_destroy(&timeline);
    return 0;
  }

  if (!recorder->saw_timeline) {
    suffix_start = timeline.count;
    for (index = 0U; index < timeline.count; index++) {
      if (timeline.records[index].model_request_id == request_id) {
        suffix_start = index;
        break;
      }
    }
  } else if (timeline.count < suffix_start) {
    ok = 0;
  }

  for (index = 0U; index < timeline.count; index++) {
    const strappy_session_message_record *record;

    record = &timeline.records[index];
    if ((record->model_request_id == request_id) &&
        (record->direction != NULL) &&
        (strcmp(record->direction, "request") == 0)) {
      current_request_count++;
    }
    if (record->http_attempt_id == call_id) {
      current_attempt_count++;
    }
  }

  if (running) {
    ok = ok && (current_request_count > 0U) &&
      (current_attempt_count == 0U);
    if (attempt_index == 0LL) {
      ok = ok && (suffix_start < timeline.count);
      if (coalesced_previous_request_id > 0LL) {
        ok = ok && (coalesced_previous_request_id != request_id);
        for (index = suffix_start; ok && (index < timeline.count); index++) {
          const strappy_session_message_record *record;

          record = &timeline.records[index];
          if (record->model_request_id == request_id) {
            saw_coalesced_current_request = 1;
            ok = (record->http_attempt_id == 0LL) &&
              (record->direction != NULL) &&
              (strcmp(record->direction, "request") == 0);
          } else if (record->model_request_id ==
                     coalesced_previous_request_id) {
            saw_coalesced_previous_response = 1;
            ok = (record->direction == NULL) ||
              (strcmp(record->direction, "request") != 0);
          } else {
            ok = 0;
          }
        }
        ok = ok && saw_coalesced_current_request &&
          saw_coalesced_previous_response;
      } else {
        for (index = suffix_start; ok && (index < timeline.count); index++) {
          const strappy_session_message_record *record;

          record = &timeline.records[index];
          ok = (record->model_request_id == request_id) &&
            (record->http_attempt_id == 0LL) &&
            (record->direction != NULL) &&
            (strcmp(record->direction, "request") == 0);
        }
      }
    } else {
      ok = ok && recorder->saw_timeline &&
        (timeline.count == suffix_start);
    }
  } else {
    ok = ok && recorder->saw_timeline &&
      (current_attempt_count > 0U) &&
      (suffix_start < timeline.count);
    for (index = suffix_start; ok && (index < timeline.count); index++) {
      const strappy_session_message_record *record;

      record = &timeline.records[index];
      ok = (record->http_attempt_id == call_id) &&
        ((record->direction == NULL) ||
         (strcmp(record->direction, "request") != 0));
    }
  }

  recorder->timeline_count = timeline.count;
  recorder->saw_timeline = 1;
  strappy_session_free_string(error);
  strappy_session_message_record_list_destroy(&timeline);
  return ok;
}

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
  sqlite3_stmt *stmt;
  const unsigned char *state;
  char extra;
  long long call_id;
  long long call_count;
  long long completed_call_count;
  long long pending_count;
  long long answer_quality_count;
  long long request_id;
  long long attempt_index;
  long long session_id;
  int event_ok;
  int coalesced_transition;
  int presented_transition;
  int opened;
  int running;

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
  recorder->event_sequence++;

  if (event->type == STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) {
    cJSON *root;
    cJSON *active;
    cJSON *kind;
    cJSON *started;
    long long started_ms;

    if (strappy_session_webview_event_requires_message_update(event)) {
      recorder->valid = 0;
    }
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
      if (recorder->started_count != recorder->count) {
        recorder->valid = 0;
      }
      if ((recorder->terminal_call_id > 0LL) &&
          (recorder->terminal_clear_sequence == 0LL)) {
        recorder->terminal_clear_sequence = recorder->event_sequence;
      }
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

  if (event->type == STRAPPY_RESPONSES_EVENT_LEDGER_UPDATED) {
    long long wall_duration_ms;
    int rc;

    call_id = 0LL;
    extra = '\0';
    db = NULL;
    stmt = NULL;
    wall_duration_ms = -1LL;
    event_ok =
      (event->kind != NULL) &&
      (strcmp(event->kind, "response_api_call") == 0) &&
      (event->message_key != NULL) &&
      (sscanf(event->message_key,
              "response-call-%lld%c",
              &call_id,
              &extra) == 1) &&
      (call_id > 0LL) &&
      (event->status_kind != NULL) &&
      (strcmp(event->status_kind, "wall_duration") == 0) &&
      strappy_session_webview_event_requires_message_update(event) &&
      (sqlite3_open(recorder->db_path, &db) == SQLITE_OK);
    if (event_ok) {
      rc = sqlite3_prepare_v2(
        db,
        "SELECT r.wall_duration_ms FROM model_requests r "
        "JOIN http_attempts a ON a.request_id=r.id WHERE a.id=?;",
        -1,
        &stmt,
        NULL);
      if ((rc != SQLITE_OK) ||
          (sqlite3_bind_int64(stmt,
                              1,
                              (sqlite3_int64)call_id) != SQLITE_OK) ||
          (sqlite3_step(stmt) != SQLITE_ROW) ||
          (sqlite3_column_type(stmt, 0) == SQLITE_NULL)) {
        event_ok = 0;
      } else {
        wall_duration_ms = (long long)sqlite3_column_int64(stmt, 0);
        event_ok = wall_duration_ms >= 0LL;
      }
    }
    sqlite3_finalize(stmt);
    if (db != NULL) {
      sqlite3_close(db);
    }
    if (event_ok) {
      recorder->wall_duration_update_count++;
      recorder->message_update_required_count++;
      if ((call_id == recorder->terminal_call_id) &&
          (recorder->terminal_wall_duration_sequence == 0LL)) {
        recorder->terminal_wall_duration_sequence =
          recorder->event_sequence;
      }
    } else {
      recorder->valid = 0;
    }
    return 1;
  }

  if ((event->type == STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED) &&
      (event->kind != NULL) &&
      (strcmp(event->kind, "response_tool_outputs") == 0)) {
    call_id = 0LL;
    extra = '\0';
    event_ok = (event->message_key != NULL) &&
      (sscanf(event->message_key,
              "response-call-%lld%c",
              &call_id,
              &extra) == 1) &&
      (call_id > 0LL) &&
      (event->actor != NULL) &&
      (strcmp(event->actor, "harness") == 0) &&
      (event->status_kind != NULL) &&
      (strcmp(event->status_kind, "tool_outputs") == 0) &&
      strappy_session_webview_event_requires_message_update(event);
    if (event_ok) {
      recorder->tool_outputs_event_count++;
      recorder->message_update_required_count++;
    } else {
      recorder->valid = 0;
    }
    return 1;
  }

  call_id = 0LL;
  extra = '\0';
  running = (event->status_kind != NULL) &&
    (strcmp(event->status_kind, "running") == 0);
  if (event->is_terminal) {
    if (running) {
      recorder->valid = 0;
    }
    recorder->terminal_count++;
  }
  db = NULL;
  opened = sqlite3_open(recorder->db_path, &db) == SQLITE_OK;
  stmt = NULL;
  call_count = 0LL;
  completed_call_count = 0LL;
  pending_count = 0LL;
  answer_quality_count = 0LL;
  request_id = 0LL;
  attempt_index = -1LL;
  session_id = 0LL;
  event_ok =
    (event->type == STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED) &&
    (event->kind != NULL) &&
    (strcmp(event->kind, "response_api_call") == 0) &&
    (event->message_key != NULL) &&
    (sscanf(event->message_key,
            "response-call-%lld%c",
            &call_id,
            &extra) == 1) &&
    (call_id > 0LL) &&
    (event->status_kind != NULL) &&
    (strappy_session_webview_event_requires_message_update(event) ==
      !running) &&
    opened;
  if (event_ok) {
    int rc;

    rc = sqlite3_prepare_v2(
      db,
      "SELECT a.request_id, a.attempt_index, a.state, t.session_id "
      "FROM http_attempts a "
      "JOIN model_requests r ON r.id = a.request_id "
      "JOIN turns t ON t.id = r.turn_id WHERE a.id = ?;",
      -1,
      &stmt,
      NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt,
                            1,
                            (sqlite3_int64)call_id) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_ROW)) {
      event_ok = 0;
    } else {
      request_id = (long long)sqlite3_column_int64(stmt, 0);
      attempt_index = (long long)sqlite3_column_int64(stmt, 1);
      state = sqlite3_column_text(stmt, 2);
      session_id = (long long)sqlite3_column_int64(stmt, 3);
      event_ok = (state != NULL) &&
        (strcmp((const char *)state, event->status_kind) == 0);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
  }
  event_ok = event_ok &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM http_attempts;",
                      &call_count) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM http_attempts "
                      "WHERE state NOT IN ('pending','running');",
                      &completed_call_count) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM http_attempts "
                      "WHERE state IN ('pending','running');",
                      &pending_count) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM answer_quality_audits;",
                      &answer_quality_count);
  coalesced_transition = event_ok && running &&
    (recorder->started_count == (recorder->count + 1LL));
  presented_transition = event_ok && running &&
    (recorder->pending_presented_request_id > 0LL) &&
    (recorder->started_count == recorder->count);
  if (event_ok && running) {
    event_ok = ((recorder->started_count == recorder->count) ||
                (coalesced_transition &&
                 (recorder->last_started_request_id > 0LL))) &&
      (call_count == (recorder->started_count + 1LL)) &&
      (completed_call_count == recorder->started_count) &&
      (pending_count == 1LL) &&
      harness_ledger_timeline_matches(recorder,
                                      session_id,
                                      request_id,
                                      call_id,
                                      attempt_index,
                                      1,
                                      coalesced_transition ?
                                        recorder->last_started_request_id :
                                        0LL);
    if (event_ok) {
      if (coalesced_transition) {
        recorder->count++;
      }
      if (coalesced_transition || presented_transition) {
        recorder->coalesced_transition_count++;
      }
      if (presented_transition) {
        recorder->pending_presented_request_id = 0LL;
      }
      recorder->started_count++;
      recorder->last_started_request_id = request_id;
    }
  } else if (event_ok) {
    event_ok = (recorder->started_count == (recorder->count + 1LL)) &&
      (call_count == (recorder->count + 1LL)) &&
      (completed_call_count == (recorder->count + 1LL)) &&
      (pending_count == 0LL) &&
      harness_ledger_timeline_matches(recorder,
                                      session_id,
                                      request_id,
                                      call_id,
                                      attempt_index,
                                      0,
                                      0LL);
    if (event_ok) {
      recorder->count++;
      recorder->completion_event_count++;
      if (event->coalesce_with_next_ledger_change) {
        sqlite3_stmt *tool_stmt;
        long long tool_execution_count;
        int tool_rc;

        tool_stmt = NULL;
        tool_execution_count = -1LL;
        tool_rc = sqlite3_prepare_v2(
          db,
          "SELECT COUNT(*) FROM tool_executions "
          "WHERE response_attempt_id=?;",
          -1,
          &tool_stmt,
          NULL);
        if ((tool_rc != SQLITE_OK) ||
            (sqlite3_bind_int64(tool_stmt,
                                1,
                                (sqlite3_int64)call_id) != SQLITE_OK) ||
            (sqlite3_step(tool_stmt) != SQLITE_ROW)) {
          event_ok = 0;
        } else {
          tool_execution_count =
            (long long)sqlite3_column_int64(tool_stmt, 0);
          event_ok = tool_execution_count == 0LL;
        }
        sqlite3_finalize(tool_stmt);
        if (event_ok) {
          recorder->coalescible_completion_count++;
          recorder->pending_presented_request_id = request_id;
        }
      }
    }
  }
  if (!event_ok) {
    recorder->valid = 0;
  } else {
    if (running) {
      recorder->message_update_skipped_count++;
    } else {
      recorder->message_update_required_count++;
    }
    if (event->is_terminal) {
      if (recorder->terminal_call_id != 0LL) {
        recorder->valid = 0;
      }
      recorder->terminal_call_id = call_id;
      recorder->terminal_ledger_sequence = recorder->event_sequence;
    }
  }
  recorder->answer_quality_count = answer_quality_count;
  if (db != NULL) {
    sqlite3_close(db);
  }
  return 1;
}

static int harness_terminal_event_order_is_valid(
  const harness_ledger_event_recorder *recorder)
{
  return (recorder != NULL) &&
    (recorder->terminal_count == 1LL) &&
    (recorder->terminal_call_id > 0LL) &&
    (recorder->terminal_ledger_sequence > 0LL) &&
    (recorder->terminal_wall_duration_sequence >
      recorder->terminal_ledger_sequence) &&
    (recorder->terminal_clear_sequence >
      recorder->terminal_wall_duration_sequence);
}

static int harness_direct_timeline_cursor_matches(
  const char *db_path,
  long long session_id,
  const strappy_session_message_record_list *timeline,
  char **error_out)
{
  const strappy_response_timeline_cursor *expected;
  strappy_response_timeline_cursor actual;

  if (timeline == NULL) {
    return 0;
  }
  strappy_response_timeline_cursor_init(&actual);
  if (!strappy_db_load_response_timeline_cursor(db_path,
                                                session_id,
                                                &actual,
                                                error_out)) {
    return 0;
  }
  if (timeline->count == 0U) {
    return !actual.valid && (actual.session_id == session_id);
  }
  expected = &timeline->records[timeline->count - 1U].timeline_cursor;
  return actual.valid &&
    (actual.session_id == expected->session_id) &&
    (actual.request_id == expected->request_id) &&
    (actual.group_phase == expected->group_phase) &&
    (actual.attempt_index == expected->attempt_index) &&
    (actual.attempt_phase == expected->attempt_phase) &&
    (actual.item_index == expected->item_index) &&
    (actual.entry_type == expected->entry_type) &&
    (actual.row_id == expected->row_id);
}

#define HARNESS_HTTP_MAX_REQUEST_BYTES (4U * 1024U * 1024U)

/* Set immediately before fork so each local fixture server receives its own
 * immutable expectation. Never set this to a live credential. */
static const char *harness_expected_authorization = NULL;

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
  HARNESS_RESPONSES_SERVER_PREFLIGHT_FIRST_PROMPT_ONLY = 15,
  HARNESS_RESPONSES_SERVER_ISOLATED_PROMPTS = 16,
  HARNESS_RESPONSES_SERVER_ROUND_LIMIT = 17,
  HARNESS_RESPONSES_SERVER_ANSWER_QUALITY_DISABLED = 18,
  HARNESS_RESPONSES_SERVER_NATIVE_WEB_SEARCH = 19,
  HARNESS_RESPONSES_SERVER_OTHER_GENERIC = 20
} harness_responses_server_scenario;

static int harness_headers_have_exact_authorization(
  const char *headers,
  size_t headers_length,
  const char *expected)
{
  const char *cursor;
  const char *headers_end;

  if (expected == NULL) {
    return 1;
  }
  cursor = headers;
  headers_end = headers + headers_length;
  while (cursor < headers_end) {
    const char *line_end;
    static const char field[] = "Authorization:";

    line_end = strstr(cursor, "\r\n");
    if ((line_end == NULL) || (line_end > headers_end)) {
      line_end = headers_end;
    }
    if (((size_t)(line_end - cursor) >= (sizeof(field) - 1U)) &&
        (strncasecmp(cursor, field, sizeof(field) - 1U) == 0)) {
      const char *value;

      value = cursor + sizeof(field) - 1U;
      while ((value < line_end) && ((*value == ' ') || (*value == '\t'))) {
        value++;
      }
      return (expected[0] != '\0') &&
        ((size_t)(line_end - value) == strlen(expected)) &&
        (memcmp(value, expected, strlen(expected)) == 0);
    }
    cursor = (line_end < headers_end) ? line_end + 2 : headers_end;
  }
  return expected[0] == '\0';
}

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

static int harness_has_expected_user_agent(const char *headers,
                                           size_t headers_length)
{
  static const char field_name[] = "User-Agent:";
  const char *cursor;
  const char *headers_end;
  char *error;
  char *expected;
  size_t expected_length;
  int found;

  if (headers == NULL) {
    return 0;
  }
  error = NULL;
  expected = strappy_identity_copy_user_agent(&error);
  if (expected == NULL) {
    fprintf(stderr,
            "Could not build expected user agent: %s\n",
            (error != NULL) ? error : "Unknown identity error.");
    free(error);
    return 0;
  }
  free(error);
  expected_length = strlen(expected);
  cursor = headers;
  headers_end = headers + headers_length;
  found = 0;
  while (cursor < headers_end) {
    const char *line_end;
    const char *value;
    size_t line_length;
    size_t value_length;

    line_end = strstr(cursor, "\r\n");
    if ((line_end == NULL) || (line_end > headers_end)) {
      line_end = headers_end;
    }
    line_length = (size_t)(line_end - cursor);
    if ((line_length >= (sizeof(field_name) - 1U)) &&
        (strncasecmp(cursor,
                     field_name,
                     sizeof(field_name) - 1U) == 0)) {
      value = cursor + sizeof(field_name) - 1U;
      while ((value < line_end) && ((*value == ' ') || (*value == '\t'))) {
        value++;
      }
      value_length = (size_t)(line_end - value);
      found = (value_length == expected_length) &&
        (memcmp(value, expected, expected_length) == 0);
      break;
    }
    if (line_end == headers_end) {
      break;
    }
    cursor = line_end + 2;
  }
  free(expected);
  return found;
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
                                    &body_length) ||
            !harness_has_expected_user_agent(request, headers_length) ||
            !harness_headers_have_exact_authorization(
              request,
              headers_length,
              harness_expected_authorization)) {
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

static int harness_message_part_type_is(cJSON *item,
                                        const char *expected_type)
{
  cJSON *content;
  cJSON *part;
  cJSON *type;

  content = cJSON_GetObjectItem(item, "content");
  part = cJSON_GetArrayItem(content, 0);
  type = cJSON_GetObjectItem(part, "type");
  return cJSON_IsString(type) && (type->valuestring != NULL) &&
    (strcmp(type->valuestring, expected_type) == 0);
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
  const char *instructions,
  int answer_quality_enabled)
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
    if ((strcmp(section_names[index], "audit") == 0) &&
        !answer_quality_enabled) {
      result = (heading != NULL) &&
        (strstr(instructions, heading) == NULL);
      continue;
    }
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
    "command -v sw_vers",
    "sw_vers -productName",
    "sw_vers -productVersion",
    "uname -s",
    "uname -m",
    "OS=%s",
    "OS_VERSION=%s",
    "MACHINE=%s",
    "command -v id",
    "UID=%s",
    "PWD=%s",
    "PATH=%s",
    "=== Core tools ===",
    ("for command_name in clang make git ruby perl python python3 node jq "
     "curl magick ldid zip xcrun xcodebuild"),
    "=== Altivec ===",
    "for command_name in altivec-sdk altivec-lib altivec-app",
    "command -v \"$command_name\"",
    "/var/altivec/share/altivec/make/ios-app.mk",
    "command -v altivec-lib",
    "altivec-lib list",
    "altivec-lib list exited"
  };
  static const char *forbidden_fragments[] = {
    "uname -a",
    "uname -r",
    "uname -v",
    "uname -n",
    "uname -i",
    "uname -p",
    "sw_vers -buildVersion",
    "=== altivec-sdk list ===",
    "altivec-sdk list exited",
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
  command = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "command") : NULL;
  ok = cJSON_IsString(command) && (command->valuestring != NULL) &&
    (root->child == command) && (command->next == NULL);
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

static int harness_preflight_object_output_equals(
  cJSON *item,
  cJSON *call,
  const char *expected_output)
{
  cJSON *output;

  if ((expected_output == NULL) ||
      !harness_preflight_output_matches(item, call, 0)) {
    return 0;
  }
  output = cJSON_GetObjectItem(item, "output");
  return cJSON_IsString(output) && (output->valuestring != NULL) &&
    (strcmp(output->valuestring, expected_output) == 0);
}

static int harness_skill_summary_matches(cJSON *item,
                                         const char *identifier,
                                         const char *title,
                                         const char *description)
{
  cJSON *skill_id;
  cJSON *skill_title;
  cJSON *skill_description;

  skill_id = cJSON_GetObjectItem(item, "skill_id");
  skill_title = cJSON_GetObjectItem(item, "title");
  skill_description = cJSON_GetObjectItem(item, "description");
  return cJSON_IsObject(item) && (cJSON_GetArraySize(item) == 3) &&
    cJSON_IsString(skill_id) && (skill_id->valuestring != NULL) &&
    (strcmp(skill_id->valuestring, identifier) == 0) &&
    cJSON_IsString(skill_title) && (skill_title->valuestring != NULL) &&
    (strcmp(skill_title->valuestring, title) == 0) &&
    cJSON_IsString(skill_description) &&
    (skill_description->valuestring != NULL) &&
    (strcmp(skill_description->valuestring, description) == 0);
}

static int harness_coding_skills_preflight_output_is_valid(cJSON *item,
                                                            cJSON *call)
{
  cJSON *output;
  cJSON *root;
  cJSON *skills;
  int ok;

  if (!harness_preflight_output_matches(item, call, 0)) {
    return 0;
  }
  output = cJSON_GetObjectItem(item, "output");
  root = cJSON_Parse(output->valuestring);
  skills = cJSON_IsObject(root) ?
    cJSON_GetObjectItem(root, "skills") : NULL;
  ok = cJSON_IsObject(root) && (cJSON_GetArraySize(root) == 1) &&
    cJSON_IsArray(skills) && (cJSON_GetArraySize(skills) == 2) &&
    harness_skill_summary_matches(
      cJSON_GetArrayItem(skills, 0),
      "ios-development",
      "iOS Development",
      HARNESS_IOS_DEVELOPMENT_SKILL_DESCRIPTION) &&
    harness_skill_summary_matches(
      cJSON_GetArrayItem(skills, 1),
      "macos-development",
      "macOS Development",
      HARNESS_MACOS_DEVELOPMENT_SKILL_DESCRIPTION);
  cJSON_Delete(root);
  return ok;
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
  cJSON *skills_call;
  cJSON *database_call;
  const char *assistant_text;

  assistant_text = harness_message_text(cJSON_GetArrayItem(input, 1));
  memory_call = cJSON_GetArrayItem(input, 2);
  skills_call = cJSON_GetArrayItem(input, 3);
  database_call = cJSON_GetArrayItem(input, 4);
  return harness_message_role_is(cJSON_GetArrayItem(input, 1), "assistant") &&
    harness_message_part_type_is(cJSON_GetArrayItem(input, 0), "input_text") &&
    harness_message_part_type_is(cJSON_GetArrayItem(input, 1), "output_text") &&
    (assistant_text != NULL) &&
    (strcmp(assistant_text, HARNESS_PERSONAL_PREFLIGHT_ASSISTANT_TEXT) == 0) &&
    harness_preflight_call_is_valid(memory_call,
                                    "memory_read",
                                    "{}",
                                    "fc_pf_0_",
                                    "call_pf_0_",
                                    prompt_group) &&
    harness_preflight_call_is_valid(skills_call,
                                    STRAPPY_TOOL_SKILLS_LIST,
                                    "{}",
                                    "fc_pf_1_",
                                    "call_pf_1_",
                                    prompt_group) &&
    harness_preflight_call_is_valid(database_call,
                                    "database_list",
                                    "{}",
                                    "fc_pf_2_",
                                    "call_pf_2_",
                                    prompt_group) &&
    harness_preflight_output_matches(cJSON_GetArrayItem(input, 5),
                                     memory_call,
                                     1) &&
    harness_preflight_object_output_equals(cJSON_GetArrayItem(input, 6),
                                           skills_call,
                                           HARNESS_SKILLS_LIST_EMPTY_RESULT) &&
    harness_preflight_output_matches(cJSON_GetArrayItem(input, 7),
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

static int harness_request_omits_max_tool_calls(cJSON *root)
{
  return cJSON_GetObjectItem(root, "max_tool_calls") == NULL;
}

static int harness_request_base_is_valid_with_answer_quality(
  cJSON *root,
  const char *expected_prompt,
  int answer_quality_enabled,
  char **session_key_out,
  char **prompt_group_out)
{
  cJSON *stream;
  cJSON *store;
  cJSON *parallel_tool_calls;
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
  parallel_tool_calls = cJSON_GetObjectItem(root, "parallel_tool_calls");
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
      !cJSON_IsTrue(parallel_tool_calls) ||
      !harness_request_omits_max_tool_calls(root) ||
      !cJSON_IsString(instructions) ||
      (instructions->valuestring == NULL) ||
      !harness_instructions_include_resource_sections(
        instructions->valuestring,
        answer_quality_enabled) ||
      (answer_quality_enabled &&
       (strstr(instructions->valuestring,
               "`unicode_emoji_absent`") == NULL)) ||
      (answer_quality_enabled &&
       (strstr(instructions->valuestring,
               "NEVER use unicode emoji in your answer") == NULL)) ||
      (!answer_quality_enabled &&
       ((strstr(instructions->valuestring,
                "`unicode_emoji_absent`") != NULL) ||
        (strstr(instructions->valuestring,
                "NEVER use unicode emoji in your answer") != NULL) ||
        has_web_reference_key || has_web_reference_instruction)) ||
      (has_web_search != has_web_fetch) ||
      (answer_quality_enabled &&
       (has_web_search != has_web_reference_key)) ||
      (answer_quality_enabled &&
       (has_web_search != has_web_reference_instruction)) ||
      !cJSON_IsString(session_key) || (session_key->valuestring == NULL) ||
      !cJSON_IsString(prompt_group) || (prompt_group->valuestring == NULL) ||
      !cJSON_IsArray(input) || (input_count != 8) ||
      !harness_message_role_is(cJSON_GetArrayItem(input, 0), "user") ||
      !harness_preflight_input_is_valid(input, prompt_group->valuestring) ||
      (text == NULL) || (strcmp(text, expected_prompt) != 0) ||
      !cJSON_IsArray(tools) || !cJSON_IsObject(first_tool) ||
      !harness_tools_hide_local_display_metadata(tools) ||
      harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_STUDY) ||
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

static int harness_request_base_is_valid(
  cJSON *root,
  const char *expected_prompt,
  char **session_key_out,
  char **prompt_group_out)
{
  return harness_request_base_is_valid_with_answer_quality(
    root,
    expected_prompt,
    1,
    session_key_out,
    prompt_group_out);
}

static int harness_world_knowledge_tools_are_valid(cJSON *tools,
                                                   int bash_enabled)
{
  return cJSON_IsArray(tools) &&
    (cJSON_GetArraySize(tools) == (bash_enabled ? 13 : 12)) &&
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
    harness_has_tool_name(tools, STRAPPY_TOOL_SKILLS_LIST) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_SKILL_READ) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_SESSION_RENAME) &&
    (harness_has_tool_name(tools, STRAPPY_TOOL_BASH) ==
      (bash_enabled ? 1 : 0)) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_LIST) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_QUERY) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_CONTEXT) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_STUDY) &&
    harness_tools_hide_local_display_metadata(tools);
}

static int harness_database_study_tools_are_valid(cJSON *tools)
{
  return cJSON_IsArray(tools) && (cJSON_GetArraySize(tools) == 6) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_LIST) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_QUERY) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_CONTEXT) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_DATABASE_STUDY) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_DATETIME_TO_ISO8601) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_DATETIME_FROM_ISO8601) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_SESSION_RENAME) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_FONTAWESOME_SEARCH) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_FONTAWESOME_CONFIRM) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_MEMORY_READ) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
    !harness_has_tool_name(tools, STRAPPY_TOOL_OPENROUTER_WEB_FETCH) &&
    harness_tools_hide_local_display_metadata(tools);
}

static int harness_world_knowledge_request_is_valid(
  cJSON *root,
  const char *expected_prompt,
  int bash_enabled,
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
  cJSON *skills_call;
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
  skills_call = cJSON_GetArrayItem(input, 3);
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
      !cJSON_IsArray(input) || (cJSON_GetArraySize(input) != 6) ||
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
      !harness_preflight_call_is_valid(skills_call,
                                       STRAPPY_TOOL_SKILLS_LIST,
                                       "{}",
                                       "fc_pf_1_",
                                       "call_pf_1_",
                                       prompt_group->valuestring) ||
      !harness_preflight_output_equals(cJSON_GetArrayItem(input, 4),
                                       memory_call,
                                       "[]") ||
      !harness_preflight_object_output_equals(
        cJSON_GetArrayItem(input, 5),
        skills_call,
        HARNESS_SKILLS_LIST_EMPTY_RESULT) ||
      (text == NULL) || (strcmp(text, expected_prompt) != 0) ||
      !harness_world_knowledge_tools_are_valid(tools, bash_enabled)) {
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

static int harness_database_study_request_is_valid(
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
  cJSON *database_call;
  const char *text;

  stream = cJSON_GetObjectItem(root, "stream");
  store = cJSON_GetObjectItem(root, "store");
  instructions = cJSON_GetObjectItem(root, "instructions");
  session_key = cJSON_GetObjectItem(root, "session_id");
  metadata = cJSON_GetObjectItem(root, "metadata");
  prompt_group = cJSON_IsObject(metadata) ?
    cJSON_GetObjectItem(metadata, "strappy_prompt_group_key") : NULL;
  input = cJSON_GetObjectItem(root, "input");
  database_call = cJSON_GetArrayItem(input, 2);
  text = harness_message_text(cJSON_GetArrayItem(input, 0));
  if (!cJSON_IsFalse(stream) || !cJSON_IsFalse(store) ||
      !cJSON_IsString(instructions) ||
      (instructions->valuestring == NULL) ||
      (strstr(instructions->valuestring,
              "You are Strappy, an expert database sleuth.") == NULL) ||
      (strstr(instructions->valuestring,
              "Study only the database_ids in the user prompt.") == NULL) ||
      (strstr(instructions->valuestring,
              "A durable description says what useful user data exists.") ==
       NULL) ||
      (strstr(instructions->valuestring,
              "Durable context tells a future assistant exactly how to "
              "retrieve it; context is not a schema inventory.") == NULL) ||
      (strstr(instructions->valuestring, "1. Call database_context.") ==
       NULL) ||
      (strstr(instructions->valuestring,
              "2. Use targeted database_query schema inspection to find the "
              "user-data entry points") == NULL) ||
      (strstr(instructions->valuestring,
              "3. Design one to three common user-data access recipes.") ==
       NULL) ||
      (strstr(instructions->valuestring,
              "4. Execute every final recipe exactly as it will be saved.") ==
       NULL) ||
      (strstr(instructions->valuestring,
              "Copy saved SQL verbatim from a successful database_query call; "
              "never add ?, :name, @name, or $name bind placeholders.") ==
       NULL) ||
      (strstr(instructions->valuestring,
              "5. If an executed recipe returns a useful numeric timestamp, "
              "verify one returned value with datetime_to_iso8601") == NULL) ||
      (strstr(instructions->valuestring,
              "6. After the investigation is complete, call database_study "
              "exactly once with both description and context.") == NULL) ||
      (strstr(instructions->valuestring,
              "In context, give each recipe's purpose and exact verified "
              "SQL") == NULL) ||
      (strstr(instructions->valuestring,
              "Do not restate table or view lists or column inventories "
              "available from database_context or sqlite_schema.") == NULL) ||
      (strstr(instructions->valuestring,
              "If no useful user-facing data exists, say so and explain the "
              "access limitation without substituting a schema summary.") ==
       NULL) ||
      (strstr(instructions->valuestring,
              "Never put sampled values, secrets, or sensitive identifiers "
              "in saved study values.") == NULL) ||
      !cJSON_IsString(session_key) || (session_key->valuestring == NULL) ||
      !cJSON_IsString(prompt_group) || (prompt_group->valuestring == NULL) ||
      !cJSON_IsArray(input) || (cJSON_GetArraySize(input) != 4) ||
      !harness_message_role_is(cJSON_GetArrayItem(input, 0), "user") ||
      !harness_message_role_is(cJSON_GetArrayItem(input, 1), "assistant") ||
      (harness_message_text(cJSON_GetArrayItem(input, 1)) == NULL) ||
      (strcmp(harness_message_text(cJSON_GetArrayItem(input, 1)),
              HARNESS_DATABASE_STUDY_PREFLIGHT_ASSISTANT_TEXT) != 0) ||
      !harness_preflight_call_is_valid(database_call,
                                       STRAPPY_TOOL_DATABASE_LIST,
                                       "{}",
                                       "fc_pf_0_",
                                       "call_pf_0_",
                                       prompt_group->valuestring) ||
      !harness_preflight_output_matches(cJSON_GetArrayItem(input, 3),
                                        database_call,
                                        0) ||
      (text == NULL) || (strcmp(text, expected_prompt) != 0) ||
      !harness_database_study_tools_are_valid(cJSON_GetObjectItem(root,
                                                                  "tools"))) {
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
  cJSON *skills_call;
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
  skills_call = cJSON_GetArrayItem(input, 3);
  first_prompt = harness_message_text(cJSON_GetArrayItem(input, 0));
  announcement = harness_message_text(cJSON_GetArrayItem(input, 1));
  first_answer = harness_message_text(cJSON_GetArrayItem(input, 6));
  second_prompt = harness_message_text(cJSON_GetArrayItem(input, 7));
  return cJSON_IsString(request_session) &&
    (request_session->valuestring != NULL) &&
    (strcmp(request_session->valuestring, session_key) == 0) &&
    cJSON_IsString(prompt_group) && (prompt_group->valuestring != NULL) &&
    (strcmp(prompt_group->valuestring, first_prompt_group) != 0) &&
    cJSON_IsArray(input) && (cJSON_GetArraySize(input) == 8) &&
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
    harness_preflight_call_is_valid(skills_call,
                                    STRAPPY_TOOL_SKILLS_LIST,
                                    "{}",
                                    "fc_pf_1_",
                                    "call_pf_1_",
                                    first_prompt_group) &&
    harness_preflight_output_equals(cJSON_GetArrayItem(input, 4),
                                    memory_call,
                                    "[]") &&
    harness_preflight_object_output_equals(
      cJSON_GetArrayItem(input, 5),
      skills_call,
      HARNESS_SKILLS_LIST_EMPTY_RESULT) &&
    harness_message_role_is(cJSON_GetArrayItem(input, 6), "assistant") &&
    (first_answer != NULL) &&
    (strcmp(first_answer, "First round answer.") == 0) &&
    harness_message_role_is(cJSON_GetArrayItem(input, 7), "user") &&
    (second_prompt != NULL) &&
    (strcmp(second_prompt, "Second prompt") == 0) &&
    harness_world_knowledge_tools_are_valid(cJSON_GetObjectItem(root, "tools"),
                                            0);
}

static int harness_isolated_followup_request_is_valid(
  cJSON *root,
  const char *session_key,
  const char *first_prompt_group)
{
  cJSON *request_session;
  cJSON *metadata;
  cJSON *prompt_group;
  cJSON *input;
  const char *prompt;

  request_session = cJSON_GetObjectItem(root, "session_id");
  metadata = cJSON_GetObjectItem(root, "metadata");
  prompt_group = cJSON_IsObject(metadata) ?
    cJSON_GetObjectItem(metadata, "strappy_prompt_group_key") : NULL;
  input = cJSON_GetObjectItem(root, "input");
  prompt = harness_message_text(cJSON_GetArrayItem(input, 0));
  return cJSON_IsString(request_session) &&
    (request_session->valuestring != NULL) &&
    (strcmp(request_session->valuestring, session_key) == 0) &&
    cJSON_IsString(prompt_group) && (prompt_group->valuestring != NULL) &&
    (strcmp(prompt_group->valuestring, first_prompt_group) != 0) &&
    cJSON_IsArray(input) && (cJSON_GetArraySize(input) == 1) &&
    harness_message_role_is(cJSON_GetArrayItem(input, 0), "user") &&
    (prompt != NULL) && (strcmp(prompt, "Second isolated prompt") == 0) &&
    harness_database_study_tools_are_valid(cJSON_GetObjectItem(root,
                                                               "tools"));
}

static int harness_coding_assistant_request_is_valid(
  cJSON *root,
  const char *expected_prompt,
  int bash_enabled,
  int parallel_tool_calls)
{
  cJSON *instructions;
  cJSON *metadata;
  cJSON *prompt_group;
  cJSON *input;
  cJSON *memory_call;
  cJSON *skills_call;
  cJSON *bash_call;
  cJSON *bash_arguments;
  cJSON *tools;
  cJSON *parallel_tool_calls_value;
  int expected_input_count;
  int memory_output_index;
  const char *text;

  instructions = cJSON_GetObjectItem(root, "instructions");
  metadata = cJSON_GetObjectItem(root, "metadata");
  prompt_group = cJSON_IsObject(metadata) ?
    cJSON_GetObjectItem(metadata, "strappy_prompt_group_key") : NULL;
  input = cJSON_GetObjectItem(root, "input");
  memory_call = cJSON_GetArrayItem(input, 2);
  skills_call = cJSON_GetArrayItem(input, 3);
  bash_call = bash_enabled ? cJSON_GetArrayItem(input, 4) : NULL;
  bash_arguments = cJSON_IsObject(bash_call) ?
    cJSON_GetObjectItem(bash_call, "arguments") : NULL;
  tools = cJSON_GetObjectItem(root, "tools");
  parallel_tool_calls_value =
    cJSON_GetObjectItem(root, "parallel_tool_calls");
  text = harness_message_text(cJSON_GetArrayItem(input, 0));
  expected_input_count = bash_enabled ? 8 : 6;
  memory_output_index = bash_enabled ? 5 : 4;
  return cJSON_IsString(instructions) &&
    (instructions->valuestring != NULL) &&
    (strstr(instructions->valuestring,
            "You are an expert coding assistant.") != NULL) &&
    cJSON_IsString(prompt_group) && (prompt_group->valuestring != NULL) &&
    cJSON_IsArray(input) &&
    (cJSON_GetArraySize(input) == expected_input_count) &&
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
    harness_preflight_call_is_valid(skills_call,
                                    STRAPPY_TOOL_SKILLS_LIST,
                                    "{}",
                                    "fc_pf_1_",
                                    "call_pf_1_",
                                    prompt_group->valuestring) &&
    (!bash_enabled ||
      (harness_preflight_call_is_valid(bash_call,
                                       STRAPPY_TOOL_BASH,
                                       NULL,
                                       "fc_pf_2_",
                                       "call_pf_2_",
                                       prompt_group->valuestring) &&
       harness_coding_preflight_bash_arguments_are_valid(
         cJSON_IsString(bash_arguments) ?
           bash_arguments->valuestring : NULL))) &&
    harness_preflight_output_matches(
      cJSON_GetArrayItem(input, memory_output_index),
      memory_call,
      1) &&
    harness_coding_skills_preflight_output_is_valid(
      cJSON_GetArrayItem(input, bash_enabled ? 6 : 5),
      skills_call) &&
    (!bash_enabled ||
      harness_preflight_bash_output_is_valid(cJSON_GetArrayItem(input, 7),
                                             bash_call)) &&
    cJSON_IsArray(tools) &&
    harness_request_omits_max_tool_calls(root) &&
    (parallel_tool_calls ?
      cJSON_IsTrue(parallel_tool_calls_value) :
      cJSON_IsFalse(parallel_tool_calls_value)) &&
    (cJSON_GetArraySize(tools) == (bash_enabled ? 14 : 13)) &&
    (harness_has_tool_name(tools, STRAPPY_TOOL_BASH) ==
      (bash_enabled ? 1 : 0)) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_FILE_READ) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_FILE_WRITE) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_FILE_EDIT) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_SKILLS_LIST) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_SKILL_READ) &&
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
    "memory_save"
  };
  static const char *call_ids[] = {
    "call-empty-context",
    "call-empty-session",
    "call-empty-icon",
    "call-empty-user-fact"
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
    (cJSON_GetObjectItem(root, "max_output_tokens") == NULL) &&
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

static int harness_run_answer_quality_disabled_server(int listener_fd)
{
  static const char *response =
    "{\"id\":\"resp-quality-disabled\",\"object\":\"response\","
    "\"created_at\":1700000000,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-quality-disabled\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Answer quality disabled.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":4,"
    "\"total_tokens\":8}}";
  char *body;
  char *session_key;
  char *prompt_group;
  cJSON *root;
  cJSON *max_output_tokens;
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
  max_output_tokens = cJSON_IsObject(root) ?
    cJSON_GetObjectItem(root, "max_output_tokens") : NULL;
  ok = cJSON_IsObject(root) &&
    cJSON_IsNumber(max_output_tokens) &&
    (max_output_tokens->valuedouble == 14286.0) &&
    harness_request_base_is_valid_with_answer_quality(
      root,
      "Skip answer quality",
      0,
      &session_key,
      &prompt_group) &&
    harness_send_json_response(client_fd, 200L, response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok) {
    fprintf(stderr,
            "Answer-quality-disabled server rejected the request.\n");
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
                                             1,
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
                                             0,
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

static int harness_run_isolated_prompts_server(int listener_fd)
{
  static const char *tool_response =
    "{\"id\":\"resp-isolated-tool\",\"object\":\"response\","
    "\"created_at\":1700000039,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"function_call\",\"id\":\"fc-isolated-datetime\","
    "\"call_id\":\"call-isolated-datetime\","
    "\"name\":\"datetime_to_iso8601\","
    "\"arguments\":\"{\\\"timestamps\\\":[\\\"0\\\"],"
    "\\\"unit\\\":\\\"unix_seconds\\\"}\","
    "\"status\":\"completed\"}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":3,"
    "\"total_tokens\":7}}";
  static const char *first_response =
    "{\"id\":\"resp-isolated-first\",\"object\":\"response\","
    "\"created_at\":1700000040,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-isolated-first\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"First isolated answer.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":4,"
    "\"total_tokens\":8}}";
  static const char *second_response =
    "{\"id\":\"resp-isolated-second\",\"object\":\"response\","
    "\"created_at\":1700000041,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-isolated-second\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Second isolated answer.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":1,\"output_tokens\":4,"
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
    harness_database_study_request_is_valid(root,
                                            "First isolated prompt",
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
    harness_named_function_output_request_is_valid(
      root,
      session_key,
      prompt_group,
      STRAPPY_TOOL_DATETIME_TO_ISO8601,
      "call-isolated-datetime",
      "[\"1970-01-01T00:00:00Z\"]") &&
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
  ok = cJSON_IsObject(root) &&
    (strstr(body, "First isolated prompt") == NULL) &&
    (strstr(body, "First isolated answer.") == NULL) &&
    harness_isolated_followup_request_is_valid(root,
                                               session_key,
                                               prompt_group) &&
    harness_send_json_response(client_fd, 200L, second_response);
  free(body);
  cJSON_Delete(root);
  close(client_fd);
  free(session_key);
  free(prompt_group);
  if (!ok) {
    fprintf(stderr, "Isolated prompt server observed leaked prior context.\n");
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

static int harness_run_native_web_search_server(int listener_fd)
{
  static const char *first_response =
    "{\"id\":\"resp-native-web-search\",\"object\":\"response\","
    "\"created_at\":1700000005,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"web_search_call\",\"id\":\"ws-native\","
    "\"status\":\"completed\",\"action\":{\"type\":\"search\","
    "\"queries\":[\"current test\"],\"sources\":[{\"type\":\"url\","
    "\"url\":\"https://example.com/article\"}]}},{"
    "\"type\":\"message\",\"id\":\"msg-native-web-search\","
    "\"role\":\"assistant\",\"status\":\"completed\","
    "\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Citation-backed answer.\",\"annotations\":[{"
    "\"type\":\"url_citation\",\"start_index\":0,\"end_index\":6,"
    "\"title\":\"Example [Source]\","
    "\"url\":\"https://example.com/article\"}]}]}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":5,"
    "\"total_tokens\":9}}";
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
                                  "Use native web search",
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

static int harness_run_round_limit_server(int listener_fd)
{
  static const char *first_tool_response =
    "{\"id\":\"resp-limit-first\",\"object\":\"response\","
    "\"created_at\":1700000042,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"function_call\",\"id\":\"fc-limit-first\","
    "\"call_id\":\"call-limit-first\","
    "\"name\":\"database_context\","
    "\"arguments\":\"{\\\"unexpected\\\":true}\","
    "\"status\":\"completed\"}],"
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":4,"
    "\"total_tokens\":8}}";
  static const char *second_tool_response =
    "{\"id\":\"resp-limit-second\",\"object\":\"response\","
    "\"created_at\":1700000043,\"model\":\"test/model\","
    "\"status\":\"completed\",\"output\":[{"
    "\"type\":\"function_call\",\"id\":\"fc-limit-second\","
    "\"call_id\":\"call-limit-second\","
    "\"name\":\"database_context\","
    "\"arguments\":\"{\\\"unexpected\\\":true}\","
    "\"status\":\"completed\"}],"
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
    harness_request_base_is_valid(
      root,
      "Reach the configured round limit",
      &session_key,
      &prompt_group) &&
    harness_send_json_response(client_fd, 200L, first_tool_response);
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
    harness_request_omits_max_tool_calls(root) &&
    harness_named_function_output_request_is_valid(
      root,
      session_key,
      prompt_group,
      STRAPPY_TOOL_DATABASE_CONTEXT,
      "call-limit-first",
      "Error: database_context does not accept argument 'unexpected'.") &&
    harness_send_json_response(client_fd, 200L, second_tool_response);
  cJSON_Delete(root);
  close(client_fd);
  free(session_key);
  free(prompt_group);
  if (!ok) {
    fprintf(stderr, "Round-limit server rejected a request.\n");
  }
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
                                              1,
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
                                                1,
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
                                                0,
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
                                              0,
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

static int harness_run_other_generic_server(int listener_fd)
{
  static const char *function_response =
    "{\"id\":\"resp-other-function\",\"object\":\"response\","
    "\"created_at\":1700000000,\"model\":\"manual\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"function_call\","
    "\"id\":\"fc-other\",\"call_id\":\"call-other-rename\","
    "\"name\":\"session_rename\","
    "\"arguments\":\"{\\\"name\\\":\\\"Generic Fixture\\\"}\","
    "\"status\":\"completed\"}],"
    "\"usage\":{\"input_tokens\":2,\"output_tokens\":1,"
    "\"total_tokens\":3}}";
  static const char *response =
    "{\"id\":\"resp-other\",\"object\":\"response\","
    "\"created_at\":1700000000,\"model\":\"manual\","
    "\"status\":\"completed\",\"output\":[{\"type\":\"message\","
    "\"id\":\"msg-other\",\"role\":\"assistant\","
    "\"status\":\"completed\",\"content\":[{\"type\":\"output_text\","
    "\"text\":\"Generic response.\",\"annotations\":[]}]}],"
    "\"usage\":{\"input_tokens\":3,\"output_tokens\":2,"
    "\"total_tokens\":5}}";
  char *body;
  cJSON *root;
  cJSON *model;
  cJSON *stream;
  cJSON *store;
  cJSON *tools;
  cJSON *input;
  cJSON *item;
  int client_fd;
  int exercise_local_function;
  int found_function_output;
  int ok;

  body = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  model = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "model") : NULL;
  stream = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "stream") : NULL;
  store = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "store") : NULL;
  tools = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "tools") : NULL;
  exercise_local_function = (harness_expected_authorization != NULL) &&
    (harness_expected_authorization[0] != '\0');
  ok = cJSON_IsString(model) && (model->valuestring != NULL) &&
    (strcmp(model->valuestring, "manual") == 0) &&
    cJSON_IsFalse(stream) && cJSON_IsFalse(store) &&
    (cJSON_GetObjectItem(root, "session_id") == NULL) &&
    (cJSON_GetObjectItem(root, "metadata") == NULL) &&
    (cJSON_GetObjectItem(root, "include") == NULL) &&
    (cJSON_GetObjectItem(root, "reasoning") == NULL) &&
    (cJSON_GetObjectItem(root, "text") == NULL) &&
    cJSON_IsArray(tools) &&
    harness_has_tool_name(tools, STRAPPY_TOOL_SESSION_RENAME) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_WEB_SEARCH) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) &&
    !harness_has_tool_type(tools, STRAPPY_TOOL_OPENROUTER_WEB_FETCH) &&
    harness_send_json_response(
      client_fd,
      200L,
      exercise_local_function ? function_response : response);
  cJSON_Delete(root);
  close(client_fd);
  if (!ok || !exercise_local_function) {
    return ok;
  }

  body = NULL;
  if (!harness_accept_request(listener_fd, &body, &client_fd)) {
    return 0;
  }
  root = cJSON_Parse(body);
  free(body);
  input = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "input") : NULL;
  found_function_output = 0;
  for (item = cJSON_IsArray(input) ? input->child : NULL;
       item != NULL;
       item = item->next) {
    cJSON *type;
    cJSON *call_id;

    type = cJSON_IsObject(item) ? cJSON_GetObjectItem(item, "type") : NULL;
    call_id = cJSON_IsObject(item) ?
      cJSON_GetObjectItem(item, "call_id") : NULL;
    if (cJSON_IsString(type) && (type->valuestring != NULL) &&
        (strcmp(type->valuestring, "function_call_output") == 0) &&
        cJSON_IsString(call_id) && (call_id->valuestring != NULL) &&
        (strcmp(call_id->valuestring, "call-other-rename") == 0)) {
      found_function_output = 1;
    }
  }
  ok = cJSON_IsObject(root) && found_function_output &&
    (cJSON_GetObjectItem(root, "session_id") == NULL) &&
    (cJSON_GetObjectItem(root, "metadata") == NULL) &&
    (cJSON_GetObjectItem(root, "include") == NULL) &&
    harness_send_json_response(client_fd, 200L, response);
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
    } else if (scenario ==
               HARNESS_RESPONSES_SERVER_ANSWER_QUALITY_DISABLED) {
      ok = harness_run_answer_quality_disabled_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_WORLD_KNOWLEDGE) {
      ok = harness_run_world_knowledge_server(listener_fd);
    } else if (scenario ==
               HARNESS_RESPONSES_SERVER_PREFLIGHT_FIRST_PROMPT_ONLY) {
      ok = harness_run_first_prompt_preflight_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_ISOLATED_PROMPTS) {
      ok = harness_run_isolated_prompts_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_SERVER_TOOL) {
      ok = harness_run_server_tool_server(listener_fd);
    } else if (scenario ==
               HARNESS_RESPONSES_SERVER_WEB_REFERENCE_VALID) {
      ok = harness_run_valid_web_reference_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_NATIVE_WEB_SEARCH) {
      ok = harness_run_native_web_search_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_OTHER_GENERIC) {
      ok = harness_run_other_generic_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_FUNCTION_TOOL) {
      ok = harness_run_function_tool_server(listener_fd);
    } else if (scenario == HARNESS_RESPONSES_SERVER_ROUND_LIMIT) {
      ok = harness_run_round_limit_server(listener_fd);
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

static int harness_create_session_database_with_answer_quality(
  const char *path,
  int answer_quality_enabled,
  long long *session_id_out,
  char **error_out)
{
  strappy_session_options options;
  strappy_session_option_mask actual_changed_fields;
  int ok;

  unlink(path);
  if (!strappy_db_restore_provider_account(
        path,
        STRAPPY_PROVIDER_ACCOUNT_OPENROUTER,
        STRAPPY_PROVIDER_OPENROUTER,
        STRAPPY_PROVIDER_ACCOUNT_OPENROUTER_NAME,
        NULL,
        error_out) ||
      !strappy_db_create_session(path, session_id_out, error_out)) {
    return 0;
  }
  if (!answer_quality_enabled) {
    return 1;
  }

  strappy_session_options_init(&options);
  actual_changed_fields = 0U;
  ok = strappy_db_load_session_options(path,
                                       *session_id_out,
                                       &options,
                                       error_out);
  if (ok) {
    options.answer_quality_enabled = 1;
    ok = strappy_db_update_session_options(
      path,
      *session_id_out,
      &options,
      STRAPPY_SESSION_OPTION_ANSWER_QUALITY,
      NULL,
      &actual_changed_fields,
      error_out) &&
      (actual_changed_fields == STRAPPY_SESSION_OPTION_ANSWER_QUALITY);
  }
  strappy_session_options_destroy(&options);
  return ok;
}

static int harness_create_session_database(const char *path,
                                           long long *session_id_out,
                                           char **error_out)
{
  return harness_create_session_database_with_answer_quality(path,
                                                             1,
                                                             session_id_out,
                                                             error_out);
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

static int harness_test_answer_quality_disabled(void)
{
  static const char *answer = "Answer quality disabled.";
  char path[] = "/tmp/strappy-responses-quality-disabled-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  strappy_session_message_record_list timeline;
  strappy_session_options options;
  long long session_id;
  long long value;
  size_t index;
  harness_ledger_event_recorder events;
  pid_t server_pid;
  int fd;
  int saw_answer;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail(
      "Could not create answer-quality-disabled harness database.");
  }
  close(fd);
  error = NULL;
  result = NULL;
  session_id = 0LL;
  strappy_session_message_record_list_init(&timeline);
  strappy_session_options_init(&options);
  ok = harness_create_session_database_with_answer_quality(path,
                                                           0,
                                                           &session_id,
                                                           &error) &&
    strappy_db_load_session_options(path,
                                    session_id,
                                    &options,
                                    &error) &&
    !options.answer_quality_enabled;
  db = NULL;
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = sqlite3_exec(
      db,
      "UPDATE provider_accounts SET max_output_tokens=14286 "
      "WHERE provider_id='openrouter';",
      NULL, NULL, NULL) == SQLITE_OK;
  } else if (ok) {
    ok = 0;
  }
  if (db != NULL) {
    sqlite3_close(db);
    db = NULL;
  }
  if (ok) {
    ok = harness_start_server(HARNESS_RESPONSES_SERVER_ANSWER_QUALITY_DISABLED,
                              endpoint,
                              sizeof(endpoint),
                              &server_pid);
  }
  if (!ok) {
    fprintf(stderr,
            "Could not prepare answer-quality-disabled integration test: "
            "%s\n",
            (error != NULL) ? error : "setup failed");
    goto cleanup;
  }

  memset(&events, 0, sizeof(events));
  events.db_path = path;
  events.valid = 1;
  result = strappy_responses_send_prompt_for_session_and_store_with_events(
    "Skip answer quality",
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
  ok = (result != NULL) && (strcmp(result, answer) == 0) && server_ok &&
    events.valid && (events.count == 1LL) &&
    (events.wall_duration_update_count == 1LL) &&
    harness_terminal_event_order_is_valid(&events) &&
    (events.answer_quality_count == 0LL) && events.saw_thinking &&
    !events.saw_tools && (events.clear_count == 1L);

  db = NULL;
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM answer_quality_audits;",
                           &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks;",
                        &value) && (value == 0LL) &&
      harness_query_int(
        db,
        "SELECT COUNT(*) FROM session_settings WHERE "
        "session_id=(SELECT id FROM sessions LIMIT 1) AND "
        "answer_quality_enabled=0;",
        &value) && (value == 1LL) &&
      harness_query_int(
        db,
        "SELECT COUNT(*) FROM model_requests r "
        "JOIN http_attempts a ON a.request_id=r.id "
        "WHERE r.wall_duration_ms IS NOT NULL AND "
        "r.wall_duration_ms >= a.completed_at_ms-a.started_at_ms;",
        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }

  if (ok) {
    ok = strappy_db_list_response_timeline(path,
                                           session_id,
                                           &timeline,
                                           &error);
  }
  saw_answer = 0;
  for (index = 0U; ok && (index < timeline.count); index++) {
    const strappy_session_message_record *record;

    record = &timeline.records[index];
    if ((record->role != NULL) &&
        (strcmp(record->role, "answer_quality") == 0)) {
      ok = 0;
    }
    if ((record->role != NULL) &&
        (strcmp(record->role, "assistant") == 0) &&
        (record->content != NULL) &&
        (strcmp(record->content, answer) == 0)) {
      saw_answer = 1;
    }
  }
  ok = ok && saw_answer;
  if (!ok) {
    fprintf(stderr,
            "Answer-quality-disabled integration failed: %s\n",
            (error != NULL) ? error : "request or storage mismatch");
  }

cleanup:
  free(result);
  free(error);
  strappy_session_options_destroy(&options);
  strappy_session_message_record_list_destroy(&timeline);
  unlink(path);
  return ok;
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
  strappy_response_timeline_cursor tail_cursor;
  strappy_session_message_record_list timeline;
  strappy_session_message_record_list timeline_tail;
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
  strappy_response_timeline_cursor_init(&tail_cursor);
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
      (timeline.count >= 3U) &&
      harness_direct_timeline_cursor_matches(path,
                                             session_id,
                                             &timeline,
                                             &error);
  }
  if (ok) {
    tail_cursor = timeline.records[timeline.count - 3U].timeline_cursor;
    ok = strappy_db_list_response_timeline_after(path,
                                                 session_id,
                                                 &tail_cursor,
                                                 &timeline_tail,
                                                 NULL,
                                                 &error) &&
      (timeline_tail.count == 2U) &&
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
      !strappy_db_update_session_bash_enabled(path,
                                              session_id,
                                              1,
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
  char *first_page_html;
  char *first_timeline_cursor;
  char *first_result;
  char *followup_append_script;
  char *next_timeline_cursor;
  char *second_result;
  sqlite3 *db;
  strappy_session_record session;
  size_t first_message_count;
  size_t appended_message_count;
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
  first_page_html = NULL;
  first_timeline_cursor = NULL;
  first_result = NULL;
  followup_append_script = NULL;
  next_timeline_cursor = NULL;
  second_result = NULL;
  first_message_count = 0U;
  appended_message_count = 0U;
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
    first_page_html =
      strappy_session_webview_messages_page_html_for_session(
        path,
        session_id,
        "../shared/Resources",
        NULL,
        NULL,
        STRAPPY_WEBVIEW_PALETTE_APPLICATION_TINTED,
        &first_message_count,
        &first_timeline_cursor,
        &error);
    if ((first_page_html != NULL) &&
        (strstr(first_page_html,
                HARNESS_WORLD_PREFLIGHT_ASSISTANT_TEXT) != NULL) &&
        (strstr(first_page_html, "call_pf_0_") != NULL)) {
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

  if (ok) {
    followup_append_script =
      strappy_session_webview_append_messages_js_for_session(
        path,
        session_id,
        "../shared/Resources",
        first_timeline_cursor,
        &appended_message_count,
        &next_timeline_cursor,
        &error);
    ok = (followup_append_script != NULL) &&
      (first_message_count > 0U) &&
      (appended_message_count > 0U) &&
      (next_timeline_cursor != NULL) &&
      (strcmp(next_timeline_cursor, first_timeline_cursor) != 0) &&
      (strstr(followup_append_script,
              HARNESS_WORLD_PREFLIGHT_ASSISTANT_TEXT) == NULL) &&
      (strstr(followup_append_script, "call_pf_0_") == NULL) &&
      (strstr(followup_append_script, "Second prompt") != NULL) &&
      (strstr(followup_append_script, "Second round answer.") != NULL);
  }

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
                        "m.role='assistant' AND p.part_type='output_text' AND "
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
  free(followup_append_script);
  free(first_page_html);
  free(first_timeline_cursor);
  free(next_timeline_cursor);
  free(error);
  unlink(path);
  return ok;
}

static int harness_test_isolated_prompt_context(void)
{
  char path[] = "/tmp/strappy-responses-isolated-XXXXXX";
  char endpoint[128];
  char *error;
  char *first_result;
  char *second_result;
  sqlite3 *db;
  long long session_id;
  long long value;
  pid_t server_pid;
  int fd;
  int first_context_excluded;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create isolated-prompt harness database.");
  }
  close(fd);
  error = NULL;
  first_result = NULL;
  second_result = NULL;
  first_context_excluded = 0;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !strappy_session_update_assistant_set(
        path,
        session_id,
        "../shared/Resources",
        STRAPPY_ASSISTANT_SET_DATABASE_STUDY,
        &error) ||
      !strappy_db_update_session_name(
        path,
        session_id,
        STRAPPY_ASSISTANT_SET_DATABASE_STUDY_SESSION_NAME,
        &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_ISOLATED_PROMPTS,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare isolated-prompt test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  first_result =
    strappy_responses_send_isolated_prompt_for_session_and_store_with_events(
      "First isolated prompt",
      "/dev/null",
      endpoint,
      "test-token",
      "../shared/Resources",
      path,
      session_id,
      NULL,
      NULL,
      &error);
  if ((first_result != NULL) &&
      (strcmp(first_result, "First isolated answer.") == 0) &&
      (error == NULL)) {
    if (sqlite3_open(path, &db) == SQLITE_OK) {
      first_context_excluded =
        harness_query_int(db,
                          "SELECT COUNT(*) FROM conversation_items "
                          "WHERE include_in_context=1;",
                          &value) && (value == 0LL) &&
        harness_query_int(db,
                          "SELECT COUNT(*) FROM conversation_items "
                          "WHERE include_in_context=0;",
                          &value) && (value > 0LL);
      sqlite3_close(db);
    }
    if (first_context_excluded) {
      second_result =
        strappy_responses_send_isolated_prompt_for_session_and_store_with_events(
          "Second isolated prompt",
          "/dev/null",
          endpoint,
          "test-token",
          "../shared/Resources",
          path,
          session_id,
          NULL,
          NULL,
          &error);
    }
  }
  server_ok = harness_wait_for_server(
    server_pid,
    (first_result == NULL) || (second_result == NULL));
  ok = (first_result != NULL) &&
    (strcmp(first_result, "First isolated answer.") == 0) &&
    first_context_excluded &&
    (second_result != NULL) &&
    (strcmp(second_result, "Second isolated answer.") == 0) &&
    server_ok && (error == NULL);
  free(first_result);
  free(second_result);

  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM turns;",
                           &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests;",
                        &value) && (value == 3LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "request_kind='user' AND previous_request_id IS NULL;",
                        &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "input_from_sequence=1 AND "
                        "input_through_sequence=4;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "request_kind='tool_continuation' AND "
                        "input_from_sequence=1 AND "
                        "input_through_sequence=6 AND "
                        "new_input_from_sequence=6;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests r "
                        "JOIN conversation_items i ON "
                        "i.sequence=r.input_through_sequence AND "
                        "i.introduced_request_id=r.id "
                        "JOIN message_items m ON m.item_id=i.id "
                        "JOIN item_text_parts p ON p.item_id=i.id WHERE "
                        "r.input_from_sequence=1 AND "
                        "r.input_through_sequence=8 AND "
                        "r.new_input_from_sequence=8 AND "
                        "m.role='user' AND p.text='Second isolated prompt';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items;",
                        &value) && (value == 9LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items "
                        "WHERE include_in_context=1;",
                        &value) && (value == 0LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items "
                        "WHERE include_in_context=0;",
                        &value) && (value == 9LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM item_text_parts WHERE text IN "
                        "('First isolated prompt','First isolated answer.',"
                        "'Second isolated prompt','Second isolated answer.');",
                        &value) && (value == 4LL) &&
      harness_query_int(
        db,
        "SELECT COUNT(*) FROM sessions WHERE name='"
        STRAPPY_ASSISTANT_SET_DATABASE_STUDY_SESSION_NAME "';",
        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_audits;",
                        &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks;",
                        &value) && (value == 4LL) &&
      harness_query_int(
        db,
        "SELECT COUNT(*) FROM answer_quality_checks WHERE check_key IN ("
        "'answer_non_empty','database_context');",
        &value) && (value == 4LL) &&
      harness_query_int(
        db,
        "SELECT COUNT(*) FROM answer_quality_checks WHERE check_key IN ("
        "'unicode_emoji_absent','web_reference','session_rename',"
        "'fontawesome_confirm');",
        &value) && (value == 0LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Isolated prompt context was not cleared between batches: %s\n",
            (error != NULL) ? error : "request or ledger mismatch");
  }
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

typedef struct harness_session_update_events {
  const char *db_path;
  long count;
  int valid;
} harness_session_update_events;

static int harness_record_session_update_event(
  const strappy_responses_event *event,
  void *user_data)
{
  harness_session_update_events *events;
  sqlite3 *db;
  long long named_session_count;
  long long tool_execution_count;
  int event_ok;

  events = (harness_session_update_events *)user_data;
  if ((events == NULL) || (event == NULL) ||
      (event->type != STRAPPY_RESPONSES_EVENT_SESSION_UPDATED)) {
    return 1;
  }

  db = NULL;
  named_session_count = 0LL;
  tool_execution_count = 0LL;
  event_ok =
    (event->prompt_group_key != NULL) &&
    (event->prompt_group_key[0] != '\0') &&
    (event->actor != NULL) &&
    (strcmp(event->actor, "application") == 0) &&
    (event->kind != NULL) &&
    (strcmp(event->kind, "session") == 0) &&
    (event->status_kind != NULL) &&
    (strcmp(event->status_kind, "name") == 0) &&
    !strappy_session_webview_event_requires_message_update(event) &&
    (sqlite3_open(events->db_path, &db) == SQLITE_OK) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM sessions "
                      "WHERE name='Empty Answer Audit';",
                      &named_session_count) &&
    harness_query_int(db,
                      "SELECT COUNT(*) FROM tool_executions;",
                      &tool_execution_count) &&
    (named_session_count == 1LL) &&
    (tool_execution_count == 2LL);
  if (db != NULL) {
    sqlite3_close(db);
  }
  if (!event_ok) {
    events->valid = 0;
  }
  events->count++;
  return 1;
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
  harness_session_update_events events;
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
  memset(&events, 0, sizeof(events));
  events.db_path = path;
  events.valid = 1;
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

  result = strappy_responses_send_prompt_for_session_and_store_with_events(
    "Audit empty answer after tools",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    harness_record_session_update_event,
    &events,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) && (result[0] == '\0') &&
    server_ok && (error == NULL) && events.valid && (events.count == 1L);
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
                        &value) && (value == 4LL) &&
      harness_query_int(db,
                        "SELECT COUNT(DISTINCT f.tool_name) FROM "
                        "tool_executions e JOIN function_calls f "
                        "ON f.item_id=e.function_call_item_id "
                        "WHERE f.tool_name IN ("
                        "'database_context',"
                        "'session_rename',"
                        "'fontawesome_confirm',"
                        "'memory_save');",
                        &value) && (value == 4LL) &&
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

static int harness_test_native_web_search_persists_citations(void)
{
  char path[] = "/tmp/strappy-responses-native-web-search-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  strappy_response_item_raw_record_list context;
  strappy_session_message_record_list timeline;
  long long session_id;
  long long value;
  size_t index;
  pid_t server_pid;
  int context_has_search;
  int timeline_has_search;
  int timeline_has_citation;
  int fd;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create native web-search harness database.");
  }
  close(fd);
  error = NULL;
  session_id = 0LL;
  if (!harness_create_session_database(path, &session_id, &error) ||
      !strappy_db_update_session_web_provider(path,
                                              session_id,
                                              STRAPPY_WEB_PROVIDER_AUTO,
                                              &error) ||
      !harness_start_server(HARNESS_RESPONSES_SERVER_NATIVE_WEB_SEARCH,
                            endpoint,
                            sizeof(endpoint),
                            &server_pid)) {
    fprintf(stderr,
            "Could not prepare native web-search integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  result = strappy_responses_send_prompt_for_session_and_store(
    "Use native web search",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    &error);
  server_ok = harness_wait_for_server(server_pid, result == NULL);
  ok = (result != NULL) &&
    (strcmp(result, "Citation-backed answer.") == 0) && server_ok;
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM conversation_items WHERE "
                           "kind='openrouter:web_search' AND "
                           "include_in_context=1;",
                           &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM item_citations WHERE "
                        "citation_type='url_citation' AND "
                        "url='https://example.com/article';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_checks WHERE "
                        "check_key='web_reference' AND status='passed';",
                        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }

  strappy_response_item_raw_record_list_init(&context);
  strappy_session_message_record_list_init(&timeline);
  context_has_search = 0;
  timeline_has_search = 0;
  timeline_has_citation = 0;
  if (ok) {
    ok = strappy_db_list_canonical_response_items(path,
                                                  session_id,
                                                  &context,
                                                  &error) &&
      strappy_db_list_response_timeline(path,
                                        session_id,
                                        &timeline,
                                        &error);
  }
  for (index = 0U; ok && (index < context.count); index++) {
    if ((context.records[index].raw_json != NULL) &&
        (strstr(context.records[index].raw_json,
                "\"type\":\"web_search_call\"") != NULL) &&
        (strstr(context.records[index].raw_json,
                "\"queries\":[\"current test\"]") != NULL)) {
      context_has_search = 1;
    }
  }
  for (index = 0U; ok && (index < timeline.count); index++) {
    if ((timeline.records[index].role != NULL) &&
        (strcmp(timeline.records[index].role, "api_item") == 0) &&
        (timeline.records[index].kind != NULL) &&
        (strcmp(timeline.records[index].kind,
                STRAPPY_RESPONSE_ITEM_WEB_SEARCH_CALL) == 0) &&
        (timeline.records[index].response_item_action_json != NULL) &&
        (strstr(timeline.records[index].response_item_action_json,
                "\"queries\":[\"current test\"]") != NULL) &&
        (strstr(timeline.records[index].response_item_action_json,
                "https://example.com/article") != NULL)) {
      timeline_has_search = 1;
    }
    if ((timeline.records[index].role != NULL) &&
        (strcmp(timeline.records[index].role, "assistant") == 0) &&
        (timeline.records[index].content != NULL) &&
        (strstr(timeline.records[index].content, "Sources:") != NULL) &&
        (strstr(timeline.records[index].content,
                "[Example \\[Source\\]](<https://example.com/article>)") !=
         NULL)) {
      timeline_has_citation = 1;
    }
  }
  ok = ok && context_has_search && timeline_has_search &&
    timeline_has_citation;
  strappy_response_item_raw_record_list_destroy(&context);
  strappy_session_message_record_list_destroy(&timeline);
  if (!ok) {
    fprintf(stderr,
            "Native web-search persistence check failed: %s\n",
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
    (events.completion_event_count == 2LL) &&
    (events.coalesced_transition_count == 1LL) &&
    (events.coalescible_completion_count == 1LL) &&
    (events.tool_outputs_event_count == 1LL) &&
    (events.wall_duration_update_count == 1LL) &&
    (events.message_update_skipped_count == 2LL) &&
    (events.message_update_required_count == 4LL) &&
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
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "wall_duration_ms IS NOT NULL;",
                        &value) && (value == 2LL) &&
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
        (strcmp(record->tool_name, "database_context") == 0) &&
        (record->arguments_json != NULL) &&
        (strcmp(record->arguments_json, "{\"unexpected\":true}") == 0);
    }
  }
  if (ok && !saw_named_output) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Function-tool continuation failed: %s "
            "valid=%d calls=%lld starts=%lld completion_events=%lld "
            "coalesced=%lld wall_updates=%lld update_skipped=%lld "
            "update_required=%lld\n",
            (error != NULL) ? error : "request or ledger mismatch",
            events.valid,
            events.count,
            events.started_count,
            events.completion_event_count,
            events.coalesced_transition_count,
            events.wall_duration_update_count,
            events.message_update_skipped_count,
            events.message_update_required_count);
  }
  strappy_session_message_record_list_destroy(&timeline);
  free(error);
  unlink(path);
  return ok;
}

typedef struct harness_round_limit_events {
  long error_update_count;
  long wall_update_count;
  int valid;
} harness_round_limit_events;

static int harness_record_round_limit_event(
  const strappy_responses_event *event,
  void *user_data)
{
  harness_round_limit_events *events;

  events = (harness_round_limit_events *)user_data;
  if ((events == NULL) || (event == NULL)) {
    return 1;
  }
  if (event->type == STRAPPY_RESPONSES_EVENT_LEDGER_UPDATED) {
    events->wall_update_count++;
    if ((event->kind == NULL) ||
        (strcmp(event->kind, "response_api_call") != 0) ||
        (event->status_kind == NULL) ||
        (strcmp(event->status_kind, "wall_duration") != 0) ||
        (event->message_key == NULL) ||
        (strncmp(event->message_key,
                 "response-call-",
                 strlen("response-call-")) != 0)) {
      events->valid = 0;
    }
    return 1;
  }
  if ((event->type == STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED) &&
      (event->status_kind != NULL) &&
      (strcmp(event->status_kind, "response_error") == 0)) {
    events->error_update_count++;
    if ((event->kind == NULL) ||
        (strcmp(event->kind, "response_api_call") != 0) ||
        (event->render_role == NULL) ||
        (strcmp(event->render_role, "api_error") != 0) ||
        (event->message_key == NULL) ||
        (strncmp(event->message_key,
                 "response-call-",
                 strlen("response-call-")) != 0)) {
      events->valid = 0;
    }
  }
  return 1;
}

static int harness_test_round_limit(void)
{
  char path[] = "/tmp/strappy-responses-round-limit-XXXXXX";
  char endpoint[128];
  char *error;
  char *result;
  sqlite3 *db;
  strappy_session_options options;
  strappy_session_message_record_list timeline;
  harness_round_limit_events events;
  long long session_id;
  long long value;
  size_t timeline_index;
  pid_t server_pid;
  int fd;
  int saw_round_limit_status;
  int server_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create round-limit database.");
  }
  close(fd);
  error = NULL;
  session_id = 0LL;
  saw_round_limit_status = 0;
  memset(&events, 0, sizeof(events));
  events.valid = 1;
  strappy_session_options_init(&options);
  strappy_session_message_record_list_init(&timeline);
  ok = harness_create_session_database(path, &session_id, &error) &&
    strappy_db_load_session_options(path, session_id, &options, &error);
  if (ok) {
    options.round_limit = 2L;
    ok = strappy_db_update_session_options(
      path,
      session_id,
      &options,
      STRAPPY_SESSION_OPTION_ROUND_LIMIT,
      NULL,
      NULL,
      &error);
  }
  if (ok) {
    ok = harness_start_server(HARNESS_RESPONSES_SERVER_ROUND_LIMIT,
                              endpoint,
                              sizeof(endpoint),
                              &server_pid);
  }
  strappy_session_options_destroy(&options);
  if (!ok) {
    fprintf(stderr,
            "Could not prepare round-limit integration test: %s\n",
            (error != NULL) ? error : "server setup failed");
    free(error);
    unlink(path);
    return 0;
  }

  result = strappy_responses_send_prompt_for_session_and_store_with_events(
    "Reach the configured round limit",
    "/dev/null",
    endpoint,
    "test-token",
    "../shared/Resources",
    path,
    session_id,
    harness_record_round_limit_event,
    &events,
    &error);
  server_ok = harness_wait_for_server(server_pid, 0);
  ok = (result == NULL) && (error != NULL) &&
    (strcmp(error, "Response round limit reached.") == 0) && server_ok &&
    events.valid && (events.error_update_count == 1L) &&
    (events.wall_update_count == 1L);
  free(result);
  if (ok && (sqlite3_open(path, &db) == SQLITE_OK)) {
    ok = harness_query_int(db,
                           "SELECT COUNT(*) FROM model_requests;",
                           &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "state='completed';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "wall_duration_ms IS NOT NULL;",
                        &value) && (value == 2LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "state='error' AND round_index=1;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM http_attempts WHERE "
                        "state='completed';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM http_attempts a "
                        "JOIN api_results r ON r.attempt_id=a.id "
                        "WHERE a.state='response_error' "
                        "AND r.provider_status='completed' "
                        "AND r.error_type='harness_error' "
                        "AND r.error_code='round_limit_reached' "
                        "AND r.error_message='Response round limit reached.';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM session_settings WHERE "
                        "session_id > 0 AND round_limit=2;",
                        &value) && (value == 1LL);
    sqlite3_close(db);
  } else if (ok) {
    ok = 0;
  }
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
        (strcmp(record->kind, "response_api_call") == 0) &&
        (record->round_index == 1L)) {
      saw_round_limit_status = record->is_error &&
        (record->attempt_state != NULL) &&
        (strcmp(record->attempt_state, "response_error") == 0) &&
        (record->content != NULL) &&
        (strstr(record->content, "Response round limit reached.") != NULL) &&
        (record->metadata_json != NULL) &&
        (strstr(record->metadata_json,
                "\"code\":\"round_limit_reached\"") != NULL);
    }
  }
  if (ok && !saw_round_limit_status) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr,
            "Configured Responses round limit failed: %s\n",
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
      !strappy_db_update_session_web_search_enabled(path,
                                                    session_id,
                                                    0,
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
      !strappy_db_update_session_web_search_enabled(path,
                                                    session_id,
                                                    0,
                                                    &error) ||
      !strappy_db_update_session_limit_to_one_tool(path,
                                                   session_id,
                                                   1,
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
                        "SELECT COUNT(*) FROM model_requests "
                        "WHERE parallel_tool_calls=0;",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM session_settings WHERE "
                        "session_id > 0 AND bash_enabled=0 "
                        "AND limit_to_one_tool=1;",
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
      !strappy_db_update_session_web_search_enabled(path,
                                                    session_id,
                                                    0,
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
      !strappy_db_update_session_web_search_enabled(path,
                                                    session_id,
                                                    0,
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
    (events.count == 1LL) &&
    (events.completion_event_count == 1LL) &&
    (events.coalesced_transition_count == 0LL) &&
    (events.coalescible_completion_count == 1LL) &&
    (events.tool_outputs_event_count == 1LL) &&
    (events.wall_duration_update_count == 0LL) &&
    (events.clear_count == 1L) &&
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
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests r "
                        "JOIN http_attempts a ON a.request_id=r.id "
                        "JOIN tool_executions e ON e.response_attempt_id=a.id "
                        "WHERE r.wall_duration_ms IS NOT NULL AND "
                        "r.wall_duration_ms >= "
                        "e.completed_at_ms-e.started_at_ms;",
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
    (events.completion_event_count == 3LL) &&
    (events.coalesced_transition_count == 1LL) &&
    (events.coalescible_completion_count == 1LL) &&
    (events.tool_outputs_event_count == 1LL) &&
    (events.wall_duration_update_count == 1LL) &&
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
                        &value) && (value == 11LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM conversation_items WHERE "
                        "introduced_request_id IS NOT NULL AND "
                        "include_in_context=1;",
                        &value) && (value == 9LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM answer_quality_audits WHERE "
                        "outcome='failed';",
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "wall_duration_ms IS NOT NULL;",
                        &value) && (value == 2LL) &&
      harness_query_int(
        db,
        "SELECT (SELECT SUM(wall_duration_ms) FROM model_requests)-"
        "(SELECT SUM(completed_at_ms-started_at_ms) FROM http_attempts);",
        &value) && (value >= 400LL);
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
            "Retry attempt ledger failed: %s valid=%d calls=%lld "
            "starts=%lld completion_events=%lld coalesced=%lld "
            "wall_updates=%lld\n",
            (error != NULL) ? error : "request or ledger mismatch",
            events.valid,
            events.count,
            events.started_count,
            events.completion_event_count,
            events.coalesced_transition_count,
            events.wall_duration_update_count);
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
    (events.wall_duration_update_count == 1LL) &&
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
                        &value) && (value == 1LL) &&
      harness_query_int(
        db,
        "SELECT COUNT(*) FROM model_requests r "
        "JOIN http_attempts a ON a.request_id=r.id "
        "WHERE r.wall_duration_ms IS NOT NULL AND "
        "r.wall_duration_ms >= a.completed_at_ms-a.started_at_ms;",
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
    (events.count == 1LL) &&
    (events.wall_duration_update_count == 1LL) &&
    (events.clear_count == 1L) &&
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
                        &value) && (value == 1LL) &&
      harness_query_int(db,
                        "SELECT COUNT(*) FROM model_requests WHERE "
                        "wall_duration_ms IS NOT NULL;",
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
  ok = strcmp((const char *)sqlite3_column_text(stmt, 0),
              STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER) == 0 &&
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

static int harness_context_selection_matches(
  const char *path,
  long long session_id,
  long long model_request_id,
  size_t expected_context_count,
  size_t expected_eligible_count,
  size_t expected_included_count,
  char **error_out)
{
  strappy_response_item_raw_record_list context;
  strappy_session_message_record_list timeline;
  size_t eligible_count;
  size_t included_count;
  size_t index;
  int ok;

  strappy_response_item_raw_record_list_init(&context);
  strappy_session_message_record_list_init(&timeline);
  eligible_count = 0U;
  included_count = 0U;
  ok = strappy_db_list_canonical_response_items(path,
                                                session_id,
                                                &context,
                                                error_out) &&
    (context.count == expected_context_count) &&
    strappy_db_list_response_timeline(path,
                                      session_id,
                                      &timeline,
                                      error_out);
  if (ok) {
    for (index = 0U; index < timeline.count; index++) {
      const strappy_session_message_record *record;

      record = &timeline.records[index];
      if ((record->model_request_id != model_request_id) ||
          !record->can_include_in_context) {
        continue;
      }
      eligible_count++;
      if (record->include_in_context) {
        included_count++;
      }
    }
    ok = (eligible_count == expected_eligible_count) &&
      (included_count == expected_included_count);
  }
  strappy_response_item_raw_record_list_destroy(&context);
  strappy_session_message_record_list_destroy(&timeline);
  return ok;
}

static int harness_append_usage_metrics_call(
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
  long long http_wait_ms,
  long long wall_duration_ms,
  long long *call_id_out,
  char **error_out)
{
  static const char *request_json =
    "{\"model\":\"" STRAPPY_CONFIG_DEFAULT_API_MODEL
    "\",\"stream\":false,\"store\":false,"
    "\"instructions\":\"System\",\"input\":[]}";
  strappy_response_call_begin_input begin;
  strappy_response_call_finish_input finish;
  long long call_id;

  memset(&begin, 0, sizeof(begin));
  begin.session_id = session_id;
  begin.previous_call_id = previous_call_id;
  begin.provider_account_id = STRAPPY_PROVIDER_ACCOUNT_OPENROUTER;
  begin.model_id = STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER;
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
  finish.completed_at_ms = finish.started_at_ms + http_wait_ms;
  finish.request_bytes = (long long)strlen(request_json);
  finish.response_bytes = (long long)strlen(response_json);
  finish.total_seconds = (double)http_wait_ms / 1000.0;
  finish.effective_url = begin.request_url;
  finish.content_type = "application/json";
  finish.response_headers = "";
  finish.response_json = response_json;
  if (!strappy_db_finish_response_call(path, &finish, error_out)) {
    return 0;
  }
  if ((wall_duration_ms >= 0LL) &&
      !strappy_db_set_response_round_wall_duration(path,
                                                   call_id,
                                                   wall_duration_ms,
                                                   error_out)) {
    return 0;
  }
  if (call_id_out != NULL) {
    *call_id_out = call_id;
  }
  return 1;
}

static int harness_test_cumulative_session_metrics(void)
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
  static const char *pending_request =
    "{\"model\":\"" STRAPPY_CONFIG_DEFAULT_API_MODEL
    "\",\"stream\":false,\"store\":false,"
    "\"instructions\":\"System\",\"input\":[{\"type\":\"message\","
    "\"role\":\"user\",\"content\":[{\"type\":\"input_text\","
    "\"text\":\"Pending metrics\"}]}]}";
  static const char *pending_response =
    "{\"id\":\"cost-pending\",\"status\":\"completed\","
    "\"model\":\"test/model\",\"output\":[],"
    "\"usage\":{\"cost\":0.001}}";
  char path[] = "/tmp/strappy-cumulative-cost-XXXXXX";
  strappy_response_call_begin_input pending_begin;
  strappy_response_call_finish_input pending_finish;
  strappy_response_timeline_cursor pending_cursor;
  strappy_response_timeline_cursor range_cursor;
  strappy_session_message_record_list ranged_timeline;
  strappy_session_message_record_list timeline;
  char *error;
  long long pending_call_id;
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
  pending_call_id = 0LL;
  session_id = 0LL;
  previous_call_id = 0LL;
  strappy_response_timeline_cursor_init(&pending_cursor);
  strappy_response_timeline_cursor_init(&range_cursor);
  strappy_session_message_record_list_init(&timeline);
  strappy_session_message_record_list_init(&ranged_timeline);
  ok = strappy_db_restore_provider_account(
         path,
         STRAPPY_PROVIDER_ACCOUNT_OPENROUTER,
         STRAPPY_PROVIDER_OPENROUTER,
         STRAPPY_PROVIDER_ACCOUNT_OPENROUTER_NAME,
         NULL,
         &error) &&
    strappy_db_create_session(path, &session_id, &error) &&
    harness_append_usage_metrics_call(path,
                                   session_id,
                                   previous_call_id,
                                   "group-one",
                                   "user",
                                   0L,
                                   0L,
                                   "completed",
                                   0,
                                   cost_one,
                                   1500LL,
                                   2500LL,
                                   &previous_call_id,
                                   &error) &&
    harness_append_usage_metrics_call(path,
                                   session_id,
                                   previous_call_id,
                                   "group-one",
                                   "tool_continuation",
                                   1L,
                                   0L,
                                   "response_error",
                                   1,
                                   cost_error,
                                   2500LL,
                                   -1LL,
                                   &previous_call_id,
                                   &error) &&
    harness_append_usage_metrics_call(path,
                                   session_id,
                                   previous_call_id,
                                   "group-one",
                                   "retry",
                                   1L,
                                   1L,
                                   "completed",
                                   0,
                                   cost_retry,
                                   500LL,
                                   7000LL,
                                   &previous_call_id,
                                   &error) &&
    harness_append_usage_metrics_call(path,
                                   session_id,
                                   previous_call_id,
                                   "group-two",
                                   "user",
                                   0L,
                                   0L,
                                   "completed",
                                   0,
                                   cost_missing,
                                   317500LL,
                                   400000LL,
                                   &previous_call_id,
                                   &error) &&
    harness_append_usage_metrics_call(path,
                                   session_id,
                                   previous_call_id,
                                   "group-two",
                                   "tool_continuation",
                                   1L,
                                   0L,
                                   "completed",
                                   0,
                                   cost_final,
                                   1000LL,
                                   2000LL,
                                   &previous_call_id,
                                   &error);
  if (ok) {
    memset(&pending_begin, 0, sizeof(pending_begin));
    pending_begin.session_id = session_id;
    pending_begin.previous_call_id = previous_call_id;
    pending_begin.provider_account_id = STRAPPY_PROVIDER_ACCOUNT_OPENROUTER;
    pending_begin.model_id = STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER;
    pending_begin.prompt_group_key = "group-three";
    pending_begin.request_kind = "user";
    pending_begin.round_index = 0L;
    pending_begin.attempt_index = 0L;
    pending_begin.new_input_start_index = 0L;
    pending_begin.request_method = "POST";
    pending_begin.request_url = "https://openrouter.ai/api/v1/responses";
    pending_begin.request_headers_json = "{}";
    pending_begin.request_json = pending_request;
    ok = strappy_db_begin_response_call(path,
                                        &pending_begin,
                                        &pending_call_id,
                                        &error);
  }
  if (ok) {
    ok = strappy_db_list_response_timeline(path,
                                           session_id,
                                           &timeline,
                                           &error) &&
    (timeline.count == 6U) &&
    (timeline.records[0].prompt_index == 0L) &&
    timeline.records[0].has_cumulative_usage_cost &&
    timeline.records[0].has_cumulative_wait_ms &&
    (timeline.records[0].cumulative_wait_ms == 2500LL) &&
    (strcmp(timeline.records[0].attempt_state, "completed") == 0) &&
    harness_double_matches(timeline.records[0].cumulative_usage_cost, 0.001) &&
    timeline.records[1].has_cumulative_usage_cost &&
    timeline.records[1].has_cumulative_wait_ms &&
    (timeline.records[1].cumulative_wait_ms == 9500LL) &&
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
    (timeline.records[2].cumulative_wait_ms == 9500LL) &&
    harness_double_matches(timeline.records[2].cumulative_usage_cost, 0.0075) &&
    (strcmp(timeline.records[3].prompt_group_key, "group-two") == 0) &&
    (timeline.records[3].prompt_index == 1L) &&
    (timeline.records[3].round_index == 0L) &&
    (timeline.records[3].cumulative_wait_ms == 409500LL) &&
    harness_double_matches(timeline.records[3].cumulative_usage_cost, 0.0075) &&
    (timeline.records[4].cumulative_wait_ms == 411500LL) &&
    harness_double_matches(timeline.records[4].cumulative_usage_cost, 0.008) &&
    (timeline.records[5].direction != NULL) &&
    (strcmp(timeline.records[5].direction, "request") == 0) &&
    (timeline.records[5].http_attempt_id == 0LL) &&
    !timeline.records[5].has_cumulative_usage_cost &&
    !timeline.records[5].has_cumulative_wait_ms &&
    (timeline.records[5].cumulative_usage_cost == 0.0) &&
    (timeline.records[5].cumulative_wait_ms == 0LL);
  }

  if (ok) {
    pending_cursor = timeline.records[5].timeline_cursor;
    range_cursor = timeline.records[1].timeline_cursor;
    ok = strappy_db_list_response_timeline_after(path,
                                                 session_id,
                                                 &range_cursor,
                                                 &ranged_timeline,
                                                 NULL,
                                                 &error) &&
      (ranged_timeline.count == 4U) &&
      harness_double_matches(
        ranged_timeline.records[0].cumulative_usage_cost, 0.0075) &&
      (ranged_timeline.records[0].cumulative_wait_ms == 9500LL) &&
      harness_double_matches(
        ranged_timeline.records[1].cumulative_usage_cost, 0.0075) &&
      (ranged_timeline.records[1].cumulative_wait_ms == 409500LL) &&
      harness_double_matches(
        ranged_timeline.records[2].cumulative_usage_cost, 0.008) &&
      (ranged_timeline.records[2].cumulative_wait_ms == 411500LL) &&
      !ranged_timeline.records[3].has_cumulative_usage_cost &&
      !ranged_timeline.records[3].has_cumulative_wait_ms &&
      (ranged_timeline.records[3].cumulative_usage_cost == 0.0) &&
      (ranged_timeline.records[3].cumulative_wait_ms == 0LL);
  }

  if (ok) {
    strappy_session_message_record_list_destroy(&ranged_timeline);
    strappy_session_message_record_list_init(&ranged_timeline);
    memset(&pending_finish, 0, sizeof(pending_finish));
    pending_finish.call_id = pending_call_id;
    pending_finish.state = "completed";
    pending_finish.output_is_canonical = 1;
    pending_finish.http_status = 200L;
    pending_finish.started_at_ms = pending_call_id * 1000LL;
    pending_finish.completed_at_ms = pending_finish.started_at_ms + 500LL;
    pending_finish.request_bytes = (long long)strlen(pending_request);
    pending_finish.response_bytes = (long long)strlen(pending_response);
    pending_finish.total_seconds = 0.5;
    pending_finish.effective_url = pending_begin.request_url;
    pending_finish.content_type = "application/json";
    pending_finish.response_headers = "";
    pending_finish.response_json = pending_response;
    ok = strappy_db_finish_response_call(path, &pending_finish, &error) &&
      strappy_db_set_response_round_wall_duration(path,
                                                  pending_call_id,
                                                  3000LL,
                                                  &error) &&
      strappy_db_list_response_timeline_after(path,
                                               session_id,
                                               &pending_cursor,
                                               &ranged_timeline,
                                               NULL,
                                               &error) &&
      (ranged_timeline.count == 1U) &&
      ranged_timeline.records[0].has_cumulative_usage_cost &&
      ranged_timeline.records[0].has_cumulative_wait_ms &&
      harness_double_matches(
        ranged_timeline.records[0].cumulative_usage_cost, 0.009) &&
      (ranged_timeline.records[0].cumulative_wait_ms == 414500LL);
  }

  if (!ok) {
    fprintf(stderr,
            "Cumulative session metrics failed: %s\n",
            (error != NULL) ? error : "timeline total mismatch");
  }
  strappy_session_message_record_list_destroy(&ranged_timeline);
  strappy_session_message_record_list_destroy(&timeline);
  free(error);
  unlink(path);
  return ok;
}

static int harness_verify_invalid_structured_text_webview_recovery(
  const char *path,
  long long session_id,
  long long call_id,
  char **error_out)
{
  static const char *continuation_request_json =
    "{\"model\":\"" STRAPPY_CONFIG_DEFAULT_API_MODEL
    "\",\"stream\":false,\"store\":false,"
    "\"instructions\":\"System\",\"input\":[{"
    "\"type\":\"function_call_output\",\"call_id\":\"call-test\","
    "\"output\":{\"value\":\"safe\"}}],\"max_output_tokens\":100,"
    "\"parallel_tool_calls\":true,\"tools\":[],"
    "\"tool_choice\":\"auto\"}";
  static const char *invalid_update_sql =
    "UPDATE structured_nodes "
    "SET text_value=CAST(X'62706C6973743030D4010203' AS TEXT) "
    "WHERE value_type='string' AND member_name='value' "
    "AND document_id IN ("
      "SELECT d.id FROM structured_documents d "
      "JOIN function_outputs o ON o.item_id=d.owner_item_id "
      "JOIN function_calls c ON c.item_id=o.function_call_item_id "
      "WHERE d.purpose='output' AND c.provider_call_id='call-test'"
    ");";
  strappy_response_tool_execution_input execution;
  strappy_response_call_begin_input continuation;
  sqlite3 *db;
  char *append_cursor;
  char *append_script;
  char *before_cursor;
  char *before_page;
  char *page_cursor;
  char *page_html;
  char *sqlite_error;
  size_t append_count;
  size_t before_count;
  size_t page_count;
  long long continuation_call_id;
  int ok;
  int rc;

  append_cursor = NULL;
  append_script = NULL;
  before_cursor = NULL;
  before_page = NULL;
  db = NULL;
  page_cursor = NULL;
  page_html = NULL;
  sqlite_error = NULL;
  append_count = 0U;
  before_count = 0U;
  page_count = 0U;
  continuation_call_id = 0LL;
  ok = strappy_webview_configure_localized_labels(error_out) ? 1 : 0;
  if (!ok) {
    goto cleanup;
  }

  before_page = strappy_session_webview_messages_page_html_for_session(
    path,
    session_id,
    "../shared/Resources",
    NULL,
    NULL,
    STRAPPY_WEBVIEW_PALETTE_APPLICATION_TINTED,
    &before_count,
    &before_cursor,
    error_out);
  if ((before_page == NULL) || (before_cursor == NULL)) {
    ok = 0;
    goto cleanup;
  }

  memset(&execution, 0, sizeof(execution));
  execution.session_id = session_id;
  execution.response_call_id = call_id;
  execution.output_index = 1L;
  execution.call_id = "call-test";
  execution.tool_name = "database_list";
  execution.arguments_json = "{}";
  execution.status = "completed";
  execution.output_json = "{\"value\":\"safe\"}";
  execution.started_at_ms = 1200LL;
  execution.completed_at_ms = 1210LL;
  if (!strappy_db_save_response_tool_execution(path, &execution, error_out)) {
    ok = 0;
    goto cleanup;
  }

  memset(&continuation, 0, sizeof(continuation));
  continuation.session_id = session_id;
  continuation.provider_account_id = STRAPPY_PROVIDER_ACCOUNT_OPENROUTER;
  continuation.model_id = STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER;
  continuation.prompt_group_key = "group-test";
  continuation.request_kind = "tool_continuation";
  continuation.round_index = 1L;
  continuation.attempt_index = 0L;
  continuation.previous_call_id = call_id;
  continuation.new_input_start_index = 0L;
  continuation.request_method = "POST";
  continuation.request_url = "https://openrouter.ai/api/v1/responses";
  continuation.request_json = continuation_request_json;
  if (!strappy_db_begin_response_call(path,
                                      &continuation,
                                      &continuation_call_id,
                                      error_out)) {
    ok = 0;
    goto cleanup;
  }

  rc = sqlite3_open(path, &db);
  if (rc != SQLITE_OK) {
    strappy_set_error(error_out,
                      "Could not open the invalid UTF-8 ledger fixture.");
    ok = 0;
    goto cleanup;
  }
  rc = sqlite3_exec(db,
                    invalid_update_sql,
                    NULL,
                    NULL,
                    &sqlite_error);
  if ((rc != SQLITE_OK) || (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(
      error_out,
      "Could not create the invalid UTF-8 ledger fixture: %s",
      (sqlite_error != NULL) ? sqlite_error : "unexpected changed row count");
    ok = 0;
    goto cleanup;
  }
  sqlite3_close(db);
  db = NULL;

  page_html = strappy_session_webview_messages_page_html_for_session(
    path,
    session_id,
    "../shared/Resources",
    NULL,
    NULL,
    STRAPPY_WEBVIEW_PALETTE_APPLICATION_TINTED,
    &page_count,
    &page_cursor,
    error_out);
  append_script = strappy_session_webview_append_messages_js_for_session(
    path,
    session_id,
    "../shared/Resources",
    before_cursor,
    &append_count,
    &append_cursor,
    error_out);
  ok = (page_html != NULL) && (page_cursor != NULL) &&
    (append_script != NULL) && (append_cursor != NULL) &&
    (page_count > before_count) &&
    (append_count == (page_count - before_count)) &&
    (strcmp(page_cursor, append_cursor) == 0) &&
    strappy_utf8_validate(page_html, strlen(page_html)) &&
    strappy_utf8_validate(append_script, strlen(append_script)) &&
    (strstr(page_html, "bplist00") != NULL) &&
    (strstr(page_html, "\xEF\xBF\xBD") != NULL) &&
    (strstr(append_script, "bplist00") != NULL) &&
    (strstr(append_script, "\xEF\xBF\xBD") != NULL);
  if (!ok && (error_out != NULL) && (*error_out == NULL)) {
    strappy_set_error(
      error_out,
      "Invalid structured UTF-8 did not render safely in both WebView paths.");
  }

cleanup:
  if (db != NULL) {
    sqlite3_close(db);
  }
  sqlite3_free(sqlite_error);
  strappy_session_free_string(append_cursor);
  strappy_session_free_string(append_script);
  strappy_session_free_string(before_cursor);
  strappy_session_free_string(before_page);
  strappy_session_free_string(page_cursor);
  strappy_session_free_string(page_html);
  return ok;
}

static int harness_test_ledger(void)
{
  static const char *request_json =
    "{\"model\":\"" STRAPPY_CONFIG_DEFAULT_API_MODEL
    "\",\"stream\":false,\"store\":false,"
    "\"instructions\":\"System\",\"input\":[{\"type\":\"message\","
    "\"role\":\"user\",\"content\":[{\"type\":\"input_text\","
    "\"text\":\"Hello\"}]}],\"max_output_tokens\":100,"
    "\"parallel_tool_calls\":true,"
    "\"reasoning\":{\"enabled\":true},"
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
  strappy_response_item_raw_record_list context;
  strappy_session_message_record_list timeline;
  sqlite3 *db;
  char *error;
  long long session_id;
  long long call_id;
  long long model_request_id;
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
  model_request_id = 0LL;
  strappy_response_item_raw_record_list_init(&context);
  strappy_session_message_record_list_init(&timeline);
  ok = strappy_db_restore_provider_account(
         path,
         STRAPPY_PROVIDER_ACCOUNT_OPENROUTER,
         STRAPPY_PROVIDER_OPENROUTER,
         STRAPPY_PROVIDER_ACCOUNT_OPENROUTER_NAME,
         NULL,
         &error) &&
    strappy_db_create_session(path, &session_id, &error);
  if (!ok) {
    fprintf(stderr, "Could not create harness session: %s\n", error);
    free(error);
    unlink(path);
    return 0;
  }

  memset(&begin, 0, sizeof(begin));
  begin.session_id = session_id;
  begin.provider_account_id = STRAPPY_PROVIDER_ACCOUNT_OPENROUTER;
  begin.model_id = STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER;
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
    (timeline.count == 1U) &&
    (strcmp(timeline.records[0].role, "user") == 0) &&
    (strcmp(timeline.records[0].direction, "request") == 0) &&
    (timeline.records[0].model_request_id > 0LL) &&
    (timeline.records[0].http_attempt_id == 0LL);
  strappy_session_message_record_list_destroy(&timeline);
  if (!ok) {
    fprintf(stderr,
            "Running Responses request was not isolated in the UI timeline: %s\n",
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
  ok = strappy_db_finish_response_call(path, &finish, &error) &&
    strappy_db_set_response_round_wall_duration(path,
                                                call_id,
                                                250LL,
                                                &error);
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
    harness_query_int(db,
                      "SELECT wall_duration_ms FROM model_requests LIMIT 1;",
                      &value) && (value == 250LL) &&
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
    timeline.records[0].can_include_in_context &&
    (timeline.records[0].round_index == 0L) &&
    (timeline.records[0].attempt_index == -1L) &&
    (timeline.records[0].model_request_id > 0LL) &&
    (timeline.records[0].http_attempt_id == 0LL) &&
    (strcmp(timeline.records[1].role, "api_call") == 0) &&
    !timeline.records[1].can_include_in_context &&
    (timeline.records[1].direction == NULL) &&
    (timeline.records[1].model_request_id ==
     timeline.records[0].model_request_id) &&
    (timeline.records[1].http_attempt_id == call_id) &&
    (timeline.records[1].round_index == 0L) &&
    (timeline.records[1].attempt_index == 0L) &&
    timeline.records[1].has_cumulative_wait_ms &&
    (timeline.records[1].cumulative_wait_ms == 250LL) &&
    (strcmp(timeline.records[1].attempt_state, "completed") == 0) &&
    (strcmp(timeline.records[1].request_method, "POST") == 0) &&
    (strcmp(timeline.records[1].request_endpoint,
            "https://openrouter.ai/api/v1/responses") == 0) &&
    (strstr(timeline.records[1].metadata_json,
            "\"id\":\"resp-test\"") != NULL) &&
    (strstr(timeline.records[1].metadata_json, "\"usage\"") != NULL) &&
    (strstr(timeline.records[1].metadata_json, "http_status") == NULL) &&
    (strstr(timeline.records[1].content,
            "Model: " STRAPPY_CONFIG_DEFAULT_API_MODEL) != NULL) &&
    (strstr(timeline.records[1].content,
            "Request: POST https://openrouter.ai/api/v1/responses") != NULL) &&
    (strstr(timeline.records[1].content, "HTTP 200") == NULL) &&
    (strstr(timeline.records[1].content, "completed") == NULL) &&
    (strcmp(timeline.records[2].role, "api_reasoning") == 0) &&
    timeline.records[2].can_include_in_context &&
    (strcmp(timeline.records[2].direction, "response") == 0) &&
    (timeline.records[2].http_attempt_id == call_id) &&
    (strcmp(timeline.records[3].role, "api_function_call") == 0) &&
    timeline.records[3].can_include_in_context &&
    (strcmp(timeline.records[3].direction, "response") == 0) &&
    (strcmp(timeline.records[4].role, "api_item") == 0) &&
    timeline.records[4].can_include_in_context &&
    (strcmp(timeline.records[4].kind,
            STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) == 0) &&
    (strcmp(timeline.records[4].response_item_action_json,
            "{\"type\":\"search\",\"query\":\"Strappy Cocoa\","
            "\"sources\":[{\"type\":\"url\","
            "\"url\":\"https://example.com/search\"}]}") == 0) &&
    (strcmp(timeline.records[5].role, "api_item") == 0) &&
    timeline.records[5].can_include_in_context &&
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
    timeline.records[6].can_include_in_context &&
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
  model_request_id = timeline.records[0].model_request_id;
  strappy_session_message_record_list_destroy(&timeline);

  ok = strappy_session_update_model_request_include_in_context(
         path,
         session_id,
         model_request_id,
         0,
         &error) &&
    harness_context_selection_matches(path,
                                      session_id,
                                      model_request_id,
                                      0U,
                                      6U,
                                      0U,
                                      &error) &&
    strappy_session_update_model_request_include_in_context(path,
                                                            session_id,
                                                            model_request_id,
                                                            1,
                                                            &error) &&
    harness_context_selection_matches(path,
                                      session_id,
                                      model_request_id,
                                      6U,
                                      6U,
                                      6U,
                                      &error);
  if (!ok) {
    fprintf(stderr,
            "Context inclusion persistence failed: %s\n",
            (error != NULL) ? error : "unexpected context state");
    free(error);
    unlink(path);
    return 0;
  }

  ok = !strappy_session_update_model_request_include_in_context(
    path,
    session_id + 1LL,
    model_request_id,
    0,
    &error);
  if (!ok || (error == NULL)) {
    fprintf(stderr,
            "Cross-session context round update unexpectedly succeeded.\n");
    free(error);
    unlink(path);
    return 0;
  }
  free(error);
  error = NULL;

  ok = harness_verify_invalid_structured_text_webview_recovery(
    path, session_id, call_id, &error);
  if (!ok) {
    fprintf(stderr,
            "Invalid UTF-8 WebView recovery failed: %s\n",
            (error != NULL) ? error : "unexpected output");
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

static int harness_test_working_directory_selection(void)
{
  char root[] = "/tmp/strappy-working-directory-XXXXXX";
  char database_path[1024];
  char developer_path[1024];
  char library_path[1024];
  char application_support_path[1024];
  char strappy_path[1024];
  char nested_developer_path[1024];
  char collision_path[1024];
  char *working_directory;
  char *error;
  struct stat directory_stat;
  FILE *collision;
  long long session_id;
  int written;
  int ok;

  if (mkdtemp(root) == NULL) {
    return harness_fail("Could not create working-directory test root.");
  }
  written = snprintf(database_path,
                     sizeof(database_path),
                     "%s/strappy.sqlite",
                     root);
  ok = (written > 0) && ((size_t)written < sizeof(database_path));
  written = snprintf(developer_path,
                     sizeof(developer_path),
                     "%s/Developer",
                     root);
  ok = ok && (written > 0) &&
    ((size_t)written < sizeof(developer_path));
  written = snprintf(library_path,
                     sizeof(library_path),
                     "%s/Library",
                     root);
  ok = ok && (written > 0) &&
    ((size_t)written < sizeof(library_path));
  written = snprintf(application_support_path,
                     sizeof(application_support_path),
                     "%s/Application Support",
                     library_path);
  ok = ok && (written > 0) &&
    ((size_t)written < sizeof(application_support_path));
  written = snprintf(strappy_path,
                     sizeof(strappy_path),
                     "%s/Strappy",
                     application_support_path);
  ok = ok && (written > 0) &&
    ((size_t)written < sizeof(strappy_path));
  written = snprintf(nested_developer_path,
                     sizeof(nested_developer_path),
                     "%s/Developer",
                     strappy_path);
  ok = ok && (written > 0) &&
    ((size_t)written < sizeof(nested_developer_path));
  written = snprintf(collision_path,
                     sizeof(collision_path),
                     "%s/not-a-directory",
                     root);
  ok = ok && (written > 0) &&
    ((size_t)written < sizeof(collision_path));
  if (!ok) {
    rmdir(root);
    return harness_fail("Could not build working-directory test paths.");
  }

  working_directory = NULL;
  error = NULL;
  session_id = 0LL;
  ok = strappy_db_restore_provider_account(
         database_path,
         STRAPPY_PROVIDER_ACCOUNT_OPENROUTER,
         STRAPPY_PROVIDER_OPENROUTER,
         STRAPPY_PROVIDER_ACCOUNT_OPENROUTER_NAME,
         NULL,
         &error) &&
    strappy_session_create_with_working_directory(database_path,
                                                     developer_path,
                                                     &session_id,
                                                     &error) &&
    (session_id > 0LL) &&
    (stat(developer_path, &directory_stat) == 0) &&
    S_ISDIR(directory_stat.st_mode) &&
    strappy_session_get_working_directory(database_path,
                                          session_id,
                                          &working_directory,
                                          &error) &&
    (working_directory != NULL) &&
    (strcmp(working_directory, developer_path) == 0);
  free(working_directory);
  working_directory = NULL;
  if (!ok) {
    fprintf(stderr,
            "Default working-directory creation failed: %s\n",
            (error != NULL) ? error : "unexpected path");
    goto cleanup;
  }

  ok = strappy_session_update_working_directory(database_path,
                                                session_id,
                                                nested_developer_path,
                                                &error) &&
    (stat(nested_developer_path, &directory_stat) == 0) &&
    S_ISDIR(directory_stat.st_mode) &&
    strappy_session_get_working_directory(database_path,
                                          session_id,
                                          &working_directory,
                                          &error) &&
    (working_directory != NULL) &&
    (strcmp(working_directory, nested_developer_path) == 0);
  free(working_directory);
  working_directory = NULL;
  if (!ok) {
    fprintf(stderr,
            "Nested working-directory selection failed: %s\n",
            (error != NULL) ? error : "unexpected path");
    goto cleanup;
  }

  collision = fopen(collision_path, "wb");
  if (collision == NULL) {
    ok = 0;
    fprintf(stderr, "Could not create working-directory collision file.\n");
    goto cleanup;
  }
  fclose(collision);
  strappy_session_free_string(error);
  error = NULL;
  ok = !strappy_session_update_working_directory(database_path,
                                                 session_id,
                                                 collision_path,
                                                 &error) &&
    (error != NULL);
  strappy_session_free_string(error);
  error = NULL;
  ok = ok &&
    strappy_session_get_working_directory(database_path,
                                          session_id,
                                          &working_directory,
                                          &error) &&
    (working_directory != NULL) &&
    (strcmp(working_directory, nested_developer_path) == 0);
  free(working_directory);
  working_directory = NULL;
  if (!ok) {
    fprintf(stderr,
            "Invalid working-directory selection changed stored state: %s\n",
            (error != NULL) ? error : "unexpected path");
  }

cleanup:
  free(working_directory);
  strappy_session_free_string(error);
  unlink(collision_path);
  unlink(database_path);
  rmdir(nested_developer_path);
  rmdir(strappy_path);
  rmdir(application_support_path);
  rmdir(library_path);
  rmdir(developer_path);
  rmdir(root);
  return ok;
}

static int harness_test_session_webview_rendering(void)
{
  static const char *first_text = "First stored WebView message";
  static const char *second_text =
    "Second stored WebView message from db_1; keep db_2 raw";
  static const char *second_display_text =
    "Second stored WebView message from "
    "strappy_session_webview_Contacts &amp; Notes.sqlite; keep db_2 raw";
  static const char *request_json =
    "{\"model\":\"" STRAPPY_CONFIG_DEFAULT_API_MODEL
    "\",\"stream\":false,\"store\":false,"
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
    "\"text\":\"Second stored WebView message from db_1; keep db_2 raw\","
    "\"annotations\":[],\"logprobs\":[]}]}],"
    "\"output_text\":\"Second stored WebView message from db_1; keep db_2 raw\","
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":5,"
    "\"total_tokens\":9}}";
  const char *path = "/tmp/strappy_session_webview_harness.sqlite";
  const char *database_path =
    "/tmp/strappy_session_webview_Contacts & Notes.sqlite";
  const char *pending_database_path =
    "/tmp/strappy_session_webview_Private.sqlite";
  strappy_discovered_database_input pending_database;
  strappy_response_call_begin_input begin;
  strappy_response_call_finish_input finish;
  strappy_response_timeline_cursor final_cursor;
  strappy_response_timeline_cursor request_cursor;
  strappy_session_webview_render_context *render_context;
  strappy_session_message_record_list range;
  const char *first_remove;
  const char *first_position;
  const char *reconcile_append;
  const char *second_remove;
  const char *second_position;
  char *append_script;
  char *append_timeline_cursor;
  char *current_timeline_cursor;
  char *empty_script;
  char *empty_timeline_cursor;
  char *error;
  char *initial_cursor;
  char *initial_page_html;
  char *invalid_script;
  char *invalid_timeline_cursor;
  char *page_html;
  char *page_timeline_cursor;
  char *reconcile_script;
  char *reconcile_timeline_cursor;
  char *request_script;
  char *request_timeline_cursor;
  char *update_script;
  long long call_id;
  long long session_id;
  size_t message_count;
  size_t reconcile_message_count;
  size_t request_message_count;
  int ok;

  unlink(path);
  unlink(database_path);
  unlink(pending_database_path);
  append_script = NULL;
  append_timeline_cursor = NULL;
  current_timeline_cursor = NULL;
  call_id = 0LL;
  empty_script = NULL;
  empty_timeline_cursor = NULL;
  error = NULL;
  initial_cursor = NULL;
  initial_page_html = NULL;
  invalid_script = NULL;
  invalid_timeline_cursor = NULL;
  page_html = NULL;
  page_timeline_cursor = NULL;
  reconcile_script = NULL;
  reconcile_timeline_cursor = NULL;
  render_context = NULL;
  request_script = NULL;
  request_timeline_cursor = NULL;
  update_script = NULL;
  session_id = 0LL;
  message_count = 0U;
  reconcile_message_count = 0U;
  request_message_count = 0U;
  strappy_response_timeline_cursor_init(&final_cursor);
  strappy_response_timeline_cursor_init(&request_cursor);
  strappy_session_message_record_list_init(&range);
  ok = strappy_webview_configure_localized_labels(&error) &&
       strappy_db_restore_provider_account(
         path,
         STRAPPY_PROVIDER_ACCOUNT_OPENROUTER,
         STRAPPY_PROVIDER_OPENROUTER,
         STRAPPY_PROVIDER_ACCOUNT_OPENROUTER_NAME,
         NULL,
         &error) &&
       strappy_db_create_session(path, &session_id, &error) &&
       harness_create_approved_preflight_database(path,
                                                  database_path,
                                                  &error);
  if (!ok) {
    fprintf(stderr,
            "Could not create the session WebView fixture: %s\n",
            (error != NULL) ? error : "unknown error");
    goto cleanup;
  }
  memset(&pending_database, 0, sizeof(pending_database));
  pending_database.path = pending_database_path;
  pending_database.size = 4096LL;
  pending_database.modified_at = 1LL;
  pending_database.device = 8ULL;
  pending_database.inode = 12ULL;
  pending_database.is_valid_sqlite = 1;
  pending_database.scan_root = "/tmp";
  if (!strappy_db_save_discovered_databases(path,
                                             &pending_database,
                                             1U,
                                             &error)) {
    fprintf(stderr,
            "Could not create the pending database display fixture: %s\n",
            (error != NULL) ? error : "unknown error");
    goto cleanup;
  }

  initial_page_html =
    strappy_session_webview_messages_page_html_for_session(
      path,
      session_id,
      "../shared/Resources",
      NULL,
      NULL,
      STRAPPY_WEBVIEW_PALETTE_APPLICATION_TINTED,
      &message_count,
      &initial_cursor,
      &error);
  if ((initial_page_html == NULL) || (message_count != 0U) ||
      (initial_cursor == NULL)) {
    fprintf(stderr,
            "Could not capture the empty WebView timeline cursor: %s\n",
            (error != NULL) ? error : "unexpected output");
    goto cleanup;
  }
  current_timeline_cursor = strappy_session_timeline_cursor_for_session(
    path,
    session_id,
    &error);
  if ((current_timeline_cursor == NULL) ||
      (strcmp(current_timeline_cursor, initial_cursor) != 0)) {
    fprintf(stderr,
            "Direct empty timeline cursor did not match the rendered page.\n");
    ok = 0;
    goto cleanup;
  }
  strappy_session_free_string(current_timeline_cursor);
  current_timeline_cursor = NULL;

  render_context = strappy_session_webview_render_context_create(
    path,
    "../shared/Resources",
    &error);
  if (render_context == NULL) {
    fprintf(stderr,
            "Could not cache the session WebView render context: %s\n",
            (error != NULL) ? error : "unknown error");
    ok = 0;
    goto cleanup;
  }

  memset(&begin, 0, sizeof(begin));
  begin.session_id = session_id;
  begin.provider_account_id = STRAPPY_PROVIDER_ACCOUNT_OPENROUTER;
  begin.model_id = STRAPPY_CONFIG_DEFAULT_MODEL_IDENTIFIER;
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

  ok = strappy_db_list_response_timeline_after(path,
                                               session_id,
                                               NULL,
                                               &range,
                                               &request_cursor,
                                               &error) &&
    (range.count == 1U) &&
    (range.records[0].direction != NULL) &&
    (strcmp(range.records[0].direction, "request") == 0) &&
    (range.records[0].http_attempt_id == 0LL) &&
    (range.records[0].content != NULL) &&
    (strcmp(range.records[0].content, first_text) == 0);
  strappy_session_message_record_list_destroy(&range);
  if (!ok) {
    fprintf(stderr,
            "Running request was not independently visible: %s\n",
            (error != NULL) ? error : "unexpected output");
    goto cleanup;
  }

  request_script =
    strappy_session_webview_append_messages_js_with_render_context(
      render_context,
      session_id,
      initial_cursor,
      &request_message_count,
      &request_timeline_cursor,
      &error);
  ok = (request_script != NULL) && (request_message_count == 1U) &&
       (request_timeline_cursor != NULL) &&
       (strstr(request_script, "appendMessage(") != NULL) &&
       (strstr(request_script, first_text) != NULL) &&
       (strstr(request_script, second_text) == NULL) &&
       (strstr(request_script, "response-call-") == NULL);
  if (!ok) {
    fprintf(stderr,
            "Could not render the running request independently: %s\n",
            (error != NULL) ? error : "unexpected output");
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

  ok = strappy_db_list_response_timeline_after(path,
                                               session_id,
                                               &request_cursor,
                                               &range,
                                               &final_cursor,
                                               &error);
  ok = ok && (range.count == 2U) &&
       (range.records[0].role != NULL) &&
       (strcmp(range.records[0].role, "api_call") == 0) &&
       (range.records[0].direction == NULL) &&
       (range.records[0].http_attempt_id == call_id) &&
       (range.records[1].direction != NULL) &&
       (strcmp(range.records[1].direction, "response") == 0) &&
       (range.records[1].content != NULL) &&
       (strcmp(range.records[1].content, second_text) == 0);
  if (ok) {
    update_script =
      strappy_session_webview_message_update_js_with_render_context(
        render_context,
        session_id,
        range.records[1].message_key,
        &error);
    ok = (update_script != NULL) &&
         (strstr(update_script, second_display_text) != NULL) &&
         (strstr(update_script, second_text) == NULL);
  }
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
    "{\"active\":true,\"message_key\":\"session-processing\","
    "\"status_kind\":\"thinking\",\"started_ms\":1000}",
    STRAPPY_WEBVIEW_PALETTE_APPLICATION_TINTED,
    &message_count,
    &page_timeline_cursor,
    &error);
  first_position = (page_html != NULL) ?
    strstr(page_html, first_text) : NULL;
  second_position = (page_html != NULL) ?
    strstr(page_html, second_display_text) : NULL;
  ok = (page_html != NULL) && (message_count == 3U) &&
       (page_timeline_cursor != NULL) &&
       (first_position != NULL) && (second_position != NULL) &&
       (first_position < second_position) &&
       (strstr(page_html, "data-context-item-id=\"") == NULL) &&
       (strstr(page_html, "data-include-in-context=\"1\"") != NULL) &&
       (strstr(page_html,
               "data-include-in-context-label="
               "\"Included in Future Context\"") != NULL) &&
       (strstr(page_html, second_text) == NULL) &&
       (strstr(page_html,
               "setProcessingStatus({\"active\":true,"
               "\"message_key\":\"session-processing\","
               "\"status_kind\":\"thinking\",\"started_ms\":1000});") != NULL);
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

  current_timeline_cursor = strappy_session_timeline_cursor_for_session(
    path,
    session_id,
    &error);
  if ((current_timeline_cursor == NULL) ||
      (strcmp(current_timeline_cursor, page_timeline_cursor) != 0)) {
    fprintf(stderr,
            "Could not capture the current presentation timeline cursor: %s\n",
            (error != NULL) ? error : "unexpected output");
    ok = 0;
    goto cleanup;
  }

  append_script = strappy_session_webview_append_messages_js_for_session(
    path,
    session_id,
    "../shared/Resources",
    request_timeline_cursor,
    &message_count,
    &append_timeline_cursor,
    &error);
  ok = (append_script != NULL) && (message_count == 2U) &&
       (append_timeline_cursor != NULL) &&
       (strcmp(append_timeline_cursor, page_timeline_cursor) == 0) &&
       (strstr(append_script, "appendMessage(") != NULL) &&
       (strstr(append_script, first_text) == NULL) &&
       (strstr(append_script, second_text) == NULL) &&
       (strstr(append_script, second_display_text) != NULL) &&
       (strstr(append_script, "response-call-") != NULL);
  if (!ok) {
    fprintf(stderr,
            "Could not render stored WebView append JavaScript: %s\n",
            (error != NULL) ? error : "unexpected output");
    goto cleanup;
  }

  reconcile_script =
    strappy_session_webview_reconcile_messages_js_with_render_context(
      render_context,
      session_id,
      request_timeline_cursor,
      &reconcile_message_count,
      &reconcile_timeline_cursor,
      &error);
  first_remove = (reconcile_script != NULL) ?
    strstr(reconcile_script, "removeMessage(") : NULL;
  second_remove = (first_remove != NULL) ?
    strstr(first_remove + 1, "removeMessage(") : NULL;
  reconcile_append = (reconcile_script != NULL) ?
    strstr(reconcile_script, "appendMessage(") : NULL;
  ok = (reconcile_script != NULL) && (reconcile_message_count == 2U) &&
       (reconcile_timeline_cursor != NULL) &&
       (strcmp(reconcile_timeline_cursor, page_timeline_cursor) == 0) &&
       (first_remove != NULL) && (second_remove != NULL) &&
       (strstr(second_remove + 1, "removeMessage(") == NULL) &&
       (reconcile_append != NULL) && (second_remove < reconcile_append) &&
       (strstr(reconcile_script, second_display_text) != NULL);
  if (!ok) {
    fprintf(stderr,
            "Could not reconcile the final WebView range: %s\n",
            (error != NULL) ? error : "unexpected output");
    goto cleanup;
  }

  empty_script = strappy_session_webview_append_messages_js_for_session(
    path,
    session_id,
    "../shared/Resources",
    page_timeline_cursor,
    &message_count,
    &empty_timeline_cursor,
    &error);
  ok = (empty_script != NULL) && (message_count == 0U) &&
       (empty_timeline_cursor != NULL) &&
       (strcmp(empty_timeline_cursor, page_timeline_cursor) == 0) &&
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
    "../shared/Resources",
    "not-a-timeline-cursor",
    &message_count,
    &invalid_timeline_cursor,
    &error);
  ok = (invalid_script == NULL) && (message_count == 0U) &&
       (invalid_timeline_cursor == NULL) && (error != NULL);
  if (!ok) {
    fprintf(stderr,
            "Invalid stored WebView append range unexpectedly succeeded.\n");
  }

cleanup:
  strappy_session_message_record_list_destroy(&range);
  strappy_session_free_string(page_html);
  strappy_session_free_string(page_timeline_cursor);
  strappy_session_free_string(append_script);
  strappy_session_free_string(append_timeline_cursor);
  strappy_session_free_string(current_timeline_cursor);
  strappy_session_free_string(empty_script);
  strappy_session_free_string(empty_timeline_cursor);
  strappy_session_free_string(initial_cursor);
  strappy_session_free_string(initial_page_html);
  strappy_session_free_string(invalid_script);
  strappy_session_free_string(invalid_timeline_cursor);
  strappy_session_free_string(request_script);
  strappy_session_free_string(request_timeline_cursor);
  strappy_session_free_string(reconcile_script);
  strappy_session_free_string(reconcile_timeline_cursor);
  strappy_session_free_string(update_script);
  strappy_session_webview_render_context_destroy(render_context);
  strappy_session_free_string(error);
  unlink(pending_database_path);
  unlink(database_path);
  unlink(path);
  return ok;
}

typedef struct harness_other_credentials {
  const char *authenticated_account_id;
  const char *unauthenticated_account_id;
  pthread_mutex_t mutex;
  int authenticated_snapshots;
  int unauthenticated_snapshots;
  int invalid_snapshots;
} harness_other_credentials;

typedef struct harness_other_request {
  const char *db_path;
  long long session_id;
  const char *prompt;
  char *result;
  char *error;
} harness_other_request;

static int harness_other_credentials_callback(
  const char *provider_id,
  const char *provider_account_id,
  int force_refresh,
  char **bearer_token_out,
  char **upstream_account_id_out,
  void *user_data,
  char **error_out)
{
  static const char token[] = "phase6-auth-token-not-live";
  harness_other_credentials *credentials;
  int valid;

  (void)error_out;
  credentials = (harness_other_credentials *)user_data;
  if ((bearer_token_out == NULL) || (upstream_account_id_out == NULL) ||
      (credentials == NULL)) {
    return 0;
  }
  *bearer_token_out = NULL;
  *upstream_account_id_out = NULL;
  valid = (provider_id != NULL) &&
    (strcmp(provider_id, STRAPPY_PROVIDER_OTHER) == 0) && !force_refresh;
  pthread_mutex_lock(&credentials->mutex);
  if (valid && (provider_account_id != NULL) &&
      (strcmp(provider_account_id,
              credentials->authenticated_account_id) == 0)) {
    credentials->authenticated_snapshots++;
    *bearer_token_out = strdup(token);
    valid = *bearer_token_out != NULL;
  } else if (valid && (provider_account_id != NULL) &&
             (strcmp(provider_account_id,
                     credentials->unauthenticated_account_id) == 0)) {
    credentials->unauthenticated_snapshots++;
  } else {
    credentials->invalid_snapshots++;
    valid = 0;
  }
  pthread_mutex_unlock(&credentials->mutex);
  return valid;
}

static void *harness_run_other_request_thread(void *context)
{
  harness_other_request *request;

  request = (harness_other_request *)context;
  request->result =
    strappy_responses_send_prompt_for_session_and_store(
      request->prompt,
      "/dev/null",
      NULL,
      NULL,
      "../shared/Resources",
      request->db_path,
      request->session_id,
      &request->error);
  return NULL;
}

static int harness_file_contains_text(const char *path, const char *needle)
{
  FILE *file;
  char *bytes;
  long length;
  size_t index;
  size_t needle_length;
  int close_ok;
  int found;

  file = fopen(path, "rb");
  if (file == NULL) {
    return 0;
  }
  if ((fseek(file, 0L, SEEK_END) != 0) ||
      ((length = ftell(file)) < 0L) ||
      (fseek(file, 0L, SEEK_SET) != 0)) {
    fclose(file);
    return 0;
  }
  bytes = (char *)malloc((size_t)length + 1U);
  if (bytes == NULL) {
    fclose(file);
    return 0;
  }
  found = fread(bytes, 1U, (size_t)length, file) == (size_t)length;
  close_ok = fclose(file) == 0;
  needle_length = strlen(needle);
  found = found && close_ok && (needle_length <= (size_t)length);
  for (index = 0U;
       found && (index + needle_length <= (size_t)length);
       index++) {
    if (memcmp(bytes + index, needle, needle_length) == 0) {
      free(bytes);
      return 1;
    }
  }
  free(bytes);
  return 0;
}

static int harness_test_other_provider_accounts(void)
{
  static const char token[] = "phase6-auth-token-not-live";
  char path[] = "/tmp/strappy-responses-other-XXXXXX";
  char endpoint_one[128];
  char endpoint_two[128];
  char wal_path[sizeof(path) + 8U];
  char *account_one;
  char *account_two;
  char *model_one;
  char *model_two;
  char *error;
  strappy_manual_model_input model_input;
  strappy_model_route_record route;
  harness_other_credentials credentials;
  harness_other_request request_one;
  harness_other_request request_two;
  pthread_t thread_one;
  pthread_t thread_two;
  pid_t server_one;
  pid_t server_two;
  long long session_one;
  long long session_two;
  int fd;
  int first_started;
  int second_started;
  int first_joined;
  int second_joined;
  int server_one_ok;
  int server_two_ok;
  int ok;

  fd = mkstemp(path);
  if (fd < 0) {
    return harness_fail("Could not create generic-provider test database.");
  }
  close(fd);
  account_one = NULL;
  account_two = NULL;
  model_one = NULL;
  model_two = NULL;
  error = NULL;
  session_one = 0LL;
  session_two = 0LL;
  server_one = (pid_t)-1;
  server_two = (pid_t)-1;
  first_started = 0;
  second_started = 0;
  first_joined = 0;
  second_joined = 0;
  server_one_ok = 0;
  server_two_ok = 0;
  wal_path[0] = '\0';
  strappy_model_route_record_init(&route);
  memset(&credentials, 0, sizeof(credentials));
  memset(&request_one, 0, sizeof(request_one));
  memset(&request_two, 0, sizeof(request_two));
  pthread_mutex_init(&credentials.mutex, NULL);

  harness_expected_authorization = "Bearer phase6-auth-token-not-live";
  ok = harness_start_server(HARNESS_RESPONSES_SERVER_OTHER_GENERIC,
                            endpoint_one,
                            sizeof(endpoint_one),
                            &server_one);
  harness_expected_authorization = "";
  if (ok) {
    ok = harness_start_server(HARNESS_RESPONSES_SERVER_OTHER_GENERIC,
                              endpoint_two,
                              sizeof(endpoint_two),
                              &server_two);
  }
  harness_expected_authorization = NULL;
  memset(&model_input, 0, sizeof(model_input));
  model_input.wire_model_id = "manual";
  model_input.display_name = "Manual";
  model_input.context_window_tokens = 8192LL;
  model_input.max_output_tokens = 1024LL;
  model_input.local_functions_enabled = 1;
  ok = ok && strappy_db_initialize(path, &error) &&
    strappy_db_create_provider_account(
      path,"other","Authenticated",endpoint_one,&account_one,&error) &&
    strappy_db_create_provider_account(
      path,"other","Unauthenticated",endpoint_two,&account_two,&error) &&
    strappy_db_create_manual_model(
      path,"other",&model_input,&model_one,&error) &&
    ((model_two = strdup(model_one)) != NULL) &&
    strappy_db_set_model_allowed(path,model_one,1,&error) &&
    strappy_db_set_model_allowed(path,model_two,1,&error) &&
    strappy_db_set_default_account_model(path,account_one,model_one,&error) &&
    strappy_db_create_session(path,&session_one,&error) &&
    strappy_db_set_default_account_model(path,account_two,model_two,&error) &&
    strappy_db_create_session(path,&session_two,&error);
  if (!ok) {
    fprintf(stderr,
            "Could not prepare generic-provider fixtures: %s\n",
            (error != NULL) ? error : "unknown");
    goto cleanup;
  }

  credentials.authenticated_account_id = account_one;
  credentials.unauthenticated_account_id = account_two;
  strappy_responses_set_provider_credentials_callback(
    harness_other_credentials_callback,
    &credentials);
  request_one.db_path = path;
  request_one.session_id = session_one;
  request_one.prompt = "Authenticated generic request";
  request_two.db_path = path;
  request_two.session_id = session_two;
  request_two.prompt = "Unauthenticated generic request";
  first_started = pthread_create(
    &thread_one,NULL,harness_run_other_request_thread,&request_one) == 0;
  second_started = first_started && (pthread_create(
    &thread_two,NULL,harness_run_other_request_thread,&request_two) == 0);
  if (first_started) {
    first_joined = pthread_join(thread_one, NULL) == 0;
  }
  if (second_started) {
    second_joined = pthread_join(thread_two, NULL) == 0;
  }
  strappy_responses_set_provider_credentials_callback(NULL, NULL);
  server_one_ok = harness_wait_for_server(
    server_one,
    request_one.result == NULL);
  server_one = (pid_t)-1;
  server_two_ok = harness_wait_for_server(
    server_two,
    request_two.result == NULL);
  server_two = (pid_t)-1;
  ok = first_started && second_started && first_joined && second_joined &&
    (request_one.result != NULL) &&
    (strcmp(request_one.result,"Generic response.") == 0) &&
    (request_two.result != NULL) &&
    (strcmp(request_two.result,"Generic response.") == 0) &&
    (request_one.error == NULL) && (request_two.error == NULL) &&
    (credentials.authenticated_snapshots == 1) &&
    (credentials.unauthenticated_snapshots == 1) &&
    (credentials.invalid_snapshots == 0) &&
    server_one_ok && server_two_ok;
  if (!ok) {
    fprintf(stderr,"Generic-provider simultaneous requests failed.\n");
    goto cleanup;
  }

  snprintf(wal_path, sizeof(wal_path), "%s-wal", path);
  /* Reopening proves account/model/session state survives process-style
   * teardown; archiving must preserve history while blocking new routing. */
  ok = !harness_file_contains_text(path, token) &&
    !harness_file_contains_text(wal_path, token) &&
    strappy_db_initialize(path,&error) &&
    strappy_db_archive_provider_account(path,account_one,&error);
  if (ok) {
    ok = !strappy_db_get_session_model_route(
           path,session_one,&route,&error) && (error != NULL) &&
      (strstr(error,"active") != NULL) &&
      (strstr(error,"Authenticated") == NULL);
  }
  strappy_free_string(error);
  error = NULL;
  if (ok) {
    sqlite3 *db;
    long long value;

    db = NULL;
    ok = (sqlite3_open(path,&db) == SQLITE_OK) &&
      harness_query_int(db,
        "SELECT COUNT(*) FROM model_requests r JOIN turns t ON t.id=r.turn_id "
        "JOIN sessions s ON s.id=t.session_id WHERE r.provider_account_id IN "
        "(SELECT id FROM provider_accounts WHERE provider_id='other');",
        &value) && (value >= 2LL) &&
      harness_query_int(db,
        "SELECT COUNT(*) FROM api_usage WHERE cost_nano_usd IS NOT NULL;",
        &value) && (value == 0LL);
    if (db != NULL) {
      sqlite3_close(db);
    }
  }

cleanup:
  strappy_responses_set_provider_credentials_callback(NULL, NULL);
  if (server_one > 0) {
    (void)harness_wait_for_server(server_one, 1);
  }
  if (server_two > 0) {
    (void)harness_wait_for_server(server_two, 1);
  }
  pthread_mutex_destroy(&credentials.mutex);
  strappy_model_route_record_destroy(&route);
  free(request_one.result);
  free(request_one.error);
  free(request_two.result);
  free(request_two.error);
  free(account_one);
  free(account_two);
  free(model_one);
  free(model_two);
  free(error);
  if (wal_path[0] != '\0') {
    unlink(wal_path);
  }
  unlink(path);
  return ok;
}

int main(void)
{
  if (harness_test_unicode_emoji_scan() &&
      harness_test_working_directory_selection() &&
      harness_test_request_surfaces() &&
      harness_test_ledger() &&
      harness_test_cumulative_session_metrics() &&
      harness_test_answer_quality_disabled() &&
      harness_test_answer_quality_report() &&
      harness_test_world_knowledge_assistant_set() &&
      harness_test_preflight_runs_only_on_first_prompt() &&
      harness_test_isolated_prompt_context() &&
      harness_test_empty_answer_quality_report() &&
      harness_test_empty_answer_after_tools_quality_report() &&
      harness_test_web_search_requires_markdown_reference() &&
      harness_test_valid_web_reference_passes_content_check() &&
      harness_test_native_web_search_persists_citations() &&
      harness_test_function_tool_continuation() &&
      harness_test_round_limit() &&
      harness_test_file_mutation_continuation() &&
      harness_test_bash_disabled_request() &&
      harness_test_bash_output_truncation_flag() &&
      harness_test_bash_tool_cancellation() &&
      harness_test_retry_attempt_ledger() &&
      harness_test_active_request_cancellation() &&
      harness_test_retry_after_clamp_and_cancellation() &&
      harness_test_other_provider_accounts() &&
      harness_test_session_webview_rendering()) {
    printf("responses_harness passed.\n");
    return 0;
  }
  fprintf(stderr, "responses_harness failed.\n");
  return 1;
}
