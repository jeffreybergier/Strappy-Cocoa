#define _POSIX_C_SOURCE 200809L

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shared/strappy_client.h"
#include "../shared/strappy_config.h"
#include "../shared/strappy_core.h"
#include "../shared/strappy_provider.h"

#define PROBE_ENV_PATH_ENV "STRAPPY_OTHER_PROBE_ENV_PATH"
#define PROBE_MODEL_ENV "STRAPPY_OTHER_PROBE_MODEL"
#define PROBE_MAX_MODEL_BYTES 256U

static char *probe_request_json(const char *model,
                                strappy_provider_kind provider)
{
  cJSON *root;
  cJSON *input;
  cJSON *message;
  cJSON *content;
  cJSON *text;
  cJSON *tools;
  char *json;
  int ok;

  root = cJSON_CreateObject();
  input = cJSON_CreateArray();
  message = cJSON_CreateObject();
  content = cJSON_CreateArray();
  text = cJSON_CreateObject();
  tools = cJSON_CreateArray();
  ok = (root != NULL) && (input != NULL) && (message != NULL) &&
    (content != NULL) && (text != NULL) && (tools != NULL);
  ok = ok &&
    (cJSON_AddStringToObject(root, "model", model) != NULL) &&
    (cJSON_AddFalseToObject(root, "stream") != NULL) &&
    (cJSON_AddFalseToObject(root, "store") != NULL) &&
    (cJSON_AddStringToObject(root, "instructions",
                            "Return a concise plain-text answer.") != NULL) &&
    (cJSON_AddStringToObject(text, "type", "input_text") != NULL) &&
    (cJSON_AddStringToObject(text, "text", "Reply with exactly OK.") !=
     NULL);
  if (ok) {
    ok = cJSON_AddItemToArray(content, text);
    if (ok) {
      text = NULL;
    }
  }
  ok = ok &&
    (cJSON_AddStringToObject(message, "type", "message") != NULL) &&
    (cJSON_AddStringToObject(message, "role", "user") != NULL);
  if (ok) {
    ok = cJSON_AddItemToObject(message, "content", content);
    if (ok) {
      content = NULL;
    }
  }
  if (ok) {
    ok = cJSON_AddItemToArray(input, message);
    if (ok) {
      message = NULL;
    }
  }
  if (ok) {
    ok = cJSON_AddItemToObject(root, "input", input);
    if (ok) {
      input = NULL;
    }
  }
  if (ok) {
    ok = cJSON_AddItemToObject(root, "tools", tools);
    if (ok) {
      tools = NULL;
    }
  }
  if (!ok) {
    cJSON_Delete(text);
    cJSON_Delete(content);
    cJSON_Delete(message);
    cJSON_Delete(input);
    cJSON_Delete(tools);
    cJSON_Delete(root);
    return NULL;
  }
  if (provider == STRAPPY_PROVIDER_KIND_OPENROUTER) {
    cJSON *include;
    cJSON *metadata;

    include = cJSON_CreateArray();
    metadata = cJSON_CreateObject();
    ok = (include != NULL) && (metadata != NULL) &&
      (cJSON_AddStringToObject(
         root,"session_id","strappy-openrouter-compatibility") != NULL) &&
      (cJSON_AddStringToObject(
         metadata,"strappy_session_id","strappy-openrouter-compatibility") !=
       NULL) &&
      (cJSON_AddStringToObject(
         metadata,"strappy_prompt_group_key","compatibility") != NULL) &&
      (cJSON_AddStringToObject(metadata,"strappy_round","0") != NULL);
    if (ok) {
      ok = cJSON_AddItemToObject(root,"metadata",metadata);
      if (ok) {
        metadata = NULL;
      }
    }
    if (ok) {
      cJSON *include_item;

      include_item = cJSON_CreateString("reasoning.encrypted_content");
      ok = (include_item != NULL) &&
        cJSON_AddItemToArray(include,include_item);
      if (!ok) {
        cJSON_Delete(include_item);
      }
    }
    if (ok) {
      ok = cJSON_AddItemToObject(root,"include",include);
      if (ok) {
        include = NULL;
      }
    }
    ok = ok &&
      (cJSON_AddTrueToObject(root,"parallel_tool_calls") != NULL) &&
      (cJSON_AddStringToObject(root,"tool_choice","auto") != NULL);
    if (!ok) {
      cJSON_Delete(include);
      cJSON_Delete(metadata);
      cJSON_Delete(root);
      return NULL;
    }
  }
  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return json;
}

static int probe_response_is_completed_with_text(const char *response_json)
{
  cJSON *root;
  cJSON *status;
  cJSON *output;
  cJSON *item;
  int has_text;

  root = cJSON_Parse(response_json);
  status = cJSON_IsObject(root) ?
    cJSON_GetObjectItemCaseSensitive(root, "status") : NULL;
  output = cJSON_IsObject(root) ?
    cJSON_GetObjectItemCaseSensitive(root, "output") : NULL;
  has_text = 0;
  for (item = cJSON_IsArray(output) ? output->child : NULL;
       (item != NULL) && !has_text;
       item = item->next) {
    cJSON *content;
    cJSON *part;

    content = cJSON_GetObjectItemCaseSensitive(item, "content");
    for (part = cJSON_IsArray(content) ? content->child : NULL;
         part != NULL;
         part = part->next) {
      cJSON *type;
      cJSON *text;

      type = cJSON_GetObjectItemCaseSensitive(part, "type");
      text = cJSON_GetObjectItemCaseSensitive(part, "text");
      if (cJSON_IsString(type) && (type->valuestring != NULL) &&
          (strcmp(type->valuestring, "output_text") == 0) &&
          cJSON_IsString(text) && (text->valuestring != NULL) &&
          (text->valuestring[0] != '\0')) {
        has_text = 1;
        break;
      }
    }
  }
  has_text = has_text && cJSON_IsString(status) &&
    (status->valuestring != NULL) &&
    (strcmp(status->valuestring, "completed") == 0);
  cJSON_Delete(root);
  return has_text;
}

static const char *probe_failure_category(
  const strappy_responses_http_result *http)
{
  if ((http != NULL) && (http->http_status == 401L)) {
    return "authentication";
  }
  if ((http != NULL) && (http->http_status == 403L)) {
    return "permission";
  }
  if ((http != NULL) && (http->http_status == 429L)) {
    return "rate-limit";
  }
  if ((http != NULL) && (http->http_status >= 500L)) {
    return "provider";
  }
  if ((http != NULL) && (http->curl_code != 0L)) {
    return "transport";
  }
  return "protocol-or-request";
}

static void probe_clear_secret(char *value)
{
  volatile char *cursor;

  if (value == NULL) {
    return;
  }
  cursor = (volatile char *)value;
  while (*cursor != '\0') {
    *cursor++ = '\0';
  }
}

int main(int argc, char **argv)
{
  const char *env_path;
  const char *model_environment;
  const strappy_provider_definition *other;
  strappy_config config;
  strappy_responses_http_result http;
  char *model;
  char *request_json;
  char *endpoint;
  char *error;
  int ok;
  strappy_provider_kind provider;

  provider = STRAPPY_PROVIDER_KIND_OTHER;
  if (argc == 2) {
    if (strcmp(argv[1], "--openrouter") != 0) {
      fprintf(stderr,"Usage: %s [--openrouter]\n",argv[0]);
      return EXIT_FAILURE;
    }
    provider = STRAPPY_PROVIDER_KIND_OPENROUTER;
  } else if (argc != 1) {
    fprintf(stderr,"Usage: %s [--openrouter]\n",argv[0]);
    return EXIT_FAILURE;
  }

  env_path = getenv(PROBE_ENV_PATH_ENV);
  if ((env_path == NULL) || (env_path[0] == '\0')) {
    env_path = "../../.env";
  }
  model_environment = getenv(PROBE_MODEL_ENV);
  model = ((model_environment != NULL) &&
           (model_environment[0] != '\0')) ?
    strappy_string_duplicate(model_environment) :
    strappy_string_duplicate(STRAPPY_CONFIG_DEFAULT_API_MODEL);
  if ((model == NULL) || (strlen(model) > PROBE_MAX_MODEL_BYTES) ||
      (strchr(model, '\r') != NULL) || (strchr(model, '\n') != NULL)) {
    fprintf(stderr,
            "Other/OpenRouter probe requires a bounded model selection.\n");
    free(model);
    return EXIT_FAILURE;
  }

  strappy_config_init(&config);
  strappy_responses_http_result_init(&http);
  error = NULL;
  request_json = NULL;
  endpoint = NULL;
  other = strappy_provider_for_kind(provider);
  ok = (other != NULL) && strappy_config_load(&config, env_path, &error) &&
    (config.api_token != NULL) && (config.api_token[0] != '\0');
  if (ok) {
    endpoint = strappy_provider_definition_responses_endpoint(
      other,
      config.api_endpoint,
      &error);
    ok = endpoint != NULL;
  }
  if (ok) {
    free(config.api_endpoint);
    config.api_endpoint = endpoint;
    endpoint = NULL;
    ok = strappy_config_set_api_model(&config, model, &error);
  }
  request_json = ok ? probe_request_json(model, provider) : NULL;
  ok = ok && (request_json != NULL) &&
    strappy_client_send_provider_responses_json(
      &config,
      provider,
      config.api_token,
      NULL,
      NULL,
      request_json,
      &http,
      NULL,
      NULL,
      &error) &&
    !http.cancelled && (http.curl_code == 0L) &&
    (http.http_status >= 200L) && (http.http_status < 300L) &&
    (http.transport_error == NULL) && (http.response_json != NULL) &&
    probe_response_is_completed_with_text(http.response_json);
  if (!ok) {
    fprintf(stderr,
            "FAIL: %s over OpenRouter (HTTP %ld, category %s).\n",
            (provider == STRAPPY_PROVIDER_KIND_OTHER) ?
              "Other provider" : "OpenRouter provider",
            http.http_status,
            probe_failure_category(&http));
  } else {
    printf("PASS: %s completed one OpenRouter response.\n",
           (provider == STRAPPY_PROVIDER_KIND_OTHER) ?
             "Other provider" : "OpenRouter provider");
  }

  free(endpoint);
  free(request_json);
  free(model);
  strappy_free_string(error);
  strappy_responses_http_result_destroy(&http);
  probe_clear_secret(config.api_token);
  strappy_config_destroy(&config);
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
