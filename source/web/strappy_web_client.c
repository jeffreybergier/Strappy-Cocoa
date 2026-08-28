#include "strappy_client.h"

#include "strappy_core.h"

#include <emscripten/emscripten.h>
#include <stdlib.h>
#include <string.h>

static char *strappy_web_client_output_text = NULL;
static char *strappy_web_client_model = NULL;
static char *strappy_web_client_status = NULL;
static char *strappy_web_client_error = NULL;
static long strappy_web_client_http_status = 0L;

static void strappy_web_client_clear_result(void)
{
  free(strappy_web_client_output_text);
  free(strappy_web_client_model);
  free(strappy_web_client_status);
  free(strappy_web_client_error);
  strappy_web_client_output_text = NULL;
  strappy_web_client_model = NULL;
  strappy_web_client_status = NULL;
  strappy_web_client_error = NULL;
  strappy_web_client_http_status = 0L;
}

static char *strappy_web_client_copy_json_string(const char *json,
                                                 const char *name)
{
  size_t name_length;
  char *marker;
  const char *start;
  const char *cursor;
  char *copy;
  size_t output_length;

  if ((json == NULL) || (name == NULL)) {
    return strappy_string_duplicate("");
  }
  name_length = strlen(name);
  marker = (char *)malloc(name_length + 3U);
  if (marker == NULL) {
    return NULL;
  }
  marker[0] = '"';
  memcpy(marker + 1U, name, name_length);
  marker[name_length + 1U] = '"';
  marker[name_length + 2U] = '\0';
  start = strstr(json, marker);
  free(marker);
  if (start == NULL) {
    return strappy_string_duplicate("");
  }
  start += name_length + 2U;
  while ((*start == ' ') || (*start == '\t') ||
         (*start == '\r') || (*start == '\n')) {
    start++;
  }
  if (*start++ != ':') {
    return strappy_string_duplicate("");
  }
  while ((*start == ' ') || (*start == '\t') ||
         (*start == '\r') || (*start == '\n')) {
    start++;
  }
  if (*start++ != '"') {
    return strappy_string_duplicate("");
  }
  cursor = start;
  output_length = 0U;
  while ((*cursor != '\0') && (*cursor != '"')) {
    if ((*cursor == '\\') && (cursor[1] != '\0')) {
      cursor++;
    }
    cursor++;
    output_length++;
  }
  copy = (char *)malloc(output_length + 1U);
  if (copy == NULL) {
    return NULL;
  }
  cursor = start;
  output_length = 0U;
  while ((*cursor != '\0') && (*cursor != '"')) {
    if ((*cursor == '\\') && (cursor[1] != '\0')) {
      cursor++;
      switch (*cursor) {
        case 'n': copy[output_length++] = '\n'; break;
        case 'r': copy[output_length++] = '\r'; break;
        case 't': copy[output_length++] = '\t'; break;
        case 'b': copy[output_length++] = '\b'; break;
        case 'f': copy[output_length++] = '\f'; break;
        default: copy[output_length++] = *cursor; break;
      }
    } else {
      copy[output_length++] = *cursor;
    }
    cursor++;
  }
  copy[output_length] = '\0';
  return copy;
}

EMSCRIPTEN_KEEPALIVE
int strappy_web_client_test_request(const char *api_key)
{
  static const char *request_json =
    "{\"model\":\"openrouter/free\","
    "\"input\":\"Reply with exactly: Strappy transport works.\","
    "\"max_output_tokens\":256,\"store\":false,\"stream\":false}";
  strappy_config config;
  strappy_responses_http_result result;
  char *error;
  int ok;

  strappy_web_client_clear_result();
  if ((api_key == NULL) || (api_key[0] == '\0')) {
    strappy_web_client_error = strappy_string_duplicate(
      "Enter an OpenRouter API key first.");
    return 0;
  }
  memset(&config, 0, sizeof(config));
  config.api_endpoint = (char *)STRAPPY_PROVIDER_OPENROUTER_RESPONSES_ENDPOINT;
  config.api_token = (char *)api_key;
  error = NULL;
  strappy_responses_http_result_init(&result);
  ok = strappy_client_send_responses_json(&config,
                                          request_json,
                                          &result,
                                          NULL,
                                          NULL,
                                          &error);
  strappy_web_client_http_status = result.http_status;
  if (!ok) {
    strappy_web_client_error = (error != NULL) ? error :
      strappy_string_duplicate("The shared C client request failed.");
    strappy_responses_http_result_destroy(&result);
    return 0;
  }
  free(error);
  if (result.transport_error != NULL) {
    strappy_web_client_error = strappy_string_duplicate(
      result.cancelled ? "Responses request was cancelled." :
        result.transport_error);
    strappy_responses_http_result_destroy(&result);
    return 0;
  }
  if ((result.http_status < 200L) || (result.http_status >= 300L)) {
    strappy_set_formatted_error(&strappy_web_client_error,
                                "OpenRouter returned HTTP %ld.",
                                result.http_status);
    strappy_responses_http_result_destroy(&result);
    return 0;
  }
  if ((result.response_json == NULL) ||
      (strchr(result.response_json, '{') == NULL)) {
    strappy_responses_http_result_destroy(&result);
    strappy_web_client_error = strappy_string_duplicate(
      "OpenRouter returned invalid JSON.");
    return 0;
  }
  strappy_web_client_model = strappy_web_client_copy_json_string(
    result.response_json, "model");
  strappy_web_client_status = strappy_web_client_copy_json_string(
    result.response_json, "status");
  strappy_web_client_output_text = strappy_web_client_copy_json_string(
    result.response_json, "output_text");
  if ((strappy_web_client_output_text != NULL) &&
      (strappy_web_client_output_text[0] == '\0')) {
    free(strappy_web_client_output_text);
    strappy_web_client_output_text = strappy_web_client_copy_json_string(
      result.response_json, "text");
  }
  strappy_responses_http_result_destroy(&result);
  if ((strappy_web_client_model == NULL) ||
      (strappy_web_client_status == NULL) ||
      (strappy_web_client_output_text == NULL)) {
    strappy_web_client_clear_result();
    strappy_web_client_error = strappy_string_duplicate(
      "Could not allocate the shared C response result.");
    return 0;
  }
  return 1;
}

EMSCRIPTEN_KEEPALIVE
long strappy_web_client_result_http_status(void)
{
  return strappy_web_client_http_status;
}

EMSCRIPTEN_KEEPALIVE
const char *strappy_web_client_result_model(void)
{
  return (strappy_web_client_model != NULL) ? strappy_web_client_model : "";
}

EMSCRIPTEN_KEEPALIVE
const char *strappy_web_client_result_status(void)
{
  return (strappy_web_client_status != NULL) ? strappy_web_client_status : "";
}

EMSCRIPTEN_KEEPALIVE
const char *strappy_web_client_result_output_text(void)
{
  return (strappy_web_client_output_text != NULL) ?
    strappy_web_client_output_text : "";
}

EMSCRIPTEN_KEEPALIVE
const char *strappy_web_client_result_error(void)
{
  return (strappy_web_client_error != NULL) ? strappy_web_client_error : "";
}
