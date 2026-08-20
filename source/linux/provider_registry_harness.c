#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shared/strappy_provider.h"

static int harness_definition_is_complete(
  const strappy_provider_definition *definition)
{
  return (definition != NULL) &&
    (definition->provider_id != NULL) &&
    (definition->display_name != NULL) &&
    (definition->operations != NULL) &&
    (definition->operations->is_available != NULL) &&
    (definition->operations->validate_endpoint != NULL) &&
    (definition->operations->supports_hosted_tool != NULL) &&
    (definition->operations->response_is_plan_limit != NULL);
}

int main(void)
{
  static const char *invalid_other_endpoints[] = {
    "relative/responses",
    "ftp://example.test/responses",
    "https:///responses",
    "https://example.test/line\nbreak"
  };
  const strappy_provider_definition *openrouter;
  const strappy_provider_definition *chatgpt;
  const strappy_provider_definition *other;
  strappy_provider_kind parsed;
  char *endpoint;
  char *error;
  size_t index;
  int ok;

  error = NULL;
  ok = (strappy_provider_count() == 3U) &&
    strappy_provider_registry_is_complete(&error);
  for (index = 0U; ok && (index < strappy_provider_count()); index++) {
    ok = harness_definition_is_complete(strappy_provider_at(index));
  }

  openrouter = strappy_provider_find(STRAPPY_PROVIDER_OPENROUTER);
  chatgpt = strappy_provider_find(STRAPPY_PROVIDER_OPENAI_CHATGPT);
  other = strappy_provider_find(STRAPPY_PROVIDER_OTHER);
  ok = ok && (openrouter != NULL) && (chatgpt != NULL) && (other != NULL) &&
    (strappy_provider_find("unknown") == NULL) &&
    (strappy_provider_for_kind(STRAPPY_PROVIDER_KIND_UNKNOWN) == NULL) &&
    !strappy_provider_parse("unknown", &parsed) &&
    strappy_provider_parse(STRAPPY_PROVIDER_OTHER, &parsed) &&
    (parsed == STRAPPY_PROVIDER_KIND_OTHER);

  ok = ok &&
    (openrouter->catalog_kind == STRAPPY_PROVIDER_CATALOG_REMOTE_ACCOUNT) &&
    strappy_provider_has_catalog_operation(openrouter) &&
    strappy_provider_supports_hosted_tool(openrouter,
                                         "openrouter:web_search") &&
    strappy_provider_supports_hosted_tool(openrouter,
                                         "openrouter:web_fetch") &&
    !strappy_provider_supports_hosted_tool(openrouter, "web_search") &&
    (chatgpt->catalog_kind == STRAPPY_PROVIDER_CATALOG_BUNDLED) &&
    strappy_provider_has_catalog_operation(chatgpt) &&
    strappy_provider_supports_hosted_tool(chatgpt, "web_search") &&
    !strappy_provider_supports_hosted_tool(chatgpt,
                                          "openrouter:web_search");

  /* The minimal provider is intentionally manual and generic. Its operation
   * table must advertise neither a catalog operation nor hosted tools. */
  ok = ok &&
    (other->credential_kind == STRAPPY_PROVIDER_CREDENTIAL_OPTIONAL_BEARER) &&
    (other->catalog_kind == STRAPPY_PROVIDER_CATALOG_MANUAL) &&
    (other->request_profile == STRAPPY_PROVIDER_REQUEST_GENERIC_RESPONSES) &&
    (other->response_transport == STRAPPY_PROVIDER_TRANSPORT_JSON) &&
    (other->billing_kind == STRAPPY_PROVIDER_BILLING_UNKNOWN) &&
    (other->default_responses_endpoint == NULL) &&
    other->requires_endpoint_override &&
    !strappy_provider_has_catalog_operation(other) &&
    !strappy_provider_supports_hosted_tool(other, "web_search") &&
    !strappy_provider_supports_hosted_tool(other,
                                          "openrouter:web_search");

  endpoint = strappy_provider_definition_responses_endpoint(
    other,
    "https://example.test/v1/responses",
    &error);
  ok = ok && (endpoint != NULL) &&
    (strcmp(endpoint, "https://example.test/v1/responses") == 0);
  free(endpoint);
  endpoint = strappy_provider_definition_responses_endpoint(other,
                                                             NULL,
                                                             &error);
  ok = ok && (endpoint == NULL) && (error != NULL);
  free(error);
  error = NULL;
  for (index = 0U;
       ok && (index < (sizeof(invalid_other_endpoints) /
                       sizeof(invalid_other_endpoints[0])));
       index++) {
    endpoint = strappy_provider_definition_responses_endpoint(
      other,
      invalid_other_endpoints[index],
      &error);
    ok = (endpoint == NULL) && (error != NULL) &&
      (strstr(error, invalid_other_endpoints[index]) == NULL);
    free(endpoint);
    free(error);
    error = NULL;
  }
  free(error);

  if (!ok) {
    fprintf(stderr, "Provider registry harness failed.\n");
    return 1;
  }
  printf("Provider registry harness passed.\n");
  return 0;
}
