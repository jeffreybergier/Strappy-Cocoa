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

typedef struct harness_turn_update_capture {
  const char *db_path;
  const char *message_key;
  long long session_id;
  int turn_finished_count;
  int first_had_metadata;
  int last_had_metadata;
} harness_turn_update_capture;

static int harness_capture_turn_update(const strappy_chat_stream_event *event,
                                       void *callback_data)
{
  harness_turn_update_capture *capture;
  strappy_session_message_record record;
  char *error;
  int has_metadata;

  if ((event == NULL) ||
      (event->type != STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED)) {
    return 1;
  }
  capture = (harness_turn_update_capture *)callback_data;
  if ((capture == NULL) || (capture->db_path == NULL) ||
      (capture->message_key == NULL) || (capture->session_id <= 0LL)) {
    return 0;
  }

  strappy_session_message_record_init(&record);
  error = NULL;
  if (!strappy_db_load_session_message_by_key(capture->db_path,
                                              capture->session_id,
                                              capture->message_key,
                                              &record,
                                              &error)) {
    if (error != NULL) {
      fprintf(stderr, "%s\n", error);
      strappy_free_string(error);
    }
    strappy_session_message_record_destroy(&record);
    return 0;
  }

  has_metadata =
    ((record.metadata_json != NULL) && (record.metadata_json[0] != '\0')) ?
      1 : 0;
  capture->turn_finished_count++;
  if (capture->turn_finished_count == 1) {
    capture->first_had_metadata = has_metadata;
  }
  capture->last_had_metadata = has_metadata;
  strappy_session_message_record_destroy(&record);
  return 1;
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

static int harness_test_post_store_turn_update_sees_metadata(void)
{
  char db_path[] = "/tmp/strappy-assistant-turn-update-XXXXXX";
  strappy_assistant_stream_store_context store_context;
  harness_turn_update_capture capture;
  strappy_assistant_turn_spec turn;
  strappy_chat_stream_event event;
  strappy_chat_result result;
  strappy_session_message_record record;
  cJSON *metadata;
  char *error;
  long long session_id;
  int fd;
  int ok;

  fd = mkstemp(db_path);
  if (fd < 0) {
    return harness_fail("Could not create temporary turn update DB path.");
  }
  close(fd);

  memset(&store_context, 0, sizeof(store_context));
  memset(&capture, 0, sizeof(capture));
  memset(&turn, 0, sizeof(turn));
  memset(&event, 0, sizeof(event));
  strappy_chat_result_init(&result);
  strappy_session_message_record_init(&record);
  metadata = NULL;
  error = NULL;
  session_id = 0LL;

  turn.turn_key = "turn-metadata";
  turn.prompt_group_key = "prompt-group-metadata";
  turn.actor = "user";
  turn.context_policy = "full";
  turn.assistant_message_key = "assistant-metadata";
  turn.session_id = 1LL;

  ok = strappy_db_create_session(db_path, &session_id, &error) &&
       harness_set_result_string(&result.response_text, "Final answer.") &&
       harness_set_result_string(&result.reasoning_text, "Final thinking.") &&
       harness_set_result_string(&result.model, "openai/gpt-4o") &&
       harness_set_result_string(&result.metadata_json,
                                 "{\"finish_reason\":\"stop\","
                                 "\"usage\":{\"total_tokens\":123}}") &&
       harness_set_result_string(&result.message_json,
                                 "{\"role\":\"assistant\","
                                 "\"content\":\"Final answer.\"}");
  result.http_status = 200L;

  capture.db_path = db_path;
  capture.session_id = session_id;
  capture.message_key = turn.assistant_message_key;

  store_context.session_db_path = db_path;
  store_context.session_id = session_id;
  store_context.callback = harness_capture_turn_update;
  store_context.callback_data = &capture;
  store_context.error_out = &error;

  event.type = STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED;
  event.text = "Final answer.";
  event.turn_key = turn.turn_key;
  event.prompt_group_key = turn.prompt_group_key;
  event.actor = turn.actor;
  event.kind = "assistant";
  event.message_key = turn.assistant_message_key;
  event.render_role = "assistant";
  event.api_role = "assistant";

  if (ok) {
    ok = strappy_assistant_stream_store_callback(&event, &store_context) &&
         (capture.turn_finished_count == 1) &&
         (capture.first_had_metadata == 0) &&
         strappy_assistant_store_turn_result(db_path,
                                             session_id,
                                             &turn,
                                             &result,
                                             &error);
  }
  if (ok) {
    strappy_assistant_emit_stored_turn_update(&turn,
                                              result.response_text,
                                              harness_capture_turn_update,
                                              &capture);
    ok = (capture.turn_finished_count == 2) &&
         (capture.last_had_metadata == 1) &&
         strappy_db_load_session_message_by_key(db_path,
                                                session_id,
                                                turn.assistant_message_key,
                                                &record,
                                                &error);
  }
  if (ok) {
    metadata = cJSON_Parse(record.metadata_json);
    ok = (record.content != NULL) &&
         (strcmp(record.content, "Final answer.") == 0) &&
         (record.reasoning != NULL) &&
         (strcmp(record.reasoning, "Final thinking.") == 0) &&
         cJSON_IsObject(metadata) &&
         harness_json_string_equals(metadata, "finish_reason", "stop") &&
         cJSON_IsObject(cJSON_GetObjectItem(metadata, "usage"));
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
    return harness_fail("Post-store turn update did not expose metadata.");
  }

  return 1;
}

static int harness_test_post_answer_helper_call_preserves_answer(void)
{
  char db_path[] = "/tmp/strappy-assistant-helper-answer-XXXXXX";
  const char *answer =
    "Database overview: Communications include messages, mail, and recents. "
    "Photos and media include asset metadata, albums, voice memos, downloads, "
    "and local media library records. Organization includes calendar events, "
    "notes, contacts, Safari bookmarks, and reminder metadata. Security "
    "includes privacy grants, account records, and password vault structure. "
    "The most interesting follow ups are photos for albums and comments, "
    "messages for conversation history, mail for sender and thread context, "
    "calendar for upcoming plans, notes for written context, reading databases "
    "for feeds and articles, wallet passes for tickets, and recordings for "
    "voice memo labels and durations. Pick one area and I can dig deeper.";
  strappy_config config;
  strappy_assistant_guidance guidance;
  strappy_assistant_turn_spec turn;
  strappy_assistant_request_messages request;
  strappy_chat_message request_messages[1];
  strappy_chat_result result;
  strappy_assistant_tool_sequence sequence;
  strappy_session_message_record_list messages;
  strappy_session_record session;
  char message_json[2048];
  char *error;
  long long session_id;
  int did_run;
  int fd;
  int ok;

  fd = mkstemp(db_path);
  if (fd < 0) {
    return harness_fail("Could not create temporary assistant DB path.");
  }
  close(fd);

  strappy_config_init(&config);
  memset(&guidance, 0, sizeof(guidance));
  memset(&turn, 0, sizeof(turn));
  strappy_assistant_request_messages_init(&request);
  strappy_chat_result_init(&result);
  strappy_assistant_tool_sequence_init(&sequence);
  strappy_session_message_record_list_init(&messages);
  strappy_session_record_init(&session);
  error = NULL;
  session_id = 0LL;
  did_run = 0;

  snprintf(message_json,
           sizeof(message_json),
           "{\"role\":\"assistant\",\"content\":\"%s\",\"tool_calls\":[{"
           "\"id\":\"call_title\",\"type\":\"function\",\"function\":{"
           "\"name\":\"helper_session_name_write\","
           "\"arguments\":\"{\\\"name\\\":\\\"Database overview\\\"}\"}}]}",
           answer);

  guidance.learning_summary_prompt = "Learning Summary";
  guidance.learning_summary_completion_text = "Learning Summary Complete";
  guidance.final_answer_prompt = "Continue.";
  guidance.tool_round_limit_message = "Tool limit reached.";
  guidance.tool_error_default = "Tool failed.";
  guidance.tool_error_not_allowed_for_turn = "Tool not allowed.";

  request_messages[0].role = "user";
  request_messages[0].content = "Explore databases.";
  request_messages[0].message_json = NULL;
  request.messages = request_messages;
  request.count = 1U;
  request.system_prompt = NULL;

  ok = strappy_config_set_guidance_resource_dir(&config,
                                                "../shared/Resources",
                                                &error) &&
       strappy_db_create_session(db_path, &session_id, &error) &&
       harness_set_result_string(&result.response_text,
                                 "Wrong visible text.\n\n"
                                 "Tool call requested: helper_session_name_write") &&
       harness_set_result_string(&result.message_json, message_json) &&
       harness_set_result_string(&result.finish_reason, "tool_calls") &&
       harness_set_result_string(&result.native_finish_reason, "tool_calls");

  turn.turn_key = "turn-helper-answer";
  turn.prompt_group_key = "prompt-group-helper-answer";
  turn.actor = "user";
  turn.render_role = "user";
  turn.api_role = "user";
  turn.context_policy = "full";
  turn.prompt = "Explore databases.";
  turn.prompt_message_key = "turn-helper-answer-prompt";
  turn.assistant_message_key = "turn-helper-answer-assistant";
  turn.session_id = session_id;
  turn.tool_policy = STRAPPY_ASSISTANT_TOOL_POLICY_NORMAL;

  if (ok) {
    ok = strappy_assistant_run_tool_sequence(&config,
                                             db_path,
                                             &guidance,
                                             &turn,
                                             0,
                                             0,
                                             &request,
                                             &result,
                                             0,
                                             0,
                                             0,
                                             NULL,
                                             NULL,
                                             &sequence,
                                             &did_run,
                                             &error);
  }
  if (ok) {
    ok = (sequence.final_result == &result) &&
         (did_run == 1) &&
         (sequence.round_count == 1U) &&
         (sequence.rounds[0].post_final == 1) &&
         (result.response_text != NULL) &&
         (strcmp(result.response_text, answer) == 0);
  }
  if (ok) {
    ok = strappy_assistant_store_tool_sequence(db_path,
                                               session_id,
                                               "Explore databases.",
                                               &guidance,
                                               &turn,
                                               NULL,
                                               &sequence,
                                               sequence.final_result,
                                               &error);
  }
  if (ok) {
    ok = strappy_db_list_session_messages(db_path,
                                          session_id,
                                          &messages,
                                          &error) &&
         strappy_db_load_session(db_path, session_id, &session, &error);
  }
  if (ok) {
    ok = (messages.count == 4U) &&
         (strcmp(messages.records[0].kind, "prompt") == 0) &&
         (strcmp(messages.records[1].kind, "assistant") == 0) &&
         (strcmp(messages.records[1].content, answer) == 0) &&
         (strcmp(messages.records[2].kind, "tool_call") == 0) &&
         (strcmp(messages.records[3].kind, "tool_result") == 0) &&
         (messages.records[3].tool_name != NULL) &&
         (strcmp(messages.records[3].tool_name,
                 STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE) == 0) &&
         (session.name != NULL) &&
         (strcmp(session.name, "Database overview") == 0);
  }

  if (error != NULL) {
    fprintf(stderr, "%s\n", error);
    strappy_free_string(error);
  }
  strappy_session_record_destroy(&session);
  strappy_session_message_record_list_destroy(&messages);
  strappy_assistant_tool_sequence_destroy(&sequence);
  strappy_chat_result_destroy(&result);
  strappy_config_destroy(&config);
  unlink(db_path);

  if (!ok) {
    return harness_fail("Post-answer helper tool call replaced the assistant answer.");
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
  if (!harness_test_post_store_turn_update_sees_metadata()) {
    fprintf(stderr, "assistant_reasoning_harness failed.\n");
    return 1;
  }
  if (!harness_test_post_answer_helper_call_preserves_answer()) {
    fprintf(stderr, "assistant_reasoning_harness failed.\n");
    return 1;
  }

  printf("assistant_reasoning_harness passed.\n");
  return 0;
}
