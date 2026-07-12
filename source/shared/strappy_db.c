#include "strappy_db.h"

#include "strappy_config.h"
#include "strappy_core.h"

#include <cJSON.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_KEY \
  "default_openrouter_model_id"
#define STRAPPY_DB_SELECTED_OPENROUTER_MODEL_KEY \
  "selected_openrouter_model_id"
#define STRAPPY_DB_BUILTIN_DEFAULT_MODEL_DESCRIPTION "Built-in default model."
#define STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL \
  "COALESCE((SELECT s.value FROM app_settings s " \
  "WHERE s.key = '" STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_KEY "' " \
  "AND EXISTS (SELECT 1 FROM openrouter_models dm WHERE dm.id = s.value))," \
  "(SELECT s.value FROM app_settings s " \
  "WHERE s.key = '" STRAPPY_DB_SELECTED_OPENROUTER_MODEL_KEY "' " \
  "AND EXISTS (SELECT 1 FROM openrouter_models sm WHERE sm.id = s.value))," \
  "'" STRAPPY_CONFIG_DEFAULT_API_MODEL "')"
#define STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL \
  "INSERT OR IGNORE INTO openrouter_models " \
  "(id, name, description) VALUES ('" STRAPPY_CONFIG_DEFAULT_API_MODEL "', '" \
  STRAPPY_CONFIG_DEFAULT_API_MODEL "', '" \
  STRAPPY_DB_BUILTIN_DEFAULT_MODEL_DESCRIPTION "');"

typedef enum strappy_response_field_type {
  STRAPPY_RESPONSE_FIELD_TEXT = 0,
  STRAPPY_RESPONSE_FIELD_INTEGER = 1,
  STRAPPY_RESPONSE_FIELD_REAL = 2
} strappy_response_field_type;

typedef struct strappy_response_field_definition {
  const char *json_path;
  const char *column_name;
  strappy_response_field_type type;
} strappy_response_field_definition;

static const strappy_response_field_definition
strappy_response_request_fields[] = {
  { "background", "background", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "cache_control", "cache_control_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "debug", "debug_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "frequency_penalty", "frequency_penalty", STRAPPY_RESPONSE_FIELD_REAL },
  { "image_config", "image_config_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "include", "include_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "input", "input_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "instructions", "instructions", STRAPPY_RESPONSE_FIELD_TEXT },
  { "max_output_tokens", "max_output_tokens", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "max_tool_calls", "max_tool_calls", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "metadata", "metadata_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "modalities", "modalities_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "model", "model", STRAPPY_RESPONSE_FIELD_TEXT },
  { "models", "models_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "parallel_tool_calls", "parallel_tool_calls", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "plugins", "plugins_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "presence_penalty", "presence_penalty", STRAPPY_RESPONSE_FIELD_REAL },
  { "previous_response_id", "previous_response_id", STRAPPY_RESPONSE_FIELD_TEXT },
  { "prompt", "prompt_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "prompt_cache_key", "prompt_cache_key", STRAPPY_RESPONSE_FIELD_TEXT },
  { "provider", "provider_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "reasoning", "reasoning_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "route", "route_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "safety_identifier", "safety_identifier", STRAPPY_RESPONSE_FIELD_TEXT },
  { "service_tier", "service_tier", STRAPPY_RESPONSE_FIELD_TEXT },
  { "session_id", "session_id", STRAPPY_RESPONSE_FIELD_TEXT },
  { "stop_server_tools_when", "stop_server_tools_when_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "store", "store", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "stream", "stream", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "temperature", "temperature", STRAPPY_RESPONSE_FIELD_REAL },
  { "text", "text_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "tool_choice", "tool_choice_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "tools", "tools_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "top_k", "top_k", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "top_logprobs", "top_logprobs", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "top_p", "top_p", STRAPPY_RESPONSE_FIELD_REAL },
  { "trace", "trace_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "truncation", "truncation_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "user", "user", STRAPPY_RESPONSE_FIELD_TEXT }
};

static const strappy_response_field_definition
strappy_response_result_fields[] = {
  { "background", "background", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "completed_at", "completed_at", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "created_at", "created_at", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "error", "error_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "error.code", "error_code", STRAPPY_RESPONSE_FIELD_TEXT },
  { "error.message", "error_message", STRAPPY_RESPONSE_FIELD_TEXT },
  { "error_type", "error_type", STRAPPY_RESPONSE_FIELD_TEXT },
  { "frequency_penalty", "frequency_penalty", STRAPPY_RESPONSE_FIELD_REAL },
  { "id", "id", STRAPPY_RESPONSE_FIELD_TEXT },
  { "incomplete_details", "incomplete_details_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "incomplete_details.reason", "incomplete_reason", STRAPPY_RESPONSE_FIELD_TEXT },
  { "instructions", "instructions_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "max_output_tokens", "max_output_tokens", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "max_tool_calls", "max_tool_calls", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "metadata", "metadata_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "model", "model", STRAPPY_RESPONSE_FIELD_TEXT },
  { "object", "object", STRAPPY_RESPONSE_FIELD_TEXT },
  { "openrouter_metadata", "openrouter_metadata_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "output", "output_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "output_text", "output_text", STRAPPY_RESPONSE_FIELD_TEXT },
  { "parallel_tool_calls", "parallel_tool_calls", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "presence_penalty", "presence_penalty", STRAPPY_RESPONSE_FIELD_REAL },
  { "previous_response_id", "previous_response_id", STRAPPY_RESPONSE_FIELD_TEXT },
  { "prompt", "prompt_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "prompt_cache_key", "prompt_cache_key", STRAPPY_RESPONSE_FIELD_TEXT },
  { "reasoning", "reasoning_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "safety_identifier", "safety_identifier", STRAPPY_RESPONSE_FIELD_TEXT },
  { "service_tier", "service_tier", STRAPPY_RESPONSE_FIELD_TEXT },
  { "status", "status", STRAPPY_RESPONSE_FIELD_TEXT },
  { "store", "store", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "temperature", "temperature", STRAPPY_RESPONSE_FIELD_REAL },
  { "text", "text_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "tool_choice", "tool_choice_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "tools", "tools_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "top_logprobs", "top_logprobs", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "top_p", "top_p", STRAPPY_RESPONSE_FIELD_REAL },
  { "truncation", "truncation", STRAPPY_RESPONSE_FIELD_TEXT },
  { "usage", "usage_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "usage.input_tokens", "usage_input_tokens", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "usage.input_tokens_details", "usage_input_tokens_details_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "usage.input_tokens_details.cached_tokens", "usage_cached_tokens", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "usage.output_tokens", "usage_output_tokens", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "usage.output_tokens_details", "usage_output_tokens_details_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "usage.output_tokens_details.reasoning_tokens", "usage_reasoning_tokens", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "usage.total_tokens", "usage_total_tokens", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "usage.cost", "usage_cost", STRAPPY_RESPONSE_FIELD_REAL },
  { "usage.cost_details", "usage_cost_details_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "usage.cost_details.upstream_inference_cost", "usage_upstream_inference_cost", STRAPPY_RESPONSE_FIELD_REAL },
  { "usage.cost_details.upstream_inference_input_cost", "usage_upstream_input_cost", STRAPPY_RESPONSE_FIELD_REAL },
  { "usage.cost_details.upstream_inference_output_cost", "usage_upstream_output_cost", STRAPPY_RESPONSE_FIELD_REAL },
  { "usage.is_byok", "usage_is_byok", STRAPPY_RESPONSE_FIELD_INTEGER },
  { "user", "user", STRAPPY_RESPONSE_FIELD_TEXT }
};

static const strappy_response_field_definition
strappy_response_item_fields[] = {
  { "action", "action_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "advice", "advice", STRAPPY_RESPONSE_FIELD_TEXT },
  { "analysis", "analysis_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "annotations", "annotations_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "approval_request_id", "approval_request_id", STRAPPY_RESPONSE_FIELD_TEXT },
  { "approve", "approve", STRAPPY_RESPONSE_FIELD_TEXT },
  { "arguments", "arguments", STRAPPY_RESPONSE_FIELD_TEXT },
  { "call_id", "call_id", STRAPPY_RESPONSE_FIELD_TEXT },
  { "code", "code", STRAPPY_RESPONSE_FIELD_TEXT },
  { "command", "command_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "container_id", "container_id", STRAPPY_RESPONSE_FIELD_TEXT },
  { "content", "content_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "datetime", "datetime", STRAPPY_RESPONSE_FIELD_TEXT },
  { "encrypted_content", "encrypted_content", STRAPPY_RESPONSE_FIELD_TEXT },
  { "environment", "environment_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "error", "error_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "exitCode", "exit_code", STRAPPY_RESPONSE_FIELD_TEXT },
  { "failed_models", "failed_models_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "failure_reason", "failure_reason", STRAPPY_RESPONSE_FIELD_TEXT },
  { "filePath", "file_path", STRAPPY_RESPONSE_FIELD_TEXT },
  { "file_id", "file_id", STRAPPY_RESPONSE_FIELD_TEXT },
  { "filename", "filename", STRAPPY_RESPONSE_FIELD_TEXT },
  { "format", "format", STRAPPY_RESPONSE_FIELD_TEXT },
  { "httpStatus", "http_status", STRAPPY_RESPONSE_FIELD_TEXT },
  { "id", "api_item_id", STRAPPY_RESPONSE_FIELD_TEXT },
  { "imageB64", "image_base64", STRAPPY_RESPONSE_FIELD_TEXT },
  { "imageUrl", "image_url", STRAPPY_RESPONSE_FIELD_TEXT },
  { "input", "input", STRAPPY_RESPONSE_FIELD_TEXT },
  { "instance_name", "instance_name", STRAPPY_RESPONSE_FIELD_TEXT },
  { "key", "key", STRAPPY_RESPONSE_FIELD_TEXT },
  { "language", "language", STRAPPY_RESPONSE_FIELD_TEXT },
  { "logprobs", "logprobs_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "max_output_length", "max_output_length", STRAPPY_RESPONSE_FIELD_TEXT },
  { "model", "model", STRAPPY_RESPONSE_FIELD_TEXT },
  { "name", "name", STRAPPY_RESPONSE_FIELD_TEXT },
  { "namespace", "namespace", STRAPPY_RESPONSE_FIELD_TEXT },
  { "operation", "operation_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "outcome", "outcome_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "output", "output", STRAPPY_RESPONSE_FIELD_TEXT },
  { "outputs", "outputs_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "pending_safety_checks", "pending_safety_checks_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "phase", "phase", STRAPPY_RESPONSE_FIELD_TEXT },
  { "prompt", "prompt", STRAPPY_RESPONSE_FIELD_TEXT },
  { "queries", "queries_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "query", "query", STRAPPY_RESPONSE_FIELD_TEXT },
  { "reason", "reason", STRAPPY_RESPONSE_FIELD_TEXT },
  { "refusal", "refusal", STRAPPY_RESPONSE_FIELD_TEXT },
  { "responses", "responses_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "result", "result", STRAPPY_RESPONSE_FIELD_TEXT },
  { "results", "results_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "revisedPrompt", "revised_prompt", STRAPPY_RESPONSE_FIELD_TEXT },
  { "role", "role", STRAPPY_RESPONSE_FIELD_TEXT },
  { "screenshotB64", "screenshot_base64", STRAPPY_RESPONSE_FIELD_TEXT },
  { "serverLabel", "server_label", STRAPPY_RESPONSE_FIELD_TEXT },
  { "server_label", "server_label_snake", STRAPPY_RESPONSE_FIELD_TEXT },
  { "signature", "signature", STRAPPY_RESPONSE_FIELD_TEXT },
  { "sources", "sources_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "status", "status", STRAPPY_RESPONSE_FIELD_TEXT },
  { "stderr", "stderr", STRAPPY_RESPONSE_FIELD_TEXT },
  { "stdout", "stdout", STRAPPY_RESPONSE_FIELD_TEXT },
  { "summary", "summary_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "task_description", "task_description", STRAPPY_RESPONSE_FIELD_TEXT },
  { "task_name", "task_name", STRAPPY_RESPONSE_FIELD_TEXT },
  { "text", "text", STRAPPY_RESPONSE_FIELD_TEXT },
  { "timezone", "timezone", STRAPPY_RESPONSE_FIELD_TEXT },
  { "title", "title", STRAPPY_RESPONSE_FIELD_TEXT },
  { "toolName", "tool_name", STRAPPY_RESPONSE_FIELD_TEXT },
  { "tools", "tools_json", STRAPPY_RESPONSE_FIELD_TEXT },
  { "type", "type", STRAPPY_RESPONSE_FIELD_TEXT },
  { "url", "url", STRAPPY_RESPONSE_FIELD_TEXT },
  { "value", "value_json", STRAPPY_RESPONSE_FIELD_TEXT }
};

static int strappy_db_copy_default_openrouter_model(sqlite3 *db,
                                                    char **model_id_out,
                                                    char **error_out);

static int strappy_db_enable_write_ahead_log(sqlite3 *db, char **error_out)
{
  sqlite3_stmt *stmt;
  const unsigned char *journal_mode;
  int rc;
  int finalize_rc;
  int ok;

  if (db == NULL) {
    strappy_set_error(error_out, "Database is not open.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, "PRAGMA journal_mode = WAL;", -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not enable database write-ahead log: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  ok = 1;
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    journal_mode = sqlite3_column_text(stmt, 0);
    if ((journal_mode == NULL) ||
        (strcmp((const char *)journal_mode, "wal") != 0)) {
      strappy_set_formatted_error(error_out,
                                  "Could not enable database write-ahead log: "
                                  "journal mode is %s",
                                  (journal_mode != NULL) ?
                                    (const char *)journal_mode : "unknown");
      ok = 0;
    }
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not enable database write-ahead log: %s",
                                sqlite3_errmsg(db));
    ok = 0;
  }

  finalize_rc = sqlite3_finalize(stmt);
  if (ok && (finalize_rc != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not enable database write-ahead log: %s",
                                sqlite3_errmsg(db));
    ok = 0;
  }

  return ok;
}

void strappy_session_record_init(strappy_session_record *record)
{
  if (record == NULL) {
    return;
  }

  record->session_id = 0;
  record->name = NULL;
  record->prompt = NULL;
  record->response = NULL;
  record->model = NULL;
  record->created_at = NULL;
  record->web_search_enabled = 1;
  record->streaming_enabled = 0;
  record->http_status = 0L;
}

void strappy_session_record_destroy(strappy_session_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->prompt);
  free(record->name);
  free(record->response);
  free(record->model);
  free(record->created_at);
  strappy_session_record_init(record);
}

void strappy_session_record_list_init(strappy_session_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
}

void strappy_session_record_list_destroy(strappy_session_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_session_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_session_record_list_init(list);
}

void strappy_session_message_record_init(strappy_session_message_record *record)
{
  if (record == NULL) {
    return;
  }

  record->message_id = 0;
  record->session_id = 0;
  record->turn_id = 0;
  record->round_index = 0L;
  record->attempt_index = 0L;
  record->cumulative_usage_cost = 0.0;
  record->has_cumulative_usage_cost = 0;
  record->turn_key = NULL;
  record->prompt_group_key = NULL;
  record->actor = NULL;
  record->kind = NULL;
  record->api_role = NULL;
  record->render_role = NULL;
  record->role = NULL;
  record->content = NULL;
  record->model = NULL;
  record->metadata_json = NULL;
  record->render_state_json = NULL;
  record->message_json = NULL;
  record->reasoning = NULL;
  record->message_key = NULL;
  record->target_message_key = NULL;
  record->direction = NULL;
  record->tool_call_id = NULL;
  record->tool_name = NULL;
  record->arguments_json = NULL;
  record->result_json = NULL;
  record->response_item_action_json = NULL;
  record->response_item_url = NULL;
  record->response_item_title = NULL;
  record->response_item_status = NULL;
  record->response_item_http_status = NULL;
  record->created_at = NULL;
  record->include_in_context = 0;
  record->is_error = 0;
  record->http_status = 0L;
}

void strappy_session_message_record_destroy(strappy_session_message_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->turn_key);
  free(record->prompt_group_key);
  free(record->actor);
  free(record->kind);
  free(record->api_role);
  free(record->render_role);
  free(record->role);
  free(record->content);
  free(record->model);
  free(record->metadata_json);
  free(record->render_state_json);
  free(record->message_json);
  free(record->reasoning);
  free(record->message_key);
  free(record->target_message_key);
  free(record->direction);
  free(record->tool_call_id);
  free(record->tool_name);
  free(record->arguments_json);
  free(record->result_json);
  free(record->response_item_action_json);
  free(record->response_item_url);
  free(record->response_item_title);
  free(record->response_item_status);
  free(record->response_item_http_status);
  free(record->created_at);
  strappy_session_message_record_init(record);
}

void strappy_session_message_record_list_init(strappy_session_message_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
}

void strappy_session_message_record_list_destroy(strappy_session_message_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_session_message_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_session_message_record_list_init(list);
}

void strappy_response_item_raw_record_list_init(
  strappy_response_item_raw_record_list *list)
{
  if (list == NULL) {
    return;
  }
  list->records = NULL;
  list->count = 0U;
}

void strappy_response_item_raw_record_list_destroy(
  strappy_response_item_raw_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }
  for (index = 0U; index < list->count; index++) {
    free(list->records[index].raw_json);
  }
  free(list->records);
  strappy_response_item_raw_record_list_init(list);
}

void strappy_discovered_database_record_init(strappy_discovered_database_record *record)
{
  if (record == NULL) {
    return;
  }

  record->catalog_id = 0;
  record->assistant_database_id = NULL;
  record->path = NULL;
  record->size = 0;
  record->modified_at = 0;
  record->device = 0U;
  record->inode = 0U;
  record->is_valid_sqlite = 0;
  record->validation_error = NULL;
  record->scan_status = NULL;
  record->user_decision = NULL;
  record->scan_root = NULL;
  record->app_group_key = NULL;
  record->app_name = NULL;
  record->app_bundle_id = NULL;
  record->app_container_path = NULL;
  record->app_bundle_path = NULL;
  record->app_source = NULL;
  record->origin_kind = NULL;
  record->location_tail = NULL;
  record->hidden = 0;
  record->first_seen_at = NULL;
  record->last_seen_at = NULL;
  record->last_scanned_at = NULL;
}

void strappy_discovered_database_record_destroy(strappy_discovered_database_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->assistant_database_id);
  free(record->path);
  free(record->validation_error);
  free(record->scan_status);
  free(record->user_decision);
  free(record->scan_root);
  free(record->app_group_key);
  free(record->app_name);
  free(record->app_bundle_id);
  free(record->app_container_path);
  free(record->app_bundle_path);
  free(record->app_source);
  free(record->origin_kind);
  free(record->location_tail);
  free(record->first_seen_at);
  free(record->last_seen_at);
  free(record->last_scanned_at);
  strappy_discovered_database_record_init(record);
}

void strappy_discovered_database_record_list_init(
  strappy_discovered_database_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
}

void strappy_discovered_database_record_list_destroy(
  strappy_discovered_database_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_discovered_database_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_discovered_database_record_list_init(list);
}

void strappy_openrouter_model_record_init(strappy_openrouter_model_record *record)
{
  if (record == NULL) {
    return;
  }

  record->model_id = NULL;
  record->canonical_slug = NULL;
  record->hugging_face_id = NULL;
  record->name = NULL;
  record->description = NULL;
  record->context_length = 0;
  record->created = 0;
  record->architecture_modality = NULL;
  record->architecture_tokenizer = NULL;
  record->architecture_instruct_type = NULL;
  record->pricing_prompt = NULL;
  record->pricing_completion = NULL;
  record->pricing_request = NULL;
  record->pricing_image = NULL;
  record->pricing_audio = NULL;
  record->pricing_web_search = NULL;
  record->pricing_internal_reasoning = NULL;
  record->pricing_input_cache_read = NULL;
  record->pricing_input_cache_write = NULL;
  record->top_provider_context_length = 0;
  record->top_provider_max_completion_tokens = 0;
  record->top_provider_is_moderated = 0;
  record->knowledge_cutoff = NULL;
  record->expiration_date = NULL;
  record->links_details = NULL;
  record->links_json = NULL;
  record->reasoning_json = NULL;
  record->benchmarks_json = NULL;
  record->default_parameters_json = NULL;
  record->per_request_limits_json = NULL;
  record->raw_json = NULL;
  record->fetched_at = NULL;
  record->selected = 0;
  record->allowed = 0;
}

void strappy_openrouter_model_record_destroy(strappy_openrouter_model_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->model_id);
  free(record->canonical_slug);
  free(record->hugging_face_id);
  free(record->name);
  free(record->description);
  free(record->architecture_modality);
  free(record->architecture_tokenizer);
  free(record->architecture_instruct_type);
  free(record->pricing_prompt);
  free(record->pricing_completion);
  free(record->pricing_request);
  free(record->pricing_image);
  free(record->pricing_audio);
  free(record->pricing_web_search);
  free(record->pricing_internal_reasoning);
  free(record->pricing_input_cache_read);
  free(record->pricing_input_cache_write);
  free(record->knowledge_cutoff);
  free(record->expiration_date);
  free(record->links_details);
  free(record->links_json);
  free(record->reasoning_json);
  free(record->benchmarks_json);
  free(record->default_parameters_json);
  free(record->per_request_limits_json);
  free(record->raw_json);
  free(record->fetched_at);
  strappy_openrouter_model_record_init(record);
}

void strappy_openrouter_model_record_list_init(
  strappy_openrouter_model_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
}

void strappy_openrouter_model_record_list_destroy(
  strappy_openrouter_model_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_openrouter_model_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_openrouter_model_record_list_init(list);
}

static int strappy_db_open(const char *db_path,
                           sqlite3 **db_out,
                           char **error_out)
{
  sqlite3 *db;
  int flags;
  int rc;

  if (db_out == NULL) {
    strappy_set_error(error_out, "strappy_db_open received no output.");
    return 0;
  }
  *db_out = NULL;

  if ((db_path == NULL) || (db_path[0] == '\0')) {
    strappy_set_error(error_out, "Session database path is not configured.");
    return 0;
  }

  db = NULL;
  flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  rc = sqlite3_open_v2(db_path, &db, flags, NULL);
  if (rc != SQLITE_OK) {
    const char *message = "unknown sqlite error";
    if (db != NULL) {
      message = sqlite3_errmsg(db);
    }
    strappy_set_formatted_error(error_out,
                                "Could not open session database: %s",
                                message);
    if (db != NULL) {
      sqlite3_close(db);
    }
    return 0;
  }

  sqlite3_busy_timeout(db, 5000);
  if (!strappy_db_enable_write_ahead_log(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  *db_out = db;
  return 1;
}

static int strappy_db_exec(sqlite3 *db,
                           const char *sql,
                           const char *error_prefix,
                           char **error_out)
{
  char *sqlite_error;
  int rc;

  sqlite_error = NULL;
  rc = sqlite3_exec(db, sql, NULL, NULL, &sqlite_error);
  if (rc != SQLITE_OK) {
    if (sqlite_error != NULL) {
      strappy_set_formatted_error(error_out,
                                  "%s: %s",
                                  error_prefix,
                                  sqlite_error);
      sqlite3_free(sqlite_error);
    } else {
      strappy_set_formatted_error(error_out,
                                  "%s: sqlite %d",
                                  error_prefix,
                                  rc);
    }
    return 0;
  }

  return 1;
}

typedef struct strappy_db_sql_buffer {
  char *data;
  size_t length;
} strappy_db_sql_buffer;

static void strappy_db_sql_buffer_destroy(strappy_db_sql_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }
  free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0U;
}

static int strappy_db_sql_buffer_append(strappy_db_sql_buffer *buffer,
                                        const char *text)
{
  size_t text_length;
  char *next;

  if ((buffer == NULL) || (text == NULL)) {
    return 0;
  }
  text_length = strlen(text);
  if (buffer->length > (((size_t)-1) - text_length - 1U)) {
    return 0;
  }
  next = (char *)realloc(buffer->data,
                         buffer->length + text_length + 1U);
  if (next == NULL) {
    return 0;
  }
  buffer->data = next;
  memcpy(buffer->data + buffer->length, text, text_length);
  buffer->length += text_length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

static const char *strappy_db_response_field_sql_type(
  strappy_response_field_type type)
{
  if (type == STRAPPY_RESPONSE_FIELD_INTEGER) {
    return "INTEGER";
  }
  if (type == STRAPPY_RESPONSE_FIELD_REAL) {
    return "REAL";
  }
  return "TEXT";
}

static int strappy_db_append_response_schema_fields(
  strappy_db_sql_buffer *buffer,
  const char *prefix,
  const strappy_response_field_definition *fields,
  size_t field_count)
{
  size_t index;

  for (index = 0U; index < field_count; index++) {
    if (!strappy_db_sql_buffer_append(buffer, ",") ||
        !strappy_db_sql_buffer_append(buffer, prefix) ||
        !strappy_db_sql_buffer_append(buffer, fields[index].column_name) ||
        !strappy_db_sql_buffer_append(buffer, " ") ||
        !strappy_db_sql_buffer_append(
          buffer,
          strappy_db_response_field_sql_type(fields[index].type))) {
      return 0;
    }
  }
  return 1;
}

static int strappy_db_create_response_calls_schema(sqlite3 *db,
                                                   char **error_out)
{
  strappy_db_sql_buffer buffer;
  int ok;

  memset(&buffer, 0, sizeof(buffer));
  ok =
    strappy_db_sql_buffer_append(
      &buffer,
      "CREATE TABLE IF NOT EXISTS response_api_calls ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "session_id INTEGER NOT NULL,"
      "previous_call_id INTEGER,"
      "prompt_group_key TEXT NOT NULL,"
      "request_kind TEXT NOT NULL,"
      "round_index INTEGER NOT NULL,"
      "attempt_index INTEGER NOT NULL,"
      "new_input_start_index INTEGER NOT NULL DEFAULT -1,"
      "state TEXT NOT NULL DEFAULT 'pending',"
      "is_error INTEGER NOT NULL DEFAULT 0,"
      "request_method TEXT NOT NULL,"
      "request_url TEXT NOT NULL,"
      "request_headers_json TEXT NOT NULL,"
      "request_started_at TEXT NOT NULL DEFAULT "
      "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
      "transport_completed_at TEXT,"
      "request_started_at_ms INTEGER,"
      "transport_completed_at_ms INTEGER,"
      "http_status INTEGER NOT NULL DEFAULT 0,"
      "curl_code INTEGER NOT NULL DEFAULT 0,"
      "retry_after_seconds INTEGER NOT NULL DEFAULT 0,"
      "request_bytes INTEGER NOT NULL DEFAULT 0,"
      "response_bytes INTEGER NOT NULL DEFAULT 0,"
      "name_lookup_seconds REAL NOT NULL DEFAULT 0,"
      "connect_seconds REAL NOT NULL DEFAULT 0,"
      "start_transfer_seconds REAL NOT NULL DEFAULT 0,"
      "total_seconds REAL NOT NULL DEFAULT 0,"
      "effective_url TEXT,"
      "transport_error TEXT,"
      "response_content_type TEXT,"
      "http_request_id TEXT,"
      "http_generation_id TEXT,"
      "rate_limit_limit TEXT,"
      "rate_limit_remaining TEXT,"
      "rate_limit_reset TEXT,"
      "rate_limit_limit_requests TEXT,"
      "rate_limit_remaining_requests TEXT,"
      "rate_limit_reset_requests TEXT,"
      "rate_limit_limit_tokens TEXT,"
      "rate_limit_remaining_tokens TEXT,"
      "rate_limit_reset_tokens TEXT") &&
    strappy_db_append_response_schema_fields(
      &buffer,
      "request_",
      strappy_response_request_fields,
      sizeof(strappy_response_request_fields) /
        sizeof(strappy_response_request_fields[0])) &&
    strappy_db_append_response_schema_fields(
      &buffer,
      "response_",
      strappy_response_result_fields,
      sizeof(strappy_response_result_fields) /
        sizeof(strappy_response_result_fields[0])) &&
    strappy_db_sql_buffer_append(
      &buffer,
      ",request_raw_json TEXT NOT NULL,"
      "response_headers_raw TEXT,"
      "response_raw_json TEXT,"
      "FOREIGN KEY(session_id) REFERENCES sessions(id),"
      "FOREIGN KEY(previous_call_id) REFERENCES response_api_calls(id)"
      ");");

  if (!ok) {
    strappy_db_sql_buffer_destroy(&buffer);
    strappy_set_error(error_out,
                      "Could not allocate Responses call schema.");
    return 0;
  }
  ok = strappy_db_exec(db,
                       buffer.data,
                       "Could not create Responses call schema",
                       error_out);
  strappy_db_sql_buffer_destroy(&buffer);
  if (!ok) {
    return 0;
  }

  return strappy_db_exec(
           db,
           "CREATE INDEX IF NOT EXISTS response_api_calls_session_id_id_idx "
           "ON response_api_calls(session_id, id);",
           "Could not create Responses call index",
           error_out) &&
         strappy_db_exec(
           db,
           "CREATE INDEX IF NOT EXISTS response_api_calls_prompt_group_idx "
           "ON response_api_calls(session_id, prompt_group_key, id);",
           "Could not create Responses prompt group index",
           error_out);
}

static int strappy_db_create_response_items_schema(sqlite3 *db,
                                                   char **error_out)
{
  strappy_db_sql_buffer buffer;
  int ok;

  memset(&buffer, 0, sizeof(buffer));
  ok =
    strappy_db_sql_buffer_append(
      &buffer,
      "CREATE TABLE IF NOT EXISTS response_api_items ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "response_call_id INTEGER NOT NULL,"
      "session_id INTEGER NOT NULL,"
      "direction TEXT NOT NULL,"
      "item_index INTEGER NOT NULL,"
      "is_canonical INTEGER NOT NULL DEFAULT 0,"
      "timeline_visible INTEGER NOT NULL DEFAULT 0,"
      "display_role TEXT NOT NULL,"
      "display_text TEXT NOT NULL DEFAULT '',"
      "is_error INTEGER NOT NULL DEFAULT 0,"
      "created_at TEXT NOT NULL DEFAULT "
      "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))") &&
    strappy_db_append_response_schema_fields(
      &buffer,
      "",
      strappy_response_item_fields,
      sizeof(strappy_response_item_fields) /
        sizeof(strappy_response_item_fields[0])) &&
    strappy_db_sql_buffer_append(
      &buffer,
      ",raw_json TEXT NOT NULL,"
      "FOREIGN KEY(response_call_id) REFERENCES response_api_calls(id),"
      "FOREIGN KEY(session_id) REFERENCES sessions(id),"
      "UNIQUE(response_call_id, direction, item_index)"
      ");");
  if (!ok) {
    strappy_db_sql_buffer_destroy(&buffer);
    strappy_set_error(error_out,
                      "Could not allocate Responses item schema.");
    return 0;
  }
  ok = strappy_db_exec(db,
                       buffer.data,
                       "Could not create Responses item schema",
                       error_out);
  strappy_db_sql_buffer_destroy(&buffer);
  if (!ok) {
    return 0;
  }

  return strappy_db_exec(
           db,
           "CREATE INDEX IF NOT EXISTS response_api_items_call_direction_idx "
           "ON response_api_items(response_call_id, direction, item_index);",
           "Could not create Responses item call index",
           error_out) &&
         strappy_db_exec(
           db,
           "CREATE INDEX IF NOT EXISTS response_api_items_context_idx "
           "ON response_api_items(session_id, is_canonical, id);",
           "Could not create Responses item context index",
           error_out) &&
         strappy_db_exec(
           db,
           "CREATE INDEX IF NOT EXISTS response_api_items_timeline_idx "
           "ON response_api_items(session_id, timeline_visible, "
           "response_call_id, direction, item_index);",
           "Could not create Responses item timeline index",
           error_out);
}

static int strappy_db_create_responses_schema(sqlite3 *db, char **error_out)
{
  static const char *parts_sql =
    "CREATE TABLE IF NOT EXISTS response_api_item_parts ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "response_item_id INTEGER NOT NULL,"
    "collection_name TEXT NOT NULL,"
    "part_index INTEGER NOT NULL,"
    "type TEXT,"
    "text TEXT,"
    "refusal TEXT,"
    "annotations_json TEXT,"
    "logprobs_json TEXT,"
    "raw_json TEXT NOT NULL,"
    "FOREIGN KEY(response_item_id) REFERENCES response_api_items(id),"
    "UNIQUE(response_item_id, collection_name, part_index)"
    ");";
  static const char *parts_index_sql =
    "CREATE INDEX IF NOT EXISTS response_api_item_parts_item_idx "
    "ON response_api_item_parts(response_item_id, collection_name, part_index);";
  static const char *tools_sql =
    "CREATE TABLE IF NOT EXISTS response_tool_executions ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "session_id INTEGER NOT NULL,"
    "response_call_id INTEGER NOT NULL,"
    "response_item_id INTEGER,"
    "output_index INTEGER NOT NULL,"
    "call_id TEXT NOT NULL,"
    "tool_name TEXT NOT NULL,"
    "arguments_json TEXT NOT NULL,"
    "status TEXT NOT NULL,"
    "output_json TEXT,"
    "error_text TEXT,"
    "started_at_ms INTEGER NOT NULL,"
    "completed_at_ms INTEGER NOT NULL,"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "FOREIGN KEY(session_id) REFERENCES sessions(id),"
    "FOREIGN KEY(response_call_id) REFERENCES response_api_calls(id),"
    "FOREIGN KEY(response_item_id) REFERENCES response_api_items(id)"
    ");";
  static const char *tools_index_sql =
    "CREATE INDEX IF NOT EXISTS response_tool_executions_call_idx "
    "ON response_tool_executions(response_call_id, output_index, id);";

  return strappy_db_create_response_calls_schema(db, error_out) &&
         strappy_db_create_response_items_schema(db, error_out) &&
         strappy_db_exec(db,
                         parts_sql,
                         "Could not create Responses item part schema",
                         error_out) &&
         strappy_db_exec(db,
                         parts_index_sql,
                         "Could not create Responses item part index",
                         error_out) &&
         strappy_db_exec(db,
                         tools_sql,
                         "Could not create Responses tool execution schema",
                         error_out) &&
         strappy_db_exec(db,
                         tools_index_sql,
                         "Could not create Responses tool execution index",
                         error_out);
}

static int strappy_db_ensure_schema(sqlite3 *db, char **error_out)
{
  static const char *sessions_sql =
    "CREATE TABLE IF NOT EXISTS sessions ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "name TEXT NOT NULL DEFAULT '',"
    "prompt TEXT NOT NULL,"
    "response TEXT NOT NULL,"
    "model TEXT,"
    "http_status INTEGER NOT NULL DEFAULT 0,"
    "web_search_enabled INTEGER NOT NULL DEFAULT 1,"
    "streaming_enabled INTEGER NOT NULL DEFAULT 0,"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *turns_sql =
    "CREATE TABLE IF NOT EXISTS session_turns ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "session_id INTEGER NOT NULL,"
    "turn_key TEXT NOT NULL,"
    "prompt_group_key TEXT,"
    "actor TEXT NOT NULL,"
    "api_role TEXT NOT NULL DEFAULT 'user',"
    "render_role TEXT NOT NULL,"
    "context_policy TEXT NOT NULL DEFAULT 'full',"
    "prompt TEXT NOT NULL DEFAULT '',"
    "status TEXT NOT NULL DEFAULT 'complete',"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "FOREIGN KEY(session_id) REFERENCES sessions(id),"
    "UNIQUE(session_id, turn_key)"
    ");";
  static const char *messages_sql =
    "CREATE TABLE IF NOT EXISTS session_messages ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "session_id INTEGER NOT NULL,"
    "turn_id INTEGER,"
    "turn_key TEXT,"
    "prompt_group_key TEXT,"
    "actor TEXT,"
    "kind TEXT NOT NULL DEFAULT 'message',"
    "api_role TEXT,"
    "render_role TEXT NOT NULL,"
    "role TEXT NOT NULL,"
    "content TEXT NOT NULL,"
    "model TEXT,"
    "http_status INTEGER NOT NULL DEFAULT 0,"
    "metadata_json TEXT,"
    "render_state_json TEXT,"
    "message_json TEXT,"
    "reasoning TEXT,"
    "message_key TEXT,"
    "target_message_key TEXT,"
    "tool_call_id TEXT,"
    "tool_name TEXT,"
    "arguments_json TEXT,"
    "result_json TEXT,"
    "include_in_context INTEGER NOT NULL DEFAULT 1,"
    "is_error INTEGER NOT NULL DEFAULT 0,"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "FOREIGN KEY(session_id) REFERENCES sessions(id),"
    "FOREIGN KEY(turn_id) REFERENCES session_turns(id)"
    ");";
  static const char *turns_index_sql =
    "CREATE INDEX IF NOT EXISTS session_turns_session_id_id_idx "
    "ON session_turns(session_id, id);";
  static const char *messages_index_sql =
    "CREATE INDEX IF NOT EXISTS session_messages_session_id_id_idx "
    "ON session_messages(session_id, id);";
  static const char *messages_context_index_sql =
    "CREATE INDEX IF NOT EXISTS session_messages_context_idx "
    "ON session_messages(session_id, include_in_context, id);";
  static const char *discovered_databases_sql =
    "CREATE TABLE IF NOT EXISTS discovered_databases ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "assistant_database_id TEXT UNIQUE,"
    "path TEXT NOT NULL UNIQUE,"
    "size INTEGER NOT NULL DEFAULT 0,"
    "modified_at INTEGER NOT NULL DEFAULT 0,"
    "device INTEGER NOT NULL DEFAULT 0,"
    "inode INTEGER NOT NULL DEFAULT 0,"
    "is_valid_sqlite INTEGER NOT NULL DEFAULT 0,"
    "validation_error TEXT,"
    "scan_status TEXT NOT NULL DEFAULT 'candidate',"
    "user_decision TEXT NOT NULL DEFAULT 'unknown',"
    "scan_root TEXT,"
    "app_group_key TEXT,"
    "app_name TEXT,"
    "app_bundle_id TEXT,"
    "app_container_path TEXT,"
    "app_bundle_path TEXT,"
    "app_source TEXT,"
    "origin_kind TEXT NOT NULL DEFAULT 'other',"
    "location_tail TEXT,"
    "first_seen_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "last_seen_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "last_scanned_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *discovered_databases_device_inode_index_sql =
    "CREATE INDEX IF NOT EXISTS discovered_databases_device_inode_idx "
    "ON discovered_databases(device, inode);";
  static const char *discovered_databases_decision_index_sql =
    "CREATE INDEX IF NOT EXISTS discovered_databases_user_decision_idx "
    "ON discovered_databases(user_decision);";
  static const char *discovered_databases_app_group_index_sql =
    "CREATE INDEX IF NOT EXISTS discovered_databases_app_group_idx "
    "ON discovered_databases(app_group_key, user_decision, path);";
  static const char *database_access_settings_sql =
    "CREATE TABLE IF NOT EXISTS database_access_settings ("
    "path TEXT PRIMARY KEY,"
    "user_decision TEXT NOT NULL DEFAULT 'unknown',"
    "assistant_database_id TEXT UNIQUE,"
    "hidden INTEGER NOT NULL DEFAULT 0,"
    "created_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
    "updated_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *database_access_settings_decision_index_sql =
    "CREATE INDEX IF NOT EXISTS database_access_settings_decision_idx "
    "ON database_access_settings(user_decision);";
  static const char *app_settings_sql =
    "CREATE TABLE IF NOT EXISTS app_settings ("
    "key TEXT PRIMARY KEY,"
    "value TEXT,"
    "updated_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *openrouter_models_sql =
    "CREATE TABLE IF NOT EXISTS openrouter_models ("
    "id TEXT PRIMARY KEY,"
    "canonical_slug TEXT,"
    "hugging_face_id TEXT,"
    "name TEXT,"
    "description TEXT,"
    "context_length INTEGER NOT NULL DEFAULT 0,"
    "created INTEGER NOT NULL DEFAULT 0,"
    "architecture_modality TEXT,"
    "architecture_tokenizer TEXT,"
    "architecture_instruct_type TEXT,"
    "pricing_prompt TEXT,"
    "pricing_completion TEXT,"
    "pricing_request TEXT,"
    "pricing_image TEXT,"
    "pricing_audio TEXT,"
    "pricing_web_search TEXT,"
    "pricing_internal_reasoning TEXT,"
    "pricing_input_cache_read TEXT,"
    "pricing_input_cache_write TEXT,"
    "top_provider_context_length INTEGER NOT NULL DEFAULT 0,"
    "top_provider_max_completion_tokens INTEGER NOT NULL DEFAULT 0,"
    "top_provider_is_moderated INTEGER NOT NULL DEFAULT 0,"
    "knowledge_cutoff TEXT,"
    "expiration_date TEXT,"
    "links_details TEXT,"
    "links_json TEXT,"
    "reasoning_json TEXT,"
    "benchmarks_json TEXT,"
    "default_parameters_json TEXT,"
    "per_request_limits_json TEXT,"
    "raw_json TEXT,"
    "fetched_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *openrouter_models_name_index_sql =
    "CREATE INDEX IF NOT EXISTS openrouter_models_name_idx "
    "ON openrouter_models(name, id);";
  static const char *openrouter_model_settings_sql =
    "CREATE TABLE IF NOT EXISTS openrouter_model_settings ("
    "model_id TEXT PRIMARY KEY,"
    "allowed INTEGER NOT NULL DEFAULT 0,"
    "updated_at TEXT NOT NULL DEFAULT "
    "(strftime('%Y-%m-%dT%H:%M:%fZ','now'))"
    ");";
  static const char *openrouter_model_input_modalities_sql =
    "CREATE TABLE IF NOT EXISTS openrouter_model_input_modalities ("
    "model_id TEXT NOT NULL,"
    "modality TEXT NOT NULL,"
    "PRIMARY KEY(model_id, modality),"
    "FOREIGN KEY(model_id) REFERENCES openrouter_models(id) ON DELETE CASCADE"
    ");";
  static const char *openrouter_model_output_modalities_sql =
    "CREATE TABLE IF NOT EXISTS openrouter_model_output_modalities ("
    "model_id TEXT NOT NULL,"
    "modality TEXT NOT NULL,"
    "PRIMARY KEY(model_id, modality),"
    "FOREIGN KEY(model_id) REFERENCES openrouter_models(id) ON DELETE CASCADE"
    ");";
  static const char *openrouter_model_supported_parameters_sql =
    "CREATE TABLE IF NOT EXISTS openrouter_model_supported_parameters ("
    "model_id TEXT NOT NULL,"
    "parameter TEXT NOT NULL,"
    "PRIMARY KEY(model_id, parameter),"
    "FOREIGN KEY(model_id) REFERENCES openrouter_models(id) ON DELETE CASCADE"
    ");";
  static const char *openrouter_model_supported_voices_sql =
    "CREATE TABLE IF NOT EXISTS openrouter_model_supported_voices ("
    "model_id TEXT NOT NULL,"
    "voice TEXT NOT NULL,"
    "PRIMARY KEY(model_id, voice),"
    "FOREIGN KEY(model_id) REFERENCES openrouter_models(id) ON DELETE CASCADE"
    ");";
  static const char *user_version_sql = "PRAGMA user_version = 1;";
  if (!strappy_db_exec(db,
                       sessions_sql,
                       "Could not create session schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       turns_sql,
                       "Could not create session turn schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       turns_index_sql,
                       "Could not create session turn index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       messages_sql,
                       "Could not create session message schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       messages_index_sql,
                       "Could not create session message index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       messages_context_index_sql,
                       "Could not create session context message index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_create_responses_schema(db, error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       discovered_databases_sql,
                       "Could not create discovered database schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       discovered_databases_device_inode_index_sql,
                       "Could not create discovered database device index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       discovered_databases_decision_index_sql,
                       "Could not create discovered database decision index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       discovered_databases_app_group_index_sql,
                       "Could not create discovered database app group index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       database_access_settings_sql,
                       "Could not create database access settings schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       database_access_settings_decision_index_sql,
                       "Could not create database access settings decision index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       app_settings_sql,
                       "Could not create app settings schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_models_sql,
                       "Could not create OpenRouter model schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_models_name_index_sql,
                       "Could not create OpenRouter model name index",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_model_settings_sql,
                       "Could not create OpenRouter model settings schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL,
                       "Could not create built-in OpenRouter default model",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_model_input_modalities_sql,
                       "Could not create OpenRouter input modality schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_model_output_modalities_sql,
                       "Could not create OpenRouter output modality schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_model_supported_parameters_sql,
                       "Could not create OpenRouter supported parameter schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       openrouter_model_supported_voices_sql,
                       "Could not create OpenRouter supported voice schema",
                       error_out)) {
    return 0;
  }

  if (!strappy_db_exec(db,
                       user_version_sql,
                       "Could not set database user_version",
                       error_out)) {
    return 0;
  }

  return 1;
}

static char *strappy_db_column_string(sqlite3_stmt *stmt, int column)
{
  const unsigned char *value;

  value = sqlite3_column_text(stmt, column);
  if (value == NULL) {
    return NULL;
  }

  return strappy_string_duplicate((const char *)value);
}

static char *strappy_db_like_pattern_for_search(const char *search_text,
                                                char **error_out)
{
  char *pattern;
  size_t index;
  size_t input_length;
  size_t output_index;
  size_t pattern_length;

  if ((search_text == NULL) || (search_text[0] == '\0')) {
    return NULL;
  }

  input_length = strlen(search_text);
  pattern_length = 2U;
  for (index = 0U; index < input_length; index++) {
    size_t character_length;

    character_length = ((search_text[index] == '%') ||
                        (search_text[index] == '_') ||
                        (search_text[index] == '\\')) ? 2U : 1U;
    if (pattern_length > (((size_t)-1) - character_length - 1U)) {
      strappy_set_error(error_out, "OpenRouter model search text is too large.");
      return NULL;
    }
    pattern_length += character_length;
  }

  pattern = (char *)malloc(pattern_length + 1U);
  if (pattern == NULL) {
    strappy_set_error(error_out, "Could not allocate OpenRouter model search.");
    return NULL;
  }

  output_index = 0U;
  pattern[output_index++] = '%';
  for (index = 0U; index < input_length; index++) {
    if ((search_text[index] == '%') ||
        (search_text[index] == '_') ||
        (search_text[index] == '\\')) {
      pattern[output_index++] = '\\';
    }
    pattern[output_index++] = search_text[index];
  }
  pattern[output_index++] = '%';
  pattern[output_index] = '\0';
  return pattern;
}

static int strappy_db_assign_record_from_statement(strappy_session_record *record,
                                                   sqlite3_stmt *stmt,
                                                   char **error_out)
{
  char *name;
  char *prompt;
  char *response;
  char *model;
  char *created_at;

  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Session row request is incomplete.");
    return 0;
  }

  strappy_session_record_destroy(record);
  record->session_id = (long long)sqlite3_column_int64(stmt, 0);
  record->http_status = (long)sqlite3_column_int64(stmt, 5);
  record->web_search_enabled = sqlite3_column_int(stmt, 7) ? 1 : 0;
  record->streaming_enabled = sqlite3_column_int(stmt, 8) ? 1 : 0;

  name = strappy_db_column_string(stmt, 1);
  prompt = strappy_db_column_string(stmt, 2);
  response = strappy_db_column_string(stmt, 3);
  model = strappy_db_column_string(stmt, 4);
  created_at = strappy_db_column_string(stmt, 6);

  if ((name == NULL) || (prompt == NULL) || (response == NULL) ||
      (created_at == NULL)) {
    free(name);
    free(prompt);
    free(response);
    free(model);
    free(created_at);
    strappy_set_error(error_out, "Could not allocate session row.");
    return 0;
  }

  record->name = name;
  record->prompt = prompt;
  record->response = response;
  record->model = model;
  record->created_at = created_at;
  return 1;
}

static int strappy_db_assign_message_from_statement(
  strappy_session_message_record *record,
  sqlite3_stmt *stmt,
  char **error_out)
{
  char *turn_key;
  char *prompt_group_key;
  char *actor;
  char *kind;
  char *api_role;
  char *render_role;
  char *role;
  char *content;
  char *model;
  char *metadata_json;
  char *render_state_json;
  char *message_json;
  char *reasoning;
  char *message_key;
  char *target_message_key;
  char *tool_call_id;
  char *tool_name;
  char *arguments_json;
  char *result_json;
  char *created_at;

  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Session message row request is incomplete.");
    return 0;
  }

  strappy_session_message_record_destroy(record);
  record->message_id = (long long)sqlite3_column_int64(stmt, 0);
  record->session_id = (long long)sqlite3_column_int64(stmt, 1);
  record->turn_id = (long long)sqlite3_column_int64(stmt, 2);
  record->http_status = (long)sqlite3_column_int64(stmt, 12);
  record->include_in_context = sqlite3_column_int(stmt, 21) ? 1 : 0;
  record->is_error = sqlite3_column_int(stmt, 22) ? 1 : 0;

  turn_key = strappy_db_column_string(stmt, 3);
  prompt_group_key = strappy_db_column_string(stmt, 4);
  actor = strappy_db_column_string(stmt, 5);
  kind = strappy_db_column_string(stmt, 6);
  api_role = strappy_db_column_string(stmt, 7);
  render_role = strappy_db_column_string(stmt, 8);
  role = strappy_db_column_string(stmt, 9);
  content = strappy_db_column_string(stmt, 10);
  model = strappy_db_column_string(stmt, 11);
  metadata_json = strappy_db_column_string(stmt, 13);
  message_json = strappy_db_column_string(stmt, 14);
  reasoning = strappy_db_column_string(stmt, 15);
  message_key = strappy_db_column_string(stmt, 16);
  target_message_key = strappy_db_column_string(stmt, 17);
  tool_call_id = strappy_db_column_string(stmt, 18);
  tool_name = strappy_db_column_string(stmt, 19);
  arguments_json = strappy_db_column_string(stmt, 20);
  result_json = strappy_db_column_string(stmt, 23);
  created_at = strappy_db_column_string(stmt, 24);
  render_state_json = strappy_db_column_string(stmt, 25);

  if ((kind == NULL) || (render_role == NULL) || (role == NULL) ||
      (content == NULL) || (created_at == NULL)) {
    free(turn_key);
    free(prompt_group_key);
    free(actor);
    free(kind);
    free(api_role);
    free(render_role);
    free(role);
    free(content);
    free(model);
    free(metadata_json);
    free(render_state_json);
    free(message_json);
    free(reasoning);
    free(message_key);
    free(target_message_key);
    free(tool_call_id);
    free(tool_name);
    free(arguments_json);
    free(result_json);
    free(created_at);
    strappy_set_error(error_out, "Could not allocate session message row.");
    return 0;
  }

  record->turn_key = turn_key;
  record->prompt_group_key = prompt_group_key;
  record->actor = actor;
  record->kind = kind;
  record->api_role = api_role;
  record->render_role = render_role;
  record->role = role;
  record->content = content;
  record->model = model;
  record->metadata_json = metadata_json;
  record->render_state_json = render_state_json;
  record->message_json = message_json;
  record->reasoning = reasoning;
  record->message_key = message_key;
  record->target_message_key = target_message_key;
  record->tool_call_id = tool_call_id;
  record->tool_name = tool_name;
  record->arguments_json = arguments_json;
  record->result_json = result_json;
  record->created_at = created_at;
  return 1;
}

static char *strappy_db_create_assistant_database_id(long long catalog_id)
{
  char buffer[64];
  int result;

  if (catalog_id <= 0) {
    return NULL;
  }

  result = snprintf(buffer, sizeof(buffer), "db_%lld", catalog_id);
  if ((result <= 0) || ((size_t)result >= sizeof(buffer))) {
    return NULL;
  }

  return strappy_string_duplicate(buffer);
}

static const char *strappy_db_scan_status_for_input(
  const strappy_discovered_database_input *record)
{
  if ((record != NULL) && record->is_valid_sqlite) {
    return "valid";
  }
  if ((record != NULL) &&
      (record->validation_error != NULL) &&
      (record->validation_error[0] != '\0')) {
    return "invalid";
  }
  return "candidate";
}

static const char *strappy_db_origin_kind_for_input(
  const strappy_discovered_database_input *record)
{
  if ((record != NULL) &&
      (record->origin_kind != NULL) &&
      (record->origin_kind[0] != '\0')) {
    return record->origin_kind;
  }

  return "other";
}

static int strappy_db_is_valid_user_decision(const char *user_decision)
{
  if (user_decision == NULL) {
    return 0;
  }

  return ((strcmp(user_decision, "unknown") == 0) ||
          (strcmp(user_decision, "allowed") == 0) ||
          (strcmp(user_decision, "denied") == 0));
}

static int strappy_db_bind_optional_text(sqlite3 *db,
                                         sqlite3_stmt *stmt,
                                         int index,
                                         const char *value,
                                         const char *error_prefix,
                                         char **error_out)
{
  int rc;

  if ((value != NULL) && (value[0] != '\0')) {
    rc = sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT);
  } else {
    rc = sqlite3_bind_null(stmt, index);
  }

  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "%s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    return 0;
  }

  return 1;
}

static int strappy_db_ensure_database_access_setting(sqlite3 *db,
                                                     const char *path,
                                                     int initial_hidden,
                                                     char **error_out)
{
  static const char *sql =
    "INSERT OR IGNORE INTO database_access_settings "
    "(path, user_decision, hidden) VALUES (?, 'unknown', ?);";
  sqlite3_stmt *stmt;
  int rc;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(error_out, "Database access path is empty.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database access setting: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 2, initial_hidden ? 1 : 0);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind database access setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save database access setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_store_database_access_hidden(sqlite3 *db,
                                                   const char *path,
                                                   int hidden,
                                                   char **error_out)
{
  static const char *sql =
    "UPDATE database_access_settings "
    "SET hidden = ?, "
    "updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
    "WHERE path = ? AND hidden != ?;";
  sqlite3_stmt *stmt;
  int rc;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(error_out, "Database access hidden path is empty.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database access hidden update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_int(stmt, 1, hidden ? 1 : 0);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, path, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 3, hidden ? 1 : 0);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind database access hidden update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save database access hidden state: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_copy_database_access_assistant_id(
  sqlite3 *db,
  const char *path,
  char **assistant_database_id_out,
  char **error_out)
{
  static const char *sql =
    "SELECT assistant_database_id FROM database_access_settings "
    "WHERE path = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if (assistant_database_id_out == NULL) {
    strappy_set_error(error_out, "Database access id lookup has no output.");
    return 0;
  }
  *assistant_database_id_out = NULL;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database access id lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind database access id lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *assistant_database_id_out = strappy_db_column_string(stmt, 0);
    sqlite3_finalize(stmt);
    if ((*assistant_database_id_out != NULL) &&
        ((*assistant_database_id_out)[0] == '\0')) {
      free(*assistant_database_id_out);
      *assistant_database_id_out = NULL;
    }
    return 1;
  }

  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    return 1;
  }

  strappy_set_formatted_error(error_out,
                              "Could not read database access id lookup: %s",
                              sqlite3_errmsg(db));
  return 0;
}

static int strappy_db_store_database_access_assistant_id(
  sqlite3 *db,
  const char *path,
  const char *assistant_database_id,
  char **error_out)
{
  static const char *sql =
    "UPDATE database_access_settings "
    "SET assistant_database_id = ? "
    "WHERE path = ?;";
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database access id update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, assistant_database_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, path, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind database access id update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save database access id: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  if (sqlite3_changes(db) < 1) {
    strappy_set_error(error_out, "Database access setting was not found.");
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_store_discovered_database_assistant_id(
  sqlite3 *db,
  long long catalog_id,
  const char *assistant_database_id,
  char **error_out)
{
  static const char *sql =
    "UPDATE discovered_databases "
    "SET assistant_database_id = ? "
    "WHERE id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database id update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, assistant_database_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)catalog_id);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind discovered database id update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save discovered database id: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_set_assistant_database_id(sqlite3 *db,
                                                long long catalog_id,
                                                const char *path,
                                                char **error_out)
{
  char *assistant_database_id;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(error_out, "Discovered database path is empty.");
    return 0;
  }

  if (!strappy_db_ensure_database_access_setting(db, path, 0, error_out)) {
    return 0;
  }

  assistant_database_id = NULL;
  if (!strappy_db_copy_database_access_assistant_id(db,
                                                    path,
                                                    &assistant_database_id,
                                                    error_out)) {
    return 0;
  }

  if (assistant_database_id == NULL) {
    assistant_database_id = strappy_db_create_assistant_database_id(catalog_id);
    if (assistant_database_id == NULL) {
      strappy_set_error(error_out, "Could not allocate discovered database id.");
      return 0;
    }
    if (!strappy_db_store_database_access_assistant_id(db,
                                                       path,
                                                       assistant_database_id,
                                                       error_out)) {
      free(assistant_database_id);
      return 0;
    }
  }

  if (!strappy_db_store_discovered_database_assistant_id(db,
                                                         catalog_id,
                                                         assistant_database_id,
                                                         error_out)) {
    free(assistant_database_id);
    return 0;
  }

  free(assistant_database_id);
  return 1;
}

static int strappy_db_existing_discovered_database_id(sqlite3 *db,
                                                      const char *path,
                                                      long long *catalog_id_out,
                                                      char **error_out)
{
  static const char *sql =
    "SELECT id FROM discovered_databases WHERE path = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if (catalog_id_out == NULL) {
    strappy_set_error(error_out, "Discovered database lookup has no output.");
    return 0;
  }
  *catalog_id_out = 0;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind discovered database lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *catalog_id_out = (long long)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return 1;
  }

  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    return 1;
  }

  strappy_set_formatted_error(error_out,
                              "Could not read discovered database lookup: %s",
                              sqlite3_errmsg(db));
  return 0;
}

static int strappy_db_insert_discovered_database(
  sqlite3 *db,
  const strappy_discovered_database_input *record,
  long long *catalog_id_out,
  char **error_out)
{
  static const char *sql =
    "INSERT INTO discovered_databases "
    "(path, size, modified_at, device, inode, is_valid_sqlite, "
    "validation_error, scan_status, user_decision, scan_root, "
    "app_group_key, app_name, app_bundle_id, app_container_path, "
    "app_bundle_path, app_source, origin_kind, location_tail) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'unknown', ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt *stmt;
  const char *scan_status;
  int rc;
  int ok;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  scan_status = strappy_db_scan_status_for_input(record);
  ok = 1;
  if (sqlite3_bind_text(stmt, 1, record->path, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)record->size) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)record->modified_at) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)record->device) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)record->inode) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt, 6, (record->is_valid_sqlite ? 1 : 0)) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           7,
                                           record->validation_error,
                                           "Could not bind discovered database insert",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 8, scan_status, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           9,
                                           record->scan_root,
                                           "Could not bind discovered database insert",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           10,
                                           record->app_group_key,
                                           "Could not bind discovered database insert",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           11,
                                           record->app_name,
                                           "Could not bind discovered database insert",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           12,
                                           record->app_bundle_id,
                                           "Could not bind discovered database insert",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           13,
                                           record->app_container_path,
                                           "Could not bind discovered database insert",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           14,
                                           record->app_bundle_path,
                                           "Could not bind discovered database insert",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           15,
                                           record->app_source,
                                           "Could not bind discovered database insert",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt,
                         16,
                         strappy_db_origin_kind_for_input(record),
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           17,
                                           record->location_tail,
                                           "Could not bind discovered database insert",
                                           error_out)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind discovered database insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save discovered database: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  if (catalog_id_out != NULL) {
    *catalog_id_out = (long long)sqlite3_last_insert_rowid(db);
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_update_discovered_database(
  sqlite3 *db,
  long long catalog_id,
  const strappy_discovered_database_input *record,
  char **error_out)
{
  static const char *sql =
    "UPDATE discovered_databases "
    "SET size = ?, modified_at = ?, device = ?, inode = ?, "
    "is_valid_sqlite = ?, validation_error = ?, scan_status = ?, "
    "scan_root = ?, app_group_key = ?, app_name = ?, app_bundle_id = ?, "
    "app_container_path = ?, app_bundle_path = ?, app_source = ?, "
    "origin_kind = ?, location_tail = ?, "
    "last_seen_at = (strftime('%Y-%m-%dT%H:%M:%fZ','now')), "
    "last_scanned_at = (strftime('%Y-%m-%dT%H:%M:%fZ','now')) "
    "WHERE id = ?;";
  sqlite3_stmt *stmt;
  const char *scan_status;
  int rc;
  int ok;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  scan_status = strappy_db_scan_status_for_input(record);
  ok = 1;
  if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)record->size) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)record->modified_at) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)record->device) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)record->inode) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt, 5, (record->is_valid_sqlite ? 1 : 0)) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           6,
                                           record->validation_error,
                                           "Could not bind discovered database update",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 7, scan_status, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           8,
                                           record->scan_root,
                                           "Could not bind discovered database update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           9,
                                           record->app_group_key,
                                           "Could not bind discovered database update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           10,
                                           record->app_name,
                                           "Could not bind discovered database update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           11,
                                           record->app_bundle_id,
                                           "Could not bind discovered database update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           12,
                                           record->app_container_path,
                                           "Could not bind discovered database update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           13,
                                           record->app_bundle_path,
                                           "Could not bind discovered database update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           14,
                                           record->app_source,
                                           "Could not bind discovered database update",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt,
                         15,
                         strappy_db_origin_kind_for_input(record),
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           16,
                                           record->location_tail,
                                           "Could not bind discovered database update",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 17, (sqlite3_int64)catalog_id) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind discovered database update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update discovered database: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_save_discovered_database(
  sqlite3 *db,
  const strappy_discovered_database_input *record,
  char **error_out)
{
  long long catalog_id;

  if ((record == NULL) || (record->path == NULL) || (record->path[0] == '\0')) {
    strappy_set_error(error_out, "Discovered database path is empty.");
    return 0;
  }

  if (!strappy_db_existing_discovered_database_id(db,
                                                  record->path,
                                                  &catalog_id,
                                                  error_out)) {
    return 0;
  }

  if (catalog_id > 0) {
    if (!strappy_db_update_discovered_database(db,
                                               catalog_id,
                                               record,
                                               error_out)) {
      return 0;
    }
  } else {
    if (!strappy_db_insert_discovered_database(db,
                                               record,
                                               &catalog_id,
                                               error_out)) {
      return 0;
    }
  }

  if (!strappy_db_ensure_database_access_setting(db,
                                                 record->path,
                                                 record->hidden,
                                                 error_out)) {
    return 0;
  }

  return strappy_db_set_assistant_database_id(db,
                                              catalog_id,
                                              record->path,
                                              error_out);
}

static int strappy_db_assign_discovered_database_from_statement(
  strappy_discovered_database_record *record,
  sqlite3_stmt *stmt,
  char **error_out)
{
  char *assistant_database_id;
  char *path;
  char *validation_error;
  char *scan_status;
  char *user_decision;
  char *scan_root;
  char *app_group_key;
  char *app_name;
  char *app_bundle_id;
  char *app_container_path;
  char *app_bundle_path;
  char *app_source;
  char *origin_kind;
  char *location_tail;
  char *first_seen_at;
  char *last_seen_at;
  char *last_scanned_at;
  long long catalog_id;
  int hidden;

  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Discovered database row request is incomplete.");
    return 0;
  }

  strappy_discovered_database_record_destroy(record);
  catalog_id = (long long)sqlite3_column_int64(stmt, 0);
  record->catalog_id = catalog_id;
  record->size = (long long)sqlite3_column_int64(stmt, 3);
  record->modified_at = (long long)sqlite3_column_int64(stmt, 4);
  record->device = (unsigned long long)sqlite3_column_int64(stmt, 5);
  record->inode = (unsigned long long)sqlite3_column_int64(stmt, 6);
  record->is_valid_sqlite = sqlite3_column_int(stmt, 7) ? 1 : 0;

  assistant_database_id = strappy_db_column_string(stmt, 1);
  if (assistant_database_id == NULL) {
    assistant_database_id = strappy_db_create_assistant_database_id(catalog_id);
  }
  path = strappy_db_column_string(stmt, 2);
  validation_error = strappy_db_column_string(stmt, 8);
  scan_status = strappy_db_column_string(stmt, 9);
  user_decision = strappy_db_column_string(stmt, 10);
  hidden = sqlite3_column_int(stmt, 11) ? 1 : 0;
  scan_root = strappy_db_column_string(stmt, 12);
  app_group_key = strappy_db_column_string(stmt, 13);
  app_name = strappy_db_column_string(stmt, 14);
  app_bundle_id = strappy_db_column_string(stmt, 15);
  app_container_path = strappy_db_column_string(stmt, 16);
  app_bundle_path = strappy_db_column_string(stmt, 17);
  app_source = strappy_db_column_string(stmt, 18);
  origin_kind = strappy_db_column_string(stmt, 19);
  location_tail = strappy_db_column_string(stmt, 20);
  first_seen_at = strappy_db_column_string(stmt, 21);
  last_seen_at = strappy_db_column_string(stmt, 22);
  last_scanned_at = strappy_db_column_string(stmt, 23);

  if ((assistant_database_id == NULL) || (path == NULL) ||
      (scan_status == NULL) || (user_decision == NULL) ||
      (origin_kind == NULL) || (first_seen_at == NULL) || (last_seen_at == NULL) ||
      (last_scanned_at == NULL)) {
    free(assistant_database_id);
    free(path);
    free(validation_error);
    free(scan_status);
    free(user_decision);
    free(scan_root);
    free(app_group_key);
    free(app_name);
    free(app_bundle_id);
    free(app_container_path);
    free(app_bundle_path);
    free(app_source);
    free(origin_kind);
    free(location_tail);
    free(first_seen_at);
    free(last_seen_at);
    free(last_scanned_at);
    strappy_set_error(error_out, "Could not allocate discovered database row.");
    return 0;
  }

  record->assistant_database_id = assistant_database_id;
  record->path = path;
  record->validation_error = validation_error;
  record->scan_status = scan_status;
  record->user_decision = user_decision;
  record->scan_root = scan_root;
  record->app_group_key = app_group_key;
  record->app_name = app_name;
  record->app_bundle_id = app_bundle_id;
  record->app_container_path = app_container_path;
  record->app_bundle_path = app_bundle_path;
  record->app_source = app_source;
  record->origin_kind = origin_kind;
  record->location_tail = location_tail;
  record->hidden = hidden;
  record->first_seen_at = first_seen_at;
  record->last_seen_at = last_seen_at;
  record->last_scanned_at = last_scanned_at;
  return 1;
}

static char *strappy_db_create_message_json(const char *role,
                                            const char *content)
{
  cJSON *root;
  char *json;

  if ((role == NULL) || (role[0] == '\0') || (content == NULL)) {
    return NULL;
  }

  root = cJSON_CreateObject();
  if (root == NULL) {
    return NULL;
  }

  if ((cJSON_AddStringToObject(root, "role", role) == NULL) ||
      (cJSON_AddStringToObject(root, "content", content) == NULL)) {
    cJSON_Delete(root);
    return NULL;
  }

  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return json;
}

static const char *strappy_db_input_render_role(
  const strappy_session_message_input *message)
{
  if ((message != NULL) &&
      (message->render_role != NULL) &&
      (message->render_role[0] != '\0')) {
    return message->render_role;
  }
  if ((message != NULL) &&
      (message->role != NULL) &&
      (message->role[0] != '\0')) {
    return message->role;
  }
  return "assistant";
}

static const char *strappy_db_input_kind(
  const strappy_session_message_input *message)
{
  if ((message != NULL) &&
      (message->kind != NULL) &&
      (message->kind[0] != '\0')) {
    return message->kind;
  }
  return "message";
}

static const char *strappy_db_input_actor(
  const strappy_session_message_input *message)
{
  if ((message != NULL) &&
      (message->actor != NULL) &&
      (message->actor[0] != '\0')) {
    return message->actor;
  }
  return strappy_db_input_render_role(message);
}

static const char *strappy_db_input_context_policy(
  const strappy_session_message_input *message)
{
  if ((message != NULL) &&
      (message->context_policy != NULL) &&
      (message->context_policy[0] != '\0')) {
    return message->context_policy;
  }
  return "full";
}

static const char *strappy_db_input_api_role(
  const strappy_session_message_input *message)
{
  if ((message != NULL) &&
      (message->api_role != NULL) &&
      (message->api_role[0] != '\0')) {
    return message->api_role;
  }
  return NULL;
}

static int strappy_db_insert_or_load_turn(
  sqlite3 *db,
  long long session_id,
  const strappy_session_message_input *message,
  long long *turn_id_out,
  char **error_out)
{
  static const char *insert_sql =
    "INSERT OR IGNORE INTO session_turns "
    "(session_id, turn_key, prompt_group_key, actor, api_role, render_role, "
    "context_policy, prompt, status) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'complete');";
  static const char *select_sql =
    "SELECT id FROM session_turns WHERE session_id = ? AND turn_key = ?;";
  sqlite3_stmt *stmt;
  const char *turn_key;
  const char *api_role;
  const char *prompt;
  int rc;
  int ok;

  if (turn_id_out == NULL) {
    strappy_set_error(error_out, "Session turn insert received no output.");
    return 0;
  }
  *turn_id_out = 0;

  if ((message == NULL) ||
      (message->turn_key == NULL) ||
      (message->turn_key[0] == '\0')) {
    return 1;
  }

  turn_key = message->turn_key;
  api_role = strappy_db_input_api_role(message);
  if (api_role == NULL) {
    api_role = "user";
  }
  prompt = "";
  if (strcmp(strappy_db_input_kind(message), "prompt") == 0) {
    prompt = (message->content != NULL) ? message->content : "";
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session turn insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 2, turn_key, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           3,
                                           message->prompt_group_key,
                                           "Could not bind session turn insert",
                                           error_out)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt,
                               4,
                               strappy_db_input_actor(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 5, api_role, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt,
                               6,
                               strappy_db_input_render_role(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt,
                               7,
                               strappy_db_input_context_policy(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 8, prompt, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session turn insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session turn: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session turn lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, turn_key, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session turn lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *turn_id_out = (long long)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return 1;
  }

  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session turn was not found after insert.");
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read session turn lookup: %s",
                                sqlite3_errmsg(db));
  }
  return 0;
}

static int strappy_db_insert_message(sqlite3 *db,
                                     long long session_id,
                                     const strappy_session_message_input *message,
                                     char **error_out)
{
  static const char *sql =
    "INSERT INTO session_messages "
    "(session_id, turn_id, turn_key, prompt_group_key, actor, kind, api_role, "
    "render_role, role, content, model, http_status, metadata_json, "
    "render_state_json, message_json, reasoning, message_key, "
    "target_message_key, tool_call_id, tool_name, arguments_json, result_json, "
    "include_in_context, is_error) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt *stmt;
  char *generated_message_json;
  const char *message_json_to_store;
  const char *api_role;
  const char *render_role;
  const char *content;
  long long turn_id;
  int rc;
  int ok;

  if ((session_id <= 0) || (message == NULL)) {
    strappy_set_error(error_out, "Session message is incomplete.");
    return 0;
  }

  render_role = strappy_db_input_render_role(message);
  api_role = strappy_db_input_api_role(message);
  content = (message->content != NULL) ? message->content : "";
  if ((render_role == NULL) || (render_role[0] == '\0')) {
    strappy_set_error(error_out, "Session message render role is incomplete.");
    return 0;
  }

  if (!strappy_db_insert_or_load_turn(db,
                                      session_id,
                                      message,
                                      &turn_id,
                                      error_out)) {
    return 0;
  }

  generated_message_json = NULL;
  message_json_to_store = message->message_json;
  if (((message_json_to_store == NULL) || (message_json_to_store[0] == '\0')) &&
      (api_role != NULL) &&
      (api_role[0] != '\0')) {
    generated_message_json = strappy_db_create_message_json(api_role, content);
    message_json_to_store = generated_message_json;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message insert: %s",
                                sqlite3_errmsg(db));
    free(generated_message_json);
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) {
    ok = 0;
  }
  if (ok && (turn_id > 0) &&
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)turn_id) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (turn_id <= 0) && (sqlite3_bind_null(stmt, 2) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           3,
                                           message->turn_key,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           4,
                                           message->prompt_group_key,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           5,
                                           message->actor,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt,
                               6,
                               strappy_db_input_kind(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (api_role != NULL) &&
      (sqlite3_bind_text(stmt, 7, api_role, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (api_role == NULL) && (sqlite3_bind_null(stmt, 7) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 8, render_role, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 9, render_role, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 10, content, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           11,
                                           message->model,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 12, (sqlite3_int64)message->http_status) !=
       SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           13,
                                           message->metadata_json,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           14,
                                           message->render_state_json,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && (message_json_to_store != NULL) &&
      (sqlite3_bind_text(stmt,
                         15,
                         message_json_to_store,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (message_json_to_store == NULL) &&
      (sqlite3_bind_null(stmt, 15) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           16,
                                           message->reasoning,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           17,
                                           message->message_key,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           18,
                                           message->target_message_key,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           19,
                                           message->tool_call_id,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           20,
                                           message->tool_name,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           21,
                                           message->arguments_json,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           22,
                                           message->result_json,
                                           "Could not bind session message",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt,
                        23,
                        message->include_in_context ? 1 : 0) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt, 24, message->is_error ? 1 : 0) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(generated_message_json);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session message: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(generated_message_json);
    return 0;
  }

  sqlite3_finalize(stmt);
  free(generated_message_json);
  return 1;
}

static int strappy_db_find_message_id_by_key(sqlite3 *db,
                                             long long session_id,
                                             const char *message_key,
                                             long long *message_id_out,
                                             char **error_out)
{
  static const char *sql =
    "SELECT id FROM session_messages "
    "WHERE session_id = ? AND message_key = ? "
    "ORDER BY id ASC LIMIT 1;";
  sqlite3_stmt *stmt;
  int rc;

  if (message_id_out == NULL) {
    strappy_set_error(error_out, "Session message lookup received no output.");
    return 0;
  }
  *message_id_out = 0LL;

  if ((message_key == NULL) || (message_key[0] == '\0')) {
    return 1;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, message_key, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *message_id_out = (long long)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return 1;
  }

  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    return 1;
  }

  strappy_set_formatted_error(error_out,
                              "Could not read session message lookup: %s",
                              sqlite3_errmsg(db));
  return 0;
}

static int strappy_db_update_message(sqlite3 *db,
                                     long long session_id,
                                     long long message_id,
                                     const strappy_session_message_input *message,
                                     char **error_out)
{
  static const char *sql =
    "UPDATE session_messages "
    "SET turn_id = ?, turn_key = ?, prompt_group_key = ?, actor = ?, "
    "kind = ?, api_role = ?, render_role = ?, role = ?, content = ?, "
    "model = ?, http_status = ?, metadata_json = ?, message_json = ?, "
    "reasoning = COALESCE(NULLIF(?, ''), reasoning), "
    "target_message_key = ?, tool_call_id = ?, "
    "tool_name = ?, arguments_json = ?, result_json = ?, render_state_json = ?, "
    "include_in_context = ?, is_error = ? "
    "WHERE id = ? AND session_id = ?;";
  sqlite3_stmt *stmt;
  char *generated_message_json;
  const char *message_json_to_store;
  const char *api_role;
  const char *render_role;
  const char *content;
  long long turn_id;
  int rc;
  int ok;

  if ((session_id <= 0) || (message_id <= 0) || (message == NULL)) {
    strappy_set_error(error_out, "Session message update is incomplete.");
    return 0;
  }

  render_role = strappy_db_input_render_role(message);
  api_role = strappy_db_input_api_role(message);
  content = (message->content != NULL) ? message->content : "";
  if ((render_role == NULL) || (render_role[0] == '\0')) {
    strappy_set_error(error_out, "Session message render role is incomplete.");
    return 0;
  }

  if (!strappy_db_insert_or_load_turn(db,
                                      session_id,
                                      message,
                                      &turn_id,
                                      error_out)) {
    return 0;
  }

  generated_message_json = NULL;
  message_json_to_store = message->message_json;
  if (((message_json_to_store == NULL) || (message_json_to_store[0] == '\0')) &&
      (api_role != NULL) &&
      (api_role[0] != '\0')) {
    generated_message_json = strappy_db_create_message_json(api_role, content);
    message_json_to_store = generated_message_json;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message update: %s",
                                sqlite3_errmsg(db));
    free(generated_message_json);
    return 0;
  }

  ok = 1;
  if ((turn_id > 0) &&
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)turn_id) != SQLITE_OK)) {
    ok = 0;
  }
  if ((turn_id <= 0) && (sqlite3_bind_null(stmt, 1) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           2,
                                           message->turn_key,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           3,
                                           message->prompt_group_key,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           4,
                                           message->actor,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt,
                               5,
                               strappy_db_input_kind(message),
                               -1,
                               SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (api_role != NULL) &&
      (sqlite3_bind_text(stmt, 6, api_role, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (api_role == NULL) && (sqlite3_bind_null(stmt, 6) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 7, render_role, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 8, render_role, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_text(stmt, 9, content, -1, SQLITE_TRANSIENT) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           10,
                                           message->model,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 11, (sqlite3_int64)message->http_status) !=
       SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           12,
                                           message->metadata_json,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && (message_json_to_store != NULL) &&
      (sqlite3_bind_text(stmt,
                         13,
                         message_json_to_store,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (message_json_to_store == NULL) &&
      (sqlite3_bind_null(stmt, 13) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           14,
                                           message->reasoning,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           15,
                                           message->target_message_key,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           16,
                                           message->tool_call_id,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           17,
                                           message->tool_name,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           18,
                                           message->arguments_json,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           19,
                                           message->result_json,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_optional_text(db,
                                           stmt,
                                           20,
                                           message->render_state_json,
                                           "Could not bind session message update",
                                           error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt,
                        21,
                        message->include_in_context ? 1 : 0) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt, 22, message->is_error ? 1 : 0) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 23, (sqlite3_int64)message_id) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 24, (sqlite3_int64)session_id) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(generated_message_json);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session message: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(generated_message_json);
    return 0;
  }

  sqlite3_finalize(stmt);
  free(generated_message_json);
  return 1;
}

static int strappy_db_upsert_message(sqlite3 *db,
                                     long long session_id,
                                     const strappy_session_message_input *message,
                                     char **error_out)
{
  long long message_id;

  if ((message != NULL) &&
      (message->message_key != NULL) &&
      (message->message_key[0] != '\0')) {
    if (!strappy_db_find_message_id_by_key(db,
                                           session_id,
                                           message->message_key,
                                           &message_id,
                                           error_out)) {
      return 0;
    }
    if (message_id > 0LL) {
      return strappy_db_update_message(db,
                                       session_id,
                                       message_id,
                                       message,
                                       error_out);
    }
  }

  return strappy_db_insert_message(db, session_id, message, error_out);
}

static int strappy_db_append_message_content(sqlite3 *db,
                                             long long session_id,
                                             const strappy_session_message_input *message,
                                             const char *content_delta,
                                             const char *reasoning_delta,
                                             char **error_out)
{
  static const char *sql =
    "UPDATE session_messages "
    "SET content = content || ?, "
    "reasoning = CASE "
    "WHEN ? = '' THEN reasoning "
    "WHEN reasoning IS NULL THEN ? "
    "ELSE reasoning || ? END "
    ", render_state_json = CASE "
    "WHEN ? IS NULL THEN render_state_json "
    "WHEN render_state_json LIKE '%\"content_started\":true%' "
    "AND ? LIKE '%\"reasoning_collapsed\":false%' THEN render_state_json "
    "ELSE ? END "
    "WHERE id = ? AND session_id = ?;";
  sqlite3_stmt *stmt;
  long long message_id;
  int rc;

  if ((session_id <= 0) || (message == NULL) ||
      (message->message_key == NULL) ||
      (message->message_key[0] == '\0')) {
    strappy_set_error(error_out, "Streamed session message is incomplete.");
    return 0;
  }

  if (!strappy_db_find_message_id_by_key(db,
                                         session_id,
                                         message->message_key,
                                         &message_id,
                                         error_out)) {
    return 0;
  }
  if (message_id <= 0LL) {
    if (!strappy_db_insert_message(db, session_id, message, error_out)) {
      return 0;
    }
    if (!strappy_db_find_message_id_by_key(db,
                                           session_id,
                                           message->message_key,
                                           &message_id,
                                           error_out)) {
      return 0;
    }
  }
  if (message_id <= 0LL) {
    strappy_set_error(error_out, "Streamed session message was not found.");
    return 0;
  }

  if (content_delta == NULL) {
    content_delta = "";
  }
  if (reasoning_delta == NULL) {
    reasoning_delta = "";
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare streamed message update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, content_delta, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, reasoning_delta, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 3, reasoning_delta, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 4, reasoning_delta, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    if (message->render_state_json != NULL) {
      rc = sqlite3_bind_text(stmt,
                             5,
                             message->render_state_json,
                             -1,
                             SQLITE_TRANSIENT);
    } else {
      rc = sqlite3_bind_null(stmt, 5);
    }
  }
  if (rc == SQLITE_OK) {
    if (message->render_state_json != NULL) {
      rc = sqlite3_bind_text(stmt,
                             6,
                             message->render_state_json,
                             -1,
                             SQLITE_TRANSIENT);
    } else {
      rc = sqlite3_bind_null(stmt, 6);
    }
  }
  if (rc == SQLITE_OK) {
    if (message->render_state_json != NULL) {
      rc = sqlite3_bind_text(stmt,
                             7,
                             message->render_state_json,
                             -1,
                             SQLITE_TRANSIENT);
    } else {
      rc = sqlite3_bind_null(stmt, 7);
    }
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 8, (sqlite3_int64)message_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 9, (sqlite3_int64)session_id);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind streamed message update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update streamed message: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_move_message_content_to_reasoning(
  sqlite3 *db,
  long long session_id,
  const strappy_session_message_input *message,
  char **error_out)
{
  static const char *sql =
    "UPDATE session_messages "
    "SET reasoning = CASE "
    "WHEN content = '' THEN reasoning "
    "WHEN reasoning IS NULL OR reasoning = '' THEN content "
    "WHEN substr(content, 1, 1) = ? THEN reasoning || content "
    "ELSE reasoning || ? || content END, "
    "content = '', "
    "render_state_json = CASE WHEN ? IS NULL THEN render_state_json ELSE ? END "
    "WHERE id = ? AND session_id = ?;";
  sqlite3_stmt *stmt;
  long long message_id;
  int rc;

  if ((session_id <= 0) || (message == NULL) ||
      (message->message_key == NULL) ||
      (message->message_key[0] == '\0')) {
    strappy_set_error(error_out, "Streamed session message is incomplete.");
    return 0;
  }

  if (!strappy_db_find_message_id_by_key(db,
                                         session_id,
                                         message->message_key,
                                         &message_id,
                                         error_out)) {
    return 0;
  }
  if (message_id <= 0LL) {
    return 1;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare streamed message move: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, "\n", -1, SQLITE_STATIC);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, "\n", -1, SQLITE_STATIC);
  }
  if (rc == SQLITE_OK) {
    if (message->render_state_json != NULL) {
      rc = sqlite3_bind_text(stmt,
                             3,
                             message->render_state_json,
                             -1,
                             SQLITE_TRANSIENT);
    } else {
      rc = sqlite3_bind_null(stmt, 3);
    }
  }
  if (rc == SQLITE_OK) {
    if (message->render_state_json != NULL) {
      rc = sqlite3_bind_text(stmt,
                             4,
                             message->render_state_json,
                             -1,
                             SQLITE_TRANSIENT);
    } else {
      rc = sqlite3_bind_null(stmt, 4);
    }
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 5, (sqlite3_int64)message_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 6, (sqlite3_int64)session_id);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind streamed message move: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not move streamed message content: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_update_message_render_state(
  sqlite3 *db,
  long long session_id,
  const char *message_key,
  const char *render_state_json,
  char **error_out)
{
  static const char *sql =
    "UPDATE session_messages "
    "SET render_state_json = ? "
    "WHERE id = ? AND session_id = ?;";
  sqlite3_stmt *stmt;
  long long message_id;
  int rc;

  if ((session_id <= 0) || (message_key == NULL) ||
      (message_key[0] == '\0')) {
    strappy_set_error(error_out, "Session message render state update is incomplete.");
    return 0;
  }

  if (!strappy_db_find_message_id_by_key(db,
                                         session_id,
                                         message_key,
                                         &message_id,
                                         error_out)) {
    return 0;
  }
  if (message_id <= 0LL) {
    return 1;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare message render state update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  if (render_state_json != NULL) {
    rc = sqlite3_bind_text(stmt, 1, render_state_json, -1, SQLITE_TRANSIENT);
  } else {
    rc = sqlite3_bind_null(stmt, 1);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)message_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 3, (sqlite3_int64)session_id);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind message render state update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update message render state: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_session_exists(sqlite3 *db,
                                     long long session_id,
                                     char **error_out)
{
  static const char *sql = "SELECT 1 FROM sessions WHERE id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc == SQLITE_ROW) {
    return 1;
  }

  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read session lookup: %s",
                                sqlite3_errmsg(db));
  }
  return 0;
}

static char *strappy_db_json_copy_value_text(cJSON *value)
{
  if ((value == NULL) || cJSON_IsNull(value)) {
    return NULL;
  }

  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return strappy_string_duplicate(value->valuestring);
  }

  return cJSON_PrintUnformatted(value);
}

static char *strappy_db_json_copy_object_text(cJSON *object, const char *key)
{
  if ((object == NULL) || (key == NULL)) {
    return NULL;
  }

  return strappy_db_json_copy_value_text(cJSON_GetObjectItem(object, key));
}

static cJSON *strappy_db_json_object_child(cJSON *object, const char *key)
{
  cJSON *child;

  if ((object == NULL) || (key == NULL)) {
    return NULL;
  }

  child = cJSON_GetObjectItem(object, key);
  return cJSON_IsObject(child) ? child : NULL;
}

static char *strappy_db_json_copy_nested_text(cJSON *object,
                                              const char *parent_key,
                                              const char *child_key)
{
  return strappy_db_json_copy_object_text(
    strappy_db_json_object_child(object, parent_key),
    child_key);
}

static long long strappy_db_json_integer(cJSON *value)
{
  if (cJSON_IsNumber(value)) {
    return (long long)value->valuedouble;
  }

  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return (long long)strtoll(value->valuestring, NULL, 10);
  }

  return 0;
}

static long long strappy_db_json_object_integer(cJSON *object, const char *key)
{
  if ((object == NULL) || (key == NULL)) {
    return 0;
  }

  return strappy_db_json_integer(cJSON_GetObjectItem(object, key));
}

static long long strappy_db_json_nested_integer(cJSON *object,
                                                const char *parent_key,
                                                const char *child_key)
{
  return strappy_db_json_object_integer(
    strappy_db_json_object_child(object, parent_key),
    child_key);
}

static int strappy_db_json_bool(cJSON *value)
{
  if (cJSON_IsTrue(value)) {
    return 1;
  }
  if (cJSON_IsFalse(value) || (value == NULL) || cJSON_IsNull(value)) {
    return 0;
  }
  if (cJSON_IsNumber(value) && (value->valuedouble != 0.0)) {
    return 1;
  }
  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return ((strcmp(value->valuestring, "true") == 0) ||
            (strcmp(value->valuestring, "1") == 0)) ? 1 : 0;
  }
  return 0;
}

static int strappy_db_json_nested_bool(cJSON *object,
                                       const char *parent_key,
                                       const char *child_key)
{
  cJSON *parent;

  parent = strappy_db_json_object_child(object, parent_key);
  return strappy_db_json_bool(cJSON_GetObjectItem(parent, child_key));
}

static int strappy_db_bind_text_or_null(sqlite3 *db,
                                        sqlite3_stmt *stmt,
                                        int index,
                                        const char *value,
                                        const char *error_prefix,
                                        char **error_out)
{
  return strappy_db_bind_optional_text(db,
                                       stmt,
                                       index,
                                       value,
                                       error_prefix,
                                       error_out);
}

static int strappy_db_insert_openrouter_model_string(
  sqlite3 *db,
  const char *sql,
  const char *model_id,
  const char *value,
  const char *error_prefix,
  char **error_out)
{
  sqlite3_stmt *stmt;
  int rc;

  if ((value == NULL) || (value[0] == '\0')) {
    return 1;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "%s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "%s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "%s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_insert_openrouter_model_value_array(
  sqlite3 *db,
  const char *sql,
  const char *model_id,
  cJSON *value,
  const char *error_prefix,
  char **error_out)
{
  int count;
  int index;

  if ((value == NULL) || cJSON_IsNull(value)) {
    return 1;
  }

  if (!cJSON_IsArray(value)) {
    char *text;
    int ok;

    text = strappy_db_json_copy_value_text(value);
    ok = strappy_db_insert_openrouter_model_string(db,
                                                   sql,
                                                   model_id,
                                                   text,
                                                   error_prefix,
                                                   error_out);
    free(text);
    return ok;
  }

  count = cJSON_GetArraySize(value);
  for (index = 0; index < count; index++) {
    cJSON *item;
    char *text;
    int ok;

    item = cJSON_GetArrayItem(value, index);
    text = strappy_db_json_copy_value_text(item);
    ok = strappy_db_insert_openrouter_model_string(db,
                                                   sql,
                                                   model_id,
                                                   text,
                                                   error_prefix,
                                                   error_out);
    free(text);
    if (!ok) {
      return 0;
    }
  }

  return 1;
}

static int strappy_db_insert_openrouter_model_arrays(sqlite3 *db,
                                                     const char *model_id,
                                                     cJSON *model,
                                                     char **error_out)
{
  static const char *input_sql =
    "INSERT OR IGNORE INTO openrouter_model_input_modalities "
    "(model_id, modality) VALUES (?, ?);";
  static const char *output_sql =
    "INSERT OR IGNORE INTO openrouter_model_output_modalities "
    "(model_id, modality) VALUES (?, ?);";
  static const char *parameters_sql =
    "INSERT OR IGNORE INTO openrouter_model_supported_parameters "
    "(model_id, parameter) VALUES (?, ?);";
  static const char *voices_sql =
    "INSERT OR IGNORE INTO openrouter_model_supported_voices "
    "(model_id, voice) VALUES (?, ?);";
  cJSON *architecture;

  architecture = strappy_db_json_object_child(model, "architecture");
  if (!strappy_db_insert_openrouter_model_value_array(
        db,
        input_sql,
        model_id,
        cJSON_GetObjectItem(architecture, "input_modalities"),
        "Could not save OpenRouter input modality",
        error_out)) {
    return 0;
  }
  if (!strappy_db_insert_openrouter_model_value_array(
        db,
        output_sql,
        model_id,
        cJSON_GetObjectItem(architecture, "output_modalities"),
        "Could not save OpenRouter output modality",
        error_out)) {
    return 0;
  }
  if (!strappy_db_insert_openrouter_model_value_array(
        db,
        parameters_sql,
        model_id,
        cJSON_GetObjectItem(model, "supported_parameters"),
        "Could not save OpenRouter supported parameter",
        error_out)) {
    return 0;
  }
  if (!strappy_db_insert_openrouter_model_value_array(
        db,
        voices_sql,
        model_id,
        cJSON_GetObjectItem(model, "supported_voices"),
        "Could not save OpenRouter supported voice",
        error_out)) {
    return 0;
  }

  return 1;
}

static int strappy_db_insert_openrouter_model(sqlite3 *db,
                                              cJSON *model,
                                              char **error_out)
{
  static const char *sql =
    "INSERT OR REPLACE INTO openrouter_models "
    "(id, canonical_slug, hugging_face_id, name, description, "
    "context_length, created, architecture_modality, "
    "architecture_tokenizer, architecture_instruct_type, pricing_prompt, "
    "pricing_completion, pricing_request, pricing_image, pricing_audio, "
    "pricing_web_search, pricing_internal_reasoning, "
    "pricing_input_cache_read, pricing_input_cache_write, "
    "top_provider_context_length, top_provider_max_completion_tokens, "
    "top_provider_is_moderated, knowledge_cutoff, expiration_date, "
    "links_details, links_json, reasoning_json, benchmarks_json, "
    "default_parameters_json, per_request_limits_json, raw_json, fetched_at) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
    "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%fZ','now'));";
  sqlite3_stmt *stmt;
  strappy_openrouter_model_record record;
  int rc;
  int ok;

  if (!cJSON_IsObject(model)) {
    strappy_set_error(error_out, "OpenRouter model entry is not an object.");
    return 0;
  }

  strappy_openrouter_model_record_init(&record);
  record.model_id = strappy_db_json_copy_object_text(model, "id");
  if ((record.model_id == NULL) || (record.model_id[0] == '\0')) {
    strappy_openrouter_model_record_destroy(&record);
    strappy_set_error(error_out, "OpenRouter model entry is missing id.");
    return 0;
  }

  record.canonical_slug =
    strappy_db_json_copy_object_text(model, "canonical_slug");
  record.hugging_face_id =
    strappy_db_json_copy_object_text(model, "hugging_face_id");
  record.name = strappy_db_json_copy_object_text(model, "name");
  record.description = strappy_db_json_copy_object_text(model, "description");
  record.context_length = strappy_db_json_object_integer(model, "context_length");
  record.created = strappy_db_json_object_integer(model, "created");
  record.architecture_modality =
    strappy_db_json_copy_nested_text(model, "architecture", "modality");
  record.architecture_tokenizer =
    strappy_db_json_copy_nested_text(model, "architecture", "tokenizer");
  record.architecture_instruct_type =
    strappy_db_json_copy_nested_text(model, "architecture", "instruct_type");
  record.pricing_prompt =
    strappy_db_json_copy_nested_text(model, "pricing", "prompt");
  record.pricing_completion =
    strappy_db_json_copy_nested_text(model, "pricing", "completion");
  record.pricing_request =
    strappy_db_json_copy_nested_text(model, "pricing", "request");
  record.pricing_image =
    strappy_db_json_copy_nested_text(model, "pricing", "image");
  record.pricing_audio =
    strappy_db_json_copy_nested_text(model, "pricing", "audio");
  record.pricing_web_search =
    strappy_db_json_copy_nested_text(model, "pricing", "web_search");
  record.pricing_internal_reasoning =
    strappy_db_json_copy_nested_text(model, "pricing", "internal_reasoning");
  record.pricing_input_cache_read =
    strappy_db_json_copy_nested_text(model, "pricing", "input_cache_read");
  record.pricing_input_cache_write =
    strappy_db_json_copy_nested_text(model, "pricing", "input_cache_write");
  record.top_provider_context_length =
    strappy_db_json_nested_integer(model, "top_provider", "context_length");
  record.top_provider_max_completion_tokens =
    strappy_db_json_nested_integer(model,
                                   "top_provider",
                                   "max_completion_tokens");
  record.top_provider_is_moderated =
    strappy_db_json_nested_bool(model, "top_provider", "is_moderated");
  record.knowledge_cutoff =
    strappy_db_json_copy_object_text(model, "knowledge_cutoff");
  record.expiration_date =
    strappy_db_json_copy_object_text(model, "expiration_date");
  record.links_details =
    strappy_db_json_copy_nested_text(model, "links", "details");
  record.links_json = strappy_db_json_copy_object_text(model, "links");
  record.reasoning_json = strappy_db_json_copy_object_text(model, "reasoning");
  record.benchmarks_json =
    strappy_db_json_copy_object_text(model, "benchmarks");
  record.default_parameters_json =
    strappy_db_json_copy_object_text(model, "default_parameters");
  record.per_request_limits_json =
    strappy_db_json_copy_object_text(model, "per_request_limits");
  record.raw_json = cJSON_PrintUnformatted(model);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare OpenRouter model insert: %s",
                                sqlite3_errmsg(db));
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, record.model_id, -1, SQLITE_TRANSIENT) !=
      SQLITE_OK) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 2, record.canonical_slug,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 3, record.hugging_face_id,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 4, record.name,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 5, record.description,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)record.context_length) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (sqlite3_bind_int64(stmt, 7, (sqlite3_int64)record.created) !=
             SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 8,
                                          record.architecture_modality,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 9,
                                          record.architecture_tokenizer,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 10,
                                          record.architecture_instruct_type,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 11, record.pricing_prompt,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 12, record.pricing_completion,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 13, record.pricing_request,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 14, record.pricing_image,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 15, record.pricing_audio,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 16, record.pricing_web_search,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(
              db,
              stmt,
              17,
              record.pricing_internal_reasoning,
              "Could not bind OpenRouter model insert",
              error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(
              db,
              stmt,
              18,
              record.pricing_input_cache_read,
              "Could not bind OpenRouter model insert",
              error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(
              db,
              stmt,
              19,
              record.pricing_input_cache_write,
              "Could not bind OpenRouter model insert",
              error_out)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt,
                          20,
                          (sqlite3_int64)record.top_provider_context_length) !=
       SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(
         stmt,
         21,
         (sqlite3_int64)record.top_provider_max_completion_tokens) !=
       SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int(stmt, 22, record.top_provider_is_moderated ? 1 : 0) !=
       SQLITE_OK)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          23,
                                          record.knowledge_cutoff,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          24,
                                          record.expiration_date,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          25,
                                          record.links_details,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          26,
                                          record.links_json,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          27,
                                          record.reasoning_json,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          28,
                                          record.benchmarks_json,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          29,
                                          record.default_parameters_json,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db,
                                          stmt,
                                          30,
                                          record.per_request_limits_json,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }
  if (ok && !strappy_db_bind_text_or_null(db, stmt, 31, record.raw_json,
                                          "Could not bind OpenRouter model insert",
                                          error_out)) {
    ok = 0;
  }

  if (!ok) {
    if ((error_out != NULL) && (*error_out == NULL)) {
      strappy_set_formatted_error(error_out,
                                  "Could not bind OpenRouter model insert: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save OpenRouter model: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  sqlite3_finalize(stmt);

  ok = strappy_db_insert_openrouter_model_arrays(db,
                                                 record.model_id,
                                                 model,
                                                 error_out);
  strappy_openrouter_model_record_destroy(&record);
  return ok;
}

static int strappy_db_assign_openrouter_model_from_statement(
  strappy_openrouter_model_record *record,
  sqlite3_stmt *stmt,
  char **error_out)
{
  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "OpenRouter model row request is incomplete.");
    return 0;
  }

  strappy_openrouter_model_record_destroy(record);
  record->context_length = (long long)sqlite3_column_int64(stmt, 5);
  record->created = (long long)sqlite3_column_int64(stmt, 6);
  record->top_provider_context_length = (long long)sqlite3_column_int64(stmt, 19);
  record->top_provider_max_completion_tokens =
    (long long)sqlite3_column_int64(stmt, 20);
  record->top_provider_is_moderated = sqlite3_column_int(stmt, 21) ? 1 : 0;
  record->selected = sqlite3_column_int(stmt, 32) ? 1 : 0;
  record->allowed = sqlite3_column_int(stmt, 33) ? 1 : 0;

  record->model_id = strappy_db_column_string(stmt, 0);
  record->canonical_slug = strappy_db_column_string(stmt, 1);
  record->hugging_face_id = strappy_db_column_string(stmt, 2);
  record->name = strappy_db_column_string(stmt, 3);
  record->description = strappy_db_column_string(stmt, 4);
  record->architecture_modality = strappy_db_column_string(stmt, 7);
  record->architecture_tokenizer = strappy_db_column_string(stmt, 8);
  record->architecture_instruct_type = strappy_db_column_string(stmt, 9);
  record->pricing_prompt = strappy_db_column_string(stmt, 10);
  record->pricing_completion = strappy_db_column_string(stmt, 11);
  record->pricing_request = strappy_db_column_string(stmt, 12);
  record->pricing_image = strappy_db_column_string(stmt, 13);
  record->pricing_audio = strappy_db_column_string(stmt, 14);
  record->pricing_web_search = strappy_db_column_string(stmt, 15);
  record->pricing_internal_reasoning = strappy_db_column_string(stmt, 16);
  record->pricing_input_cache_read = strappy_db_column_string(stmt, 17);
  record->pricing_input_cache_write = strappy_db_column_string(stmt, 18);
  record->knowledge_cutoff = strappy_db_column_string(stmt, 22);
  record->expiration_date = strappy_db_column_string(stmt, 23);
  record->links_details = strappy_db_column_string(stmt, 24);
  record->links_json = strappy_db_column_string(stmt, 25);
  record->reasoning_json = strappy_db_column_string(stmt, 26);
  record->benchmarks_json = strappy_db_column_string(stmt, 27);
  record->default_parameters_json = strappy_db_column_string(stmt, 28);
  record->per_request_limits_json = strappy_db_column_string(stmt, 29);
  record->raw_json = strappy_db_column_string(stmt, 30);
  record->fetched_at = strappy_db_column_string(stmt, 31);

  if ((record->model_id == NULL) || (record->fetched_at == NULL)) {
    strappy_openrouter_model_record_destroy(record);
    strappy_set_error(error_out, "Could not allocate OpenRouter model row.");
    return 0;
  }

  return 1;
}

int strappy_db_initialize(const char *db_path, char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  ok = strappy_db_ensure_schema(db, error_out);
  sqlite3_close(db);
  return ok;
}

int strappy_db_save_discovered_databases(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  char **error_out)
{
  sqlite3 *db;
  size_t index;

  if ((records == NULL) && (count > 0U)) {
    strappy_set_error(error_out, "Discovered database records are missing.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin discovered database save",
                       error_out)) {
    sqlite3_close(db);
    return 0;
  }

  for (index = 0U; index < count; index++) {
    if (!strappy_db_save_discovered_database(db,
                                             &records[index],
                                             error_out)) {
      strappy_db_exec(db,
                      "ROLLBACK;",
                      "Could not roll back discovered database save",
                      NULL);
      sqlite3_close(db);
      return 0;
    }
  }

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit discovered database save",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database save",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

static int strappy_db_prepare_scan_path_table(sqlite3 *db, char **error_out)
{
  if (!strappy_db_exec(db,
                       "CREATE TEMP TABLE IF NOT EXISTS strappy_scan_paths "
                       "(path TEXT PRIMARY KEY);",
                       "Could not create scan path table",
                       error_out)) {
    return 0;
  }

  return strappy_db_exec(db,
                         "DELETE FROM strappy_scan_paths;",
                         "Could not clear scan path table",
                         error_out);
}

static int strappy_db_add_scan_path(sqlite3 *db,
                                    const char *path,
                                    char **error_out)
{
  static const char *sql =
    "INSERT OR IGNORE INTO strappy_scan_paths (path) VALUES (?);";
  sqlite3_stmt *stmt;
  int rc;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(error_out, "Scan path is empty.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare scan path insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind scan path insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save scan path: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_delete_missing_scan_root_databases(sqlite3 *db,
                                                         const char *scan_root,
                                                         char **error_out)
{
  static const char *sql =
    "DELETE FROM discovered_databases "
    "WHERE ((? IS NULL AND scan_root IS NULL) OR scan_root = ?) "
    "AND NOT EXISTS ("
    "SELECT 1 FROM strappy_scan_paths p "
    "WHERE p.path = discovered_databases.path);";
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare stale database delete: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  if ((scan_root != NULL) && (scan_root[0] != '\0')) {
    rc = sqlite3_bind_text(stmt, 1, scan_root, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
      rc = sqlite3_bind_text(stmt, 2, scan_root, -1, SQLITE_TRANSIENT);
    }
  } else {
    rc = sqlite3_bind_null(stmt, 1);
    if (rc == SQLITE_OK) {
      rc = sqlite3_bind_null(stmt, 2);
    }
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind stale database delete: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not delete stale databases: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

int strappy_db_replace_discovered_databases_for_scan_root(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  const char *scan_root,
  char **error_out)
{
  sqlite3 *db;
  size_t index;

  if ((records == NULL) && (count > 0U)) {
    strappy_set_error(error_out, "Discovered database records are missing.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin discovered database replacement",
                       error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_prepare_scan_path_table(db, error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database replacement",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  for (index = 0U; index < count; index++) {
    if (!strappy_db_add_scan_path(db, records[index].path, error_out)) {
      strappy_db_exec(db,
                      "ROLLBACK;",
                      "Could not roll back discovered database replacement",
                      NULL);
      sqlite3_close(db);
      return 0;
    }
  }

  if (!strappy_db_delete_missing_scan_root_databases(db,
                                                     scan_root,
                                                     error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database replacement",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  for (index = 0U; index < count; index++) {
    if (!strappy_db_save_discovered_database(db,
                                             &records[index],
                                             error_out)) {
      strappy_db_exec(db,
                      "ROLLBACK;",
                      "Could not roll back discovered database replacement",
                      NULL);
      sqlite3_close(db);
      return 0;
    }
  }

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit discovered database replacement",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database replacement",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_list_discovered_databases(
  const char *db_path,
  strappy_discovered_database_record_list *list,
  char **error_out)
{
  static const char *sql =
    "SELECT d.id, COALESCE(a.assistant_database_id, d.assistant_database_id), "
    "d.path, d.size, d.modified_at, d.device, d.inode, d.is_valid_sqlite, "
    "d.validation_error, d.scan_status, "
    "COALESCE(a.user_decision, d.user_decision, 'unknown'), "
    "COALESCE(a.hidden, 0), "
    "d.scan_root, d.app_group_key, d.app_name, d.app_bundle_id, "
    "d.app_container_path, d.app_bundle_path, d.app_source, "
    "COALESCE(NULLIF(d.origin_kind, ''), 'other'), d.location_tail, "
    "d.first_seen_at, d.last_seen_at, d.last_scanned_at "
    "FROM discovered_databases d "
    "LEFT JOIN database_access_settings a ON a.path = d.path "
    "ORDER BY d.last_seen_at DESC, d.id DESC;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "strappy_db_list_discovered_databases received no output.");
    return 0;
  }
  strappy_discovered_database_record_list_init(list);

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database list: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_discovered_database_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_discovered_database_record))) {
      strappy_set_error(error_out, "Discovered database list is too large.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_discovered_database_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_discovered_database_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_discovered_database_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate discovered database list.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_discovered_database_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_discovered_database_record_init(&list->records[list->count]);
    if (!strappy_db_assign_discovered_database_from_statement(
          &list->records[list->count],
          stmt,
          error_out)) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_discovered_database_record_list_destroy(list);
      return 0;
    }

    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read discovered database list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_discovered_database_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_update_discovered_database_decision(
  const char *db_path,
  long long catalog_id,
  const char *user_decision,
  char **error_out)
{
  static const char *lookup_sql =
    "SELECT path FROM discovered_databases "
    "WHERE id = ? "
    "AND (? != 'allowed' OR is_valid_sqlite = 1);";
  static const char *access_sql =
    "UPDATE database_access_settings "
    "SET user_decision = ?, "
    "updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') "
    "WHERE path = ?;";
  static const char *legacy_sql =
    "UPDATE discovered_databases "
    "SET user_decision = ? "
    "WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  char *path;
  int rc;

  if (catalog_id <= 0) {
    strappy_set_error(error_out, "Discovered database id is not valid.");
    return 0;
  }

  if (!strappy_db_is_valid_user_decision(user_decision)) {
    strappy_set_error(error_out, "Discovered database decision is not valid.");
    return 0;
  }

  db = NULL;
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin discovered database decision update",
                       error_out)) {
    sqlite3_close(db);
    return 0;
  }

  path = NULL;
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, lookup_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database decision lookup: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)catalog_id);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, user_decision, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind discovered database decision lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    path = strappy_db_column_string(stmt, 0);
  } else if (rc == SQLITE_DONE) {
    strappy_set_error(error_out,
                      "Discovered database was not found or is not valid SQLite.");
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read discovered database decision lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }
  sqlite3_finalize(stmt);

  if (path == NULL) {
    strappy_set_error(error_out, "Could not allocate discovered database path.");
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_set_assistant_database_id(db, catalog_id, path, error_out)) {
    free(path);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, access_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database access decision update: %s",
                                sqlite3_errmsg(db));
    free(path);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, user_decision, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, path, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind database access decision update: %s",
                                sqlite3_errmsg(db));
    free(path);
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update database access decision: %s",
                                sqlite3_errmsg(db));
    free(path);
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }
  if (sqlite3_changes(db) < 1) {
    strappy_set_error(error_out, "Database access setting was not found.");
    free(path);
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, legacy_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database decision update: %s",
                                sqlite3_errmsg(db));
    free(path);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, user_decision, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)catalog_id);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind discovered database decision update: %s",
                                sqlite3_errmsg(db));
    free(path);
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update discovered database decision: %s",
                                sqlite3_errmsg(db));
    free(path);
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }
  sqlite3_finalize(stmt);

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit discovered database decision update",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    free(path);
    sqlite3_close(db);
    return 0;
  }

  free(path);
  sqlite3_close(db);
  return 1;
}

int strappy_db_update_discovered_database_hidden(const char *db_path,
                                                 long long catalog_id,
                                                 int hidden,
                                                 char **error_out)
{
  static const char *lookup_sql =
    "SELECT path FROM discovered_databases "
    "WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  char *path;
  int rc;

  if (catalog_id <= 0) {
    strappy_set_error(error_out, "Discovered database id is not valid.");
    return 0;
  }

  db = NULL;
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin discovered database hidden update",
                       error_out)) {
    sqlite3_close(db);
    return 0;
  }

  path = NULL;
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, lookup_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database hidden lookup: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database hidden update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)catalog_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind discovered database hidden lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database hidden update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    path = strappy_db_column_string(stmt, 0);
  } else if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Discovered database was not found.");
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database hidden update",
                    NULL);
    sqlite3_close(db);
    return 0;
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read discovered database hidden lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database hidden update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }
  sqlite3_finalize(stmt);

  if (path == NULL) {
    strappy_set_error(error_out, "Could not allocate discovered database path.");
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database hidden update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_ensure_database_access_setting(db,
                                                 path,
                                                 hidden,
                                                 error_out)) {
    free(path);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database hidden update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_store_database_access_hidden(db,
                                               path,
                                               hidden,
                                               error_out)) {
    free(path);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database hidden update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit discovered database hidden update",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database hidden update",
                    NULL);
    free(path);
    sqlite3_close(db);
    return 0;
  }

  free(path);
  sqlite3_close(db);
  return 1;
}

int strappy_db_save_exchange(const char *db_path,
                             const char *prompt,
                             const char *response,
                             const char *model,
                             long http_status,
                             const char *metadata_json,
                             const char *message_json,
                             const char *reasoning,
                             char **error_out)
{
  return strappy_db_save_exchange_with_id(db_path,
                                          prompt,
                                          response,
                                          model,
                                          http_status,
                                          metadata_json,
                                          message_json,
                                          reasoning,
                                          NULL,
                                          error_out);
}

int strappy_db_save_exchange_with_id(const char *db_path,
                                     const char *prompt,
                                     const char *response,
                                     const char *model,
                                     long http_status,
                                     const char *metadata_json,
                                     const char *message_json,
                                     const char *reasoning,
                                     long long *session_id_out,
                                     char **error_out)
{
  static const char *sql =
    "INSERT INTO sessions "
    "(prompt, response, model, http_status) "
    "VALUES (?, ?, ?, ?);";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;
  long long session_id;

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

  if ((prompt == NULL) || (response == NULL)) {
    strappy_set_error(error_out, "Session exchange is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session insert", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session insert: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, prompt, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 2, response, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model != NULL) &&
      (sqlite3_bind_text(stmt, 3, model, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model == NULL) && (sqlite3_bind_null(stmt, 3) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)http_status) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  session_id = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);

  {
    strappy_session_message_input input;

    memset(&input, 0, sizeof(input));
    input.actor = "user";
    input.context_policy = "full";
    input.kind = "prompt";
    input.api_role = "user";
    input.render_role = "user";
    input.role = "user";
    input.content = prompt;
    input.include_in_context = 1;
    if (!strappy_db_insert_message(db, session_id, &input, error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
      sqlite3_close(db);
      return 0;
    }

    memset(&input, 0, sizeof(input));
    input.actor = "user";
    input.context_policy = "full";
    input.kind = "assistant";
    input.api_role = "assistant";
    input.render_role = "assistant";
    input.role = "assistant";
    input.content = response;
    input.model = model;
    input.http_status = http_status;
    input.metadata_json = metadata_json;
    input.message_json = message_json;
    input.reasoning = reasoning;
    input.include_in_context = 1;
    if (!strappy_db_insert_message(db, session_id, &input, error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
      sqlite3_close(db);
      return 0;
    }
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session insert", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (session_id_out != NULL) {
    *session_id_out = session_id;
  }

  sqlite3_close(db);
  return 1;
}

static int strappy_db_insert_message_sequence(
  sqlite3 *db,
  long long session_id,
  const strappy_session_message_input *messages,
  size_t message_count,
  char **error_out)
{
  size_t index;

  if ((messages == NULL) && (message_count > 0U)) {
    strappy_set_error(error_out, "Session message sequence is missing.");
    return 0;
  }

  for (index = 0U; index < message_count; index++) {
    if (!strappy_db_upsert_message(db,
                                   session_id,
                                   &messages[index],
                                   error_out)) {
      return 0;
    }
  }

  return 1;
}

static int strappy_db_update_session_summary(sqlite3 *db,
                                             long long session_id,
                                             const char *prompt,
                                             const char *response,
                                             const char *model,
                                             long http_status,
                                             char **error_out)
{
  static const char *update_sql =
    "UPDATE sessions "
    "SET prompt = CASE WHEN prompt = '' THEN ? ELSE prompt END, "
    "response = ?, http_status = ? "
    "WHERE id = ?;";
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if ((session_id <= 0) || (prompt == NULL) || (response == NULL)) {
    strappy_set_error(error_out, "Session summary update is incomplete.");
    return 0;
  }
  (void)model;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, prompt, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 2, response, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)http_status) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)session_id) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

int strappy_db_create_session(const char *db_path,
                              long long *session_id_out,
                              char **error_out)
{
  static const char *sql =
    "INSERT INTO sessions "
    "(prompt, response, model, http_status) "
    "VALUES ('', '', ?, 0);";
  char *default_model_id;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  default_model_id = NULL;
  if (!strappy_db_copy_default_openrouter_model(db,
                                                &default_model_id,
                                                error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session insert: %s",
                                sqlite3_errmsg(db));
    free(default_model_id);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, default_model_id, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session insert: %s",
                                sqlite3_errmsg(db));
    free(default_model_id);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not create session: %s",
                                sqlite3_errmsg(db));
    free(default_model_id);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  if (session_id_out != NULL) {
    *session_id_out = (long long)sqlite3_last_insert_rowid(db);
  }

  free(default_model_id);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

static int strappy_db_delete_session_rows(sqlite3 *db,
                                          const char *sql,
                                          long long session_id,
                                          const char *prepare_error,
                                          const char *bind_error,
                                          const char *step_error,
                                          char **error_out)
{
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "%s: %s",
                                prepare_error,
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "%s: %s",
                                bind_error,
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "%s: %s",
                                step_error,
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

int strappy_db_delete_session(const char *db_path,
                              long long session_id,
                              char **error_out)
{
  sqlite3 *db;

  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin session delete",
                       error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_delete_session_rows(
        db,
        "DELETE FROM response_api_item_parts WHERE response_item_id IN "
        "(SELECT id FROM response_api_items WHERE session_id = ?);",
        session_id,
        "Could not prepare Responses item part delete",
        "Could not bind Responses item part delete",
        "Could not delete Responses item parts",
        error_out) ||
      !strappy_db_delete_session_rows(
        db,
        "DELETE FROM response_tool_executions WHERE session_id = ?;",
        session_id,
        "Could not prepare Responses tool execution delete",
        "Could not bind Responses tool execution delete",
        "Could not delete Responses tool executions",
        error_out) ||
      !strappy_db_delete_session_rows(
        db,
        "DELETE FROM response_api_items WHERE session_id = ?;",
        session_id,
        "Could not prepare Responses item delete",
        "Could not bind Responses item delete",
        "Could not delete Responses items",
        error_out) ||
      !strappy_db_delete_session_rows(
        db,
        "DELETE FROM response_api_calls WHERE session_id = ?;",
        session_id,
        "Could not prepare Responses call delete",
        "Could not bind Responses call delete",
        "Could not delete Responses calls",
        error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session delete", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_delete_session_rows(
        db,
        "DELETE FROM session_messages WHERE session_id = ?;",
        session_id,
        "Could not prepare session message delete",
        "Could not bind session message delete",
        "Could not delete session messages",
        error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session delete", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_delete_session_rows(
        db,
        "DELETE FROM session_turns WHERE session_id = ?;",
        session_id,
        "Could not prepare session turn delete",
        "Could not bind session turn delete",
        "Could not delete session turns",
        error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session delete", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_delete_session_rows(
        db,
        "DELETE FROM sessions WHERE id = ?;",
        session_id,
        "Could not prepare session delete",
        "Could not bind session delete",
        "Could not delete session",
        error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session delete", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit session delete",
                       error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session delete", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_update_session_name_if_empty(const char *db_path,
                                            long long session_id,
                                            const char *name,
                                            int *did_update_out,
                                            char **error_out)
{
  static const char *sql =
    "UPDATE sessions "
    "SET name = ? "
    "WHERE id = ? AND name = '';";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int changed;

  if (did_update_out != NULL) {
    *did_update_out = 0;
  }

  if ((name == NULL) || (name[0] == '\0')) {
    strappy_set_error(error_out, "Session name is empty.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session name update: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  if ((sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session name update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session name: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  changed = sqlite3_changes(db);
  if (did_update_out != NULL) {
    *did_update_out = (changed > 0) ? 1 : 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_update_session_streaming_enabled(const char *db_path,
                                                long long session_id,
                                                int streaming_enabled,
                                                char **error_out)
{
  static const char *sql =
    "UPDATE sessions "
    "SET streaming_enabled = ? "
    "WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session streaming update: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  if ((sqlite3_bind_int(stmt, 1, streaming_enabled ? 1 : 0) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session streaming update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session streaming setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_save_message_sequence_with_id(
  const char *db_path,
  const char *prompt,
  const char *response,
  const char *model,
  long http_status,
  const strappy_session_message_input *messages,
  size_t message_count,
  long long *session_id_out,
  char **error_out)
{
  static const char *sql =
    "INSERT INTO sessions "
    "(prompt, response, model, http_status) "
    "VALUES (?, ?, ?, ?);";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;
  long long session_id;

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

  if ((prompt == NULL) || (response == NULL) ||
      ((messages == NULL) && (message_count > 0U))) {
    strappy_set_error(error_out, "Session message sequence is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session insert", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session insert: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, prompt, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 2, response, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model != NULL) &&
      (sqlite3_bind_text(stmt, 3, model, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok && (model == NULL) && (sqlite3_bind_null(stmt, 3) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)http_status) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  session_id = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);

  if (!strappy_db_insert_message_sequence(db,
                                          session_id,
                                          messages,
                                          message_count,
                                          error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session insert", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (session_id_out != NULL) {
    *session_id_out = session_id;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_update_session_web_search_enabled(const char *db_path,
                                                 long long session_id,
                                                 int web_search_enabled,
                                                 char **error_out)
{
  static const char *sql =
    "UPDATE sessions "
    "SET web_search_enabled = ? "
    "WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session web search update: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  if ((sqlite3_bind_int(stmt, 1, web_search_enabled ? 1 : 0) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session web search update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session web search setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_list_sessions(const char *db_path,
                             strappy_session_record_list *list,
                             char **error_out)
{
  static const char *sql =
    "SELECT id, name, prompt, response, model, http_status, created_at, "
    "web_search_enabled, streaming_enabled "
    "FROM sessions "
    "ORDER BY id DESC;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "strappy_db_list_sessions received no output.");
    return 0;
  }
  strappy_session_record_list_init(list);

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session list: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_session_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_session_record))) {
      strappy_set_error(error_out, "Session list is too large.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_session_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_session_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate session list.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_session_record_init(&list->records[list->count]);
    if (!strappy_db_assign_record_from_statement(&list->records[list->count],
                                                 stmt,
                                                 error_out)) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read session list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_session_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_load_session(const char *db_path,
                            long long session_id,
                            strappy_session_record *record,
                            char **error_out)
{
  static const char *sql =
    "SELECT id, name, prompt, response, model, http_status, created_at, "
    "web_search_enabled, streaming_enabled "
    "FROM sessions "
    "WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if (record == NULL) {
    strappy_set_error(error_out, "strappy_db_load_session received no output.");
    return 0;
  }
  strappy_session_record_init(record);

  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session load: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session load: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read session: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  ok = strappy_db_assign_record_from_statement(record, stmt, error_out);
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  if (!ok) {
    strappy_session_record_destroy(record);
    return 0;
  }

  return 1;
}

int strappy_db_append_exchange_to_session(const char *db_path,
                                          long long session_id,
                                          const char *prompt,
                                          const char *response,
                                          const char *model,
                                          long http_status,
                                          const char *metadata_json,
                                          const char *message_json,
                                          const char *reasoning,
                                          char **error_out)
{
  static const char *update_sql =
    "UPDATE sessions "
    "SET prompt = CASE WHEN prompt = '' THEN ? ELSE prompt END, "
    "response = ?, http_status = ? "
    "WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if ((prompt == NULL) || (response == NULL)) {
    strappy_set_error(error_out, "Session exchange is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session append", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  {
    strappy_session_message_input input;

    memset(&input, 0, sizeof(input));
    input.actor = "user";
    input.context_policy = "full";
    input.kind = "prompt";
    input.api_role = "user";
    input.render_role = "user";
    input.role = "user";
    input.content = prompt;
    input.include_in_context = 1;
    if (!strappy_db_insert_message(db, session_id, &input, error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
      sqlite3_close(db);
      return 0;
    }

    memset(&input, 0, sizeof(input));
    input.actor = "user";
    input.context_policy = "full";
    input.kind = "assistant";
    input.api_role = "assistant";
    input.render_role = "assistant";
    input.role = "assistant";
    input.content = response;
    input.model = model;
    input.http_status = http_status;
    input.metadata_json = metadata_json;
    input.message_json = message_json;
    input.reasoning = reasoning;
    input.include_in_context = 1;
    if (!strappy_db_insert_message(db, session_id, &input, error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
      sqlite3_close(db);
      return 0;
    }
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session update: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  ok = 1;
  if (sqlite3_bind_text(stmt, 1, prompt, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_text(stmt, 2, response, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)http_status) != SQLITE_OK)) {
    ok = 0;
  }
  if (ok &&
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)session_id) != SQLITE_OK)) {
    ok = 0;
  }

  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_finalize(stmt);

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session append", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_append_message_sequence_to_session(
  const char *db_path,
  long long session_id,
  const char *prompt,
  const char *response,
  const char *model,
  long http_status,
  const strappy_session_message_input *messages,
  size_t message_count,
  char **error_out)
{
  sqlite3 *db;

  if ((session_id <= 0) || (prompt == NULL) || (response == NULL) ||
      ((messages == NULL) && (message_count > 0U))) {
    strappy_set_error(error_out, "Session message sequence is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session append", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_insert_message_sequence(db,
                                          session_id,
                                          messages,
                                          message_count,
                                          error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_update_session_summary(db,
                                         session_id,
                                         prompt,
                                         response,
                                         model,
                                         http_status,
                                         error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session append", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_upsert_session_message(
  const char *db_path,
  long long session_id,
  const strappy_session_message_input *message,
  char **error_out)
{
  sqlite3 *db;

  if ((session_id <= 0) || (message == NULL)) {
    strappy_set_error(error_out, "Session message upsert is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session message upsert", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_upsert_message(db, session_id, message, error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session message upsert", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session message upsert", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session message upsert", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_append_session_message_content(
  const char *db_path,
  long long session_id,
  const strappy_session_message_input *message,
  const char *content_delta,
  const char *reasoning_delta,
  char **error_out)
{
  sqlite3 *db;

  if ((session_id <= 0) || (message == NULL)) {
    strappy_set_error(error_out, "Streamed session message append is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin streamed message append", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_append_message_content(db,
                                         session_id,
                                         message,
                                         content_delta,
                                         reasoning_delta,
                                         error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back streamed message append", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit streamed message append", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back streamed message append", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_move_session_message_content_to_reasoning(
  const char *db_path,
  long long session_id,
  const strappy_session_message_input *message,
  char **error_out)
{
  sqlite3 *db;

  if ((session_id <= 0) || (message == NULL)) {
    strappy_set_error(error_out, "Streamed session message move is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin streamed message move", error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_move_message_content_to_reasoning(db,
                                                    session_id,
                                                    message,
                                                    error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back streamed message move", NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit streamed message move", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back streamed message move", NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

int strappy_db_update_session_message_render_state(
  const char *db_path,
  long long session_id,
  const char *message_key,
  const char *render_state_json,
  char **error_out)
{
  sqlite3 *db;

  if ((session_id <= 0) || (message_key == NULL) ||
      (message_key[0] == '\0')) {
    strappy_set_error(error_out, "Session message render state update is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin message render state update",
                       error_out)) {
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_update_message_render_state(db,
                                              session_id,
                                              message_key,
                                              render_state_json,
                                              error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back message render state update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit message render state update",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back message render state update",
                    NULL);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_close(db);
  return 1;
}

static const char *strappy_db_session_message_select_clause(void)
{
  return
    "SELECT id, session_id, COALESCE(turn_id, 0), turn_key, "
    "prompt_group_key, actor, kind, api_role, render_role, role, content, "
    "model, http_status, metadata_json, message_json, reasoning, message_key, "
    "target_message_key, tool_call_id, tool_name, arguments_json, "
    "include_in_context, is_error, result_json, created_at, render_state_json "
    "FROM session_messages ";
}

static int strappy_db_list_session_messages_with_filter(
  const char *db_path,
  long long session_id,
  const char *where_suffix,
  strappy_session_message_record_list *list,
  char **error_out)
{
  static const char *all_suffix =
    "FROM session_messages "
    "WHERE session_id = ? "
    "ORDER BY id ASC;";
  char *sql;
  size_t select_length;
  size_t suffix_length;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "strappy_db_list_session_messages received no output.");
    return 0;
  }
  strappy_session_message_record_list_init(list);

  if ((where_suffix == NULL) || (where_suffix[0] == '\0')) {
    where_suffix = all_suffix + strlen("FROM session_messages ");
  }

  select_length = strlen(strappy_db_session_message_select_clause());
  suffix_length = strlen(where_suffix);
  if (select_length > (((size_t)-1) - suffix_length - 1U)) {
    strappy_set_error(error_out, "Session message query is too large.");
    return 0;
  }
  sql = (char *)malloc(select_length + suffix_length + 1U);
  if (sql == NULL) {
    strappy_set_error(error_out, "Could not allocate session message query.");
    return 0;
  }
  memcpy(sql, strappy_db_session_message_select_clause(), select_length);
  memcpy(sql + select_length, where_suffix, suffix_length);
  sql[select_length + suffix_length] = '\0';

  if (!strappy_db_open(db_path, &db, error_out)) {
    free(sql);
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    free(sql);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    free(sql);
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  free(sql);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message list: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_session_message_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_session_message_record))) {
      strappy_set_error(error_out, "Session message list is too large.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_session_message_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_session_message_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate session message list.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_session_message_record_init(&list->records[list->count]);
    if (!strappy_db_assign_message_from_statement(&list->records[list->count],
                                                  stmt,
                                                  error_out)) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }

    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read session message list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_session_message_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_list_session_messages(const char *db_path,
                                     long long session_id,
                                     strappy_session_message_record_list *list,
                                     char **error_out)
{
  return strappy_db_list_session_messages_with_filter(
    db_path,
    session_id,
    "WHERE session_id = ? ORDER BY id ASC;",
    list,
    error_out);
}

int strappy_db_load_session_message_by_key(
  const char *db_path,
  long long session_id,
  const char *message_key,
  strappy_session_message_record *record,
  char **error_out)
{
  static const char *where_suffix =
    "WHERE session_id = ? AND message_key = ? ORDER BY id DESC LIMIT 1;";
  char *sql;
  size_t select_length;
  size_t suffix_length;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (record == NULL) {
    strappy_set_error(error_out, "Session message output is missing.");
    return 0;
  }
  strappy_session_message_record_init(record);

  if ((message_key == NULL) || (message_key[0] == '\0')) {
    strappy_set_error(error_out, "Session message key is missing.");
    return 0;
  }

  select_length = strlen(strappy_db_session_message_select_clause());
  suffix_length = strlen(where_suffix);
  if (select_length > (((size_t)-1) - suffix_length - 1U)) {
    strappy_set_error(error_out, "Session message query is too large.");
    return 0;
  }
  sql = (char *)malloc(select_length + suffix_length + 1U);
  if (sql == NULL) {
    strappy_set_error(error_out, "Could not allocate session message query.");
    return 0;
  }
  memcpy(sql, strappy_db_session_message_select_clause(), select_length);
  memcpy(sql + select_length, where_suffix, suffix_length);
  sql[select_length + suffix_length] = '\0';

  if (!strappy_db_open(db_path, &db, error_out)) {
    free(sql);
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    free(sql);
    sqlite3_close(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    free(sql);
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  free(sql);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, message_key, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (!strappy_db_assign_message_from_statement(record, stmt, error_out)) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      return 0;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 1;
  }

  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session message was not found.");
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read session message lookup: %s",
                                sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 0;
}

int strappy_db_list_session_context_messages(
  const char *db_path,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out)
{
  return strappy_db_list_session_messages_with_filter(
    db_path,
    session_id,
    "WHERE session_id = ? "
    "AND include_in_context = 1 "
    "AND api_role IS NOT NULL "
    "AND api_role != '' "
    "ORDER BY id ASC;",
    list,
    error_out);
}

int strappy_db_save_openrouter_models_json(const char *db_path,
                                           const char *json,
                                           char **error_out)
{
  sqlite3 *db;
  cJSON *root;
  cJSON *data;
  int count;
  int index;

  if ((json == NULL) || (json[0] == '\0')) {
    strappy_set_error(error_out, "OpenRouter model JSON is empty.");
    return 0;
  }

  root = cJSON_Parse(json);
  if (root == NULL) {
    strappy_set_error(error_out, "OpenRouter model JSON could not be parsed.");
    return 0;
  }

  data = cJSON_GetObjectItem(root, "data");
  if (!cJSON_IsArray(data) && cJSON_IsArray(root)) {
    data = root;
  }
  if (!cJSON_IsArray(data)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "OpenRouter model JSON is missing data array.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    cJSON_Delete(root);
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    cJSON_Delete(root);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin OpenRouter model catalog save",
                       error_out)) {
    sqlite3_close(db);
    cJSON_Delete(root);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "DELETE FROM openrouter_model_supported_voices;",
                       "Could not clear OpenRouter supported voices",
                       error_out) ||
      !strappy_db_exec(db,
                       "DELETE FROM openrouter_model_supported_parameters;",
                       "Could not clear OpenRouter supported parameters",
                       error_out) ||
      !strappy_db_exec(db,
                       "DELETE FROM openrouter_model_output_modalities;",
                       "Could not clear OpenRouter output modalities",
                       error_out) ||
      !strappy_db_exec(db,
                       "DELETE FROM openrouter_model_input_modalities;",
                       "Could not clear OpenRouter input modalities",
                       error_out) ||
      !strappy_db_exec(db,
                       "DELETE FROM openrouter_models;",
                       "Could not clear OpenRouter model catalog",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back OpenRouter model catalog save",
                    NULL);
    sqlite3_close(db);
    cJSON_Delete(root);
    return 0;
  }

  count = cJSON_GetArraySize(data);
  for (index = 0; index < count; index++) {
    cJSON *model;

    model = cJSON_GetArrayItem(data, index);
    if (!strappy_db_insert_openrouter_model(db, model, error_out)) {
      strappy_db_exec(db,
                      "ROLLBACK;",
                      "Could not roll back OpenRouter model catalog save",
                      NULL);
      sqlite3_close(db);
      cJSON_Delete(root);
      return 0;
    }
  }

  if (!strappy_db_exec(db,
                       STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL,
                       "Could not save built-in OpenRouter default model",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back OpenRouter model catalog save",
                    NULL);
    sqlite3_close(db);
    cJSON_Delete(root);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit OpenRouter model catalog save",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back OpenRouter model catalog save",
                    NULL);
    sqlite3_close(db);
    cJSON_Delete(root);
    return 0;
  }

  sqlite3_close(db);
  cJSON_Delete(root);
  return 1;
}

int strappy_db_list_openrouter_models(
  const char *db_path,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  return strappy_db_list_openrouter_models_matching(db_path,
                                                    NULL,
                                                    list,
                                                    error_out);
}

int strappy_db_list_allowed_openrouter_models(
  const char *db_path,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  size_t read_index;
  size_t write_index;

  if (!strappy_db_list_openrouter_models(db_path, list, error_out)) {
    return 0;
  }

  write_index = 0U;
  for (read_index = 0U; read_index < list->count; read_index++) {
    if (list->records[read_index].allowed) {
      if (write_index != read_index) {
        strappy_openrouter_model_record_destroy(&list->records[write_index]);
        list->records[write_index] = list->records[read_index];
        strappy_openrouter_model_record_init(&list->records[read_index]);
      }
      write_index++;
    } else {
      strappy_openrouter_model_record_destroy(&list->records[read_index]);
    }
  }
  list->count = write_index;
  return 1;
}

int strappy_db_list_openrouter_models_matching(
  const char *db_path,
  const char *search_text,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  static const char *unfiltered_sql =
    "SELECT m.id, m.canonical_slug, m.hugging_face_id, m.name, "
    "m.description, m.context_length, m.created, m.architecture_modality, "
    "m.architecture_tokenizer, m.architecture_instruct_type, "
    "m.pricing_prompt, m.pricing_completion, m.pricing_request, "
    "m.pricing_image, m.pricing_audio, m.pricing_web_search, "
    "m.pricing_internal_reasoning, m.pricing_input_cache_read, "
    "m.pricing_input_cache_write, m.top_provider_context_length, "
    "m.top_provider_max_completion_tokens, m.top_provider_is_moderated, "
    "m.knowledge_cutoff, m.expiration_date, m.links_details, m.links_json, "
    "m.reasoning_json, m.benchmarks_json, m.default_parameters_json, "
    "m.per_request_limits_json, m.raw_json, m.fetched_at, "
    "CASE WHEN m.id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " THEN 1 ELSE 0 END, "
    "CASE WHEN COALESCE(ms.allowed, 0) = 1 OR m.id = "
    STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL " THEN 1 ELSE 0 END "
    "FROM openrouter_models m "
    "LEFT JOIN openrouter_model_settings ms ON ms.model_id = m.id "
    "ORDER BY CASE WHEN m.id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " THEN 0 ELSE 1 END, "
    "LOWER(COALESCE(NULLIF(m.name, ''), m.id)), m.id;";
  static const char *filtered_sql =
    "SELECT m.id, m.canonical_slug, m.hugging_face_id, m.name, "
    "m.description, m.context_length, m.created, m.architecture_modality, "
    "m.architecture_tokenizer, m.architecture_instruct_type, "
    "m.pricing_prompt, m.pricing_completion, m.pricing_request, "
    "m.pricing_image, m.pricing_audio, m.pricing_web_search, "
    "m.pricing_internal_reasoning, m.pricing_input_cache_read, "
    "m.pricing_input_cache_write, m.top_provider_context_length, "
    "m.top_provider_max_completion_tokens, m.top_provider_is_moderated, "
    "m.knowledge_cutoff, m.expiration_date, m.links_details, m.links_json, "
    "m.reasoning_json, m.benchmarks_json, m.default_parameters_json, "
    "m.per_request_limits_json, m.raw_json, m.fetched_at, "
    "CASE WHEN m.id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " THEN 1 ELSE 0 END, "
    "CASE WHEN COALESCE(ms.allowed, 0) = 1 OR m.id = "
    STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL " THEN 1 ELSE 0 END "
    "FROM openrouter_models m "
    "LEFT JOIN openrouter_model_settings ms ON ms.model_id = m.id "
    "WHERE (m.id LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.canonical_slug, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.hugging_face_id, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.name, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.description, '') LIKE ?1 ESCAPE '\\' "
    "OR CAST(m.context_length AS TEXT) LIKE ?1 ESCAPE '\\' "
    "OR CAST(m.created AS TEXT) LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.architecture_modality, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.architecture_tokenizer, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.architecture_instruct_type, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_prompt, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_completion, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_request, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_image, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_audio, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_web_search, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_internal_reasoning, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_input_cache_read, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.pricing_input_cache_write, '') LIKE ?1 ESCAPE '\\' "
    "OR CAST(m.top_provider_context_length AS TEXT) LIKE ?1 ESCAPE '\\' "
    "OR CAST(m.top_provider_max_completion_tokens AS TEXT) LIKE ?1 ESCAPE '\\' "
    "OR CAST(m.top_provider_is_moderated AS TEXT) LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.knowledge_cutoff, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.expiration_date, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.links_details, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.fetched_at, '') LIKE ?1 ESCAPE '\\' "
    "OR EXISTS (SELECT 1 FROM openrouter_model_input_modalities im "
    "WHERE im.model_id = m.id AND im.modality LIKE ?1 ESCAPE '\\') "
    "OR EXISTS (SELECT 1 FROM openrouter_model_output_modalities om "
    "WHERE om.model_id = m.id AND om.modality LIKE ?1 ESCAPE '\\') "
    "OR EXISTS (SELECT 1 FROM openrouter_model_supported_parameters sp "
    "WHERE sp.model_id = m.id AND sp.parameter LIKE ?1 ESCAPE '\\') "
    "OR EXISTS (SELECT 1 FROM openrouter_model_supported_voices sv "
    "WHERE sv.model_id = m.id AND sv.voice LIKE ?1 ESCAPE '\\')) "
    "ORDER BY CASE WHEN m.id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " THEN 0 ELSE 1 END, "
    "LOWER(COALESCE(NULLIF(m.name, ''), m.id)), m.id;";
  const char *sql;
  char *search_pattern;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "strappy_db_list_openrouter_models received no output.");
    return 0;
  }
  strappy_openrouter_model_record_list_init(list);

  sql = unfiltered_sql;
  search_pattern = NULL;
  if ((search_text != NULL) && (search_text[0] != '\0')) {
    search_pattern = strappy_db_like_pattern_for_search(search_text, error_out);
    if (search_pattern == NULL) {
      return 0;
    }
    sql = filtered_sql;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    free(search_pattern);
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    free(search_pattern);
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare OpenRouter model list: %s",
                                sqlite3_errmsg(db));
    free(search_pattern);
    sqlite3_close(db);
    return 0;
  }

  if (search_pattern != NULL) {
    rc = sqlite3_bind_text(stmt, 1, search_pattern, -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
      strappy_set_formatted_error(error_out,
                                  "Could not bind OpenRouter model search: %s",
                                  sqlite3_errmsg(db));
      free(search_pattern);
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      return 0;
    }
    free(search_pattern);
    search_pattern = NULL;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_openrouter_model_record *next_records;

    if (list->count >= (((size_t)-1) /
                        sizeof(strappy_openrouter_model_record))) {
      strappy_set_error(error_out, "OpenRouter model list is too large.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_openrouter_model_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_openrouter_model_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate OpenRouter model list.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_openrouter_model_record_init(&list->records[list->count]);
    if (!strappy_db_assign_openrouter_model_from_statement(
          &list->records[list->count],
          stmt,
          error_out)) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }

    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read OpenRouter model list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_openrouter_model_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

static int strappy_db_model_exists(sqlite3 *db,
                                   const char *model_id,
                                   char **error_out)
{
  static const char *exists_sql =
    "SELECT 1 FROM openrouter_models WHERE id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if ((model_id == NULL) || (model_id[0] == '\0')) {
    strappy_set_error(error_out, "OpenRouter model id is empty.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, exists_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare OpenRouter model lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind OpenRouter model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "OpenRouter model was not found.");
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read OpenRouter model lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int strappy_db_upsert_app_setting(sqlite3 *db,
                                         const char *key,
                                         const char *value,
                                         const char *error_prefix,
                                         char **error_out)
{
  static const char *update_sql =
    "UPDATE app_settings "
    "SET value = ?, updated_at = (strftime('%Y-%m-%dT%H:%M:%fZ','now')) "
    "WHERE key = ?;";
  static const char *insert_sql =
    "INSERT INTO app_settings (key, value) VALUES (?, ?);";
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare %s update: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    return 0;
  }
  rc = sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, key, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind %s update: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update %s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  if (sqlite3_changes(db) == 0) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare %s insert: %s",
                                  error_prefix,
                                  sqlite3_errmsg(db));
      return 0;
    }
    rc = sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
      rc = sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    }
    if (rc != SQLITE_OK) {
      strappy_set_formatted_error(error_out,
                                  "Could not bind %s insert: %s",
                                  error_prefix,
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      strappy_set_formatted_error(error_out,
                                  "Could not save %s: %s",
                                  error_prefix,
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
  }

  return 1;
}

static int strappy_db_copy_default_openrouter_model(sqlite3 *db,
                                                    char **model_id_out,
                                                    char **error_out)
{
  static const char *sql =
    "SELECT " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL ";";
  sqlite3_stmt *stmt;
  int rc;

  if (model_id_out == NULL) {
    strappy_set_error(error_out, "Default OpenRouter model output is missing.");
    return 0;
  }
  *model_id_out = NULL;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare default model lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *model_id_out = strappy_db_column_string(stmt, 0);
    if ((*model_id_out == NULL) || ((*model_id_out)[0] == '\0')) {
      sqlite3_finalize(stmt);
      free(*model_id_out);
      *model_id_out = strappy_string_duplicate(STRAPPY_CONFIG_DEFAULT_API_MODEL);
      if (*model_id_out == NULL) {
        strappy_set_error(error_out, "Could not allocate default model id.");
        return 0;
      }
      return 1;
    }
  } else if (rc == SQLITE_DONE) {
    *model_id_out = strappy_string_duplicate(STRAPPY_CONFIG_DEFAULT_API_MODEL);
    if (*model_id_out == NULL) {
      sqlite3_finalize(stmt);
      strappy_set_error(error_out, "Could not allocate default model id.");
      return 0;
    }
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read default model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_model_is_effectively_allowed(sqlite3 *db,
                                                   const char *model_id,
                                                   int *allowed_out,
                                                   char **error_out)
{
  static const char *sql =
    "SELECT CASE WHEN ? = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " OR EXISTS (SELECT 1 FROM openrouter_model_settings "
    "WHERE model_id = ? AND allowed = 1) THEN 1 ELSE 0 END;";
  sqlite3_stmt *stmt;
  int rc;

  if (allowed_out == NULL) {
    strappy_set_error(error_out, "Allowed model output is missing.");
    return 0;
  }
  *allowed_out = 0;
  if ((model_id == NULL) || (model_id[0] == '\0')) {
    strappy_set_error(error_out, "OpenRouter model id is empty.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare allowed model lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, model_id, -1, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind allowed model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *allowed_out = sqlite3_column_int(stmt, 0) ? 1 : 0;
    sqlite3_finalize(stmt);
    return 1;
  }

  sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    return 1;
  }

  strappy_set_formatted_error(error_out,
                              "Could not read allowed model lookup: %s",
                              sqlite3_errmsg(db));
  return 0;
}

static int strappy_db_set_openrouter_model_allowed_in_db(sqlite3 *db,
                                                         const char *model_id,
                                                         int allowed,
                                                         char **error_out)
{
  static const char *sql =
    "INSERT OR REPLACE INTO openrouter_model_settings "
    "(model_id, allowed, updated_at) "
    "VALUES (?, ?, strftime('%Y-%m-%dT%H:%M:%fZ','now'));";
  char *default_model_id;
  sqlite3_stmt *stmt;
  int rc;

  if ((model_id == NULL) || (model_id[0] == '\0')) {
    strappy_set_error(error_out, "OpenRouter model id is empty.");
    return 0;
  }

  default_model_id = NULL;
  if (!strappy_db_copy_default_openrouter_model(db, &default_model_id, error_out)) {
    return 0;
  }
  if (!allowed && (default_model_id != NULL) &&
      (strcmp(model_id, default_model_id) == 0)) {
    free(default_model_id);
    strappy_set_error(error_out, "The default model is always allowed.");
    return 0;
  }
  free(default_model_id);

  if (!strappy_db_model_exists(db, model_id, error_out)) {
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare OpenRouter model setting: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 2, allowed ? 1 : 0);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind OpenRouter model setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save OpenRouter model setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return 1;
}

int strappy_db_set_openrouter_model_allowed(const char *db_path,
                                            const char *model_id,
                                            int allowed,
                                            char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  ok = strappy_db_set_openrouter_model_allowed_in_db(db,
                                                     model_id,
                                                     allowed,
                                                     error_out);
  sqlite3_close(db);
  return ok;
}

int strappy_db_set_default_openrouter_model(const char *db_path,
                                            const char *model_id,
                                            char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }
  if (!strappy_db_model_exists(db, model_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  ok = strappy_db_upsert_app_setting(db,
                                     STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_KEY,
                                     model_id,
                                     "default model",
                                     error_out);
  if (ok) {
    ok = strappy_db_set_openrouter_model_allowed_in_db(db,
                                                       model_id,
                                                       1,
                                                       error_out);
  }

  sqlite3_close(db);
  return ok;
}

int strappy_db_get_default_openrouter_model(const char *db_path,
                                            char **model_id_out,
                                            char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  ok = strappy_db_copy_default_openrouter_model(db, model_id_out, error_out);
  sqlite3_close(db);
  return ok;
}

int strappy_db_set_selected_openrouter_model(const char *db_path,
                                             const char *model_id,
                                             char **error_out)
{
  return strappy_db_set_default_openrouter_model(db_path, model_id, error_out);
}

int strappy_db_get_selected_openrouter_model(const char *db_path,
                                             char **model_id_out,
                                             char **error_out)
{
  return strappy_db_get_default_openrouter_model(db_path,
                                                 model_id_out,
                                                 error_out);
}

int strappy_db_update_session_model(const char *db_path,
                                    long long session_id,
                                    const char *model_id,
                                    char **error_out)
{
  static const char *sql =
    "UPDATE sessions SET model = ? WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int allowed;
  int rc;

  if ((session_id <= 0) || (model_id == NULL) || (model_id[0] == '\0')) {
    strappy_set_error(error_out, "Session model update is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }
  if (!strappy_db_session_exists(db, session_id, error_out) ||
      !strappy_db_model_exists(db, model_id, error_out) ||
      !strappy_db_model_is_effectively_allowed(db, model_id, &allowed, error_out)) {
    sqlite3_close(db);
    return 0;
  }
  if (!allowed) {
    sqlite3_close(db);
    strappy_set_error(error_out, "OpenRouter model is not allowed.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session model update: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  rc = sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session model update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session model: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_get_session_model(const char *db_path,
                                 long long session_id,
                                 char **model_id_out,
                                 char **error_out)
{
  static const char *sql =
    "SELECT CASE WHEN NULLIF(s.model, '') IS NOT NULL AND "
    "(s.model = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " OR EXISTS (SELECT 1 FROM openrouter_model_settings ms "
    "WHERE ms.model_id = s.model AND ms.allowed = 1)) "
    "THEN s.model ELSE " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " END FROM sessions s WHERE s.id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (model_id_out == NULL) {
    strappy_set_error(error_out, "Session model output is missing.");
    return 0;
  }
  *model_id_out = NULL;
  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *model_id_out = strappy_db_column_string(stmt, 0);
    if ((*model_id_out == NULL) || ((*model_id_out)[0] == '\0')) {
      free(*model_id_out);
      *model_id_out = strappy_string_duplicate(STRAPPY_CONFIG_DEFAULT_API_MODEL);
    }
    if (*model_id_out == NULL) {
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_set_error(error_out, "Could not allocate session model id.");
      return 0;
    }
  } else if (rc == SQLITE_DONE) {
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_set_error(error_out, "Session was not found.");
    return 0;
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read session model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

static cJSON *strappy_db_response_json_path(cJSON *root, const char *path)
{
  cJSON *current;
  const char *cursor;

  if ((root == NULL) || (path == NULL) || (path[0] == '\0')) {
    return NULL;
  }

  current = root;
  cursor = path;
  while ((current != NULL) && (*cursor != '\0')) {
    const char *dot;
    size_t length;
    char name[96];

    dot = strchr(cursor, '.');
    length = (dot != NULL) ? (size_t)(dot - cursor) : strlen(cursor);
    if ((length == 0U) || (length >= sizeof(name)) ||
        !cJSON_IsObject(current)) {
      return NULL;
    }
    memcpy(name, cursor, length);
    name[length] = '\0';
    current = cJSON_GetObjectItem(current, name);
    if (dot == NULL) {
      break;
    }
    cursor = dot + 1;
  }
  return current;
}

static int strappy_db_bind_nullable_text_value(sqlite3_stmt *stmt,
                                               int index,
                                               const char *value)
{
  if (value == NULL) {
    return sqlite3_bind_null(stmt, index) == SQLITE_OK;
  }
  return sqlite3_bind_text(stmt,
                           index,
                           value,
                           -1,
                           SQLITE_TRANSIENT) == SQLITE_OK;
}

static int strappy_db_bind_response_field(
  sqlite3_stmt *stmt,
  int index,
  cJSON *root,
  const strappy_response_field_definition *field)
{
  cJSON *value;
  char *json;
  int rc;

  if ((stmt == NULL) || (field == NULL)) {
    return 0;
  }

  value = strappy_db_response_json_path(root, field->json_path);
  if ((value == NULL) || cJSON_IsNull(value)) {
    return sqlite3_bind_null(stmt, index) == SQLITE_OK;
  }

  if (field->type == STRAPPY_RESPONSE_FIELD_INTEGER) {
    if (cJSON_IsBool(value)) {
      return sqlite3_bind_int(stmt, index, cJSON_IsTrue(value) ? 1 : 0) ==
        SQLITE_OK;
    }
    if (cJSON_IsNumber(value)) {
      return sqlite3_bind_int64(stmt,
                                index,
                                (sqlite3_int64)value->valuedouble) == SQLITE_OK;
    }
    return sqlite3_bind_null(stmt, index) == SQLITE_OK;
  }

  if (field->type == STRAPPY_RESPONSE_FIELD_REAL) {
    if (cJSON_IsNumber(value)) {
      return sqlite3_bind_double(stmt, index, value->valuedouble) == SQLITE_OK;
    }
    return sqlite3_bind_null(stmt, index) == SQLITE_OK;
  }

  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return sqlite3_bind_text(stmt,
                             index,
                             value->valuestring,
                             -1,
                             SQLITE_TRANSIENT) == SQLITE_OK;
  }

  json = cJSON_PrintUnformatted(value);
  if (json == NULL) {
    return 0;
  }
  rc = sqlite3_bind_text(stmt, index, json, -1, SQLITE_TRANSIENT);
  free(json);
  return rc == SQLITE_OK;
}

static int strappy_db_append_response_column_names(
  strappy_db_sql_buffer *buffer,
  const char *prefix,
  const strappy_response_field_definition *fields,
  size_t field_count)
{
  size_t index;

  for (index = 0U; index < field_count; index++) {
    if (!strappy_db_sql_buffer_append(buffer, ",") ||
        !strappy_db_sql_buffer_append(buffer, prefix) ||
        !strappy_db_sql_buffer_append(buffer, fields[index].column_name)) {
      return 0;
    }
  }
  return 1;
}

static int strappy_db_append_response_placeholders(
  strappy_db_sql_buffer *buffer,
  size_t count)
{
  size_t index;

  for (index = 0U; index < count; index++) {
    if (!strappy_db_sql_buffer_append(buffer, ",?")) {
      return 0;
    }
  }
  return 1;
}

static int strappy_db_response_display_append(strappy_db_sql_buffer *buffer,
                                              const char *text)
{
  if ((text == NULL) || (text[0] == '\0')) {
    return 1;
  }
  if ((buffer->length > 0U) &&
      !strappy_db_sql_buffer_append(buffer, "\n")) {
    return 0;
  }
  return strappy_db_sql_buffer_append(buffer, text);
}

static int strappy_db_response_display_append_json_value(
  strappy_db_sql_buffer *buffer,
  cJSON *value)
{
  cJSON *text;
  cJSON *refusal;

  if ((value == NULL) || cJSON_IsNull(value)) {
    return 1;
  }
  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    return strappy_db_response_display_append(buffer, value->valuestring);
  }
  if (!cJSON_IsObject(value)) {
    return 1;
  }

  text = cJSON_GetObjectItem(value, "text");
  refusal = cJSON_GetObjectItem(value, "refusal");
  if (cJSON_IsString(text) && (text->valuestring != NULL)) {
    return strappy_db_response_display_append(buffer, text->valuestring);
  }
  if (cJSON_IsString(refusal) && (refusal->valuestring != NULL)) {
    return strappy_db_response_display_append(buffer, refusal->valuestring);
  }
  return 1;
}

static int strappy_db_response_display_append_member(
  strappy_db_sql_buffer *buffer,
  cJSON *item,
  const char *name)
{
  cJSON *member;
  cJSON *child;

  if (!cJSON_IsObject(item)) {
    return 1;
  }
  member = cJSON_GetObjectItem(item, name);
  if (cJSON_IsArray(member)) {
    for (child = member->child; child != NULL; child = child->next) {
      if (!strappy_db_response_display_append_json_value(buffer, child)) {
        return 0;
      }
    }
    return 1;
  }
  return strappy_db_response_display_append_json_value(buffer, member);
}

static char *strappy_db_response_item_display_text(cJSON *item)
{
  strappy_db_sql_buffer buffer;
  cJSON *type;
  cJSON *name;
  cJSON *arguments;
  cJSON *member;
  const char *type_text;
  const char *fallback_names[] = {
    "advice", "analysis", "stdout", "stderr", "result", "output",
    "error", "outcome", "prompt", "query", "title", "url", "value"
  };
  size_t index;

  memset(&buffer, 0, sizeof(buffer));
  if (cJSON_IsString(item) && (item->valuestring != NULL)) {
    return strappy_string_duplicate(item->valuestring);
  }
  if (!cJSON_IsObject(item)) {
    return strappy_string_duplicate("");
  }

  type = cJSON_GetObjectItem(item, "type");
  type_text = (cJSON_IsString(type) && (type->valuestring != NULL)) ?
    type->valuestring : "";

  if (strcmp(type_text, "message") == 0) {
    if (!strappy_db_response_display_append_member(&buffer,
                                                   item,
                                                   "content")) {
      strappy_db_sql_buffer_destroy(&buffer);
      return NULL;
    }
  } else if (strcmp(type_text, "reasoning") == 0) {
    if (!strappy_db_response_display_append_member(&buffer,
                                                   item,
                                                   "summary") ||
        !strappy_db_response_display_append_member(&buffer,
                                                   item,
                                                   "content")) {
      strappy_db_sql_buffer_destroy(&buffer);
      return NULL;
    }
  } else if ((strcmp(type_text, "function_call") == 0) ||
             (strcmp(type_text, "custom_tool_call") == 0)) {
    name = cJSON_GetObjectItem(item, "name");
    arguments = cJSON_GetObjectItem(item, "arguments");
    if (!cJSON_IsString(arguments)) {
      arguments = cJSON_GetObjectItem(item, "input");
    }
    if ((cJSON_IsString(name) &&
         !strappy_db_response_display_append(&buffer, name->valuestring)) ||
        (cJSON_IsString(arguments) &&
         !strappy_db_response_display_append(&buffer,
                                             arguments->valuestring))) {
      strappy_db_sql_buffer_destroy(&buffer);
      return NULL;
    }
  } else if ((strcmp(type_text, "function_call_output") == 0) ||
             (strcmp(type_text, "custom_tool_call_output") == 0)) {
    member = cJSON_GetObjectItem(item, "output");
    if (!strappy_db_response_display_append_json_value(&buffer, member)) {
      strappy_db_sql_buffer_destroy(&buffer);
      return NULL;
    }
  } else {
    for (index = 0U;
         (index < (sizeof(fallback_names) / sizeof(fallback_names[0]))) &&
         (buffer.length == 0U);
         index++) {
      member = cJSON_GetObjectItem(item, fallback_names[index]);
      if (!strappy_db_response_display_append_json_value(&buffer, member)) {
        strappy_db_sql_buffer_destroy(&buffer);
        return NULL;
      }
    }
  }

  if ((buffer.length == 0U) && (type_text[0] != '\0') &&
      !strappy_db_sql_buffer_append(&buffer, type_text)) {
    strappy_db_sql_buffer_destroy(&buffer);
    return NULL;
  }
  if (buffer.data == NULL) {
    return strappy_string_duplicate("");
  }
  return buffer.data;
}

static const char *strappy_db_response_item_display_role(cJSON *item,
                                                         const char *direction)
{
  cJSON *type;
  cJSON *role;
  const char *type_text;
  const char *role_text;

  if (!cJSON_IsObject(item)) {
    return (direction != NULL) && (strcmp(direction, "request") == 0) ?
      "user" : "api_item";
  }
  type = cJSON_GetObjectItem(item, "type");
  role = cJSON_GetObjectItem(item, "role");
  type_text = (cJSON_IsString(type) && (type->valuestring != NULL)) ?
    type->valuestring : "";
  role_text = (cJSON_IsString(role) && (role->valuestring != NULL)) ?
    role->valuestring : "";

  if (strcmp(type_text, "message") == 0) {
    if (strcmp(role_text, "assistant") == 0) {
      return "assistant";
    }
    if (strcmp(role_text, "developer") == 0) {
      return "developer";
    }
    if (strcmp(role_text, "system") == 0) {
      return "harness";
    }
    return "user";
  }
  if ((strcmp(type_text, "function_call") == 0) ||
      (strcmp(type_text, "custom_tool_call") == 0)) {
    return "api_function_call";
  }
  if ((strcmp(type_text, "function_call_output") == 0) ||
      (strcmp(type_text, "custom_tool_call_output") == 0)) {
    return "api_function_output";
  }
  if (strcmp(type_text, "reasoning") == 0) {
    return "api_reasoning";
  }
  return "api_item";
}

static int strappy_db_response_item_is_error(cJSON *item)
{
  cJSON *error;
  cJSON *status;

  if (!cJSON_IsObject(item)) {
    return 0;
  }
  error = cJSON_GetObjectItem(item, "error");
  if ((error != NULL) && !cJSON_IsNull(error)) {
    return 1;
  }
  status = cJSON_GetObjectItem(item, "status");
  return (status != NULL) && cJSON_IsString(status) &&
    (status->valuestring != NULL) &&
    ((strcmp(status->valuestring, "failed") == 0) ||
     (strcmp(status->valuestring, "cancelled") == 0));
}

static int strappy_db_insert_response_item_parts(sqlite3 *db,
                                                 long long response_item_id,
                                                 cJSON *item,
                                                 char **error_out)
{
  static const char *sql =
    "INSERT INTO response_api_item_parts "
    "(response_item_id, collection_name, part_index, type, text, refusal, "
    "annotations_json, logprobs_json, raw_json) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
  size_t field_index;

  if (!cJSON_IsObject(item)) {
    return 1;
  }

  for (field_index = 0U;
       field_index < (sizeof(strappy_response_item_fields) /
                      sizeof(strappy_response_item_fields[0]));
       field_index++) {
    cJSON *array;
    cJSON *part;
    int part_index;

    array = strappy_db_response_json_path(
      item,
      strappy_response_item_fields[field_index].json_path);
    if (!cJSON_IsArray(array)) {
      continue;
    }

    part_index = 0;
    for (part = array->child; part != NULL; part = part->next) {
      sqlite3_stmt *stmt;
      cJSON *type;
      cJSON *text;
      cJSON *refusal;
      cJSON *annotations;
      cJSON *logprobs;
      char *annotations_json;
      char *logprobs_json;
      char *raw_json;
      const char *type_value;
      const char *text_value;
      const char *refusal_value;
      int rc;
      int ok;

      type = cJSON_IsObject(part) ? cJSON_GetObjectItem(part, "type") : NULL;
      text = cJSON_IsObject(part) ? cJSON_GetObjectItem(part, "text") : NULL;
      refusal = cJSON_IsObject(part) ?
        cJSON_GetObjectItem(part, "refusal") : NULL;
      annotations = cJSON_IsObject(part) ?
        cJSON_GetObjectItem(part, "annotations") : NULL;
      logprobs = cJSON_IsObject(part) ?
        cJSON_GetObjectItem(part, "logprobs") : NULL;
      annotations_json = (annotations != NULL) ?
        cJSON_PrintUnformatted(annotations) : NULL;
      logprobs_json = (logprobs != NULL) ?
        cJSON_PrintUnformatted(logprobs) : NULL;
      type_value = ((type != NULL) && cJSON_IsString(type)) ?
        type->valuestring : NULL;
      text_value = cJSON_IsString(part) ? part->valuestring :
        (((text != NULL) && cJSON_IsString(text)) ? text->valuestring : NULL);
      refusal_value = ((refusal != NULL) && cJSON_IsString(refusal)) ?
        refusal->valuestring : NULL;
      raw_json = cJSON_PrintUnformatted(part);
      if (raw_json == NULL) {
        free(annotations_json);
        free(logprobs_json);
        strappy_set_error(error_out,
                          "Could not serialize Responses item part.");
        return 0;
      }

      stmt = NULL;
      rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
      if (rc != SQLITE_OK) {
        free(raw_json);
        free(annotations_json);
        free(logprobs_json);
        strappy_set_formatted_error(
          error_out,
          "Could not prepare Responses item part insert: %s",
          sqlite3_errmsg(db));
        return 0;
      }
      ok = sqlite3_bind_int64(stmt,
                              1,
                              (sqlite3_int64)response_item_id) == SQLITE_OK &&
           sqlite3_bind_text(stmt,
                             2,
                             strappy_response_item_fields[field_index].json_path,
                             -1,
                             SQLITE_TRANSIENT) == SQLITE_OK &&
           sqlite3_bind_int(stmt, 3, part_index) == SQLITE_OK &&
           strappy_db_bind_nullable_text_value(
             stmt,
             4,
             type_value) &&
           strappy_db_bind_nullable_text_value(
             stmt,
             5,
             text_value) &&
           strappy_db_bind_nullable_text_value(
             stmt,
             6,
             refusal_value) &&
           strappy_db_bind_nullable_text_value(stmt, 7, annotations_json) &&
           strappy_db_bind_nullable_text_value(stmt, 8, logprobs_json) &&
           sqlite3_bind_text(stmt,
                             9,
                             raw_json,
                             -1,
                             SQLITE_TRANSIENT) == SQLITE_OK;
      free(raw_json);
      free(annotations_json);
      free(logprobs_json);
      if (!ok) {
        strappy_set_formatted_error(
          error_out,
          "Could not bind Responses item part insert: %s",
          sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 0;
      }
      rc = sqlite3_step(stmt);
      sqlite3_finalize(stmt);
      if (rc != SQLITE_DONE) {
        strappy_set_formatted_error(
          error_out,
          "Could not save Responses item part: %s",
          sqlite3_errmsg(db));
        return 0;
      }
      part_index++;
    }
  }
  return 1;
}

static int strappy_db_insert_response_item(sqlite3 *db,
                                           long long response_call_id,
                                           long long session_id,
                                           const char *direction,
                                           long item_index,
                                           int is_canonical,
                                           int timeline_visible,
                                           cJSON *item,
                                           char **error_out)
{
  strappy_db_sql_buffer sql;
  sqlite3_stmt *stmt;
  char *display_text;
  char *raw_json;
  const char *display_role;
  size_t field_count;
  size_t field_index;
  long long response_item_id;
  int parameter;
  int rc;
  int ok;

  field_count = sizeof(strappy_response_item_fields) /
    sizeof(strappy_response_item_fields[0]);
  memset(&sql, 0, sizeof(sql));
  ok = strappy_db_sql_buffer_append(
         &sql,
         "INSERT INTO response_api_items "
         "(response_call_id,session_id,direction,item_index,is_canonical,"
         "timeline_visible,display_role,display_text,is_error") &&
       strappy_db_append_response_column_names(&sql,
                                               "",
                                               strappy_response_item_fields,
                                               field_count) &&
       strappy_db_sql_buffer_append(&sql, ",raw_json) VALUES (?,?,?,?,?,?,?,?,?") &&
       strappy_db_append_response_placeholders(&sql, field_count) &&
       strappy_db_sql_buffer_append(&sql, ",?);");
  if (!ok) {
    strappy_db_sql_buffer_destroy(&sql);
    strappy_set_error(error_out,
                      "Could not allocate Responses item insert.");
    return 0;
  }

  display_text = strappy_db_response_item_display_text(item);
  raw_json = cJSON_PrintUnformatted(item);
  display_role = strappy_db_response_item_display_role(item, direction);
  if ((display_text == NULL) || (raw_json == NULL)) {
    free(display_text);
    free(raw_json);
    strappy_db_sql_buffer_destroy(&sql);
    strappy_set_error(error_out,
                      "Could not serialize Responses item.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql.data, -1, &stmt, NULL);
  strappy_db_sql_buffer_destroy(&sql);
  if (rc != SQLITE_OK) {
    free(display_text);
    free(raw_json);
    strappy_set_formatted_error(error_out,
                                "Could not prepare Responses item insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  parameter = 1;
  ok = sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)response_call_id) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)session_id) == SQLITE_OK &&
       sqlite3_bind_text(stmt,
                         parameter++,
                         direction,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)item_index) == SQLITE_OK &&
       sqlite3_bind_int(stmt, parameter++, is_canonical ? 1 : 0) == SQLITE_OK &&
       sqlite3_bind_int(stmt,
                        parameter++,
                        timeline_visible ? 1 : 0) == SQLITE_OK &&
       sqlite3_bind_text(stmt,
                         parameter++,
                         display_role,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK &&
       sqlite3_bind_text(stmt,
                         parameter++,
                         display_text,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK &&
       sqlite3_bind_int(stmt,
                        parameter++,
                        strappy_db_response_item_is_error(item)) == SQLITE_OK;
  for (field_index = 0U; ok && (field_index < field_count); field_index++) {
    ok = strappy_db_bind_response_field(stmt,
                                        parameter++,
                                        item,
                                        &strappy_response_item_fields[field_index]);
  }
  if (ok) {
    ok = sqlite3_bind_text(stmt,
                           parameter++,
                           raw_json,
                           -1,
                           SQLITE_TRANSIENT) == SQLITE_OK;
  }
  free(display_text);
  free(raw_json);
  if (!ok) {
    strappy_set_formatted_error(error_out,
                                "Could not bind Responses item insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save Responses item: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  response_item_id = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);
  return strappy_db_insert_response_item_parts(db,
                                               response_item_id,
                                               item,
                                               error_out);
}

int strappy_db_begin_response_call(
  const char *db_path,
  const strappy_response_call_begin_input *input,
  long long *call_id_out,
  char **error_out)
{
  strappy_db_sql_buffer sql;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  cJSON *root;
  cJSON *request_input;
  size_t field_count;
  size_t field_index;
  long long call_id;
  int parameter;
  int rc;
  int ok;

  if (call_id_out != NULL) {
    *call_id_out = 0LL;
  }
  if ((input == NULL) || (input->session_id <= 0LL) ||
      (input->prompt_group_key == NULL) ||
      (input->prompt_group_key[0] == '\0') ||
      (input->request_kind == NULL) ||
      (input->request_method == NULL) ||
      (input->request_url == NULL) ||
      (input->request_headers_json == NULL) ||
      (input->request_json == NULL) ||
      (input->request_json[0] == '\0')) {
    strappy_set_error(error_out, "Responses call request is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_session_exists(db, input->session_id, error_out)) {
    sqlite3_close(db);
    return 0;
  }
  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin Responses call",
                       error_out)) {
    sqlite3_close(db);
    return 0;
  }

  field_count = sizeof(strappy_response_request_fields) /
    sizeof(strappy_response_request_fields[0]);
  memset(&sql, 0, sizeof(sql));
  ok = strappy_db_sql_buffer_append(
         &sql,
         "INSERT INTO response_api_calls "
         "(session_id,previous_call_id,prompt_group_key,request_kind,"
         "round_index,attempt_index,new_input_start_index,request_method,"
         "request_url,request_headers_json") &&
       strappy_db_append_response_column_names(&sql,
                                               "request_",
                                               strappy_response_request_fields,
                                               field_count) &&
       strappy_db_sql_buffer_append(
         &sql,
         ",request_raw_json) VALUES (?,?,?,?,?,?,?,?,?,?") &&
       strappy_db_append_response_placeholders(&sql, field_count) &&
       strappy_db_sql_buffer_append(&sql, ",?);");
  if (!ok) {
    strappy_db_sql_buffer_destroy(&sql);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
    sqlite3_close(db);
    strappy_set_error(error_out,
                      "Could not allocate Responses call insert.");
    return 0;
  }

  root = cJSON_Parse(input->request_json);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql.data, -1, &stmt, NULL);
  strappy_db_sql_buffer_destroy(&sql);
  if (rc != SQLITE_OK) {
    cJSON_Delete(root);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
    strappy_set_formatted_error(error_out,
                                "Could not prepare Responses call insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  parameter = 1;
  ok = sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)input->session_id) == SQLITE_OK;
  if (ok && (input->previous_call_id > 0LL)) {
    ok = sqlite3_bind_int64(stmt,
                            parameter++,
                            (sqlite3_int64)input->previous_call_id) == SQLITE_OK;
  } else if (ok) {
    ok = sqlite3_bind_null(stmt, parameter++) == SQLITE_OK;
  }
  ok = ok &&
       sqlite3_bind_text(stmt,
                         parameter++,
                         input->prompt_group_key,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK &&
       sqlite3_bind_text(stmt,
                         parameter++,
                         input->request_kind,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)input->round_index) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)input->attempt_index) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)input->new_input_start_index) ==
         SQLITE_OK &&
       sqlite3_bind_text(stmt,
                         parameter++,
                         input->request_method,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK &&
       sqlite3_bind_text(stmt,
                         parameter++,
                         input->request_url,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK &&
       sqlite3_bind_text(stmt,
                         parameter++,
                         input->request_headers_json,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK;
  for (field_index = 0U; ok && (field_index < field_count); field_index++) {
    ok = strappy_db_bind_response_field(
      stmt,
      parameter++,
      root,
      &strappy_response_request_fields[field_index]);
  }
  if (ok) {
    ok = sqlite3_bind_text(stmt,
                           parameter++,
                           input->request_json,
                           -1,
                           SQLITE_TRANSIENT) == SQLITE_OK;
  }
  if (!ok) {
    sqlite3_finalize(stmt);
    cJSON_Delete(root);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
    strappy_set_formatted_error(error_out,
                                "Could not bind Responses call insert: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    cJSON_Delete(root);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
    strappy_set_formatted_error(error_out,
                                "Could not save Responses call: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  call_id = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);

  request_input = cJSON_IsObject(root) ?
    cJSON_GetObjectItem(root, "input") : NULL;
  if (cJSON_IsArray(request_input)) {
    cJSON *item;
    long item_index;

    item_index = 0L;
    for (item = request_input->child; item != NULL; item = item->next) {
      int is_new;

      is_new = (input->new_input_start_index >= 0L) &&
        (item_index >= input->new_input_start_index);
      if (!strappy_db_insert_response_item(db,
                                           call_id,
                                           input->session_id,
                                           "request",
                                           item_index,
                                           is_new,
                                           is_new,
                                           item,
                                           error_out)) {
        cJSON_Delete(root);
        strappy_db_exec(db,
                        "ROLLBACK;",
                        "Could not roll back Responses call",
                        NULL);
        sqlite3_close(db);
        return 0;
      }
      item_index++;
    }
  } else if (request_input != NULL) {
    int is_new;

    is_new = input->new_input_start_index == 0L;
    if (!strappy_db_insert_response_item(db,
                                         call_id,
                                         input->session_id,
                                         "request",
                                         0L,
                                         is_new,
                                         is_new,
                                         request_input,
                                         error_out)) {
      cJSON_Delete(root);
      strappy_db_exec(db,
                      "ROLLBACK;",
                      "Could not roll back Responses call",
                      NULL);
      sqlite3_close(db);
      return 0;
    }
  }
  cJSON_Delete(root);

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit Responses call",
                       error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
    sqlite3_close(db);
    return 0;
  }
  sqlite3_close(db);
  if (call_id_out != NULL) {
    *call_id_out = call_id;
  }
  return 1;
}

static int strappy_db_append_response_assignments(
  strappy_db_sql_buffer *buffer,
  const char *prefix,
  const strappy_response_field_definition *fields,
  size_t field_count)
{
  size_t index;

  for (index = 0U; index < field_count; index++) {
    if (!strappy_db_sql_buffer_append(buffer, ",") ||
        !strappy_db_sql_buffer_append(buffer, prefix) ||
        !strappy_db_sql_buffer_append(buffer, fields[index].column_name) ||
        !strappy_db_sql_buffer_append(buffer, "=?")) {
      return 0;
    }
  }
  return 1;
}

static int strappy_db_response_call_session_id(sqlite3 *db,
                                               long long call_id,
                                               long long *session_id_out,
                                               char **error_out)
{
  sqlite3_stmt *stmt;
  int rc;

  if (session_id_out == NULL) {
    strappy_set_error(error_out,
                      "Responses call session lookup has no output.");
    return 0;
  }
  *session_id_out = 0LL;

  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "SELECT session_id FROM response_api_calls WHERE id = ?;",
    -1,
    &stmt,
    NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare Responses call lookup: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)call_id);
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc == SQLITE_ROW) {
    *session_id_out = (long long)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return 1;
  }
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Responses call was not found.");
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read Responses call lookup: %s",
                                sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
  return 0;
}

int strappy_db_finish_response_call(
  const char *db_path,
  const strappy_response_call_finish_input *input,
  char **error_out)
{
  strappy_db_sql_buffer sql;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  cJSON *root;
  cJSON *output;
  size_t field_count;
  size_t field_index;
  long long session_id;
  int parameter;
  int rc;
  int ok;

  if ((input == NULL) || (input->call_id <= 0LL) ||
      (input->state == NULL) || (input->state[0] == '\0') ||
      (input->response_headers == NULL) ||
      (input->response_json == NULL)) {
    strappy_set_error(error_out, "Responses call result is incomplete.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_response_call_session_id(db,
                                           input->call_id,
                                           &session_id,
                                           error_out)) {
    sqlite3_close(db);
    return 0;
  }
  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin Responses call result",
                       error_out)) {
    sqlite3_close(db);
    return 0;
  }

  field_count = sizeof(strappy_response_result_fields) /
    sizeof(strappy_response_result_fields[0]);
  memset(&sql, 0, sizeof(sql));
  ok = strappy_db_sql_buffer_append(
         &sql,
         "UPDATE response_api_calls SET "
         "state=?,is_error=?,"
         "transport_completed_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'),"
         "request_started_at_ms=?,transport_completed_at_ms=?,"
         "http_status=?,curl_code=?,retry_after_seconds=?,"
         "request_bytes=?,response_bytes=?,name_lookup_seconds=?,"
         "connect_seconds=?,start_transfer_seconds=?,total_seconds=?,"
         "effective_url=?,transport_error=?,response_content_type=?,"
         "http_request_id=?,http_generation_id=?,"
         "rate_limit_limit=?,rate_limit_remaining=?,rate_limit_reset=?,"
         "rate_limit_limit_requests=?,rate_limit_remaining_requests=?,"
         "rate_limit_reset_requests=?,rate_limit_limit_tokens=?,"
         "rate_limit_remaining_tokens=?,rate_limit_reset_tokens=?") &&
       strappy_db_append_response_assignments(&sql,
                                              "response_",
                                              strappy_response_result_fields,
                                              field_count) &&
       strappy_db_sql_buffer_append(
         &sql,
         ",response_headers_raw=?,response_raw_json=? WHERE id=?;");
  if (!ok) {
    strappy_db_sql_buffer_destroy(&sql);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back Responses call result",
                    NULL);
    sqlite3_close(db);
    strappy_set_error(error_out,
                      "Could not allocate Responses call update.");
    return 0;
  }

  root = cJSON_Parse(input->response_json);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql.data, -1, &stmt, NULL);
  strappy_db_sql_buffer_destroy(&sql);
  if (rc != SQLITE_OK) {
    cJSON_Delete(root);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back Responses call result",
                    NULL);
    strappy_set_formatted_error(error_out,
                                "Could not prepare Responses call update: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  parameter = 1;
  ok = sqlite3_bind_text(stmt,
                         parameter++,
                         input->state,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK &&
       sqlite3_bind_int(stmt, parameter++, input->is_error ? 1 : 0) ==
         SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)input->started_at_ms) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)input->completed_at_ms) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)input->http_status) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)input->curl_code) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)input->retry_after_seconds) ==
         SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)input->request_bytes) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          parameter++,
                          (sqlite3_int64)input->response_bytes) == SQLITE_OK &&
       sqlite3_bind_double(stmt,
                           parameter++,
                           input->name_lookup_seconds) == SQLITE_OK &&
       sqlite3_bind_double(stmt,
                           parameter++,
                           input->connect_seconds) == SQLITE_OK &&
       sqlite3_bind_double(stmt,
                           parameter++,
                           input->start_transfer_seconds) == SQLITE_OK &&
       sqlite3_bind_double(stmt,
                           parameter++,
                           input->total_seconds) == SQLITE_OK &&
       strappy_db_bind_nullable_text_value(stmt,
                                           parameter++,
                                           input->effective_url) &&
       strappy_db_bind_nullable_text_value(stmt,
                                           parameter++,
                                           input->transport_error) &&
       strappy_db_bind_nullable_text_value(stmt,
                                           parameter++,
                                           input->content_type) &&
       strappy_db_bind_nullable_text_value(stmt,
                                           parameter++,
                                           input->request_id) &&
       strappy_db_bind_nullable_text_value(stmt,
                                           parameter++,
                                           input->generation_id) &&
       strappy_db_bind_nullable_text_value(stmt,
                                           parameter++,
                                           input->rate_limit_limit) &&
       strappy_db_bind_nullable_text_value(stmt,
                                           parameter++,
                                           input->rate_limit_remaining) &&
       strappy_db_bind_nullable_text_value(stmt,
                                           parameter++,
                                           input->rate_limit_reset) &&
       strappy_db_bind_nullable_text_value(
         stmt,
         parameter++,
         input->rate_limit_limit_requests) &&
       strappy_db_bind_nullable_text_value(
         stmt,
         parameter++,
         input->rate_limit_remaining_requests) &&
       strappy_db_bind_nullable_text_value(
         stmt,
         parameter++,
         input->rate_limit_reset_requests) &&
       strappy_db_bind_nullable_text_value(stmt,
                                           parameter++,
                                           input->rate_limit_limit_tokens) &&
       strappy_db_bind_nullable_text_value(
         stmt,
         parameter++,
         input->rate_limit_remaining_tokens) &&
       strappy_db_bind_nullable_text_value(stmt,
                                           parameter++,
                                           input->rate_limit_reset_tokens);
  for (field_index = 0U; ok && (field_index < field_count); field_index++) {
    ok = strappy_db_bind_response_field(
      stmt,
      parameter++,
      root,
      &strappy_response_result_fields[field_index]);
  }
  if (ok) {
    ok = sqlite3_bind_text(stmt,
                           parameter++,
                           input->response_headers,
                           -1,
                           SQLITE_TRANSIENT) == SQLITE_OK &&
         sqlite3_bind_text(stmt,
                           parameter++,
                           input->response_json,
                           -1,
                           SQLITE_TRANSIENT) == SQLITE_OK &&
         sqlite3_bind_int64(stmt,
                            parameter++,
                            (sqlite3_int64)input->call_id) == SQLITE_OK;
  }
  if (!ok) {
    sqlite3_finalize(stmt);
    cJSON_Delete(root);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back Responses call result",
                    NULL);
    strappy_set_formatted_error(error_out,
                                "Could not bind Responses call update: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cJSON_Delete(root);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back Responses call result",
                    NULL);
    strappy_set_formatted_error(error_out,
                                "Could not update Responses call: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  output = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "output") : NULL;
  if (cJSON_IsArray(output)) {
    cJSON *item;
    long item_index;

    item_index = 0L;
    for (item = output->child; item != NULL; item = item->next) {
      if (!strappy_db_insert_response_item(db,
                                           input->call_id,
                                           session_id,
                                           "response",
                                           item_index,
                                           input->output_is_canonical,
                                           1,
                                           item,
                                           error_out)) {
        cJSON_Delete(root);
        strappy_db_exec(db,
                        "ROLLBACK;",
                        "Could not roll back Responses call result",
                        NULL);
        sqlite3_close(db);
        return 0;
      }
      item_index++;
    }
  }
  cJSON_Delete(root);

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit Responses call result",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back Responses call result",
                    NULL);
    sqlite3_close(db);
    return 0;
  }
  sqlite3_close(db);
  return 1;
}

int strappy_db_list_canonical_response_items(
  const char *db_path,
  long long session_id,
  strappy_response_item_raw_record_list *list,
  char **error_out)
{
  static const char *sql =
    "SELECT id, raw_json FROM response_api_items "
    "WHERE session_id = ? AND is_canonical = 1 ORDER BY id;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out,
                      "Canonical Responses item list has no output.");
    return 0;
  }
  strappy_response_item_raw_record_list_init(list);
  if (session_id <= 0LL) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(
      error_out,
      "Could not prepare canonical Responses item query: %s",
      sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(
      error_out,
      "Could not bind canonical Responses item query: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_response_item_raw_record *records;
    strappy_response_item_raw_record *record;

    if (list->count >= (((size_t)-1) /
                        sizeof(strappy_response_item_raw_record))) {
      strappy_set_error(error_out,
                        "Canonical Responses item list is too large.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_response_item_raw_record_list_destroy(list);
      return 0;
    }
    records = (strappy_response_item_raw_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(*records));
    if (records == NULL) {
      strappy_set_error(error_out,
                        "Could not allocate canonical Responses item list.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_response_item_raw_record_list_destroy(list);
      return 0;
    }
    list->records = records;
    record = &list->records[list->count];
    record->item_id = (long long)sqlite3_column_int64(stmt, 0);
    record->raw_json = strappy_db_column_string(stmt, 1);
    if (record->raw_json == NULL) {
      strappy_set_error(error_out,
                        "Could not allocate canonical Responses item JSON.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_response_item_raw_record_list_destroy(list);
      return 0;
    }
    list->count++;
  }
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not list canonical Responses items: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_response_item_raw_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}

int strappy_db_save_response_tool_execution(
  const char *db_path,
  const strappy_response_tool_execution_input *input,
  char **error_out)
{
  static const char *sql =
    "INSERT INTO response_tool_executions "
    "(session_id,response_call_id,response_item_id,output_index,call_id,"
    "tool_name,arguments_json,status,output_json,error_text,started_at_ms,"
    "completed_at_ms) "
    "VALUES (?,?,(SELECT id FROM response_api_items "
    "WHERE response_call_id = ? AND direction = 'response' "
    "AND item_index = ?),?,?,?,?,?,?,?,?,?);";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if ((input == NULL) || (input->session_id <= 0LL) ||
      (input->response_call_id <= 0LL) ||
      (input->output_index < 0L) || (input->call_id == NULL) ||
      (input->call_id[0] == '\0') || (input->tool_name == NULL) ||
      (input->tool_name[0] == '\0') || (input->arguments_json == NULL) ||
      (input->status == NULL) || (input->status[0] == '\0')) {
    strappy_set_error(error_out,
                      "Responses tool execution is incomplete.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(
      error_out,
      "Could not prepare Responses tool execution insert: %s",
      sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  ok = sqlite3_bind_int64(stmt,
                          1,
                          (sqlite3_int64)input->session_id) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          2,
                          (sqlite3_int64)input->response_call_id) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          3,
                          (sqlite3_int64)input->response_call_id) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          4,
                          (sqlite3_int64)input->output_index) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          5,
                          (sqlite3_int64)input->output_index) == SQLITE_OK &&
       sqlite3_bind_text(stmt,
                         6,
                         input->call_id,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK &&
       sqlite3_bind_text(stmt,
                         7,
                         input->tool_name,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK &&
       sqlite3_bind_text(stmt,
                         8,
                         input->arguments_json,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK &&
       sqlite3_bind_text(stmt,
                         9,
                         input->status,
                         -1,
                         SQLITE_TRANSIENT) == SQLITE_OK &&
       strappy_db_bind_nullable_text_value(stmt, 10, input->output_json) &&
       strappy_db_bind_nullable_text_value(stmt, 11, input->error_text) &&
       sqlite3_bind_int64(stmt,
                          12,
                          (sqlite3_int64)input->started_at_ms) == SQLITE_OK &&
       sqlite3_bind_int64(stmt,
                          13,
                          (sqlite3_int64)input->completed_at_ms) == SQLITE_OK;
  if (!ok) {
    strappy_set_formatted_error(
      error_out,
      "Could not bind Responses tool execution insert: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save Responses tool execution: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  sqlite3_close(db);
  return 1;
}

int strappy_db_update_response_session_summary(
  const char *db_path,
  long long session_id,
  const char *prompt,
  const char *response,
  const char *model,
  long http_status,
  char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }
  ok = strappy_db_update_session_summary(db,
                                         session_id,
                                         prompt,
                                         response,
                                         model,
                                         http_status,
                                         error_out);
  sqlite3_close(db);
  return ok;
}

static char *strappy_db_response_timeline_key(const char *prefix,
                                              long long identifier)
{
  char buffer[96];
  int written;

  written = snprintf(buffer,
                     sizeof(buffer),
                     "%s-%lld",
                     prefix,
                     identifier);
  if ((written < 0) || ((size_t)written >= sizeof(buffer))) {
    return NULL;
  }
  return strappy_string_duplicate(buffer);
}

static char *strappy_db_response_call_display_text(sqlite3_stmt *stmt)
{
  strappy_db_sql_buffer buffer;
  const unsigned char *request_url;
  const unsigned char *state;
  const unsigned char *model;
  const unsigned char *transport_error;
  const unsigned char *response_status;
  const unsigned char *error_message;
  const unsigned char *incomplete_reason;
  long round_index;
  long attempt_index;
  long http_status;
  char line[512];
  int written;

  memset(&buffer, 0, sizeof(buffer));
  request_url = sqlite3_column_text(stmt, 25);
  state = sqlite3_column_text(stmt, 10);
  model = sqlite3_column_text(stmt, 13);
  transport_error = sqlite3_column_text(stmt, 15);
  response_status = sqlite3_column_text(stmt, 26);
  error_message = sqlite3_column_text(stmt, 27);
  incomplete_reason = sqlite3_column_text(stmt, 28);
  round_index = (long)sqlite3_column_int64(stmt, 8);
  attempt_index = (long)sqlite3_column_int64(stmt, 9);
  http_status = (long)sqlite3_column_int64(stmt, 12);

  written = snprintf(
    line,
    sizeof(line),
    "POST %s\nRound %ld, attempt %ld",
    (request_url != NULL) ? (const char *)request_url : "/responses",
    round_index + 1L,
    attempt_index + 1L);
  if ((written < 0) || ((size_t)written >= sizeof(line)) ||
      !strappy_db_sql_buffer_append(&buffer, line)) {
    strappy_db_sql_buffer_destroy(&buffer);
    return NULL;
  }

  written = snprintf(
    line,
    sizeof(line),
    "\n%s%s%s%s%s",
    (state != NULL) ? (const char *)state : "pending",
    (response_status != NULL) ? " / " : "",
    (response_status != NULL) ? (const char *)response_status : "",
    (http_status > 0L) ? " / HTTP " : "",
    "");
  if ((written < 0) || ((size_t)written >= sizeof(line)) ||
      !strappy_db_sql_buffer_append(&buffer, line)) {
    strappy_db_sql_buffer_destroy(&buffer);
    return NULL;
  }
  if (http_status > 0L) {
    written = snprintf(line, sizeof(line), "%ld", http_status);
    if ((written < 0) || ((size_t)written >= sizeof(line)) ||
        !strappy_db_sql_buffer_append(&buffer, line)) {
      strappy_db_sql_buffer_destroy(&buffer);
      return NULL;
    }
  }
  if ((model != NULL) &&
      (!strappy_db_sql_buffer_append(&buffer, "\n") ||
       !strappy_db_sql_buffer_append(&buffer, (const char *)model))) {
    strappy_db_sql_buffer_destroy(&buffer);
    return NULL;
  }
  if ((transport_error != NULL) &&
      (!strappy_db_sql_buffer_append(&buffer, "\n") ||
       !strappy_db_sql_buffer_append(&buffer,
                                     (const char *)transport_error))) {
    strappy_db_sql_buffer_destroy(&buffer);
    return NULL;
  }
  if ((error_message != NULL) &&
      (!strappy_db_sql_buffer_append(&buffer, "\n") ||
       !strappy_db_sql_buffer_append(&buffer,
                                     (const char *)error_message))) {
    strappy_db_sql_buffer_destroy(&buffer);
    return NULL;
  }
  if ((incomplete_reason != NULL) &&
      (!strappy_db_sql_buffer_append(&buffer, "\nIncomplete: ") ||
       !strappy_db_sql_buffer_append(&buffer,
                                     (const char *)incomplete_reason))) {
    strappy_db_sql_buffer_destroy(&buffer);
    return NULL;
  }
  return buffer.data;
}

int strappy_db_list_response_timeline(
  const char *db_path,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out)
{
  static const char *sql =
    "SELECT 0 AS entry_type,c.id AS row_id,c.id AS call_id,"
    "3 AS sort_phase,-1 AS item_index,NULL AS direction,"
    "c.prompt_group_key,c.request_kind,c.round_index,c.attempt_index,"
    "c.state,c.is_error,c.http_status,"
    "COALESCE(c.response_model,c.request_model),c.request_started_at,"
    "c.transport_error,c.response_raw_json,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,"
    "c.request_url,c.response_status,c.response_error_message,"
    "c.response_incomplete_reason,0,NULL,NULL,NULL,NULL,NULL,"
    "c.response_usage_cost,"
    "2 AS presentation_phase "
    "FROM response_api_calls c WHERE c.session_id = ? "
    "AND c.state <> 'pending' "
    "UNION ALL "
    "SELECT 1,i.id,c.id,CASE WHEN i.direction = 'request' THEN 1 ELSE 2 END,"
    "i.item_index,i.direction,c.prompt_group_key,c.request_kind,"
    "c.round_index,c.attempt_index,c.state,i.is_error,c.http_status,"
    "COALESCE(c.response_model,c.request_model),i.created_at,NULL,NULL,"
    "i.type,i.display_role,i.display_text,i.raw_json,i.call_id,i.name,"
    "i.arguments,i.output,c.request_url,c.response_status,"
    "c.response_error_message,c.response_incomplete_reason,i.is_canonical,"
    "i.action_json,i.url,i.title,i.status,i.http_status,"
    "c.response_usage_cost,"
    "CASE WHEN i.display_role IN "
    "('user','harness','developer','assistant') "
    "THEN CASE WHEN i.direction = 'request' THEN 0 ELSE 3 END "
    "ELSE 1 END "
    "FROM response_api_items i JOIN response_api_calls c "
    "ON c.id = i.response_call_id "
    "WHERE i.session_id = ? AND i.timeline_visible = 1 "
    "AND c.state <> 'pending' "
    "ORDER BY call_id,presentation_phase,sort_phase,item_index;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  long long cumulative_call_id;
  double cumulative_usage_cost;
  int has_cumulative_usage_cost;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "Responses timeline has no output.");
    return 0;
  }
  strappy_session_message_record_list_init(list);
  if (session_id <= 0LL) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    sqlite3_close(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare Responses timeline: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind Responses timeline: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
  }

  cumulative_call_id = 0LL;
  cumulative_usage_cost = 0.0;
  has_cumulative_usage_cost = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_session_message_record *records;
    strappy_session_message_record *record;
    long long row_id;
    long long call_id;
    int entry_type;

    if (list->count >= (((size_t)-1) /
                        sizeof(strappy_session_message_record))) {
      strappy_set_error(error_out, "Responses timeline is too large.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }
    records = (strappy_session_message_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(*records));
    if (records == NULL) {
      strappy_set_error(error_out,
                        "Could not allocate Responses timeline.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }
    list->records = records;
    record = &list->records[list->count];
    strappy_session_message_record_init(record);
    entry_type = sqlite3_column_int(stmt, 0);
    row_id = (long long)sqlite3_column_int64(stmt, 1);
    call_id = (long long)sqlite3_column_int64(stmt, 2);
    if (call_id != cumulative_call_id) {
      cumulative_call_id = call_id;
      if (sqlite3_column_type(stmt, 35) != SQLITE_NULL) {
        cumulative_usage_cost += sqlite3_column_double(stmt, 35);
        has_cumulative_usage_cost = 1;
      }
    }
    record->message_id = (entry_type == 0) ?
      (call_id * 2LL) : ((row_id * 2LL) + 1LL);
    record->session_id = session_id;
    record->turn_id = call_id;
    record->round_index = (long)sqlite3_column_int64(stmt, 8);
    record->attempt_index = (long)sqlite3_column_int64(stmt, 9);
    record->cumulative_usage_cost = cumulative_usage_cost;
    record->has_cumulative_usage_cost = has_cumulative_usage_cost;
    record->turn_key = strappy_db_column_string(stmt, 6);
    record->prompt_group_key = strappy_db_column_string(stmt, 6);
    record->model = strappy_db_column_string(stmt, 13);
    record->created_at = strappy_db_column_string(stmt, 14);
    record->http_status = (long)sqlite3_column_int64(stmt, 12);
    record->is_error = sqlite3_column_int(stmt, 11) ? 1 : 0;
    record->include_in_context = sqlite3_column_int(stmt, 29) ? 1 : 0;

    if (entry_type == 0) {
      record->actor = strappy_string_duplicate("api");
      record->kind = strappy_string_duplicate("response_api_call");
      record->render_role = strappy_string_duplicate(
        record->is_error ? "api_error" : "api_call");
      record->role = strappy_string_duplicate(
        record->is_error ? "api_error" : "api_call");
      record->content = strappy_db_response_call_display_text(stmt);
      record->metadata_json = strappy_db_column_string(stmt, 16);
      record->message_json = strappy_db_column_string(stmt, 16);
      record->message_key =
        strappy_db_response_timeline_key("response-call", call_id);
    } else {
      record->direction = strappy_db_column_string(stmt, 5);
      record->kind = strappy_db_column_string(stmt, 17);
      record->render_role = strappy_db_column_string(stmt, 18);
      record->role = strappy_db_column_string(stmt, 18);
      record->content = strappy_db_column_string(stmt, 19);
      record->message_json = strappy_db_column_string(stmt, 20);
      record->message_key =
        strappy_db_response_timeline_key("response-item", row_id);
      record->tool_call_id = strappy_db_column_string(stmt, 21);
      record->tool_name = strappy_db_column_string(stmt, 22);
      record->arguments_json = strappy_db_column_string(stmt, 23);
      record->result_json = strappy_db_column_string(stmt, 24);
      record->response_item_action_json = strappy_db_column_string(stmt, 30);
      record->response_item_url = strappy_db_column_string(stmt, 31);
      record->response_item_title = strappy_db_column_string(stmt, 32);
      record->response_item_status = strappy_db_column_string(stmt, 33);
      record->response_item_http_status = strappy_db_column_string(stmt, 34);
      if ((record->role != NULL) &&
          (strcmp(record->role, "developer") == 0)) {
        record->actor = strappy_string_duplicate("developer");
        record->api_role = strappy_string_duplicate("developer");
      } else if ((record->role != NULL) &&
                 (strcmp(record->role, "harness") == 0)) {
        record->actor = strappy_string_duplicate("harness");
        record->api_role = strappy_string_duplicate("developer");
      } else if ((record->role != NULL) &&
                 (strcmp(record->role, "user") == 0)) {
        record->actor = strappy_string_duplicate("user");
        record->api_role = strappy_string_duplicate("user");
      } else {
        record->actor = strappy_string_duplicate("assistant");
        record->api_role = strappy_string_duplicate("assistant");
      }
    }

    if ((record->turn_key == NULL) || (record->prompt_group_key == NULL) ||
        (record->actor == NULL) || (record->kind == NULL) ||
        (record->render_role == NULL) || (record->role == NULL) ||
        (record->content == NULL) || (record->message_key == NULL) ||
        ((entry_type != 0) && (record->direction == NULL)) ||
        (record->created_at == NULL)) {
      strappy_session_message_record_destroy(record);
      strappy_set_error(error_out,
                        "Could not allocate Responses timeline row.");
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }
    list->count++;
  }
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read Responses timeline: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    strappy_session_message_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 1;
}
