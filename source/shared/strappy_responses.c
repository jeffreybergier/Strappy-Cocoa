#include "strappy_responses.h"

#include "strappy_assistant_sets.h"
#include "strappy_bash.h"
#include "strappy_client.h"
#include "strappy_config.h"
#include "strappy_core.h"
#include "strappy_db.h"
#include "strappy_prompt.h"
#include "strappy_quality_policy.h"
#include "strappy_tools.h"

#include <cJSON.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <syslog.h>
#include <sys/time.h>

#define STRAPPY_RESPONSES_MAX_ROUNDS 50L
#define STRAPPY_RESPONSES_MAX_ATTEMPTS 3L
#define STRAPPY_RESPONSES_INITIAL_RETRY_DELAY_MS 500L
#define STRAPPY_RESPONSES_MAX_RETRY_DELAY_MS 60000L
#define STRAPPY_RESPONSES_MAX_AUDIT_RULES 16U

typedef struct strappy_responses_buffer {
  char *data;
  size_t length;
} strappy_responses_buffer;

typedef struct strappy_responses_owned_items {
  char **items;
  size_t count;
} strappy_responses_owned_items;

typedef struct strappy_responses_tool_call {
  long output_index;
  char *call_id;
  char *name;
  char *arguments;
} strappy_responses_tool_call;

typedef struct strappy_responses_analysis {
  int valid_json;
  int valid_response;
  int has_api_error;
  char *status;
  char *model;
  char *response_text;
  char *error_message;
  char *error_type;
  char **tool_activity_names;
  size_t tool_activity_count;
  strappy_responses_tool_call *tool_calls;
  size_t tool_call_count;
} strappy_responses_analysis;

typedef struct strappy_responses_audit_rule {
  const strappy_quality_check_definition *definition;
  int completed;
} strappy_responses_audit_rule;

typedef struct strappy_responses_audit {
  strappy_responses_audit_rule rules[STRAPPY_RESPONSES_MAX_AUDIT_RULES];
  size_t rule_count;
  int web_reference_required;
} strappy_responses_audit;

static int strappy_responses_text_has_non_whitespace(const char *text);

static void strappy_responses_buffer_destroy(strappy_responses_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }
  free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0U;
}

static int strappy_responses_buffer_append(strappy_responses_buffer *buffer,
                                           const char *data,
                                           size_t length)
{
  char *next;

  if ((buffer == NULL) || ((data == NULL) && (length > 0U)) ||
      (buffer->length > (((size_t)-1) - length - 1U))) {
    return 0;
  }
  next = (char *)realloc(buffer->data, buffer->length + length + 1U);
  if (next == NULL) {
    return 0;
  }
  buffer->data = next;
  if (length > 0U) {
    memcpy(buffer->data + buffer->length, data, length);
  }
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

static int strappy_responses_buffer_append_string(
  strappy_responses_buffer *buffer,
  const char *text)
{
  return (text == NULL) ? 1 :
    strappy_responses_buffer_append(buffer, text, strlen(text));
}

static void strappy_responses_audit_reset(strappy_responses_audit *audit)
{
  if (audit == NULL) {
    return;
  }
  memset(audit, 0, sizeof(*audit));
}

static int strappy_responses_audit_init(
  strappy_responses_audit *audit,
  const strappy_assistant_set_profile *profile,
  char **error_out)
{
  size_t profile_index;

  if ((audit == NULL) || (profile == NULL)) {
    strappy_set_error(error_out,
                      "Answer-quality assistant set is incomplete.");
    return 0;
  }
  strappy_responses_audit_reset(audit);
  for (profile_index = 0U;
       profile_index < profile->quality_check_key_count;
       profile_index++) {
    const char *check_key;
    const strappy_quality_check_definition *definition;

    check_key = profile->quality_check_keys[profile_index];
    definition = strappy_quality_policy_find(check_key);
    if (definition == NULL) {
      strappy_set_formatted_error(error_out,
                                  "Answer-quality check is not registered: %s",
                                  check_key);
      return 0;
    }
    if ((definition->tool_name != NULL) &&
        !strappy_assistant_set_profile_allows_tool(profile,
                                                   definition->tool_name)) {
      strappy_set_formatted_error(
        error_out,
        "Answer-quality tool is not allowed by the assistant set: %s",
        definition->tool_name);
      return 0;
    }
    if (audit->rule_count >= STRAPPY_RESPONSES_MAX_AUDIT_RULES) {
      strappy_set_error(error_out,
                        "Assistant-set quality policy is too large.");
      return 0;
    }
    audit->rules[audit->rule_count].definition = definition;
    audit->rules[audit->rule_count].completed = 0;
    audit->rule_count++;
  }
  if (audit->rule_count == 0U) {
    strappy_set_error(error_out,
                      "Assistant-set quality policy is empty.");
    return 0;
  }
  return 1;
}

static void strappy_responses_audit_record_activity(
  strappy_responses_audit *audit,
  const char *tool_name)
{
  if ((audit == NULL) || (tool_name == NULL) || (tool_name[0] == '\0')) {
    return;
  }
  if ((strcmp(tool_name, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) == 0) ||
      (strcmp(tool_name, STRAPPY_TOOL_OPENROUTER_WEB_FETCH) == 0)) {
    audit->web_reference_required = 1;
  }
}

static void strappy_responses_audit_record_completed_tool(
  strappy_responses_audit *audit,
  const char *tool_name)
{
  size_t index;

  if ((audit == NULL) || (tool_name == NULL) || (tool_name[0] == '\0')) {
    return;
  }
  for (index = 0U; index < audit->rule_count; index++) {
    if ((audit->rules[index].definition->tool_name != NULL) &&
        (strcmp(audit->rules[index].definition->tool_name, tool_name) == 0)) {
      audit->rules[index].completed = 1;
    }
  }
}

static int strappy_responses_text_has_http_markdown_link(const char *text)
{
  static const char http_scheme[] = "http://";
  static const char https_scheme[] = "https://";
  const unsigned char *begin;
  const unsigned char *cursor;
  const unsigned char *url_start;
  size_t scheme_length;
  int title_has_text;

  if (text == NULL) {
    return 0;
  }
  begin = (const unsigned char *)text;
  cursor = begin;
  /* Match [title](http://url) or [title](https://url), excluding images. */
  while (*cursor != '\0') {
    if ((*cursor != '[') ||
        ((cursor > begin) && (cursor[-1] == '!'))) {
      cursor++;
      continue;
    }

    cursor++;
    title_has_text = 0;
    while ((*cursor != '\0') && (*cursor != ']') &&
           (*cursor != '\r') && (*cursor != '\n')) {
      if (!isspace((int)*cursor)) {
        title_has_text = 1;
      }
      cursor++;
    }
    if ((*cursor != ']') || !title_has_text || (cursor[1] != '(')) {
      if (*cursor != '\0') {
        cursor++;
      }
      continue;
    }
    if (strncmp((const char *)cursor + 2,
                https_scheme,
                sizeof(https_scheme) - 1U) == 0) {
      scheme_length = sizeof(https_scheme) - 1U;
    } else if (strncmp((const char *)cursor + 2,
                       http_scheme,
                       sizeof(http_scheme) - 1U) == 0) {
      scheme_length = sizeof(http_scheme) - 1U;
    } else {
      cursor++;
      continue;
    }

    cursor += 2U + scheme_length;
    url_start = cursor;
    while ((*cursor != '\0') && (*cursor != ')') &&
           !isspace((int)*cursor) && !iscntrl((int)*cursor)) {
      cursor++;
    }
    if ((cursor > url_start) && (*cursor == ')')) {
      return 1;
    }
  }
  return 0;
}

static void strappy_responses_answer_quality_destroy(
  strappy_answer_quality_audit_input *result)
{
  if (result == NULL) {
    return;
  }
  free((void *)result->checks);
  memset(result, 0, sizeof(*result));
}

static int strappy_responses_audit_evaluate(
  const strappy_responses_audit *audit,
  const char *response_text,
  strappy_answer_quality_audit_input *result,
  char **error_out)
{
  strappy_answer_quality_check_input *checks;
  size_t index;
  int failed;

  if ((audit == NULL) || (result == NULL) || (audit->rule_count == 0U)) {
    strappy_set_error(error_out, "Answer quality evaluation is incomplete.");
    return 0;
  }
  memset(result, 0, sizeof(*result));
  checks = (strappy_answer_quality_check_input *)calloc(
    audit->rule_count,
    sizeof(strappy_answer_quality_check_input));
  if (checks == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate answer quality checks.");
    return 0;
  }

  failed = 0;
  for (index = 0U; index < audit->rule_count; index++) {
    const strappy_responses_audit_rule *rule;
    strappy_answer_quality_check_input *check;

    rule = &audit->rules[index];
    check = &checks[index];
    check->check_key = rule->definition->check_key;
    check->check_kind = rule->definition->check_kind;
    check->label = rule->definition->label;
    check->tool_name = rule->definition->tool_name;
    if (rule->definition->evaluation_kind ==
        STRAPPY_QUALITY_CHECK_ANSWER_NON_EMPTY) {
      if (strappy_responses_text_has_non_whitespace(response_text)) {
        check->status = "passed";
      } else {
        check->status = "failed";
        check->detail =
          "The response did not include a non-empty assistant answer.";
        failed = 1;
      }
    } else if (rule->definition->evaluation_kind ==
               STRAPPY_QUALITY_CHECK_UNICODE_EMOJI_ABSENT) {
      if (strappy_quality_policy_text_has_unicode_emoji(response_text)) {
        check->status = "failed";
        check->detail = "The response included emoji.";
        failed = 1;
      } else {
        check->status = "passed";
      }
    } else if (rule->definition->evaluation_kind ==
               STRAPPY_QUALITY_CHECK_WEB_REFERENCE) {
      if (!audit->web_reference_required) {
        check->status = "not_applicable";
        check->detail = "No web search or web fetch was used.";
      } else if (strappy_responses_text_has_http_markdown_link(response_text)) {
        check->status = "passed";
      } else {
        check->status = "failed";
        check->detail =
          "A linked HTTP source reference was required but not found.";
        failed = 1;
      }
    } else if (rule->definition->evaluation_kind ==
               STRAPPY_QUALITY_CHECK_REQUIRED_TOOL) {
      if (rule->completed) {
        check->status = "passed";
      } else {
        check->status = "failed";
        check->detail = "The required tool did not complete successfully.";
        failed = 1;
      }
    } else {
      free(checks);
      strappy_set_error(error_out,
                        "Answer-quality check has an invalid evaluator.");
      return 0;
    }
  }

  result->outcome = failed ? "failed" : "passed";
  result->guidance_version = STRAPPY_QUALITY_POLICY_GUIDANCE_VERSION;
  result->checks = checks;
  result->check_count = audit->rule_count;
  return 1;
}

static long long strappy_responses_now_ms(void)
{
  struct timeval value;

  if (gettimeofday(&value, NULL) != 0) {
    return 0LL;
  }
  return ((long long)value.tv_sec * 1000LL) +
    ((long long)value.tv_usec / 1000LL);
}

static void strappy_responses_emit_processing_status(
  strappy_responses_event_callback callback,
  void *callback_data,
  const char *prompt_group_key,
  int active,
  const char *status_kind,
  long long started_ms,
  long retry_delay_ms,
  unsigned int retry_attempt,
  const char *status_reason)
{
  strappy_responses_event event;
  cJSON *root;
  char *status_json;
  long long now_ms;
  unsigned int retry_after_seconds;

  if (callback == NULL) {
    return;
  }
  now_ms = strappy_responses_now_ms();
  if (started_ms <= 0LL) {
    started_ms = now_ms;
  }
  retry_after_seconds = 0U;
  if (retry_delay_ms > 0L) {
    retry_after_seconds = (unsigned int)((retry_delay_ms + 999L) / 1000L);
  }

  root = cJSON_CreateObject();
  if ((root == NULL) ||
      (cJSON_AddBoolToObject(root, "active", active ? 1 : 0) == NULL)) {
    cJSON_Delete(root);
    return;
  }
  if (active &&
      ((cJSON_AddStringToObject(
          root,
          "message_key",
          (prompt_group_key != NULL) ? prompt_group_key :
            "responses-processing") == NULL) ||
       (cJSON_AddStringToObject(
          root,
          "status_kind",
          ((status_kind != NULL) && (status_kind[0] != '\0')) ?
            status_kind : "thinking") == NULL) ||
       (cJSON_AddNumberToObject(root,
                               "started_ms",
                               (double)started_ms) == NULL) ||
       (cJSON_AddNumberToObject(root,
                               "updated_ms",
                               (double)now_ms) == NULL) ||
       (cJSON_AddNumberToObject(root,
                               "retry_after_seconds",
                               (double)retry_after_seconds) == NULL) ||
       (cJSON_AddNumberToObject(root,
                               "retry_attempt",
                               (double)retry_attempt) == NULL) ||
       (cJSON_AddNumberToObject(
          root,
          "retry_max_attempts",
          (double)((unsigned int)STRAPPY_RESPONSES_MAX_ATTEMPTS)) == NULL))) {
    cJSON_Delete(root);
    return;
  }
  if (active && (retry_delay_ms > 0L) && (now_ms > 0LL) &&
      (cJSON_AddNumberToObject(root,
                              "retry_until_ms",
                              (double)(now_ms + retry_delay_ms)) == NULL)) {
    cJSON_Delete(root);
    return;
  }
  if (active && (status_reason != NULL) && (status_reason[0] != '\0') &&
      (cJSON_AddStringToObject(root,
                              "retry_reason",
                              status_reason) == NULL)) {
    cJSON_Delete(root);
    return;
  }

  status_json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (status_json == NULL) {
    return;
  }
  memset(&event, 0, sizeof(event));
  event.type = STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS;
  event.prompt_group_key = prompt_group_key;
  event.actor = "api";
  event.kind = "response_api_call";
  event.message_key = (prompt_group_key != NULL) ? prompt_group_key :
    "responses-processing";
  event.render_role = "api_call";
  event.status_json = status_json;
  event.status_kind = status_kind;
  event.status_reason = status_reason;
  event.status_started_ms = started_ms;
  event.status_updated_ms = now_ms;
  event.retry_after_seconds = retry_after_seconds;
  event.retry_attempt = retry_attempt;
  event.retry_max_attempts =
    (unsigned int)STRAPPY_RESPONSES_MAX_ATTEMPTS;
  if ((retry_delay_ms > 0L) && (now_ms > 0LL)) {
    event.retry_until_ms = now_ms + retry_delay_ms;
  }
  (void)callback(&event, callback_data);
  free(status_json);
}

static const char *strappy_responses_retry_status_reason(
  const strappy_responses_http_result *http,
  const strappy_responses_analysis *analysis,
  char *buffer,
  size_t buffer_size)
{
  if ((analysis != NULL) && (analysis->error_type != NULL) &&
      (analysis->error_type[0] != '\0')) {
    return analysis->error_type;
  }
  if ((analysis != NULL) && (analysis->error_message != NULL) &&
      (analysis->error_message[0] != '\0')) {
    return analysis->error_message;
  }
  if ((http != NULL) && (http->transport_error != NULL) &&
      (http->transport_error[0] != '\0')) {
    return http->transport_error;
  }
  if ((http != NULL) && (http->http_status > 0L) &&
      (buffer != NULL) && (buffer_size > 0U)) {
    snprintf(buffer, buffer_size, "HTTP %ld", http->http_status);
    return buffer;
  }
  return "";
}

static int strappy_responses_poll_cancelled(
  strappy_responses_event_callback callback,
  void *callback_data)
{
  strappy_responses_event event;

  if (callback == NULL) {
    return 0;
  }
  memset(&event, 0, sizeof(event));
  event.type = STRAPPY_RESPONSES_EVENT_CANCELLATION_POLL;
  return callback(&event, callback_data) ? 0 : 1;
}

typedef struct strappy_responses_tool_continue_context {
  strappy_responses_event_callback callback;
  void *callback_data;
} strappy_responses_tool_continue_context;

static int strappy_responses_tool_should_continue(void *user_data)
{
  strappy_responses_tool_continue_context *context;

  context = (strappy_responses_tool_continue_context *)user_data;
  if (context == NULL) {
    return 1;
  }
  return strappy_responses_poll_cancelled(context->callback,
                                           context->callback_data) ? 0 : 1;
}

static int strappy_responses_sleep_ms(long milliseconds,
                                      strappy_responses_event_callback callback,
                                      void *callback_data)
{
  struct timeval delay;
  long remaining;
  long interval;

  if (milliseconds <= 0L) {
    return strappy_responses_poll_cancelled(callback, callback_data) ? 0 : 1;
  }
  remaining = milliseconds;
  while (remaining > 0L) {
    if (strappy_responses_poll_cancelled(callback, callback_data)) {
      return 0;
    }
    interval = (remaining > 100L) ? 100L : remaining;
    delay.tv_sec = interval / 1000L;
    delay.tv_usec = (interval % 1000L) * 1000L;
    select(0, NULL, NULL, NULL, &delay);
    remaining -= interval;
  }
  return strappy_responses_poll_cancelled(callback, callback_data) ? 0 : 1;
}

static void strappy_responses_owned_items_init(
  strappy_responses_owned_items *items)
{
  if (items == NULL) {
    return;
  }
  items->items = NULL;
  items->count = 0U;
}

static void strappy_responses_owned_items_destroy(
  strappy_responses_owned_items *items)
{
  size_t index;

  if (items == NULL) {
    return;
  }
  for (index = 0U; index < items->count; index++) {
    free(items->items[index]);
  }
  free(items->items);
  strappy_responses_owned_items_init(items);
}

static int strappy_responses_owned_items_append(
  strappy_responses_owned_items *items,
  char *owned_json,
  char **error_out)
{
  char **next;

  if ((items == NULL) || (owned_json == NULL)) {
    free(owned_json);
    strappy_set_error(error_out, "Responses input item is missing.");
    return 0;
  }
  if (items->count >= (((size_t)-1) / sizeof(char *))) {
    free(owned_json);
    strappy_set_error(error_out, "Responses input item list is too large.");
    return 0;
  }
  next = (char **)realloc(items->items,
                          (items->count + 1U) * sizeof(char *));
  if (next == NULL) {
    free(owned_json);
    strappy_set_error(error_out,
                      "Could not allocate Responses input item list.");
    return 0;
  }
  items->items = next;
  items->items[items->count++] = owned_json;
  return 1;
}

static void strappy_responses_analysis_init(
  strappy_responses_analysis *analysis)
{
  if (analysis == NULL) {
    return;
  }
  memset(analysis, 0, sizeof(*analysis));
}

static void strappy_responses_analysis_destroy(
  strappy_responses_analysis *analysis)
{
  size_t index;

  if (analysis == NULL) {
    return;
  }
  free(analysis->status);
  free(analysis->model);
  free(analysis->response_text);
  free(analysis->error_message);
  free(analysis->error_type);
  for (index = 0U; index < analysis->tool_activity_count; index++) {
    free(analysis->tool_activity_names[index]);
  }
  free(analysis->tool_activity_names);
  for (index = 0U; index < analysis->tool_call_count; index++) {
    free(analysis->tool_calls[index].call_id);
    free(analysis->tool_calls[index].name);
    free(analysis->tool_calls[index].arguments);
  }
  free(analysis->tool_calls);
  strappy_responses_analysis_init(analysis);
}

static int strappy_responses_analysis_append_tool_activity(
  strappy_responses_analysis *analysis,
  cJSON *item,
  const char *type,
  char **error_out)
{
  cJSON *name;
  const char *activity_name;
  char **next;

  if ((analysis == NULL) || (type == NULL)) {
    strappy_set_error(error_out, "Responses tool activity is incomplete.");
    return 0;
  }
  name = cJSON_IsObject(item) ? cJSON_GetObjectItem(item, "name") : NULL;
  activity_name = ((name != NULL) &&
                   cJSON_IsString(name) &&
                   (name->valuestring != NULL) &&
                   (name->valuestring[0] != '\0')) ?
    name->valuestring : type;
  if (analysis->tool_activity_count >=
      (((size_t)-1) / sizeof(char *))) {
    strappy_set_error(error_out,
                      "Responses tool activity list is too large.");
    return 0;
  }
  next = (char **)realloc(
    analysis->tool_activity_names,
    (analysis->tool_activity_count + 1U) * sizeof(char *));
  if (next == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate Responses tool activity list.");
    return 0;
  }
  analysis->tool_activity_names = next;
  analysis->tool_activity_names[analysis->tool_activity_count] =
    strappy_string_duplicate(activity_name);
  if (analysis->tool_activity_names[analysis->tool_activity_count] == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate Responses tool activity name.");
    return 0;
  }
  analysis->tool_activity_count++;
  return 1;
}

static char *strappy_responses_json_value_text(cJSON *value)
{
  if ((value == NULL) || cJSON_IsNull(value)) {
    return NULL;
  }
  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return strappy_string_duplicate(value->valuestring);
  }
  return cJSON_PrintUnformatted(value);
}

static int strappy_responses_capture_optional_string(cJSON *root,
                                                     const char *name,
                                                     char **target,
                                                     char **error_out)
{
  cJSON *value;

  value = cJSON_GetObjectItem(root, name);
  if ((value == NULL) || !cJSON_IsString(value) ||
      (value->valuestring == NULL)) {
    return 1;
  }
  *target = strappy_string_duplicate(value->valuestring);
  if (*target == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate Responses analysis field.");
    return 0;
  }
  return 1;
}

static int strappy_responses_type_is_tool_activity(const char *type)
{
  size_t length;

  if ((type == NULL) || (type[0] == '\0')) {
    return 0;
  }
  if (strncmp(type, "openrouter:", strlen("openrouter:")) == 0) {
    return 1;
  }
  length = strlen(type);
  if ((length >= strlen("_call")) &&
      (strcmp(type + length - strlen("_call"), "_call") == 0)) {
    return 1;
  }
  return (length >= strlen("_call_output")) &&
    (strcmp(type + length - strlen("_call_output"), "_call_output") == 0);
}

static int strappy_responses_append_output_text(
  strappy_responses_buffer *buffer,
  const char *text)
{
  if ((text == NULL) || (text[0] == '\0')) {
    return 1;
  }
  if ((buffer->length > 0U) &&
      !strappy_responses_buffer_append_string(buffer, "\n\n")) {
    return 0;
  }
  return strappy_responses_buffer_append_string(buffer, text);
}

static int strappy_responses_collect_message_text(
  strappy_responses_buffer *buffer,
  cJSON *message)
{
  cJSON *content;
  cJSON *part;

  content = cJSON_GetObjectItem(message, "content");
  if (cJSON_IsString(content)) {
    return strappy_responses_append_output_text(buffer,
                                                content->valuestring);
  }
  if (!cJSON_IsArray(content)) {
    return 1;
  }
  for (part = content->child; part != NULL; part = part->next) {
    cJSON *text;
    cJSON *refusal;

    if (cJSON_IsString(part)) {
      if (!strappy_responses_append_output_text(buffer,
                                                part->valuestring)) {
        return 0;
      }
      continue;
    }
    if (!cJSON_IsObject(part)) {
      continue;
    }
    text = cJSON_GetObjectItem(part, "text");
    refusal = cJSON_GetObjectItem(part, "refusal");
    if (cJSON_IsString(text) &&
        !strappy_responses_append_output_text(buffer,
                                              text->valuestring)) {
      return 0;
    }
    if (cJSON_IsString(refusal) &&
        !strappy_responses_append_output_text(buffer,
                                              refusal->valuestring)) {
      return 0;
    }
  }
  return 1;
}

static int strappy_responses_analysis_append_tool_call(
  strappy_responses_analysis *analysis,
  cJSON *item,
  long output_index,
  char **error_out)
{
  strappy_responses_tool_call *next;
  strappy_responses_tool_call *call;
  cJSON *call_id;
  cJSON *name;
  cJSON *arguments;

  call_id = cJSON_GetObjectItem(item, "call_id");
  name = cJSON_GetObjectItem(item, "name");
  arguments = cJSON_GetObjectItem(item, "arguments");
  if (!cJSON_IsString(call_id) || (call_id->valuestring == NULL) ||
      !cJSON_IsString(name) || (name->valuestring == NULL)) {
    strappy_set_error(error_out,
                      "Responses function call is missing call_id or name.");
    return 0;
  }

  if (analysis->tool_call_count >=
      (((size_t)-1) / sizeof(strappy_responses_tool_call))) {
    strappy_set_error(error_out,
                      "Responses function call list is too large.");
    return 0;
  }
  next = (strappy_responses_tool_call *)realloc(
    analysis->tool_calls,
    (analysis->tool_call_count + 1U) * sizeof(*next));
  if (next == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate Responses function call list.");
    return 0;
  }
  analysis->tool_calls = next;
  call = &analysis->tool_calls[analysis->tool_call_count];
  memset(call, 0, sizeof(*call));
  call->output_index = output_index;
  call->call_id = strappy_string_duplicate(call_id->valuestring);
  call->name = strappy_string_duplicate(name->valuestring);
  call->arguments = strappy_string_duplicate(
    (cJSON_IsString(arguments) && (arguments->valuestring != NULL)) ?
      arguments->valuestring : "{}");
  if ((call->call_id == NULL) || (call->name == NULL) ||
      (call->arguments == NULL)) {
    free(call->call_id);
    free(call->name);
    free(call->arguments);
    memset(call, 0, sizeof(*call));
    strappy_set_error(error_out,
                      "Could not allocate Responses function call.");
    return 0;
  }
  analysis->tool_call_count++;
  return 1;
}

static int strappy_responses_analyze_json(
  const char *json,
  strappy_responses_analysis *analysis,
  char **error_out)
{
  strappy_responses_buffer text;
  cJSON *root;
  cJSON *value;
  cJSON *output;
  cJSON *item;
  long output_index;

  if (analysis == NULL) {
    strappy_set_error(error_out, "Responses analysis has no output.");
    return 0;
  }
  strappy_responses_analysis_init(analysis);
  memset(&text, 0, sizeof(text));

  root = cJSON_Parse((json != NULL) ? json : "");
  if (root == NULL) {
    return 1;
  }
  analysis->valid_json = 1;
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return 1;
  }
  if (!strappy_responses_capture_optional_string(root,
                                                 "status",
                                                 &analysis->status,
                                                 error_out) ||
      !strappy_responses_capture_optional_string(root,
                                                 "model",
                                                 &analysis->model,
                                                 error_out) ||
      !strappy_responses_capture_optional_string(root,
                                                 "error_type",
                                                 &analysis->error_type,
                                                 error_out)) {
    cJSON_Delete(root);
    return 0;
  }

  value = cJSON_GetObjectItem(root, "error");
  output = cJSON_GetObjectItem(root, "output");
  analysis->valid_response =
    (cJSON_GetObjectItem(root, "id") != NULL) ||
    (cJSON_GetObjectItem(root, "object") != NULL) ||
    (cJSON_GetObjectItem(root, "status") != NULL) ||
    (value != NULL) || cJSON_IsArray(output);
  if ((value != NULL) && !cJSON_IsNull(value)) {
    cJSON *message;
    cJSON *code;

    analysis->has_api_error = 1;
    message = cJSON_IsObject(value) ?
      cJSON_GetObjectItem(value, "message") : NULL;
    code = cJSON_IsObject(value) ? cJSON_GetObjectItem(value, "code") : NULL;
    analysis->error_message = ((message != NULL) &&
                               cJSON_IsString(message) &&
                               (message->valuestring != NULL)) ?
      strappy_string_duplicate(message->valuestring) :
      strappy_responses_json_value_text(value);
    if (analysis->error_message == NULL) {
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "Could not allocate Responses error details.");
      return 0;
    }
    if ((analysis->error_type == NULL) && (code != NULL)) {
      analysis->error_type = strappy_responses_json_value_text(code);
      if (!cJSON_IsNull(code) && (analysis->error_type == NULL)) {
        cJSON_Delete(root);
        strappy_set_error(error_out,
                          "Could not allocate Responses error code.");
        return 0;
      }
    }
  }

  output_index = 0L;
  if (cJSON_IsArray(output)) {
    for (item = output->child; item != NULL; item = item->next) {
      cJSON *type;

      type = cJSON_IsObject(item) ? cJSON_GetObjectItem(item, "type") : NULL;
      if ((type != NULL) && cJSON_IsString(type) &&
          (type->valuestring != NULL) &&
          strappy_responses_type_is_tool_activity(type->valuestring)) {
        if (!strappy_responses_analysis_append_tool_activity(
              analysis,
              item,
              type->valuestring,
              error_out)) {
          strappy_responses_buffer_destroy(&text);
          cJSON_Delete(root);
          return 0;
        }
      }
      if ((type != NULL) && cJSON_IsString(type) &&
          (type->valuestring != NULL) &&
          (strcmp(type->valuestring, "function_call") == 0)) {
        if (!strappy_responses_analysis_append_tool_call(analysis,
                                                         item,
                                                         output_index,
                                                         error_out)) {
          strappy_responses_buffer_destroy(&text);
          cJSON_Delete(root);
          return 0;
        }
      } else if ((type != NULL) && cJSON_IsString(type) &&
                 (type->valuestring != NULL) &&
                 (strcmp(type->valuestring, "message") == 0) &&
                 !strappy_responses_collect_message_text(&text, item)) {
        strappy_responses_buffer_destroy(&text);
        cJSON_Delete(root);
        strappy_set_error(error_out,
                          "Could not allocate Responses output text.");
        return 0;
      }
      output_index++;
    }
  }

  if (text.length == 0U) {
    value = cJSON_GetObjectItem(root, "output_text");
    if ((value != NULL) && cJSON_IsString(value) &&
        (value->valuestring != NULL) &&
        !strappy_responses_append_output_text(&text, value->valuestring)) {
      strappy_responses_buffer_destroy(&text);
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "Could not allocate Responses output text.");
      return 0;
    }
  }
  analysis->response_text = (text.data != NULL) ? text.data :
    strappy_string_duplicate("");
  text.data = NULL;
  text.length = 0U;
  strappy_responses_buffer_destroy(&text);
  cJSON_Delete(root);

  if (analysis->response_text == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate Responses analysis.");
    return 0;
  }
  return 1;
}

static char *strappy_responses_json_string(const char *value)
{
  cJSON *string;
  char *json;

  string = cJSON_CreateString((value != NULL) ? value : "");
  if (string == NULL) {
    return NULL;
  }
  json = cJSON_PrintUnformatted(string);
  cJSON_Delete(string);
  return json;
}

static char *strappy_responses_message_item_json(const char *role,
                                                 const char *text,
                                                 char **error_out)
{
  cJSON *item;
  cJSON *content;
  cJSON *part;
  char *json;

  item = cJSON_CreateObject();
  content = cJSON_CreateArray();
  part = cJSON_CreateObject();
  if ((item == NULL) || (content == NULL) || (part == NULL) ||
      (cJSON_AddStringToObject(item, "type", "message") == NULL) ||
      (cJSON_AddStringToObject(item, "role", role) == NULL) ||
      (cJSON_AddStringToObject(part, "type", "input_text") == NULL) ||
      (cJSON_AddStringToObject(part,
                               "text",
                               (text != NULL) ? text : "") == NULL)) {
    cJSON_Delete(part);
    cJSON_Delete(content);
    cJSON_Delete(item);
    strappy_set_error(error_out,
                      "Could not build Responses message input item.");
    return NULL;
  }
  if (!cJSON_AddItemToArray(content, part)) {
    cJSON_Delete(part);
    cJSON_Delete(content);
    cJSON_Delete(item);
    strappy_set_error(error_out,
                      "Could not build Responses message input item.");
    return NULL;
  }
  part = NULL;
  if (!cJSON_AddItemToObject(item, "content", content)) {
    cJSON_Delete(content);
    cJSON_Delete(item);
    strappy_set_error(error_out,
                      "Could not build Responses message input item.");
    return NULL;
  }
  content = NULL;

  json = cJSON_PrintUnformatted(item);
  cJSON_Delete(item);
  if (json == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize Responses message input item.");
  }
  return json;
}

static char *strappy_responses_function_call_item_json(
  const char *item_id,
  const char *call_id,
  const char *name,
  const char *arguments,
  char **error_out)
{
  cJSON *item;
  char *json;

  if ((item_id == NULL) || (call_id == NULL) || (name == NULL) ||
      (arguments == NULL)) {
    strappy_set_error(error_out,
                      "Responses function call item is incomplete.");
    return NULL;
  }

  item = cJSON_CreateObject();
  if ((item == NULL) ||
      (cJSON_AddStringToObject(item, "type", "function_call") == NULL) ||
      (cJSON_AddStringToObject(item, "id", item_id) == NULL) ||
      (cJSON_AddStringToObject(item, "call_id", call_id) == NULL) ||
      (cJSON_AddStringToObject(item, "name", name) == NULL) ||
      (cJSON_AddStringToObject(item, "arguments", arguments) == NULL)) {
    cJSON_Delete(item);
    strappy_set_error(error_out,
                      "Could not build Responses function call item.");
    return NULL;
  }
  json = cJSON_PrintUnformatted(item);
  cJSON_Delete(item);
  if (json == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize Responses function call item.");
  }
  return json;
}

static char *strappy_responses_function_output_item_json(
  const char *call_id,
  const char *output,
  char **error_out)
{
  cJSON *item;
  char *json;

  item = cJSON_CreateObject();
  if ((item == NULL) ||
      (cJSON_AddStringToObject(item,
                               "type",
                               "function_call_output") == NULL) ||
      (cJSON_AddStringToObject(item, "call_id", call_id) == NULL) ||
      (cJSON_AddStringToObject(item,
                               "output",
                               (output != NULL) ? output : "") == NULL)) {
    cJSON_Delete(item);
    strappy_set_error(error_out,
                      "Could not build Responses function output item.");
    return NULL;
  }
  json = cJSON_PrintUnformatted(item);
  cJSON_Delete(item);
  if (json == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize Responses function output item.");
  }
  return json;
}

static char *strappy_responses_preflight_identifier(
  const char *prefix,
  const char *prompt_group_key,
  char **error_out)
{
  strappy_responses_buffer buffer;

  if ((prefix == NULL) || (prompt_group_key == NULL)) {
    strappy_set_error(error_out,
                      "Responses preflight identifier is incomplete.");
    return NULL;
  }
  memset(&buffer, 0, sizeof(buffer));
  if (!strappy_responses_buffer_append_string(&buffer, prefix) ||
      !strappy_responses_buffer_append_string(&buffer, prompt_group_key)) {
    strappy_responses_buffer_destroy(&buffer);
    strappy_set_error(error_out,
                      "Could not allocate Responses preflight identifier.");
    return NULL;
  }
  return buffer.data;
}

static int strappy_responses_append_preflight_items(
  strappy_responses_owned_items *items,
  const char *session_db_path,
  long long session_id,
  const char *resource_dir,
  const char *prompt_group_key,
  const strappy_assistant_set_profile *profile,
  char **error_out)
{
  char **call_ids;
  char **item_ids;
  char **results;
  size_t index;
  int ok;

  if ((items == NULL) || (profile == NULL)) {
    strappy_set_error(error_out,
                      "Responses preflight assistant set is incomplete.");
    return 0;
  }
  if (profile->preflight_call_count == 0U) {
    return 1;
  }
  call_ids = (char **)calloc(profile->preflight_call_count,
                             sizeof(char *));
  item_ids = (char **)calloc(profile->preflight_call_count,
                             sizeof(char *));
  results = (char **)calloc(profile->preflight_call_count,
                            sizeof(char *));
  if ((call_ids == NULL) || (item_ids == NULL) || (results == NULL)) {
    free(call_ids);
    free(item_ids);
    free(results);
    strappy_set_error(error_out,
                      "Could not allocate Responses preflight items.");
    return 0;
  }
  ok = 1;
  for (index = 0U;
       ok && (index < profile->preflight_call_count);
       index++) {
    char call_prefix[64];
    char item_prefix[64];
    char *item_json;
    int call_written;
    int item_written;

    call_written = snprintf(call_prefix,
                            sizeof(call_prefix),
                            "call_pf_%lu_",
                            (unsigned long)index);
    item_written = snprintf(item_prefix,
                            sizeof(item_prefix),
                            "fc_pf_%lu_",
                            (unsigned long)index);
    if ((call_written < 0) ||
        ((size_t)call_written >= sizeof(call_prefix)) ||
        (item_written < 0) ||
        ((size_t)item_written >= sizeof(item_prefix))) {
      strappy_set_error(error_out,
                        "Responses preflight identifier is too large.");
      ok = 0;
      break;
    }
    results[index] = strappy_tools_execute_preflight(
      session_db_path,
      session_id,
      resource_dir,
      profile->preflight_calls[index].tool_name,
      profile->preflight_calls[index].arguments_json,
      error_out);
    call_ids[index] = strappy_responses_preflight_identifier(call_prefix,
                                                             prompt_group_key,
                                                             error_out);
    item_ids[index] = strappy_responses_preflight_identifier(item_prefix,
                                                             prompt_group_key,
                                                             error_out);
    if ((results[index] == NULL) || (call_ids[index] == NULL) ||
        (item_ids[index] == NULL)) {
      ok = 0;
      break;
    }
    item_json = strappy_responses_function_call_item_json(
      item_ids[index],
      call_ids[index],
      profile->preflight_calls[index].tool_name,
      profile->preflight_calls[index].arguments_json,
      error_out);
    if ((item_json == NULL) ||
        !strappy_responses_owned_items_append(items,
                                              item_json,
                                              error_out)) {
      ok = 0;
    }
  }
  for (index = 0U;
       ok && (index < profile->preflight_call_count);
       index++) {
    char *item_json;

    item_json = strappy_responses_function_output_item_json(call_ids[index],
                                                            results[index],
                                                            error_out);
    if ((item_json == NULL) ||
        !strappy_responses_owned_items_append(items,
                                              item_json,
                                              error_out)) {
      ok = 0;
    }
  }

  for (index = 0U; index < profile->preflight_call_count; index++) {
    free(call_ids[index]);
    free(item_ids[index]);
    free(results[index]);
  }
  free(call_ids);
  free(item_ids);
  free(results);
  return ok;
}

static int strappy_responses_append_initial_items(
  strappy_responses_owned_items *items,
  const char *prompt,
  const char *session_db_path,
  long long session_id,
  const char *resource_dir,
  const char *prompt_group_key,
  const strappy_assistant_set_profile *profile,
  int should_run_preflight,
  char **error_out)
{
  char *item_json;

  item_json = strappy_responses_message_item_json("user",
                                                  prompt,
                                                  error_out);
  if ((item_json == NULL) ||
      !strappy_responses_owned_items_append(items, item_json, error_out)) {
    return 0;
  }
  if (!should_run_preflight) {
    return 1;
  }

  item_json = strappy_responses_message_item_json(
    "assistant",
    profile->preflight_assistant_text,
    error_out);
  if ((item_json == NULL) ||
      !strappy_responses_owned_items_append(items, item_json, error_out)) {
    return 0;
  }

  return strappy_responses_append_preflight_items(items,
                                                   session_db_path,
                                                   session_id,
                                                   resource_dir,
                                                   prompt_group_key,
                                                   profile,
                                                   error_out);
}

static char *strappy_responses_tool_error_output(const char *message,
                                                 char **error_out)
{
  strappy_responses_buffer buffer;
  const char *detail;

  buffer.data = NULL;
  buffer.length = 0U;
  detail = ((message != NULL) && (message[0] != '\0')) ?
    message : "Tool failed.";
  if (!strappy_responses_buffer_append_string(&buffer, "Error: ") ||
      !strappy_responses_buffer_append_string(&buffer, detail)) {
    strappy_responses_buffer_destroy(&buffer);
    strappy_set_error(error_out,
                      "Could not build Responses tool error output.");
    return NULL;
  }
  return buffer.data;
}

static char *strappy_responses_prompt_group_key(long long session_id)
{
  char buffer[128];
  int written;

  written = snprintf(buffer,
                     sizeof(buffer),
                     "responses-%lld-%lld",
                     session_id,
                     strappy_responses_now_ms());
  if ((written < 0) || ((size_t)written >= sizeof(buffer))) {
    return NULL;
  }
  return strappy_string_duplicate(buffer);
}

static char *strappy_responses_session_key(long long session_id)
{
  char buffer[96];
  int written;

  written = snprintf(buffer,
                     sizeof(buffer),
                     "strappy-session-%lld",
                     session_id);
  if ((written < 0) || ((size_t)written >= sizeof(buffer))) {
    return NULL;
  }
  return strappy_string_duplicate(buffer);
}

static char *strappy_responses_build_request_json(
  const strappy_config *config,
  const char *instructions,
  long long session_id,
  const char *prompt_group_key,
  long round_index,
  const char *tools_json,
  const strappy_response_item_raw_record_list *history,
  const strappy_responses_owned_items *new_items,
  long *new_input_start_index_out,
  char **error_out)
{
  strappy_responses_buffer buffer;
  char round_text[64];
  char *model_json;
  char *instructions_json;
  char *session_key;
  char *session_key_json;
  char *prompt_group_json;
  char *round_json;
  size_t index;
  int written;
  int ok;

  if ((config == NULL) || (config->api_model == NULL) ||
      (instructions == NULL) || (prompt_group_key == NULL) ||
      (tools_json == NULL) || (history == NULL) || (new_items == NULL)) {
    strappy_set_error(error_out, "Responses request builder is incomplete.");
    return NULL;
  }
  if (new_input_start_index_out != NULL) {
    *new_input_start_index_out = (long)history->count;
  }

  written = snprintf(round_text, sizeof(round_text), "%ld", round_index);
  if ((written < 0) || ((size_t)written >= sizeof(round_text))) {
    strappy_set_error(error_out, "Responses round number is invalid.");
    return NULL;
  }

  model_json = strappy_responses_json_string(config->api_model);
  instructions_json = strappy_responses_json_string(instructions);
  session_key = strappy_responses_session_key(session_id);
  session_key_json = strappy_responses_json_string(session_key);
  prompt_group_json = strappy_responses_json_string(prompt_group_key);
  round_json = strappy_responses_json_string(round_text);
  free(session_key);
  if ((model_json == NULL) || (instructions_json == NULL) ||
      (session_key_json == NULL) || (prompt_group_json == NULL) ||
      (round_json == NULL)) {
    free(model_json);
    free(instructions_json);
    free(session_key_json);
    free(prompt_group_json);
    free(round_json);
    strappy_set_error(error_out,
                      "Could not allocate Responses request fields.");
    return NULL;
  }

  memset(&buffer, 0, sizeof(buffer));
  ok = strappy_responses_buffer_append_string(&buffer, "{\"model\":") &&
       strappy_responses_buffer_append_string(&buffer, model_json) &&
       strappy_responses_buffer_append_string(
         &buffer,
         ",\"stream\":false,\"store\":false,\"session_id\":") &&
       strappy_responses_buffer_append_string(&buffer, session_key_json) &&
       strappy_responses_buffer_append_string(
         &buffer,
         ",\"metadata\":{\"strappy_session_id\":") &&
       strappy_responses_buffer_append_string(&buffer, session_key_json) &&
       strappy_responses_buffer_append_string(
         &buffer,
         ",\"strappy_prompt_group_key\":") &&
       strappy_responses_buffer_append_string(&buffer, prompt_group_json) &&
       strappy_responses_buffer_append_string(&buffer, ",\"strappy_round\":") &&
       strappy_responses_buffer_append_string(&buffer, round_json) &&
       strappy_responses_buffer_append_string(&buffer, "},\"instructions\":") &&
       strappy_responses_buffer_append_string(&buffer, instructions_json) &&
       strappy_responses_buffer_append_string(&buffer, ",\"input\":[");
  free(model_json);
  free(instructions_json);
  free(session_key_json);
  free(prompt_group_json);
  free(round_json);

  for (index = 0U; ok && (index < history->count); index++) {
    if (index > 0U) {
      ok = strappy_responses_buffer_append_string(&buffer, ",");
    }
    if (ok) {
      ok = strappy_responses_buffer_append_string(
        &buffer,
        history->records[index].raw_json);
    }
  }
  for (index = 0U; ok && (index < new_items->count); index++) {
    if ((history->count > 0U) || (index > 0U)) {
      ok = strappy_responses_buffer_append_string(&buffer, ",");
    }
    if (ok) {
      ok = strappy_responses_buffer_append_string(&buffer,
                                                  new_items->items[index]);
    }
  }

  ok = ok && strappy_responses_buffer_append_string(
    &buffer,
    "],\"include\":[\"reasoning.encrypted_content\"],"
    "\"reasoning\":{\"enabled\":true,\"summary\":\"auto\"},"
    "\"parallel_tool_calls\":true,\"tool_choice\":\"") &&
    strappy_responses_buffer_append_string(
      &buffer,
      "auto") &&
    strappy_responses_buffer_append_string(&buffer, "\",\"tools\":") &&
    strappy_responses_buffer_append_string(&buffer, tools_json);
  ok = ok && strappy_responses_buffer_append_string(&buffer, "}");
  if (!ok) {
    strappy_responses_buffer_destroy(&buffer);
    strappy_set_error(error_out,
                      "Could not allocate Responses request JSON.");
    return NULL;
  }
  return buffer.data;
}

typedef struct strappy_responses_runtime {
  strappy_config config;
  strappy_assistant_set_profile assistant_set;
  char *system_prompt;
  char *tools_json;
  strappy_responses_audit audit;
  char *request_url;
  int is_first_user_prompt;
} strappy_responses_runtime;

static void strappy_responses_runtime_init(strappy_responses_runtime *runtime)
{
  if (runtime == NULL) {
    return;
  }
  strappy_config_init(&runtime->config);
  strappy_assistant_set_profile_init(&runtime->assistant_set);
  runtime->system_prompt = NULL;
  runtime->tools_json = NULL;
  strappy_responses_audit_reset(&runtime->audit);
  runtime->request_url = NULL;
  runtime->is_first_user_prompt = 0;
}

static void strappy_responses_runtime_destroy(
  strappy_responses_runtime *runtime)
{
  if (runtime == NULL) {
    return;
  }
  strappy_config_destroy(&runtime->config);
  strappy_assistant_set_profile_destroy(&runtime->assistant_set);
  free(runtime->system_prompt);
  free(runtime->tools_json);
  free(runtime->request_url);
  strappy_responses_runtime_init(runtime);
}

static void strappy_responses_profile_remove_tool(
  strappy_assistant_set_profile *profile,
  const char *tool_name)
{
  size_t index;

  if ((profile == NULL) || (tool_name == NULL)) {
    return;
  }
  for (index = 0U; index < profile->tool_name_count; index++) {
    if (strcmp(profile->tool_names[index], tool_name) == 0) {
      free(profile->tool_names[index]);
      if ((index + 1U) < profile->tool_name_count) {
        memmove(&profile->tool_names[index],
                &profile->tool_names[index + 1U],
                (profile->tool_name_count - index - 1U) *
                  sizeof(profile->tool_names[0]));
      }
      profile->tool_name_count--;
      profile->tool_names[profile->tool_name_count] = NULL;
      return;
    }
  }
}

static int strappy_responses_validate_assistant_set(
  strappy_responses_runtime *runtime,
  char **error_out)
{
  size_t index;

  if (runtime == NULL) {
    strappy_set_error(error_out, "Responses assistant set is missing.");
    return 0;
  }
  if (!strappy_assistant_set_profile_is_available(&runtime->assistant_set)) {
    strappy_set_formatted_error(
      error_out,
      "Assistant set is not available: %s",
      (runtime->assistant_set.identifier != NULL) ?
        runtime->assistant_set.identifier : "(unknown)");
    return 0;
  }
  for (index = 0U;
       index < runtime->assistant_set.tool_name_count;
       index++) {
    if (!strappy_tools_is_registered(runtime->assistant_set.tool_names[index])) {
      strappy_set_formatted_error(
        error_out,
        "Assistant-set tool is not registered: %s",
        runtime->assistant_set.tool_names[index]);
      return 0;
    }
  }
  if ((runtime->assistant_set.preflight_when == NULL) ||
      (strcmp(runtime->assistant_set.preflight_when,
              STRAPPY_ASSISTANT_SET_PREFLIGHT_FIRST_USER_PROMPT) != 0) ||
      (runtime->assistant_set.preflight_assistant_text == NULL) ||
      (runtime->assistant_set.preflight_assistant_text[0] == '\0')) {
    strappy_set_error(error_out,
                      "Assistant-set preflight configuration is invalid.");
    return 0;
  }
  for (index = 0U;
       index < runtime->assistant_set.preflight_call_count;
       index++) {
    const char *tool_name;

    tool_name = runtime->assistant_set.preflight_calls[index].tool_name;
    if (!strappy_tools_is_registered(tool_name) ||
        strappy_tools_is_server(tool_name) ||
        (runtime->assistant_set.preflight_calls[index].arguments_json == NULL) ||
        !strappy_assistant_set_profile_allows_tool(&runtime->assistant_set,
                                                   tool_name)) {
      strappy_set_formatted_error(error_out,
                                  "Assistant-set preflight tool is invalid: %s",
                                  tool_name);
      return 0;
    }
  }
  return strappy_responses_audit_init(&runtime->audit,
                                      &runtime->assistant_set,
                                      error_out);
}

static int strappy_responses_prepare_runtime(
  strappy_responses_runtime *runtime,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *guidance_resource_dir,
  const char *session_db_path,
  long long session_id,
  char **error_out)
{
  strappy_session_record session;
  char *model;
  char *assistant_set_id;
  int bash_enabled;
  int ok;

  if ((runtime == NULL) || (guidance_resource_dir == NULL) ||
      (guidance_resource_dir[0] == '\0') ||
      (session_db_path == NULL) || (session_id <= 0LL)) {
    strappy_set_error(error_out,
                      "Responses runtime configuration is incomplete.");
    return 0;
  }
  strappy_responses_runtime_init(runtime);
  strappy_session_record_init(&session);
  model = NULL;
  assistant_set_id = NULL;

  ok = strappy_config_load_with_fallback_credentials(
    &runtime->config,
    env_path,
    fallback_api_endpoint,
    fallback_api_token,
    error_out);
  if (!ok) {
    return 0;
  }
  if (!strappy_db_load_session(session_db_path,
                               session_id,
                               &session,
                               error_out)) {
    strappy_responses_runtime_destroy(runtime);
    return 0;
  }
  runtime->config.web_provider = session.web_provider;
  bash_enabled = session.bash_enabled ? 1 : 0;
  runtime->is_first_user_prompt =
    (session.prompt == NULL) || (session.prompt[0] == '\0');
  assistant_set_id = strappy_string_duplicate(session.assistant_set_id);
  strappy_session_record_destroy(&session);
  if (assistant_set_id == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate session assistant set.");
    strappy_responses_runtime_destroy(runtime);
    return 0;
  }

  if (!strappy_db_get_session_model(session_db_path,
                                    session_id,
                                    &model,
                                    error_out) ||
      !strappy_config_set_api_model(&runtime->config, model, error_out)) {
    free(model);
    free(assistant_set_id);
    strappy_responses_runtime_destroy(runtime);
    return 0;
  }
  free(model);

  if (!strappy_config_set_guidance_resource_dir(&runtime->config,
                                                guidance_resource_dir,
                                                error_out)) {
    free(assistant_set_id);
    strappy_responses_runtime_destroy(runtime);
    return 0;
  }

  if (!strappy_assistant_sets_load_profile(
        runtime->config.guidance_resource_dir,
        assistant_set_id,
        &runtime->assistant_set,
        error_out)) {
    free(assistant_set_id);
    strappy_responses_runtime_destroy(runtime);
    return 0;
  }
  free(assistant_set_id);
  if (!strappy_responses_validate_assistant_set(runtime, error_out)) {
    strappy_responses_runtime_destroy(runtime);
    return 0;
  }
  if (!bash_enabled) {
    strappy_responses_profile_remove_tool(&runtime->assistant_set,
                                          STRAPPY_TOOL_BASH);
  }
  runtime->config.tool_allowlist =
    (const char * const *)runtime->assistant_set.tool_names;
  runtime->config.tool_allowlist_count = runtime->assistant_set.tool_name_count;
  runtime->system_prompt =
    strappy_prompt_build(runtime->config.guidance_resource_dir,
                         &runtime->assistant_set,
                         runtime->config.web_provider,
                         error_out);
  if (runtime->system_prompt == NULL) {
    strappy_responses_runtime_destroy(runtime);
    return 0;
  }
  runtime->tools_json =
    strappy_tools_responses_request_json_filtered(
      runtime->config.guidance_resource_dir,
      runtime->config.tool_allowlist,
      runtime->config.tool_allowlist_count,
      runtime->config.web_provider,
      error_out);
  if (runtime->tools_json == NULL) {
    strappy_responses_runtime_destroy(runtime);
    return 0;
  }
  runtime->request_url =
    strappy_client_build_responses_url(runtime->config.api_endpoint);
  if (runtime->request_url == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate Responses request URL.");
    strappy_responses_runtime_destroy(runtime);
    return 0;
  }
  return 1;
}

static int strappy_responses_status_is_retryable(long http_status)
{
  return (http_status == 408L) || (http_status == 409L) ||
    (http_status == 425L) || (http_status == 429L) ||
    ((http_status >= 500L) && (http_status <= 599L));
}

static int strappy_responses_error_type_is_retryable(const char *type)
{
  if ((type == NULL) || (type[0] == '\0')) {
    return 0;
  }
  return (strcmp(type, "rate_limit_exceeded") == 0) ||
    (strcmp(type, "provider_overloaded") == 0) ||
    (strcmp(type, "provider_unavailable") == 0) ||
    (strcmp(type, "timeout") == 0) ||
    (strcmp(type, "server") == 0) ||
    (strcmp(type, "server_error") == 0);
}

static int strappy_responses_http_is_success(
  const strappy_responses_http_result *http,
  const strappy_responses_analysis *analysis)
{
  if ((http == NULL) || (analysis == NULL) ||
      http->cancelled || (http->transport_error != NULL) ||
      (http->http_status < 200L) ||
      (http->http_status >= 300L) || !analysis->valid_response ||
      analysis->has_api_error) {
    return 0;
  }
  if ((analysis->status == NULL) || (analysis->status[0] == '\0')) {
    return 1;
  }
  return strcmp(analysis->status, "completed") == 0;
}

static int strappy_responses_output_is_canonical(
  const strappy_responses_http_result *http,
  const strappy_responses_analysis *analysis)
{
  if ((http == NULL) || (analysis == NULL) ||
      http->cancelled || (http->transport_error != NULL) ||
      (http->http_status < 200L) ||
      (http->http_status >= 300L) || !analysis->valid_response ||
      analysis->has_api_error) {
    return 0;
  }
  return (analysis->status == NULL) ||
    ((strcmp(analysis->status, "failed") != 0) &&
     (strcmp(analysis->status, "cancelled") != 0));
}

static int strappy_responses_should_retry(
  const strappy_responses_http_result *http,
  const strappy_responses_analysis *analysis)
{
  if (http == NULL) {
    return 0;
  }
  if (http->cancelled) {
    return 0;
  }
  if (http->transport_error != NULL) {
    return 1;
  }
  if (strappy_responses_status_is_retryable(http->http_status)) {
    return 1;
  }
  if ((http->http_status >= 200L) && (http->http_status < 300L) &&
      ((analysis == NULL) || !analysis->valid_json ||
       !analysis->valid_response)) {
    return 1;
  }
  return (analysis != NULL) &&
    strappy_responses_error_type_is_retryable(analysis->error_type);
}

static const char *strappy_responses_call_state(
  const strappy_responses_http_result *http,
  const strappy_responses_analysis *analysis,
  int client_ok)
{
  if ((http != NULL) && http->cancelled) {
    return "cancelled";
  }
  if (!client_ok) {
    return "client_error";
  }
  if ((http != NULL) && (http->transport_error != NULL)) {
    return "transport_error";
  }
  if ((http == NULL) || (http->http_status < 200L) ||
      (http->http_status >= 300L)) {
    return "http_error";
  }
  if ((analysis == NULL) || !analysis->valid_json ||
      !analysis->valid_response) {
    return "invalid_json";
  }
  if (analysis->has_api_error ||
      ((analysis->status != NULL) &&
       (strcmp(analysis->status, "completed") != 0))) {
    return "response_error";
  }
  return "completed";
}

static int strappy_responses_finish_call(
  const char *session_db_path,
  long long call_id,
  const strappy_responses_http_result *http,
  const strappy_responses_analysis *analysis,
  int client_ok,
  int output_is_canonical,
  const strappy_answer_quality_audit_input *answer_quality_audit,
  char **error_out)
{
  strappy_response_call_finish_input input;

  memset(&input, 0, sizeof(input));
  input.call_id = call_id;
  input.state = strappy_responses_call_state(http, analysis, client_ok);
  input.is_error = (strcmp(input.state, "completed") != 0) ? 1 : 0;
  input.output_is_canonical = output_is_canonical;
  input.answer_quality_audit = answer_quality_audit;
  if (http != NULL) {
    input.http_status = http->http_status;
    input.retry_after_seconds = http->retry_after_seconds;
    input.curl_code = http->curl_code;
    input.started_at_ms = http->started_at_ms;
    input.completed_at_ms = http->completed_at_ms;
    input.request_bytes = http->request_bytes;
    input.response_bytes = http->response_bytes;
    input.name_lookup_seconds = http->name_lookup_seconds;
    input.connect_seconds = http->connect_seconds;
    input.start_transfer_seconds = http->start_transfer_seconds;
    input.total_seconds = http->total_seconds;
    input.effective_url = http->effective_url;
    input.transport_error = http->transport_error;
    input.content_type = http->content_type;
    input.request_id = http->request_id;
    input.generation_id = http->generation_id;
    input.rate_limit_limit = http->rate_limit_limit;
    input.rate_limit_remaining = http->rate_limit_remaining;
    input.rate_limit_reset = http->rate_limit_reset;
    input.rate_limit_limit_requests = http->rate_limit_limit_requests;
    input.rate_limit_remaining_requests = http->rate_limit_remaining_requests;
    input.rate_limit_reset_requests = http->rate_limit_reset_requests;
    input.rate_limit_limit_tokens = http->rate_limit_limit_tokens;
    input.rate_limit_remaining_tokens = http->rate_limit_remaining_tokens;
    input.rate_limit_reset_tokens = http->rate_limit_reset_tokens;
    input.response_headers = (http->response_headers != NULL) ?
      http->response_headers : "";
    input.response_json = (http->response_json != NULL) ?
      http->response_json : "";
  } else {
    input.response_headers = "";
    input.response_json = "";
  }
  return strappy_db_finish_response_call(session_db_path, &input, error_out);
}

static void strappy_responses_log_call_started(
  long long session_id,
  long long call_id,
  long round_index,
  long attempt_index,
  const char *request_kind,
  const char *request_json)
{
  syslog(LOG_NOTICE,
         "StrappyResponses request_started session=%lld call=%lld "
         "round=%ld attempt=%ld kind=%s request_bytes=%lu",
         session_id,
         call_id,
         round_index,
         attempt_index,
         (request_kind != NULL) ? request_kind : "unknown",
         (unsigned long)((request_json != NULL) ? strlen(request_json) : 0U));
}

static void strappy_responses_call_did_finish(
  long long session_id,
  long long call_id,
  const char *prompt_group_key,
  long round_index,
  long attempt_index,
  const strappy_responses_http_result *http,
  const strappy_responses_analysis *analysis,
  int client_ok,
  strappy_responses_event_callback callback,
  void *callback_data)
{
  strappy_responses_event event;
  const char *state;
  char message_key[64];

  state = strappy_responses_call_state(http, analysis, client_ok);
  syslog(LOG_NOTICE,
         "StrappyResponses request_finished session=%lld call=%lld "
         "round=%ld attempt=%ld state=%s http=%ld curl=%ld "
         "response_bytes=%lld elapsed=%.3f",
         session_id,
         call_id,
         round_index,
         attempt_index,
         state,
         (http != NULL) ? http->http_status : 0L,
         (http != NULL) ? http->curl_code : 0L,
         (http != NULL) ? http->response_bytes : 0LL,
         (http != NULL) ? http->total_seconds : 0.0);

  if (callback == NULL) {
    return;
  }

  snprintf(message_key,
           sizeof(message_key),
           "response-call-%lld",
           call_id);
  memset(&event, 0, sizeof(event));
  event.type = STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED;
  event.prompt_group_key = prompt_group_key;
  event.actor = "api";
  event.kind = "response_api_call";
  event.message_key = message_key;
  event.render_role = (strcmp(state, "completed") == 0) ?
    "api_call" : "api_error";
  event.status_kind = state;
  if (http != NULL) {
    event.status_started_ms = http->started_at_ms;
    event.status_updated_ms = http->completed_at_ms;
  }
  (void)callback(&event, callback_data);
}

static char *strappy_responses_failure_message(
  const strappy_responses_http_result *http,
  const strappy_responses_analysis *analysis)
{
  char buffer[1024];
  int written;

  if ((http != NULL) && http->cancelled) {
    written = snprintf(buffer,
                       sizeof(buffer),
                       "Responses request was cancelled.");
  } else if ((http != NULL) && (http->transport_error != NULL)) {
    written = snprintf(buffer,
                       sizeof(buffer),
                       "Responses request failed: %s",
                       http->transport_error);
  } else if ((analysis != NULL) && (analysis->error_message != NULL)) {
    written = snprintf(buffer,
                       sizeof(buffer),
                       "Responses API failed: %s",
                       analysis->error_message);
  } else if ((http != NULL) &&
             ((http->http_status < 200L) || (http->http_status >= 300L))) {
    written = snprintf(buffer,
                       sizeof(buffer),
                       "Responses API failed with HTTP %ld.",
                       http->http_status);
  } else if ((analysis == NULL) || !analysis->valid_json) {
    written = snprintf(buffer,
                       sizeof(buffer),
                       "Responses API returned invalid JSON.");
  } else if (!analysis->valid_response) {
    written = snprintf(buffer,
                       sizeof(buffer),
                       "Responses API returned an invalid response object.");
  } else if ((analysis->status != NULL) &&
             (analysis->status[0] != '\0')) {
    written = snprintf(buffer,
                       sizeof(buffer),
                       "Responses API finished with status %s.",
                       analysis->status);
  } else {
    written = snprintf(buffer, sizeof(buffer), "Responses API failed.");
  }
  if ((written < 0) || ((size_t)written >= sizeof(buffer))) {
    return strappy_string_duplicate("Responses API failed.");
  }
  return strappy_string_duplicate(buffer);
}

static long strappy_responses_retry_delay_ms(
  const strappy_responses_http_result *http,
  long fallback_delay_ms)
{
  long delay;

  delay = fallback_delay_ms;
  if ((http != NULL) && (http->retry_after_seconds > 0L)) {
    if (http->retry_after_seconds >=
        (STRAPPY_RESPONSES_MAX_RETRY_DELAY_MS / 1000L)) {
      delay = STRAPPY_RESPONSES_MAX_RETRY_DELAY_MS;
    } else {
      delay = http->retry_after_seconds * 1000L;
    }
  }
  if (delay > STRAPPY_RESPONSES_MAX_RETRY_DELAY_MS) {
    delay = STRAPPY_RESPONSES_MAX_RETRY_DELAY_MS;
  }
  return delay;
}

static int strappy_responses_execute_tool_calls(
  const char *session_db_path,
  long long session_id,
  const char *resource_dir,
  long long response_call_id,
  const strappy_responses_analysis *analysis,
  strappy_responses_audit *audit,
  strappy_responses_owned_items *outputs,
  strappy_responses_event_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_responses_tool_continue_context continue_context;
  size_t index;

  continue_context.callback = callback;
  continue_context.callback_data = callback_data;
  strappy_responses_owned_items_init(outputs);
  for (index = 0U; index < analysis->tool_call_count; index++) {
    const strappy_responses_tool_call *call;
    strappy_response_tool_execution_input execution;
    char *tool_error;
    char *output;
    char *item_json;
    long long started_at_ms;
    long long completed_at_ms;
    int output_truncated;
    int tool_cancelled;
    int tool_succeeded;

    call = &analysis->tool_calls[index];
    tool_error = NULL;
    output_truncated = 0;
    tool_cancelled = 0;
    started_at_ms = strappy_responses_now_ms();
    output = strappy_tools_execute_for_function_call_with_cancellation(
      session_db_path,
      session_id,
      resource_dir,
      call->call_id,
      call->name,
      call->arguments,
      strappy_responses_tool_should_continue,
      &continue_context,
      &output_truncated,
      &tool_cancelled,
      &tool_error);
    completed_at_ms = strappy_responses_now_ms();
    tool_succeeded = (output != NULL) ? 1 : 0;
    if (output == NULL) {
      output = strappy_responses_tool_error_output(
        (tool_error != NULL) ? tool_error : "Tool failed.",
        error_out);
      if (output == NULL) {
        free(tool_error);
        strappy_responses_owned_items_destroy(outputs);
        return 0;
      }
      if (strcmp(call->name, STRAPPY_TOOL_BASH) == 0) {
        char *bash_error_output;

        bash_error_output = strappy_bash_result_json(
          output,
          output_truncated,
          error_out);
        free(output);
        output = bash_error_output;
        if (output == NULL) {
          free(tool_error);
          strappy_responses_owned_items_destroy(outputs);
          return 0;
        }
      }
    }

    memset(&execution, 0, sizeof(execution));
    execution.session_id = session_id;
    execution.response_call_id = response_call_id;
    execution.output_index = call->output_index;
    execution.call_id = call->call_id;
    execution.tool_name = call->name;
    execution.arguments_json = call->arguments;
    execution.status = tool_cancelled ? "cancelled" :
      (tool_succeeded ? "completed" : "error");
    execution.output_json = output;
    execution.error_text = tool_error;
    execution.started_at_ms = started_at_ms;
    execution.completed_at_ms = completed_at_ms;
    if (!strappy_db_save_response_tool_execution(session_db_path,
                                                 &execution,
                                                 error_out)) {
      free(output);
      free(tool_error);
      strappy_responses_owned_items_destroy(outputs);
      return 0;
    }
    if (tool_succeeded) {
      strappy_responses_audit_record_completed_tool(audit, call->name);
      strappy_responses_audit_record_activity(audit, call->name);
    }
    item_json = strappy_responses_function_output_item_json(call->call_id,
                                                            output,
                                                            error_out);
    free(output);
    free(tool_error);
    if ((item_json == NULL) ||
        !strappy_responses_owned_items_append(outputs,
                                              item_json,
                                              error_out)) {
      strappy_responses_owned_items_destroy(outputs);
      return 0;
    }
    if (tool_cancelled) {
      if (!strappy_db_finalize_cancelled_response_tool_outputs(
            session_db_path,
            session_id,
            response_call_id,
            (const char * const *)outputs->items,
            outputs->count,
            error_out)) {
        strappy_responses_owned_items_destroy(outputs);
        return 0;
      }
      strappy_responses_owned_items_destroy(outputs);
      strappy_set_error(error_out, "Responses request was cancelled.");
      return 0;
    }
  }
  return 1;
}

static int strappy_responses_send_round(
  strappy_responses_runtime *runtime,
  const char *session_db_path,
  long long session_id,
  const char *prompt_group_key,
  long round_index,
  const char *request_kind,
  const char *request_json,
  long new_input_start_index,
  long long *previous_call_id_io,
  long long *successful_call_id_out,
  long long processing_started_ms,
  strappy_responses_http_result *http_out,
  strappy_responses_analysis *analysis_out,
  strappy_responses_event_callback callback,
  void *callback_data,
  char **error_out)
{
  static const char *request_headers_json =
    "{\"Content-Type\":\"application/json\","
    "\"Accept\":\"application/json\","
    "\"X-OpenRouter-Title\":\"Strappy\","
    "\"X-OpenRouter-Metadata\":\"enabled\","
    "\"Authorization\":\"Bearer [REDACTED]\"}";
  long attempt_index;
  long retry_delay_ms;

  strappy_responses_http_result_init(http_out);
  strappy_responses_analysis_init(analysis_out);
  if (successful_call_id_out != NULL) {
    *successful_call_id_out = 0LL;
  }
  retry_delay_ms = STRAPPY_RESPONSES_INITIAL_RETRY_DELAY_MS;

  for (attempt_index = 0L;
       attempt_index < STRAPPY_RESPONSES_MAX_ATTEMPTS;
       attempt_index++) {
    strappy_response_call_begin_input begin;
    strappy_responses_http_result http;
    strappy_responses_analysis analysis;
    char *failure;
    long long call_id;
    int client_ok;
    int analyze_ok;
    int should_retry;
    int response_ok;
    int output_is_canonical;
    strappy_answer_quality_audit_input answer_quality;
    const strappy_answer_quality_audit_input *answer_quality_to_store;

    memset(&answer_quality, 0, sizeof(answer_quality));
    answer_quality_to_store = NULL;

    memset(&begin, 0, sizeof(begin));
    begin.session_id = session_id;
    begin.previous_call_id = (previous_call_id_io != NULL) ?
      *previous_call_id_io : 0LL;
    begin.prompt_group_key = prompt_group_key;
    begin.request_kind = (attempt_index == 0L) ? request_kind : "retry";
    begin.round_index = round_index;
    begin.attempt_index = attempt_index;
    begin.new_input_start_index = (attempt_index == 0L) ?
      new_input_start_index : -1L;
    begin.request_method = "POST";
    begin.request_url = runtime->request_url;
    begin.request_headers_json = request_headers_json;
    begin.request_json = request_json;
    if (!strappy_db_begin_response_call(session_db_path,
                                        &begin,
                                        &call_id,
                                        error_out)) {
      return 0;
    }
    strappy_responses_log_call_started(session_id,
                                       call_id,
                                       round_index,
                                       attempt_index,
                                       begin.request_kind,
                                       request_json);

    strappy_responses_http_result_init(&http);
    strappy_responses_analysis_init(&analysis);
    client_ok = strappy_client_send_responses_json(
      &runtime->config,
      request_json,
      &http,
      callback,
      callback_data,
      error_out);
    analyze_ok = (client_ok && !http.cancelled) ?
      strappy_responses_analyze_json(http.response_json,
                                     &analysis,
                                     error_out) : client_ok;
    if (client_ok && !analyze_ok) {
      if (strappy_responses_finish_call(session_db_path,
                                        call_id,
                                        &http,
                                        &analysis,
                                        0,
                                        0,
                                        NULL,
                                        NULL)) {
        strappy_responses_call_did_finish(session_id,
                                          call_id,
                                          prompt_group_key,
                                          round_index,
                                          attempt_index,
                                          &http,
                                          &analysis,
                                          0,
                                          callback,
                                          callback_data);
      }
      *http_out = http;
      *analysis_out = analysis;
      return 0;
    }

    should_retry = client_ok &&
      ((attempt_index + 1L) < STRAPPY_RESPONSES_MAX_ATTEMPTS) &&
      strappy_responses_should_retry(&http, &analysis);
    output_is_canonical = !should_retry &&
      strappy_responses_output_is_canonical(&http, &analysis);
    response_ok = client_ok &&
      strappy_responses_http_is_success(&http, &analysis);
    if (output_is_canonical && response_ok) {
      size_t activity_index;

      for (activity_index = 0U;
           activity_index < analysis.tool_activity_count;
           activity_index++) {
        strappy_responses_audit_record_activity(
          &runtime->audit,
          analysis.tool_activity_names[activity_index]);
      }
      if (analysis.tool_call_count == 0U) {
        if (!strappy_responses_audit_evaluate(&runtime->audit,
                                              analysis.response_text,
                                              &answer_quality,
                                              error_out)) {
          *http_out = http;
          *analysis_out = analysis;
          return 0;
        }
        answer_quality.evaluated_at_ms = strappy_responses_now_ms();
        answer_quality_to_store = &answer_quality;
      }
    }
    if (!strappy_responses_finish_call(session_db_path,
                                       call_id,
                                       &http,
                                       &analysis,
                                       client_ok,
                                       output_is_canonical,
                                       answer_quality_to_store,
                                       error_out)) {
      strappy_responses_answer_quality_destroy(&answer_quality);
      *http_out = http;
      *analysis_out = analysis;
      return 0;
    }
    strappy_responses_answer_quality_destroy(&answer_quality);
    strappy_responses_call_did_finish(session_id,
                                      call_id,
                                      prompt_group_key,
                                      round_index,
                                      attempt_index,
                                      &http,
                                      &analysis,
                                      client_ok,
                                      callback,
                                      callback_data);
    if (previous_call_id_io != NULL) {
      *previous_call_id_io = call_id;
    }

    if (!client_ok) {
      *http_out = http;
      *analysis_out = analysis;
      return 0;
    }
    if (should_retry) {
      long delay;
      char retry_reason_buffer[64];
      const char *retry_reason;
      unsigned int next_attempt;

      delay = strappy_responses_retry_delay_ms(&http, retry_delay_ms);
      retry_reason = strappy_responses_retry_status_reason(
        &http,
        &analysis,
        retry_reason_buffer,
        sizeof(retry_reason_buffer));
      next_attempt = (unsigned int)(attempt_index + 2L);
      strappy_responses_emit_processing_status(
        callback,
        callback_data,
        prompt_group_key,
        1,
        "retry_wait",
        processing_started_ms,
        delay,
        next_attempt,
        retry_reason);
      if (!strappy_responses_sleep_ms(delay, callback, callback_data)) {
        strappy_set_error(error_out, "Responses request was cancelled.");
        *http_out = http;
        *analysis_out = analysis;
        return 0;
      }
      strappy_responses_analysis_destroy(&analysis);
      strappy_responses_http_result_destroy(&http);
      strappy_responses_emit_processing_status(
        callback,
        callback_data,
        prompt_group_key,
        1,
        "retrying",
        processing_started_ms,
        0L,
        next_attempt,
        NULL);
      if (retry_delay_ms <=
          (STRAPPY_RESPONSES_MAX_RETRY_DELAY_MS / 2L)) {
        retry_delay_ms *= 2L;
      }
      continue;
    }

    if (!response_ok) {
      failure = strappy_responses_failure_message(&http, &analysis);
      if (failure != NULL) {
        strappy_set_error(error_out, failure);
      } else {
        strappy_set_error(error_out, "Responses API failed.");
      }
      free(failure);
      *http_out = http;
      *analysis_out = analysis;
      return 0;
    }

    *http_out = http;
    *analysis_out = analysis;
    if (successful_call_id_out != NULL) {
      *successful_call_id_out = call_id;
    }
    return 1;
  }

  strappy_set_error(error_out, "Responses API retry limit was reached.");
  return 0;
}

static int strappy_responses_replace_string(char **target,
                                            const char *value,
                                            char **error_out)
{
  char *copy;

  if ((target == NULL) || (value == NULL)) {
    return 1;
  }
  copy = strappy_string_duplicate(value);
  if (copy == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate Responses result text.");
    return 0;
  }
  free(*target);
  *target = copy;
  return 1;
}

static int strappy_responses_text_has_non_whitespace(const char *text)
{
  const unsigned char *cursor;

  if (text == NULL) {
    return 0;
  }
  cursor = (const unsigned char *)text;
  while (*cursor != '\0') {
    if (!isspace(*cursor)) {
      return 1;
    }
    cursor++;
  }
  return 0;
}

static void strappy_responses_update_failure_summary(
  const char *session_db_path,
  long long session_id,
  const char *prompt,
  const char *model,
  long http_status,
  const char *message)
{
  char *ignored_error;

  ignored_error = NULL;
  strappy_db_update_response_session_summary(
    session_db_path,
    session_id,
    prompt,
    (message != NULL) ? message : "Responses API failed.",
    model,
    http_status,
    &ignored_error);
  free(ignored_error);
}

char *strappy_responses_send_prompt_for_session_and_store_with_events(
  const char *prompt,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *guidance_resource_dir,
  const char *session_db_path,
  long long session_id,
  strappy_responses_event_callback callback,
  void *callback_data,
  char **error_out)
{
  strappy_responses_runtime runtime;
  strappy_responses_owned_items new_items;
  char *prompt_group_key;
  char *final_text;
  char *last_model;
  const char *next_request_kind;
  long long previous_call_id;
  long last_http_status;
  long round_index;
  long long processing_started_ms;
  int ok;

  if ((prompt == NULL) || (prompt[0] == '\0') ||
      (session_db_path == NULL) || (session_db_path[0] == '\0') ||
      (session_id <= 0LL)) {
    strappy_set_error(error_out, "Responses prompt request is incomplete.");
    return NULL;
  }

  strappy_responses_runtime_init(&runtime);
  strappy_responses_owned_items_init(&new_items);
  prompt_group_key = NULL;
  final_text = NULL;
  last_model = NULL;
  previous_call_id = 0LL;
  last_http_status = 0L;
  processing_started_ms = strappy_responses_now_ms();
  next_request_kind = "user";

  if (!strappy_responses_prepare_runtime(&runtime,
                                         env_path,
                                         fallback_api_endpoint,
                                         fallback_api_token,
                                         guidance_resource_dir,
                                         session_db_path,
                                         session_id,
                                         error_out)) {
    return NULL;
  }
  prompt_group_key = strappy_responses_prompt_group_key(session_id);
  last_model = strappy_string_duplicate(runtime.config.api_model);
  if ((prompt_group_key == NULL) || (last_model == NULL) ||
      !strappy_responses_append_initial_items(
        &new_items,
        prompt,
        session_db_path,
        session_id,
        runtime.config.guidance_resource_dir,
        prompt_group_key,
        &runtime.assistant_set,
        runtime.is_first_user_prompt,
        error_out)) {
    free(prompt_group_key);
    free(last_model);
    strappy_responses_owned_items_destroy(&new_items);
    strappy_responses_runtime_destroy(&runtime);
    if ((error_out == NULL) || (*error_out == NULL)) {
      strappy_set_error(error_out,
                        "Could not allocate Responses prompt request.");
    }
    return NULL;
  }

  for (round_index = 0L;
       round_index < STRAPPY_RESPONSES_MAX_ROUNDS;
       round_index++) {
    strappy_response_item_raw_record_list history;
    strappy_responses_http_result http;
    strappy_responses_analysis analysis;
    char *request_json;
    long new_input_start_index;
    long long successful_call_id;

    strappy_responses_emit_processing_status(callback,
                                             callback_data,
                                             prompt_group_key,
                                             1,
                                             "thinking",
                                             processing_started_ms,
                                             0L,
                                             0U,
                                             NULL);
    strappy_response_item_raw_record_list_init(&history);
    strappy_responses_http_result_init(&http);
    strappy_responses_analysis_init(&analysis);
    if (!strappy_db_list_canonical_response_items(session_db_path,
                                                  session_id,
                                                  &history,
                                                  error_out)) {
      strappy_responses_update_failure_summary(
        session_db_path,
        session_id,
        prompt,
        last_model,
        last_http_status,
        (error_out != NULL) ? *error_out : NULL);
      break;
    }
    if (history.count > (size_t)LONG_MAX) {
      strappy_response_item_raw_record_list_destroy(&history);
      strappy_set_error(error_out,
                        "Responses conversation history is too large.");
      break;
    }

    request_json = strappy_responses_build_request_json(
      &runtime.config,
      runtime.system_prompt,
      session_id,
      prompt_group_key,
      round_index,
      runtime.tools_json,
      &history,
      &new_items,
      &new_input_start_index,
      error_out);
    strappy_response_item_raw_record_list_destroy(&history);
    strappy_responses_owned_items_destroy(&new_items);
    if (request_json == NULL) {
      break;
    }

    ok = strappy_responses_send_round(&runtime,
                                      session_db_path,
                                      session_id,
                                      prompt_group_key,
                                      round_index,
                                      next_request_kind,
                                      request_json,
                                      new_input_start_index,
                                      &previous_call_id,
                                      &successful_call_id,
                                      processing_started_ms,
                                      &http,
                                      &analysis,
                                      callback,
                                      callback_data,
                                      error_out);
    free(request_json);
    if (!ok) {
      last_http_status = http.http_status;
      strappy_responses_update_failure_summary(
        session_db_path,
        session_id,
        prompt,
        last_model,
        last_http_status,
        (error_out != NULL) ? *error_out : NULL);
      strappy_responses_analysis_destroy(&analysis);
      strappy_responses_http_result_destroy(&http);
      break;
    }

    last_http_status = http.http_status;
    if ((analysis.model != NULL) &&
        !strappy_responses_replace_string(&last_model,
                                          analysis.model,
                                          error_out)) {
      strappy_responses_analysis_destroy(&analysis);
      strappy_responses_http_result_destroy(&http);
      break;
    }

    if (analysis.tool_call_count > 0U) {
      strappy_responses_emit_processing_status(callback,
                                               callback_data,
                                               prompt_group_key,
                                               1,
                                               "tools",
                                               processing_started_ms,
                                               0L,
                                               0U,
                                               NULL);
      if (!strappy_responses_execute_tool_calls(
            session_db_path,
            session_id,
            runtime.config.guidance_resource_dir,
            successful_call_id,
            &analysis,
            &runtime.audit,
            &new_items,
            callback,
            callback_data,
            error_out)) {
        strappy_responses_analysis_destroy(&analysis);
        strappy_responses_http_result_destroy(&http);
        break;
      }
      next_request_kind = "tool_continuation";
      strappy_responses_analysis_destroy(&analysis);
      strappy_responses_http_result_destroy(&http);
      continue;
    }

    if (!strappy_responses_replace_string(
          &final_text,
          (analysis.response_text != NULL) ? analysis.response_text : "",
          error_out)) {
      strappy_responses_analysis_destroy(&analysis);
      strappy_responses_http_result_destroy(&http);
      break;
    }

    strappy_responses_analysis_destroy(&analysis);
    strappy_responses_http_result_destroy(&http);
    if ((final_text == NULL) ||
        !strappy_db_update_response_session_summary(session_db_path,
                                                    session_id,
                                                    prompt,
                                                    final_text,
                                                    last_model,
                                                    last_http_status,
                                                    error_out)) {
      free(final_text);
      final_text = NULL;
    }
    strappy_responses_emit_processing_status(callback,
                                             callback_data,
                                             prompt_group_key,
                                             0,
                                             NULL,
                                             processing_started_ms,
                                             0L,
                                             0U,
                                             NULL);
    free(prompt_group_key);
    free(last_model);
    strappy_responses_owned_items_destroy(&new_items);
    strappy_responses_runtime_destroy(&runtime);
    return final_text;
  }

  if ((round_index >= STRAPPY_RESPONSES_MAX_ROUNDS) &&
      ((error_out == NULL) || (*error_out == NULL))) {
    char *limit_message;

    limit_message = strappy_tools_tool_guidance_string(
      runtime.config.guidance_resource_dir,
      "assistant_messages",
      "tool_round_limit",
      NULL);
    strappy_set_error(error_out,
                      (limit_message != NULL) ? limit_message :
                        "Responses tool round limit was reached.");
    strappy_responses_update_failure_summary(
      session_db_path,
      session_id,
      prompt,
      last_model,
      last_http_status,
      (limit_message != NULL) ? limit_message :
        "Responses tool round limit was reached.");
    free(limit_message);
  }

  free(final_text);
  strappy_responses_emit_processing_status(callback,
                                           callback_data,
                                           prompt_group_key,
                                           0,
                                           NULL,
                                           processing_started_ms,
                                           0L,
                                           0U,
                                           NULL);
  free(prompt_group_key);
  free(last_model);
  strappy_responses_owned_items_destroy(&new_items);
  strappy_responses_runtime_destroy(&runtime);
  return NULL;
}

char *strappy_responses_send_prompt_for_session_and_store(
  const char *prompt,
  const char *env_path,
  const char *fallback_api_endpoint,
  const char *fallback_api_token,
  const char *guidance_resource_dir,
  const char *session_db_path,
  long long session_id,
  char **error_out)
{
  return strappy_responses_send_prompt_for_session_and_store_with_events(
    prompt,
    env_path,
    fallback_api_endpoint,
    fallback_api_token,
    guidance_resource_dir,
    session_db_path,
    session_id,
    NULL,
    NULL,
    error_out);
}
