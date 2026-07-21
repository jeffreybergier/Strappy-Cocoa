#define _XOPEN_SOURCE 700

#include "strappy_tools.h"
#include "strappy_web.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int harness_fail(const char *message)
{
  fprintf(stderr, "%s\n", message);
  return 0;
}

static int harness_error_contains(const char *database_path,
                                  const char *tool_name,
                                  const char *arguments,
                                  const char *expected)
{
  char *output;
  char *error;
  int ok;

  error = NULL;
  output = strappy_tools_execute(database_path,
                                 1LL,
                                 "../shared/Resources",
                                 tool_name,
                                 arguments,
                                 &error);
  ok = (output == NULL) && (error != NULL) &&
    (strstr(error, expected) != NULL);
  if (!ok) {
    fprintf(stderr,
            "%s unexpectedly returned output=%s error=%s\n",
            tool_name,
            (output != NULL) ? output : "(null)",
            (error != NULL) ? error : "(null)");
  }
  free(output);
  free(error);
  return ok;
}

static int harness_stop_immediately(void *unused)
{
  (void)unused;
  return 0;
}

static int harness_typed_web_boundary_rejects_invalid_inputs(
  const char *database_path)
{
  strappy_web_result result;
  char *error;
  int ok;

  strappy_web_result_init(&result);
  error = NULL;
  ok = !strappy_web_fetch(database_path,
                          "file:///etc/passwd",
                          NULL,
                          NULL,
                          &result,
                          &error) &&
    (error != NULL) && (strstr(error, "HTTP or HTTPS") != NULL);
  free(error);
  strappy_web_result_destroy(&result);
  if (!ok) {
    return 0;
  }

  strappy_web_result_init(&result);
  error = NULL;
  ok = !strappy_web_search(database_path,
                           "test",
                           0,
                           NULL,
                           NULL,
                           &result,
                           &error) &&
    (error != NULL) && (strstr(error, "from 1 through 10") != NULL);
  free(error);
  strappy_web_result_destroy(&result);
  return ok;
}

static int harness_cancellation_is_reported(const char *database_path)
{
  char *output;
  char *error;
  int truncated;
  int cancelled;
  int ok;

  error = NULL;
  truncated = 0;
  cancelled = 0;
  output = strappy_tools_execute_for_function_call_with_cancellation(
    database_path,
    1LL,
    "../shared/Resources",
    "web-harness-cancel",
    STRAPPY_TOOL_WEB_FETCH,
    "{\"url\":\"https://example.com/\"}",
    harness_stop_immediately,
    NULL,
    &truncated,
    &cancelled,
    &error);
  ok = (output == NULL) && (truncated == 0) && (cancelled == 1) &&
    (error != NULL) && (strstr(error, "cancelled") != NULL);
  free(output);
  free(error);
  return ok;
}

static int harness_search_rate_limit_is_classified(void)
{
  const char challenge[] =
    "<html><form id=\"challenge-form\" "
    "action=\"//duckduckgo.com/anomaly.js?sv=lite\">"
    "Unfortunately, bots use DuckDuckGo too.</form></html>";
  const char ordinary[] =
    "<html><a class=\"result-link\">CAPTCHA article</a></html>";

  return strappy_web_search_response_is_rate_limited(
      202L,
      ordinary,
      strlen(ordinary)) &&
    strappy_web_search_response_is_rate_limited(
      200L,
      challenge,
      strlen(challenge)) &&
    !strappy_web_search_response_is_rate_limited(
      200L,
      ordinary,
      strlen(ordinary));
}

static int harness_output_has_exact_envelope(const char *output)
{
  cJSON *root;
  cJSON *status;
  cJSON *content_type;
  cJSON *body;
  cJSON *field;
  int field_count;
  int ok;

  root = cJSON_Parse(output);
  status = cJSON_GetObjectItemCaseSensitive(root, "status");
  content_type = cJSON_GetObjectItemCaseSensitive(root, "content_type");
  body = cJSON_GetObjectItemCaseSensitive(root, "body");
  field_count = 0;
  for (field = cJSON_IsObject(root) ? root->child : NULL;
       field != NULL;
       field = field->next) {
    field_count++;
  }
  ok = cJSON_IsObject(root) && (field_count == 3) &&
    cJSON_IsNumber(status) && (status->valuedouble > 0.0) &&
    cJSON_IsString(content_type) && (content_type->valuestring != NULL) &&
    (strstr(content_type->valuestring, "text") != NULL) &&
    cJSON_IsString(body) && (body->valuestring != NULL) &&
    (body->valuestring[0] != '\0');
  cJSON_Delete(root);
  return ok;
}

static long harness_output_status(const char *output)
{
  cJSON *root;
  cJSON *status;
  long value;

  root = cJSON_Parse(output);
  status = cJSON_GetObjectItemCaseSensitive(root, "status");
  value = cJSON_IsNumber(status) ? (long)status->valuedouble : 0L;
  cJSON_Delete(root);
  return value;
}

static int harness_output_has_next_form(const char *output)
{
  cJSON *root;
  cJSON *body;
  int has_next;

  root = cJSON_Parse(output);
  body = cJSON_GetObjectItemCaseSensitive(root, "body");
  has_next = cJSON_IsString(body) && (body->valuestring != NULL) &&
    (strstr(body->valuestring, "next_form") != NULL);
  cJSON_Delete(root);
  return has_next;
}

static int harness_run_live(const char *database_path)
{
  char *output;
  char *error;
  long search_status;
  int search_has_next_form;
  int search_rate_limited;
  int ok;

  error = NULL;
  output = strappy_tools_execute(database_path,
                                 1LL,
                                 "../shared/Resources",
                                 STRAPPY_TOOL_WEB_SEARCH,
                                 "{\"query\":\"Strappy Cocoa\"}",
                                 &error);
  search_rate_limited = (output == NULL) && (error != NULL) &&
    (strstr(error, "temporarily rate limited") != NULL);
  ok = ((output != NULL) && harness_output_has_exact_envelope(output)) ||
    search_rate_limited;
  if (!ok) {
    fprintf(stderr,
            "Live web_search failed: %s\n",
            (error != NULL) ? error : "invalid output envelope");
  }
  search_status = (output != NULL) ? harness_output_status(output) : 0L;
  search_has_next_form = (output != NULL) ?
    harness_output_has_next_form(output) : 0;
  free(output);
  free(error);
  if (!ok) {
    return 0;
  }

  if (!search_rate_limited && (search_status == 200L) &&
      search_has_next_form) {
    int paginated_search_rate_limited;

    error = NULL;
    output = strappy_tools_execute(
      database_path,
      1LL,
      "../shared/Resources",
      STRAPPY_TOOL_WEB_SEARCH,
      "{\"query\":\"Strappy Cocoa\",\"page\":2}",
      &error);
    paginated_search_rate_limited = (output == NULL) && (error != NULL) &&
      (strstr(error, "temporarily rate limited") != NULL);
    ok = ((output != NULL) && harness_output_has_exact_envelope(output)) ||
      paginated_search_rate_limited;
    if (!ok) {
      fprintf(stderr,
              "Live paginated web_search failed: %s\n",
              (error != NULL) ? error : "invalid output envelope");
    }
    free(output);
    free(error);
    if (!ok) {
      return 0;
    }
  }

  error = NULL;
  output = strappy_tools_execute(database_path,
                                 1LL,
                                 "../shared/Resources",
                                 STRAPPY_TOOL_WEB_FETCH,
                                 "{\"url\":\"https://example.com/\"}",
                                 &error);
  ok = (output != NULL) && harness_output_has_exact_envelope(output);
  if (!ok) {
    fprintf(stderr,
            "Live web_fetch failed: %s\n",
            (error != NULL) ? error : "invalid output envelope");
  }
  free(output);
  free(error);
  return ok;
}

int main(int argc, char **argv)
{
  char directory_template[] = "/tmp/strappy-web-harness-XXXXXX";
  char database_path[256];
  char cookie_path[256];
  char *directory;
  int live;
  int ok;

  live = (argc == 2) && (strcmp(argv[1], "--live") == 0);
  if ((argc > 2) || ((argc == 2) && !live)) {
    return harness_fail("Usage: web_harness [--live]") ? 0 : 1;
  }
  directory = mkdtemp(directory_template);
  if (directory == NULL) {
    return harness_fail("Could not create web harness directory.") ? 0 : 1;
  }
  (void)snprintf(database_path,
                 sizeof(database_path),
                 "%s/strappy.sqlite",
                 directory);
  (void)snprintf(cookie_path,
                 sizeof(cookie_path),
                 "%s/strappy-web-cookies.txt",
                 directory);

  ok = strappy_tools_is_registered(STRAPPY_TOOL_WEB_SEARCH) &&
    strappy_tools_is_registered(STRAPPY_TOOL_WEB_FETCH) &&
    harness_search_rate_limit_is_classified() &&
    harness_typed_web_boundary_rejects_invalid_inputs(database_path) &&
    harness_error_contains(database_path,
                           STRAPPY_TOOL_WEB_SEARCH,
                           "{\"query\":\"test\",\"page\":0}",
                           "integer from 1 through 10") &&
    harness_error_contains(database_path,
                           STRAPPY_TOOL_WEB_SEARCH,
                           "{\"query\":\"test\",\"extra\":true}",
                           "only query and page") &&
    harness_error_contains(database_path,
                           STRAPPY_TOOL_WEB_FETCH,
                           "{\"url\":\"file:///etc/passwd\"}",
                           "HTTP or HTTPS") &&
    harness_error_contains(database_path,
                           STRAPPY_TOOL_WEB_FETCH,
                           "{\"url\":\"http://user:pass@example.com/\"}",
                           "contains credentials") &&
    harness_error_contains(database_path,
                           STRAPPY_TOOL_WEB_FETCH,
                           "{\"url\":\"http://example.com\\\\path\"}",
                           "backslashes") &&
    harness_error_contains(database_path,
                           STRAPPY_TOOL_WEB_FETCH,
                           "{\"url\":\"http://localhost/\"}",
                           "public Internet host") &&
    harness_error_contains(database_path,
                           STRAPPY_TOOL_WEB_FETCH,
                           "{\"url\":\"http://127.0.0.1/\"}",
                           "not a public address") &&
    harness_cancellation_is_reported(database_path);
  if (ok && live) {
    ok = harness_run_live(database_path);
  }

  (void)unlink(cookie_path);
  (void)rmdir(directory);
  if (!ok) {
    return 1;
  }
  printf("web_harness passed%s.\n", live ? " (live)" : "");
  return 0;
}
