#include "strappy_assistant_sets.h"

#include "strappy_core.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strappy_assistant_set_record_init(
  strappy_assistant_set_record *record)
{
  if (record == NULL) {
    return;
  }
  record->identifier = NULL;
  record->display_name = NULL;
  record->detail = NULL;
  record->availability = NULL;
}

static void strappy_assistant_set_record_destroy(
  strappy_assistant_set_record *record)
{
  if (record == NULL) {
    return;
  }
  free(record->identifier);
  free(record->display_name);
  free(record->detail);
  free(record->availability);
  strappy_assistant_set_record_init(record);
}

void strappy_assistant_set_record_list_init(
  strappy_assistant_set_record_list *list)
{
  if (list == NULL) {
    return;
  }
  list->records = NULL;
  list->count = 0U;
}

void strappy_assistant_set_record_list_destroy(
  strappy_assistant_set_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }
  for (index = 0U; index < list->count; index++) {
    strappy_assistant_set_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_assistant_set_record_list_init(list);
}

void strappy_assistant_set_profile_init(strappy_assistant_set_profile *profile)
{
  if (profile == NULL) {
    return;
  }
  memset(profile, 0, sizeof(*profile));
}

static void strappy_assistant_sets_destroy_strings(char **values, size_t count)
{
  size_t index;

  for (index = 0U; index < count; index++) {
    free(values[index]);
  }
  free(values);
}

void strappy_assistant_set_profile_destroy(
  strappy_assistant_set_profile *profile)
{
  if (profile == NULL) {
    return;
  }
  free(profile->identifier);
  free(profile->display_name);
  free(profile->detail);
  free(profile->availability);
  free(profile->prompt_resource);
  strappy_assistant_sets_destroy_strings(profile->tool_names,
                                         profile->tool_name_count);
  strappy_assistant_sets_destroy_strings(profile->preflight_tool_names,
                                         profile->preflight_tool_name_count);
  strappy_assistant_sets_destroy_strings(profile->quality_check_keys,
                                         profile->quality_check_key_count);
  strappy_assistant_set_profile_init(profile);
}

static char *strappy_assistant_sets_resource_path(const char *resource_dir,
                                                  char **error_out)
{
  size_t directory_length;
  size_t resource_length;
  int needs_slash;
  char *path;

  if ((resource_dir == NULL) || (resource_dir[0] == '\0')) {
    strappy_set_error(error_out,
                      "Assistant-set resource directory is not configured.");
    return NULL;
  }
  directory_length = strlen(resource_dir);
  resource_length = strlen(STRAPPY_ASSISTANT_SETS_RESOURCE_NAME);
  needs_slash = (resource_dir[directory_length - 1U] == '/') ? 0 : 1;
  if (directory_length > ((size_t)-1) - resource_length -
      (size_t)needs_slash - 1U) {
    strappy_set_error(error_out, "Assistant-set resource path is too large.");
    return NULL;
  }
  path = (char *)malloc(directory_length + (size_t)needs_slash +
                        resource_length + 1U);
  if (path == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate assistant-set resource path.");
    return NULL;
  }
  memcpy(path, resource_dir, directory_length);
  if (needs_slash) {
    path[directory_length++] = '/';
  }
  memcpy(path + directory_length,
         STRAPPY_ASSISTANT_SETS_RESOURCE_NAME,
         resource_length + 1U);
  return path;
}

static cJSON *strappy_assistant_sets_read_root(const char *resource_dir,
                                               char **error_out)
{
  char *path;
  FILE *file;
  long file_length;
  size_t length;
  char *buffer;
  cJSON *root;

  path = strappy_assistant_sets_resource_path(resource_dir, error_out);
  if (path == NULL) {
    return NULL;
  }
  file = fopen(path, "rb");
  if (file == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not open assistant-set resource: %s",
                                path);
    free(path);
    return NULL;
  }
  if ((fseek(file, 0L, SEEK_END) != 0) ||
      ((file_length = ftell(file)) < 0L) ||
      (fseek(file, 0L, SEEK_SET) != 0)) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not read assistant-set resource: %s",
                                path);
    free(path);
    return NULL;
  }
  free(path);
  if ((unsigned long)file_length >= (unsigned long)((size_t)-1)) {
    fclose(file);
    strappy_set_error(error_out, "Assistant-set resource is too large.");
    return NULL;
  }
  length = (size_t)file_length;
  buffer = (char *)malloc(length + 1U);
  if (buffer == NULL) {
    fclose(file);
    strappy_set_error(error_out,
                      "Could not allocate assistant-set resource.");
    return NULL;
  }
  if ((fread(buffer, 1U, length, file) != length) || ferror(file)) {
    free(buffer);
    fclose(file);
    strappy_set_error(error_out, "Could not read assistant-set resource.");
    return NULL;
  }
  buffer[length] = '\0';
  fclose(file);
  root = cJSON_Parse(buffer);
  free(buffer);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "Assistant-set resource root must be an object.");
    return NULL;
  }
  return root;
}

static const char *strappy_assistant_sets_required_string(cJSON *object,
                                                          const char *key,
                                                          char **error_out)
{
  cJSON *value;

  value = cJSON_IsObject(object) ?
    cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
  if ((value == NULL) || !cJSON_IsString(value) ||
      (value->valuestring == NULL) ||
      (value->valuestring[0] == '\0')) {
    strappy_set_formatted_error(error_out,
                                "Assistant set is missing %s.",
                                key);
    return NULL;
  }
  return value->valuestring;
}

static int strappy_assistant_sets_copy_string(char **target,
                                              const char *value,
                                              char **error_out)
{
  *target = strappy_string_duplicate(value);
  if (*target == NULL) {
    strappy_set_error(error_out, "Could not allocate assistant-set field.");
    return 0;
  }
  return 1;
}

static int strappy_assistant_sets_availability_is_valid(const char *value)
{
  return (value != NULL) &&
    ((strcmp(value, STRAPPY_ASSISTANT_SET_AVAILABILITY_AVAILABLE) == 0) ||
     (strcmp(value, STRAPPY_ASSISTANT_SET_AVAILABILITY_COMING_SOON) == 0));
}

static int strappy_assistant_sets_resource_name_is_safe(const char *value)
{
  return (value != NULL) && (value[0] != '\0') &&
    (strchr(value, '/') == NULL) && (strchr(value, '\\') == NULL) &&
    (strstr(value, "..") == NULL);
}

static int strappy_assistant_sets_append_unique(char ***values,
                                                size_t *count,
                                                const char *value,
                                                char **error_out)
{
  size_t index;
  char **next;

  for (index = 0U; index < *count; index++) {
    if (strcmp((*values)[index], value) == 0) {
      return 1;
    }
  }
  if (*count >= (((size_t)-1) / sizeof(char *))) {
    strappy_set_error(error_out, "Assistant-set name list is too large.");
    return 0;
  }
  next = (char **)realloc(*values, (*count + 1U) * sizeof(char *));
  if (next == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate assistant-set name list.");
    return 0;
  }
  *values = next;
  (*values)[*count] = strappy_string_duplicate(value);
  if ((*values)[*count] == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate assistant-set name.");
    return 0;
  }
  (*count)++;
  return 1;
}

static int strappy_assistant_sets_append_array(cJSON *object,
                                               const char *key,
                                               char ***values,
                                               size_t *count,
                                               char **error_out)
{
  cJSON *array;
  cJSON *item;

  array = cJSON_IsObject(object) ?
    cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
  if ((array == NULL) || !cJSON_IsArray(array)) {
    strappy_set_formatted_error(error_out,
                                "Assistant-set %s must be an array.",
                                key);
    return 0;
  }
  for (item = array->child; item != NULL; item = item->next) {
    if (!cJSON_IsString(item) || (item->valuestring == NULL) ||
        (item->valuestring[0] == '\0')) {
      strappy_set_formatted_error(error_out,
                                  "Assistant-set %s contains an invalid name.",
                                  key);
      return 0;
    }
    if (!strappy_assistant_sets_append_unique(values,
                                              count,
                                              item->valuestring,
                                              error_out)) {
      return 0;
    }
  }
  return 1;
}

static cJSON *strappy_assistant_sets_find(cJSON *root,
                                          const char *identifier,
                                          char **error_out)
{
  cJSON *sets;
  cJSON *set;

  sets = cJSON_GetObjectItemCaseSensitive(root, "sets");
  if (!cJSON_IsArray(sets)) {
    strappy_set_error(error_out, "Assistant-set list must be an array.");
    return NULL;
  }
  for (set = sets->child; set != NULL; set = set->next) {
    cJSON *value;

    value = cJSON_IsObject(set) ?
      cJSON_GetObjectItemCaseSensitive(set, "id") : NULL;
    if ((value != NULL) && cJSON_IsString(value) &&
        (value->valuestring != NULL) &&
        (strcmp(value->valuestring, identifier) == 0)) {
      return set;
    }
  }
  strappy_set_formatted_error(error_out,
                              "Assistant set is not registered: %s",
                              identifier);
  return NULL;
}

static int strappy_assistant_sets_fill_record(
  cJSON *set,
  strappy_assistant_set_record *record,
  char **error_out)
{
  const char *identifier;
  const char *display_name;
  const char *detail;
  const char *availability;

  identifier = strappy_assistant_sets_required_string(set, "id", error_out);
  display_name = strappy_assistant_sets_required_string(set,
                                                        "display_name",
                                                        error_out);
  detail = strappy_assistant_sets_required_string(set, "detail", error_out);
  availability = strappy_assistant_sets_required_string(set,
                                                        "availability",
                                                        error_out);
  if ((identifier == NULL) || (display_name == NULL) || (detail == NULL) ||
      (availability == NULL)) {
    return 0;
  }
  if (!strappy_assistant_sets_availability_is_valid(availability)) {
    strappy_set_formatted_error(error_out,
                                "Assistant set has invalid availability: %s",
                                identifier);
    return 0;
  }
  return strappy_assistant_sets_copy_string(&record->identifier,
                                            identifier,
                                            error_out) &&
    strappy_assistant_sets_copy_string(&record->display_name,
                                       display_name,
                                       error_out) &&
    strappy_assistant_sets_copy_string(&record->detail,
                                       detail,
                                       error_out) &&
    strappy_assistant_sets_copy_string(&record->availability,
                                       availability,
                                       error_out);
}

int strappy_assistant_sets_list(const char *resource_dir,
                                strappy_assistant_set_record_list *list,
                                char **error_out)
{
  cJSON *root;
  cJSON *sets;
  cJSON *set;

  if (list == NULL) {
    strappy_set_error(error_out, "Assistant-set list output is missing.");
    return 0;
  }
  strappy_assistant_set_record_list_init(list);
  root = strappy_assistant_sets_read_root(resource_dir, error_out);
  if (root == NULL) {
    return 0;
  }
  sets = cJSON_GetObjectItemCaseSensitive(root, "sets");
  if (!cJSON_IsArray(sets)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Assistant-set list must be an array.");
    return 0;
  }
  for (set = sets->child; set != NULL; set = set->next) {
    strappy_assistant_set_record *next;

    if (list->count >= (((size_t)-1) /
                        sizeof(strappy_assistant_set_record))) {
      strappy_set_error(error_out, "Assistant-set list is too large.");
      cJSON_Delete(root);
      strappy_assistant_set_record_list_destroy(list);
      return 0;
    }
    next = (strappy_assistant_set_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_assistant_set_record));
    if (next == NULL) {
      strappy_set_error(error_out, "Could not allocate assistant-set list.");
      cJSON_Delete(root);
      strappy_assistant_set_record_list_destroy(list);
      return 0;
    }
    list->records = next;
    strappy_assistant_set_record_init(&list->records[list->count]);
    if (!strappy_assistant_sets_fill_record(
          set,
          &list->records[list->count],
          error_out)) {
      cJSON_Delete(root);
      strappy_assistant_set_record_list_destroy(list);
      return 0;
    }
    list->count++;
  }
  cJSON_Delete(root);
  if (list->count == 0U) {
    strappy_set_error(error_out, "Assistant-set list is empty.");
    strappy_assistant_set_record_list_destroy(list);
    return 0;
  }
  return 1;
}

static int strappy_assistant_sets_array_contains(char **values,
                                                 size_t count,
                                                 const char *value)
{
  size_t index;

  if (value == NULL) {
    return 0;
  }
  for (index = 0U; index < count; index++) {
    if (strcmp(values[index], value) == 0) {
      return 1;
    }
  }
  return 0;
}

int strappy_assistant_sets_load_profile(
  const char *resource_dir,
  const char *identifier,
  strappy_assistant_set_profile *profile,
  char **error_out)
{
  cJSON *root;
  cJSON *universal;
  cJSON *set;
  cJSON *default_value;
  const char *selected_identifier;
  const char *display_name;
  const char *detail;
  const char *availability;
  const char *prompt_resource;
  size_t index;
  int ok;

  if (profile == NULL) {
    strappy_set_error(error_out, "Assistant-set profile output is missing.");
    return 0;
  }
  strappy_assistant_set_profile_init(profile);
  root = strappy_assistant_sets_read_root(resource_dir, error_out);
  if (root == NULL) {
    return 0;
  }
  default_value = cJSON_GetObjectItemCaseSensitive(root, "default_set_id");
  selected_identifier = identifier;
  if ((selected_identifier == NULL) || (selected_identifier[0] == '\0')) {
    selected_identifier = (cJSON_IsString(default_value) &&
                           (default_value->valuestring != NULL)) ?
      default_value->valuestring : STRAPPY_ASSISTANT_SET_DEFAULT;
  }
  universal = cJSON_GetObjectItemCaseSensitive(root, "universal");
  if (!cJSON_IsObject(universal)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "Assistant-set universal profile must be an object.");
    return 0;
  }
  set = strappy_assistant_sets_find(root, selected_identifier, error_out);
  if (set == NULL) {
    cJSON_Delete(root);
    return 0;
  }
  display_name = strappy_assistant_sets_required_string(set,
                                                        "display_name",
                                                        error_out);
  detail = strappy_assistant_sets_required_string(set, "detail", error_out);
  availability = strappy_assistant_sets_required_string(set,
                                                        "availability",
                                                        error_out);
  prompt_resource = strappy_assistant_sets_required_string(set,
                                                           "prompt_resource",
                                                           error_out);
  if ((display_name == NULL) || (detail == NULL) || (availability == NULL) ||
      (prompt_resource == NULL) ||
      !strappy_assistant_sets_availability_is_valid(availability) ||
      !strappy_assistant_sets_resource_name_is_safe(prompt_resource)) {
    if ((error_out == NULL) || (*error_out == NULL)) {
      strappy_set_error(error_out, "Assistant-set profile is invalid.");
    }
    cJSON_Delete(root);
    return 0;
  }

  ok = strappy_assistant_sets_copy_string(&profile->identifier,
                                          selected_identifier,
                                          error_out) &&
    strappy_assistant_sets_copy_string(&profile->display_name,
                                       display_name,
                                       error_out) &&
    strappy_assistant_sets_copy_string(&profile->detail,
                                       detail,
                                       error_out) &&
    strappy_assistant_sets_copy_string(&profile->availability,
                                       availability,
                                       error_out) &&
    strappy_assistant_sets_copy_string(&profile->prompt_resource,
                                       prompt_resource,
                                       error_out) &&
    strappy_assistant_sets_append_array(universal,
                                        "tools",
                                        &profile->tool_names,
                                        &profile->tool_name_count,
                                        error_out) &&
    strappy_assistant_sets_append_array(set,
                                        "additional_tools",
                                        &profile->tool_names,
                                        &profile->tool_name_count,
                                        error_out) &&
    strappy_assistant_sets_append_array(universal,
                                        "preflight_tools",
                                        &profile->preflight_tool_names,
                                        &profile->preflight_tool_name_count,
                                        error_out) &&
    strappy_assistant_sets_append_array(set,
                                        "additional_preflight_tools",
                                        &profile->preflight_tool_names,
                                        &profile->preflight_tool_name_count,
                                        error_out) &&
    strappy_assistant_sets_append_array(universal,
                                        "quality_checks",
                                        &profile->quality_check_keys,
                                        &profile->quality_check_key_count,
                                        error_out) &&
    strappy_assistant_sets_append_array(set,
                                        "additional_quality_checks",
                                        &profile->quality_check_keys,
                                        &profile->quality_check_key_count,
                                        error_out);
  cJSON_Delete(root);
  if (!ok) {
    strappy_assistant_set_profile_destroy(profile);
    return 0;
  }
  for (index = 0U; index < profile->preflight_tool_name_count; index++) {
    if (!strappy_assistant_sets_array_contains(
          profile->tool_names,
          profile->tool_name_count,
          profile->preflight_tool_names[index])) {
      strappy_set_formatted_error(
        error_out,
        "Assistant-set preflight tool is not allowed: %s",
        profile->preflight_tool_names[index]);
      strappy_assistant_set_profile_destroy(profile);
      return 0;
    }
  }
  return 1;
}

int strappy_assistant_set_profile_is_available(
  const strappy_assistant_set_profile *profile)
{
  return (profile != NULL) && (profile->availability != NULL) &&
    (strcmp(profile->availability,
            STRAPPY_ASSISTANT_SET_AVAILABILITY_AVAILABLE) == 0);
}

int strappy_assistant_set_profile_allows_tool(
  const strappy_assistant_set_profile *profile,
  const char *tool_name)
{
  return (profile != NULL) &&
    strappy_assistant_sets_array_contains(profile->tool_names,
                                          profile->tool_name_count,
                                          tool_name);
}

int strappy_assistant_set_profile_has_quality_check(
  const strappy_assistant_set_profile *profile,
  const char *check_key)
{
  return (profile != NULL) &&
    strappy_assistant_sets_array_contains(profile->quality_check_keys,
                                          profile->quality_check_key_count,
                                          check_key);
}
