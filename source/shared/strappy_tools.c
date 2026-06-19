#include "strappy_tools.h"

#include "strappy_core.h"
#include "strappy_db.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

#define STRAPPY_TOOLS_AVAILABILITY_ERROR "error"
#define STRAPPY_TOOLS_AVAILABILITY_SCAN_NEEDED "possible_scan_needed"
#define STRAPPY_TOOLS_AVAILABILITY_WHITELIST_NEEDED "possible_whitelist_needed"
#define STRAPPY_TOOLS_AVAILABILITY_AVAILABLE "available"
#define STRAPPY_DATABASE_MANAGE_HREF "strappy://database-manage"

typedef struct strappy_tool_definition {
  const char *name;
  const char *description;
} strappy_tool_definition;

typedef struct strappy_tools_catalog_summary {
  int discovered_count;
  int valid_sqlite_count;
  int approved_count;
  int pending_approval_count;
  int denied_count;
} strappy_tools_catalog_summary;

static const strappy_tool_definition strappy_tool_definitions[] = {
  {
    STRAPPY_TOOL_DATABASE_LIST,
    "List user-approved SQLite databases available to Strappy. Returns "
    "assistant-visible database IDs, safe catalog metadata, availability "
    "state, and recommended next steps without full filesystem paths."
  }
};

static const size_t strappy_tool_definition_count =
  sizeof(strappy_tool_definitions) / sizeof(strappy_tool_definitions[0]);

static int strappy_tools_add_empty_parameters(cJSON *function)
{
  cJSON *parameters;
  cJSON *properties;
  cJSON *required;
  cJSON *additional_properties;

  if (function == NULL) {
    return 0;
  }

  parameters = cJSON_CreateObject();
  properties = cJSON_CreateObject();
  required = cJSON_CreateArray();
  additional_properties = cJSON_CreateBool(0);
  if ((parameters == NULL) || (properties == NULL) || (required == NULL) ||
      (additional_properties == NULL)) {
    cJSON_Delete(parameters);
    cJSON_Delete(properties);
    cJSON_Delete(required);
    cJSON_Delete(additional_properties);
    return 0;
  }

  if (cJSON_AddStringToObject(parameters, "type", "object") == NULL) {
    cJSON_Delete(parameters);
    cJSON_Delete(properties);
    cJSON_Delete(required);
    cJSON_Delete(additional_properties);
    return 0;
  }

  if (!cJSON_AddItemToObject(parameters, "properties", properties)) {
    cJSON_Delete(parameters);
    cJSON_Delete(properties);
    cJSON_Delete(required);
    cJSON_Delete(additional_properties);
    return 0;
  }
  properties = NULL;

  if (!cJSON_AddItemToObject(parameters, "required", required)) {
    cJSON_Delete(parameters);
    cJSON_Delete(required);
    cJSON_Delete(additional_properties);
    return 0;
  }
  required = NULL;

  if (!cJSON_AddItemToObject(parameters,
                             "additionalProperties",
                             additional_properties)) {
    cJSON_Delete(parameters);
    cJSON_Delete(additional_properties);
    return 0;
  }
  additional_properties = NULL;

  if (!cJSON_AddItemToObject(function, "parameters", parameters)) {
    cJSON_Delete(parameters);
    return 0;
  }

  return 1;
}

static cJSON *strappy_tools_create_tool_schema(
  const strappy_tool_definition *definition)
{
  cJSON *tool;
  cJSON *function;

  if (definition == NULL) {
    return NULL;
  }

  tool = cJSON_CreateObject();
  function = cJSON_CreateObject();
  if ((tool == NULL) || (function == NULL)) {
    cJSON_Delete(tool);
    cJSON_Delete(function);
    return NULL;
  }

  if ((cJSON_AddStringToObject(tool, "type", "function") == NULL) ||
      (cJSON_AddStringToObject(function, "name", definition->name) == NULL) ||
      (cJSON_AddStringToObject(function,
                               "description",
                               definition->description) == NULL) ||
      !strappy_tools_add_empty_parameters(function) ||
      !cJSON_AddItemToObject(tool, "function", function)) {
    cJSON_Delete(tool);
    cJSON_Delete(function);
    return NULL;
  }

  return tool;
}

char *strappy_tools_request_json(char **error_out)
{
  cJSON *tools;
  char *json;
  size_t index;

  tools = cJSON_CreateArray();
  if (tools == NULL) {
    strappy_set_error(error_out, "Could not allocate tool schema list.");
    return NULL;
  }

  for (index = 0U; index < strappy_tool_definition_count; index++) {
    cJSON *tool;

    tool = strappy_tools_create_tool_schema(&strappy_tool_definitions[index]);
    if ((tool == NULL) || !cJSON_AddItemToArray(tools, tool)) {
      cJSON_Delete(tool);
      cJSON_Delete(tools);
      strappy_set_error(error_out, "Could not build tool schema list.");
      return NULL;
    }
  }

  json = cJSON_PrintUnformatted(tools);
  cJSON_Delete(tools);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize tool schema list.");
    return NULL;
  }

  return json;
}

char *strappy_tools_prompt_fragment(char **error_out)
{
  (void)error_out;
  return strappy_string_duplicate(
    "- database_list: Lists valid SQLite databases the user has "
    "approved for Strappy. Takes no arguments. Use the returned database_id "
    "values for database-specific tools. If no database is available, guide "
    "the user to the returned database management link.");
}

static int strappy_tools_validate_empty_arguments(const char *arguments_json,
                                                  char **error_out)
{
  cJSON *root;

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
    strappy_set_error(error_out, "database_list takes no arguments.");
    return 0;
  }

  cJSON_Delete(root);
  return 1;
}

static const char *strappy_tools_path_basename(const char *path)
{
  const char *slash;

  if ((path == NULL) || (path[0] == '\0')) {
    return NULL;
  }

  slash = strrchr(path, '/');
  if ((slash == NULL) || (*(slash + 1) == '\0')) {
    return path;
  }

  return slash + 1;
}

static int strappy_tools_add_database_record(
  cJSON *databases,
  const strappy_discovered_database_record *record)
{
  cJSON *object;
  const char *display_name;

  if ((databases == NULL) || (record == NULL)) {
    return 0;
  }

  object = cJSON_CreateObject();
  if (object == NULL) {
    return 0;
  }

  display_name = strappy_tools_path_basename(record->path);
  if ((cJSON_AddStringToObject(object,
                               "database_id",
                               record->assistant_database_id) == NULL) ||
      ((display_name != NULL) &&
       (cJSON_AddStringToObject(object,
                                "display_name",
                                display_name) == NULL)) ||
      (cJSON_AddNumberToObject(object,
                               "size",
                               (double)record->size) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "modified_at",
                               (double)record->modified_at) == NULL) ||
      ((record->scan_status != NULL) &&
       (cJSON_AddStringToObject(object,
                                "scan_status",
                                record->scan_status) == NULL)) ||
      ((record->first_seen_at != NULL) &&
       (cJSON_AddStringToObject(object,
                                "first_seen_at",
                                record->first_seen_at) == NULL)) ||
      ((record->last_seen_at != NULL) &&
       (cJSON_AddStringToObject(object,
                                "last_seen_at",
                                record->last_seen_at) == NULL)) ||
      ((record->last_scanned_at != NULL) &&
       (cJSON_AddStringToObject(object,
                                "last_scanned_at",
                                record->last_scanned_at) == NULL)) ||
      !cJSON_AddItemToArray(databases, object)) {
    cJSON_Delete(object);
    return 0;
  }

  return 1;
}

static void strappy_tools_catalog_summary_init(
  strappy_tools_catalog_summary *summary)
{
  if (summary == NULL) {
    return;
  }

  summary->discovered_count = 0;
  summary->valid_sqlite_count = 0;
  summary->approved_count = 0;
  summary->pending_approval_count = 0;
  summary->denied_count = 0;
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

static void strappy_tools_catalog_summary_add_record(
  strappy_tools_catalog_summary *summary,
  const strappy_discovered_database_record *record)
{
  if ((summary == NULL) || (record == NULL)) {
    return;
  }

  summary->discovered_count++;
  if (!record->is_valid_sqlite) {
    return;
  }

  summary->valid_sqlite_count++;
  if (strappy_tools_record_decision_is(record, "allowed") &&
      strappy_tools_record_has_database_id(record)) {
    summary->approved_count++;
  } else if (strappy_tools_record_decision_is(record, "denied")) {
    summary->denied_count++;
  } else {
    summary->pending_approval_count++;
  }
}

static const char *strappy_tools_availability_state(
  const strappy_tools_catalog_summary *summary)
{
  if (summary == NULL) {
    return STRAPPY_TOOLS_AVAILABILITY_ERROR;
  }

  if (summary->approved_count > 0) {
    return STRAPPY_TOOLS_AVAILABILITY_AVAILABLE;
  }

  if (summary->valid_sqlite_count > 0) {
    return STRAPPY_TOOLS_AVAILABILITY_WHITELIST_NEEDED;
  }

  return STRAPPY_TOOLS_AVAILABILITY_SCAN_NEEDED;
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

static int strappy_tools_add_catalog_summary(
  cJSON *root,
  const strappy_tools_catalog_summary *summary)
{
  cJSON *object;

  if ((root == NULL) || (summary == NULL)) {
    return 0;
  }

  object = cJSON_CreateObject();
  if (object == NULL) {
    return 0;
  }

  if ((cJSON_AddNumberToObject(object,
                               "discovered_count",
                               (double)summary->discovered_count) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "valid_sqlite_count",
                               (double)summary->valid_sqlite_count) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "approved_count",
                               (double)summary->approved_count) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "pending_approval_count",
                               (double)summary->pending_approval_count) == NULL) ||
      (cJSON_AddNumberToObject(object,
                               "denied_count",
                               (double)summary->denied_count) == NULL) ||
      !cJSON_AddItemToObject(root, "catalog_summary", object)) {
    cJSON_Delete(object);
    return 0;
  }

  return 1;
}

static cJSON *strappy_tools_create_database_manage_step(const char *purpose)
{
  cJSON *step;

  step = cJSON_CreateObject();
  if (step == NULL) {
    return NULL;
  }

  if ((cJSON_AddStringToObject(step, "type", "user_action") == NULL) ||
      (cJSON_AddStringToObject(step, "action", "database_manage") == NULL) ||
      (cJSON_AddStringToObject(step, "label", "Manage databases") == NULL) ||
      (cJSON_AddStringToObject(step,
                               "href",
                               STRAPPY_DATABASE_MANAGE_HREF) == NULL) ||
      !strappy_tools_add_bool_to_object(step,
                                        "requires_user_click",
                                        1) ||
      ((purpose != NULL) &&
       (cJSON_AddStringToObject(step, "purpose", purpose) == NULL))) {
    cJSON_Delete(step);
    return NULL;
  }

  return step;
}

static int strappy_tools_add_recommended_next_steps(
  cJSON *root,
  const char *availability_state)
{
  cJSON *steps;
  cJSON *step;

  if ((root == NULL) || (availability_state == NULL)) {
    return 0;
  }

  steps = cJSON_CreateArray();
  if (steps == NULL) {
    return 0;
  }

  if (strcmp(availability_state, STRAPPY_TOOLS_AVAILABILITY_SCAN_NEEDED) == 0) {
    step = strappy_tools_create_database_manage_step(
      "Ask the user to open database management and scan for local SQLite "
      "databases.");
    if ((step == NULL) || !cJSON_AddItemToArray(steps, step)) {
      cJSON_Delete(step);
      cJSON_Delete(steps);
      return 0;
    }
  } else if (strcmp(availability_state,
                    STRAPPY_TOOLS_AVAILABILITY_WHITELIST_NEEDED) == 0) {
    step = strappy_tools_create_database_manage_step(
      "Ask the user to approve which discovered databases Strappy may use.");
    if ((step == NULL) || !cJSON_AddItemToArray(steps, step)) {
      cJSON_Delete(step);
      cJSON_Delete(steps);
      return 0;
    }
  }

  if (!cJSON_AddItemToObject(root, "recommended_next_steps", steps)) {
    cJSON_Delete(steps);
    return 0;
  }

  return 1;
}

static char *strappy_tools_execute_database_list(
  const char *session_db_path,
  const char *arguments_json,
  char **error_out)
{
  strappy_discovered_database_record_list list;
  strappy_tools_catalog_summary summary;
  cJSON *root;
  cJSON *databases;
  const char *availability_state;
  char *json;
  size_t index;
  int count;

  if ((session_db_path == NULL) || (session_db_path[0] == '\0')) {
    strappy_set_error(error_out, "Catalog database path is not configured.");
    return NULL;
  }

  if (!strappy_tools_validate_empty_arguments(arguments_json, error_out)) {
    return NULL;
  }

  strappy_discovered_database_record_list_init(&list);
  if (!strappy_db_list_discovered_databases(session_db_path,
                                           &list,
                                           error_out)) {
    return NULL;
  }

  root = cJSON_CreateObject();
  databases = cJSON_CreateArray();
  if ((root == NULL) || (databases == NULL)) {
    cJSON_Delete(root);
    cJSON_Delete(databases);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_set_error(error_out, "Could not allocate tool result.");
    return NULL;
  }

  count = 0;
  strappy_tools_catalog_summary_init(&summary);
  for (index = 0U; index < list.count; index++) {
    const strappy_discovered_database_record *record;

    record = &list.records[index];
    strappy_tools_catalog_summary_add_record(&summary, record);
    if (!strappy_tools_record_is_available_database(record)) {
      continue;
    }

    if (!strappy_tools_add_database_record(databases, record)) {
      cJSON_Delete(root);
      cJSON_Delete(databases);
      strappy_discovered_database_record_list_destroy(&list);
      strappy_set_error(error_out, "Could not build tool result.");
      return NULL;
    }

    count++;
  }
  availability_state = strappy_tools_availability_state(&summary);

  if (!cJSON_AddItemToObject(root, "databases", databases)) {
    cJSON_Delete(root);
    cJSON_Delete(databases);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_set_error(error_out, "Could not build tool result.");
    return NULL;
  }
  databases = NULL;

  if ((cJSON_AddNumberToObject(root, "count", (double)count) == NULL) ||
      (cJSON_AddStringToObject(root,
                               "availability_state",
                               availability_state) == NULL) ||
      !strappy_tools_add_catalog_summary(root, &summary) ||
      !strappy_tools_add_recommended_next_steps(root, availability_state)) {
    cJSON_Delete(root);
    strappy_discovered_database_record_list_destroy(&list);
    strappy_set_error(error_out, "Could not build tool result.");
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  strappy_discovered_database_record_list_destroy(&list);
  if (json == NULL) {
    strappy_set_error(error_out, "Could not serialize tool result.");
    return NULL;
  }

  return json;
}

char *strappy_tools_execute(const char *session_db_path,
                            const char *tool_name,
                            const char *arguments_json,
                            char **error_out)
{
  if ((tool_name == NULL) || (tool_name[0] == '\0')) {
    strappy_set_error(error_out, "Tool name is empty.");
    return NULL;
  }

  if (strcmp(tool_name, STRAPPY_TOOL_DATABASE_LIST) == 0) {
    return strappy_tools_execute_database_list(session_db_path,
                                              arguments_json,
                                              error_out);
  }

  strappy_set_formatted_error(error_out, "Tool is not registered: %s", tool_name);
  return NULL;
}
