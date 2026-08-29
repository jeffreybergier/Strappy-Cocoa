#include "strappy_db.h"

#include "strappy_core.h"
#include "strappy_platform_profile.h"
#include "strappy_provider.h"

#include "cJSON.h"
#include <emscripten/emscripten.h>
#include <stdlib.h>
#include <string.h>

#define STRAPPY_WEB_DATABASE_PATH "/strappy.sqlite"
#define STRAPPY_WEB_ACCOUNT_ID STRAPPY_PROVIDER_ACCOUNT_OPENROUTER

static char *strappy_web_database_last_error = NULL;
static long long strappy_web_database_last_created_session_id = 0LL;
static long strappy_web_database_last_count = 0L;
static const char *strappy_web_database_active_path = ":memory:";

void strappy_db_web_set_persistent_vfs_enabled(int enabled);

static void strappy_web_database_clear_error(void)
{
  free(strappy_web_database_last_error);
  strappy_web_database_last_error = NULL;
}

static int strappy_web_database_refresh_count(const char *db_path)
{
  strappy_session_record_list list;
  int ok;

  strappy_session_record_list_init(&list);
  ok = strappy_db_list_sessions(db_path,
                                &list,
                                &strappy_web_database_last_error);
  if (ok) {
    if (list.count > 2147483647U) {
      strappy_set_error(&strappy_web_database_last_error,
                        "The session list is too large.");
      ok = 0;
    } else {
      strappy_web_database_last_count = (long)list.count;
      strappy_web_database_last_created_session_id =
        (list.count > 0U) ? list.records[0].session_id : 0LL;
    }
  }
  strappy_session_record_list_destroy(&list);
  return ok;
}

static int strappy_web_database_enforce_assistant_set(const char *db_path)
{
  return (strappy_web_database_last_created_session_id <= 0LL) ||
    strappy_db_update_session_assistant_set(
      db_path,
      strappy_web_database_last_created_session_id,
      strappy_platform_default_assistant_set_id(),
      &strappy_web_database_last_error);
}

static int strappy_web_database_add_string(cJSON *object,
                                           const char *name,
                                           const char *value)
{
  return cJSON_AddStringToObject(object,
                                 name,
                                 (value != NULL) ? value : "") != NULL;
}

static cJSON *strappy_web_database_options_json(
  const strappy_session_options *options)
{
  cJSON *result;
  const char *web_provider;

  if (options == NULL) {
    return NULL;
  }
  web_provider = strappy_web_provider_name(options->web_provider);
  result = cJSON_CreateObject();
  if ((result == NULL) ||
      !strappy_web_database_add_string(result, "model_id", options->model_id) ||
      !strappy_web_database_add_string(result,
                                       "provider_account_id",
                                       options->provider_account_id) ||
      !strappy_web_database_add_string(result,
                                       "assistant_set_id",
                                       options->assistant_set_id) ||
      !strappy_web_database_add_string(result,
                                       "web_provider",
                                       web_provider) ||
      (cJSON_AddBoolToObject(result,
                             "web_search_enabled",
                             options->web_search_enabled) == NULL) ||
      (cJSON_AddBoolToObject(result,
                             "limit_to_one_tool",
                             options->limit_to_one_tool) == NULL) ||
      (cJSON_AddBoolToObject(result,
                             "answer_quality_enabled",
                             options->answer_quality_enabled) == NULL) ||
      (cJSON_AddNumberToObject(result,
                               "round_limit",
                               (double)options->round_limit) == NULL)) {
    cJSON_Delete(result);
    return NULL;
  }
  return result;
}

static cJSON *strappy_web_database_session_json(
  const strappy_session_record *record)
{
  cJSON *result;

  if (record == NULL) {
    return NULL;
  }
  result = cJSON_CreateObject();
  if ((result == NULL) ||
      (cJSON_AddNumberToObject(result,
                               "id",
                               (double)record->session_id) == NULL) ||
      !strappy_web_database_add_string(result, "name", record->name) ||
      !strappy_web_database_add_string(result,
                                       "model_name",
                                       record->model_name) ||
      !strappy_web_database_add_string(result,
                                       "last_activity_at",
                                       record->last_activity_at) ||
      !strappy_web_database_add_string(result,
                                       "assistant_set_id",
                                       record->assistant_set_id) ||
      !strappy_web_database_add_string(
        result,
        "web_provider",
        strappy_web_provider_name(record->web_provider)) ||
      (cJSON_AddBoolToObject(result,
                             "web_search_enabled",
                             record->web_search_enabled) == NULL) ||
      (cJSON_AddBoolToObject(result,
                             "limit_to_one_tool",
                             record->limit_to_one_tool) == NULL) ||
      (cJSON_AddBoolToObject(result,
                             "answer_quality_enabled",
                             record->answer_quality_enabled) == NULL) ||
      (cJSON_AddNumberToObject(result,
                               "round_limit",
                               (double)record->round_limit) == NULL)) {
    cJSON_Delete(result);
    return NULL;
  }
  return result;
}

static char *strappy_web_database_print_json(cJSON *value)
{
  char *json;

  if (value == NULL) {
    strappy_set_error(&strappy_web_database_last_error,
                      "Could not allocate browser database state.");
    return NULL;
  }
  json = cJSON_PrintUnformatted(value);
  cJSON_Delete(value);
  if (json == NULL) {
    strappy_set_error(&strappy_web_database_last_error,
                      "Could not serialize browser database state.");
  }
  return json;
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_database_initialize_temporary(void)
{
  strappy_web_database_clear_error();
  strappy_web_database_last_count = 0L;
  strappy_web_database_active_path = ":memory:";
  return strappy_db_initialize(strappy_web_database_active_path,
                               &strappy_web_database_last_error) &&
    strappy_db_restore_provider_account(
      strappy_web_database_active_path,
      STRAPPY_WEB_ACCOUNT_ID,
      STRAPPY_PROVIDER_OPENROUTER,
      "OpenRouter",
      STRAPPY_PROVIDER_OPENROUTER_RESPONSES_ENDPOINT,
      &strappy_web_database_last_error);
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_database_initialize_persistent(void)
{
  strappy_web_database_clear_error();
  strappy_web_database_active_path = STRAPPY_WEB_DATABASE_PATH;
  strappy_web_database_last_created_session_id = 0LL;
  strappy_web_database_last_count = 0L;
  if (!strappy_db_initialize(STRAPPY_WEB_DATABASE_PATH,
                             &strappy_web_database_last_error) ||
      !strappy_db_restore_provider_account(
        STRAPPY_WEB_DATABASE_PATH,
        STRAPPY_WEB_ACCOUNT_ID,
        STRAPPY_PROVIDER_OPENROUTER,
        "OpenRouter",
        STRAPPY_PROVIDER_OPENROUTER_RESPONSES_ENDPOINT,
        &strappy_web_database_last_error)) {
    return 0;
  }
  return strappy_web_database_refresh_count(STRAPPY_WEB_DATABASE_PATH) &&
    strappy_web_database_enforce_assistant_set(STRAPPY_WEB_DATABASE_PATH);
}

EMSCRIPTEN_KEEPALIVE
void strappy_web_database_enable_persistence(void)
{
  strappy_db_web_set_persistent_vfs_enabled(1);
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_database_create_session(void)
{
  strappy_web_database_clear_error();
  strappy_web_database_last_created_session_id = 0LL;
  if (!strappy_db_create_session_with_working_directory(
        strappy_web_database_active_path,
        "/",
        &strappy_web_database_last_created_session_id,
        &strappy_web_database_last_error)) {
    return 0;
  }
  return strappy_web_database_refresh_count(strappy_web_database_active_path) &&
    strappy_web_database_enforce_assistant_set(
      strappy_web_database_active_path);
}

EMSCRIPTEN_KEEPALIVE
char *strappy_web_database_list_sessions(void)
{
  strappy_session_record_list list;
  cJSON *array;
  size_t index;

  strappy_web_database_clear_error();
  strappy_session_record_list_init(&list);
  if (!strappy_db_list_sessions(strappy_web_database_active_path,
                                &list,
                                &strappy_web_database_last_error)) {
    return NULL;
  }
  array = cJSON_CreateArray();
  for (index = 0U; (array != NULL) && (index < list.count); index++) {
    cJSON *entry;

    entry = strappy_web_database_session_json(&list.records[index]);
    if ((entry == NULL) || !cJSON_AddItemToArray(array, entry)) {
      cJSON_Delete(entry);
      cJSON_Delete(array);
      array = NULL;
    }
  }
  strappy_session_record_list_destroy(&list);
  return strappy_web_database_print_json(array);
}

EMSCRIPTEN_KEEPALIVE
char *strappy_web_database_load_session_options(long long session_id)
{
  strappy_session_options options;
  cJSON *result;

  strappy_web_database_clear_error();
  strappy_session_options_init(&options);
  if (!strappy_db_load_session_options(strappy_web_database_active_path,
                                       session_id,
                                       &options,
                                       &strappy_web_database_last_error)) {
    return NULL;
  }
  result = strappy_web_database_options_json(&options);
  strappy_session_options_destroy(&options);
  return strappy_web_database_print_json(result);
}

EMSCRIPTEN_KEEPALIVE
char *strappy_web_database_load_default_options(void)
{
  strappy_session_options options;
  cJSON *result;

  strappy_web_database_clear_error();
  strappy_session_options_init(&options);
  if (!strappy_db_load_default_session_options(
        strappy_web_database_active_path,
        "/",
        &options,
        &strappy_web_database_last_error)) {
    return NULL;
  }
  result = strappy_web_database_options_json(&options);
  strappy_session_options_destroy(&options);
  return strappy_web_database_print_json(result);
}

static int strappy_web_database_update_options(
  long long session_id,
  int edits_defaults,
  const char *web_provider,
  int web_search_enabled,
  int limit_to_one_tool,
  int answer_quality_enabled,
  long round_limit)
{
  const strappy_session_option_mask changed_fields =
    STRAPPY_SESSION_OPTION_WEB_PROVIDER |
    STRAPPY_SESSION_OPTION_WEB_SEARCH |
    STRAPPY_SESSION_OPTION_LIMIT_TO_ONE_TOOL |
    STRAPPY_SESSION_OPTION_ANSWER_QUALITY |
    STRAPPY_SESSION_OPTION_ROUND_LIMIT;
  strappy_session_options requested;
  strappy_web_provider parsed_provider;
  int ok;

  strappy_web_database_clear_error();
  if ((web_provider == NULL) ||
      !strappy_web_provider_parse(web_provider, &parsed_provider) ||
      (round_limit < 1L) ||
      (round_limit > STRAPPY_SESSION_MAX_LIMIT)) {
    strappy_set_error(&strappy_web_database_last_error,
                      "Browser session options are invalid.");
    return 0;
  }
  strappy_session_options_init(&requested);
  requested.web_provider = parsed_provider;
  requested.web_search_enabled = web_search_enabled ? 1 : 0;
  requested.limit_to_one_tool = limit_to_one_tool ? 1 : 0;
  requested.answer_quality_enabled = answer_quality_enabled ? 1 : 0;
  requested.round_limit = round_limit;
  if (edits_defaults) {
    ok = strappy_db_update_default_session_options(
      strappy_web_database_active_path,
      "/",
      &requested,
      changed_fields,
      NULL,
      NULL,
      &strappy_web_database_last_error);
  } else {
    ok = strappy_db_update_session_options(
      strappy_web_database_active_path,
      session_id,
      &requested,
      changed_fields,
      NULL,
      NULL,
      &strappy_web_database_last_error);
  }
  strappy_session_options_destroy(&requested);
  return ok;
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_database_update_session_options(
  long long session_id,
  const char *web_provider,
  int web_search_enabled,
  int limit_to_one_tool,
  int answer_quality_enabled,
  long round_limit)
{
  return strappy_web_database_update_options(session_id,
                                             0,
                                             web_provider,
                                             web_search_enabled,
                                             limit_to_one_tool,
                                             answer_quality_enabled,
                                             round_limit);
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_database_update_default_options(
  const char *web_provider,
  int web_search_enabled,
  int limit_to_one_tool,
  int answer_quality_enabled,
  long round_limit)
{
  return strappy_web_database_update_options(0LL,
                                             1,
                                             web_provider,
                                             web_search_enabled,
                                             limit_to_one_tool,
                                             answer_quality_enabled,
                                             round_limit);
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_database_rename_session(long long session_id,
                                        const char *name)
{
  strappy_web_database_clear_error();
  if ((name == NULL) || (name[0] == '\0')) {
    strappy_set_error(&strappy_web_database_last_error,
                      "Enter a session name.");
    return 0;
  }
  return strappy_db_update_session_name(strappy_web_database_active_path,
                                        session_id,
                                        name,
                                        &strappy_web_database_last_error);
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_database_delete_session(long long session_id)
{
  strappy_web_database_clear_error();
  if (!strappy_db_delete_session(strappy_web_database_active_path,
                                 session_id,
                                 &strappy_web_database_last_error)) {
    return 0;
  }
  return strappy_web_database_refresh_count(strappy_web_database_active_path);
}

const char *strappy_web_database_path(void)
{
  return strappy_web_database_active_path;
}

EMSCRIPTEN_KEEPALIVE
long strappy_web_database_session_count(void)
{
  return strappy_web_database_last_count;
}

EMSCRIPTEN_KEEPALIVE
long long strappy_web_database_last_session_id(void)
{
  return strappy_web_database_last_created_session_id;
}

EMSCRIPTEN_KEEPALIVE
const char *strappy_web_database_error(void)
{
  return (strappy_web_database_last_error != NULL) ?
    strappy_web_database_last_error : "";
}
