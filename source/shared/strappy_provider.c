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

int strappy_provider_response_is_plan_limit(strappy_provider_kind provider,
                                            long http_status,
                                            const char *error_type,
                                            const char *error_message)
{
  if (provider != STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT) {
    return 0;
  }
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

const char *strappy_provider_name(strappy_provider_kind provider)
{
  switch (provider) {
    case STRAPPY_PROVIDER_KIND_OPENROUTER:
      return STRAPPY_PROVIDER_OPENROUTER;
    case STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT:
      return STRAPPY_PROVIDER_OPENAI_CHATGPT;
    case STRAPPY_PROVIDER_KIND_UNKNOWN:
      break;
  }
  return NULL;
}

int strappy_provider_parse(const char *name,
                           strappy_provider_kind *provider_out)
{
  strappy_provider_kind provider;

  if ((name == NULL) || (provider_out == NULL)) {
    return 0;
  }
  if (strcmp(name, STRAPPY_PROVIDER_OPENROUTER) == 0) {
    provider = STRAPPY_PROVIDER_KIND_OPENROUTER;
  } else if (strcmp(name, STRAPPY_PROVIDER_OPENAI_CHATGPT) == 0) {
    provider = STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT;
  } else {
    return 0;
  }
  *provider_out = provider;
  return 1;
}

const char *strappy_provider_display_name(strappy_provider_kind provider)
{
  switch (provider) {
    case STRAPPY_PROVIDER_KIND_OPENROUTER:
      return STRAPPY_PROVIDER_ACCOUNT_OPENROUTER_NAME;
    case STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT:
      return STRAPPY_PROVIDER_ACCOUNT_OPENAI_CHATGPT_NAME;
    case STRAPPY_PROVIDER_KIND_UNKNOWN:
      break;
  }
  return NULL;
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

char *strappy_provider_responses_endpoint(
  strappy_provider_kind provider,
  const char *configured_openrouter_endpoint,
  char **error_out)
{
  const char *endpoint;
  char *copy;

  endpoint = NULL;
  switch (provider) {
    case STRAPPY_PROVIDER_KIND_OPENROUTER:
      endpoint = configured_openrouter_endpoint;
      break;
    case STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT:
      endpoint = STRAPPY_PROVIDER_OPENAI_CHATGPT_RESPONSES_ENDPOINT;
      break;
    case STRAPPY_PROVIDER_KIND_UNKNOWN:
      break;
  }
  if ((endpoint == NULL) || (endpoint[0] == '\0')) {
    strappy_set_error(error_out,
                      "The selected model account has no Responses endpoint.");
    return NULL;
  }
  copy = strappy_string_duplicate(endpoint);
  if (copy == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate the model account endpoint.");
  }
  return copy;
}
