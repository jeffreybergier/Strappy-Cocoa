#ifndef STRAPPY_PLATFORM_PROFILE_H
#define STRAPPY_PLATFORM_PROFILE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *strappy_platform_default_assistant_set_id(void);
int strappy_platform_allows_assistant_set(const char *identifier);
int strappy_platform_allows_provider(const char *provider_id);
int strappy_platform_allows_tool(const char *tool_name);
const char * const *strappy_platform_tool_allowlist(size_t *count_out);

#ifdef __cplusplus
}
#endif

#endif
