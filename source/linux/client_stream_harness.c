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

int main(void)
{
  if (!harness_test_sparse_tool_call_indexes()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }
  if (!harness_test_concatenated_tool_arguments_split()) {
    fprintf(stderr, "client_stream_harness failed.\n");
    return 1;
  }

  printf("client_stream_harness passed.\n");
  return 0;
}
