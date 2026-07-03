#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../shared/strappy_assistant.c"

static int harness_fail(const char *message)
{
  fprintf(stderr, "%s\n", message);
  return 0;
}

static int harness_set_reasoning(strappy_chat_result *result,
                                 const char *reasoning)
{
  if (result == NULL) {
    return 0;
  }

  free(result->reasoning_text);
  result->reasoning_text =
    (reasoning != NULL) ? strappy_string_duplicate(reasoning) : NULL;
  return ((reasoning == NULL) || (result->reasoning_text != NULL)) ? 1 : 0;
}

static int harness_set_result_string(char **target, const char *value)
{
  if (target == NULL) {
    return 0;
  }

  *target = (value != NULL) ? strappy_string_duplicate(value) : NULL;
  return ((value == NULL) || (*target != NULL)) ? 1 : 0;
}

static int harness_json_string_equals(cJSON *object,
                                      const char *key,
                                      const char *expected)
{
  cJSON *value;

  value = cJSON_GetObjectItem(object, key);
  return cJSON_IsString(value) &&
    (value->valuestring != NULL) &&
    (strcmp(value->valuestring, expected) == 0);
}

static int harness_json_number_equals(cJSON *object,
                                      const char *key,
                                      double expected)
{
  cJSON *value;

  value = cJSON_GetObjectItem(object, key);
  return cJSON_IsNumber(value) && (value->valuedouble == expected);
}

static int harness_test_final_result_gets_accumulated_reasoning(void)
{
  strappy_assistant_tool_sequence sequence;
  strappy_chat_result first;
  strappy_chat_result second;
  strappy_chat_result final;
  char *error;
  int ok;

  strappy_assistant_tool_sequence_init(&sequence);
  strappy_chat_result_init(&first);
  strappy_chat_result_init(&second);
  strappy_chat_result_init(&final);
  error = NULL;

  ok = harness_set_reasoning(&first, "first tool reasoning") &&
       harness_set_reasoning(&second, "second tool reasoning") &&
       harness_set_reasoning(&final, "final answer reasoning") &&
       strappy_assistant_accumulate_result_reasoning(&sequence,
                                                     &first,
                                                     &error) &&
       strappy_assistant_accumulate_result_reasoning(&sequence,
                                                     &second,
                                                     &error) &&
       strappy_assistant_accumulate_result_reasoning(&sequence,
                                                     &final,
                                                     &error) &&
       strappy_assistant_apply_accumulated_reasoning(&sequence,
                                                    &final,
                                                    &error) &&
       (final.reasoning_text != NULL) &&
       (strcmp(final.reasoning_text,
               "first tool reasoning\n\n"
               "second tool reasoning\n\n"
               "final answer reasoning") == 0) &&
       (first.reasoning_text != NULL) &&
       (strcmp(first.reasoning_text, "first tool reasoning") == 0) &&
       (second.reasoning_text != NULL) &&
       (strcmp(second.reasoning_text, "second tool reasoning") == 0);

  if (error != NULL) {
    strappy_free_string(error);
  }
  strappy_chat_result_destroy(&final);
  strappy_chat_result_destroy(&second);
  strappy_chat_result_destroy(&first);
  strappy_assistant_tool_sequence_destroy(&sequence);

  if (!ok) {
    return harness_fail("Final assistant reasoning was not accumulated.");
  }

  return 1;
}

static int harness_test_stream_error_row_preserves_openrouter_metadata(void)
{
  char db_path[] = "/tmp/strappy-assistant-error-XXXXXX";
  const char *message =
    "OpenRouter stream failed: Provider disconnected unexpectedly";
  strappy_assistant_turn_spec turn;
  strappy_chat_result result;
  strappy_session_message_record record;
  cJSON *metadata;
  char *error;
  long long session_id;
  int fd;
  int ok;

  fd = mkstemp(db_path);
  if (fd < 0) {
    return harness_fail("Could not create temporary assistant DB path.");
  }
  close(fd);

  memset(&turn, 0, sizeof(turn));
  strappy_chat_result_init(&result);
  strappy_session_message_record_init(&record);
  metadata = NULL;
  error = NULL;
  session_id = 0LL;

  turn.turn_key = "turn-stream-error";
  turn.prompt_group_key = "prompt-group-stream-error";
  turn.actor = "user";
  turn.context_policy = "full";
  turn.assistant_message_key = "assistant-stream-error";
  turn.session_id = 1LL;

  result.http_status = 200L;
  ok = harness_set_result_string(&result.response_id, "gen-stream-error") &&
       harness_set_result_string(&result.model, "openai/gpt-4o") &&
       harness_set_result_string(&result.provider_name, "OpenAI") &&
       harness_set_result_string(&result.finish_reason, "error") &&
       harness_set_result_string(&result.native_finish_reason, "error") &&
       harness_set_result_string(&result.error_code, "server_error") &&
       harness_set_result_string(&result.error_type, "server") &&
       harness_set_result_string(&result.provider_code,
                                 "upstream_disconnect") &&
       strappy_db_create_session(db_path, &session_id, &error) &&
       strappy_assistant_store_turn_error(db_path,
                                          session_id,
                                          &turn,
                                          &result,
                                          message,
                                          &error) &&
       strappy_db_load_session_message_by_key(db_path,
                                              session_id,
                                              turn.assistant_message_key,
                                              &record,
                                              &error);

  if (ok) {
    metadata = cJSON_Parse(record.metadata_json);
    ok = (record.is_error == 1) &&
         (record.include_in_context == 0) &&
         (record.http_status == 200L) &&
         (record.content != NULL) &&
         (strcmp(record.content, message) == 0) &&
         (record.model != NULL) &&
         (strcmp(record.model, "openai/gpt-4o") == 0) &&
         cJSON_IsObject(metadata) &&
         harness_json_string_equals(metadata, "finish_reason", "error") &&
         harness_json_string_equals(metadata, "native_finish_reason", "error") &&
         harness_json_number_equals(metadata, "http_status", 200.0) &&
         harness_json_string_equals(metadata, "error", message) &&
         harness_json_string_equals(metadata,
                                    "response_id",
                                    "gen-stream-error") &&
         harness_json_string_equals(metadata, "model", "openai/gpt-4o") &&
         harness_json_string_equals(metadata,
                                    "provider_finish_reason",
                                    "error") &&
         harness_json_string_equals(metadata,
                                    "provider_native_finish_reason",
                                    "error") &&
         harness_json_string_equals(metadata, "error_code", "server_error") &&
         harness_json_string_equals(metadata, "error_type", "server") &&
         harness_json_string_equals(metadata, "provider", "OpenAI") &&
         harness_json_string_equals(metadata,
                                    "provider_code",
                                    "upstream_disconnect");
  }

  if (error != NULL) {
    fprintf(stderr, "%s\n", error);
    strappy_free_string(error);
  }
  cJSON_Delete(metadata);
  strappy_session_message_record_destroy(&record);
  strappy_chat_result_destroy(&result);
  unlink(db_path);

  if (!ok) {
    return harness_fail("Stream error row did not preserve OpenRouter metadata.");
  }

  return 1;
}

int main(void)
{
  if (!harness_test_final_result_gets_accumulated_reasoning()) {
    fprintf(stderr, "assistant_reasoning_harness failed.\n");
    return 1;
  }
  if (!harness_test_stream_error_row_preserves_openrouter_metadata()) {
    fprintf(stderr, "assistant_reasoning_harness failed.\n");
    return 1;
  }

  printf("assistant_reasoning_harness passed.\n");
  return 0;
}
