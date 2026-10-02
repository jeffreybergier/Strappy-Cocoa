#include "strappy_tools.h"
#include "strappy_tool_metadata_internal.h"

#include "strappy_bash.h"
#include "strappy_assistant_sets.h"
#include "strappy_cocoa.h"
#include "strappy_core.h"
#include "strappy_db.h"
#include "strappy_file_edit.h"
#include "strappy_file_read.h"
#include "strappy_file_write.h"
#include "strappy_skills.h"
#include "strappy_study_execution.h"

#include <cJSON.h>
#include <errno.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define STRAPPY_TOOL_GUIDANCE_RESOURCE "GuidanceTools.json"
#define STRAPPY_TOOL_SERVER_TOOLS_KEY "server_tools"
#define STRAPPY_TOOL_SERVER_FEATURE_KEY "feature"
#define STRAPPY_TOOL_SERVER_FEATURE_WEB_SEARCH "web_search"
#define STRAPPY_TOOL_DESCRIPTION_KEY "description"
#define STRAPPY_TOOL_DISPLAY_METADATA_KEY "x-strappy-display"
#define STRAPPY_TOOL_DISPLAY_LABEL_KEY "label"
#define STRAPPY_TOOL_DISPLAY_PROMOTED_ARGUMENT_KEY "promoted_argument"
#define STRAPPY_TOOL_DISPLAY_PROMOTED_PATH_KEY "promoted_path"
#define STRAPPY_TOOL_DISPLAY_TRANSFORM_KEY "transform"
#define STRAPPY_TOOL_RESULT_GUIDANCE_KEY "result_guidance"
#define STRAPPY_TOOL_RESULT_GUIDANCE_ON_EMPTY_KEY "onEmpty"
#define STRAPPY_TOOL_RESULT_GUIDANCE_ON_NOT_EMPTY_KEY "onNotEmpty"
#define STRAPPY_DATABASE_GUIDANCE_RESOURCE "GuidanceDatabase.json"
#define STRAPPY_FONTAWESOME_ICONS_RESOURCE "FontAwesomeIcons.json"
#define STRAPPY_DATABASE_QUERY_MAX_ROWS 100
#define STRAPPY_DATABASE_QUERY_MAX_COLUMNS 64
#define STRAPPY_DATABASE_QUERY_MAX_DATABASE_ID_BYTES 64U
#define STRAPPY_DATABASE_QUERY_MAX_SQL_BYTES 8192U
#define STRAPPY_DATABASE_QUERY_MAX_CELL_BYTES 4096U
#define STRAPPY_DATABASE_QUERY_MAX_PAYLOAD_BYTES (48U * 1024U)
#define STRAPPY_DATABASE_QUERY_MAX_RESULT_BYTES (64U * 1024U)
#define STRAPPY_DATABASE_QUERY_TIMEOUT_SECONDS 5.0
#define STRAPPY_DATABASE_QUERY_PROGRESS_INSTRUCTIONS 1000
#define STRAPPY_DATABASE_QUERY_MAX_PROGRESS_CALLS 20000
#define STRAPPY_DATABASE_QUERY_JSON_SAFE_INTEGER_MAX 9007199254740991LL
#define STRAPPY_DATABASE_QUERY_JSON_SAFE_INTEGER_MIN (-9007199254740991LL)
#define STRAPPY_HELPER_DATETIME_MAX_INPUT_BYTES 8192U
#define STRAPPY_HELPER_DATETIME_MAX_ITEMS 256U
#define STRAPPY_HELPER_INFO_MAX_SHORT_BYTES 128U
#define STRAPPY_HELPER_INFO_MAX_VALUE_BYTES 2048U
#define STRAPPY_HELPER_INFO_MAX_CONTENT_BYTES 4096U
#define STRAPPY_HELPER_INFO_DEFAULT_LIMIT 20
#define STRAPPY_DATABASE_CONTEXT_TABLE_LIMIT 20
#define STRAPPY_DATABASE_CONTEXT_VIEW_LIMIT 20
#define STRAPPY_HELPER_SESSION_NAME_MAX_BYTES 96U
#define STRAPPY_HELPER_FONTAWESOME_MAX_QUERY_BYTES 128U
#define STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODE_BYTES 96U
#define STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODES 32U
#define STRAPPY_HELPER_FONTAWESOME_RESULT_LIMIT 20

typedef enum strappy_tool_kind {
  STRAPPY_TOOL_KIND_DATABASE = 1,
  STRAPPY_TOOL_KIND_HELPER = 2,
  STRAPPY_TOOL_KIND_DEVELOPER = 3
} strappy_tool_kind;

typedef struct strappy_tool_definition {
  const char *name;
  strappy_tool_kind kind;
} strappy_tool_definition;

typedef struct strappy_server_tool_definition {
  const char *type;
  const char *feature;
  const char *response_item_type;
} strappy_server_tool_definition;

typedef struct strappy_tools_text_buffer {
  char *data;
  size_t length;
} strappy_tools_text_buffer;

typedef struct strappy_database_query_arguments {
  char *database_id;
  char *sql;
} strappy_database_query_arguments;

typedef struct strappy_helper_datetime_arguments {
  char **items;
  size_t item_count;
  strappy_cocoa_timestamp_unit unit;
} strappy_helper_datetime_arguments;

typedef struct strappy_memory_save_arguments {
  char *fact;
} strappy_memory_save_arguments;

typedef struct strappy_session_rename_arguments {
  char *name;
} strappy_session_rename_arguments;

typedef struct strappy_memory_delete_arguments {
  long long id;
} strappy_memory_delete_arguments;

typedef struct strappy_skill_read_arguments {
  char *skill_identifier;
} strappy_skill_read_arguments;

typedef struct strappy_database_study_arguments {
  char *database_id;
  char *description;
  char *context;
} strappy_database_study_arguments;

typedef struct strappy_database_context_arguments {
  char *database_id;
} strappy_database_context_arguments;

typedef struct strappy_fontawesome_search_arguments {
  char *query;
} strappy_fontawesome_search_arguments;

typedef struct strappy_fontawesome_confirm_arguments {
  char **shortcodes;
  size_t shortcode_count;
} strappy_fontawesome_confirm_arguments;

typedef struct strappy_database_query_authorizer_context {
  int denied_action;
  int read_real_table;
} strappy_database_query_authorizer_context;

typedef struct strappy_database_query_progress_context {
  clock_t started_at;
  int progress_calls;
  int timed_out;
} strappy_database_query_progress_context;

typedef char *(*strappy_tools_datetime_token_converter)(
  const char *token,
  size_t length,
  strappy_cocoa_timestamp_unit unit,
  char **error_out);

static int strappy_tools_helper_is_space(char value);






static void strappy_database_query_arguments_init(
  strappy_database_query_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->database_id = NULL;
  arguments->sql = NULL;
}

static void strappy_database_query_arguments_destroy(
  strappy_database_query_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->database_id);
  free(arguments->sql);
  strappy_database_query_arguments_init(arguments);
}

static void strappy_helper_datetime_arguments_init(
  strappy_helper_datetime_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->items = NULL;
  arguments->item_count = 0U;
  arguments->unit = (strappy_cocoa_timestamp_unit)0;
}

static void strappy_helper_datetime_arguments_destroy(
  strappy_helper_datetime_arguments *arguments)
{
  size_t index;

  if (arguments == NULL) {
    return;
  }

  for (index = 0U; index < arguments->item_count; index++) {
    free(arguments->items[index]);
  }
  free(arguments->items);
  strappy_helper_datetime_arguments_init(arguments);
}

static void strappy_memory_save_arguments_init(
  strappy_memory_save_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->fact = NULL;
}

static void strappy_memory_save_arguments_destroy(
  strappy_memory_save_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->fact);
  strappy_memory_save_arguments_init(arguments);
}

static void strappy_session_rename_arguments_init(
  strappy_session_rename_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->name = NULL;
}

static void strappy_session_rename_arguments_destroy(
  strappy_session_rename_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->name);
  strappy_session_rename_arguments_init(arguments);
}

static void strappy_memory_delete_arguments_init(
  strappy_memory_delete_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->id = 0;
}

static void strappy_skill_read_arguments_init(
  strappy_skill_read_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->skill_identifier = NULL;
}

static void strappy_skill_read_arguments_destroy(
  strappy_skill_read_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->skill_identifier);
  strappy_skill_read_arguments_init(arguments);
}

static void strappy_database_study_arguments_init(
  strappy_database_study_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->database_id = NULL;
  arguments->description = NULL;
  arguments->context = NULL;
}

static void strappy_database_study_arguments_destroy(
  strappy_database_study_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->database_id);
  free(arguments->description);
  free(arguments->context);
  strappy_database_study_arguments_init(arguments);
}

static void strappy_database_context_arguments_init(
  strappy_database_context_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->database_id = NULL;
}

static void strappy_database_context_arguments_destroy(
  strappy_database_context_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->database_id);
  strappy_database_context_arguments_init(arguments);
}

static void strappy_fontawesome_search_arguments_init(
  strappy_fontawesome_search_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->query = NULL;
}

static void strappy_fontawesome_search_arguments_destroy(
  strappy_fontawesome_search_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  free(arguments->query);
  strappy_fontawesome_search_arguments_init(arguments);
}

static void strappy_fontawesome_confirm_arguments_init(
  strappy_fontawesome_confirm_arguments *arguments)
{
  if (arguments == NULL) {
    return;
  }

  arguments->shortcodes = NULL;
  arguments->shortcode_count = 0U;
}

static void strappy_fontawesome_confirm_arguments_destroy(
  strappy_fontawesome_confirm_arguments *arguments)
{
  size_t index;

  if (arguments == NULL) {
    return;
  }

  for (index = 0U; index < arguments->shortcode_count; index++) {
    free(arguments->shortcodes[index]);
  }
  free(arguments->shortcodes);
  strappy_fontawesome_confirm_arguments_init(arguments);
}

































static int strappy_tools_add_result_guidance(cJSON *result,
                                             const char *resource_dir,
                                             const char *tool_name,
                                             const char *condition_name,
                                             char **error_out)
{
  cJSON *root;
  cJSON *result_guidance;
  cJSON *tool_guidance;
  cJSON *guidance;

  if (!cJSON_IsObject(result) || (tool_name == NULL) ||
      (tool_name[0] == '\0') || (condition_name == NULL) ||
      (condition_name[0] == '\0')) {
    strappy_set_error(error_out, "Tool result guidance lookup is incomplete.");
    return 0;
  }
  root = strappy_tools_read_json_resource(resource_dir,
                                          STRAPPY_TOOL_GUIDANCE_RESOURCE,
                                          error_out);
  if (root == NULL) {
    return 0;
  }
  result_guidance = cJSON_GetObjectItemCaseSensitive(
    root,
    STRAPPY_TOOL_RESULT_GUIDANCE_KEY);
  tool_guidance = cJSON_IsObject(result_guidance) ?
    cJSON_GetObjectItemCaseSensitive(result_guidance, tool_name) : NULL;
  guidance = cJSON_IsObject(tool_guidance) ?
    cJSON_GetObjectItemCaseSensitive(tool_guidance, condition_name) : NULL;
  if ((guidance == NULL) || !cJSON_IsString(guidance) ||
      (guidance->valuestring == NULL) ||
      (guidance->valuestring[0] == '\0')) {
    cJSON_Delete(root);
    strappy_set_formatted_error(
      error_out,
      "Tool result guidance is missing result_guidance.%s.%s.",
      tool_name,
      condition_name);
    return 0;
  }
  if (cJSON_AddStringToObject(result,
                             "guidance",
                             guidance->valuestring) == NULL) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not add tool result guidance.");
    return 0;
  }
  cJSON_Delete(root);
  return 1;
}

static char *strappy_tools_tool_guidance_optional_string(
  const char *resource_dir,
  const char *section_name,
  const char *key)
{
  char *error;
  char *value;

  error = NULL;
  value = strappy_tools_tool_guidance_string(resource_dir,
                                             section_name,
                                             key,
                                             &error);
  free(error);
  return value;
}

static void strappy_tools_set_error_with_tool_guidance(char **error_out,
                                                       const char *message,
                                                       const char *resource_dir,
                                                       const char *guidance_key)
{
  char *guidance;

  guidance = strappy_tools_tool_guidance_optional_string(resource_dir,
                                                         "tool_errors",
                                                         guidance_key);
  if ((guidance != NULL) && (guidance[0] != '\0')) {
    strappy_set_formatted_error(error_out, "%s %s", message, guidance);
  } else {
    strappy_set_error(error_out, message);
  }

  free(guidance);
}

static int strappy_tools_validate_empty_arguments(const char *tool_name,
                                                   const char *arguments_json,
                                                   char **error_out)
{
  cJSON *root;

  if ((tool_name == NULL) || (tool_name[0] == '\0')) {
    strappy_set_error(error_out, "Tool name is missing.");
    return 0;
  }
  if ((arguments_json == NULL) || (arguments_json[0] == '\0')) {
    return 1;
  }

  root = cJSON_Parse(arguments_json);
  if (root == NULL) {
    strappy_set_error(error_out, "Tool arguments are not valid JSON.");
    return 0;
  }

  if (!cJSON_IsObject(root) || (root->child != NULL)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "%s takes no arguments.",
                                tool_name);
    return 0;
  }

  cJSON_Delete(root);
  return 1;
}

static int strappy_tools_record_has_database_id(
  const strappy_discovered_database_record *record)
{
  return ((record != NULL) &&
          (record->assistant_database_id != NULL) &&
          (record->assistant_database_id[0] != '\0')) ? 1 : 0;
}

static int strappy_tools_record_decision_is(
  const strappy_discovered_database_record *record,
  const char *decision)
{
  return ((record != NULL) &&
          (record->user_decision != NULL) &&
          (decision != NULL) &&
          (strcmp(record->user_decision, decision) == 0)) ? 1 : 0;
}

static int strappy_tools_record_is_available_database(
  const strappy_discovered_database_record *record)
{
  return ((record != NULL) &&
          record->is_valid_sqlite &&
          strappy_tools_record_has_database_id(record) &&
          strappy_tools_record_decision_is(record, "allowed")) ? 1 : 0;
}

static int strappy_tools_add_bool_to_object(cJSON *object,
                                            const char *key,
                                            int value)
{
  cJSON *item;

  if ((object == NULL) || (key == NULL)) {
    return 0;
  }

  item = cJSON_CreateBool(value ? 1 : 0);
  if (item == NULL) {
    return 0;
  }

  if (!cJSON_AddItemToObject(object, key, item)) {
    cJSON_Delete(item);
    return 0;
  }

  return 1;
}

static char *strappy_tools_build_empty_result(char **error_out)
{
  char *json;

  json = strappy_string_duplicate("{}");
  if (json == NULL) {
    strappy_set_error(error_out, "Could not allocate empty tool result.");
  }
  return json;
}

static char *strappy_tools_database_guidance_string(const char *resource_dir,
                                                    const char *section_name,
                                                    const char *key,
                                                    char **error_out)
{
  cJSON *root;
  cJSON *section;
  cJSON *value;
  char *copy;

  if ((section_name == NULL) || (key == NULL)) {
    strappy_set_error(error_out, "Database guidance lookup is incomplete.");
    return NULL;
  }

  root = strappy_tools_read_json_resource(resource_dir,
                                          STRAPPY_DATABASE_GUIDANCE_RESOURCE,
                                          error_out);
  if (root == NULL) {
    return NULL;
  }

  section = cJSON_GetObjectItem(root, section_name);
  value = cJSON_IsObject(section) ? cJSON_GetObjectItem(section, key) : NULL;
  if ((value == NULL) || !cJSON_IsString(value) ||
      (value->valuestring == NULL) ||
      (value->valuestring[0] == '\0')) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "Database guidance is missing %s.%s.",
                                section_name,
                                key);
    return NULL;
  }

  copy = strappy_string_duplicate(value->valuestring);
  cJSON_Delete(root);
  if (copy == NULL) {
    strappy_set_error(error_out, "Could not allocate database guidance string.");
    return NULL;
  }

  return copy;
}

static char *strappy_tools_database_guidance_optional_string(
  const char *resource_dir,
  const char *section_name,
  const char *key)
{
  char *error;
  char *value;

  error = NULL;
  value = strappy_tools_database_guidance_string(resource_dir,
                                                 section_name,
                                                 key,
                                                 &error);
  free(error);
  return value;
}

static void strappy_tools_set_error_with_database_guidance(
  char **error_out,
  const char *message,
  const char *resource_dir,
  const char *guidance_key)
{
  char *guidance;

  guidance = strappy_tools_database_guidance_optional_string(
    resource_dir,
    "recovery_guidance",
    guidance_key);

  if ((guidance != NULL) && (guidance[0] != '\0')) {
    strappy_set_formatted_error(error_out, "%s %s", message, guidance);
  } else {
    strappy_set_error(error_out, message);
  }

  free(guidance);
}

static int strappy_tools_add_null_to_object(cJSON *object, const char *key)
{
  cJSON *item;

  if ((object == NULL) || (key == NULL)) {
    return 0;
  }

  item = cJSON_CreateNull();
  if (item == NULL) {
    return 0;
  }

  if (!cJSON_AddItemToObject(object, key, item)) {
    cJSON_Delete(item);
    return 0;
  }

  return 1;
}

static int strappy_tools_string_has_value(const char *value)
{
  return ((value != NULL) && (value[0] != '\0')) ? 1 : 0;
}

static int strappy_tools_string_has_non_whitespace(const char *value)
{
  const char *cursor;

  if (value == NULL) {
    return 0;
  }
  for (cursor = value; cursor[0] != '\0'; cursor++) {
    if ((cursor[0] != ' ') && (cursor[0] != '\t') &&
        (cursor[0] != '\r') && (cursor[0] != '\n') &&
        (cursor[0] != '\f') && (cursor[0] != '\v')) {
      return 1;
    }
  }
  return 0;
}

static int strappy_tools_string_is_null_literal(const char *value)
{
  const char *start;
  const char *end;

  if (value == NULL) {
    return 0;
  }
  start = value;
  while ((start[0] == ' ') || (start[0] == '\t') ||
         (start[0] == '\r') || (start[0] == '\n') ||
         (start[0] == '\f') || (start[0] == '\v')) {
    start++;
  }
  end = start + strlen(start);
  while ((end > start) &&
         ((end[-1] == ' ') || (end[-1] == '\t') ||
          (end[-1] == '\r') || (end[-1] == '\n') ||
          (end[-1] == '\f') || (end[-1] == '\v'))) {
    end--;
  }
  return (((end - start) == 4) &&
          ((start[0] == 'n') || (start[0] == 'N')) &&
          ((start[1] == 'u') || (start[1] == 'U')) &&
          ((start[2] == 'l') || (start[2] == 'L')) &&
          ((start[3] == 'l') || (start[3] == 'L'))) ? 1 : 0;
}

static cJSON *strappy_tools_parse_arguments_object(const char *tool_name,
                                                   const char *arguments_json,
                                                   char **error_out)
{
  cJSON *root;

  if (!strappy_tools_string_has_value(arguments_json)) {
    root = cJSON_CreateObject();
    if (root == NULL) {
      strappy_set_formatted_error(error_out,
                                  "Could not allocate %s arguments.",
                                  tool_name);
    }
    return root;
  }

  root = cJSON_Parse(arguments_json);
  if (root == NULL) {
    strappy_set_formatted_error(error_out,
                                "%s arguments are not valid JSON.",
                                tool_name);
    return NULL;
  }

  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "%s arguments must be a JSON object.",
                                tool_name);
    return NULL;
  }

  return root;
}

static int strappy_tools_json_object_accepts_only(
  cJSON *root,
  const char *tool_name,
  const char *const *allowed_names,
  size_t allowed_count,
  char **error_out)
{
  cJSON *child;

  if ((root == NULL) || (tool_name == NULL) || (allowed_names == NULL)) {
    strappy_set_error(error_out, "Tool argument validation is incomplete.");
    return 0;
  }

  for (child = root->child; child != NULL; child = child->next) {
    size_t index;
    int found;

    if (child->string == NULL) {
      strappy_set_formatted_error(error_out,
                                  "%s arguments contain an unnamed field.",
                                  tool_name);
      return 0;
    }

    found = 0;
    for (index = 0U; index < allowed_count; index++) {
      if (strcmp(child->string, allowed_names[index]) == 0) {
        found = 1;
        break;
      }
    }

    if (!found) {
      strappy_set_formatted_error(error_out,
                                  "%s does not accept argument '%s'.",
                                  tool_name,
                                  child->string);
      return 0;
    }
  }

  return 1;
}

static int strappy_tools_copy_string_argument(const char *tool_name,
                                              cJSON *root,
                                              const char *name,
                                              int required,
                                              size_t max_bytes,
                                              char **value_out,
                                              char **error_out)
{
  cJSON *item;
  size_t length;

  if (value_out == NULL) {
    strappy_set_error(error_out, "Tool string argument output is missing.");
    return 0;
  }
  *value_out = NULL;

  item = cJSON_GetObjectItemCaseSensitive(root, name);
  if (item == NULL) {
    if (required) {
      strappy_set_formatted_error(error_out,
                                  "%s requires a non-empty %s string.",
                                  tool_name,
                                  name);
      return 0;
    }
    return 1;
  }

  if (!cJSON_IsString(item)) {
    strappy_set_formatted_error(error_out,
                                "%s %s must be a string.",
                                tool_name,
                                name);
    return 0;
  }

  if (!strappy_tools_string_has_value(item->valuestring)) {
    if (required) {
      strappy_set_formatted_error(error_out,
                                  "%s requires a non-empty %s string.",
                                  tool_name,
                                  name);
      return 0;
    }
    return 1;
  }

  length = strlen(item->valuestring);
  if (length > max_bytes) {
    strappy_set_formatted_error(error_out,
                                "%s %s is too long; maximum is %u bytes.",
                                tool_name,
                                name,
                                (unsigned int)max_bytes);
    return 0;
  }

  *value_out = strappy_string_duplicate(item->valuestring);
  if (*value_out == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not allocate %s %s argument.",
                                tool_name,
                                name);
    return 0;
  }

  return 1;
}

static int strappy_tools_copy_required_action_string_argument(
  const char *tool_name,
  cJSON *root,
  const char *name,
  size_t max_bytes,
  char **value_out,
  char **error_out)
{
  if (value_out == NULL) {
    strappy_set_error(error_out, "Tool action string output is missing.");
    return 0;
  }
  *value_out = NULL;
  if (!strappy_tools_copy_string_argument(tool_name,
                                          root,
                                          name,
                                          1,
                                          max_bytes,
                                          value_out,
                                          error_out)) {
    return 0;
  }
  if (!strappy_tools_string_has_non_whitespace(*value_out) ||
      strappy_tools_string_is_null_literal(*value_out)) {
    free(*value_out);
    *value_out = NULL;
    strappy_set_formatted_error(error_out,
                                "%s %s must not be blank or null.",
                                tool_name,
                                name);
    return 0;
  }
  return 1;
}

static int strappy_tools_parse_memory_delete_arguments(
  const char *arguments_json,
  strappy_memory_delete_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = { "id" };
  cJSON *root;
  cJSON *id;

  if (arguments == NULL) {
    strappy_set_formatted_error(error_out,
                                "%s argument output is missing.",
                                STRAPPY_TOOL_MEMORY_DELETE);
    return 0;
  }
  strappy_memory_delete_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(STRAPPY_TOOL_MEMORY_DELETE,
                                              arguments_json,
                                              error_out);
  if (root == NULL) {
    return 0;
  }

  if (!strappy_tools_json_object_accepts_only(root,
                                              STRAPPY_TOOL_MEMORY_DELETE,
                                              allowed_names,
                                              sizeof(allowed_names) /
                                                sizeof(allowed_names[0]),
                                              error_out)) {
    cJSON_Delete(root);
    return 0;
  }

  id = cJSON_GetObjectItemCaseSensitive(root, "id");
  if (!cJSON_IsNumber(id) || ((double)id->valueint != id->valuedouble) ||
      (id->valueint <= 0)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "%s requires a positive integer id.",
                                STRAPPY_TOOL_MEMORY_DELETE);
    return 0;
  }

  arguments->id = (long long)id->valueint;
  cJSON_Delete(root);
  return 1;
}

static int strappy_tools_parse_skill_read_arguments(
  const char *arguments_json,
  strappy_skill_read_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = { "skill_id" };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(error_out,
                      "skill_read argument output is missing.");
    return 0;
  }
  strappy_skill_read_arguments_init(arguments);
  root = strappy_tools_parse_arguments_object(STRAPPY_TOOL_SKILL_READ,
                                              arguments_json,
                                              error_out);
  if (root == NULL) {
    return 0;
  }
  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_SKILL_READ,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_required_action_string_argument(
         STRAPPY_TOOL_SKILL_READ,
         root,
         "skill_id",
         STRAPPY_SKILL_ID_MAX_BYTES,
         &arguments->skill_identifier,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_skill_read_arguments_destroy(arguments);
  }
  return ok;
}

static int strappy_tools_parse_database_query_arguments(
  const char *arguments_json,
  strappy_database_query_arguments *arguments,
  char **error_out)
{
  cJSON *root;
  cJSON *database_id;
  cJSON *sql;
  cJSON *child;
  size_t database_id_length;
  size_t sql_length;

  if (arguments == NULL) {
    strappy_set_error(error_out, "database_query argument output is missing.");
    return 0;
  }
  strappy_database_query_arguments_init(arguments);

  if (!strappy_tools_string_has_value(arguments_json)) {
    strappy_set_error(error_out,
                      "database_query requires database_id and sql arguments.");
    return 0;
  }

  root = cJSON_Parse(arguments_json);
  if (root == NULL) {
    strappy_set_error(error_out, "database_query arguments are not valid JSON.");
    return 0;
  }

  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "database_query arguments must be a JSON object.");
    return 0;
  }

  for (child = root->child; child != NULL; child = child->next) {
    if ((child->string == NULL) ||
        ((strcmp(child->string, "database_id") != 0) &&
         (strcmp(child->string, "sql") != 0))) {
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "database_query only accepts database_id and sql.");
      return 0;
    }
  }

  database_id = cJSON_GetObjectItemCaseSensitive(root, "database_id");
  sql = cJSON_GetObjectItemCaseSensitive(root, "sql");
  if (!cJSON_IsString(database_id) ||
      !strappy_tools_string_has_value(database_id->valuestring) ||
      !cJSON_IsString(sql) ||
      !strappy_tools_string_has_value(sql->valuestring)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "database_query requires non-empty database_id and sql strings.");
    return 0;
  }

  database_id_length = strlen(database_id->valuestring);
  if (database_id_length > STRAPPY_DATABASE_QUERY_MAX_DATABASE_ID_BYTES) {
    cJSON_Delete(root);
    strappy_set_formatted_error(
      error_out,
      "database_query database_id is too long; maximum is %u bytes.",
      (unsigned int)STRAPPY_DATABASE_QUERY_MAX_DATABASE_ID_BYTES);
    return 0;
  }

  if (!strappy_tools_string_has_non_whitespace(sql->valuestring)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "database_query sql must not be blank.");
    return 0;
  }

  sql_length = strlen(sql->valuestring);
  if (sql_length > STRAPPY_DATABASE_QUERY_MAX_SQL_BYTES) {
    cJSON_Delete(root);
    strappy_set_formatted_error(
      error_out,
      "database_query SQL is too long; maximum is %u bytes.",
      (unsigned int)STRAPPY_DATABASE_QUERY_MAX_SQL_BYTES);
    return 0;
  }

  arguments->database_id = strappy_string_duplicate(database_id->valuestring);
  arguments->sql = strappy_string_duplicate(sql->valuestring);
  cJSON_Delete(root);
  if ((arguments->database_id == NULL) || (arguments->sql == NULL)) {
    strappy_database_query_arguments_destroy(arguments);
    strappy_set_error(error_out, "Could not allocate database_query arguments.");
    return 0;
  }

  return 1;
}

static int strappy_tools_parse_helper_datetime_arguments(
  const char *tool_name,
  const char *arguments_json,
  const char *items_argument_name,
  strappy_helper_datetime_arguments *arguments,
  char **error_out)
{
  const char *allowed_names[2];
  cJSON *root;
  cJSON *items;
  cJSON *unit;
  int item_count;
  int index;
  size_t total_length;

  if ((tool_name == NULL) || (items_argument_name == NULL) ||
      (arguments == NULL)) {
    strappy_set_error(error_out, "Datetime helper argument output is missing.");
    return 0;
  }
  strappy_helper_datetime_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(tool_name,
                                              arguments_json,
                                              error_out);
  if (root == NULL) {
    return 0;
  }

  allowed_names[0] = items_argument_name;
  allowed_names[1] = "unit";
  if (!strappy_tools_json_object_accepts_only(
        root,
        tool_name,
        allowed_names,
        sizeof(allowed_names) / sizeof(allowed_names[0]),
        error_out)) {
    cJSON_Delete(root);
    return 0;
  }

  unit = cJSON_GetObjectItemCaseSensitive(root, "unit");
  if (!cJSON_IsString(unit) ||
      !strappy_tools_string_has_non_whitespace(unit->valuestring)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "%s requires a non-empty unit string.",
                                tool_name);
    return 0;
  }
  if (!strappy_cocoa_parse_timestamp_unit(unit->valuestring,
                                          &arguments->unit,
                                          error_out)) {
    cJSON_Delete(root);
    return 0;
  }

  items = cJSON_GetObjectItemCaseSensitive(root, items_argument_name);
  if (!cJSON_IsArray(items)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "%s %s must be an array of strings.",
                                tool_name,
                                items_argument_name);
    return 0;
  }

  item_count = cJSON_GetArraySize(items);
  if ((item_count < 1) ||
      ((size_t)item_count > STRAPPY_HELPER_DATETIME_MAX_ITEMS)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(
      error_out,
      "%s %s must contain between 1 and %u items.",
      tool_name,
      items_argument_name,
      (unsigned int)STRAPPY_HELPER_DATETIME_MAX_ITEMS);
    return 0;
  }

  arguments->items = (char **)calloc((size_t)item_count, sizeof(char *));
  if (arguments->items == NULL) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "Could not allocate %s arguments.",
                                tool_name);
    return 0;
  }
  arguments->item_count = (size_t)item_count;

  total_length = 0U;
  for (index = 0; index < item_count; index++) {
    cJSON *item;
    const char *value;
    size_t length;
    size_t start;
    size_t end;

    item = cJSON_GetArrayItem(items, index);
    if (!cJSON_IsString(item) || (item->valuestring == NULL)) {
      cJSON_Delete(root);
      strappy_helper_datetime_arguments_destroy(arguments);
      strappy_set_formatted_error(error_out,
                                  "%s %s items must be strings.",
                                  tool_name,
                                  items_argument_name);
      return 0;
    }

    value = item->valuestring;
    length = strlen(value);
    if (length > (STRAPPY_HELPER_DATETIME_MAX_INPUT_BYTES - total_length)) {
      cJSON_Delete(root);
      strappy_helper_datetime_arguments_destroy(arguments);
      strappy_set_formatted_error(
        error_out,
        "%s %s exceeds the %u-byte combined input limit.",
        tool_name,
        items_argument_name,
        (unsigned int)STRAPPY_HELPER_DATETIME_MAX_INPUT_BYTES);
      return 0;
    }
    total_length += length;

    start = 0U;
    end = length;
    while ((start < end) && strappy_tools_helper_is_space(value[start])) {
      start++;
    }
    while ((end > start) && strappy_tools_helper_is_space(value[end - 1U])) {
      end--;
    }
    if (start == end) {
      cJSON_Delete(root);
      strappy_helper_datetime_arguments_destroy(arguments);
      strappy_set_formatted_error(error_out,
                                  "%s %s contains an empty item.",
                                  tool_name,
                                  items_argument_name);
      return 0;
    }

    arguments->items[index] =
      strappy_string_duplicate_length(value + start, end - start);
    if (arguments->items[index] == NULL) {
      cJSON_Delete(root);
      strappy_helper_datetime_arguments_destroy(arguments);
      strappy_set_formatted_error(error_out,
                                  "Could not allocate %s arguments.",
                                  tool_name);
      return 0;
    }
  }

  cJSON_Delete(root);
  return 1;
}

static int strappy_tools_parse_memory_save_arguments(
  const char *arguments_json,
  strappy_memory_save_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = { "fact" };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(error_out,
                      "memory_save argument output is missing.");
    return 0;
  }
  strappy_memory_save_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(
    STRAPPY_TOOL_MEMORY_SAVE,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_MEMORY_SAVE,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_required_action_string_argument(
         STRAPPY_TOOL_MEMORY_SAVE,
         root,
         "fact",
         STRAPPY_HELPER_INFO_MAX_VALUE_BYTES,
         &arguments->fact,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_memory_save_arguments_destroy(arguments);
  }
  return ok;
}

static int strappy_tools_parse_session_rename_arguments(
  const char *arguments_json,
  strappy_session_rename_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = { "name" };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(
      error_out,
      "session_rename argument output is missing.");
    return 0;
  }
  strappy_session_rename_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(
    STRAPPY_TOOL_SESSION_RENAME,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_SESSION_RENAME,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_SESSION_RENAME,
         root,
         "name",
         1,
         STRAPPY_HELPER_SESSION_NAME_MAX_BYTES,
         &arguments->name,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_session_rename_arguments_destroy(arguments);
  }
  return ok;
}

static int strappy_tools_parse_database_context_arguments(
  const char *arguments_json,
  strappy_database_context_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = { "database_id" };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(error_out,
                      "database_context argument output is missing.");
    return 0;
  }
  strappy_database_context_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(
    STRAPPY_TOOL_DATABASE_CONTEXT,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_DATABASE_CONTEXT,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_required_action_string_argument(
         STRAPPY_TOOL_DATABASE_CONTEXT,
         root,
         "database_id",
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->database_id,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_database_context_arguments_destroy(arguments);
    return 0;
  }
  return 1;
}

static int strappy_tools_parse_fontawesome_search_arguments(
  const char *arguments_json,
  strappy_fontawesome_search_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = { "query" };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(
      error_out,
      "fontawesome_search argument output is missing.");
    return 0;
  }
  strappy_fontawesome_search_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(
    STRAPPY_TOOL_FONTAWESOME_SEARCH,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_FONTAWESOME_SEARCH,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_string_argument(
         STRAPPY_TOOL_FONTAWESOME_SEARCH,
         root,
         "query",
         1,
         STRAPPY_HELPER_FONTAWESOME_MAX_QUERY_BYTES,
         &arguments->query,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_fontawesome_search_arguments_destroy(arguments);
    return 0;
  }
  if (!strappy_tools_string_has_non_whitespace(arguments->query)) {
    strappy_fontawesome_search_arguments_destroy(arguments);
    strappy_set_error(
      error_out,
      "fontawesome_search query must not be blank.");
    return 0;
  }
  return 1;
}

static int strappy_tools_parse_fontawesome_confirm_arguments(
  const char *arguments_json,
  strappy_fontawesome_confirm_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = {
    "shortcodes"
  };
  cJSON *root;
  cJSON *shortcodes;
  cJSON *item;
  int count;
  int index;

  if (arguments == NULL) {
    strappy_set_error(
      error_out,
      "fontawesome_confirm argument output is missing.");
    return 0;
  }
  strappy_fontawesome_confirm_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(
    STRAPPY_TOOL_FONTAWESOME_CONFIRM,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  if (!strappy_tools_json_object_accepts_only(
        root,
        STRAPPY_TOOL_FONTAWESOME_CONFIRM,
        allowed_names,
        sizeof(allowed_names) / sizeof(allowed_names[0]),
        error_out)) {
    cJSON_Delete(root);
    return 0;
  }

  shortcodes = cJSON_GetObjectItemCaseSensitive(root, "shortcodes");
  if (!cJSON_IsArray(shortcodes)) {
    cJSON_Delete(root);
    strappy_set_error(
      error_out,
      "fontawesome_confirm requires a shortcodes array.");
    return 0;
  }

  count = cJSON_GetArraySize(shortcodes);
  if ((count <= 0) ||
      (count > (int)STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODES)) {
    cJSON_Delete(root);
    strappy_set_formatted_error(
      error_out,
      "fontawesome_confirm shortcodes must contain 1 to %u items.",
      (unsigned int)STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODES);
    return 0;
  }

  arguments->shortcodes = (char **)calloc((size_t)count, sizeof(char *));
  if (arguments->shortcodes == NULL) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "Could not allocate Font Awesome shortcode arguments.");
    return 0;
  }

  index = 0;
  for (item = shortcodes->child; item != NULL; item = item->next) {
    size_t length;

    if (!cJSON_IsString(item) || (item->valuestring == NULL)) {
      cJSON_Delete(root);
      strappy_fontawesome_confirm_arguments_destroy(arguments);
      strappy_set_error(
        error_out,
        "fontawesome_confirm shortcodes must be strings.");
      return 0;
    }

    length = strlen(item->valuestring);
    if (length > STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODE_BYTES) {
      cJSON_Delete(root);
      strappy_fontawesome_confirm_arguments_destroy(arguments);
      strappy_set_formatted_error(
        error_out,
        "fontawesome_confirm shortcode is too long; maximum is %u bytes.",
        (unsigned int)STRAPPY_HELPER_FONTAWESOME_MAX_SHORTCODE_BYTES);
      return 0;
    }

    arguments->shortcodes[index] = strappy_string_duplicate(item->valuestring);
    if (arguments->shortcodes[index] == NULL) {
      cJSON_Delete(root);
      strappy_fontawesome_confirm_arguments_destroy(arguments);
      strappy_set_error(error_out,
                        "Could not allocate Font Awesome shortcode argument.");
      return 0;
    }
    index++;
  }

  arguments->shortcode_count = (size_t)count;
  cJSON_Delete(root);
  return 1;
}

static int strappy_tools_parse_database_study_arguments(
  const char *arguments_json,
  strappy_database_study_arguments *arguments,
  char **error_out)
{
  static const char *const allowed_names[] = {
    "database_id",
    "description",
    "context"
  };
  cJSON *root;
  int ok;

  if (arguments == NULL) {
    strappy_set_error(
      error_out,
      "database_study argument output is missing.");
    return 0;
  }
  strappy_database_study_arguments_init(arguments);

  root = strappy_tools_parse_arguments_object(
    STRAPPY_TOOL_DATABASE_STUDY,
    arguments_json,
    error_out);
  if (root == NULL) {
    return 0;
  }

  ok = strappy_tools_json_object_accepts_only(
         root,
         STRAPPY_TOOL_DATABASE_STUDY,
         allowed_names,
         sizeof(allowed_names) / sizeof(allowed_names[0]),
         error_out) &&
       strappy_tools_copy_required_action_string_argument(
         STRAPPY_TOOL_DATABASE_STUDY,
         root,
         "database_id",
         STRAPPY_HELPER_INFO_MAX_SHORT_BYTES,
         &arguments->database_id,
         error_out) &&
       strappy_tools_copy_required_action_string_argument(
         STRAPPY_TOOL_DATABASE_STUDY,
         root,
         "description",
         STRAPPY_HELPER_INFO_MAX_CONTENT_BYTES,
         &arguments->description,
         error_out) &&
       strappy_tools_copy_required_action_string_argument(
         STRAPPY_TOOL_DATABASE_STUDY,
         root,
         "context",
         STRAPPY_HELPER_INFO_MAX_CONTENT_BYTES,
         &arguments->context,
         error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_database_study_arguments_destroy(arguments);
    return 0;
  }
  return 1;
}

static int strappy_tools_helper_is_space(char value)
{
  return ((value == ' ') || (value == '\t') || (value == '\r') ||
          (value == '\n') || (value == '\f') || (value == '\v')) ? 1 : 0;
}

static char *strappy_tools_copy_normalized_session_name(const char *name,
                                                        char **error_out)
{
  char *normalized;
  size_t start;
  size_t end;
  size_t index;
  size_t output_index;
  int pending_space;

  if (name == NULL) {
    strappy_set_error(error_out, "Session name is empty.");
    return NULL;
  }

  start = 0U;
  end = strlen(name);
  while ((start < end) && strappy_tools_helper_is_space(name[start])) {
    start++;
  }
  while ((end > start) && strappy_tools_helper_is_space(name[end - 1U])) {
    end--;
  }
  if (start == end) {
    strappy_set_error(error_out, "Session name is empty.");
    return NULL;
  }

  normalized = (char *)malloc((end - start) + 1U);
  if (normalized == NULL) {
    strappy_set_error(error_out, "Could not allocate session name.");
    return NULL;
  }

  output_index = 0U;
  pending_space = 0;
  for (index = start; index < end; index++) {
    if (strappy_tools_helper_is_space(name[index])) {
      pending_space = 1;
      continue;
    }

    if (pending_space && (output_index > 0U)) {
      normalized[output_index++] = ' ';
    }
    pending_space = 0;
    normalized[output_index++] = name[index];
  }

  if (output_index == 0U) {
    free(normalized);
    strappy_set_error(error_out, "Session name is empty.");
    return NULL;
  }
  if (output_index > STRAPPY_HELPER_SESSION_NAME_MAX_BYTES) {
    free(normalized);
    strappy_set_formatted_error(
      error_out,
      "session_rename name is too long; maximum is %u bytes.",
      (unsigned int)STRAPPY_HELPER_SESSION_NAME_MAX_BYTES);
    return NULL;
  }

  normalized[output_index] = '\0';
  return normalized;
}

static char *strappy_tools_convert_timestamp_token_to_iso8601(
  const char *token,
  size_t length,
  strappy_cocoa_timestamp_unit unit,
  char **error_out)
{
  char *timestamp;
  char *iso8601;

  timestamp = strappy_string_duplicate_length(token, length);
  if (timestamp == NULL) {
    strappy_set_error(error_out, "Could not allocate timestamp token.");
    return NULL;
  }

  iso8601 = strappy_cocoa_copy_iso8601_timestamp_value(timestamp,
                                                       unit,
                                                       error_out);
  free(timestamp);
  return iso8601;
}

static char *strappy_tools_convert_iso8601_token_to_timestamp(
  const char *token,
  size_t length,
  strappy_cocoa_timestamp_unit unit,
  char **error_out)
{
  char *iso8601;
  char *timestamp;

  iso8601 = strappy_string_duplicate_length(token, length);
  if (iso8601 == NULL) {
    strappy_set_error(error_out, "Could not allocate ISO8601 datetime token.");
    return NULL;
  }

  timestamp = strappy_cocoa_copy_timestamp_value_from_iso8601(iso8601,
                                                              unit,
                                                              error_out);
  free(iso8601);
  return timestamp;
}

static const char *strappy_tools_sqlite_column_text(sqlite3_stmt *stmt,
                                                    int column)
{
  const unsigned char *value;

  value = sqlite3_column_text(stmt, column);
  if (value == NULL) {
    return NULL;
  }

  return (const char *)value;
}

static int strappy_tools_open_readonly_database(const char *path,
                                                sqlite3 **db_out,
                                                char **error_out)
{
  sqlite3 *db;
  int rc;

  if (db_out == NULL) {
    strappy_set_error(error_out, "SQLite database output is missing.");
    return 0;
  }
  *db_out = NULL;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(error_out, "Cataloged database path is empty.");
    return 0;
  }

  db = NULL;
  rc = sqlite3_open_v2(path,
                       &db,
                       SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
                       NULL);
  if (rc != SQLITE_OK) {
    const char *message;

    message = "unknown sqlite error";
    if (db != NULL) {
      message = sqlite3_errmsg(db);
    }
    strappy_set_formatted_error(error_out,
                                "Could not open approved database read-only: %s",
                                message);
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  sqlite3_busy_timeout(db, 1000);
  *db_out = db;
  return 1;
}

static int strappy_tools_database_file_is_missing(const char *path)
{
  struct stat info;

  if ((path == NULL) || (path[0] == '\0')) {
    return 0;
  }

  if (stat(path, &info) == 0) {
    return 0;
  }

  return ((errno == ENOENT) || (errno == ENOTDIR)) ? 1 : 0;
}

static int strappy_tools_open_helper_info_database(const char *session_db_path,
                                                  sqlite3 **db_out,
                                                  char **error_out)
{
  sqlite3 *db;
  int rc;

  if (db_out == NULL) {
    strappy_set_error(error_out, "Memory database output is missing.");
    return 0;
  }
  *db_out = NULL;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0')) {
    strappy_set_error(error_out, "Catalog database path is not configured.");
    return 0;
  }

  if (!strappy_db_initialize(session_db_path, error_out)) {
    return 0;
  }

  db = NULL;
  rc = sqlite3_open_v2(session_db_path,
                       &db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                         SQLITE_OPEN_FULLMUTEX,
                       NULL);
  if (rc != SQLITE_OK) {
    const char *message;

    message = "unknown sqlite error";
    if (db != NULL) {
      message = sqlite3_errmsg(db);
    }
    strappy_set_formatted_error(error_out,
                                "Could not open memory database: %s",
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

static int strappy_tools_copy_active_assistant_set_id(
  const char *session_db_path,
  long long active_session_id,
  const char *tool_name,
  char **assistant_set_id_out,
  char **error_out)
{
  if (assistant_set_id_out != NULL) {
    *assistant_set_id_out = NULL;
  }
  if ((assistant_set_id_out == NULL) || (tool_name == NULL)) {
    strappy_set_error(error_out,
                      "Tool assistant-set request is incomplete.");
    return 0;
  }
  if (active_session_id <= 0LL) {
    strappy_set_formatted_error(error_out,
                                "%s requires an active session.",
                                tool_name);
    return 0;
  }
  return strappy_db_get_session_assistant_set(session_db_path,
                                              active_session_id,
                                              assistant_set_id_out,
                                              error_out);
}

static int strappy_tools_load_active_assistant_set_profile(
  const char *session_db_path,
  long long active_session_id,
  const char *resource_dir,
  const char *tool_name,
  strappy_assistant_set_profile *profile,
  char **error_out)
{
  char *assistant_set_id;
  int ok;

  if (profile == NULL) {
    strappy_set_error(error_out,
                      "Tool assistant-set profile output is missing.");
    return 0;
  }
  strappy_assistant_set_profile_init(profile);
  assistant_set_id = NULL;
  if (!strappy_tools_copy_active_assistant_set_id(session_db_path,
                                                  active_session_id,
                                                  tool_name,
                                                  &assistant_set_id,
                                                  error_out)) {
    return 0;
  }
  ok = strappy_assistant_sets_load_profile(resource_dir,
                                           assistant_set_id,
                                           profile,
                                           error_out);
  free(assistant_set_id);
  if (!ok) {
    return 0;
  }
  if (!strappy_assistant_set_profile_allows_tool(profile, tool_name)) {
    strappy_set_formatted_error(error_out,
                                "Tool is not allowed by the assistant set: %s",
                                tool_name);
    strappy_assistant_set_profile_destroy(profile);
    return 0;
  }
  return 1;
}

static int strappy_tools_resolve_source_item_id(sqlite3 *db,
                                                long long session_id,
                                                const char *provider_call_id,
                                                long long *source_item_id_out,
                                                char **error_out)
{
  static const char *sql =
    "SELECT f.item_id FROM function_calls f "
    "JOIN conversation_items i ON i.id = f.item_id "
    "WHERE f.provider_call_id = ? AND i.session_id = ? "
    "AND i.include_in_context=1 ORDER BY i.sequence DESC LIMIT 1;";
  sqlite3_stmt *stmt;
  int rc;

  if ((db == NULL) || (source_item_id_out == NULL)) {
    strappy_set_error(error_out, "Memory provenance request is incomplete.");
    return 0;
  }
  *source_item_id_out = 0LL;
  if ((provider_call_id == NULL) || (provider_call_id[0] == '\0')) {
    return 1;
  }
  if (session_id <= 0LL) {
    strappy_set_error(error_out,
                      "Memory provenance requires an active session.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, provider_call_id, -1,
                         SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare memory provenance: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    if (rc == SQLITE_DONE) {
      strappy_set_error(error_out,
                        "Memory provenance function call was not found.");
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not read memory provenance: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  *source_item_id_out = (long long)sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_tools_add_user_info_row(cJSON *array,
                                           sqlite3_stmt *stmt,
                                           char **error_out)
{
  cJSON *object;
  const char *fact;
  const char *date_saved;

  if ((array == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "User fact row request is incomplete.");
    return 0;
  }

  fact = strappy_tools_sqlite_column_text(stmt, 1);
  date_saved = strappy_tools_sqlite_column_text(stmt, 2);
  if ((fact == NULL) || (date_saved == NULL)) {
    strappy_set_error(error_out, "Stored user fact is incomplete.");
    return 0;
  }

  object = cJSON_CreateObject();
  if (object == NULL) {
    strappy_set_error(error_out, "Could not allocate user fact row.");
    return 0;
  }

  if ((cJSON_AddNumberToObject(object,
                               "id",
                               (double)sqlite3_column_int64(stmt, 0)) == NULL) ||
      (cJSON_AddStringToObject(object, "fact", fact) == NULL) ||
      (cJSON_AddStringToObject(object, "date_saved", date_saved) == NULL) ||
      !cJSON_AddItemToArray(array, object)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build user fact row.");
    return 0;
  }

  return 1;
}

static char *strappy_tools_read_user_info(sqlite3 *db,
                                          const char *assistant_set_id,
                                          char **error_out)
{
  static const char *sql =
    "SELECT id, value, "
    "strftime('%Y-%m-%dT%H:%M:%fZ', created_at_ms / 1000.0, 'unixepoch') "
    "FROM user_facts WHERE assistant_set_id = ?1 AND kind = 'fact' "
    "ORDER BY created_at_ms DESC, id DESC "
    "LIMIT ?2;";
  sqlite3_stmt *stmt;
  cJSON *facts;
  char *json;
  int rc;

  if ((db == NULL) || (assistant_set_id == NULL) ||
      (assistant_set_id[0] == '\0')) {
    strappy_set_error(error_out, "memory_read request is incomplete.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare memory_read: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }

  if ((sqlite3_bind_text(stmt,
                         1,
                         assistant_set_id,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int(stmt, 2, STRAPPY_HELPER_INFO_DEFAULT_LIMIT) !=
       SQLITE_OK)) {
    sqlite3_finalize(stmt);
    strappy_set_formatted_error(error_out,
                                "Could not bind memory_read: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }

  facts = cJSON_CreateArray();
  if (facts == NULL) {
    sqlite3_finalize(stmt);
    strappy_set_error(error_out, "Could not allocate user fact result.");
    return NULL;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (!strappy_tools_add_user_info_row(facts, stmt, error_out)) {
      cJSON_Delete(facts);
      sqlite3_finalize(stmt);
      return NULL;
    }
  }

  if (rc != SQLITE_DONE) {
    cJSON_Delete(facts);
    strappy_set_formatted_error(error_out,
                                "Could not read memory_user_fact rows: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_finalize(stmt);

  json = cJSON_PrintUnformatted(facts);
  cJSON_Delete(facts);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize user fact result.");
    return NULL;
  }

  return json;
}

static char *strappy_tools_remember_user_info(
  sqlite3 *db,
  const char *assistant_set_id,
  const strappy_memory_save_arguments *arguments,
  long long source_item_id,
  char **error_out)
{
  static const char *sql =
    "INSERT INTO user_facts "
    "(assistant_set_id, kind, subject, predicate, value, "
     "confidence_basis_points, "
     "source_item_id, created_at_ms, updated_at_ms) "
    "VALUES (?, 'fact', 'user', 'fact', ?, 7500, ?, ?, ?);";
  sqlite3_stmt *stmt;
  sqlite3_int64 now_ms;
  int rc;

  if ((db == NULL) || (assistant_set_id == NULL) ||
      (assistant_set_id[0] == '\0') || (arguments == NULL) ||
      (arguments->fact == NULL)) {
    strappy_set_error(error_out,
                      "memory_save request is incomplete.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare memory_save: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }

  now_ms = (sqlite3_int64)time(NULL) * 1000;
  if ((sqlite3_bind_text(stmt,
                         1,
                         assistant_set_id,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt,
                         2,
                         arguments->fact,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK) ||
      ((source_item_id > 0LL) ?
         (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)source_item_id) !=
          SQLITE_OK) :
         (sqlite3_bind_null(stmt, 3) != SQLITE_OK)) ||
      (sqlite3_bind_int64(stmt, 4, now_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 5, now_ms) != SQLITE_OK)) {
    sqlite3_finalize(stmt);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_formatted_error(error_out,
                                  "Could not bind memory_save: %s",
                                  sqlite3_errmsg(db));
    }
    return NULL;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save memory_user_fact: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_finalize(stmt);

  return strappy_tools_build_empty_result(error_out);
}

static char *strappy_tools_delete_user_fact(sqlite3 *db,
                                            const char *assistant_set_id,
                                            long long id,
                                            char **error_out)
{
  static const char *sql =
    "DELETE FROM user_facts WHERE id = ? AND assistant_set_id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if ((db == NULL) || (assistant_set_id == NULL) ||
      (assistant_set_id[0] == '\0') || (id <= 0)) {
    strappy_set_error(error_out, "memory_delete request is incomplete.");
    return NULL;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare memory_delete: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }

  if ((sqlite3_bind_int64(stmt, 1, (sqlite3_int64)id) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt,
                         2,
                         assistant_set_id,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind memory_delete: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not execute memory_delete: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  sqlite3_finalize(stmt);

  return strappy_tools_build_empty_result(error_out);
}

static int strappy_tools_read_database_object_names(sqlite3 *db,
                                                    const char *object_type,
                                                    int limit,
                                                    cJSON *names,
                                                    int *truncated_out,
                                                    char **error_out)
{
  static const char *sql =
    "SELECT name FROM sqlite_master "
    "WHERE type = ?1 AND name NOT LIKE 'sqlite_%' "
    "ORDER BY name "
    "LIMIT ?2;";
  sqlite3_stmt *stmt;
  int rc;
  int count;

  if ((db == NULL) || !strappy_tools_string_has_value(object_type) ||
      (limit <= 0) || (names == NULL) || (truncated_out == NULL)) {
    strappy_set_error(error_out,
                      "Database object-name read request is incomplete.");
    return 0;
  }
  *truncated_out = 0;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database object-name read: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  if ((sqlite3_bind_text(stmt,
                         1,
                         object_type,
                         -1,
                         SQLITE_STATIC) != SQLITE_OK) ||
      (sqlite3_bind_int(stmt, 2, limit + 1) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind database object-name read: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  count = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cJSON *name_item;
    const char *name;

    if (count >= limit) {
      *truncated_out = 1;
      continue;
    }

    name = strappy_tools_sqlite_column_text(stmt, 0);
    if (!strappy_tools_string_has_value(name)) {
      strappy_set_error(error_out, "Stored database object name is empty.");
      sqlite3_finalize(stmt);
      return 0;
    }

    name_item = cJSON_CreateString(name);
    if ((name_item == NULL) || !cJSON_AddItemToArray(names, name_item)) {
      cJSON_Delete(name_item);
      strappy_set_error(error_out, "Could not build database object-name list.");
      sqlite3_finalize(stmt);
      return 0;
    }
    count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read database object names: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static char *strappy_tools_read_database_info(
  sqlite3 *catalog_db,
  const strappy_discovered_database_record *record,
  const char *resource_dir,
  char **error_out)
{
  sqlite3 *database;
  cJSON *root;
  cJSON *tables;
  cJSON *views;
  char *context;
  char *json;
  int tables_truncated;
  int views_truncated;

  if ((catalog_db == NULL) || (record == NULL) ||
      !strappy_tools_string_has_value(record->path)) {
    strappy_set_error(error_out,
                      "database_context request is incomplete.");
    return NULL;
  }

  root = cJSON_CreateObject();
  tables = cJSON_CreateArray();
  views = cJSON_CreateArray();
  context = NULL;
  if ((root == NULL) || (tables == NULL) || (views == NULL)) {
    cJSON_Delete(root);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    strappy_set_error(error_out, "Could not allocate database context result.");
    return NULL;
  }

  if (!strappy_study_copy_fresh_value(catalog_db,
                                       record,
                                       STRAPPY_STUDY_KEY_CONTEXT,
                                       &context,
                                       error_out)) {
    cJSON_Delete(root);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    return NULL;
  }

  database = NULL;
  if (!strappy_tools_open_readonly_database(record->path,
                                            &database,
                                            error_out)) {
    free(context);
    cJSON_Delete(root);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    return NULL;
  }

  if (!strappy_tools_read_database_object_names(
        database,
        "table",
        STRAPPY_DATABASE_CONTEXT_TABLE_LIMIT,
        tables,
        &tables_truncated,
        error_out) ||
      !strappy_tools_read_database_object_names(
        database,
        "view",
        STRAPPY_DATABASE_CONTEXT_VIEW_LIMIT,
        views,
        &views_truncated,
        error_out)) {
    sqlite3_close(database);
    free(context);
    cJSON_Delete(root);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    return NULL;
  }
  sqlite3_close(database);

  if ((context != NULL) ?
        (cJSON_AddStringToObject(root, "context", context) == NULL) :
        !strappy_tools_add_null_to_object(root, "context")) {
    free(context);
    cJSON_Delete(root);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    strappy_set_error(error_out, "Could not build database context result.");
    return NULL;
  }
  free(context);
  if (((tables->child != NULL) || (views->child != NULL)) &&
      !strappy_tools_add_result_guidance(
        root,
        resource_dir,
        STRAPPY_TOOL_DATABASE_CONTEXT,
        STRAPPY_TOOL_RESULT_GUIDANCE_ON_NOT_EMPTY_KEY,
        error_out)) {
    cJSON_Delete(root);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    return NULL;
  }
  if (!cJSON_AddItemToObject(root, "tables", tables)) {
    cJSON_Delete(root);
    cJSON_Delete(tables);
    cJSON_Delete(views);
    strappy_set_error(error_out, "Could not build database context result.");
    return NULL;
  }
  tables = NULL;
  if (!strappy_tools_add_bool_to_object(root,
                                        "tables_truncated",
                                        tables_truncated) ||
      !cJSON_AddItemToObject(root, "views", views)) {
    cJSON_Delete(root);
    cJSON_Delete(views);
    strappy_set_error(error_out, "Could not build database context result.");
    return NULL;
  }
  views = NULL;
  if (!strappy_tools_add_bool_to_object(root,
                                        "views_truncated",
                                        views_truncated)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build database context result.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize database context result.");
    return NULL;
  }

  return json;
}

static int strappy_tools_add_database_list_record(
  sqlite3 *catalog_db,
  cJSON *databases,
  const strappy_discovered_database_record *record,
  char **error_out)
{
  cJSON *object;
  char *description;

  if ((catalog_db == NULL) || (databases == NULL) || (record == NULL) ||
      (record->assistant_database_id == NULL) ||
      (record->path == NULL)) {
    strappy_set_error(error_out, "Database list info request is incomplete.");
    return 0;
  }

  object = cJSON_CreateObject();
  if (object == NULL) {
    strappy_set_error(error_out, "Could not allocate database list result.");
    return 0;
  }

  description = NULL;
  if (!strappy_study_copy_fresh_value(catalog_db,
                                       record,
                                       STRAPPY_STUDY_KEY_DESCRIPTION,
                                       &description,
                                       error_out)) {
    cJSON_Delete(object);
    return 0;
  }

  if ((cJSON_AddStringToObject(object,
                               "database_id",
                               record->assistant_database_id) == NULL) ||
      ((description != NULL) ?
         (cJSON_AddStringToObject(object,
                                  "description",
                                  description) == NULL) :
         !strappy_tools_add_null_to_object(object, "description")) ||
      (((record->app_name != NULL) && (record->app_name[0] != '\0')) ?
         (cJSON_AddStringToObject(object,
                                  "app_name",
                                  record->app_name) == NULL) :
         !strappy_tools_add_null_to_object(object, "app_name")) ||
      (cJSON_AddStringToObject(object, "path", record->path) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "size_bytes",
                               (double)record->size) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "modified_at",
                               (double)record->modified_at) == NULL)) {
    free(description);
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build database metadata result.");
    return 0;
  }
  free(description);

  if (!cJSON_AddItemToArray(databases, object)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build database list result.");
    return 0;
  }

  return 1;
}

static int strappy_tools_sql_tail_is_empty(const char *tail)
{
  const char *cursor;

  if (tail == NULL) {
    return 1;
  }

  cursor = tail;
  while (*cursor != '\0') {
    if ((*cursor != ' ') && (*cursor != '\t') && (*cursor != '\r') &&
        (*cursor != '\n') && (*cursor != '\f') && (*cursor != '\v')) {
      return 0;
    }
    cursor++;
  }

  return 1;
}

static char strappy_tools_ascii_lower_char(char value)
{
  if ((value >= 'A') && (value <= 'Z')) {
    return (char)(value + ('a' - 'A'));
  }

  return value;
}

static int strappy_tools_ascii_equal_ignore_case(const char *left,
                                                 const char *right)
{
  size_t index;

  if ((left == NULL) || (right == NULL)) {
    return 0;
  }

  index = 0U;
  while ((left[index] != '\0') && (right[index] != '\0')) {
    if (strappy_tools_ascii_lower_char(left[index]) !=
        strappy_tools_ascii_lower_char(right[index])) {
      return 0;
    }
    index++;
  }

  return ((left[index] == '\0') && (right[index] == '\0')) ? 1 : 0;
}

static int strappy_tools_ascii_starts_with_ignore_case(const char *value,
                                                       const char *prefix)
{
  size_t index;

  if ((value == NULL) || (prefix == NULL)) {
    return 0;
  }

  index = 0U;
  while (prefix[index] != '\0') {
    if (value[index] == '\0') {
      return 0;
    }

    if (strappy_tools_ascii_lower_char(value[index]) !=
        strappy_tools_ascii_lower_char(prefix[index])) {
      return 0;
    }
    index++;
  }

  return 1;
}

static int strappy_tools_database_query_function_is_denied(
  const char *function_name)
{
  if (function_name == NULL) {
    return 0;
  }

  return (strappy_tools_ascii_equal_ignore_case(function_name,
                                                "load_extension") ||
          strappy_tools_ascii_equal_ignore_case(function_name, "readfile") ||
          strappy_tools_ascii_equal_ignore_case(function_name, "writefile") ||
          strappy_tools_ascii_starts_with_ignore_case(function_name,
                                                     "pragma_")) ? 1 : 0;
}

static int strappy_tools_database_query_database_is_allowed(
  const char *database_name)
{
  return ((database_name == NULL) ||
          (strcmp(database_name, "main") == 0)) ? 1 : 0;
}

static int strappy_tools_database_query_authorizer(void *data,
                                                   int action,
                                                   const char *detail1,
                                                   const char *detail2,
                                                   const char *database_name,
                                                   const char *source)
{
  strappy_database_query_authorizer_context *context;

  (void)source;

  context = (strappy_database_query_authorizer_context *)data;

  if ((action == SQLITE_SELECT) || (action == SQLITE_RECURSIVE)) {
    return SQLITE_OK;
  }

  if (action == SQLITE_READ) {
    if (strappy_tools_database_query_database_is_allowed(database_name)) {
      if ((context != NULL) && (detail1 != NULL) &&
          !strappy_tools_ascii_starts_with_ignore_case(detail1, "sqlite_")) {
        context->read_real_table = 1;
      }
      return SQLITE_OK;
    }
  } else if (action == SQLITE_FUNCTION) {
    if (!strappy_tools_database_query_function_is_denied(detail2)) {
      return SQLITE_OK;
    }
  }

  if ((context != NULL) && (context->denied_action == 0)) {
    context->denied_action = action;
  }

  return SQLITE_DENY;
}

static int strappy_tools_database_query_progress(void *data)
{
  strappy_database_query_progress_context *context;
  clock_t now;
  double elapsed_seconds;

  context = (strappy_database_query_progress_context *)data;
  if (context == NULL) {
    return 0;
  }

  context->progress_calls++;
  if (context->progress_calls > STRAPPY_DATABASE_QUERY_MAX_PROGRESS_CALLS) {
    context->timed_out = 1;
    return 1;
  }

  if (context->started_at == (clock_t)-1) {
    return 0;
  }

  now = clock();
  if (now == (clock_t)-1) {
    return 0;
  }

  elapsed_seconds =
    ((double)(now - context->started_at)) / ((double)CLOCKS_PER_SEC);
  if (elapsed_seconds >= STRAPPY_DATABASE_QUERY_TIMEOUT_SECONDS) {
    context->timed_out = 1;
    return 1;
  }

  return 0;
}

static size_t strappy_tools_bounded_strlen(const char *value,
                                           size_t max_length)
{
  size_t length;

  if (value == NULL) {
    return 0U;
  }

  length = 0U;
  while ((length < max_length) && (value[length] != '\0')) {
    length++;
  }

  return length;
}

static size_t strappy_tools_utf8_prefix_length(const char *value,
                                               size_t max_length)
{
  size_t length;

  if (value == NULL) {
    return 0U;
  }

  length = max_length;
  while ((length > 0U) &&
         (((unsigned char)value[length] & 0xC0U) == 0x80U)) {
    length--;
  }
  return length;
}

static int strappy_tools_size_add_would_exceed(size_t current,
                                               size_t addition,
                                               size_t limit)
{
  return (current > limit) || (addition > (limit - current));
}

static cJSON *strappy_tools_create_truncated_text_value(
  const char *text,
  size_t byte_count,
  size_t visible_length,
  size_t *payload_bytes_out,
  char **error_out)
{
  cJSON *object;
  char *prefix;
  size_t prefix_length;

  prefix_length = visible_length;
  if (prefix_length > STRAPPY_DATABASE_QUERY_MAX_CELL_BYTES) {
    prefix_length =
      strappy_tools_utf8_prefix_length(text,
                                       STRAPPY_DATABASE_QUERY_MAX_CELL_BYTES);
  }

  prefix = strappy_string_duplicate_length(text, prefix_length);
  if (prefix == NULL) {
    strappy_set_error(error_out, "Could not allocate truncated text value.");
    return NULL;
  }

  object = cJSON_CreateObject();
  if (object == NULL) {
    free(prefix);
    strappy_set_error(error_out, "Could not allocate truncated text value.");
    return NULL;
  }

  if ((cJSON_AddStringToObject(object, "type", "text") == NULL) ||
      (cJSON_AddStringToObject(object, "value", prefix) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "size_bytes",
                               (double)byte_count) == NULL) ||
      !strappy_tools_add_bool_to_object(object, "truncated", 1) ||
      ((visible_length < byte_count) &&
       !strappy_tools_add_bool_to_object(object,
                                         "contains_nul_bytes",
                                         1))) {
    cJSON_Delete(object);
    free(prefix);
    strappy_set_error(error_out, "Could not build truncated text value.");
    return NULL;
  }

  free(prefix);
  if (payload_bytes_out != NULL) {
    *payload_bytes_out = prefix_length + 64U;
  }

  return object;
}

static cJSON *strappy_tools_create_blob_value(size_t byte_count,
                                              size_t *payload_bytes_out,
                                              char **error_out)
{
  cJSON *object;

  object = cJSON_CreateObject();
  if (object == NULL) {
    strappy_set_error(error_out, "Could not allocate blob value.");
    return NULL;
  }

  if ((cJSON_AddStringToObject(object, "type", "blob") == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "size_bytes",
                               (double)byte_count) == NULL) ||
      !strappy_tools_add_bool_to_object(object, "omitted", 1)) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build blob value.");
    return NULL;
  }

  if (payload_bytes_out != NULL) {
    *payload_bytes_out = 64U;
  }

  return object;
}

static cJSON *strappy_tools_create_invalid_text_value(
  size_t byte_count,
  int contains_nul_bytes,
  size_t *payload_bytes_out,
  char **error_out)
{
  cJSON *object;

  object = cJSON_CreateObject();
  if (object == NULL) {
    strappy_set_error(error_out, "Could not allocate invalid text value.");
    return NULL;
  }

  if ((cJSON_AddStringToObject(object, "type", "text") == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "size_bytes",
                               (double)byte_count) == NULL) ||
      !strappy_tools_add_bool_to_object(object, "omitted", 1) ||
      !strappy_tools_add_bool_to_object(object, "invalid_utf8", 1) ||
      (contains_nul_bytes &&
       !strappy_tools_add_bool_to_object(object,
                                         "contains_nul_bytes",
                                         1))) {
    cJSON_Delete(object);
    strappy_set_error(error_out, "Could not build invalid text value.");
    return NULL;
  }

  if (payload_bytes_out != NULL) {
    *payload_bytes_out = 96U;
  }
  return object;
}

static cJSON *strappy_tools_create_database_query_integer_value(
  sqlite3_int64 integer_value,
  size_t *payload_bytes_out,
  char **error_out)
{
  char buffer[32];
  cJSON *value;
  int written;

  if ((integer_value >= STRAPPY_DATABASE_QUERY_JSON_SAFE_INTEGER_MIN) &&
      (integer_value <= STRAPPY_DATABASE_QUERY_JSON_SAFE_INTEGER_MAX)) {
    value = cJSON_CreateNumber((double)integer_value);
    if (payload_bytes_out != NULL) {
      *payload_bytes_out = 32U;
    }
    return value;
  }

  written = snprintf(buffer,
                     sizeof(buffer),
                     "%lld",
                     (long long)integer_value);
  if ((written <= 0) || ((size_t)written >= sizeof(buffer))) {
    strappy_set_error(error_out, "Could not format integer value.");
    return NULL;
  }

  value = cJSON_CreateString(buffer);
  if (value == NULL) {
    strappy_set_error(error_out, "Could not allocate integer value.");
    return NULL;
  }

  if (payload_bytes_out != NULL) {
    *payload_bytes_out = ((size_t)written) + 2U;
  }

  return value;
}

static cJSON *strappy_tools_create_database_query_value(
  sqlite3_stmt *stmt,
  int column,
  size_t *payload_bytes_out,
  char **error_out)
{
  cJSON *value;
  int column_type;
  int byte_count_int;
  size_t byte_count;

  if (payload_bytes_out != NULL) {
    *payload_bytes_out = 0U;
  }

  column_type = sqlite3_column_type(stmt, column);
  if (column_type == SQLITE_NULL) {
    value = cJSON_CreateNull();
    if (payload_bytes_out != NULL) {
      *payload_bytes_out = 4U;
    }
    return value;
  }

  if (column_type == SQLITE_INTEGER) {
    return strappy_tools_create_database_query_integer_value(
      sqlite3_column_int64(stmt, column),
      payload_bytes_out,
      error_out);
  }

  if (column_type == SQLITE_FLOAT) {
    value = cJSON_CreateNumber(sqlite3_column_double(stmt, column));
    if (payload_bytes_out != NULL) {
      *payload_bytes_out = 32U;
    }
    return value;
  }

  byte_count_int = sqlite3_column_bytes(stmt, column);
  if (byte_count_int < 0) {
    strappy_set_error(error_out, "SQLite returned a negative value size.");
    return NULL;
  }
  byte_count = (size_t)byte_count_int;

  if (column_type == SQLITE_BLOB) {
    return strappy_tools_create_blob_value(byte_count,
                                           payload_bytes_out,
                                           error_out);
  }

  if (column_type == SQLITE_TEXT) {
    const char *text;
    int contains_nul_bytes;
    size_t visible_length;

    text = strappy_tools_sqlite_column_text(stmt, column);
    if (text == NULL) {
      value = cJSON_CreateNull();
      if (payload_bytes_out != NULL) {
        *payload_bytes_out = 4U;
      }
      return value;
    }

    visible_length = strappy_tools_bounded_strlen(text, byte_count);
    contains_nul_bytes = (visible_length < byte_count) ? 1 : 0;
    if (!strappy_utf8_validate(text, byte_count)) {
      return strappy_tools_create_invalid_text_value(byte_count,
                                                     contains_nul_bytes,
                                                     payload_bytes_out,
                                                     error_out);
    }
    if ((byte_count > STRAPPY_DATABASE_QUERY_MAX_CELL_BYTES) ||
        contains_nul_bytes) {
      return strappy_tools_create_truncated_text_value(text,
                                                       byte_count,
                                                       visible_length,
                                                       payload_bytes_out,
                                                       error_out);
    }

    value = cJSON_CreateString(text);
    if (payload_bytes_out != NULL) {
      *payload_bytes_out = byte_count;
    }
    return value;
  }

  value = cJSON_CreateNull();
  if (payload_bytes_out != NULL) {
    *payload_bytes_out = 4U;
  }
  return value;
}

static int strappy_tools_add_database_query_columns(sqlite3_stmt *stmt,
                                                    cJSON *root,
                                                    int column_count,
                                                    char **error_out)
{
  cJSON *columns;
  int column;

  columns = cJSON_CreateArray();
  if (columns == NULL) {
    strappy_set_error(error_out, "Could not allocate query columns.");
    return 0;
  }

  for (column = 0; column < column_count; column++) {
    cJSON *column_name;
    char *sanitized_name;
    const char *name;
    size_t name_length;

    name = sqlite3_column_name(stmt, column);
    if (name == NULL) {
      name = "";
    }
    name_length = strlen(name);
    sanitized_name = NULL;
    if (!strappy_utf8_validate(name, name_length)) {
      sanitized_name =
        strappy_utf8_sanitized_string_duplicate(name, name_length);
      if (sanitized_name == NULL) {
        cJSON_Delete(columns);
        strappy_set_error(error_out,
                          "Could not sanitize query column name.");
        return 0;
      }
      name = sanitized_name;
    }

    column_name = cJSON_CreateString(name);
    free(sanitized_name);
    if (column_name == NULL) {
      cJSON_Delete(columns);
      strappy_set_error(error_out, "Could not allocate query column.");
      return 0;
    }

    if (!cJSON_AddItemToArray(columns, column_name)) {
      cJSON_Delete(column_name);
      cJSON_Delete(columns);
      strappy_set_error(error_out, "Could not build query column result.");
      return 0;
    }
  }

  if (!cJSON_AddItemToObject(root, "columns", columns)) {
    cJSON_Delete(columns);
    strappy_set_error(error_out, "Could not build query column result.");
    return 0;
  }

  return 1;
}

static int strappy_tools_add_database_query_rows(
  sqlite3 *db,
  sqlite3_stmt *stmt,
  cJSON *root,
  int column_count,
  strappy_database_query_progress_context *progress,
  int *row_limit_reached_out,
  int *result_size_limit_reached_out,
  char **error_out)
{
  cJSON *rows;
  size_t payload_bytes;
  int row_count;
  int row_limit_reached;
  int result_size_limit_reached;
  int rc;

  rows = cJSON_CreateArray();
  if (rows == NULL) {
    strappy_set_error(error_out, "Could not allocate query rows.");
    return 0;
  }

  payload_bytes = 0U;
  row_count = 0;
  row_limit_reached = 0;
  result_size_limit_reached = 0;

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cJSON *row;
    size_t row_payload_bytes;
    int column;
    int stop_for_result_size;

    if (row_count >= STRAPPY_DATABASE_QUERY_MAX_ROWS) {
      row_limit_reached = 1;
      break;
    }

    row = cJSON_CreateArray();
    if (row == NULL) {
      cJSON_Delete(rows);
      strappy_set_error(error_out, "Could not allocate query row.");
      return 0;
    }

    row_payload_bytes = 0U;
    stop_for_result_size = 0;
    for (column = 0; column < column_count; column++) {
      cJSON *value;
      size_t value_payload_bytes;
      size_t cell_payload_bytes;

      value = strappy_tools_create_database_query_value(
        stmt,
        column,
        &value_payload_bytes,
        error_out);
      if (value == NULL) {
        cJSON_Delete(row);
        cJSON_Delete(rows);
        if ((error_out != NULL) && (*error_out == NULL)) {
          strappy_set_error(error_out, "Could not build query value.");
        }
        return 0;
      }

      cell_payload_bytes = value_payload_bytes;
      if (cell_payload_bytes > (((size_t)-1) - 8U)) {
        cJSON_Delete(value);
        cJSON_Delete(row);
        cJSON_Delete(rows);
        strappy_set_error(error_out, "Query result size overflowed.");
        return 0;
      }
      cell_payload_bytes += 8U;

      if (strappy_tools_size_add_would_exceed(row_payload_bytes,
                                              cell_payload_bytes,
                                              STRAPPY_DATABASE_QUERY_MAX_PAYLOAD_BYTES) ||
          strappy_tools_size_add_would_exceed(payload_bytes,
                                              row_payload_bytes + cell_payload_bytes,
                                              STRAPPY_DATABASE_QUERY_MAX_PAYLOAD_BYTES)) {
        cJSON_Delete(value);
        cJSON_Delete(row);
        result_size_limit_reached = 1;
        stop_for_result_size = 1;
        break;
      }

      if (!cJSON_AddItemToArray(row, value)) {
        cJSON_Delete(value);
        cJSON_Delete(row);
        cJSON_Delete(rows);
        strappy_set_error(error_out, "Could not build query row.");
        return 0;
      }

      row_payload_bytes += cell_payload_bytes;
    }

    if (stop_for_result_size) {
      break;
    }

    if (!cJSON_AddItemToArray(rows, row)) {
      cJSON_Delete(row);
      cJSON_Delete(rows);
      strappy_set_error(error_out, "Could not build query rows.");
      return 0;
    }

    payload_bytes += row_payload_bytes;
    row_count++;
  }

  if ((rc != SQLITE_DONE) && !row_limit_reached &&
      !result_size_limit_reached) {
    if ((progress != NULL) && progress->timed_out) {
      strappy_set_formatted_error(
        error_out,
        "database_query timed out after %.0f seconds.",
        STRAPPY_DATABASE_QUERY_TIMEOUT_SECONDS);
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not run database_query: %s",
                                  sqlite3_errmsg(db));
    }
    cJSON_Delete(rows);
    return 0;
  }

  if (!cJSON_AddItemToObject(root, "rows", rows)) {
    cJSON_Delete(rows);
    strappy_set_error(error_out, "Could not build query rows.");
    return 0;
  }

  if (row_limit_reached_out != NULL) {
    *row_limit_reached_out = row_limit_reached;
  }
  if (result_size_limit_reached_out != NULL) {
    *result_size_limit_reached_out = result_size_limit_reached;
  }
  return 1;
}

static int strappy_tools_prepare_database_query_statement(
  sqlite3 *db,
  const char *sql,
  strappy_database_query_authorizer_context *authorizer_context,
  sqlite3_stmt **stmt_out,
  char **error_out)
{
  sqlite3_stmt *stmt;
  const char *tail;
  int rc;

  if (stmt_out == NULL) {
    strappy_set_error(error_out, "database_query statement output is missing.");
    return 0;
  }
  *stmt_out = NULL;

  if (authorizer_context == NULL) {
    strappy_set_error(error_out, "database_query authorizer context is missing.");
    return 0;
  }

  authorizer_context->denied_action = 0;
  authorizer_context->read_real_table = 0;
  rc = sqlite3_set_authorizer(db,
                              strappy_tools_database_query_authorizer,
                              authorizer_context);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not install database_query authorizer: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  stmt = NULL;
  tail = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, &tail);
  if (rc != SQLITE_OK) {
    if (authorizer_context->denied_action != 0) {
      strappy_set_error(
        error_out,
        "database_query only permits one read-only SELECT or EXPLAIN query "
        "against the approved database. PRAGMA, ATTACH, writes, filesystem "
        "access helpers, and transaction statements are not allowed.");
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare database_query SQL: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_set_authorizer(db, NULL, NULL);
    if (stmt != NULL) {
      sqlite3_finalize(stmt);
    }
    return 0;
  }

  if (stmt == NULL) {
    sqlite3_set_authorizer(db, NULL, NULL);
    strappy_set_error(error_out, "database_query SQL produced no statement.");
    return 0;
  }

  if (!strappy_tools_sql_tail_is_empty(tail)) {
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    strappy_set_error(error_out,
                      "database_query accepts exactly one SQL statement.");
    return 0;
  }

  if (!sqlite3_stmt_readonly(stmt)) {
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    strappy_set_error(error_out, "database_query SQL must be read-only.");
    return 0;
  }

  if (sqlite3_bind_parameter_count(stmt) > 0) {
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    strappy_set_error(error_out,
                      "database_query does not accept bind parameters.");
    return 0;
  }

  if (sqlite3_column_count(stmt) <= 0) {
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    strappy_set_error(error_out,
                      "database_query SQL must return columns.");
    return 0;
  }

  if (sqlite3_column_count(stmt) > STRAPPY_DATABASE_QUERY_MAX_COLUMNS) {
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    strappy_set_formatted_error(
      error_out,
      "database_query selected too many columns; maximum is %d.",
      STRAPPY_DATABASE_QUERY_MAX_COLUMNS);
    return 0;
  }

  *stmt_out = stmt;
  return 1;
}

static void strappy_tools_configure_database_query_connection(sqlite3 *db)
{
  if (db == NULL) {
    return;
  }

  sqlite3_limit(db,
                SQLITE_LIMIT_SQL_LENGTH,
                (int)STRAPPY_DATABASE_QUERY_MAX_SQL_BYTES);
  sqlite3_limit(db, SQLITE_LIMIT_ATTACHED, 0);
  sqlite3_limit(db, SQLITE_LIMIT_COMPOUND_SELECT, 8);
  sqlite3_limit(db, SQLITE_LIMIT_FUNCTION_ARG, 32);
  sqlite3_limit(db, SQLITE_LIMIT_LIKE_PATTERN_LENGTH, 1024);
}

static char *strappy_tools_run_database_query(
  const strappy_discovered_database_record *record,
  const char *sql,
  char **error_out)
{
  sqlite3 *db;
  sqlite3_stmt *stmt;
  cJSON *root;
  char *json;
  strappy_database_query_authorizer_context authorizer_context;
  strappy_database_query_progress_context progress;
  int column_count;
  int row_limit_reached;
  int result_size_limit_reached;
  int rows_truncated;
  int finalize_rc;

  if ((record == NULL) || !strappy_tools_string_has_value(sql)) {
    strappy_set_error(error_out, "database_query request is incomplete.");
    return NULL;
  }

  if (!strappy_tools_open_readonly_database(record->path, &db, error_out)) {
    return NULL;
  }

  strappy_tools_configure_database_query_connection(db);
  progress.started_at = clock();
  progress.progress_calls = 0;
  progress.timed_out = 0;
  sqlite3_progress_handler(db,
                           STRAPPY_DATABASE_QUERY_PROGRESS_INSTRUCTIONS,
                           strappy_tools_database_query_progress,
                           &progress);

  stmt = NULL;
  if (!strappy_tools_prepare_database_query_statement(db,
                                                      sql,
                                                      &authorizer_context,
                                                      &stmt,
                                                      error_out)) {
    sqlite3_progress_handler(db, 0, NULL, NULL);
    sqlite3_close(db);
    return NULL;
  }

  column_count = sqlite3_column_count(stmt);
  root = cJSON_CreateObject();
  if (root == NULL) {
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    sqlite3_progress_handler(db, 0, NULL, NULL);
    sqlite3_close(db);
    strappy_set_error(error_out, "Could not allocate database_query result.");
    return NULL;
  }

  row_limit_reached = 0;
  result_size_limit_reached = 0;
  if (!strappy_tools_add_database_query_columns(stmt,
                                                root,
                                                column_count,
                                                error_out) ||
      !strappy_tools_add_database_query_rows(db,
                                             stmt,
                                             root,
                                             column_count,
                                             &progress,
                                             &row_limit_reached,
                                             &result_size_limit_reached,
                                             error_out)) {
    cJSON_Delete(root);
    sqlite3_finalize(stmt);
    sqlite3_set_authorizer(db, NULL, NULL);
    sqlite3_progress_handler(db, 0, NULL, NULL);
    sqlite3_close(db);
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_error(error_out, "Could not build database_query result.");
    }
    return NULL;
  }

  finalize_rc = sqlite3_finalize(stmt);
  sqlite3_set_authorizer(db, NULL, NULL);
  sqlite3_progress_handler(db, 0, NULL, NULL);
  if (finalize_rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not finalize database_query: %s",
                                sqlite3_errmsg(db));
    cJSON_Delete(root);
    sqlite3_close(db);
    return NULL;
  }
  sqlite3_close(db);

  rows_truncated = (row_limit_reached || result_size_limit_reached) ? 1 : 0;
  if (!strappy_tools_add_bool_to_object(root,
                                        "rows_truncated",
                                        rows_truncated)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Could not build database_query result.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize database_query result.");
    return NULL;
  }

  if (strlen(json) > STRAPPY_DATABASE_QUERY_MAX_RESULT_BYTES) {
    free(json);
    strappy_set_formatted_error(
      error_out,
      "database_query result exceeded the %u byte serialized result limit.",
      (unsigned int)STRAPPY_DATABASE_QUERY_MAX_RESULT_BYTES);
    return NULL;
  }

  return json;
}

static const strappy_discovered_database_record *
strappy_tools_find_database_query_record(
  const strappy_discovered_database_record_list *list,
  const char *database_id,
  int *matched_unavailable_out)
{
  size_t index;

  if (matched_unavailable_out != NULL) {
    *matched_unavailable_out = 0;
  }

  if ((list == NULL) || !strappy_tools_string_has_value(database_id)) {
    return NULL;
  }

  for (index = 0U; index < list->count; index++) {
    const strappy_discovered_database_record *record;

    record = &list->records[index];
    if (!strappy_tools_record_has_database_id(record) ||
        (strcmp(record->assistant_database_id, database_id) != 0)) {
      continue;
    }

    if (strappy_tools_record_is_available_database(record)) {
      return record;
    }

    if (matched_unavailable_out != NULL) {
      *matched_unavailable_out = 1;
    }
    return NULL;
  }

  return NULL;
}

static char *strappy_tools_execute_database_query(const char *session_db_path,
                                                  const char *resource_dir,
                                                  const char *arguments_json,
                                                  char **error_out)
{
  strappy_database_query_arguments arguments;
  strappy_discovered_database_record_list list;
  const strappy_discovered_database_record *record;
  char *json;
  int matched_unavailable;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0')) {
    strappy_set_error(error_out, "Catalog database path is not configured.");
    return NULL;
  }

  strappy_database_query_arguments_init(&arguments);
  if (!strappy_tools_parse_database_query_arguments(arguments_json,
                                                   &arguments,
                                                   error_out)) {
    return NULL;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(session_db_path,
                                           &list,
                                           error_out)) {
    strappy_database_query_arguments_destroy(&arguments);
    return NULL;
  }

  matched_unavailable = 0;
  record = strappy_tools_find_database_query_record(&list,
                                                    arguments.database_id,
                                                    &matched_unavailable);
  if (record == NULL) {
    if (matched_unavailable) {
      strappy_tools_set_error_with_database_guidance(
        error_out,
        "database_query database_id is not approved or is no longer valid.",
        resource_dir,
        "database_query_unavailable_database_id");
    } else {
      strappy_tools_set_error_with_database_guidance(
        error_out,
        "database_query database_id was not found.",
        resource_dir,
        "database_query_missing_database_id");
    }
    strappy_discovered_database_record_list_destroy(&list);
    strappy_database_query_arguments_destroy(&arguments);
    return NULL;
  }

  if (strappy_tools_database_file_is_missing(record->path)) {
    strappy_tools_set_error_with_tool_guidance(
      error_out,
      "database_query approved database file is missing.",
      resource_dir,
      "database_query_missing_file");
    strappy_discovered_database_record_list_destroy(&list);
    strappy_database_query_arguments_destroy(&arguments);
    return NULL;
  }

  json = strappy_tools_run_database_query(record, arguments.sql, error_out);

  strappy_discovered_database_record_list_destroy(&list);
  strappy_database_query_arguments_destroy(&arguments);
  return json;
}

static char *strappy_tools_execute_helper_datetime(
  const char *tool_name,
  const char *arguments_json,
  const char *items_argument_name,
  strappy_tools_datetime_token_converter converter,
  char **error_out)
{
  strappy_helper_datetime_arguments arguments;
  cJSON *results;
  char *json;
  size_t index;

  strappy_helper_datetime_arguments_init(&arguments);
  if (!strappy_tools_parse_helper_datetime_arguments(tool_name,
                                                     arguments_json,
                                                     items_argument_name,
                                                     &arguments,
                                                     error_out)) {
    return NULL;
  }

  results = cJSON_CreateArray();
  if (results == NULL) {
    strappy_helper_datetime_arguments_destroy(&arguments);
    strappy_set_formatted_error(error_out,
                                "Could not build %s result.",
                                tool_name);
    return NULL;
  }

  for (index = 0U; index < arguments.item_count; index++) {
    char *converted;
    cJSON *result;

    converted = converter(arguments.items[index],
                          strlen(arguments.items[index]),
                          arguments.unit,
                          error_out);
    if (converted == NULL) {
      cJSON_Delete(results);
      strappy_helper_datetime_arguments_destroy(&arguments);
      return NULL;
    }

    result = cJSON_CreateString(converted);
    free(converted);
    if ((result == NULL) || !cJSON_AddItemToArray(results, result)) {
      cJSON_Delete(result);
      cJSON_Delete(results);
      strappy_helper_datetime_arguments_destroy(&arguments);
      strappy_set_formatted_error(error_out,
                                  "Could not build %s result.",
                                  tool_name);
      return NULL;
    }
  }

  json = cJSON_PrintUnformatted(results);
  cJSON_Delete(results);
  strappy_helper_datetime_arguments_destroy(&arguments);
  if (json == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not serialize %s result.",
                                tool_name);
    return NULL;
  }
  return json;
}

static char *strappy_tools_execute_datetime_to_iso8601(
  const char *arguments_json,
  char **error_out)
{
  return strappy_tools_execute_helper_datetime(
    STRAPPY_TOOL_DATETIME_TO_ISO8601,
    arguments_json,
    "timestamps",
    strappy_tools_convert_timestamp_token_to_iso8601,
    error_out);
}

static char *strappy_tools_execute_datetime_from_iso8601(
  const char *arguments_json,
  char **error_out)
{
  return strappy_tools_execute_helper_datetime(
    STRAPPY_TOOL_DATETIME_FROM_ISO8601,
    arguments_json,
    "datetimes",
    strappy_tools_convert_iso8601_token_to_timestamp,
    error_out);
}

static unsigned char strappy_tools_ascii_lower(unsigned char value)
{
  if ((value >= 'A') && (value <= 'Z')) {
    return (unsigned char)(value + ('a' - 'A'));
  }

  return value;
}

static int strappy_tools_ascii_equal_length_ignore_case(
  const char *text,
  const char *needle,
  size_t needle_length)
{
  size_t index;

  if ((text == NULL) || (needle == NULL)) {
    return 0;
  }
  if (strlen(text) != needle_length) {
    return 0;
  }

  for (index = 0U; index < needle_length; index++) {
    if (strappy_tools_ascii_lower((unsigned char)text[index]) !=
        strappy_tools_ascii_lower((unsigned char)needle[index])) {
      return 0;
    }
  }

  return 1;
}

static int strappy_tools_ascii_contains_length_ignore_case(
  const char *text,
  const char *needle,
  size_t needle_length)
{
  size_t text_length;
  size_t offset;
  size_t index;

  if ((text == NULL) || (needle == NULL) || (needle_length == 0U)) {
    return 0;
  }

  text_length = strlen(text);
  if (needle_length > text_length) {
    return 0;
  }

  for (offset = 0U; offset <= (text_length - needle_length); offset++) {
    for (index = 0U; index < needle_length; index++) {
      if (strappy_tools_ascii_lower((unsigned char)text[offset + index]) !=
          strappy_tools_ascii_lower((unsigned char)needle[index])) {
        break;
      }
    }
    if (index == needle_length) {
      return 1;
    }
  }

  return 0;
}

static int strappy_tools_database_study_query_reads_real_table(
  const char *database_path,
  const char *sql)
{
  strappy_database_query_authorizer_context authorizer_context;
  sqlite3 *database;
  sqlite3_stmt *stmt;
  char *error;
  int reads_real_table;

  if ((database_path == NULL) || (database_path[0] == '\0') ||
      (sql == NULL) || (sql[0] == '\0')) {
    return 0;
  }

  memset(&authorizer_context, 0, sizeof(authorizer_context));
  database = NULL;
  stmt = NULL;
  error = NULL;
  reads_real_table =
    strappy_tools_open_readonly_database(database_path, &database, &error) &&
    strappy_tools_prepare_database_query_statement(database,
                                                   sql,
                                                   &authorizer_context,
                                                   &stmt,
                                                   &error) &&
    authorizer_context.read_real_table;
  if (stmt != NULL) {
    sqlite3_finalize(stmt);
  }
  if (database != NULL) {
    sqlite3_set_authorizer(database, NULL, NULL);
    sqlite3_close(database);
  }
  free(error);
  return reads_real_table ? 1 : 0;
}

static int strappy_tools_database_study_has_same_turn_research(
  sqlite3 *db,
  long long session_id,
  long long source_item_id,
  const char *database_id,
  const char *database_path,
  char **error_out)
{
  static const char *sql =
    "SELECT f.tool_name, query_sql.text_value "
    "FROM conversation_items current_item "
    "JOIN conversation_items prior_item "
      "ON prior_item.session_id = current_item.session_id "
      "AND prior_item.turn_id = current_item.turn_id "
      "AND prior_item.sequence < current_item.sequence "
    "JOIN function_calls f ON f.item_id = prior_item.id "
    "JOIN tool_executions e ON e.function_call_item_id = f.item_id "
      "AND e.state = 'completed' "
    "JOIN function_outputs o ON o.function_call_item_id = f.item_id "
      "AND o.execution_state = 'completed' "
    "JOIN conversation_items output_item ON output_item.id = o.item_id "
      "AND output_item.session_id = current_item.session_id "
      "AND output_item.turn_id = current_item.turn_id "
      "AND output_item.sequence < current_item.sequence "
    "JOIN structured_documents arguments_document "
      "ON arguments_document.owner_item_id = f.item_id "
      "AND arguments_document.purpose = 'arguments' "
    "JOIN structured_nodes database_id_node "
      "ON database_id_node.document_id = arguments_document.id "
      "AND database_id_node.parent_node_id = 0 "
      "AND database_id_node.member_name = 'database_id' "
      "AND database_id_node.value_type = 'string' "
    "LEFT JOIN structured_nodes query_sql "
      "ON query_sql.document_id = arguments_document.id "
      "AND query_sql.parent_node_id = 0 "
      "AND query_sql.member_name = 'sql' "
      "AND query_sql.value_type = 'string' "
    "WHERE current_item.id = ?1 AND current_item.session_id = ?2 "
      "AND database_id_node.text_value = ?3 "
      "AND f.tool_name IN (?4, ?5) "
    "ORDER BY prior_item.sequence;";
  sqlite3_stmt *stmt;
  int has_context;
  int has_real_table_query;
  int rc;

  if ((db == NULL) || (session_id <= 0LL) || (source_item_id <= 0LL) ||
      (database_id == NULL) || (database_id[0] == '\0') ||
      (database_path == NULL) || (database_path[0] == '\0')) {
    strappy_set_error(
      error_out,
      "Database Study research validation is incomplete.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt,
                          1,
                          (sqlite3_int64)source_item_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt,
                          2,
                          (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt,
                         3,
                         database_id,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt,
                         4,
                         STRAPPY_TOOL_DATABASE_CONTEXT,
                         -1,
                         SQLITE_STATIC) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt,
                         5,
                         STRAPPY_TOOL_DATABASE_QUERY,
                         -1,
                         SQLITE_STATIC) != SQLITE_OK)) {
    strappy_set_formatted_error(
      error_out,
      "Could not prepare Database Study research validation: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  has_context = 0;
  has_real_table_query = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const unsigned char *tool_name;
    const unsigned char *query_sql;

    tool_name = sqlite3_column_text(stmt, 0);
    query_sql = sqlite3_column_text(stmt, 1);
    if ((tool_name != NULL) &&
        (strcmp((const char *)tool_name,
                STRAPPY_TOOL_DATABASE_CONTEXT) == 0)) {
      has_context = 1;
    } else if ((tool_name != NULL) &&
               (strcmp((const char *)tool_name,
                       STRAPPY_TOOL_DATABASE_QUERY) == 0) &&
               (query_sql != NULL) &&
               strappy_tools_database_study_query_reads_real_table(
                 database_path,
                 (const char *)query_sql)) {
      has_real_table_query = 1;
    }
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(
      error_out,
      "Could not read Database Study research validation: %s",
      sqlite3_errmsg(db));
    return 0;
  }

  if (!has_context && !has_real_table_query) {
    strappy_set_error(
      error_out,
      "database_study requires successful database_context and a real-table "
      "database_query for this database in the current study batch; "
      "sqlite_schema queries do not count.");
    return 0;
  }
  if (!has_context) {
    strappy_set_error(
      error_out,
      "database_study requires successful database_context for this database "
      "in the current study batch.");
    return 0;
  }
  if (!has_real_table_query) {
    strappy_set_error(
      error_out,
      "database_study requires a successful database_query against a real "
      "table for this database in the current study batch; sqlite_schema "
      "queries do not count.");
    return 0;
  }

  return 1;
}

static int strappy_tools_fontawesome_query_char(char value)
{
  if ((value >= 'A') && (value <= 'Z')) {
    return 1;
  }
  if ((value >= 'a') && (value <= 'z')) {
    return 1;
  }
  if ((value >= '0') && (value <= '9')) {
    return 1;
  }
  return ((value == '-') || (value == '_')) ? 1 : 0;
}

static int strappy_tools_fontawesome_text_score(const char *text,
                                                const char *query)
{
  const char *cursor;
  size_t query_length;
  int score;

  if (!strappy_tools_string_has_value(text) ||
      !strappy_tools_string_has_value(query)) {
    return 0;
  }

  query_length = strlen(query);
  score = 0;
  if (strappy_tools_ascii_equal_length_ignore_case(text,
                                                   query,
                                                   query_length)) {
    score += 240;
  } else if (strappy_tools_ascii_contains_length_ignore_case(text,
                                                            query,
                                                            query_length)) {
    score += 120;
  }

  cursor = query;
  while (*cursor != '\0') {
    const char *token_start;
    size_t token_length;

    while ((*cursor != '\0') &&
           !strappy_tools_fontawesome_query_char(*cursor)) {
      cursor++;
    }

    token_start = cursor;
    while ((*cursor != '\0') &&
           strappy_tools_fontawesome_query_char(*cursor)) {
      cursor++;
    }

    token_length = (size_t)(cursor - token_start);
    if (token_length == 0U) {
      continue;
    }

    if (strappy_tools_ascii_equal_length_ignore_case(text,
                                                     token_start,
                                                     token_length)) {
      score += 80;
    } else if (strappy_tools_ascii_contains_length_ignore_case(text,
                                                              token_start,
                                                              token_length)) {
      score += 30;
    }
  }

  return score;
}

static int strappy_tools_fontawesome_string_array_score(cJSON *icon,
                                                        const char *key,
                                                        const char *query,
                                                        char **error_out)
{
  cJSON *array;
  cJSON *item;
  int score;

  if ((icon == NULL) || (key == NULL)) {
    strappy_set_error(error_out, "Font Awesome icon search is incomplete.");
    return -1;
  }

  array = cJSON_GetObjectItemCaseSensitive(icon, key);
  if (array == NULL) {
    return 0;
  }
  if (!cJSON_IsArray(array)) {
    strappy_set_formatted_error(
      error_out,
      "Font Awesome icon resource contains malformed %s.",
      key);
    return -1;
  }

  score = 0;
  for (item = array->child; item != NULL; item = item->next) {
    if (!cJSON_IsString(item)) {
      strappy_set_formatted_error(
        error_out,
        "Font Awesome icon resource contains malformed %s item.",
        key);
      return -1;
    }
    score += strappy_tools_fontawesome_text_score(item->valuestring, query);
  }

  return score;
}

static int strappy_tools_fontawesome_icon_score(
  cJSON *icon,
  const strappy_fontawesome_search_arguments *arguments,
  char **error_out)
{
  cJSON *name;
  cJSON *label;
  cJSON *style;
  cJSON *shortcode;
  int score;
  int array_score;

  if ((icon == NULL) || (arguments == NULL)) {
    strappy_set_error(error_out, "Font Awesome icon search is incomplete.");
    return -1;
  }

  name = cJSON_GetObjectItemCaseSensitive(icon, "name");
  label = cJSON_GetObjectItemCaseSensitive(icon, "label");
  style = cJSON_GetObjectItemCaseSensitive(icon, "style");
  shortcode = cJSON_GetObjectItemCaseSensitive(icon, "shortcode");
  if (!cJSON_IsString(name) ||
      !cJSON_IsString(label) ||
      !cJSON_IsString(style) ||
      !cJSON_IsString(shortcode)) {
    strappy_set_error(error_out,
                      "Font Awesome icon resource contains a malformed icon.");
    return -1;
  }

  if (!strappy_tools_string_has_value(arguments->query)) {
    return 1;
  }

  score = 0;
  score += 4 * strappy_tools_fontawesome_text_score(name->valuestring,
                                                    arguments->query);
  score += 3 * strappy_tools_fontawesome_text_score(label->valuestring,
                                                    arguments->query);
  score += strappy_tools_fontawesome_text_score(shortcode->valuestring,
                                                arguments->query);
  array_score = strappy_tools_fontawesome_string_array_score(icon,
                                                             "aliases",
                                                             arguments->query,
                                                             error_out);
  if (array_score < 0) {
    return -1;
  }
  score += 4 * array_score;
  array_score = strappy_tools_fontawesome_string_array_score(icon,
                                                             "search_terms",
                                                             arguments->query,
                                                             error_out);
  if (array_score < 0) {
    return -1;
  }
  score += 2 * array_score;
  array_score = strappy_tools_fontawesome_string_array_score(icon,
                                                             "categories",
                                                             arguments->query,
                                                             error_out);
  if (array_score < 0) {
    return -1;
  }
  score += array_score;

  return score;
}

static int strappy_tools_fontawesome_find_icon_by_shortcode(cJSON *catalog_icons,
                                                            const char *shortcode,
                                                            cJSON **icon_out,
                                                            char **error_out)
{
  cJSON *icon;

  if ((catalog_icons == NULL) || (shortcode == NULL) || (icon_out == NULL)) {
    strappy_set_error(error_out,
                      "Font Awesome shortcode lookup is incomplete.");
    return 0;
  }
  *icon_out = NULL;

  for (icon = catalog_icons->child; icon != NULL; icon = icon->next) {
    cJSON *catalog_shortcode;

    catalog_shortcode = cJSON_GetObjectItemCaseSensitive(icon, "shortcode");
    if (!cJSON_IsString(catalog_shortcode)) {
      strappy_set_error(error_out,
                        "Font Awesome icon resource contains a malformed icon.");
      return 0;
    }

    if (strcmp(catalog_shortcode->valuestring, shortcode) == 0) {
      *icon_out = icon;
      return 1;
    }
  }

  return 1;
}

static int strappy_tools_fontawesome_add_candidate(
  cJSON *icon,
  int score,
  cJSON **candidates,
  int *scores,
  int *candidate_count,
  int limit,
  char **error_out)
{
  cJSON *shortcode;
  cJSON *copy;
  int position;
  int shift_start;
  int index;

  if ((candidates == NULL) || (scores == NULL) ||
      (candidate_count == NULL) || (limit <= 0)) {
    strappy_set_error(error_out, "Font Awesome candidate storage is incomplete.");
    return 0;
  }

  if (score <= 0) {
    return 1;
  }

  position = 0;
  while ((position < *candidate_count) && (scores[position] >= score)) {
    position++;
  }
  if (position >= limit) {
    return 1;
  }

  shortcode = cJSON_IsObject(icon) ?
    cJSON_GetObjectItemCaseSensitive(icon, "shortcode") : NULL;
  if ((shortcode == NULL) || !cJSON_IsString(shortcode) ||
      (shortcode->valuestring == NULL)) {
    strappy_set_error(error_out,
                      "Font Awesome icon resource contains a malformed icon.");
    return 0;
  }
  copy = cJSON_CreateString(shortcode->valuestring);
  if (copy == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate Font Awesome search result.");
    return 0;
  }

  if (*candidate_count < limit) {
    shift_start = *candidate_count;
    (*candidate_count)++;
  } else {
    cJSON_Delete(candidates[limit - 1]);
    shift_start = limit - 1;
  }

  for (index = shift_start; index > position; index--) {
    candidates[index] = candidates[index - 1];
    scores[index] = scores[index - 1];
  }

  candidates[position] = copy;
  scores[position] = score;
  return 1;
}

static void strappy_tools_fontawesome_clear_candidates(cJSON **candidates,
                                                       int candidate_count)
{
  int index;

  if (candidates == NULL) {
    return;
  }

  for (index = 0; index < candidate_count; index++) {
    cJSON_Delete(candidates[index]);
    candidates[index] = NULL;
  }
}

static char *strappy_tools_execute_fontawesome_search(
  const char *resource_dir,
  const char *arguments_json,
  char **error_out)
{
  strappy_fontawesome_search_arguments arguments;
  cJSON *catalog;
  cJSON *catalog_icons;
  cJSON *catalog_icon;
  cJSON *root;
  cJSON *candidates[STRAPPY_HELPER_FONTAWESOME_RESULT_LIMIT];
  int scores[STRAPPY_HELPER_FONTAWESOME_RESULT_LIMIT];
  int index;
  int candidate_count;
  char *json;

  strappy_fontawesome_search_arguments_init(&arguments);
  if (!strappy_tools_parse_fontawesome_search_arguments(
        arguments_json,
        &arguments,
        error_out)) {
    return NULL;
  }

  catalog = strappy_tools_read_json_resource(resource_dir,
                                             STRAPPY_FONTAWESOME_ICONS_RESOURCE,
                                             error_out);
  if (catalog == NULL) {
    strappy_fontawesome_search_arguments_destroy(&arguments);
    return NULL;
  }

  catalog_icons = cJSON_GetObjectItemCaseSensitive(catalog, "icons");
  if (!cJSON_IsArray(catalog_icons)) {
    cJSON_Delete(catalog);
    strappy_fontawesome_search_arguments_destroy(&arguments);
    strappy_set_error(error_out,
                      "Font Awesome icon resource must contain an icons array.");
    return NULL;
  }

  for (index = 0;
       index < STRAPPY_HELPER_FONTAWESOME_RESULT_LIMIT;
       index++) {
    candidates[index] = NULL;
    scores[index] = 0;
  }
  candidate_count = 0;

  for (catalog_icon = catalog_icons->child;
       catalog_icon != NULL;
       catalog_icon = catalog_icon->next) {
    int score;

    score = strappy_tools_fontawesome_icon_score(catalog_icon,
                                                 &arguments,
                                                 error_out);
    if (score < 0) {
      strappy_tools_fontawesome_clear_candidates(candidates, candidate_count);
      cJSON_Delete(catalog);
      strappy_fontawesome_search_arguments_destroy(&arguments);
      return NULL;
    }
    if (score > 0) {
      if (!strappy_tools_fontawesome_add_candidate(catalog_icon,
                                                   score,
                                                   candidates,
                                                   scores,
                                                   &candidate_count,
                                                   STRAPPY_HELPER_FONTAWESOME_RESULT_LIMIT,
                                                   error_out)) {
        strappy_tools_fontawesome_clear_candidates(candidates, candidate_count);
        cJSON_Delete(catalog);
        strappy_fontawesome_search_arguments_destroy(&arguments);
        return NULL;
      }
    }
  }

  root = cJSON_CreateArray();
  if (root == NULL) {
    strappy_tools_fontawesome_clear_candidates(candidates, candidate_count);
    cJSON_Delete(catalog);
    strappy_fontawesome_search_arguments_destroy(&arguments);
    strappy_set_error(error_out,
                      "Could not allocate Font Awesome search result.");
    return NULL;
  }

  for (index = 0; index < candidate_count; index++) {
    if (!cJSON_AddItemToArray(root, candidates[index])) {
      cJSON_Delete(candidates[index]);
      candidates[index] = NULL;
      cJSON_Delete(root);
      strappy_tools_fontawesome_clear_candidates(candidates, candidate_count);
      cJSON_Delete(catalog);
      strappy_fontawesome_search_arguments_destroy(&arguments);
      strappy_set_error(error_out,
                        "Could not add Font Awesome shortcode to search result.");
      return NULL;
    }
    candidates[index] = NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  cJSON_Delete(catalog);
  strappy_fontawesome_search_arguments_destroy(&arguments);
  if (json == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize Font Awesome search result.");
    return NULL;
  }

  return json;
}

static char *strappy_tools_execute_fontawesome_confirm(
  const char *resource_dir,
  const char *arguments_json,
  char **error_out)
{
  strappy_fontawesome_confirm_arguments arguments;
  cJSON *catalog;
  cJSON *catalog_icons;
  cJSON *root;
  cJSON *confirmed;
  cJSON *not_found;
  size_t index;
  char *json;

  strappy_fontawesome_confirm_arguments_init(&arguments);
  if (!strappy_tools_parse_fontawesome_confirm_arguments(
        arguments_json,
        &arguments,
        error_out)) {
    return NULL;
  }

  catalog = strappy_tools_read_json_resource(resource_dir,
                                             STRAPPY_FONTAWESOME_ICONS_RESOURCE,
                                             error_out);
  if (catalog == NULL) {
    strappy_fontawesome_confirm_arguments_destroy(&arguments);
    return NULL;
  }

  catalog_icons = cJSON_GetObjectItemCaseSensitive(catalog, "icons");
  if (!cJSON_IsArray(catalog_icons)) {
    cJSON_Delete(catalog);
    strappy_fontawesome_confirm_arguments_destroy(&arguments);
    strappy_set_error(error_out,
                      "Font Awesome icon resource must contain an icons array.");
    return NULL;
  }

  root = cJSON_CreateObject();
  confirmed = cJSON_CreateArray();
  not_found = cJSON_CreateArray();
  if ((root == NULL) || (confirmed == NULL) || (not_found == NULL)) {
    cJSON_Delete(not_found);
    cJSON_Delete(confirmed);
    cJSON_Delete(root);
    cJSON_Delete(catalog);
    strappy_fontawesome_confirm_arguments_destroy(&arguments);
    strappy_set_error(error_out,
                      "Could not allocate Font Awesome confirm result.");
    return NULL;
  }

  if (!cJSON_AddItemToObject(root, "confirmed", confirmed)) {
    cJSON_Delete(not_found);
    cJSON_Delete(confirmed);
    cJSON_Delete(root);
    cJSON_Delete(catalog);
    strappy_fontawesome_confirm_arguments_destroy(&arguments);
    strappy_set_error(error_out, "Could not build Font Awesome confirm result.");
    return NULL;
  }
  confirmed = NULL;

  if (!cJSON_AddItemToObject(root, "not_found", not_found)) {
    cJSON_Delete(not_found);
    cJSON_Delete(root);
    cJSON_Delete(catalog);
    strappy_fontawesome_confirm_arguments_destroy(&arguments);
    strappy_set_error(error_out, "Could not build Font Awesome confirm result.");
    return NULL;
  }
  not_found = NULL;

  for (index = 0U; index < arguments.shortcode_count; index++) {
    cJSON *icon;
    cJSON *item;
    cJSON *target;

    icon = NULL;
    if (!strappy_tools_fontawesome_find_icon_by_shortcode(catalog_icons,
                                                          arguments.shortcodes[index],
                                                          &icon,
                                                          error_out)) {
      cJSON_Delete(root);
      cJSON_Delete(catalog);
      strappy_fontawesome_confirm_arguments_destroy(&arguments);
      return NULL;
    }

    target = cJSON_GetObjectItemCaseSensitive(
      root,
      (icon != NULL) ? "confirmed" : "not_found");
    item = cJSON_CreateString(arguments.shortcodes[index]);
    if ((item == NULL) || !cJSON_AddItemToArray(target, item)) {
      cJSON_Delete(item);
      cJSON_Delete(root);
      cJSON_Delete(catalog);
      strappy_fontawesome_confirm_arguments_destroy(&arguments);
      strappy_set_error(error_out,
                        "Could not add shortcode to Font Awesome confirm result.");
      return NULL;
    }
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  cJSON_Delete(catalog);
  strappy_fontawesome_confirm_arguments_destroy(&arguments);
  if (json == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize Font Awesome confirm result.");
    return NULL;
  }

  return json;
}

static char *strappy_tools_execute_memory_read(
  const char *session_db_path,
  long long active_session_id,
  const char *arguments_json,
  char **error_out)
{
  char *assistant_set_id;
  sqlite3 *db;
  char *json;

  if (!strappy_tools_validate_empty_arguments(
        STRAPPY_TOOL_MEMORY_READ,
        arguments_json,
        error_out)) {
    return NULL;
  }

  assistant_set_id = NULL;
  if (!strappy_tools_copy_active_assistant_set_id(
        session_db_path,
        active_session_id,
        STRAPPY_TOOL_MEMORY_READ,
        &assistant_set_id,
        error_out)) {
    return NULL;
  }
  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    free(assistant_set_id);
    return NULL;
  }

  json = strappy_tools_read_user_info(db, assistant_set_id, error_out);
  sqlite3_close(db);
  free(assistant_set_id);
  return json;
}

static char *strappy_tools_execute_memory_save(
  const char *session_db_path,
  long long active_session_id,
  const char *provider_call_id,
  const char *arguments_json,
  char **error_out)
{
  strappy_memory_save_arguments arguments;
  char *assistant_set_id;
  sqlite3 *db;
  char *json;
  long long source_item_id;

  strappy_memory_save_arguments_init(&arguments);
  if (!strappy_tools_parse_memory_save_arguments(arguments_json,
                                                   &arguments,
                                                   error_out)) {
    return NULL;
  }
  assistant_set_id = NULL;
  if (!strappy_tools_copy_active_assistant_set_id(
        session_db_path,
        active_session_id,
        STRAPPY_TOOL_MEMORY_SAVE,
        &assistant_set_id,
        error_out)) {
    strappy_memory_save_arguments_destroy(&arguments);
    return NULL;
  }
  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    free(assistant_set_id);
    strappy_memory_save_arguments_destroy(&arguments);
    return NULL;
  }

  source_item_id = 0LL;
  if (!strappy_tools_resolve_source_item_id(db,
                                            active_session_id,
                                            provider_call_id,
                                            &source_item_id,
                                            error_out)) {
    sqlite3_close(db);
    free(assistant_set_id);
    strappy_memory_save_arguments_destroy(&arguments);
    return NULL;
  }
  json = strappy_tools_remember_user_info(db,
                                          assistant_set_id,
                                          &arguments,
                                          source_item_id,
                                          error_out);
  sqlite3_close(db);
  free(assistant_set_id);
  strappy_memory_save_arguments_destroy(&arguments);
  return json;
}

static char *strappy_tools_execute_memory_delete(
  const char *session_db_path,
  long long active_session_id,
  const char *arguments_json,
  char **error_out)
{
  strappy_memory_delete_arguments arguments;
  char *assistant_set_id;
  sqlite3 *db;
  char *json;

  if (!strappy_tools_parse_memory_delete_arguments(arguments_json,
                                                   &arguments,
                                                   error_out)) {
    return NULL;
  }

  assistant_set_id = NULL;
  if (!strappy_tools_copy_active_assistant_set_id(
        session_db_path,
        active_session_id,
        STRAPPY_TOOL_MEMORY_DELETE,
        &assistant_set_id,
        error_out)) {
    return NULL;
  }
  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    free(assistant_set_id);
    return NULL;
  }

  json = strappy_tools_delete_user_fact(db,
                                        assistant_set_id,
                                        arguments.id,
                                        error_out);
  sqlite3_close(db);
  free(assistant_set_id);
  return json;
}

static int strappy_tools_add_skill_summary(
  cJSON *skills,
  const strappy_skill_record *record,
  char **error_out)
{
  cJSON *item;

  if (!cJSON_IsArray(skills) || (record == NULL) ||
      (record->identifier == NULL) || (record->title == NULL) ||
      (record->description == NULL)) {
    strappy_set_error(error_out, "Skill summary is incomplete.");
    return 0;
  }
  item = cJSON_CreateObject();
  if ((item == NULL) ||
      (cJSON_AddStringToObject(item,
                               "skill_id",
                               record->identifier) == NULL) ||
      (cJSON_AddStringToObject(item, "title", record->title) == NULL) ||
      (cJSON_AddStringToObject(item,
                               "description",
                               record->description) == NULL) ||
      !cJSON_AddItemToArray(skills, item)) {
    cJSON_Delete(item);
    strappy_set_error(error_out, "Could not build skill summary.");
    return 0;
  }
  return 1;
}

static char *strappy_tools_execute_skills_list(
  const char *session_db_path,
  long long active_session_id,
  const char *resource_dir,
  const char *arguments_json,
  char **error_out)
{
  strappy_assistant_set_profile profile;
  strappy_skill_record_list list;
  cJSON *root;
  cJSON *skills;
  char *json;
  size_t index;

  if (!strappy_tools_validate_empty_arguments(STRAPPY_TOOL_SKILLS_LIST,
                                              arguments_json,
                                              error_out)) {
    return NULL;
  }
  strappy_assistant_set_profile_init(&profile);
  strappy_skill_record_list_init(&list);
  if (!strappy_tools_load_active_assistant_set_profile(
        session_db_path,
        active_session_id,
        resource_dir,
        STRAPPY_TOOL_SKILLS_LIST,
        &profile,
        error_out) ||
      !strappy_skills_list_allowed(
        resource_dir,
        (const char * const *)profile.skill_identifiers,
        profile.skill_identifier_count,
        &list,
        error_out)) {
    strappy_skill_record_list_destroy(&list);
    strappy_assistant_set_profile_destroy(&profile);
    return NULL;
  }
  strappy_assistant_set_profile_destroy(&profile);

  root = cJSON_CreateObject();
  skills = cJSON_CreateArray();
  if ((root == NULL) || (skills == NULL)) {
    cJSON_Delete(skills);
    cJSON_Delete(root);
    strappy_skill_record_list_destroy(&list);
    strappy_set_error(error_out, "Could not allocate skills_list result.");
    return NULL;
  }
  for (index = 0U; index < list.count; index++) {
    if (!strappy_tools_add_skill_summary(skills,
                                        &list.records[index],
                                        error_out)) {
      cJSON_Delete(skills);
      cJSON_Delete(root);
      strappy_skill_record_list_destroy(&list);
      return NULL;
    }
  }
  if (!cJSON_AddItemToObject(root, "skills", skills)) {
    cJSON_Delete(skills);
    cJSON_Delete(root);
    strappy_skill_record_list_destroy(&list);
    strappy_set_error(error_out, "Could not build skills_list result.");
    return NULL;
  }
  skills = NULL;
  if ((list.count == 0U) &&
      !strappy_tools_add_result_guidance(
        root,
        resource_dir,
        STRAPPY_TOOL_SKILLS_LIST,
        STRAPPY_TOOL_RESULT_GUIDANCE_ON_EMPTY_KEY,
        error_out)) {
    cJSON_Delete(root);
    strappy_skill_record_list_destroy(&list);
    return NULL;
  }
  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  strappy_skill_record_list_destroy(&list);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize skills_list result.");
  }
  return json;
}

static char *strappy_tools_execute_skill_read(
  const char *session_db_path,
  long long active_session_id,
  const char *resource_dir,
  const char *arguments_json,
  char **error_out)
{
  strappy_skill_read_arguments arguments;
  strappy_assistant_set_profile profile;
  strappy_skill_record record;
  cJSON *root;
  char *json;

  strappy_skill_read_arguments_init(&arguments);
  strappy_assistant_set_profile_init(&profile);
  strappy_skill_record_init(&record);
  if (!strappy_tools_parse_skill_read_arguments(arguments_json,
                                                &arguments,
                                                error_out) ||
      !strappy_tools_load_active_assistant_set_profile(
        session_db_path,
        active_session_id,
        resource_dir,
        STRAPPY_TOOL_SKILL_READ,
        &profile,
        error_out) ||
      !strappy_skills_read_allowed(
        resource_dir,
        (const char * const *)profile.skill_identifiers,
        profile.skill_identifier_count,
        arguments.skill_identifier,
        &record,
        error_out)) {
    strappy_skill_record_destroy(&record);
    strappy_assistant_set_profile_destroy(&profile);
    strappy_skill_read_arguments_destroy(&arguments);
    return NULL;
  }
  strappy_assistant_set_profile_destroy(&profile);
  strappy_skill_read_arguments_destroy(&arguments);

  root = cJSON_CreateObject();
  if ((root == NULL) ||
      (cJSON_AddStringToObject(root,
                               "skill_id",
                               record.identifier) == NULL) ||
      (cJSON_AddStringToObject(root, "title", record.title) == NULL) ||
      (cJSON_AddStringToObject(root,
                               "instructions",
                               record.instructions) == NULL)) {
    cJSON_Delete(root);
    strappy_skill_record_destroy(&record);
    strappy_set_error(error_out, "Could not build skill_read result.");
    return NULL;
  }
  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  strappy_skill_record_destroy(&record);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize skill_read result.");
  }
  return json;
}

static char *strappy_tools_execute_session_rename(
  const char *session_db_path,
  long long active_session_id,
  const char *arguments_json,
  char **error_out)
{
  strappy_session_rename_arguments arguments;
  char *name;
  char *json;

  if (active_session_id <= 0) {
    strappy_set_error(error_out,
                      "session_rename requires an active session.");
    return NULL;
  }

  strappy_session_rename_arguments_init(&arguments);
  if (!strappy_tools_parse_session_rename_arguments(arguments_json,
                                                               &arguments,
                                                               error_out)) {
    return NULL;
  }

  name = strappy_tools_copy_normalized_session_name(arguments.name, error_out);
  strappy_session_rename_arguments_destroy(&arguments);
  if (name == NULL) {
    return NULL;
  }

  if (!strappy_db_update_session_name(session_db_path,
                                      active_session_id,
                                      name,
                                      error_out)) {
    free(name);
    return NULL;
  }

  json = strappy_tools_build_empty_result(error_out);
  free(name);
  return json;
}

static char *strappy_tools_execute_database_context(
  const char *session_db_path,
  const char *resource_dir,
  const char *arguments_json,
  char **error_out)
{
  strappy_database_context_arguments arguments;
  strappy_discovered_database_record_list list;
  const strappy_discovered_database_record *record;
  sqlite3 *db;
  char *json;
  int matched_unavailable;

  strappy_database_context_arguments_init(&arguments);
  if (!strappy_tools_parse_database_context_arguments(arguments_json,
                                                               &arguments,
                                                               error_out)) {
    return NULL;
  }
  strappy_discovered_database_record_list_init(&list);
  record = NULL;
  if (!strappy_db_list_discovered_databases(session_db_path,
                                           &list,
                                           error_out)) {
    strappy_database_context_arguments_destroy(&arguments);
    return NULL;
  }

  matched_unavailable = 0;
  record = strappy_tools_find_database_query_record(&list,
                                                    arguments.database_id,
                                                    &matched_unavailable);
  if (record == NULL) {
    if (matched_unavailable) {
      strappy_set_error(error_out,
                        "database_context database_id is not approved "
                        "or is no longer valid.");
    } else {
      strappy_set_error(error_out,
                        "database_context database_id was not found.");
    }
    strappy_discovered_database_record_list_destroy(&list);
    strappy_database_context_arguments_destroy(&arguments);
    return NULL;
  }

  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    strappy_discovered_database_record_list_destroy(&list);
    strappy_database_context_arguments_destroy(&arguments);
    return NULL;
  }

  json = strappy_tools_read_database_info(db,
                                          record,
                                          resource_dir,
                                          error_out);
  sqlite3_close(db);
  strappy_discovered_database_record_list_destroy(&list);
  strappy_database_context_arguments_destroy(&arguments);
  return json;
}

static char *strappy_tools_execute_database_study(
  const char *session_db_path,
  long long active_session_id,
  const char *resource_dir,
  const char *provider_call_id,
  const char *arguments_json,
  char **error_out)
{
  strappy_database_study_arguments arguments;
  strappy_discovered_database_record_list list;
  const strappy_discovered_database_record *record;
  sqlite3 *db;
  char *json;
  long long source_item_id;
  int matched_unavailable;

  if (active_session_id > 0LL) {
    char *assistant_set_id;

    assistant_set_id = NULL;
    if (!strappy_db_get_session_assistant_set(session_db_path,
                                              active_session_id,
                                              &assistant_set_id,
                                              error_out)) {
      return NULL;
    }
    if ((assistant_set_id == NULL) ||
        (strcmp(assistant_set_id,
                STRAPPY_ASSISTANT_SET_DATABASE_STUDY) != 0)) {
      free(assistant_set_id);
      strappy_set_error(
        error_out,
        "database_study is only available in Database Study sessions.");
      return NULL;
    }
    free(assistant_set_id);
  }

  strappy_database_study_arguments_init(&arguments);
  if (!strappy_tools_parse_database_study_arguments(
        arguments_json,
        &arguments,
        error_out)) {
    return NULL;
  }
  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(session_db_path,
                                           &list,
                                           error_out)) {
    strappy_database_study_arguments_destroy(&arguments);
    return NULL;
  }

  matched_unavailable = 0;
  record = strappy_tools_find_database_query_record(&list,
                                                    arguments.database_id,
                                                    &matched_unavailable);
  if (record == NULL) {
    if (matched_unavailable) {
      strappy_tools_set_error_with_database_guidance(
        error_out,
        "database_study database_id is not approved or is no "
        "longer valid.",
        resource_dir,
        "database_study_unavailable_database_id");
    } else {
      strappy_tools_set_error_with_database_guidance(
        error_out,
        "database_study database_id was not found.",
        resource_dir,
        "database_study_missing_database_id");
    }
    strappy_discovered_database_record_list_destroy(&list);
    strappy_database_study_arguments_destroy(&arguments);
    return NULL;
  }

  db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &db,
                                               error_out)) {
    strappy_discovered_database_record_list_destroy(&list);
    strappy_database_study_arguments_destroy(&arguments);
    return NULL;
  }

  source_item_id = 0LL;
  if (!strappy_tools_resolve_source_item_id(db,
                                            active_session_id,
                                            provider_call_id,
                                            &source_item_id,
                                            error_out)) {
    sqlite3_close(db);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_database_study_arguments_destroy(&arguments);
    return NULL;
  }
  if ((active_session_id > 0LL) && (source_item_id <= 0LL)) {
    sqlite3_close(db);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_database_study_arguments_destroy(&arguments);
    strappy_set_error(
      error_out,
      "database_study requires model function-call provenance.");
    return NULL;
  }
  if ((active_session_id > 0LL) &&
      !strappy_tools_database_study_has_same_turn_research(
        db,
        active_session_id,
        source_item_id,
        arguments.database_id,
        record->path,
        error_out)) {
    sqlite3_close(db);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_database_study_arguments_destroy(&arguments);
    return NULL;
  }
  json = strappy_study_save_values(db,
                                   record,
                                   arguments.description,
                                   arguments.context,
                                   source_item_id,
                                   error_out);
  sqlite3_close(db);
  strappy_discovered_database_record_list_destroy(&list);
  strappy_database_study_arguments_destroy(&arguments);
  return json;
}

static char *strappy_tools_execute_database_list(
  const char *session_db_path,
  const char *resource_dir,
  const char *arguments_json,
  char **error_out)
{
  strappy_discovered_database_record_list list;
  sqlite3 *catalog_db;
  cJSON *root;
  cJSON *databases;
  char *json;
  size_t index;
  int empty;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0')) {
    strappy_set_error(error_out, "Catalog database path is not configured.");
    return NULL;
  }

  if (!strappy_tools_validate_empty_arguments(STRAPPY_TOOL_DATABASE_LIST,
                                              arguments_json,
                                              error_out)) {
    return NULL;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(session_db_path,
                                           &list,
                                           error_out)) {
    return NULL;
  }
  catalog_db = NULL;
  if (!strappy_tools_open_helper_info_database(session_db_path,
                                               &catalog_db,
                                               error_out)) {
    strappy_discovered_database_record_list_destroy(&list);
    return NULL;
  }

  root = cJSON_CreateObject();
  databases = cJSON_CreateArray();
  if ((root == NULL) || (databases == NULL)) {
    sqlite3_close(catalog_db);
    cJSON_Delete(root);
    cJSON_Delete(databases);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_set_error(error_out, "Could not allocate tool result.");
    return NULL;
  }

  for (index = 0U; index < list.count; index++) {
    const strappy_discovered_database_record *record;

    record = &list.records[index];
    if (!strappy_tools_record_is_available_database(record)) {
      continue;
    }

    if (!strappy_tools_add_database_list_record(catalog_db,
                                                databases,
                                                record,
                                                error_out)) {
      sqlite3_close(catalog_db);
      cJSON_Delete(root);
      cJSON_Delete(databases);
      strappy_discovered_database_record_list_destroy(&list);
      return NULL;
    }
  }

  empty = (databases->child == NULL) ? 1 : 0;
  if (!cJSON_AddItemToObject(root, "databases", databases)) {
    sqlite3_close(catalog_db);
    cJSON_Delete(root);
    cJSON_Delete(databases);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_set_error(error_out, "Could not build database list result.");
    return NULL;
  }
  databases = NULL;
  if (empty &&
      !strappy_tools_add_result_guidance(
        root,
        resource_dir,
        STRAPPY_TOOL_DATABASE_LIST,
        STRAPPY_TOOL_RESULT_GUIDANCE_ON_EMPTY_KEY,
        error_out)) {
    sqlite3_close(catalog_db);
    cJSON_Delete(root);
    strappy_discovered_database_record_list_destroy(&list);
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  sqlite3_close(catalog_db);
  cJSON_Delete(root);
  strappy_discovered_database_record_list_destroy(&list);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize tool result.");
    return NULL;
  }

  return json;
}

static char *strappy_tools_execute_internal(const char *session_db_path,
                                            long long active_session_id,
                                            const char *resource_dir,
                                            const char *provider_call_id,
                                            const char *tool_name,
                                            const char *arguments_json,
                                            strappy_tools_continue_callback
                                              continue_callback,
                                            void *continue_callback_data,
                                            int *output_truncated_out,
                                            int *cancelled_out,
                                            char **error_out)
{
  if (output_truncated_out != NULL) {
    *output_truncated_out = 0;
  }
  if (cancelled_out != NULL) {
    *cancelled_out = 0;
  }
  if ((tool_name == NULL) || (tool_name[0] == '\0')) {
    strappy_set_error(error_out, "Tool name is empty.");
    return NULL;
  }

  if (strcmp(tool_name, STRAPPY_TOOL_BASH) == 0) {
    return strappy_bash_execute(session_db_path,
                                active_session_id,
                                arguments_json,
                                continue_callback,
                                continue_callback_data,
                                output_truncated_out,
                                cancelled_out,
                                error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_FILE_READ) == 0) {
    return strappy_file_read_execute(session_db_path,
                                     active_session_id,
                                     arguments_json,
                                     error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_FILE_WRITE) == 0) {
    return strappy_file_write_execute(session_db_path,
                                      active_session_id,
                                      arguments_json,
                                      error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_FILE_EDIT) == 0) {
    return strappy_file_edit_execute(session_db_path,
                                     active_session_id,
                                     arguments_json,
                                     error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_DATABASE_LIST) == 0) {
    return strappy_tools_execute_database_list(session_db_path,
                                               resource_dir,
                                               arguments_json,
                                               error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_DATABASE_QUERY) == 0) {
    return strappy_tools_execute_database_query(session_db_path,
                                                resource_dir,
                                                arguments_json,
                                                error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_DATETIME_TO_ISO8601) == 0) {
    return strappy_tools_execute_datetime_to_iso8601(arguments_json,
                                                            error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_DATETIME_FROM_ISO8601) == 0) {
    return strappy_tools_execute_datetime_from_iso8601(arguments_json,
                                                              error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_MEMORY_READ) == 0) {
    return strappy_tools_execute_memory_read(session_db_path,
                                             active_session_id,
                                             arguments_json,
                                             error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_MEMORY_SAVE) == 0) {
    return strappy_tools_execute_memory_save(session_db_path,
                                             active_session_id,
                                             provider_call_id,
                                             arguments_json,
                                             error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_MEMORY_DELETE) == 0) {
    return strappy_tools_execute_memory_delete(session_db_path,
                                               active_session_id,
                                               arguments_json,
                                               error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_SKILLS_LIST) == 0) {
    return strappy_tools_execute_skills_list(session_db_path,
                                             active_session_id,
                                             resource_dir,
                                             arguments_json,
                                             error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_SKILL_READ) == 0) {
    return strappy_tools_execute_skill_read(session_db_path,
                                            active_session_id,
                                            resource_dir,
                                            arguments_json,
                                            error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_SESSION_RENAME) == 0) {
    return strappy_tools_execute_session_rename(session_db_path,
                                                           active_session_id,
                                                           arguments_json,
                                                           error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_DATABASE_CONTEXT) == 0) {
    return strappy_tools_execute_database_context(session_db_path,
                                                  resource_dir,
                                                  arguments_json,
                                                  error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_DATABASE_STUDY) == 0) {
    return strappy_tools_execute_database_study(session_db_path,
                                                               active_session_id,
                                                               resource_dir,
                                                               provider_call_id,
                                                               arguments_json,
                                                               error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_FONTAWESOME_SEARCH) == 0) {
    return strappy_tools_execute_fontawesome_search(resource_dir,
                                                                     arguments_json,
                                                                     error_out);
  }

  if (strcmp(tool_name, STRAPPY_TOOL_FONTAWESOME_CONFIRM) == 0) {
    return strappy_tools_execute_fontawesome_confirm(
      resource_dir,
      arguments_json,
      error_out);
  }

  strappy_set_formatted_error(error_out, "Tool is not registered: %s", tool_name);
  return NULL;
}

char *strappy_tools_execute(const char *session_db_path,
                            long long active_session_id,
                            const char *resource_dir,
                            const char *tool_name,
                            const char *arguments_json,
                            char **error_out)
{
  return strappy_tools_execute_internal(session_db_path,
                                        active_session_id,
                                        resource_dir,
                                        NULL,
                                        tool_name,
                                        arguments_json,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        error_out);
}

char *strappy_tools_execute_preflight(const char *session_db_path,
                                      long long active_session_id,
                                      const char *resource_dir,
                                      const char *tool_name,
                                      const char *arguments_json,
                                      char **error_out)
{
  if ((tool_name != NULL) &&
      (strcmp(tool_name, STRAPPY_TOOL_BASH) == 0)) {
    return strappy_bash_execute_preflight(session_db_path,
                                          active_session_id,
                                          arguments_json,
                                          error_out);
  }
  return strappy_tools_execute(session_db_path,
                               active_session_id,
                               resource_dir,
                               tool_name,
                               arguments_json,
                               error_out);
}

char *strappy_tools_execute_for_function_call(
  const char *session_db_path,
  long long active_session_id,
  const char *resource_dir,
  const char *provider_call_id,
  const char *tool_name,
  const char *arguments_json,
  char **error_out)
{
  if ((provider_call_id == NULL) || (provider_call_id[0] == '\0')) {
    strappy_set_error(error_out, "Tool function call id is empty.");
    return NULL;
  }
  return strappy_tools_execute_internal(session_db_path,
                                        active_session_id,
                                        resource_dir,
                                        provider_call_id,
                                        tool_name,
                                        arguments_json,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        error_out);
}

char *strappy_tools_execute_for_function_call_with_cancellation(
  const char *session_db_path,
  long long active_session_id,
  const char *resource_dir,
  const char *provider_call_id,
  const char *tool_name,
  const char *arguments_json,
  strappy_tools_continue_callback continue_callback,
  void *continue_callback_data,
  int *output_truncated_out,
  int *cancelled_out,
  char **error_out)
{
  if (output_truncated_out != NULL) {
    *output_truncated_out = 0;
  }
  if (cancelled_out != NULL) {
    *cancelled_out = 0;
  }
  if ((provider_call_id == NULL) || (provider_call_id[0] == '\0')) {
    strappy_set_error(error_out, "Tool function call id is empty.");
    return NULL;
  }
  return strappy_tools_execute_internal(session_db_path,
                                        active_session_id,
                                        resource_dir,
                                        provider_call_id,
                                        tool_name,
                                        arguments_json,
                                        continue_callback,
                                        continue_callback_data,
                                        output_truncated_out,
                                        cancelled_out,
                                        error_out);
}
