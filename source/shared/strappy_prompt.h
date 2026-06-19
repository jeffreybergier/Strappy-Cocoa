#ifndef STRAPPY_PROMPT_H
#define STRAPPY_PROMPT_H

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_PROMPT_TEMPLATE_RESOURCE_NAME "PromptSystem"
#define STRAPPY_PROMPT_TEMPLATE_RESOURCE_TYPE "txt"
#define STRAPPY_PROMPT_TOOLS_PLACEHOLDER "{{STRAPPY_TOOLS}}"
#define STRAPPY_PROMPT_WEBVIEW_USER_AGENT_PLACEHOLDER \
  "{{STRAPPY_WEBVIEW_USER_AGENT}}"

char *strappy_prompt_render_system_prompt(const char *template_path,
                                          const char *webview_user_agent,
                                          char **error_out);
char *strappy_prompt_tools_fragment(char **error_out);

#ifdef __cplusplus
}
#endif

#endif
