#include "strappy_platform_profile.h"

#include "strappy_assistant_sets.h"
#include "strappy_provider.h"
#include "strappy_tools.h"

#include <string.h>

#if defined(STRAPPY_PLATFORM_WEB)
static const char * const strappy_web_tool_allowlist[] = {
  STRAPPY_TOOL_OPENROUTER_WEB_SEARCH,
  STRAPPY_TOOL_OPENROUTER_WEB_FETCH,
  STRAPPY_TOOL_DATETIME_TO_ISO8601,
  STRAPPY_TOOL_DATETIME_FROM_ISO8601,
  STRAPPY_TOOL_FONTAWESOME_SEARCH,
  STRAPPY_TOOL_FONTAWESOME_CONFIRM,
  STRAPPY_TOOL_MEMORY_READ,
  STRAPPY_TOOL_MEMORY_SAVE,
  STRAPPY_TOOL_MEMORY_DELETE,
  STRAPPY_TOOL_SKILLS_LIST,
  STRAPPY_TOOL_SKILL_READ,
  STRAPPY_TOOL_SESSION_RENAME
};
#endif

const char *strappy_platform_default_assistant_set_id(void)
{
#if defined(STRAPPY_PLATFORM_WEB)
  return STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE;
#else
  return STRAPPY_ASSISTANT_SET_DEFAULT;
#endif
}

int strappy_platform_allows_assistant_set(const char *identifier)
{
  if ((identifier == NULL) || (identifier[0] == '\0')) {
    return 0;
  }
#if defined(STRAPPY_PLATFORM_WEB)
  return strcmp(identifier, STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE) == 0;
#else
  return 1;
#endif
}

int strappy_platform_allows_provider(const char *provider_id)
{
  if ((provider_id == NULL) || (provider_id[0] == '\0')) {
    return 0;
  }
#if defined(STRAPPY_PLATFORM_WEB)
  return strcmp(provider_id, STRAPPY_PROVIDER_OPENROUTER) == 0;
#else
  return 1;
#endif
}

int strappy_platform_allows_tool(const char *tool_name)
{
  size_t count;
  size_t index;
  const char * const *allowlist;

  if ((tool_name == NULL) || (tool_name[0] == '\0')) {
    return 0;
  }
  allowlist = strappy_platform_tool_allowlist(&count);
  if (allowlist == NULL) {
    return 1;
  }
  for (index = 0U; index < count; index++) {
    if (strcmp(allowlist[index], tool_name) == 0) {
      return 1;
    }
  }
  return 0;
}

const char * const *strappy_platform_tool_allowlist(size_t *count_out)
{
#if defined(STRAPPY_PLATFORM_WEB)
  if (count_out != NULL) {
    *count_out = sizeof(strappy_web_tool_allowlist) /
      sizeof(strappy_web_tool_allowlist[0]);
  }
  return strappy_web_tool_allowlist;
#else
  if (count_out != NULL) {
    *count_out = 0U;
  }
  return NULL;
#endif
}
