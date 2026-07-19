#include "strappy_quality_policy.h"

#include "strappy_tools.h"

#include <stddef.h>
#include <string.h>

static const strappy_quality_check_definition
strappy_quality_check_definitions[] = {
  {
    "answer_non_empty",
    "answer_content",
    "Answer provided",
    NULL,
    STRAPPY_QUALITY_CHECK_ANSWER_NON_EMPTY
  },
  {
    "web_reference",
    "answer_content",
    "Source link included",
    NULL,
    STRAPPY_QUALITY_CHECK_WEB_REFERENCE
  },
  {
    "database_context_read",
    "required_tool",
    "Database context checked",
    STRAPPY_TOOL_DATABASE_CONTEXT_READ,
    STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
  },
  {
    "helper_session_name_write",
    "required_tool",
    "Session named",
    STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
    STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
  },
  {
    "helper_fontawesome_shortcode_confirm",
    "required_tool",
    "Font Awesome shortcode confirmed",
    STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
    STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
  },
  {
    "memory_user_fact_remember",
    "required_tool",
    "User memory considered",
    STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
    STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
  },
  {
    "memory_database_hint_remember",
    "required_tool",
    "Database memory considered",
    STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
    STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
  }
};

static const size_t strappy_quality_check_definition_count =
  sizeof(strappy_quality_check_definitions) /
  sizeof(strappy_quality_check_definitions[0]);

const strappy_quality_check_definition *strappy_quality_policy_find(
  const char *check_key)
{
  size_t index;

  if ((check_key == NULL) || (check_key[0] == '\0')) {
    return NULL;
  }
  for (index = 0U;
       index < strappy_quality_check_definition_count;
       index++) {
    if (strcmp(strappy_quality_check_definitions[index].check_key,
               check_key) == 0) {
      return &strappy_quality_check_definitions[index];
    }
  }
  return NULL;
}

size_t strappy_quality_policy_count(void)
{
  return strappy_quality_check_definition_count;
}

const strappy_quality_check_definition *strappy_quality_policy_at(
  size_t index)
{
  return (index < strappy_quality_check_definition_count) ?
    &strappy_quality_check_definitions[index] : NULL;
}
