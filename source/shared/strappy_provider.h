#ifndef STRAPPY_PROVIDER_H
#define STRAPPY_PROVIDER_H

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_PROVIDER_OPENROUTER "openrouter"
#define STRAPPY_PROVIDER_OPENAI_CHATGPT "openai_chatgpt"

/* These are internal account buckets, not identifiers copied from an OAuth
 * token or an upstream account. They own catalogs, preferences, and routing. */
#define STRAPPY_PROVIDER_ACCOUNT_OPENROUTER "openrouter"
#define STRAPPY_PROVIDER_ACCOUNT_OPENAI_CHATGPT "openai_chatgpt"

#define STRAPPY_PROVIDER_ACCOUNT_OPENROUTER_NAME "OpenRouter"
#define STRAPPY_PROVIDER_ACCOUNT_OPENAI_CHATGPT_NAME "ChatGPT (Codex)"

#define STRAPPY_PROVIDER_OPENAI_CHATGPT_RESPONSES_ENDPOINT \
  "https://chatgpt.com/backend-api/codex/responses"

#ifndef STRAPPY_ENABLE_EXPERIMENTAL_CHATGPT
#define STRAPPY_ENABLE_EXPERIMENTAL_CHATGPT 1
#endif

typedef enum strappy_provider_kind {
  STRAPPY_PROVIDER_KIND_UNKNOWN = 0,
  STRAPPY_PROVIDER_KIND_OPENROUTER = 1,
  STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT = 2
} strappy_provider_kind;

const char *strappy_provider_name(strappy_provider_kind provider);
int strappy_provider_parse(const char *name,
                           strappy_provider_kind *provider_out);
const char *strappy_provider_display_name(strappy_provider_kind provider);
int strappy_provider_chatgpt_is_enabled(void);
int strappy_provider_response_is_plan_limit(strappy_provider_kind provider,
                                            long http_status,
                                            const char *error_type,
                                            const char *error_message);

char *strappy_provider_model_identifier(const char *provider_account_id,
                                        const char *wire_model_id,
                                        char **error_out);

/* Resolve only adapter-owned endpoint policy. OpenRouter remains user
 * configurable; ChatGPT is fixed. No model row may supply an endpoint. */
char *strappy_provider_responses_endpoint(
  strappy_provider_kind provider,
  const char *configured_openrouter_endpoint,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
