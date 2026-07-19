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
    "REQUIRED: Return a non-whitespace, self-contained final answer that "
      "directly fulfills the user's request. Include the requested result "
      "itself; never merely say that it is ready or was provided earlier.",
    STRAPPY_QUALITY_CHECK_ANSWER_NON_EMPTY
  },
  {
    "web_reference",
    "answer_content",
    "Source link included",
    NULL,
    "CONDITIONAL: If web search or web fetch activity occurs, your final "
      "answer MUST include at least one titled inline Markdown HTTP or HTTPS "
      "link to a source you used. Otherwise this check is not applicable.",
    STRAPPY_QUALITY_CHECK_WEB_REFERENCE
  },
  {
    "database_context_read",
    "required_tool",
    "Database context checked",
    STRAPPY_TOOL_DATABASE_CONTEXT_READ,
    "CONDITIONAL: When the request depends on approved personal data, you "
      "MUST call database_context_read with a relevant approved database_id "
      "before database_query. Otherwise skip it; never fabricate an id.",
    STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
  },
  {
    "helper_session_name_write",
    "required_tool",
    "Session named",
    STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
    "REQUIRED: Before your final answer, helper_session_name_write MUST "
      "complete successfully with a short descriptive name for the user's "
      "latest prompt.",
    STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
  },
  {
    "helper_fontawesome_shortcode_confirm",
    "required_tool",
    "Font Awesome shortcode confirmed",
    STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
    "REQUIRED: Before your final answer, "
      "helper_fontawesome_shortcode_confirm MUST complete successfully with "
      "at least one valid shortcode, which you must use in the answer.",
    STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
  },
  {
    "memory_user_fact_remember",
    "required_tool",
    "User memory considered",
    STRAPPY_TOOL_MEMORY_USER_FACT_REMEMBER,
    "CONDITIONAL: If this request reveals a real, useful, durable user fact, "
      "store it with memory_user_fact_remember. Otherwise skip the tool; "
      "never invent a fact or store sensitive information.",
    STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
  },
  {
    "memory_database_hint_remember",
    "required_tool",
    "Database memory considered",
    STRAPPY_TOOL_MEMORY_DATABASE_HINT_REMEMBER,
    "CONDITIONAL: If this request reveals a real, useful, durable database "
      "hint, store it with memory_database_hint_remember. Otherwise skip the "
      "tool; never store private row values, guesses, or one-off results.",
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
