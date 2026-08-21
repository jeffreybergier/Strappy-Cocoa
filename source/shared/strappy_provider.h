#ifndef STRAPPY_PROVIDER_H
#define STRAPPY_PROVIDER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_PROVIDER_OPENROUTER "openrouter"
#define STRAPPY_PROVIDER_OPENAI_CHATGPT "openai_chatgpt"
#define STRAPPY_PROVIDER_OTHER "other"

/* Designated-account compatibility only. These opaque ids preserve the
 * current UI while generic database APIs accept any account instance.
 * Provider behavior must never infer a provider from either id. */
#define STRAPPY_PROVIDER_ACCOUNT_OPENROUTER \
  "acct_4f70656e526f75746572000000000001"
#define STRAPPY_PROVIDER_ACCOUNT_OPENAI_CHATGPT \
  "acct_43686174475054000000000000000001"

#define STRAPPY_PROVIDER_ACCOUNT_OPENROUTER_NAME "OpenRouter"
#define STRAPPY_PROVIDER_ACCOUNT_OPENAI_CHATGPT_NAME "ChatGPT"

#define STRAPPY_PROVIDER_OPENROUTER_RESPONSES_ENDPOINT \
  "https://openrouter.ai/api/v1/responses"
#define STRAPPY_PROVIDER_OPENAI_CHATGPT_RESPONSES_ENDPOINT \
  "https://chatgpt.com/backend-api/codex/responses"

#ifndef STRAPPY_ENABLE_EXPERIMENTAL_CHATGPT
#define STRAPPY_ENABLE_EXPERIMENTAL_CHATGPT 1
#endif

typedef enum strappy_provider_kind {
  STRAPPY_PROVIDER_KIND_UNKNOWN = 0,
  STRAPPY_PROVIDER_KIND_OPENROUTER = 1,
  STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT = 2,
  STRAPPY_PROVIDER_KIND_OTHER = 3
} strappy_provider_kind;

typedef enum strappy_provider_credential_kind {
  STRAPPY_PROVIDER_CREDENTIAL_API_TOKEN = 1,
  STRAPPY_PROVIDER_CREDENTIAL_OAUTH_DEVICE = 2,
  STRAPPY_PROVIDER_CREDENTIAL_OPTIONAL_BEARER = 3
} strappy_provider_credential_kind;

typedef enum strappy_provider_catalog_kind {
  STRAPPY_PROVIDER_CATALOG_REMOTE_ACCOUNT = 1,
  STRAPPY_PROVIDER_CATALOG_BUNDLED = 2,
  STRAPPY_PROVIDER_CATALOG_MANUAL = 3
} strappy_provider_catalog_kind;

typedef enum strappy_provider_billing_kind {
  STRAPPY_PROVIDER_BILLING_METERED_API = 1,
  STRAPPY_PROVIDER_BILLING_CHATGPT_PLAN = 2,
  STRAPPY_PROVIDER_BILLING_UNKNOWN = 3
} strappy_provider_billing_kind;

typedef enum strappy_provider_request_profile {
  STRAPPY_PROVIDER_REQUEST_OPENROUTER = 1,
  STRAPPY_PROVIDER_REQUEST_CHATGPT_CODEX = 2,
  STRAPPY_PROVIDER_REQUEST_GENERIC_RESPONSES = 3
} strappy_provider_request_profile;

typedef enum strappy_provider_response_transport {
  STRAPPY_PROVIDER_TRANSPORT_JSON = 1,
  STRAPPY_PROVIDER_TRANSPORT_SSE = 2
} strappy_provider_response_transport;

typedef struct strappy_provider_definition strappy_provider_definition;

typedef struct strappy_provider_operations {
  int (*is_available)(void);
  int (*validate_endpoint)(const strappy_provider_definition *definition,
                           const char *endpoint,
                           int is_override,
                           char **error_out);
  int (*supports_hosted_tool)(const strappy_provider_definition *definition,
                              const char *tool_name);
  int (*response_is_plan_limit)(long http_status,
                                const char *error_type,
                                const char *error_message);
} strappy_provider_operations;

/* Immutable, code-owned provider policy. Runtime accounts refer to this by
 * provider_id; none of these fields contain an account id. */
struct strappy_provider_definition {
  strappy_provider_kind kind;
  const char *provider_id;
  const char *display_name;
  strappy_provider_credential_kind credential_kind;
  const char *default_responses_endpoint;
  int allows_endpoint_override;
  int requires_endpoint_override;
  strappy_provider_catalog_kind catalog_kind;
  strappy_provider_request_profile request_profile;
  strappy_provider_response_transport response_transport;
  strappy_provider_billing_kind billing_kind;
  int supports_local_functions;
  const strappy_provider_operations *operations;
};

size_t strappy_provider_count(void);
const strappy_provider_definition *strappy_provider_at(size_t index);
const strappy_provider_definition *strappy_provider_find(const char *provider_id);
const strappy_provider_definition *strappy_provider_for_kind(
  strappy_provider_kind provider);
int strappy_provider_registry_is_complete(char **error_out);

const char *strappy_provider_name(strappy_provider_kind provider);
int strappy_provider_parse(const char *name,
                           strappy_provider_kind *provider_out);
const char *strappy_provider_display_name(strappy_provider_kind provider);
int strappy_provider_is_available(const strappy_provider_definition *definition);
int strappy_provider_chatgpt_is_enabled(void);
int strappy_provider_has_catalog_operation(
  const strappy_provider_definition *definition);
int strappy_provider_supports_hosted_tool(
  const strappy_provider_definition *definition,
  const char *tool_name);
int strappy_provider_response_is_plan_limit(strappy_provider_kind provider,
                                            long http_status,
                                            const char *error_type,
                                            const char *error_message);

char *strappy_provider_model_identifier(const char *provider_id,
                                        const char *wire_model_id,
                                        char **error_out);

char *strappy_provider_definition_responses_endpoint(
  const strappy_provider_definition *definition,
  const char *account_endpoint_override,
  char **error_out);

/* Compatibility entry point for existing callers. */
char *strappy_provider_responses_endpoint(
  strappy_provider_kind provider,
  const char *configured_openrouter_endpoint,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
