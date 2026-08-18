#include "strappy_provider.h"

#include "strappy_core.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int strappy_provider_ascii_contains_case_insensitive(
  const char *value,
  const char *needle)
{
  size_t value_length;
  size_t needle_length;
  size_t start;

  if ((value == NULL) || (needle == NULL) || (needle[0] == '\0')) {
    return 0;
  }
  value_length = strlen(value);
  needle_length = strlen(needle);
  if (needle_length > value_length) {
    return 0;
  }
  for (start = 0U; start <= (value_length - needle_length); start++) {
    size_t index;

    for (index = 0U; index < needle_length; index++) {
      if (tolower((unsigned char)value[start + index]) !=
          tolower((unsigned char)needle[index])) {
        break;
      }
    }
    if (index == needle_length) {
      return 1;
    }
  }
  return 0;
}

int strappy_provider_chatgpt_is_enabled(void)
{
#if STRAPPY_ENABLE_EXPERIMENTAL_CHATGPT
  const char *disabled;

  disabled = getenv("STRAPPY_DISABLE_EXPERIMENTAL_CHATGPT");
  return ((disabled == NULL) ||
          ((strcmp(disabled, "1") != 0) &&
           (strcmp(disabled, "true") != 0) &&
           (strcmp(disabled, "TRUE") != 0))) ? 1 : 0;
#else
  return 0;
#endif
}

static int strappy_provider_always_available(void)
{
  return 1;
}

static int strappy_provider_url_is_absolute_http(const char *endpoint)
{
  const char *authority;
  const char *cursor;

  if (endpoint == NULL) {
    return 0;
  }
  if (strncmp(endpoint, "https://", 8U) == 0) {
    authority = endpoint + 8U;
  } else if (strncmp(endpoint, "http://", 7U) == 0) {
    authority = endpoint + 7U;
  } else {
    return 0;
  }
  if ((*authority == '\0') || (*authority == '/') || (*authority == '?') ||
      (*authority == '#')) {
    return 0;
  }
  for (cursor = endpoint; *cursor != '\0'; cursor++) {
    if (((unsigned char)*cursor < 0x20U) ||
        ((unsigned char)*cursor == 0x7fU)) {
      return 0;
    }
  }
  return 1;
}

static int strappy_provider_validate_configurable_endpoint(
  const strappy_provider_definition *definition,
  const char *endpoint,
  int is_override,
  char **error_out)
{
  if ((definition == NULL) || !strappy_provider_url_is_absolute_http(endpoint)) {
    strappy_set_error(error_out,
                      "Provider endpoint must be an absolute HTTP or HTTPS URL.");
    return 0;
  }
  if (is_override && !definition->allows_endpoint_override) {
    strappy_set_error(error_out,
                      "The selected provider does not allow endpoint overrides.");
    return 0;
  }
  return 1;
}

static int strappy_provider_validate_fixed_endpoint(
  const strappy_provider_definition *definition,
  const char *endpoint,
  int is_override,
  char **error_out)
{
  if (is_override) {
    strappy_set_error(error_out,
                      "The selected provider has a fixed Responses endpoint.");
    return 0;
  }
  return strappy_provider_validate_configurable_endpoint(definition,
                                                          endpoint,
                                                          0,
                                                          error_out);
}

static int strappy_provider_no_hosted_tools(
  const strappy_provider_definition *definition,
  const char *tool_name)
{
  (void)definition;
  (void)tool_name;
  return 0;
}

static int strappy_provider_openrouter_hosted_tool(
  const strappy_provider_definition *definition,
  const char *tool_name)
{
  (void)definition;
  return (tool_name != NULL) &&
    ((strcmp(tool_name, "openrouter:web_search") == 0) ||
     (strcmp(tool_name, "openrouter:web_fetch") == 0));
}

static int strappy_provider_chatgpt_hosted_tool(
  const strappy_provider_definition *definition,
  const char *tool_name)
{
  (void)definition;
  return (tool_name != NULL) && (strcmp(tool_name, "web_search") == 0);
}

static int strappy_provider_never_plan_limit(long http_status,
                                             const char *error_type,
                                             const char *error_message)
{
  (void)http_status;
  (void)error_type;
  (void)error_message;
  return 0;
}

static int strappy_provider_chatgpt_plan_limit(long http_status,
                                               const char *error_type,
                                               const char *error_message)
{
  if (http_status == 429L) {
    return 1;
  }
  if ((error_type != NULL) &&
      ((strcmp(error_type, "usage_limit_reached") == 0) ||
       (strcmp(error_type, "usage_not_included") == 0) ||
       (strcmp(error_type, "rate_limit_exceeded") == 0) ||
       (strcmp(error_type, "insufficient_quota") == 0) ||
       (strcmp(error_type, "quota_exceeded") == 0) ||
       (strcmp(error_type, "plan_limit_reached") == 0))) {
    return 1;
  }
  return strappy_provider_ascii_contains_case_insensitive(error_message,
                                                           "usage limit") ||
    strappy_provider_ascii_contains_case_insensitive(error_message,
                                                      "not included") ||
    strappy_provider_ascii_contains_case_insensitive(error_message,
                                                      "insufficient quota") ||
    strappy_provider_ascii_contains_case_insensitive(error_message,
                                                      "out of budget");
}

static const strappy_provider_operations strappy_openrouter_operations = {
  strappy_provider_always_available,
  strappy_provider_validate_configurable_endpoint,
  strappy_provider_openrouter_hosted_tool,
  strappy_provider_never_plan_limit
};

static const strappy_provider_operations strappy_chatgpt_operations = {
  strappy_provider_chatgpt_is_enabled,
  strappy_provider_validate_fixed_endpoint,
  strappy_provider_chatgpt_hosted_tool,
  strappy_provider_chatgpt_plan_limit
};

static const strappy_provider_operations strappy_other_operations = {
  strappy_provider_always_available,
  strappy_provider_validate_configurable_endpoint,
  strappy_provider_no_hosted_tools,
  strappy_provider_never_plan_limit
};

static const strappy_provider_definition strappy_provider_registry[] = {
  {
    STRAPPY_PROVIDER_KIND_OPENROUTER,
    STRAPPY_PROVIDER_OPENROUTER,
    "OpenRouter",
    STRAPPY_PROVIDER_CREDENTIAL_API_TOKEN,
    STRAPPY_PROVIDER_OPENROUTER_RESPONSES_ENDPOINT,
    1,
    0,
    STRAPPY_PROVIDER_CATALOG_REMOTE_ACCOUNT,
    STRAPPY_PROVIDER_REQUEST_OPENROUTER,
    STRAPPY_PROVIDER_TRANSPORT_JSON,
    STRAPPY_PROVIDER_BILLING_METERED_API,
    1,
    &strappy_openrouter_operations
  },
  {
    STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT,
    STRAPPY_PROVIDER_OPENAI_CHATGPT,
    "ChatGPT (Codex)",
    STRAPPY_PROVIDER_CREDENTIAL_OAUTH_DEVICE,
    STRAPPY_PROVIDER_OPENAI_CHATGPT_RESPONSES_ENDPOINT,
    0,
    0,
    STRAPPY_PROVIDER_CATALOG_BUNDLED,
    STRAPPY_PROVIDER_REQUEST_CHATGPT_CODEX,
    STRAPPY_PROVIDER_TRANSPORT_SSE,
    STRAPPY_PROVIDER_BILLING_CHATGPT_PLAN,
    1,
    &strappy_chatgpt_operations
  },
  {
    STRAPPY_PROVIDER_KIND_OTHER,
    STRAPPY_PROVIDER_OTHER,
    "Other",
    STRAPPY_PROVIDER_CREDENTIAL_OPTIONAL_BEARER,
    NULL,
    1,
    1,
    STRAPPY_PROVIDER_CATALOG_MANUAL,
    STRAPPY_PROVIDER_REQUEST_GENERIC_RESPONSES,
    STRAPPY_PROVIDER_TRANSPORT_JSON,
    STRAPPY_PROVIDER_BILLING_UNKNOWN,
    1,
    &strappy_other_operations
  }
};

size_t strappy_provider_count(void)
{
  return sizeof(strappy_provider_registry) /
    sizeof(strappy_provider_registry[0]);
}

const strappy_provider_definition *strappy_provider_at(size_t index)
{
  return (index < strappy_provider_count()) ?
    &strappy_provider_registry[index] : NULL;
}

const strappy_provider_definition *strappy_provider_find(const char *provider_id)
{
  size_t index;

  if ((provider_id == NULL) || (provider_id[0] == '\0')) {
    return NULL;
  }
  for (index = 0U; index < strappy_provider_count(); index++) {
    if (strcmp(strappy_provider_registry[index].provider_id, provider_id) == 0) {
      return &strappy_provider_registry[index];
    }
  }
  return NULL;
}

const strappy_provider_definition *strappy_provider_for_kind(
  strappy_provider_kind provider)
{
  size_t index;

  for (index = 0U; index < strappy_provider_count(); index++) {
    if (strappy_provider_registry[index].kind == provider) {
      return &strappy_provider_registry[index];
    }
  }
  return NULL;
}

int strappy_provider_registry_is_complete(char **error_out)
{
  size_t index;
  size_t other_index;

  for (index = 0U; index < strappy_provider_count(); index++) {
    const strappy_provider_definition *definition;

    definition = &strappy_provider_registry[index];
    if ((definition->kind == STRAPPY_PROVIDER_KIND_UNKNOWN) ||
        (definition->provider_id == NULL) ||
        (definition->provider_id[0] == '\0') ||
        (definition->display_name == NULL) ||
        (definition->display_name[0] == '\0') ||
        (definition->credential_kind == 0) ||
        (definition->catalog_kind == 0) ||
        (definition->request_profile == 0) ||
        (definition->response_transport == 0) ||
        (definition->billing_kind == 0) ||
        (definition->operations == NULL) ||
        (definition->operations->is_available == NULL) ||
        (definition->operations->validate_endpoint == NULL) ||
        (definition->operations->supports_hosted_tool == NULL) ||
        (definition->operations->response_is_plan_limit == NULL) ||
        ((!definition->requires_endpoint_override) &&
         ((definition->default_responses_endpoint == NULL) ||
          (definition->default_responses_endpoint[0] == '\0')))) {
      strappy_set_error(error_out,
                        "Provider registry contains an incomplete definition.");
      return 0;
    }
    for (other_index = index + 1U;
         other_index < strappy_provider_count();
         other_index++) {
      if ((definition->kind == strappy_provider_registry[other_index].kind) ||
          (strcmp(definition->provider_id,
                  strappy_provider_registry[other_index].provider_id) == 0)) {
        strappy_set_error(error_out,
                          "Provider registry contains a duplicate definition.");
        return 0;
      }
    }
  }
  return 1;
}

const char *strappy_provider_name(strappy_provider_kind provider)
{
  const strappy_provider_definition *definition;

  definition = strappy_provider_for_kind(provider);
  return (definition != NULL) ? definition->provider_id : NULL;
}

int strappy_provider_parse(const char *name,
                           strappy_provider_kind *provider_out)
{
  const strappy_provider_definition *definition;

  if (provider_out == NULL) {
    return 0;
  }
  definition = strappy_provider_find(name);
  if (definition == NULL) {
    return 0;
  }
  *provider_out = definition->kind;
  return 1;
}

const char *strappy_provider_display_name(strappy_provider_kind provider)
{
  const strappy_provider_definition *definition;

  definition = strappy_provider_for_kind(provider);
  return (definition != NULL) ? definition->display_name : NULL;
}

int strappy_provider_is_available(const strappy_provider_definition *definition)
{
  return (definition != NULL) && (definition->operations != NULL) &&
    (definition->operations->is_available != NULL) &&
    definition->operations->is_available();
}

int strappy_provider_has_catalog_operation(
  const strappy_provider_definition *definition)
{
  return (definition != NULL) &&
    ((definition->catalog_kind == STRAPPY_PROVIDER_CATALOG_REMOTE_ACCOUNT) ||
     (definition->catalog_kind == STRAPPY_PROVIDER_CATALOG_BUNDLED));
}

int strappy_provider_supports_hosted_tool(
  const strappy_provider_definition *definition,
  const char *tool_name)
{
  return (definition != NULL) && (definition->operations != NULL) &&
    (definition->operations->supports_hosted_tool != NULL) &&
    definition->operations->supports_hosted_tool(definition, tool_name);
}

int strappy_provider_response_is_plan_limit(strappy_provider_kind provider,
                                            long http_status,
                                            const char *error_type,
                                            const char *error_message)
{
  const strappy_provider_definition *definition;

  definition = strappy_provider_for_kind(provider);
  return (definition != NULL) && (definition->operations != NULL) &&
    (definition->operations->response_is_plan_limit != NULL) &&
    definition->operations->response_is_plan_limit(http_status,
                                                   error_type,
                                                   error_message);
}

char *strappy_provider_model_identifier(const char *provider_account_id,
                                        const char *wire_model_id,
                                        char **error_out)
{
  size_t account_length;
  size_t wire_length;
  char *identifier;

  if ((provider_account_id == NULL) ||
      (provider_account_id[0] == '\0') ||
      (wire_model_id == NULL) || (wire_model_id[0] == '\0')) {
    strappy_set_error(error_out,
                      "Provider account and wire model id are required.");
    return NULL;
  }
  account_length = strlen(provider_account_id);
  wire_length = strlen(wire_model_id);
  if (account_length > (((size_t)-1) - wire_length - 2U)) {
    strappy_set_error(error_out, "Provider-qualified model id is too large.");
    return NULL;
  }
  identifier = (char *)malloc(account_length + wire_length + 2U);
  if (identifier == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate provider-qualified model id.");
    return NULL;
  }
  memcpy(identifier, provider_account_id, account_length);
  identifier[account_length] = ':';
  memcpy(identifier + account_length + 1U, wire_model_id, wire_length);
  identifier[account_length + wire_length + 1U] = '\0';
  return identifier;
}

char *strappy_provider_definition_responses_endpoint(
  const strappy_provider_definition *definition,
  const char *account_endpoint_override,
  char **error_out)
{
  const char *endpoint;
  int is_override;
  char *copy;

  if ((definition == NULL) || (definition->operations == NULL)) {
    strappy_set_error(error_out, "The selected provider is not registered.");
    return NULL;
  }
  is_override = (account_endpoint_override != NULL) &&
    (account_endpoint_override[0] != '\0');
  endpoint = is_override ? account_endpoint_override :
    definition->default_responses_endpoint;
  if ((endpoint == NULL) || (endpoint[0] == '\0')) {
    strappy_set_error(error_out,
                      "The selected provider account requires a Responses endpoint.");
    return NULL;
  }
  if (!definition->operations->validate_endpoint(definition,
                                                  endpoint,
                                                  is_override,
                                                  error_out)) {
    return NULL;
  }
  copy = strappy_string_duplicate(endpoint);
  if (copy == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate the provider account endpoint.");
  }
  return copy;
}

char *strappy_provider_responses_endpoint(
  strappy_provider_kind provider,
  const char *configured_openrouter_endpoint,
  char **error_out)
{
  const strappy_provider_definition *definition;
  const char *override;

  definition = strappy_provider_for_kind(provider);
  override = (definition != NULL) && definition->allows_endpoint_override ?
    configured_openrouter_endpoint : NULL;
  return strappy_provider_definition_responses_endpoint(definition,
                                                         override,
                                                         error_out);
}
