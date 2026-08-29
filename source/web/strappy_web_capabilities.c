#include "strappy_assistant_sets.h"
#include "strappy_core.h"
#include "strappy_platform_profile.h"
#include "strappy_provider.h"
#include "strappy_tools.h"

#include <emscripten/emscripten.h>
#include <stdlib.h>
#include <string.h>

static char *strappy_web_capabilities_tools_json = NULL;
static char *strappy_web_capabilities_error = NULL;

static void strappy_web_capabilities_clear(void)
{
  free(strappy_web_capabilities_tools_json);
  free(strappy_web_capabilities_error);
  strappy_web_capabilities_tools_json = NULL;
  strappy_web_capabilities_error = NULL;
}

static int strappy_web_capabilities_fail(const char *message)
{
  if (strappy_web_capabilities_error == NULL) {
    strappy_web_capabilities_error = strappy_string_duplicate(message);
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_capabilities_initialize(const char *resource_dir)
{
  strappy_assistant_set_record_list list;
  strappy_assistant_set_profile profile;
  strappy_assistant_set_profile rejected_profile;
  char *expected_error;

  strappy_web_capabilities_clear();
  strappy_assistant_set_record_list_init(&list);
  strappy_assistant_set_profile_init(&profile);
  strappy_assistant_set_profile_init(&rejected_profile);

  if (!strappy_assistant_sets_list(resource_dir,
                                   &list,
                                   &strappy_web_capabilities_error) ||
      (list.count != 1U) ||
      (list.records[0].identifier == NULL) ||
      (strcmp(list.records[0].identifier,
              STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE) != 0)) {
    strappy_assistant_set_record_list_destroy(&list);
    return strappy_web_capabilities_fail(
      "The web assistant-set catalog is not restricted to World Knowledge.");
  }
  strappy_assistant_set_record_list_destroy(&list);

  if (!strappy_assistant_sets_load_profile(resource_dir,
                                            NULL,
                                            &profile,
                                            &strappy_web_capabilities_error) ||
      (profile.identifier == NULL) ||
      (strcmp(profile.identifier,
              STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE) != 0)) {
    strappy_assistant_set_profile_destroy(&profile);
    return strappy_web_capabilities_fail(
      "The web default assistant set is not World Knowledge.");
  }

  expected_error = NULL;
  if (strappy_assistant_sets_load_profile(
        resource_dir,
        STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT,
        &rejected_profile,
        &expected_error)) {
    strappy_assistant_set_profile_destroy(&rejected_profile);
    strappy_assistant_set_profile_destroy(&profile);
    free(expected_error);
    return strappy_web_capabilities_fail(
      "The web C boundary accepted an unsupported assistant set.");
  }
  free(expected_error);

  if ((strappy_provider_count() != 1U) ||
      (strappy_provider_find(STRAPPY_PROVIDER_OPENROUTER) == NULL) ||
      (strappy_provider_find(STRAPPY_PROVIDER_OPENAI_CHATGPT) != NULL) ||
      (strappy_provider_find(STRAPPY_PROVIDER_OTHER) != NULL)) {
    strappy_assistant_set_profile_destroy(&profile);
    return strappy_web_capabilities_fail(
      "The web C boundary is not restricted to OpenRouter.");
  }

  if (!strappy_tools_is_registered(STRAPPY_TOOL_MEMORY_READ) ||
      !strappy_tools_is_registered(STRAPPY_TOOL_SKILLS_LIST) ||
      !strappy_tools_is_registered(STRAPPY_TOOL_SESSION_RENAME) ||
      !strappy_tools_is_registered(STRAPPY_TOOL_DATETIME_TO_ISO8601) ||
      !strappy_tools_is_registered(STRAPPY_TOOL_FONTAWESOME_CONFIRM) ||
      !strappy_tools_is_registered(STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) ||
      strappy_tools_is_registered(STRAPPY_TOOL_DATABASE_LIST) ||
      strappy_tools_is_registered(STRAPPY_TOOL_BASH) ||
      strappy_tools_is_registered(STRAPPY_TOOL_FILE_READ) ||
      strappy_tools_is_registered(STRAPPY_TOOL_DATABASE_STUDY) ||
      strappy_tools_is_registered(STRAPPY_TOOL_WEB_SEARCH)) {
    strappy_assistant_set_profile_destroy(&profile);
    return strappy_web_capabilities_fail(
      "The web tool registry does not match the browser-safe profile.");
  }

  strappy_web_capabilities_tools_json =
    strappy_tools_responses_request_json_filtered_for_provider(
      resource_dir,
      (const char * const *)profile.tool_names,
      profile.tool_name_count,
      STRAPPY_PROVIDER_KIND_OPENROUTER,
      STRAPPY_WEB_PROVIDER_NATIVE,
      &strappy_web_capabilities_error);
  strappy_assistant_set_profile_destroy(&profile);
  return (strappy_web_capabilities_tools_json != NULL) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
const char *strappy_web_capabilities_default_assistant_set(void)
{
  return strappy_platform_default_assistant_set_id();
}

EMSCRIPTEN_KEEPALIVE
const char *strappy_web_capabilities_tools(void)
{
  return (strappy_web_capabilities_tools_json != NULL) ?
    strappy_web_capabilities_tools_json : "";
}

EMSCRIPTEN_KEEPALIVE
const char *strappy_web_capabilities_last_error(void)
{
  return (strappy_web_capabilities_error != NULL) ?
    strappy_web_capabilities_error : "";
}
