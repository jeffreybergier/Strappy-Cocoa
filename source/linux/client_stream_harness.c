#include <cJSON.h>
#include <stdio.h>
#include <string.h>

#include "../shared/strappy_client.c"

static int harness_fail(const char *message)
{
  fprintf(stderr, "%s\n", message);
  return 0;
}

static int harness_feed_stream_event(strappy_stream_context *context,
                                     const char *json)
{
  if (!strappy_client_stream_parse_json_event(context, json)) {
    if ((context != NULL) && (context->stream_error != NULL)) {
      fprintf(stderr, "%s\n", context->stream_error);
    }
    return 0;
  }
  return 1;
}

static int harness_expect_string(cJSON *object,
                                 const char *key,
                                 const char *expected)
{
  cJSON *value;

  value = cJSON_GetObjectItem(object, key);
  return cJSON_IsString(value) &&
    (value->valuestring != NULL) &&
    (strcmp(value->valuestring, expected) == 0);
}

typedef struct harness_stream_record {
  int content_count;
  int reasoning_count;
  int retracted_count;
  char content[4096];
  char reasoning[4096];
} harness_stream_record;

static int harness_append_recorded_text(char *buffer,
                                        size_t buffer_size,
                                        const char *text)
{
  size_t length;
  size_t text_length;

  if ((buffer == NULL) || (buffer_size == 0U) || (text == NULL)) {
    return 0;
  }

  length = strlen(buffer);
  text_length = strlen(text);
  if (text_length > (buffer_size - length - 1U)) {
    return 0;
  }

  memcpy(buffer + length, text, text_length + 1U);
  return 1;
}

static int harness_record_stream_event(const strappy_chat_stream_event *event,
                                       void *user_data)
{
  harness_stream_record *record;
  const char *text;

  if ((event == NULL) || (user_data == NULL)) {
    return 1;
  }

  record = (harness_stream_record *)user_data;
  text = (event->text != NULL) ? event->text : "";

  if (event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA) {
    if (text[0] == '\0') {
      return 1;
    }
    record->content_count++;
    return harness_append_recorded_text(record->content,
                                        sizeof(record->content),
                                        text);
  }
  if (event->type == STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA) {
    if (text[0] == '\0') {
      return 1;
    }
    record->reasoning_count++;
    return harness_append_recorded_text(record->reasoning,
                                        sizeof(record->reasoning),
                                        text);
  }
  if (event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_RETRACTED) {
    record->retracted_count++;
  }

  return 1;
}

static int harness_test_retry_after_and_server_error_type(void)
{
  strappy_chat_result result;
  char retry_after_header[] = "Retry-After: 12\r\n";
  char generation_header[] = "X-Generation-Id: gen-test\r\n";
  int ok;

  strappy_chat_result_init(&result);
  ok = (strappy_client_header_callback(retry_after_header,
                                       1U,
                                       strlen(retry_after_header),
                                       &result) ==
        strlen(retry_after_header)) &&
       (result.retry_after_seconds == 12L) &&
       (strappy_client_header_callback(generation_header,
                                       1U,
                                       strlen(generation_header),
                                       &result) ==
        strlen(generation_header)) &&
       (result.response_id != NULL) &&
       (strcmp(result.response_id, "gen-test") == 0) &&
       strappy_client_error_type_is_retryable("server");
  strappy_chat_result_destroy(&result);

  if (!ok) {
    return harness_fail("Retry-After or server retry handling failed.");
  }

  return 1;
}

static int harness_expect_function_string(cJSON *tool_call,
                                          const char *key,
                                          const char *expected)
{
  cJSON *function;

  function = cJSON_GetObjectItem(tool_call, "function");
  if (!cJSON_IsObject(function)) {
    return 0;
  }

  return harness_expect_string(function, key, expected);
}

static int harness_test_sparse_tool_call_indexes(void)
{
  strappy_stream_context context;
  strappy_chat_result result;
  cJSON *root;
  cJSON *tool_calls;
  cJSON *first_call;
  cJSON *second_call;
  int ok;

  strappy_chat_result_init(&result);
  strappy_stream_context_init(&context);
  context.result = &result;

  ok =
    harness_feed_stream_event(
      &context,
      "{\"id\":\"chatcmpl-test\",\"model\":\"test-model\",\"choices\":[{"
      "\"delta\":{\"role\":\"assistant\",\"tool_calls\":[{"
      "\"index\":4,\"id\":\"call_db\",\"type\":\"function\","
      "\"function\":{\"name\":\"database_context_read\","
      "\"arguments\":\"{\\\"database_id\\\":\\\"\"}}]},"
      "\"finish_reason\":null}]}") &&
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
      "\"index\":10,\"id\":\"call_icons\",\"type\":\"function\","
      "\"function\":{\"name\":\"helper_fontawesome_shortcode_search\","
      "\"arguments\":\"{\\\"query\\\":\\\"\"}}]},"
      "\"finish_reason\":null}]}") &&
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
      "\"index\":4,\"function\":{\"arguments\":\"db_15\\\"}\"}}]},"
      "\"finish_reason\":null}]}") &&
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
      "\"index\":10,\"function\":{\"arguments\":\"chat\\\"}\"}}]},"
      "\"finish_reason\":\"tool_calls\"}]}") &&
    strappy_client_stream_finalize_message(&context);

  if (!ok) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Sparse tool-call stream parsing failed.");
  }

  root = cJSON_Parse(result.message_json);
  if (root == NULL) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Streamed assistant message JSON was invalid.");
  }

  tool_calls = cJSON_GetObjectItem(root, "tool_calls");
  if (!cJSON_IsArray(tool_calls) || (cJSON_GetArraySize(tool_calls) != 2)) {
    cJSON_Delete(root);
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Sparse stream indexes produced placeholder tool calls.");
  }

  first_call = cJSON_GetArrayItem(tool_calls, 0);
  second_call = cJSON_GetArrayItem(tool_calls, 1);
  ok =
    harness_expect_string(first_call, "id", "call_db") &&
    harness_expect_function_string(first_call,
                                   "name",
                                   "database_context_read") &&
    harness_expect_function_string(first_call,
                                   "arguments",
                                   "{\"database_id\":\"db_15\"}") &&
    harness_expect_string(second_call, "id", "call_icons") &&
    harness_expect_function_string(
      second_call,
      "name",
      "helper_fontawesome_shortcode_search") &&
    harness_expect_function_string(second_call,
                                   "arguments",
                                   "{\"query\":\"chat\"}");

  cJSON_Delete(root);
  strappy_stream_context_destroy(&context);
  strappy_chat_result_destroy(&result);

  if (!ok) {
    return harness_fail("Sparse tool-call chunks were not reconstructed.");
  }

  return 1;
}

static int harness_test_concatenated_tool_arguments_split(void)
{
  strappy_stream_context context;
  strappy_chat_result result;
  cJSON *root;
  cJSON *tool_calls;
  cJSON *first_call;
  cJSON *second_call;
  int ok;

  strappy_chat_result_init(&result);
  strappy_stream_context_init(&context);
  context.result = &result;

  ok =
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"role\":\"assistant\","
      "\"tool_calls\":[{\"id\":\"call_db\",\"type\":\"function\","
      "\"function\":{\"name\":\"database_context_read\","
      "\"arguments\":\"{\\\"database_id\\\": \\\"db_15\\\"}\"}}]},"
      "\"finish_reason\":null}]}") &&
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
      "\"function\":{\"arguments\":\"{\\\"database_id\\\": \\\"db_6\\\"}\"}}]},"
      "\"finish_reason\":\"tool_calls\"}]}") &&
    strappy_client_stream_finalize_message(&context);

  if (!ok) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Concatenated tool argument stream parsing failed.");
  }

  root = cJSON_Parse(result.message_json);
  if (root == NULL) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Normalized assistant message JSON was invalid.");
  }

  tool_calls = cJSON_GetObjectItem(root, "tool_calls");
  if (!cJSON_IsArray(tool_calls) || (cJSON_GetArraySize(tool_calls) != 2)) {
    cJSON_Delete(root);
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Concatenated tool arguments were not split.");
  }

  first_call = cJSON_GetArrayItem(tool_calls, 0);
  second_call = cJSON_GetArrayItem(tool_calls, 1);
  ok =
    harness_expect_string(first_call, "id", "call_db") &&
    harness_expect_function_string(first_call,
                                   "name",
                                   "database_context_read") &&
    harness_expect_function_string(first_call,
                                   "arguments",
                                   "{\"database_id\": \"db_15\"}") &&
    harness_expect_string(second_call, "id", "call_db-split-1") &&
    harness_expect_function_string(second_call,
                                   "name",
                                   "database_context_read") &&
    harness_expect_function_string(second_call,
                                   "arguments",
                                   "{\"database_id\": \"db_6\"}");

  cJSON_Delete(root);
  strappy_stream_context_destroy(&context);
  strappy_chat_result_destroy(&result);

  if (!ok) {
    return harness_fail("Split tool calls did not preserve ids or arguments.");
  }

  return 1;
}

static int harness_test_openrouter_reasoning_request(void)
{
  strappy_config config;
  strappy_chat_message message;
  char *request_json;
  char *error;
  cJSON *root;
  cJSON *reasoning;
  cJSON *include_reasoning;
  cJSON *provider;
  cJSON *parallel_tool_calls;
  cJSON *tool_choice;
  cJSON *tools;
  int ok;

  memset(&config, 0, sizeof(config));
  memset(&message, 0, sizeof(message));
  config.api_endpoint = "https://openrouter.ai/api/v1/chat/completions";
  config.api_model = "google/gemma-4-31b-it";
  config.guidance_resource_dir = "../shared/Resources";
  message.role = "user";
  message.content = "Hello";
  error = NULL;

  request_json =
    strappy_client_build_messages_request_json(&config,
                                               &message,
                                               1U,
                                               0,
                                               &error);
  if (request_json == NULL) {
    if (error != NULL) {
      fprintf(stderr, "%s\n", error);
      strappy_free_string(error);
    }
    return harness_fail("Could not build OpenRouter request JSON.");
  }

  root = cJSON_Parse(request_json);
  free(request_json);
  if (root == NULL) {
    return harness_fail("OpenRouter request JSON was invalid.");
  }

  reasoning = cJSON_GetObjectItem(root, "reasoning");
  include_reasoning = cJSON_GetObjectItem(root, "include_reasoning");
  provider = cJSON_GetObjectItem(root, "provider");
  parallel_tool_calls = cJSON_GetObjectItem(root, "parallel_tool_calls");
  tool_choice = cJSON_GetObjectItem(root, "tool_choice");
  tools = cJSON_GetObjectItem(root, "tools");
  ok = cJSON_IsObject(reasoning) &&
       cJSON_IsTrue(cJSON_GetObjectItem(reasoning, "enabled")) &&
       cJSON_IsFalse(cJSON_GetObjectItem(reasoning, "exclude")) &&
       cJSON_IsObject(provider) &&
       cJSON_IsTrue(cJSON_GetObjectItem(provider, "require_parameters")) &&
       cJSON_IsString(tool_choice) &&
       (strcmp(tool_choice->valuestring, "auto") == 0) &&
       cJSON_IsArray(tools) &&
       (cJSON_GetArraySize(tools) > 0) &&
       (parallel_tool_calls == NULL) &&
       (include_reasoning == NULL);

  cJSON_Delete(root);
  if (!ok) {
    return harness_fail("OpenRouter request did not use unified reasoning.");
  }

  return 1;
}

static int harness_test_request_replays_reasoning_details_canonically(void)
{
  strappy_config config;
  strappy_chat_message message;
  char *request_json;
  char *error;
  cJSON *root;
  cJSON *messages;
  cJSON *assistant;
  cJSON *reasoning;
  cJSON *reasoning_content;
  cJSON *reasoning_details;
  cJSON *first_detail;
  int ok;

  memset(&config, 0, sizeof(config));
  memset(&message, 0, sizeof(message));
  config.api_endpoint = "https://openrouter.ai/api/v1/chat/completions";
  config.api_model = "z-ai/glm-5.2";
  message.role = "assistant";
  message.content = "";
  message.message_json =
    "{\"role\":\"assistant\",\"content\":null,"
    "\"reasoning\":\"duplicate text\","
    "\"reasoning_content\":\"duplicate text\","
    "\"reasoning_details\":["
    "{\"type\":\"reasoning.text\",\"index\":0,"
    "\"format\":\"unknown\",\"text\":\"There\"},"
    "{\"type\":\"reasoning.text\",\"index\":0,"
    "\"format\":\"unknown\",\"text\":\"\\n\"},"
    "{\"type\":\"reasoning.text\",\"index\":0,"
    "\"format\":\"unknown\",\"text\":\"'s\"}]}";
  error = NULL;

  request_json =
    strappy_client_build_messages_request_json(&config,
                                               &message,
                                               1U,
                                               0,
                                               &error);
  if (request_json == NULL) {
    if (error != NULL) {
      fprintf(stderr, "%s\n", error);
      strappy_free_string(error);
    }
    return harness_fail("Could not build request with stored reasoning.");
  }

  root = cJSON_Parse(request_json);
  free(request_json);
  if (root == NULL) {
    return harness_fail("Stored reasoning request JSON was invalid.");
  }

  messages = cJSON_GetObjectItem(root, "messages");
  assistant = cJSON_GetArrayItem(messages, 0);
  reasoning = cJSON_GetObjectItem(assistant, "reasoning");
  reasoning_content = cJSON_GetObjectItem(assistant, "reasoning_content");
  reasoning_details = cJSON_GetObjectItem(assistant, "reasoning_details");
  first_detail = cJSON_GetArrayItem(reasoning_details, 0);
  ok = (reasoning == NULL) &&
       (reasoning_content == NULL) &&
       cJSON_IsArray(reasoning_details) &&
       (cJSON_GetArraySize(reasoning_details) == 1) &&
       harness_expect_string(first_detail, "text", "There\n's") &&
       harness_expect_string(first_detail, "type", "reasoning.text");

  cJSON_Delete(root);
  if (!ok) {
    return harness_fail("Stored reasoning details were not replayed canonically.");
  }

  return 1;
}

static int harness_test_content_parts_filter_non_text(void)
{
  const char *response_json =
    "{\"id\":\"chatcmpl-test\",\"model\":\"test-model\",\"choices\":[{"
    "\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\","
    "\"content\":["
    "{\"type\":\"reasoning\",\"text\":\"hidden thinking\"},"
    "{\"type\":\"text\",\"text\":\"Visible answer.\"},"
    "{\"type\":\"image_url\",\"text\":\"ignored media text\"}]}}]}";
  strappy_chat_result result;
  char *error;
  int ok;

  strappy_chat_result_init(&result);
  error = NULL;

  if (!strappy_client_parse_response(response_json, 200L, &result, &error)) {
    if (error != NULL) {
      fprintf(stderr, "%s\n", error);
      strappy_free_string(error);
    }
    strappy_chat_result_destroy(&result);
    return harness_fail("Content-part response parsing failed.");
  }

  ok = (result.response_text != NULL) &&
       (strcmp(result.response_text, "Visible answer.") == 0);

  strappy_chat_result_destroy(&result);
  if (!ok) {
    return harness_fail("Non-text content parts leaked into assistant text.");
  }

  return 1;
}

static int harness_test_non_stream_choice_error_metadata(void)
{
  const char *response_json =
    "{\"id\":\"gen-test\",\"model\":\"google/gemma-4-31b-it-20260402\","
    "\"provider\":\"DeepInfra\",\"choices\":[{"
    "\"finish_reason\":\"error\",\"native_finish_reason\":\"error\","
    "\"message\":{\"role\":\"assistant\",\"content\":null},"
    "\"error\":{\"code\":429,\"message\":\"Provider returned error\","
    "\"metadata\":{\"error_type\":\"rate_limit_exceeded\","
    "\"provider_code\":\"rate_limited\"}}}]}";
  strappy_chat_result result;
  char *error;
  int ok;

  strappy_chat_result_init(&result);
  error = NULL;

  ok = !strappy_client_parse_response(response_json, 200L, &result, &error) &&
       (error != NULL) &&
       (strstr(error, "Provider returned error") != NULL) &&
       (result.response_id != NULL) &&
       (strcmp(result.response_id, "gen-test") == 0) &&
       (result.provider_name != NULL) &&
       (strcmp(result.provider_name, "DeepInfra") == 0) &&
       (result.error_type != NULL) &&
       (strcmp(result.error_type, "rate_limit_exceeded") == 0) &&
       (result.provider_code != NULL) &&
       (strcmp(result.provider_code, "rate_limited") == 0) &&
       (result.finish_reason != NULL) &&
       (strcmp(result.finish_reason, "error") == 0);

  if (error != NULL) {
    strappy_free_string(error);
  }
  strappy_chat_result_destroy(&result);

  if (!ok) {
    return harness_fail("Choice error metadata was not captured.");
  }

  return 1;
}

static int harness_test_stream_midstream_error_metadata(void)
{
  const char *content_json =
    "{\"id\":\"gen-stream-error\",\"object\":\"chat.completion.chunk\","
    "\"model\":\"openai/gpt-4o\",\"provider\":\"OpenAI\","
    "\"choices\":[{\"index\":0,\"delta\":{\"content\":\"partial\"},"
    "\"finish_reason\":null}]}";
  const char *error_json =
    "{\"id\":\"gen-stream-error\",\"object\":\"chat.completion.chunk\","
    "\"model\":\"openai/gpt-4o\",\"provider\":\"OpenAI\","
    "\"error\":{\"code\":\"server_error\","
    "\"message\":\"Provider disconnected unexpectedly\","
    "\"error_type\":\"server\",\"provider_code\":\"upstream_disconnect\"},"
    "\"choices\":[{\"index\":0,\"delta\":{\"content\":\"\"},"
    "\"finish_reason\":\"error\"}]}";
  strappy_stream_context context;
  strappy_chat_result result;
  harness_stream_record record;
  int ok;

  memset(&record, 0, sizeof(record));
  strappy_chat_result_init(&result);
  strappy_stream_context_init(&context);
  context.result = &result;
  context.callback = harness_record_stream_event;
  context.callback_data = &record;

  ok = strappy_client_stream_parse_json_event(&context, content_json) &&
       !strappy_client_stream_parse_json_event(&context, error_json) &&
       (context.stream_error != NULL) &&
       (strstr(context.stream_error,
               "Provider disconnected unexpectedly") != NULL) &&
       (record.content_count == 1) &&
       (strcmp(record.content, "partial") == 0) &&
       (result.response_id != NULL) &&
       (strcmp(result.response_id, "gen-stream-error") == 0) &&
       (result.model != NULL) &&
       (strcmp(result.model, "openai/gpt-4o") == 0) &&
       (result.provider_name != NULL) &&
       (strcmp(result.provider_name, "OpenAI") == 0) &&
       (result.error_code != NULL) &&
       (strcmp(result.error_code, "server_error") == 0) &&
       (result.error_type != NULL) &&
       (strcmp(result.error_type, "server") == 0) &&
       (result.provider_code != NULL) &&
       (strcmp(result.provider_code, "upstream_disconnect") == 0) &&
       (result.finish_reason != NULL) &&
       (strcmp(result.finish_reason, "error") == 0);

  strappy_stream_context_destroy(&context);
  strappy_chat_result_destroy(&result);

  if (!ok) {
    return harness_fail("Mid-stream OpenRouter error metadata was not captured.");
  }

  return 1;
}

static int harness_test_non_stream_reasoning_detail_chunks_are_joined(void)
{
  const char *response_json =
    "{\"id\":\"gen-test\",\"choices\":[{\"finish_reason\":\"stop\","
    "\"message\":{\"role\":\"assistant\",\"content\":\"Visible answer.\","
    "\"reasoning\":\"There\\n's a clue\","
    "\"reasoning_details\":["
    "{\"type\":\"reasoning.text\",\"index\":0,"
    "\"format\":\"unknown\",\"text\":\"There\"},"
    "{\"type\":\"reasoning.text\",\"index\":0,"
    "\"format\":\"unknown\",\"text\":\"\\n\"},"
    "{\"type\":\"reasoning.text\",\"index\":0,"
    "\"format\":\"unknown\",\"text\":\"'s a clue\"}]}}]}";
  strappy_chat_result result;
  char *error;
  cJSON *message;
  cJSON *reasoning;
  cJSON *reasoning_details;
  cJSON *detail;
  int ok;

  strappy_chat_result_init(&result);
  error = NULL;

  ok = strappy_client_parse_response(response_json, 200L, &result, &error) &&
       (result.reasoning_text != NULL) &&
       (strcmp(result.reasoning_text, "There\n's a clue") == 0) &&
       (result.response_text != NULL) &&
       (strcmp(result.response_text, "Visible answer.") == 0);
  message = (result.message_json != NULL) ? cJSON_Parse(result.message_json) : NULL;
  reasoning = cJSON_GetObjectItem(message, "reasoning");
  reasoning_details = cJSON_GetObjectItem(message, "reasoning_details");
  detail = cJSON_GetArrayItem(reasoning_details, 0);
  ok = ok &&
       (reasoning == NULL) &&
       cJSON_IsArray(reasoning_details) &&
       (cJSON_GetArraySize(reasoning_details) == 1) &&
       harness_expect_string(detail, "text", "There\n's a clue");

  if (error != NULL) {
    strappy_free_string(error);
  }
  cJSON_Delete(message);
  strappy_chat_result_destroy(&result);

  if (!ok) {
    return harness_fail("Non-streamed reasoning detail chunks were not joined.");
  }

  return 1;
}

static int harness_test_stream_reasoning_details_context_message(void)
{
  strappy_stream_context context;
  strappy_chat_result result;
  cJSON *root;
  cJSON *reasoning;
  cJSON *reasoning_details;
  int ok;

  strappy_chat_result_init(&result);
  strappy_stream_context_init(&context);
  context.result = &result;

  ok =
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"role\":\"assistant\","
      "\"reasoning\":\"hidden thinking\","
      "\"reasoning_details\":[{\"type\":\"reasoning.text\","
      "\"text\":\"hidden thinking\"}],"
      "\"content\":\"Visible answer.\"},\"finish_reason\":\"stop\"}]}") &&
    strappy_client_stream_finalize_message(&context);

  if (!ok) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Streamed reasoning-details parsing failed.");
  }

  root = cJSON_Parse(result.message_json);
  if (root == NULL) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Streamed reasoning message JSON was invalid.");
  }

  reasoning = cJSON_GetObjectItem(root, "reasoning");
  reasoning_details = cJSON_GetObjectItem(root, "reasoning_details");
  ok = (reasoning == NULL) &&
       cJSON_IsArray(reasoning_details) &&
       (cJSON_GetArraySize(reasoning_details) == 1) &&
       harness_expect_string(root, "content", "Visible answer.") &&
       (result.reasoning_text != NULL) &&
       (strcmp(result.reasoning_text, "hidden thinking") == 0);

  cJSON_Delete(root);
  strappy_stream_context_destroy(&context);
  strappy_chat_result_destroy(&result);

  if (!ok) {
    return harness_fail("Streamed reasoning details were not replayed canonically.");
  }

  return 1;
}

static int harness_test_answer_content_streams_live(void)
{
  strappy_stream_context context;
  strappy_chat_result result;
  harness_stream_record record;
  cJSON *root;
  int ok;

  memset(&record, 0, sizeof(record));
  strappy_chat_result_init(&result);
  strappy_stream_context_init(&context);
  context.result = &result;
  context.callback = harness_record_stream_event;
  context.callback_data = &record;

  ok =
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"role\":\"assistant\","
      "\"content\":\"Visible \"},\"finish_reason\":null}]}") &&
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"content\":\"answer.\"},"
      "\"finish_reason\":null}]}") &&
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}") &&
    strappy_client_stream_finalize_message(&context);

  if (!ok) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Live answer content stream failed.");
  }

  root = cJSON_Parse(result.message_json);
  if (root == NULL) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Live answer content JSON was invalid.");
  }

  ok = (record.content_count == 1) &&
       (record.reasoning_count == 0) &&
       (record.retracted_count == 0) &&
       (strcmp(record.content, "Visible answer.") == 0) &&
       harness_expect_string(root, "content", "Visible answer.");

  cJSON_Delete(root);
  strappy_stream_context_destroy(&context);
  strappy_chat_result_destroy(&result);

  if (!ok) {
    return harness_fail("Answer content was not streamed live.");
  }

  return 1;
}

static int harness_test_tool_call_stream_content_retracted_live(void)
{
  strappy_stream_context context;
  strappy_chat_result result;
  harness_stream_record record;
  cJSON *root;
  int ok;

  memset(&record, 0, sizeof(record));
  strappy_chat_result_init(&result);
  strappy_stream_context_init(&context);
  context.result = &result;
  context.callback = harness_record_stream_event;
  context.callback_data = &record;

  ok =
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"role\":\"assistant\","
      "\"content\":\"Let me check \"},\"finish_reason\":null}]}") &&
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
      "\"index\":0,\"id\":\"call_db\",\"type\":\"function\","
      "\"function\":{\"name\":\"database_query\","
      "\"arguments\":\"{\\\"sql\\\":\\\"\"}}]},"
      "\"finish_reason\":null}]}") &&
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"content\":\"the database.\","
      "\"tool_calls\":[{\"index\":0,"
      "\"function\":{\"arguments\":\"SELECT 1\\\"}\"}}]},"
      "\"finish_reason\":\"tool_calls\"}]}") &&
    strappy_client_stream_finalize_message(&context);

  if (!ok) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Tool-call content retraction stream failed.");
  }

  root = cJSON_Parse(result.message_json);
  if (root == NULL) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Tool-call content retraction JSON was invalid.");
  }

  ok = (record.content_count == 0) &&
       (record.retracted_count == 0) &&
       (record.reasoning_count == 2) &&
       (strcmp(record.content, "") == 0) &&
       (strcmp(record.reasoning, "Let me check the database.") == 0) &&
       harness_expect_string(root, "content", "Let me check the database.");

  cJSON_Delete(root);
  strappy_stream_context_destroy(&context);
  strappy_chat_result_destroy(&result);

  if (!ok) {
    return harness_fail("Tool-call content leaked into answer deltas.");
  }

  return 1;
}

static int harness_test_same_chunk_tool_call_content_is_reasoning(void)
{
  strappy_stream_context context;
  strappy_chat_result result;
  harness_stream_record record;
  cJSON *root;
  int ok;

  memset(&record, 0, sizeof(record));
  strappy_chat_result_init(&result);
  strappy_stream_context_init(&context);
  context.result = &result;
  context.callback = harness_record_stream_event;
  context.callback_data = &record;

  ok =
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"role\":\"assistant\","
      "\"content\":\"Planning the lookup.\","
      "\"tool_calls\":[{\"index\":0,\"id\":\"call_db\","
      "\"type\":\"function\",\"function\":{\"name\":\"database_query\","
      "\"arguments\":\"{\\\"sql\\\":\\\"SELECT 1\\\"}\"}}]},"
      "\"finish_reason\":\"tool_calls\"}]}") &&
    strappy_client_stream_finalize_message(&context);

  if (!ok) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Same-chunk tool-call content stream failed.");
  }

  root = cJSON_Parse(result.message_json);
  if (root == NULL) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Same-chunk tool-call content JSON was invalid.");
  }

  ok = (record.content_count == 0) &&
       (record.retracted_count == 0) &&
       (record.reasoning_count == 1) &&
       (strcmp(record.reasoning, "Planning the lookup.") == 0) &&
       harness_expect_string(root, "content", "Planning the lookup.");

  cJSON_Delete(root);
  strappy_stream_context_destroy(&context);
  strappy_chat_result_destroy(&result);

  if (!ok) {
    return harness_fail("Same-chunk tool-call content leaked as answer.");
  }

  return 1;
}

static int harness_test_long_helper_tool_call_content_remains_answer(void)
{
  const char *chunk_one =
    "Database overview: Communications include messages, mail, and recents. "
    "Photos and media include asset metadata, albums, voice memos, downloads, "
    "and local media library records. Organization includes calendar events, "
    "notes, contacts, Safari bookmarks, and reminder metadata. Security "
    "includes privacy grants, account records, and password vault structure. ";
  const char *chunk_two =
    "The most interesting follow ups are photos for albums and comments, "
    "messages for conversation history, mail for sender and thread context, "
    "calendar for upcoming plans, notes for written context, reading databases "
    "for feeds and articles, wallet passes for tickets, and recordings for "
    "voice memo labels and durations. Pick one area and I can dig deeper.";
  strappy_stream_context context;
  strappy_chat_result result;
  harness_stream_record record;
  cJSON *root;
  cJSON *tool_calls;
  char first_event[1024];
  char second_event[1024];
  char expected[2048];
  int ok;

  memset(&record, 0, sizeof(record));
  strappy_chat_result_init(&result);
  strappy_stream_context_init(&context);
  context.result = &result;
  context.callback = harness_record_stream_event;
  context.callback_data = &record;

  snprintf(first_event,
           sizeof(first_event),
           "{\"choices\":[{\"delta\":{\"role\":\"assistant\","
           "\"content\":\"%s\"},\"finish_reason\":null}]}",
           chunk_one);
  snprintf(second_event,
           sizeof(second_event),
           "{\"choices\":[{\"delta\":{\"content\":\"%s\"},"
           "\"finish_reason\":null}]}",
           chunk_two);
  snprintf(expected, sizeof(expected), "%s%s", chunk_one, chunk_two);

  ok =
    harness_feed_stream_event(&context, first_event) &&
    harness_feed_stream_event(&context, second_event) &&
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
      "\"index\":0,\"id\":\"call_title\",\"type\":\"function\","
      "\"function\":{\"name\":\"helper_session_name_write\","
      "\"arguments\":\"{\\\"name\\\":\\\"Database overview\\\"}\"}}]},"
      "\"finish_reason\":\"tool_calls\"}]}") &&
    strappy_client_stream_finalize_message(&context);

  if (!ok) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Long helper-call answer stream failed.");
  }

  root = cJSON_Parse(result.message_json);
  if (root == NULL) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Long helper-call answer JSON was invalid.");
  }

  tool_calls = cJSON_GetObjectItem(root, "tool_calls");
  ok = (record.reasoning_count == 0) &&
       (record.retracted_count == 0) &&
       (context.helper_tool_calls_preserve_content == 1) &&
       (strcmp(record.content, expected) == 0) &&
       harness_expect_string(root, "content", expected) &&
       cJSON_IsArray(tool_calls) &&
       (cJSON_GetArraySize(tool_calls) == 1);

  cJSON_Delete(root);
  strappy_stream_context_destroy(&context);
  strappy_chat_result_destroy(&result);

  if (!ok) {
    return harness_fail("Long helper-call answer was not preserved as content.");
  }

  return 1;
}

static int harness_test_stream_reasoning_detail_chunks_coalesced(void)
{
  strappy_stream_context context;
  strappy_chat_result result;
  cJSON *root;
  cJSON *reasoning_details;
  cJSON *detail;
  int ok;

  strappy_chat_result_init(&result);
  strappy_stream_context_init(&context);
  context.result = &result;

  ok =
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"role\":\"assistant\","
      "\"reasoning_details\":[{\"type\":\"reasoning.text\","
      "\"index\":0,\"format\":\"unknown\",\"text\":\"There\"}]},"
      "\"finish_reason\":null}]}") &&
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"reasoning_details\":[{"
      "\"type\":\"reasoning.text\",\"index\":0,"
      "\"format\":\"unknown\",\"text\":\"\\n\"}]},"
      "\"finish_reason\":null}]}") &&
    harness_feed_stream_event(
      &context,
      "{\"choices\":[{\"delta\":{\"reasoning_details\":[{"
      "\"type\":\"reasoning.text\",\"index\":0,"
      "\"format\":\"unknown\",\"text\":\"'s a clue\"}],"
      "\"content\":\"Visible answer.\"},\"finish_reason\":\"stop\"}]}") &&
    strappy_client_stream_finalize_message(&context);

  if (!ok) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Streamed reasoning-detail chunk parsing failed.");
  }

  root = cJSON_Parse(result.message_json);
  if (root == NULL) {
    strappy_stream_context_destroy(&context);
    strappy_chat_result_destroy(&result);
    return harness_fail("Chunked reasoning message JSON was invalid.");
  }

  reasoning_details = cJSON_GetObjectItem(root, "reasoning_details");
  detail = cJSON_GetArrayItem(reasoning_details, 0);
  ok = cJSON_IsArray(reasoning_details) &&
       (cJSON_GetArraySize(reasoning_details) == 1) &&
       harness_expect_string(detail, "text", "There\n's a clue") &&
       harness_expect_string(detail, "type", "reasoning.text") &&
       harness_expect_string(root, "content", "Visible answer.") &&
       (result.reasoning_text != NULL) &&
       (strcmp(result.reasoning_text, "There\n's a clue") == 0);

  cJSON_Delete(root);
  strappy_stream_context_destroy(&context);
  strappy_chat_result_destroy(&result);

  if (!ok) {
    return harness_fail("Streamed reasoning details were not preserved in order.");
  }

  return 1;
}

int main(void)
{
  if (!harness_test_retry_after_and_server_error_type()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_sparse_tool_call_indexes()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_concatenated_tool_arguments_split()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_openrouter_reasoning_request()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_request_replays_reasoning_details_canonically()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_content_parts_filter_non_text()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_non_stream_choice_error_metadata()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_stream_midstream_error_metadata()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_non_stream_reasoning_detail_chunks_are_joined()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_stream_reasoning_details_context_message()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_answer_content_streams_live()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_tool_call_stream_content_retracted_live()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_same_chunk_tool_call_content_is_reasoning()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_long_helper_tool_call_content_remains_answer()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_stream_reasoning_detail_chunks_coalesced()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }

  printf("client_stream_harness passed.\n");
  return 0;
}
