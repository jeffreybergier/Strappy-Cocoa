#include "strappy_db.h"

#include "strappy_assistant_sets.h"
#include "strappy_config.h"
#include "strappy_core.h"
#include "strappy_tools.h"

#include <cJSON.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#define STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_KEY \
  "default_openrouter_model_id"
#define STRAPPY_DB_SELECTED_OPENROUTER_MODEL_KEY \
  "selected_openrouter_model_id"
#define STRAPPY_DB_BUILTIN_DEFAULT_MODEL_DESCRIPTION "Built-in default model."
#define STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL \
  "COALESCE((SELECT p.default_model_id FROM app_preferences p " \
  "WHERE p.id = 1 " \
  "AND EXISTS (SELECT 1 FROM models dm " \
  "WHERE dm.id = p.default_model_id))," \
  "'" STRAPPY_CONFIG_DEFAULT_API_MODEL "')"
#define STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL \
  "COALESCE(s.model_id, " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL ")"
#define STRAPPY_DB_SESSION_ASSISTANT_SET_SQL \
  "COALESCE((SELECT a.assistant_set_id FROM session_assistant_sets a " \
  "WHERE a.session_id = s.id), '" STRAPPY_ASSISTANT_SET_DEFAULT "')"
#define STRAPPY_DB_SESSION_WEB_PROVIDER_SQL \
  "COALESCE((SELECT x.web_provider FROM session_settings x " \
  "WHERE x.session_id = s.id), 'none')"
#define STRAPPY_DB_DEFAULT_SESSION_WEB_PROVIDER "auto"
#define STRAPPY_DB_SESSION_BASH_ENABLED_SQL \
  "CASE WHEN " STRAPPY_DB_SESSION_ASSISTANT_SET_SQL " = '" \
  STRAPPY_ASSISTANT_SET_CODING_ASSISTANT "' THEN " \
  "COALESCE((SELECT b.bash_enabled FROM session_settings b " \
  "WHERE b.session_id = s.id), 0) ELSE 0 END"
#define STRAPPY_DB_SESSION_STREAMING_ENABLED_SQL \
  "COALESCE((SELECT x.streaming_enabled FROM session_settings x " \
  "WHERE x.session_id = s.id), 0)"
#define STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL \
  "COALESCE((SELECT i.created_at_ms FROM conversation_items i " \
  "WHERE i.session_id = s.id ORDER BY i.sequence DESC LIMIT 1), " \
  "s.created_at_ms)"
#define STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL \
  "INSERT OR IGNORE INTO models " \
  "(id, name, description, catalog_active, last_seen_at_ms) VALUES ('" \
  STRAPPY_CONFIG_DEFAULT_API_MODEL "', '" \
  STRAPPY_CONFIG_DEFAULT_API_MODEL "', '" \
  STRAPPY_DB_BUILTIN_DEFAULT_MODEL_DESCRIPTION "', 1, " \
  "CAST(strftime('%s','now') AS INTEGER) * 1000);"
#define STRAPPY_DB_CONTEXT_ELIGIBLE_ATTEMPT_SQL \
  "a.http_status BETWEEN 200 AND 299 " \
  "AND a.state IN('completed','response_error') " \
  "AND IFNULL(a.transport_error,'')='' " \
  "AND ar.attempt_id IS NOT NULL AND ar.parse_error IS NULL " \
  "AND COALESCE(ar.error_type,ar.error_code,ar.error_message," \
    "ar.error_parameter) IS NULL " \
  "AND IFNULL(ar.provider_status,'') NOT IN('failed','cancelled') " \
  "AND NOT EXISTS(SELECT 1 FROM http_attempts later " \
    "WHERE later.request_id=a.request_id " \
    "AND later.attempt_index>a.attempt_index)"

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
  record->model_name = NULL;
  record->assistant_set_id = NULL;
  record->created_at = NULL;
  record->last_activity_at = NULL;
  record->last_activity_at_ms = 0LL;
  record->web_provider = STRAPPY_WEB_PROVIDER_NONE;
  record->bash_enabled = 0;
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
  free(record->model_name);
  free(record->assistant_set_id);
  free(record->created_at);
  free(record->last_activity_at);
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
  record->model_request_id = 0;
  record->http_attempt_id = 0;
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
  record->request_method = NULL;
  record->request_endpoint = NULL;
  record->created_at = NULL;
  record->attempt_state = NULL;
  record->can_include_in_context = 0;
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
  free(record->request_method);
  free(record->request_endpoint);
  free(record->created_at);
  free(record->attempt_state);
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

void strappy_database_display_name_record_list_init(
  strappy_database_display_name_record_list *list)
{
  if (list == NULL) {
    return;
  }
  list->records = NULL;
  list->count = 0U;
}

void strappy_database_display_name_record_list_destroy(
  strappy_database_display_name_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }
  for (index = 0U; index < list->count; index++) {
    free(list->records[index].assistant_database_id);
    free(list->records[index].filename);
  }
  free(list->records);
  strappy_database_display_name_record_list_init(list);
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

static pthread_mutex_t strappy_db_connection_mutex =
  PTHREAD_MUTEX_INITIALIZER;
static sqlite3 *strappy_db_connection = NULL;
static char *strappy_db_connection_path = NULL;
static dev_t strappy_db_connection_device = (dev_t)0;
static ino_t strappy_db_connection_inode = (ino_t)0;
static int strappy_db_connection_has_identity = 0;
static int strappy_db_connection_schema_ready = 0;
static int strappy_db_connection_shutdown_registered = 0;

static void strappy_db_close_connection_locked(void)
{
  sqlite3_stmt *stmt;

  if (strappy_db_connection == NULL) {
    return;
  }
  while ((stmt = sqlite3_next_stmt(strappy_db_connection, NULL)) != NULL) {
    sqlite3_finalize(stmt);
  }
  if (!sqlite3_get_autocommit(strappy_db_connection)) {
    sqlite3_exec(strappy_db_connection, "ROLLBACK;", NULL, NULL, NULL);
  }
  sqlite3_exec(strappy_db_connection,
               "PRAGMA wal_checkpoint(PASSIVE);",
               NULL,
               NULL,
               NULL);
  sqlite3_close(strappy_db_connection);
  strappy_db_connection = NULL;
  free(strappy_db_connection_path);
  strappy_db_connection_path = NULL;
  strappy_db_connection_device = (dev_t)0;
  strappy_db_connection_inode = (ino_t)0;
  strappy_db_connection_has_identity = 0;
  strappy_db_connection_schema_ready = 0;
}

static void strappy_db_shutdown_connection(void)
{
  if (pthread_mutex_lock(&strappy_db_connection_mutex) != 0) {
    return;
  }
  strappy_db_close_connection_locked();
  pthread_mutex_unlock(&strappy_db_connection_mutex);
}

static void strappy_db_release(sqlite3 *db)
{
  sqlite3_stmt *stmt;

  if (db != strappy_db_connection) {
    if (db != NULL) {
      sqlite3_close(db);
    }
    return;
  }
  while ((stmt = sqlite3_next_stmt(db, NULL)) != NULL) {
    sqlite3_finalize(stmt);
  }
  if (!sqlite3_get_autocommit(db)) {
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
  }
  pthread_mutex_unlock(&strappy_db_connection_mutex);
}

static int strappy_db_open(const char *db_path,
                           sqlite3 **db_out,
                           char **error_out)
{
  struct stat path_stat;
  sqlite3 *db;
  char *path_copy;
  int flags;
  int path_has_identity;
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
  if (pthread_mutex_lock(&strappy_db_connection_mutex) != 0) {
    strappy_set_error(error_out, "Could not lock the session database.");
    return 0;
  }

  path_has_identity = (stat(db_path, &path_stat) == 0) ? 1 : 0;
  if ((strappy_db_connection != NULL) &&
      (strappy_db_connection_path != NULL) &&
      (strcmp(strappy_db_connection_path, db_path) == 0) &&
      (((path_has_identity && strappy_db_connection_has_identity) &&
        (path_stat.st_dev == strappy_db_connection_device) &&
        (path_stat.st_ino == strappy_db_connection_inode)) ||
       (!path_has_identity && !strappy_db_connection_has_identity &&
        (db_path[0] == ':')))) {
    *db_out = strappy_db_connection;
    return 1;
  }

  strappy_db_close_connection_locked();
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
    pthread_mutex_unlock(&strappy_db_connection_mutex);
    return 0;
  }

  sqlite3_busy_timeout(db, 5000);
  if (!strappy_db_enable_write_ahead_log(db, error_out)) {
    sqlite3_close(db);
    pthread_mutex_unlock(&strappy_db_connection_mutex);
    return 0;
  }
  path_copy = strappy_string_duplicate(db_path);
  if (path_copy == NULL) {
    strappy_set_error(error_out, "Could not remember session database path.");
    sqlite3_close(db);
    pthread_mutex_unlock(&strappy_db_connection_mutex);
    return 0;
  }

  strappy_db_connection = db;
  strappy_db_connection_path = path_copy;
  strappy_db_connection_schema_ready = 0;
  if (stat(db_path, &path_stat) == 0) {
    strappy_db_connection_device = path_stat.st_dev;
    strappy_db_connection_inode = path_stat.st_ino;
    strappy_db_connection_has_identity = 1;
  }
  if (!strappy_db_connection_shutdown_registered) {
    if (atexit(strappy_db_shutdown_connection) == 0) {
      strappy_db_connection_shutdown_registered = 1;
    }
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

static int strappy_db_has_legacy_schema(sqlite3 *db, char **error_out)
{
  sqlite3_stmt *stmt;
  int rc;
  int legacy;

  stmt = NULL;
  legacy = 0;
  rc = sqlite3_prepare_v2(db, "PRAGMA table_info(sessions);", -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not inspect session schema: %s",
                                sqlite3_errmsg(db));
    return -1;
  }
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const unsigned char *name;

    name = sqlite3_column_text(stmt, 1);
    if ((name != NULL) && (strcmp((const char *)name, "prompt") == 0)) {
      legacy = 1;
      break;
    }
  }
  if ((rc != SQLITE_ROW) && (rc != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not inspect session schema: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1;
  }
  sqlite3_finalize(stmt);
  return legacy;
}

static int strappy_db_reset_legacy_schema(sqlite3 *db, char **error_out)
{
  static const char *sql =
    "DROP TABLE IF EXISTS response_api_item_parts;"
    "DROP TABLE IF EXISTS response_tool_executions;"
    "DROP TABLE IF EXISTS response_api_items;"
    "DROP TABLE IF EXISTS response_api_calls;"
    "DROP TABLE IF EXISTS session_messages;"
    "DROP TABLE IF EXISTS session_turns;"
    "DROP TABLE IF EXISTS helper_database_info;"
    "DROP TABLE IF EXISTS helper_user_info;"
    "DROP TABLE IF EXISTS database_access_settings;"
    "DROP TABLE IF EXISTS discovered_databases;"
    "DROP TABLE IF EXISTS openrouter_model_supported_voices;"
    "DROP TABLE IF EXISTS openrouter_model_supported_parameters;"
    "DROP TABLE IF EXISTS openrouter_model_output_modalities;"
    "DROP TABLE IF EXISTS openrouter_model_input_modalities;"
    "DROP TABLE IF EXISTS openrouter_model_settings;"
    "DROP TABLE IF EXISTS openrouter_models;"
    "DROP TABLE IF EXISTS app_settings;"
    "DROP TABLE IF EXISTS sessions;";

  return strappy_db_exec(db,
                         sql,
                         "Could not reset the pre-release database schema",
                         error_out);
}

static int strappy_db_ensure_semantic_schema(sqlite3 *db, char **error_out)
{
  static const char schema_models_sql[] =
    "CREATE TABLE IF NOT EXISTS schema_metadata ("
    "id INTEGER PRIMARY KEY CHECK(id = 1),"
    "schema_name TEXT NOT NULL,"
    "created_at_ms INTEGER NOT NULL"
    ");"
    "INSERT OR IGNORE INTO schema_metadata "
    "(id, schema_name, created_at_ms) VALUES "
    "(1, 'semantic-v1', CAST(strftime('%s','now') AS INTEGER) * 1000);"

    "CREATE TABLE IF NOT EXISTS models ("
    "id TEXT PRIMARY KEY,"
    "canonical_slug TEXT,"
    "hugging_face_id TEXT,"
    "name TEXT NOT NULL,"
    "description TEXT,"
    "context_length INTEGER NOT NULL DEFAULT 0 CHECK(context_length >= 0),"
    "created_at_s INTEGER NOT NULL DEFAULT 0,"
    "architecture_modality TEXT,"
    "architecture_tokenizer TEXT,"
    "architecture_instruct_type TEXT,"
    "provider_context_length INTEGER NOT NULL DEFAULT 0 "
      "CHECK(provider_context_length >= 0),"
    "provider_max_completion_tokens INTEGER NOT NULL DEFAULT 0 "
      "CHECK(provider_max_completion_tokens >= 0),"
    "provider_is_moderated INTEGER NOT NULL DEFAULT 0 "
      "CHECK(provider_is_moderated IN (0,1)),"
    "knowledge_cutoff TEXT,"
    "expiration_date TEXT,"
    "details_url TEXT,"
    "catalog_active INTEGER NOT NULL DEFAULT 1 "
      "CHECK(catalog_active IN (0,1)),"
    "last_seen_at_ms INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS models_name_idx ON models(name, id);"
    "CREATE TABLE IF NOT EXISTS model_prices ("
    "model_id TEXT NOT NULL,"
    "price_kind TEXT NOT NULL,"
    "price_decimal TEXT NOT NULL,"
    "PRIMARY KEY(model_id, price_kind),"
    "FOREIGN KEY(model_id) REFERENCES models(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS model_features ("
    "model_id TEXT NOT NULL,"
    "feature_kind TEXT NOT NULL CHECK(feature_kind IN "
      "('input_modality','output_modality','parameter','voice','reasoning')),"
    "feature_value TEXT NOT NULL,"
    "PRIMARY KEY(model_id, feature_kind, feature_value),"
    "FOREIGN KEY(model_id) REFERENCES models(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS model_preferences ("
    "model_id TEXT PRIMARY KEY,"
    "allowed INTEGER NOT NULL DEFAULT 0 CHECK(allowed IN (0,1)),"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(model_id) REFERENCES models(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS app_preferences ("
    "id INTEGER PRIMARY KEY CHECK(id = 1),"
    "default_model_id TEXT,"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(default_model_id) REFERENCES models(id)"
    ");"
    "INSERT OR IGNORE INTO app_preferences (id, updated_at_ms) VALUES "
      "(1, CAST(strftime('%s','now') AS INTEGER) * 1000);";

  static const char schema_sessions_sql[] =
    "CREATE TABLE IF NOT EXISTS sessions ("
    "id INTEGER PRIMARY KEY,"
    "name TEXT NOT NULL DEFAULT '',"
    "model_id TEXT,"
    "created_at_ms INTEGER NOT NULL,"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(model_id) REFERENCES models(id)"
    ");"
    "CREATE INDEX IF NOT EXISTS sessions_updated_idx "
      "ON sessions(updated_at_ms DESC, id DESC);"
    "CREATE TABLE IF NOT EXISTS session_assistant_sets ("
    "session_id INTEGER PRIMARY KEY,"
    "assistant_set_id TEXT NOT NULL CHECK(length(assistant_set_id) > 0),"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS session_settings ("
    "session_id INTEGER PRIMARY KEY,"
    "web_provider TEXT NOT NULL DEFAULT '"
      STRAPPY_DB_DEFAULT_SESSION_WEB_PROVIDER "' "
      "CHECK(web_provider IN ('none','auto','native','exa','parallel')),"
    "bash_enabled INTEGER NOT NULL DEFAULT 0 "
      "CHECK(bash_enabled IN (0,1)),"
    "streaming_enabled INTEGER NOT NULL DEFAULT 0 "
      "CHECK(streaming_enabled IN (0,1)),"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS session_working_directories ("
    "session_id INTEGER PRIMARY KEY,"
    "working_directory TEXT NOT NULL CHECK(length(working_directory) > 0),"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS turns ("
    "id INTEGER PRIMARY KEY,"
    "session_id INTEGER NOT NULL,"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "prompt_group_key TEXT NOT NULL,"
    "state TEXT NOT NULL DEFAULT 'running' CHECK(state IN "
      "('running','completed','error','cancelled')),"
    "created_at_ms INTEGER NOT NULL,"
    "completed_at_ms INTEGER,"
    "UNIQUE(session_id, ordinal),"
    "UNIQUE(session_id, prompt_group_key),"
    "UNIQUE(id, session_id),"
    "FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE"
    ");"

    "CREATE TABLE IF NOT EXISTS instruction_revisions ("
    "id INTEGER PRIMARY KEY,"
    "digest TEXT NOT NULL UNIQUE,"
    "text TEXT NOT NULL,"
    "created_at_ms INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS toolset_revisions ("
    "id INTEGER PRIMARY KEY,"
    "digest TEXT NOT NULL UNIQUE,"
    "resource_version TEXT,"
    "created_at_ms INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS toolset_members ("
    "toolset_revision_id INTEGER NOT NULL,"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "tool_name TEXT NOT NULL,"
    "definition_digest TEXT,"
    "PRIMARY KEY(toolset_revision_id, ordinal),"
    "UNIQUE(toolset_revision_id, tool_name),"
    "FOREIGN KEY(toolset_revision_id) REFERENCES toolset_revisions(id) "
      "ON DELETE CASCADE"
    ");";

  static const char schema_requests_sql[] =
    "CREATE TABLE IF NOT EXISTS model_requests ("
    "id INTEGER PRIMARY KEY,"
    "turn_id INTEGER NOT NULL,"
    "previous_request_id INTEGER,"
    "round_index INTEGER NOT NULL CHECK(round_index >= 0),"
    "request_kind TEXT NOT NULL,"
    "model_id TEXT NOT NULL,"
    "instruction_revision_id INTEGER,"
    "toolset_revision_id INTEGER,"
    "input_from_sequence INTEGER NOT NULL DEFAULT 1 "
      "CHECK(input_from_sequence >= 1),"
    "input_through_sequence INTEGER NOT NULL DEFAULT 0 "
      "CHECK(input_through_sequence >= 0),"
    "new_input_from_sequence INTEGER,"
    "max_output_tokens INTEGER,"
    "temperature_millionths INTEGER,"
    "web_provider TEXT NOT NULL DEFAULT 'none' "
      "CHECK(web_provider IN ('none','auto','native','exa','parallel')),"
    "stream_enabled INTEGER NOT NULL DEFAULT 0 CHECK(stream_enabled IN (0,1)),"
    "reasoning_enabled INTEGER NOT NULL DEFAULT 1 "
      "CHECK(reasoning_enabled IN (0,1)),"
    "reasoning_summary TEXT,"
    "tool_calls_enabled INTEGER NOT NULL DEFAULT 1 "
      "CHECK(tool_calls_enabled IN (0,1)),"
    "state TEXT NOT NULL DEFAULT 'pending' CHECK(state IN "
      "('pending','running','completed','error','cancelled')),"
    "created_at_ms INTEGER NOT NULL,"
    "completed_at_ms INTEGER,"
    "UNIQUE(turn_id, round_index),"
    "FOREIGN KEY(turn_id) REFERENCES turns(id) ON DELETE CASCADE,"
    "FOREIGN KEY(previous_request_id) REFERENCES model_requests(id),"
    "FOREIGN KEY(model_id) REFERENCES models(id),"
    "FOREIGN KEY(instruction_revision_id) REFERENCES instruction_revisions(id),"
    "FOREIGN KEY(toolset_revision_id) REFERENCES toolset_revisions(id)"
    ");"
    "CREATE INDEX IF NOT EXISTS model_requests_turn_idx "
      "ON model_requests(turn_id, round_index);";

  static const char schema_attempts_sql[] =
    "CREATE TABLE IF NOT EXISTS http_attempts ("
    "id INTEGER PRIMARY KEY,"
    "request_id INTEGER NOT NULL,"
    "previous_attempt_id INTEGER,"
    "attempt_index INTEGER NOT NULL CHECK(attempt_index >= 0),"
    "state TEXT NOT NULL DEFAULT 'pending' CHECK(state IN "
      "('pending','running','completed','http_error','response_error',"
       "'transport_error','parse_error','cancelled')),"
    "method TEXT NOT NULL,"
    "endpoint TEXT NOT NULL,"
    "started_at_ms INTEGER NOT NULL,"
    "completed_at_ms INTEGER,"
    "http_status INTEGER NOT NULL DEFAULT 0,"
    "curl_code INTEGER NOT NULL DEFAULT 0,"
    "retry_after_seconds INTEGER NOT NULL DEFAULT 0,"
    "request_bytes INTEGER NOT NULL DEFAULT 0,"
    "response_bytes INTEGER NOT NULL DEFAULT 0,"
    "name_lookup_us INTEGER NOT NULL DEFAULT 0,"
    "connect_us INTEGER NOT NULL DEFAULT 0,"
    "start_transfer_us INTEGER NOT NULL DEFAULT 0,"
    "total_us INTEGER NOT NULL DEFAULT 0,"
    "effective_url TEXT,"
    "transport_error TEXT,"
    "content_type TEXT,"
    "provider_request_id TEXT,"
    "provider_generation_id TEXT,"
    "rate_limit_limit TEXT,"
    "rate_limit_remaining TEXT,"
    "rate_limit_reset TEXT,"
    "rate_limit_requests_limit TEXT,"
    "rate_limit_requests_remaining TEXT,"
    "rate_limit_requests_reset TEXT,"
    "rate_limit_tokens_limit TEXT,"
    "rate_limit_tokens_remaining TEXT,"
    "rate_limit_tokens_reset TEXT,"
    "UNIQUE(request_id, attempt_index),"
    "FOREIGN KEY(request_id) REFERENCES model_requests(id) ON DELETE CASCADE,"
    "FOREIGN KEY(previous_attempt_id) REFERENCES http_attempts(id)"
    ");"
    "CREATE INDEX IF NOT EXISTS http_attempts_request_idx "
      "ON http_attempts(request_id, attempt_index);"
    "CREATE TABLE IF NOT EXISTS api_results ("
    "attempt_id INTEGER PRIMARY KEY,"
    "provider_response_id TEXT,"
    "provider_model_id TEXT,"
    "provider_status TEXT,"
    "provider_created_at_s INTEGER,"
    "provider_completed_at_s INTEGER,"
    "incomplete_reason TEXT,"
    "error_type TEXT,"
    "error_code TEXT,"
    "error_message TEXT,"
    "error_parameter TEXT,"
    "parse_error TEXT,"
    "FOREIGN KEY(attempt_id) REFERENCES http_attempts(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS api_usage ("
    "attempt_id INTEGER PRIMARY KEY,"
    "input_tokens INTEGER NOT NULL DEFAULT 0,"
    "cached_input_tokens INTEGER NOT NULL DEFAULT 0,"
    "output_tokens INTEGER NOT NULL DEFAULT 0,"
    "reasoning_tokens INTEGER NOT NULL DEFAULT 0,"
    "total_tokens INTEGER NOT NULL DEFAULT 0,"
    "cost_nano_usd INTEGER,"
    "upstream_cost_nano_usd INTEGER,"
    "upstream_input_cost_nano_usd INTEGER,"
    "upstream_output_cost_nano_usd INTEGER,"
    "is_byok INTEGER NOT NULL DEFAULT 0 CHECK(is_byok IN (0,1)),"
    "FOREIGN KEY(attempt_id) REFERENCES api_results(attempt_id) ON DELETE CASCADE"
    ");";

  static const char schema_conversation_sql[] =
    "CREATE TABLE IF NOT EXISTS conversation_items ("
    "id INTEGER PRIMARY KEY,"
    "session_id INTEGER NOT NULL,"
    "turn_id INTEGER NOT NULL,"
    "sequence INTEGER NOT NULL CHECK(sequence >= 1),"
    "kind TEXT NOT NULL CHECK(kind IN "
      "('message','reasoning','function_call','function_call_output',"
       "'openrouter:web_search','openrouter:web_fetch')),"
    "introduced_request_id INTEGER,"
    "source_attempt_id INTEGER,"
    "source_item_index INTEGER NOT NULL DEFAULT 0 CHECK(source_item_index >= 0),"
    "provider_item_id TEXT,"
    "provider_status TEXT,"
    "include_in_context INTEGER NOT NULL DEFAULT 1 "
      "CHECK(include_in_context IN (0,1)),"
    "timeline_visible INTEGER NOT NULL DEFAULT 1 "
      "CHECK(timeline_visible IN (0,1)),"
    "is_error INTEGER NOT NULL DEFAULT 0 CHECK(is_error IN (0,1)),"
    "created_at_ms INTEGER NOT NULL,"
    "UNIQUE(session_id, sequence),"
    "UNIQUE(source_attempt_id, source_item_index),"
    "FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE,"
    "FOREIGN KEY(turn_id, session_id) REFERENCES turns(id, session_id) "
      "ON DELETE CASCADE,"
    "FOREIGN KEY(introduced_request_id) REFERENCES model_requests(id) "
      "ON DELETE CASCADE,"
    "FOREIGN KEY(source_attempt_id) REFERENCES http_attempts(id) "
      "ON DELETE CASCADE"
    ");"
    "CREATE INDEX IF NOT EXISTS conversation_items_context_idx "
      "ON conversation_items(session_id, include_in_context, sequence);"
    "CREATE INDEX IF NOT EXISTS conversation_items_request_idx "
      "ON conversation_items(introduced_request_id, source_item_index);"
    "CREATE INDEX IF NOT EXISTS conversation_items_attempt_idx "
      "ON conversation_items(source_attempt_id, source_item_index);"
    "CREATE TABLE IF NOT EXISTS message_items ("
    "item_id INTEGER PRIMARY KEY,"
    "role TEXT NOT NULL CHECK(role IN ('user','assistant','developer','system')),"
    "phase TEXT,"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS item_text_parts ("
    "id INTEGER PRIMARY KEY,"
    "item_id INTEGER NOT NULL,"
    "collection_name TEXT NOT NULL CHECK(collection_name IN ('content','summary')),"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "part_type TEXT NOT NULL CHECK(part_type IN "
      "('input_text','output_text','refusal','reasoning_text','summary_text')),"
    "text TEXT NOT NULL,"
    "UNIQUE(item_id, collection_name, ordinal),"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS item_citations ("
    "id INTEGER PRIMARY KEY,"
    "text_part_id INTEGER NOT NULL,"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "citation_type TEXT NOT NULL,"
    "start_offset INTEGER,"
    "end_offset INTEGER,"
    "title TEXT,"
    "url TEXT,"
    "excerpt TEXT,"
    "UNIQUE(text_part_id, ordinal),"
    "FOREIGN KEY(text_part_id) REFERENCES item_text_parts(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS reasoning_items ("
    "item_id INTEGER PRIMARY KEY,"
    "encrypted_content TEXT,"
    "provider_format TEXT,"
    "provider_signature TEXT,"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE"
    ");";

  static const char schema_tool_items_sql[] =
    "CREATE TABLE IF NOT EXISTS function_calls ("
    "item_id INTEGER PRIMARY KEY,"
    "provider_call_id TEXT NOT NULL UNIQUE,"
    "tool_name TEXT NOT NULL,"
    "tool_namespace TEXT,"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS function_outputs ("
    "item_id INTEGER PRIMARY KEY,"
    "function_call_item_id INTEGER NOT NULL UNIQUE,"
    "execution_state TEXT NOT NULL DEFAULT 'completed' CHECK(execution_state IN "
      "('pending','running','completed','error','cancelled')),"
    "started_at_ms INTEGER,"
    "completed_at_ms INTEGER,"
    "output_format TEXT NOT NULL DEFAULT 'text' "
      "CHECK(output_format IN ('text','structured')),"
    "text_output TEXT,"
    "error_code TEXT,"
    "error_message TEXT,"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE,"
    "FOREIGN KEY(function_call_item_id) REFERENCES function_calls(item_id)"
    ");"
    "CREATE TABLE IF NOT EXISTS tool_executions ("
    "id INTEGER PRIMARY KEY,"
    "function_call_item_id INTEGER NOT NULL UNIQUE,"
    "response_attempt_id INTEGER NOT NULL,"
    "output_index INTEGER NOT NULL CHECK(output_index >= 0),"
    "state TEXT NOT NULL CHECK(state IN "
      "('pending','running','completed','error','cancelled')),"
    "started_at_ms INTEGER,"
    "completed_at_ms INTEGER,"
    "error_code TEXT,"
    "error_message TEXT,"
    "FOREIGN KEY(function_call_item_id) REFERENCES function_calls(item_id) "
      "ON DELETE CASCADE,"
    "FOREIGN KEY(response_attempt_id) REFERENCES http_attempts(id) "
      "ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS web_searches ("
    "item_id INTEGER PRIMARY KEY,"
    "action_type TEXT NOT NULL,"
    "query TEXT,"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS web_search_sources ("
    "web_search_item_id INTEGER NOT NULL,"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "source_type TEXT NOT NULL,"
    "url TEXT NOT NULL,"
    "PRIMARY KEY(web_search_item_id, ordinal),"
    "FOREIGN KEY(web_search_item_id) REFERENCES web_searches(item_id) "
      "ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS web_fetches ("
    "item_id INTEGER PRIMARY KEY,"
    "url TEXT NOT NULL,"
    "title TEXT,"
    "content TEXT,"
    "http_status INTEGER,"
    "FOREIGN KEY(item_id) REFERENCES conversation_items(id) ON DELETE CASCADE"
    ");";

  static const char schema_answer_quality_sql[] =
    "CREATE TABLE IF NOT EXISTS answer_quality_audits ("
    "id INTEGER PRIMARY KEY,"
    "response_attempt_id INTEGER NOT NULL UNIQUE,"
    "outcome TEXT NOT NULL CHECK(outcome IN ('passed','failed','error')),"
    "guidance_version TEXT,"
    "evaluated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(response_attempt_id) REFERENCES http_attempts(id) "
      "ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS answer_quality_checks ("
    "audit_id INTEGER NOT NULL,"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "check_key TEXT NOT NULL,"
    "check_kind TEXT NOT NULL CHECK(check_kind IN "
      "('required_tool','answer_content')),"
    "label TEXT NOT NULL,"
    "status TEXT NOT NULL CHECK(status IN "
      "('passed','failed','not_applicable','error')),"
    "tool_name TEXT,"
    "detail TEXT,"
    "evidence_item_id INTEGER,"
    "PRIMARY KEY(audit_id, ordinal),"
    "UNIQUE(audit_id, check_key),"
    "FOREIGN KEY(audit_id) REFERENCES answer_quality_audits(id) "
      "ON DELETE CASCADE,"
    "FOREIGN KEY(evidence_item_id) REFERENCES conversation_items(id) "
      "ON DELETE SET NULL"
    ");";

  static const char schema_structured_sql[] =
    "CREATE TABLE IF NOT EXISTS structured_documents ("
    "id INTEGER PRIMARY KEY,"
    "owner_item_id INTEGER NOT NULL,"
    "purpose TEXT NOT NULL CHECK(purpose IN ('arguments','output')),"
    "UNIQUE(owner_item_id, purpose),"
    "FOREIGN KEY(owner_item_id) REFERENCES conversation_items(id) "
      "ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS structured_nodes ("
    "document_id INTEGER NOT NULL,"
    "node_id INTEGER NOT NULL CHECK(node_id >= 0),"
    "parent_node_id INTEGER,"
    "ordinal INTEGER NOT NULL CHECK(ordinal >= 0),"
    "member_name TEXT,"
    "value_type TEXT NOT NULL CHECK(value_type IN "
      "('object','array','string','number','boolean','null')),"
    "text_value TEXT,"
    "number_value TEXT,"
    "boolean_value INTEGER CHECK(boolean_value IN (0,1)),"
    "PRIMARY KEY(document_id, node_id),"
    "UNIQUE(document_id, parent_node_id, ordinal),"
    "CHECK((node_id = 0 AND parent_node_id IS NULL) OR "
      "(node_id > 0 AND parent_node_id IS NOT NULL)),"
    "FOREIGN KEY(document_id) REFERENCES structured_documents(id) "
      "ON DELETE CASCADE,"
    "FOREIGN KEY(document_id, parent_node_id) "
      "REFERENCES structured_nodes(document_id, node_id) ON DELETE CASCADE"
    ");";

  static const char schema_catalog_sql[] =
    "CREATE TABLE IF NOT EXISTS applications ("
    "id INTEGER PRIMARY KEY,"
    "stable_key TEXT UNIQUE,"
    "name TEXT,"
    "bundle_id TEXT,"
    "container_path TEXT,"
    "bundle_path TEXT,"
    "source TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS scan_roots ("
    "id INTEGER PRIMARY KEY,"
    "path TEXT NOT NULL UNIQUE,"
    "kind TEXT NOT NULL DEFAULT 'filesystem',"
    "last_started_at_ms INTEGER,"
    "last_completed_at_ms INTEGER"
    ");"
    "CREATE TABLE IF NOT EXISTS scan_runs ("
    "id INTEGER PRIMARY KEY,"
    "scan_root_id INTEGER NOT NULL,"
    "state TEXT NOT NULL CHECK(state IN ('running','completed','error','cancelled')),"
    "started_at_ms INTEGER NOT NULL,"
    "completed_at_ms INTEGER,"
    "candidate_count INTEGER NOT NULL DEFAULT 0,"
    "database_count INTEGER NOT NULL DEFAULT 0,"
    "error_message TEXT,"
    "FOREIGN KEY(scan_root_id) REFERENCES scan_roots(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS databases ("
    "id INTEGER PRIMARY KEY,"
    "assistant_database_id TEXT UNIQUE,"
    "stable_key TEXT NOT NULL UNIQUE,"
    "application_id INTEGER,"
    "display_name TEXT,"
    "origin_kind TEXT NOT NULL DEFAULT 'other',"
    "location_tail TEXT,"
    "first_seen_at_ms INTEGER NOT NULL,"
    "last_seen_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(application_id) REFERENCES applications(id)"
    ");"
    "CREATE TABLE IF NOT EXISTS database_locations ("
    "id INTEGER PRIMARY KEY,"
    "database_id INTEGER NOT NULL,"
    "path TEXT NOT NULL UNIQUE,"
    "scan_root_id INTEGER,"
    "last_scan_run_id INTEGER,"
    "device INTEGER NOT NULL DEFAULT 0,"
    "inode INTEGER NOT NULL DEFAULT 0,"
    "size_bytes INTEGER NOT NULL DEFAULT 0,"
    "modified_at_s INTEGER NOT NULL DEFAULT 0,"
    "validation_state TEXT NOT NULL DEFAULT 'candidate' CHECK(validation_state IN "
      "('candidate','valid','invalid')),"
    "validation_error TEXT,"
    "active INTEGER NOT NULL DEFAULT 1 CHECK(active IN (0,1)),"
    "first_seen_at_ms INTEGER NOT NULL,"
    "last_seen_at_ms INTEGER NOT NULL,"
    "last_scanned_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(database_id) REFERENCES databases(id) ON DELETE CASCADE,"
    "FOREIGN KEY(scan_root_id) REFERENCES scan_roots(id),"
    "FOREIGN KEY(last_scan_run_id) REFERENCES scan_runs(id)"
    ");"
    "CREATE INDEX IF NOT EXISTS database_locations_device_inode_idx "
      "ON database_locations(device, inode);"
    "CREATE INDEX IF NOT EXISTS database_locations_database_idx "
      "ON database_locations(database_id, active, path);"
    "CREATE TABLE IF NOT EXISTS database_permissions ("
    "database_id INTEGER PRIMARY KEY,"
    "decision TEXT NOT NULL DEFAULT 'unknown' "
      "CHECK(decision IN ('unknown','allowed','denied')),"
    "hidden INTEGER NOT NULL DEFAULT 0 CHECK(hidden IN (0,1)),"
    "updated_at_ms INTEGER NOT NULL,"
    "FOREIGN KEY(database_id) REFERENCES databases(id) ON DELETE CASCADE"
    ");"
    "CREATE INDEX IF NOT EXISTS database_permissions_decision_idx "
      "ON database_permissions(decision, hidden, database_id);";

  static const char schema_memory_sql[] =
    "CREATE TABLE IF NOT EXISTS user_facts ("
    "id INTEGER PRIMARY KEY,"
    "assistant_set_id TEXT NOT NULL CHECK(length(assistant_set_id) > 0),"
    "kind TEXT NOT NULL,"
    "subject TEXT,"
    "predicate TEXT,"
    "value TEXT NOT NULL,"
    "confidence_basis_points INTEGER NOT NULL DEFAULT 10000 "
      "CHECK(confidence_basis_points BETWEEN 0 AND 10000),"
    "source_item_id INTEGER,"
    "created_at_ms INTEGER NOT NULL,"
    "updated_at_ms INTEGER NOT NULL,"
    "last_used_at_ms INTEGER,"
    "FOREIGN KEY(source_item_id) REFERENCES conversation_items(id) "
      "ON DELETE SET NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS user_facts_assistant_set_kind_idx "
      "ON user_facts(assistant_set_id, kind, updated_at_ms DESC, id DESC);"
    "CREATE TABLE IF NOT EXISTS database_hints ("
    "id INTEGER PRIMARY KEY,"
    "database_id INTEGER NOT NULL,"
    "kind TEXT NOT NULL,"
    "content TEXT NOT NULL,"
    "observed_modified_at_s INTEGER NOT NULL DEFAULT 0,"
    "source_item_id INTEGER,"
    "created_at_ms INTEGER NOT NULL,"
    "updated_at_ms INTEGER NOT NULL,"
    "last_used_at_ms INTEGER,"
    "FOREIGN KEY(database_id) REFERENCES databases(id) ON DELETE CASCADE,"
    "FOREIGN KEY(source_item_id) REFERENCES conversation_items(id) "
      "ON DELETE SET NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS database_hints_database_idx "
      "ON database_hints(database_id, kind, updated_at_ms DESC);"

    "PRAGMA user_version = 1;";
  static const char *const schema_sql[] = {
    schema_models_sql,
    schema_sessions_sql,
    schema_requests_sql,
    schema_attempts_sql,
    schema_conversation_sql,
    schema_tool_items_sql,
    schema_answer_quality_sql,
    schema_structured_sql,
    schema_catalog_sql,
    schema_memory_sql
  };
  size_t schema_index;
  int legacy;

  legacy = strappy_db_has_legacy_schema(db, error_out);
  if (legacy < 0) {
    return 0;
  }
  if (legacy && !strappy_db_reset_legacy_schema(db, error_out)) {
    return 0;
  }
  for (schema_index = 0U;
       schema_index < (sizeof(schema_sql) / sizeof(schema_sql[0]));
       schema_index++) {
    if (!strappy_db_exec(db,
                         schema_sql[schema_index],
                         "Could not create semantic database schema",
                         error_out)) {
      return 0;
    }
  }
  if (!strappy_db_exec(db,
                       STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL,
                       "Could not create built-in default model",
                       error_out)) {
    return 0;
  }
  return strappy_db_exec(db,
                         "PRAGMA foreign_keys = ON;",
                         "Could not enable foreign keys",
                         error_out);
}

static int strappy_db_ensure_schema(sqlite3 *db, char **error_out)
{
  int ok;

  if ((db == strappy_db_connection) &&
      strappy_db_connection_schema_ready) {
    return 1;
  }
  ok = strappy_db_ensure_semantic_schema(db, error_out);
  if (ok && (db == strappy_db_connection)) {
    strappy_db_connection_schema_ready = 1;
  }
  return ok;

  static const char *sessions_sql =
    "CREATE TABLE IF NOT EXISTS sessions ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "name TEXT NOT NULL DEFAULT '',"
    "prompt TEXT NOT NULL,"
    "response TEXT NOT NULL,"
    "model TEXT,"
    "http_status INTEGER NOT NULL DEFAULT 0,"
    "web_provider TEXT NOT NULL DEFAULT 'none',"
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
  const unsigned char *web_provider;
  char *name;
  char *prompt;
  char *response;
  char *model;
  char *model_name;
  char *assistant_set_id;
  char *created_at;
  char *last_activity_at;

  if ((record == NULL) || (stmt == NULL)) {
    strappy_set_error(error_out, "Session row request is incomplete.");
    return 0;
  }

  strappy_session_record_destroy(record);
  record->session_id = (long long)sqlite3_column_int64(stmt, 0);
  record->http_status = (long)sqlite3_column_int64(stmt, 6);
  record->last_activity_at_ms = (long long)sqlite3_column_int64(stmt, 9);
  web_provider = sqlite3_column_text(stmt, 10);
  if ((web_provider == NULL) ||
      !strappy_web_provider_parse((const char *)web_provider,
                                  &record->web_provider)) {
    strappy_set_error(error_out, "Session web provider is invalid.");
    return 0;
  }
  record->bash_enabled = sqlite3_column_int(stmt, 11) ? 1 : 0;
  record->streaming_enabled = sqlite3_column_int(stmt, 12) ? 1 : 0;

  name = strappy_db_column_string(stmt, 1);
  prompt = strappy_db_column_string(stmt, 2);
  response = strappy_db_column_string(stmt, 3);
  model = strappy_db_column_string(stmt, 4);
  model_name = strappy_db_column_string(stmt, 5);
  assistant_set_id = strappy_db_column_string(stmt, 13);
  created_at = strappy_db_column_string(stmt, 7);
  last_activity_at = strappy_db_column_string(stmt, 8);

  if ((name == NULL) || (prompt == NULL) || (response == NULL) ||
      (model == NULL) || (model_name == NULL) || (created_at == NULL) ||
      (last_activity_at == NULL) || (assistant_set_id == NULL)) {
    free(name);
    free(prompt);
    free(response);
    free(model);
    free(model_name);
    free(assistant_set_id);
    free(created_at);
    free(last_activity_at);
    strappy_set_error(error_out, "Could not allocate session row.");
    return 0;
  }

  record->name = name;
  record->prompt = prompt;
  record->response = response;
  record->model = model;
  record->model_name = model_name;
  record->assistant_set_id = assistant_set_id;
  record->created_at = created_at;
  record->last_activity_at = last_activity_at;
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

static long long strappy_db_now_ms(void)
{
  sqlite3_int64 value;

  value = (sqlite3_int64)time(NULL);
  return ((long long)value) * 1000LL;
}

static char *strappy_db_catalog_stable_key(
  const strappy_discovered_database_input *record)
{
  const char *prefix;
  const char *first;
  const char *second;
  size_t length;
  char *key;

  if ((record == NULL) || (record->path == NULL)) {
    return NULL;
  }
  if ((record->app_group_key != NULL) &&
      (record->app_group_key[0] != '\0') &&
      (record->location_tail != NULL) &&
      (record->location_tail[0] != '\0')) {
    prefix = "app:";
    first = record->app_group_key;
    second = record->location_tail;
  } else {
    prefix = "path:";
    first = record->path;
    second = "";
  }
  length = strlen(prefix) + strlen(first) + strlen(second) + 2U;
  key = (char *)malloc(length);
  if (key == NULL) {
    return NULL;
  }
  if (second[0] != '\0') {
    snprintf(key, length, "%s%s:%s", prefix, first, second);
  } else {
    snprintf(key, length, "%s%s", prefix, first);
  }
  return key;
}

static int strappy_db_catalog_application_id(
  sqlite3 *db,
  const strappy_discovered_database_input *record,
  long long now_ms,
  long long *application_id_out,
  char **error_out)
{
  static const char *insert_sql =
    "INSERT OR IGNORE INTO applications "
    "(stable_key, name, bundle_id, container_path, bundle_path, source) "
    "VALUES (?, ?, ?, ?, ?, ?);";
  static const char *update_sql =
    "UPDATE applications SET "
    "name = COALESCE(?, name), bundle_id = COALESCE(?, bundle_id), "
    "container_path = COALESCE(?, container_path), "
    "bundle_path = COALESCE(?, bundle_path), source = COALESCE(?, source) "
    "WHERE stable_key = ?;";
  static const char *select_sql =
    "SELECT id FROM applications WHERE stable_key = ?;";
  sqlite3_stmt *stmt;
  const char *stable_key;
  int rc;
  int ok;

  (void)now_ms;
  if (application_id_out == NULL) {
    strappy_set_error(error_out, "Application catalog output is missing.");
    return 0;
  }
  *application_id_out = 0LL;
  stable_key = ((record != NULL) && (record->app_group_key != NULL) &&
                (record->app_group_key[0] != '\0')) ?
    record->app_group_key : NULL;
  if (stable_key == NULL) {
    return 1;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare application insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  ok = (sqlite3_bind_text(stmt, 1, stable_key, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
       strappy_db_bind_optional_text(db, stmt, 2, record->app_name,
                                     "Could not bind application insert",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 3, record->app_bundle_id,
                                     "Could not bind application insert",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 4, record->app_container_path,
                                     "Could not bind application insert",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 5, record->app_bundle_path,
                                     "Could not bind application insert",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 6, record->app_source,
                                     "Could not bind application insert",
                                     error_out);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    if (ok) {
      strappy_set_formatted_error(error_out,
                                  "Could not save application: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare application update: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  ok = strappy_db_bind_optional_text(db, stmt, 1, record->app_name,
                                     "Could not bind application update",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 2, record->app_bundle_id,
                                     "Could not bind application update",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 3, record->app_container_path,
                                     "Could not bind application update",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 4, record->app_bundle_path,
                                     "Could not bind application update",
                                     error_out) &&
       strappy_db_bind_optional_text(db, stmt, 5, record->app_source,
                                     "Could not bind application update",
                                     error_out) &&
       (sqlite3_bind_text(stmt, 6, stable_key, -1, SQLITE_TRANSIENT) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    if (ok) {
      strappy_set_formatted_error(error_out,
                                  "Could not update application: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, stable_key, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare application lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read application: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  *application_id_out = (long long)sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_catalog_scan_root_id(sqlite3 *db,
                                           const char *path,
                                           long long now_ms,
                                           long long *root_id_out,
                                           char **error_out)
{
  static const char *insert_sql =
    "INSERT OR IGNORE INTO scan_roots (path) VALUES (?);";
  static const char *select_sql =
    "SELECT id FROM scan_roots WHERE path = ?;";
  sqlite3_stmt *stmt;
  int rc;

  (void)now_ms;
  if (root_id_out == NULL) {
    strappy_set_error(error_out, "Scan-root output is missing.");
    return 0;
  }
  *root_id_out = 0LL;
  if ((path == NULL) || (path[0] == '\0')) {
    return 1;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save scan root: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_ROW)) {
    strappy_set_formatted_error(error_out,
                                "Could not read scan root: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  *root_id_out = (long long)sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_catalog_save_one(sqlite3 *db,
                                       const strappy_discovered_database_input *record,
                                       long long scan_run_id,
                                       long long now_ms,
                                       char **error_out)
{
  static const char *find_path_sql =
    "SELECT database_id FROM database_locations WHERE path = ?;";
  static const char *find_stable_sql =
    "SELECT id FROM databases WHERE stable_key = ?;";
  static const char *insert_database_sql =
    "INSERT INTO databases "
    "(stable_key, application_id, display_name, origin_kind, location_tail, "
     "first_seen_at_ms, last_seen_at_ms) VALUES (?, ?, ?, ?, ?, ?, ?);";
  static const char *update_database_sql =
    "UPDATE databases SET application_id = COALESCE(?, application_id), "
    "display_name = COALESCE(?, display_name), origin_kind = ?, "
    "location_tail = COALESCE(?, location_tail), last_seen_at_ms = ? "
    "WHERE id = ?;";
  static const char *assistant_id_sql =
    "UPDATE databases SET assistant_database_id = ? "
    "WHERE id = ? AND assistant_database_id IS NULL;";
  static const char *permission_sql =
    "INSERT OR IGNORE INTO database_permissions "
    "(database_id, decision, hidden, updated_at_ms) "
    "VALUES (?, 'unknown', ?, ?);";
  static const char *insert_location_sql =
    "INSERT OR IGNORE INTO database_locations "
    "(database_id, path, scan_root_id, last_scan_run_id, device, inode, "
     "size_bytes, modified_at_s, validation_state, validation_error, active, "
     "first_seen_at_ms, last_seen_at_ms, last_scanned_at_ms) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?, ?);";
  static const char *update_location_sql =
    "UPDATE database_locations SET database_id = ?, scan_root_id = ?, "
    "last_scan_run_id = COALESCE(?, last_scan_run_id), device = ?, inode = ?, "
    "size_bytes = ?, "
    "modified_at_s = ?, validation_state = ?, validation_error = ?, active = 1, "
    "last_seen_at_ms = ?, last_scanned_at_ms = ? WHERE path = ?;";
  static const char *count_scan_record_sql =
    "UPDATE scan_runs SET candidate_count = candidate_count + 1, "
    "database_count = database_count + ? "
    "WHERE id = ? AND scan_root_id = ? AND state = 'running';";
  sqlite3_stmt *stmt;
  char *stable_key;
  char *assistant_id;
  const char *validation_state;
  long long application_id;
  long long scan_root_id;
  long long database_id;
  int rc;
  int ok;

  if ((record == NULL) || (record->path == NULL) || (record->path[0] == '\0')) {
    strappy_set_error(error_out, "Discovered database path is empty.");
    return 0;
  }
  stable_key = strappy_db_catalog_stable_key(record);
  if (stable_key == NULL) {
    strappy_set_error(error_out, "Could not allocate database stable key.");
    return 0;
  }
  if (!strappy_db_catalog_application_id(db,
                                         record,
                                         now_ms,
                                         &application_id,
                                         error_out) ||
      !strappy_db_catalog_scan_root_id(db,
                                      record->scan_root,
                                      now_ms,
                                      &scan_root_id,
                                      error_out)) {
    free(stable_key);
    return 0;
  }

  database_id = 0LL;
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, find_path_sql, -1, &stmt, NULL);
  if ((rc == SQLITE_OK) &&
      (sqlite3_bind_text(stmt, 1, record->path, -1, SQLITE_TRANSIENT) == SQLITE_OK)) {
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      database_id = (long long)sqlite3_column_int64(stmt, 0);
    }
  }
  if ((rc != SQLITE_ROW) && (rc != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not find database location: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(stable_key);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (database_id == 0LL) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, find_stable_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 1, stable_key, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare database lookup: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      free(stable_key);
      return 0;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      database_id = (long long)sqlite3_column_int64(stmt, 0);
    } else if (rc != SQLITE_DONE) {
      strappy_set_formatted_error(error_out,
                                  "Could not read database lookup: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      free(stable_key);
      return 0;
    }
    sqlite3_finalize(stmt);
  }

  if (database_id == 0LL) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, insert_database_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare database insert: %s",
                                  sqlite3_errmsg(db));
      free(stable_key);
      return 0;
    }
    ok = (sqlite3_bind_text(stmt, 1, stable_key, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
         ((application_id > 0LL) ?
            (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)application_id) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 2) == SQLITE_OK)) &&
         strappy_db_bind_optional_text(db, stmt, 3, record->app_name,
                                       "Could not bind database insert", error_out) &&
         (sqlite3_bind_text(stmt, 4,
                            strappy_db_origin_kind_for_input(record),
                            -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
         strappy_db_bind_optional_text(db, stmt, 5, record->location_tail,
                                       "Could not bind database insert", error_out) &&
         (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)now_ms) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 7, (sqlite3_int64)now_ms) == SQLITE_OK);
    if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
      if (ok) {
        strappy_set_formatted_error(error_out,
                                    "Could not save database: %s",
                                    sqlite3_errmsg(db));
      }
      sqlite3_finalize(stmt);
      free(stable_key);
      return 0;
    }
    database_id = (long long)sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
  } else {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, update_database_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare database update: %s",
                                  sqlite3_errmsg(db));
      free(stable_key);
      return 0;
    }
    ok = ((application_id > 0LL) ?
            (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)application_id) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 1) == SQLITE_OK)) &&
         strappy_db_bind_optional_text(db, stmt, 2, record->app_name,
                                       "Could not bind database update", error_out) &&
         (sqlite3_bind_text(stmt, 3,
                            strappy_db_origin_kind_for_input(record),
                            -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
         strappy_db_bind_optional_text(db, stmt, 4, record->location_tail,
                                       "Could not bind database update", error_out) &&
         (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)now_ms) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)database_id) == SQLITE_OK);
    if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
      if (ok) {
        strappy_set_formatted_error(error_out,
                                    "Could not update database: %s",
                                    sqlite3_errmsg(db));
      }
      sqlite3_finalize(stmt);
      free(stable_key);
      return 0;
    }
    sqlite3_finalize(stmt);
  }

  assistant_id = strappy_db_create_assistant_database_id(database_id);
  if (assistant_id == NULL) {
    free(stable_key);
    strappy_set_error(error_out, "Could not allocate assistant database id.");
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, assistant_id_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, assistant_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)database_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save assistant database id: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(assistant_id);
    free(stable_key);
    return 0;
  }
  sqlite3_finalize(stmt);
  free(assistant_id);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, permission_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)database_id) != SQLITE_OK) ||
      (sqlite3_bind_int(stmt, 2, record->hidden ? 1 : 0) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save database permission: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    free(stable_key);
    return 0;
  }
  sqlite3_finalize(stmt);

  validation_state = record->is_valid_sqlite ? "valid" :
    (((record->validation_error != NULL) &&
      (record->validation_error[0] != '\0')) ? "invalid" : "candidate");
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_location_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database location insert: %s",
                                sqlite3_errmsg(db));
    free(stable_key);
    return 0;
  }
  ok = (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)database_id) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 2, record->path, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
       ((scan_root_id > 0LL) ?
          (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)scan_root_id) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 3) == SQLITE_OK)) &&
       ((scan_run_id > 0LL) ?
          (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)scan_run_id) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 4) == SQLITE_OK)) &&
       (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)record->device) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)record->inode) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 7, (sqlite3_int64)record->size) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 8, (sqlite3_int64)record->modified_at) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 9, validation_state, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
       strappy_db_bind_optional_text(db, stmt, 10, record->validation_error,
                                     "Could not bind database location insert",
                                     error_out) &&
       (sqlite3_bind_int64(stmt, 11, (sqlite3_int64)now_ms) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 12, (sqlite3_int64)now_ms) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 13, (sqlite3_int64)now_ms) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    if (ok) {
      strappy_set_formatted_error(error_out,
                                  "Could not save database location: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    free(stable_key);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_location_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database location update: %s",
                                sqlite3_errmsg(db));
    free(stable_key);
    return 0;
  }
  ok = (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)database_id) == SQLITE_OK) &&
       ((scan_root_id > 0LL) ?
          (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)scan_root_id) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 2) == SQLITE_OK)) &&
       ((scan_run_id > 0LL) ?
          (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)scan_run_id) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 3) == SQLITE_OK)) &&
       (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)record->device) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)record->inode) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)record->size) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 7, (sqlite3_int64)record->modified_at) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 8, validation_state, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
       strappy_db_bind_optional_text(db, stmt, 9, record->validation_error,
                                     "Could not bind database location update",
                                     error_out) &&
       (sqlite3_bind_int64(stmt, 10, (sqlite3_int64)now_ms) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 11, (sqlite3_int64)now_ms) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 12, record->path, -1, SQLITE_TRANSIENT) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    if (ok) {
      strappy_set_formatted_error(error_out,
                                  "Could not update database location: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    free(stable_key);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (scan_run_id > 0LL) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, count_scan_record_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int(stmt, 1, record->is_valid_sqlite ? 1 : 0) !=
         SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)scan_run_id) !=
         SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)scan_root_id) !=
         SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_DONE) ||
        (sqlite3_changes(db) != 1)) {
      strappy_set_formatted_error(error_out,
                                  "Could not count scan record: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      free(stable_key);
      return 0;
    }
    sqlite3_finalize(stmt);
  }
  free(stable_key);
  return 1;
}

static int strappy_db_catalog_begin_scan(sqlite3 *db,
                                         const char *scan_root,
                                         long long now_ms,
                                         long long *scan_run_id_out,
                                         char **error_out)
{
  static const char *cancel_running_sql =
    "UPDATE scan_runs SET state = 'cancelled', completed_at_ms = ?, "
    "error_message = COALESCE(error_message, 'Superseded by a newer scan.') "
    "WHERE scan_root_id = ? AND state = 'running';";
  static const char *insert_run_sql =
    "INSERT INTO scan_runs (scan_root_id, state, started_at_ms) "
    "VALUES (?, 'running', ?);";
  static const char *deactivate_sql =
    "UPDATE database_locations SET active = 0 WHERE scan_root_id = ?;";
  static const char *start_root_sql =
    "UPDATE scan_roots SET last_started_at_ms = ? WHERE id = ?;";
  sqlite3_stmt *stmt;
  long long root_id;
  int rc;

  if ((db == NULL) || (scan_root == NULL) || (scan_root[0] == '\0') ||
      (scan_run_id_out == NULL)) {
    strappy_set_error(error_out, "Database scan start is incomplete.");
    return 0;
  }
  *scan_run_id_out = 0LL;
  root_id = 0LL;
  if (!strappy_db_catalog_scan_root_id(db,
                                      scan_root,
                                      now_ms,
                                      &root_id,
                                      error_out)) {
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, cancel_running_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)root_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not cancel stale scan run: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_run_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)root_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not begin scan run: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  *scan_run_id_out = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, deactivate_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)root_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not reset scan locations: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, start_root_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)root_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not start scan root: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_catalog_finish_scan(sqlite3 *db,
                                          long long scan_run_id,
                                          const char *state,
                                          const char *error_message,
                                          long long now_ms,
                                          char **error_out)
{
  static const char *finish_run_sql =
    "UPDATE scan_runs SET state = ?, completed_at_ms = ?, error_message = ? "
    "WHERE id = ? AND state = 'running';";
  static const char *finish_root_sql =
    "UPDATE scan_roots SET last_completed_at_ms = ? WHERE id = "
    "(SELECT scan_root_id FROM scan_runs WHERE id = ?);";
  sqlite3_stmt *stmt;
  int rc;

  if ((db == NULL) || (scan_run_id <= 0LL) || (state == NULL) ||
      ((strcmp(state, "completed") != 0) &&
       (strcmp(state, "error") != 0) &&
       (strcmp(state, "cancelled") != 0))) {
    strappy_set_error(error_out, "Database scan finish is incomplete.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, finish_run_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, state, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      !strappy_db_bind_optional_text(db,
                                     stmt,
                                     3,
                                     error_message,
                                     "Could not bind scan error",
                                     error_out) ||
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)scan_run_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not finish scan run: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);

  if (strcmp(state, "completed") != 0) {
    return 1;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, finish_root_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)scan_run_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not finish scan root: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_catalog_save(sqlite3 *db,
                                   const strappy_discovered_database_input *records,
                                   size_t count,
                                   int replace_root,
                                   const char *scan_root,
                                   long long scan_run_id,
                                   char **error_out)
{
  long long now_ms;
  long long run_id;
  size_t index;
  int ok;

  now_ms = strappy_db_now_ms();
  run_id = scan_run_id;
  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin catalog write",
                       error_out)) {
    return 0;
  }
  ok = 1;
  if (replace_root) {
    ok = strappy_db_catalog_begin_scan(db,
                                       scan_root,
                                       now_ms,
                                       &run_id,
                                       error_out);
  }
  for (index = 0U; ok && (index < count); index++) {
    ok = strappy_db_catalog_save_one(db,
                                     &records[index],
                                     run_id,
                                     now_ms,
                                     error_out);
  }
  if (ok && replace_root) {
    ok = strappy_db_catalog_finish_scan(db,
                                        run_id,
                                        "completed",
                                        NULL,
                                        now_ms,
                                        error_out);
  }
  if (ok) {
    ok = strappy_db_exec(db, "COMMIT;", "Could not commit catalog write",
                         error_out);
  }
  if (!ok) {
    (void)strappy_db_exec(db, "ROLLBACK;", "Could not roll back catalog write",
                          NULL);
  }
  return ok;
}

static int strappy_db_semantic_save_discovered_databases(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  int replace_root,
  const char *scan_root,
  long long scan_run_id,
  char **error_out)
{
  sqlite3 *db;
  int ok;

  if ((records == NULL) && (count > 0U)) {
    strappy_set_error(error_out, "Discovered database records are missing.");
    return 0;
  }
  if (replace_root && ((scan_root == NULL) || (scan_root[0] == '\0'))) {
    strappy_set_error(error_out, "Replacement scan root is empty.");
    return 0;
  }
  if (replace_root && (scan_run_id > 0LL)) {
    strappy_set_error(error_out,
                      "Replacement scan cannot reuse an existing scan run.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  ok = strappy_db_ensure_schema(db, error_out) &&
       strappy_db_catalog_save(db,
                               records,
                               count,
                               replace_root,
                               scan_root,
                               scan_run_id,
                               error_out);
  strappy_db_release(db);
  return ok;
}

static int strappy_db_semantic_update_database_permission(
  const char *db_path,
  long long database_id,
  const char *decision,
  int update_hidden,
  int hidden,
  char **error_out)
{
  static const char *decision_sql =
    "UPDATE database_permissions SET decision = ?, updated_at_ms = ? "
    "WHERE database_id = ? AND "
    "(? != 'allowed' OR EXISTS (SELECT 1 FROM database_locations l "
    "WHERE l.database_id = database_permissions.database_id "
    "AND l.active = 1 AND l.validation_state = 'valid'));";
  static const char *hidden_sql =
    "UPDATE database_permissions SET hidden = ?, updated_at_ms = ? "
    "WHERE database_id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  const char *sql;
  long long now_ms;
  int rc;
  int ok;

  if (database_id <= 0LL) {
    strappy_set_error(error_out, "Discovered database id is not valid.");
    return 0;
  }
  if (!update_hidden && !strappy_db_is_valid_user_decision(decision)) {
    strappy_set_error(error_out, "Discovered database decision is not valid.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  sql = update_hidden ? hidden_sql : decision_sql;
  now_ms = strappy_db_now_ms();
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare database permission update: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }
  if (update_hidden) {
    ok = (sqlite3_bind_int(stmt, 1, hidden ? 1 : 0) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)now_ms) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)database_id) == SQLITE_OK);
  } else {
    ok = (sqlite3_bind_text(stmt, 1, decision, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)now_ms) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)database_id) == SQLITE_OK) &&
         (sqlite3_bind_text(stmt, 4, decision, -1, SQLITE_TRANSIENT) == SQLITE_OK);
  }
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not update database permission: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  if (sqlite3_changes(db) < 1) {
    strappy_set_error(error_out,
                      update_hidden ? "Discovered database was not found." :
                      "Discovered database was not found or is not valid SQLite.");
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  sqlite3_finalize(stmt);
  strappy_db_release(db);
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
  strappy_db_release(db);
  return ok;
}

int strappy_db_begin_discovered_database_scan(
  const char *db_path,
  const char *scan_root,
  long long *scan_run_id_out,
  char **error_out)
{
  sqlite3 *db;
  int ok;

  if (scan_run_id_out == NULL) {
    strappy_set_error(error_out, "Database scan run output is missing.");
    return 0;
  }
  *scan_run_id_out = 0LL;
  if ((scan_root == NULL) || (scan_root[0] == '\0')) {
    strappy_set_error(error_out, "Database scan root is empty.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin database scan",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }
  ok = strappy_db_catalog_begin_scan(db,
                                     scan_root,
                                     strappy_db_now_ms(),
                                     scan_run_id_out,
                                     error_out);
  if (ok) {
    ok = strappy_db_exec(db,
                         "COMMIT;",
                         "Could not commit database scan start",
                         error_out);
  }
  if (!ok) {
    (void)strappy_db_exec(db,
                          "ROLLBACK;",
                          "Could not roll back database scan start",
                          NULL);
    *scan_run_id_out = 0LL;
  }
  strappy_db_release(db);
  return ok;
}

int strappy_db_finish_discovered_database_scan(
  const char *db_path,
  long long scan_run_id,
  const char *state,
  const char *error_message,
  char **error_out)
{
  sqlite3 *db;
  int ok;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin database scan finish",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }
  ok = strappy_db_catalog_finish_scan(db,
                                      scan_run_id,
                                      state,
                                      error_message,
                                      strappy_db_now_ms(),
                                      error_out);
  if (ok) {
    ok = strappy_db_exec(db,
                         "COMMIT;",
                         "Could not commit database scan finish",
                         error_out);
  }
  if (!ok) {
    (void)strappy_db_exec(db,
                          "ROLLBACK;",
                          "Could not roll back database scan finish",
                          NULL);
  }
  strappy_db_release(db);
  return ok;
}

int strappy_db_save_discovered_databases_for_scan_run(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  long long scan_run_id,
  char **error_out)
{
  if (scan_run_id <= 0LL) {
    strappy_set_error(error_out, "Database scan run is missing.");
    return 0;
  }
  return strappy_db_semantic_save_discovered_databases(db_path,
                                                       records,
                                                       count,
                                                       0,
                                                       NULL,
                                                       scan_run_id,
                                                       error_out);
}

int strappy_db_save_discovered_databases(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  char **error_out)
{
  return strappy_db_semantic_save_discovered_databases(db_path,
                                                       records,
                                                       count,
                                                       0,
                                                       NULL,
                                                       0LL,
                                                       error_out);

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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin discovered database save",
                       error_out)) {
    strappy_db_release(db);
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
      strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  strappy_db_release(db);
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
  return strappy_db_semantic_save_discovered_databases(db_path,
                                                       records,
                                                       count,
                                                       1,
                                                       scan_root,
                                                       0LL,
                                                       error_out);

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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin discovered database replacement",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_prepare_scan_path_table(db, error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database replacement",
                    NULL);
    strappy_db_release(db);
    return 0;
  }

  for (index = 0U; index < count; index++) {
    if (!strappy_db_add_scan_path(db, records[index].path, error_out)) {
      strappy_db_exec(db,
                      "ROLLBACK;",
                      "Could not roll back discovered database replacement",
                      NULL);
      strappy_db_release(db);
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
    strappy_db_release(db);
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
      strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  strappy_db_release(db);
  return 1;
}

int strappy_db_list_discovered_databases(
  const char *db_path,
  strappy_discovered_database_record_list *list,
  char **error_out)
{
  static const char *sql =
    "SELECT d.id, d.assistant_database_id, l.path, l.size_bytes, "
    "l.modified_at_s, l.device, l.inode, "
    "CASE WHEN l.validation_state = 'valid' THEN 1 ELSE 0 END, "
    "l.validation_error, l.validation_state, p.decision, p.hidden, "
    "r.path, a.stable_key, COALESCE(a.name, d.display_name), "
    "a.bundle_id, a.container_path, "
    "a.bundle_path, a.source, d.origin_kind, d.location_tail, "
    "strftime('%Y-%m-%dT%H:%M:%fZ', d.first_seen_at_ms / 1000.0, 'unixepoch'), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', l.last_seen_at_ms / 1000.0, 'unixepoch'), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', l.last_scanned_at_ms / 1000.0, 'unixepoch') "
    "FROM databases d "
    "JOIN database_locations l ON l.database_id = d.id AND l.active = 1 "
    "JOIN database_permissions p ON p.database_id = d.id "
    "LEFT JOIN applications a ON a.id = d.application_id "
    "LEFT JOIN scan_roots r ON r.id = l.scan_root_id "
    "ORDER BY l.last_seen_at_ms DESC, d.id DESC, l.path;";
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
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare discovered database list: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_discovered_database_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_discovered_database_record))) {
      strappy_set_error(error_out, "Discovered database list is too large.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_discovered_database_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_discovered_database_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_discovered_database_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate discovered database list.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
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
      strappy_db_release(db);
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
    strappy_db_release(db);
    strappy_discovered_database_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

static char *strappy_db_copy_database_filename(const char *path)
{
  const char *backslash;
  const char *filename;
  const char *slash;

  if (path == NULL) {
    return NULL;
  }
  slash = strrchr(path, '/');
  backslash = strrchr(path, '\\');
  filename = path;
  if ((slash != NULL) &&
      ((backslash == NULL) || (slash > backslash))) {
    filename = slash + 1;
  } else if (backslash != NULL) {
    filename = backslash + 1;
  }
  if (filename[0] == '\0') {
    return NULL;
  }
  return strappy_string_duplicate(filename);
}

int strappy_db_list_approved_database_display_names(
  const char *db_path,
  strappy_database_display_name_record_list *list,
  char **error_out)
{
  static const char *sql =
    "SELECT d.assistant_database_id, l.path "
    "FROM databases d "
    "JOIN database_locations l ON l.database_id = d.id AND l.active = 1 "
    "JOIN database_permissions p ON p.database_id = d.id "
    "WHERE d.assistant_database_id IS NOT NULL "
    "AND d.assistant_database_id <> '' "
    "AND l.validation_state = 'valid' "
    "AND p.decision = 'allowed' "
    "ORDER BY d.id, l.path;";
  strappy_database_display_name_record *record;
  strappy_database_display_name_record *next_records;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  char *path;
  int rc;

  if (list == NULL) {
    strappy_set_error(
      error_out,
      "strappy_db_list_approved_database_display_names received no output.");
    return 0;
  }
  strappy_database_display_name_record_list_init(list);

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(
      error_out,
      "Could not prepare approved database display names: %s",
      sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (list->count >=
        (((size_t)-1) / sizeof(strappy_database_display_name_record))) {
      strappy_set_error(error_out,
                        "Approved database display name list is too large.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_database_display_name_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_database_display_name_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(*next_records));
    if (next_records == NULL) {
      strappy_set_error(
        error_out,
        "Could not allocate approved database display names.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_database_display_name_record_list_destroy(list);
      return 0;
    }
    list->records = next_records;
    record = &list->records[list->count];
    record->assistant_database_id = strappy_db_column_string(stmt, 0);
    record->filename = NULL;
    path = strappy_db_column_string(stmt, 1);
    if (path != NULL) {
      record->filename = strappy_db_copy_database_filename(path);
    }
    free(path);
    if ((record->assistant_database_id == NULL) ||
        (record->filename == NULL)) {
      free(record->assistant_database_id);
      free(record->filename);
      record->assistant_database_id = NULL;
      record->filename = NULL;
      strappy_set_error(
        error_out,
        "Could not copy an approved database display name.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_database_display_name_record_list_destroy(list);
      return 0;
    }
    list->count++;
  }

  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(
      error_out,
      "Could not read approved database display names: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    strappy_database_display_name_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_update_discovered_database_decision(
  const char *db_path,
  long long catalog_id,
  const char *user_decision,
  char **error_out)
{
  return strappy_db_semantic_update_database_permission(db_path,
                                                        catalog_id,
                                                        user_decision,
                                                        0,
                                                        0,
                                                        error_out);

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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin discovered database decision update",
                       error_out)) {
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }
  sqlite3_finalize(stmt);

  if (path == NULL) {
    strappy_set_error(error_out, "Could not allocate discovered database path.");
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_set_assistant_database_id(db, catalog_id, path, error_out)) {
    free(path);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database decision update",
                    NULL);
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  free(path);
  strappy_db_release(db);
  return 1;
}

int strappy_db_update_discovered_database_hidden(const char *db_path,
                                                 long long catalog_id,
                                                 int hidden,
                                                 char **error_out)
{
  return strappy_db_semantic_update_database_permission(db_path,
                                                        catalog_id,
                                                        NULL,
                                                        1,
                                                        hidden,
                                                        error_out);

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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin discovered database hidden update",
                       error_out)) {
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }
  sqlite3_finalize(stmt);

  if (path == NULL) {
    strappy_set_error(error_out, "Could not allocate discovered database path.");
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back discovered database hidden update",
                    NULL);
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  free(path);
  strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session insert", error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session insert: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    strappy_db_release(db);
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
      strappy_db_release(db);
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
      strappy_db_release(db);
      return 0;
    }
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session insert", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    strappy_db_release(db);
    return 0;
  }

  if (session_id_out != NULL) {
    *session_id_out = session_id;
  }

  strappy_db_release(db);
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

static int strappy_db_validate_working_directory(
  const char *working_directory,
  char **error_out)
{
  struct stat directory_stat;

  if ((working_directory == NULL) || (working_directory[0] == '\0')) {
    strappy_set_error(error_out, "Session working directory is empty.");
    return 0;
  }
  if (working_directory[0] != '/') {
    strappy_set_error(error_out,
                      "Session working directory must be an absolute path.");
    return 0;
  }
  errno = 0;
  if (stat(working_directory, &directory_stat) != 0) {
    strappy_set_formatted_error(error_out,
                                "Could not inspect session working directory "
                                "%s: %s.",
                                working_directory,
                                strerror(errno));
    return 0;
  }
  if (!S_ISDIR(directory_stat.st_mode)) {
    strappy_set_formatted_error(error_out,
                                "Session working directory is not a directory: "
                                "%s.",
                                working_directory);
    return 0;
  }
  return 1;
}

static int strappy_db_save_session_settings(sqlite3 *db,
                                            long long session_id,
                                            char **error_out)
{
  static const char *sql =
    "INSERT INTO session_settings "
    "(session_id, web_provider, bash_enabled, streaming_enabled, "
     "updated_at_ms) VALUES (?, '"
       STRAPPY_DB_DEFAULT_SESSION_WEB_PROVIDER "', 0, 0, ?);";
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)strappy_db_now_ms());
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session settings: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int strappy_db_save_session_working_directory(
  sqlite3 *db,
  long long session_id,
  const char *working_directory,
  char **error_out)
{
  static const char *sql =
    "INSERT OR REPLACE INTO session_working_directories "
    "(session_id, working_directory, updated_at_ms) VALUES (?, ?, ?);";
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt,
                           2,
                           working_directory,
                           -1,
                           SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 3, (sqlite3_int64)strappy_db_now_ms());
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session working directory: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

int strappy_db_create_session_with_working_directory(
  const char *db_path,
  const char *working_directory,
  long long *session_id_out,
  char **error_out)
{
  static const char *sql =
    "INSERT INTO sessions "
    "(model_id, created_at_ms, updated_at_ms) "
    "VALUES (?, ?, ?);";
  char *default_model_id;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  long long session_id;
  long long now_ms;

  if (session_id_out != NULL) {
    *session_id_out = 0;
  }

  if (!strappy_db_validate_working_directory(working_directory, error_out)) {
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  default_model_id = NULL;
  if (!strappy_db_copy_default_openrouter_model(db,
                                                &default_model_id,
                                                error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin session insert",
                       error_out)) {
    free(default_model_id);
    strappy_db_release(db);
    return 0;
  }

  now_ms = strappy_db_now_ms();
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session insert: %s",
                                sqlite3_errmsg(db));
    free(default_model_id);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back session insert",
                    NULL);
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_bind_text(stmt, 1, default_model_id, -1, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)now_ms);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ms);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session insert: %s",
                                sqlite3_errmsg(db));
    free(default_model_id);
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back session insert",
                    NULL);
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not create session: %s",
                                sqlite3_errmsg(db));
    free(default_model_id);
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back session insert",
                    NULL);
    strappy_db_release(db);
    return 0;
  }

  session_id = (long long)sqlite3_last_insert_rowid(db);
  free(default_model_id);
  sqlite3_finalize(stmt);
  if (!strappy_db_save_session_settings(db, session_id, error_out) ||
      !strappy_db_save_session_working_directory(db,
                                                  session_id,
                                                  working_directory,
                                                  error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back session insert",
                    NULL);
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit session insert",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back session insert",
                    NULL);
    strappy_db_release(db);
    return 0;
  }
  if (session_id_out != NULL) {
    *session_id_out = session_id;
  }
  strappy_db_release(db);
  return 1;
}

int strappy_db_create_session(const char *db_path,
                              long long *session_id_out,
                              char **error_out)
{
  const char *home_directory;

  home_directory = getenv("HOME");
  if ((home_directory == NULL) || (home_directory[0] == '\0')) {
    if (session_id_out != NULL) {
      *session_id_out = 0;
    }
    strappy_set_error(error_out,
                      "Could not determine the session home directory.");
    return 0;
  }
  return strappy_db_create_session_with_working_directory(db_path,
                                                          home_directory,
                                                          session_id_out,
                                                          error_out);
}

static int strappy_db_copy_session_working_directory(
  sqlite3 *db,
  long long session_id,
  char **working_directory_out,
  int *found_out,
  char **error_out)
{
  static const char *sql =
    "SELECT working_directory FROM session_working_directories "
    "WHERE session_id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  *working_directory_out = NULL;
  *found_out = 0;
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc == SQLITE_ROW) {
    *working_directory_out = strappy_db_column_string(stmt, 0);
    *found_out = 1;
    sqlite3_finalize(stmt);
    if (*working_directory_out == NULL) {
      strappy_set_error(error_out,
                        "Could not allocate session working directory.");
      return 0;
    }
    return 1;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read session working directory: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

int strappy_db_get_session_working_directory(
  const char *db_path,
  long long session_id,
  char **working_directory_out,
  char **error_out)
{
  sqlite3 *db;
  const char *home_directory;
  int found;

  if (working_directory_out != NULL) {
    *working_directory_out = NULL;
  }
  if ((working_directory_out == NULL) || (session_id <= 0LL)) {
    strappy_set_error(error_out,
                      "Session working-directory request is incomplete.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_copy_session_working_directory(db,
                                                 session_id,
                                                 working_directory_out,
                                                 &found,
                                                 error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (found) {
    strappy_db_release(db);
    return 1;
  }
  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  home_directory = getenv("HOME");
  if (!strappy_db_validate_working_directory(home_directory, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_save_session_working_directory(db,
                                                 session_id,
                                                 home_directory,
                                                 error_out)) {
    strappy_db_release(db);
    return 0;
  }
  *working_directory_out = strappy_string_duplicate(home_directory);
  if (*working_directory_out == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate session working directory.");
    strappy_db_release(db);
    return 0;
  }
  strappy_db_release(db);
  return 1;
}

int strappy_db_update_session_working_directory(
  const char *db_path,
  long long session_id,
  const char *working_directory,
  char **error_out)
{
  sqlite3 *db;
  int ok;

  if (session_id <= 0LL) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }
  if (!strappy_db_validate_working_directory(working_directory, error_out)) {
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  ok = strappy_db_save_session_working_directory(db,
                                                 session_id,
                                                 working_directory,
                                                 error_out);
  strappy_db_release(db);
  return ok;
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
  sqlite3_stmt *stmt;
  int rc;

  if (session_id <= 0) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, "DELETE FROM sessions WHERE id = ?;", -1,
                          &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not delete session: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin session delete",
                       error_out)) {
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit session delete",
                       error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session delete", NULL);
    strappy_db_release(db);
    return 0;
  }

  strappy_db_release(db);
  return 1;
}

int strappy_db_update_session_name(const char *db_path,
                                   long long session_id,
                                   const char *name,
                                   char **error_out)
{
  static const char *sql =
    "UPDATE sessions "
    "SET name = ?, updated_at_ms = "
      "CAST(strftime('%s','now') AS INTEGER) * 1000 "
    "WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if ((name == NULL) || (name[0] == '\0')) {
    strappy_set_error(error_out, "Session name is empty.");
    return 0;
  }

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session name update: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  if ((sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session name update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session name: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_update_session_streaming_enabled(const char *db_path,
                                                long long session_id,
                                                int streaming_enabled,
                                                char **error_out)
{
  static const char *sql =
    "UPDATE session_settings "
    "SET streaming_enabled = ?, updated_at_ms = "
      "CAST(strftime('%s','now') AS INTEGER) * 1000 "
    "WHERE session_id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session streaming update: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  if ((sqlite3_bind_int(stmt, 1, streaming_enabled ? 1 : 0) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session streaming update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if ((rc != SQLITE_DONE) || (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not update session streaming setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session insert", error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session insert: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session insert", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session insert", NULL);
    strappy_db_release(db);
    return 0;
  }

  if (session_id_out != NULL) {
    *session_id_out = session_id;
  }

  strappy_db_release(db);
  return 1;
}

int strappy_db_update_session_web_provider(
  const char *db_path,
  long long session_id,
  strappy_web_provider web_provider,
  char **error_out)
{
  static const char *sql =
    "UPDATE session_settings "
    "SET web_provider = ?, updated_at_ms = "
      "CAST(strftime('%s','now') AS INTEGER) * 1000 "
    "WHERE session_id = ?;";
  const char *provider_name;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  provider_name = strappy_web_provider_name(web_provider);
  if (provider_name == NULL) {
    strappy_set_error(error_out, "Session web provider is invalid.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }

  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session web provider update: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  if ((sqlite3_bind_text(stmt,
                         1,
                         provider_name,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session web provider update: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if ((rc != SQLITE_DONE) || (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not update session web provider: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

static int strappy_db_save_session_bash_enabled(sqlite3 *db,
                                                long long session_id,
                                                int bash_enabled,
                                                char **error_out)
{
  static const char *sql =
    "UPDATE session_settings "
    "SET bash_enabled = ?, updated_at_ms = ? WHERE session_id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 1, bash_enabled ? 1 : 0);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)strappy_db_now_ms());
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 3, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  if ((rc != SQLITE_DONE) || (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not save session Bash setting: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int strappy_db_copy_session_assistant_set(
  sqlite3 *db,
  long long session_id,
  char **assistant_set_id_out,
  char **error_out)
{
  static const char *sql =
    "SELECT COALESCE((SELECT a.assistant_set_id "
      "FROM session_assistant_sets a WHERE a.session_id = ?), '"
      STRAPPY_ASSISTANT_SET_DEFAULT "') "
    "WHERE EXISTS (SELECT 1 FROM sessions s WHERE s.id = ?);";
  sqlite3_stmt *stmt;
  int rc;

  if (assistant_set_id_out != NULL) {
    *assistant_set_id_out = NULL;
  }
  if ((db == NULL) || (assistant_set_id_out == NULL) || (session_id <= 0LL)) {
    strappy_set_error(error_out,
                      "Session assistant-set request is incomplete.");
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session assistant-set read: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  if ((sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session assistant-set read: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
    sqlite3_finalize(stmt);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read session assistant set: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  *assistant_set_id_out = strappy_db_column_string(stmt, 0);
  sqlite3_finalize(stmt);
  if (*assistant_set_id_out == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate session assistant set.");
    return 0;
  }
  return 1;
}

int strappy_db_get_session_assistant_set(const char *db_path,
                                         long long session_id,
                                         char **assistant_set_id_out,
                                         char **error_out)
{
  sqlite3 *db;
  int ok;

  if (assistant_set_id_out != NULL) {
    *assistant_set_id_out = NULL;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  ok = strappy_db_copy_session_assistant_set(db,
                                             session_id,
                                             assistant_set_id_out,
                                             error_out);
  strappy_db_release(db);
  return ok;
}

int strappy_db_get_session_bash_enabled(const char *db_path,
                                        long long session_id,
                                        int *bash_enabled_out,
                                        char **error_out)
{
  static const char *sql =
    "SELECT " STRAPPY_DB_SESSION_BASH_ENABLED_SQL " "
    "FROM sessions s WHERE s.id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (bash_enabled_out == NULL) {
    strappy_set_error(error_out, "Session Bash setting has no output.");
    return 0;
  }
  *bash_enabled_out = 0;
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read session Bash setting: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  *bash_enabled_out = sqlite3_column_int(stmt, 0) ? 1 : 0;
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_update_session_bash_enabled(const char *db_path,
                                           long long session_id,
                                           int bash_enabled,
                                           char **error_out)
{
  sqlite3 *db;
  char *assistant_set_id;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin Bash-setting update",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }
  assistant_set_id = NULL;
  if (!strappy_db_copy_session_assistant_set(db,
                                             session_id,
                                             &assistant_set_id,
                                             error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back Bash-setting update",
                    NULL);
    strappy_db_release(db);
    return 0;
  }
  if (bash_enabled &&
      (strcmp(assistant_set_id,
              STRAPPY_ASSISTANT_SET_CODING_ASSISTANT) != 0)) {
    free(assistant_set_id);
    strappy_set_error(
      error_out,
      "Bash can be enabled only for Coding Assistant sessions.");
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back Bash-setting update",
                    NULL);
    strappy_db_release(db);
    return 0;
  }
  free(assistant_set_id);
  if (!strappy_db_save_session_bash_enabled(db,
                                            session_id,
                                            bash_enabled,
                                            error_out) ||
      !strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit Bash-setting update",
                       error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back Bash-setting update",
                    NULL);
    strappy_db_release(db);
    return 0;
  }
  strappy_db_release(db);
  return 1;
}

int strappy_db_update_session_assistant_set(const char *db_path,
                                            long long session_id,
                                            const char *assistant_set_id,
                                            char **error_out)
{
  static const char *insert_sql =
    "INSERT OR REPLACE INTO session_assistant_sets "
    "(session_id, assistant_set_id, updated_at_ms) VALUES (?, ?, ?);";
  static const char *touch_sql =
    "UPDATE sessions SET updated_at_ms = ? WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  char *current;
  int rc;

  if ((assistant_set_id == NULL) || (assistant_set_id[0] == '\0')) {
    strappy_set_error(error_out, "Assistant set is not selected.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin assistant-set update",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }
  current = NULL;
  if (!strappy_db_copy_session_assistant_set(db,
                                             session_id,
                                             &current,
                                             error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back assistant-set update", NULL);
    strappy_db_release(db);
    return 0;
  }
  if ((strcmp(assistant_set_id,
              STRAPPY_ASSISTANT_SET_CODING_ASSISTANT) != 0) &&
      !strappy_db_save_session_bash_enabled(db,
                                            session_id,
                                            0,
                                            error_out)) {
    free(current);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back assistant-set update",
                    NULL);
    strappy_db_release(db);
    return 0;
  }
  if (strcmp(current, assistant_set_id) == 0) {
    free(current);
    if (!strappy_db_exec(db,
                         "COMMIT;",
                         "Could not commit assistant-set update",
                         error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back assistant-set update", NULL);
      strappy_db_release(db);
      return 0;
    }
    strappy_db_release(db);
    return 1;
  }
  free(current);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 2, assistant_set_id, -1, SQLITE_TRANSIENT);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 3, (sqlite3_int64)strappy_db_now_ms());
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save session assistant set: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back assistant-set update", NULL);
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, touch_sql, -1, &stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)strappy_db_now_ms());
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_step(stmt);
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not touch session assistant set: %s",
                                sqlite3_errmsg(db));
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back assistant-set update", NULL);
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit assistant-set update",
                       error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back assistant-set update", NULL);
    strappy_db_release(db);
    return 0;
  }
  strappy_db_release(db);
  return 1;
}

int strappy_db_list_sessions(const char *db_path,
                             strappy_session_record_list *list,
                             char **error_out)
{
  static const char *sql =
    "SELECT s.id, s.name, '', '', "
    STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL ", "
    "COALESCE(m.name, " STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL "), 0, "
    "strftime('%Y-%m-%dT%H:%M:%fZ', s.created_at_ms / 1000.0, 'unixepoch'), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', "
      STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL " / 1000.0, 'unixepoch'), "
    STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL ", "
    STRAPPY_DB_SESSION_WEB_PROVIDER_SQL ", "
    STRAPPY_DB_SESSION_BASH_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_STREAMING_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_ASSISTANT_SET_SQL " "
    "FROM sessions s LEFT JOIN models m ON m.id = "
      STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL " "
    "ORDER BY " STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL " DESC, s.id DESC;";
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
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session list: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_session_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_session_record))) {
      strappy_set_error(error_out, "Session list is too large.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_session_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_session_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate session list.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_session_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_session_record_init(&list->records[list->count]);
    if (!strappy_db_assign_record_from_statement(&list->records[list->count],
                                                 stmt,
                                                 error_out)) {
      sqlite3_finalize(stmt);
      strappy_db_release(db);
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
    strappy_db_release(db);
    strappy_session_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_load_session(const char *db_path,
                            long long session_id,
                            strappy_session_record *record,
                            char **error_out)
{
  static const char *sql =
    "SELECT s.id, s.name, "
    "COALESCE((SELECT p.text FROM conversation_items i "
      "JOIN message_items m ON m.item_id = i.id "
      "JOIN item_text_parts p ON p.item_id = i.id "
      "WHERE i.session_id = s.id AND m.role = 'user' "
      "ORDER BY i.sequence DESC, p.ordinal LIMIT 1), ''), "
    "COALESCE((SELECT p.text FROM conversation_items i "
      "JOIN message_items m ON m.item_id = i.id "
      "JOIN item_text_parts p ON p.item_id = i.id "
      "WHERE i.session_id = s.id AND m.role = 'assistant' "
      "AND i.include_in_context = 1 AND i.source_attempt_id IS NOT NULL "
      "ORDER BY i.sequence DESC, p.ordinal LIMIT 1), ''), "
    STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL ", "
    "COALESCE(m.name, " STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL "), "
    "COALESCE((SELECT a.http_status FROM http_attempts a "
      "JOIN model_requests r ON r.id = a.request_id "
      "JOIN turns t ON t.id = r.turn_id "
      "WHERE t.session_id = s.id ORDER BY a.id DESC LIMIT 1), 0), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', s.created_at_ms / 1000.0, 'unixepoch'), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', "
      STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL " / 1000.0, 'unixepoch'), "
    STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL ", "
    STRAPPY_DB_SESSION_WEB_PROVIDER_SQL ", "
    STRAPPY_DB_SESSION_BASH_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_STREAMING_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_ASSISTANT_SET_SQL " "
    "FROM sessions s LEFT JOIN models m ON m.id = "
      STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL " WHERE s.id = ?;";
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
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session load: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session load: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read session: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  ok = strappy_db_assign_record_from_statement(record, stmt, error_out);
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  if (!ok) {
    strappy_session_record_destroy(record);
    return 0;
  }

  return 1;
}

int strappy_db_load_session_list_record(const char *db_path,
                                        long long session_id,
                                        strappy_session_record *record,
                                        char **error_out)
{
  static const char *sql =
    "SELECT s.id, s.name, '', '', "
    STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL ", "
    "COALESCE(m.name, " STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL "), 0, "
    "strftime('%Y-%m-%dT%H:%M:%fZ', s.created_at_ms / 1000.0, 'unixepoch'), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', "
      STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL " / 1000.0, 'unixepoch'), "
    STRAPPY_DB_SESSION_LAST_ACTIVITY_MS_SQL ", "
    STRAPPY_DB_SESSION_WEB_PROVIDER_SQL ", "
    STRAPPY_DB_SESSION_BASH_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_STREAMING_ENABLED_SQL ", "
    STRAPPY_DB_SESSION_ASSISTANT_SET_SQL " "
    "FROM sessions s LEFT JOIN models m ON m.id = "
      STRAPPY_DB_SESSION_EFFECTIVE_MODEL_SQL " WHERE s.id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if (record == NULL) {
    strappy_set_error(error_out,
                      "strappy_db_load_session_list_record received no output.");
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
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session list row load: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }
  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session list row load: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    strappy_set_error(error_out, "Session was not found.");
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not read session list row: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  ok = strappy_db_assign_record_from_statement(record, stmt, error_out);
  sqlite3_finalize(stmt);
  strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session append", error_out)) {
    strappy_db_release(db);
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
      strappy_db_release(db);
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
      strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session exchange: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    strappy_db_release(db);
    return 0;
  }

  sqlite3_finalize(stmt);

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session append", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    strappy_db_release(db);
    return 0;
  }

  strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session append", error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_insert_message_sequence(db,
                                          session_id,
                                          messages,
                                          message_count,
                                          error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session append", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session append", NULL);
    strappy_db_release(db);
    return 0;
  }

  strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin session message upsert", error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_upsert_message(db, session_id, message, error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session message upsert", NULL);
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit session message upsert", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back session message upsert", NULL);
    strappy_db_release(db);
    return 0;
  }

  strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin streamed message append", error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_append_message_content(db,
                                         session_id,
                                         message,
                                         content_delta,
                                         reasoning_delta,
                                         error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back streamed message append", NULL);
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit streamed message append", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back streamed message append", NULL);
    strappy_db_release(db);
    return 0;
  }

  strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin streamed message move", error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_move_message_content_to_reasoning(db,
                                                    session_id,
                                                    message,
                                                    error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back streamed message move", NULL);
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db, "COMMIT;", "Could not commit streamed message move", error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back streamed message move", NULL);
    strappy_db_release(db);
    return 0;
  }

  strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin message render state update",
                       error_out)) {
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    free(sql);
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  free(sql);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message list: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session message list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_session_message_record *next_records;

    if (list->count >= (((size_t)-1) / sizeof(strappy_session_message_record))) {
      strappy_set_error(error_out, "Session message list is too large.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_session_message_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_session_message_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate session message list.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }

    list->records = next_records;
    strappy_session_message_record_init(&list->records[list->count]);
    if (!strappy_db_assign_message_from_statement(&list->records[list->count],
                                                  stmt,
                                                  error_out)) {
      sqlite3_finalize(stmt);
      strappy_db_release(db);
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
    strappy_db_release(db);
    strappy_session_message_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_list_session_messages(const char *db_path,
                                     long long session_id,
                                     strappy_session_message_record_list *list,
                                     char **error_out)
{
  return strappy_db_list_response_timeline(db_path,
                                           session_id,
                                           list,
                                           error_out);
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

  {
    strappy_session_message_record_list timeline;
    size_t index;

    strappy_session_message_record_list_init(&timeline);
    if (!strappy_db_list_response_timeline(db_path,
                                           session_id,
                                           &timeline,
                                           error_out)) {
      return 0;
    }
    for (index = 0U; index < timeline.count; index++) {
      if ((timeline.records[index].message_key != NULL) &&
          (strcmp(timeline.records[index].message_key, message_key) == 0)) {
        *record = timeline.records[index];
        strappy_session_message_record_init(&timeline.records[index]);
        strappy_session_message_record_list_destroy(&timeline);
        return 1;
      }
    }
    strappy_session_message_record_list_destroy(&timeline);
    strappy_set_error(error_out, "Session message was not found.");
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
    strappy_db_release(db);
    return 0;
  }

  if (!strappy_db_session_exists(db, session_id, error_out)) {
    free(sql);
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  free(sql);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session message lookup: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    if (!strappy_db_assign_message_from_statement(record, stmt, error_out)) {
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      return 0;
    }
    sqlite3_finalize(stmt);
    strappy_db_release(db);
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
  strappy_db_release(db);
  return 0;
}

int strappy_db_list_session_context_messages(
  const char *db_path,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out)
{
  strappy_session_message_record_list timeline;
  size_t index;

  if (list == NULL) {
    strappy_set_error(error_out, "Session context list has no output.");
    return 0;
  }
  strappy_session_message_record_list_init(list);
  strappy_session_message_record_list_init(&timeline);
  if (!strappy_db_list_response_timeline(db_path,
                                         session_id,
                                         &timeline,
                                         error_out)) {
    return 0;
  }
  for (index = 0U; index < timeline.count; index++) {
    strappy_session_message_record *records;

    if (!timeline.records[index].include_in_context ||
        (timeline.records[index].api_role == NULL) ||
        (timeline.records[index].api_role[0] == '\0')) {
      continue;
    }
    records = (strappy_session_message_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(*records));
    if (records == NULL) {
      strappy_session_message_record_list_destroy(&timeline);
      strappy_session_message_record_list_destroy(list);
      strappy_set_error(error_out, "Could not allocate session context list.");
      return 0;
    }
    list->records = records;
    list->records[list->count] = timeline.records[index];
    strappy_session_message_record_init(&timeline.records[index]);
    list->count++;
  }
  strappy_session_message_record_list_destroy(&timeline);
  return 1;
}

static int strappy_db_semantic_insert_model_price(sqlite3 *db,
                                                  const char *model_id,
                                                  const char *kind,
                                                  const char *value,
                                                  char **error_out)
{
  static const char *sql =
    "INSERT INTO model_prices (model_id, price_kind, price_decimal) "
    "VALUES (?, ?, ?);";
  sqlite3_stmt *stmt;
  int rc;

  if ((value == NULL) || (value[0] == '\0')) {
    return 1;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, kind, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 3, value, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save model price: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_semantic_insert_model_feature_value(
  sqlite3 *db,
  const char *model_id,
  const char *kind,
  const char *value,
  char **error_out)
{
  static const char *sql =
    "INSERT OR IGNORE INTO model_features "
    "(model_id, feature_kind, feature_value) VALUES (?, ?, ?);";
  sqlite3_stmt *stmt;
  int rc;

  if ((value == NULL) || (value[0] == '\0')) {
    return 1;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, kind, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 3, value, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save model feature: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_semantic_insert_model_feature_array(
  sqlite3 *db,
  const char *model_id,
  const char *kind,
  cJSON *value,
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
    ok = strappy_db_semantic_insert_model_feature_value(db,
                                                        model_id,
                                                        kind,
                                                        text,
                                                        error_out);
    free(text);
    return ok;
  }
  count = cJSON_GetArraySize(value);
  for (index = 0; index < count; index++) {
    char *text;
    int ok;

    text = strappy_db_json_copy_value_text(cJSON_GetArrayItem(value, index));
    ok = strappy_db_semantic_insert_model_feature_value(db,
                                                        model_id,
                                                        kind,
                                                        text,
                                                        error_out);
    free(text);
    if (!ok) {
      return 0;
    }
  }
  return 1;
}

static int strappy_db_semantic_insert_model(sqlite3 *db,
                                            cJSON *model,
                                            long long now_ms,
                                            char **error_out)
{
  static const char *insert_sql =
    "INSERT OR IGNORE INTO models "
    "(id, name, catalog_active, last_seen_at_ms) VALUES (?, ?, 1, ?);";
  static const char *update_sql =
    "UPDATE models SET canonical_slug = ?, hugging_face_id = ?, name = ?, "
    "description = ?, context_length = ?, created_at_s = ?, "
    "architecture_modality = ?, architecture_tokenizer = ?, "
    "architecture_instruct_type = ?, provider_context_length = ?, "
    "provider_max_completion_tokens = ?, provider_is_moderated = ?, "
    "knowledge_cutoff = ?, expiration_date = ?, details_url = ?, "
    "catalog_active = 1, last_seen_at_ms = ? WHERE id = ?;";
  static const char *clear_prices_sql =
    "DELETE FROM model_prices WHERE model_id = ?;";
  static const char *clear_features_sql =
    "DELETE FROM model_features WHERE model_id = ?;";
  sqlite3_stmt *stmt;
  strappy_openrouter_model_record record;
  cJSON *architecture;
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
  if (record.name == NULL) {
    record.name = strappy_string_duplicate(record.model_id);
  }
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
  if (record.name == NULL) {
    strappy_openrouter_model_record_destroy(&record);
    strappy_set_error(error_out, "Could not allocate OpenRouter model name.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, record.model_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, record.name, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not insert OpenRouter model: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare OpenRouter model update: %s",
                                sqlite3_errmsg(db));
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  ok = strappy_db_bind_text_or_null(db, stmt, 1, record.canonical_slug,
                                    "Could not bind model update", error_out) &&
       strappy_db_bind_text_or_null(db, stmt, 2, record.hugging_face_id,
                                    "Could not bind model update", error_out) &&
       (sqlite3_bind_text(stmt, 3, record.name, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
       strappy_db_bind_text_or_null(db, stmt, 4, record.description,
                                    "Could not bind model update", error_out) &&
       (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)record.context_length) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)record.created) == SQLITE_OK) &&
       strappy_db_bind_text_or_null(db, stmt, 7, record.architecture_modality,
                                    "Could not bind model update", error_out) &&
       strappy_db_bind_text_or_null(db, stmt, 8, record.architecture_tokenizer,
                                    "Could not bind model update", error_out) &&
       strappy_db_bind_text_or_null(db, stmt, 9, record.architecture_instruct_type,
                                    "Could not bind model update", error_out) &&
       (sqlite3_bind_int64(stmt, 10,
                           (sqlite3_int64)record.top_provider_context_length) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 11,
                           (sqlite3_int64)record.top_provider_max_completion_tokens) == SQLITE_OK) &&
       (sqlite3_bind_int(stmt, 12,
                         record.top_provider_is_moderated ? 1 : 0) == SQLITE_OK) &&
       strappy_db_bind_text_or_null(db, stmt, 13, record.knowledge_cutoff,
                                    "Could not bind model update", error_out) &&
       strappy_db_bind_text_or_null(db, stmt, 14, record.expiration_date,
                                    "Could not bind model update", error_out) &&
       strappy_db_bind_text_or_null(db, stmt, 15, record.links_details,
                                    "Could not bind model update", error_out) &&
       (sqlite3_bind_int64(stmt, 16, (sqlite3_int64)now_ms) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 17, record.model_id, -1, SQLITE_TRANSIENT) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    if (ok) {
      strappy_set_formatted_error(error_out,
                                  "Could not update OpenRouter model: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, clear_prices_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, record.model_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not clear model prices: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, clear_features_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, record.model_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not clear model features: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_openrouter_model_record_destroy(&record);
    return 0;
  }
  sqlite3_finalize(stmt);

  ok = strappy_db_semantic_insert_model_price(db, record.model_id, "prompt",
                                               record.pricing_prompt, error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id, "completion",
                                               record.pricing_completion, error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id, "request",
                                               record.pricing_request, error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id, "image",
                                               record.pricing_image, error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id, "audio",
                                               record.pricing_audio, error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id, "web_search",
                                               record.pricing_web_search, error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id,
                                               "internal_reasoning",
                                               record.pricing_internal_reasoning,
                                               error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id,
                                               "input_cache_read",
                                               record.pricing_input_cache_read,
                                               error_out) &&
       strappy_db_semantic_insert_model_price(db, record.model_id,
                                               "input_cache_write",
                                               record.pricing_input_cache_write,
                                               error_out);
  architecture = strappy_db_json_object_child(model, "architecture");
  if (ok) {
    ok = strappy_db_semantic_insert_model_feature_array(
           db, record.model_id, "input_modality",
           cJSON_GetObjectItem(architecture, "input_modalities"), error_out) &&
         strappy_db_semantic_insert_model_feature_array(
           db, record.model_id, "output_modality",
           cJSON_GetObjectItem(architecture, "output_modalities"), error_out) &&
         strappy_db_semantic_insert_model_feature_array(
           db, record.model_id, "parameter",
           cJSON_GetObjectItem(model, "supported_parameters"), error_out) &&
         strappy_db_semantic_insert_model_feature_array(
           db, record.model_id, "voice",
           cJSON_GetObjectItem(model, "supported_voices"), error_out);
  }
  strappy_openrouter_model_record_destroy(&record);
  return ok;
}

static int strappy_db_semantic_save_models(const char *db_path,
                                           const char *json,
                                           char **error_out)
{
  sqlite3 *db;
  cJSON *root;
  cJSON *data;
  long long now_ms;
  int count;
  int index;
  int ok;

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
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_exec(db, "BEGIN IMMEDIATE;", "Could not begin model refresh",
                       error_out)) {
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }
  ok = strappy_db_exec(db,
                       "UPDATE models SET catalog_active = 0;",
                       "Could not mark model catalog stale",
                       error_out);
  now_ms = strappy_db_now_ms();
  count = cJSON_GetArraySize(data);
  for (index = 0; ok && (index < count); index++) {
    ok = strappy_db_semantic_insert_model(db,
                                          cJSON_GetArrayItem(data, index),
                                          now_ms,
                                          error_out);
  }
  if (ok) {
    ok = strappy_db_exec(db,
                         STRAPPY_DB_INSERT_BUILTIN_DEFAULT_MODEL_SQL,
                         "Could not save built-in default model",
                         error_out);
  }
  if (ok) {
    ok = strappy_db_exec(
      db,
      "UPDATE models SET catalog_active = 1, "
      "last_seen_at_ms = CAST(strftime('%s','now') AS INTEGER) * 1000 "
      "WHERE id = '" STRAPPY_CONFIG_DEFAULT_API_MODEL "';",
      "Could not retain built-in default model",
      error_out);
  }
  if (ok) {
    ok = strappy_db_exec(db, "COMMIT;", "Could not commit model refresh",
                         error_out);
  } else {
    (void)strappy_db_exec(db, "ROLLBACK;", "Could not roll back model refresh",
                          NULL);
  }
  strappy_db_release(db);
  cJSON_Delete(root);
  return ok;
}

int strappy_db_save_openrouter_models_json(const char *db_path,
                                           const char *json,
                                           char **error_out)
{
  return strappy_db_semantic_save_models(db_path, json, error_out);

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
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }

  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin OpenRouter model catalog save",
                       error_out)) {
    strappy_db_release(db);
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
    strappy_db_release(db);
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
      strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }

  strappy_db_release(db);
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

static int strappy_db_semantic_list_models(
  const char *db_path,
  const char *search_text,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  static const char *select_columns =
    "SELECT m.id, m.canonical_slug, m.hugging_face_id, m.name, "
    "m.description, m.context_length, m.created_at_s, "
    "m.architecture_modality, m.architecture_tokenizer, "
    "m.architecture_instruct_type, "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'prompt'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'completion'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'request'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'image'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'audio'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'web_search'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'internal_reasoning'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'input_cache_read'), "
    "(SELECT p.price_decimal FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_kind = 'input_cache_write'), "
    "m.provider_context_length, m.provider_max_completion_tokens, "
    "m.provider_is_moderated, m.knowledge_cutoff, m.expiration_date, "
    "m.details_url, NULL, NULL, NULL, NULL, NULL, NULL, "
    "strftime('%Y-%m-%dT%H:%M:%fZ', m.last_seen_at_ms / 1000.0, 'unixepoch'), "
    "CASE WHEN m.id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
      " THEN 1 ELSE 0 END, "
    "CASE WHEN m.id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
      " OR COALESCE(mp.allowed, 0) = 1 THEN 1 ELSE 0 END "
    "FROM models m LEFT JOIN model_preferences mp ON mp.model_id = m.id ";
  static const char *unfiltered_suffix =
    "WHERE m.catalog_active = 1 "
    "ORDER BY CASE WHEN m.id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
      " THEN 0 ELSE 1 END, LOWER(m.name), m.id;";
  static const char *filtered_suffix =
    "WHERE m.catalog_active = 1 AND ("
    "m.id LIKE ?1 ESCAPE '\\' OR COALESCE(m.canonical_slug, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.hugging_face_id, '') LIKE ?1 ESCAPE '\\' "
    "OR m.name LIKE ?1 ESCAPE '\\' OR COALESCE(m.description, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.architecture_modality, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.architecture_tokenizer, '') LIKE ?1 ESCAPE '\\' "
    "OR COALESCE(m.architecture_instruct_type, '') LIKE ?1 ESCAPE '\\' "
    "OR EXISTS (SELECT 1 FROM model_features f WHERE f.model_id = m.id "
      "AND f.feature_value LIKE ?1 ESCAPE '\\') "
    "OR EXISTS (SELECT 1 FROM model_prices p WHERE p.model_id = m.id "
      "AND p.price_decimal LIKE ?1 ESCAPE '\\')) "
    "ORDER BY CASE WHEN m.id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
      " THEN 0 ELSE 1 END, LOWER(m.name), m.id;";
  strappy_db_sql_buffer query;
  sqlite3 *db;
  sqlite3_stmt *stmt;
  char *pattern;
  const char *suffix;
  int rc;

  if (list == NULL) {
    strappy_set_error(error_out, "OpenRouter model list output is missing.");
    return 0;
  }
  strappy_openrouter_model_record_list_init(list);
  pattern = NULL;
  suffix = unfiltered_suffix;
  if ((search_text != NULL) && (search_text[0] != '\0')) {
    pattern = strappy_db_like_pattern_for_search(search_text, error_out);
    if (pattern == NULL) {
      return 0;
    }
    suffix = filtered_suffix;
  }
  memset(&query, 0, sizeof(query));
  if (!strappy_db_sql_buffer_append(&query, select_columns) ||
      !strappy_db_sql_buffer_append(&query, suffix)) {
    free(pattern);
    strappy_db_sql_buffer_destroy(&query);
    strappy_set_error(error_out, "Could not allocate model catalog query.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    free(pattern);
    strappy_db_sql_buffer_destroy(&query);
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    free(pattern);
    strappy_db_sql_buffer_destroy(&query);
    strappy_db_release(db);
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, query.data, -1, &stmt, NULL);
  strappy_db_sql_buffer_destroy(&query);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare model catalog list: %s",
                                sqlite3_errmsg(db));
    free(pattern);
    strappy_db_release(db);
    return 0;
  }
  if ((pattern != NULL) &&
      (sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not bind model catalog search: %s",
                                sqlite3_errmsg(db));
    free(pattern);
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  free(pattern);
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_openrouter_model_record *records;

    if (list->count >= (((size_t)-1) /
                        sizeof(strappy_openrouter_model_record))) {
      strappy_set_error(error_out, "OpenRouter model list is too large.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }
    records = (strappy_openrouter_model_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_openrouter_model_record));
    if (records == NULL) {
      strappy_set_error(error_out, "Could not allocate model catalog list.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }
    list->records = records;
    strappy_openrouter_model_record_init(&list->records[list->count]);
    if (!strappy_db_assign_openrouter_model_from_statement(
          &list->records[list->count], stmt, error_out)) {
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }
    list->count++;
  }
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read model catalog list: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    strappy_openrouter_model_record_list_destroy(list);
    return 0;
  }
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_list_openrouter_models_matching(
  const char *db_path,
  const char *search_text,
  strappy_openrouter_model_record_list *list,
  char **error_out)
{
  return strappy_db_semantic_list_models(db_path,
                                         search_text,
                                         list,
                                         error_out);

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
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare OpenRouter model list: %s",
                                sqlite3_errmsg(db));
    free(search_pattern);
    strappy_db_release(db);
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
      strappy_db_release(db);
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
      strappy_db_release(db);
      strappy_openrouter_model_record_list_destroy(list);
      return 0;
    }

    next_records = (strappy_openrouter_model_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(strappy_openrouter_model_record));
    if (next_records == NULL) {
      strappy_set_error(error_out, "Could not allocate OpenRouter model list.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
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
      strappy_db_release(db);
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
    strappy_db_release(db);
    strappy_openrouter_model_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

static int strappy_db_model_exists(sqlite3 *db,
                                   const char *model_id,
                                   char **error_out)
{
  static const char *exists_sql =
    "SELECT 1 FROM models WHERE id = ?;";
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
  sqlite3_stmt *semantic_stmt;
  int semantic_rc;

  (void)key;
  semantic_stmt = NULL;
  semantic_rc = sqlite3_prepare_v2(
    db,
    "UPDATE app_preferences SET default_model_id = ?, updated_at_ms = ? "
    "WHERE id = 1;",
    -1,
    &semantic_stmt,
    NULL);
  if ((semantic_rc != SQLITE_OK) ||
      (sqlite3_bind_text(semantic_stmt, 1, value, -1, SQLITE_TRANSIENT) !=
       SQLITE_OK) ||
      (sqlite3_bind_int64(semantic_stmt,
                          2,
                          (sqlite3_int64)strappy_db_now_ms()) != SQLITE_OK) ||
      (sqlite3_step(semantic_stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not update %s: %s",
                                error_prefix,
                                sqlite3_errmsg(db));
    sqlite3_finalize(semantic_stmt);
    return 0;
  }
  sqlite3_finalize(semantic_stmt);
  return 1;

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
    " OR EXISTS (SELECT 1 FROM model_preferences "
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
    "INSERT OR REPLACE INTO model_preferences "
    "(model_id, allowed, updated_at_ms) VALUES (?, ?, ?);";
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
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int64(stmt, 3, (sqlite3_int64)strappy_db_now_ms());
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
    strappy_db_release(db);
    return 0;
  }

  ok = strappy_db_set_openrouter_model_allowed_in_db(db,
                                                     model_id,
                                                     allowed,
                                                     error_out);
  strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_model_exists(db, model_id, error_out)) {
    strappy_db_release(db);
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

  strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }

  ok = strappy_db_copy_default_openrouter_model(db, model_id_out, error_out);
  strappy_db_release(db);
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
    "UPDATE sessions SET model_id = ?, updated_at_ms = "
    "CAST(strftime('%s','now') AS INTEGER) * 1000 WHERE id = ?;";
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
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_session_exists(db, session_id, error_out) ||
      !strappy_db_model_exists(db, model_id, error_out) ||
      !strappy_db_model_is_effectively_allowed(db, model_id, &allowed, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!allowed) {
    strappy_db_release(db);
    strappy_set_error(error_out, "OpenRouter model is not allowed.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session model update: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not update session model: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_get_session_model(const char *db_path,
                                 long long session_id,
                                 char **model_id_out,
                                 char **error_out)
{
  static const char *sql =
    "SELECT CASE WHEN NULLIF(s.model_id, '') IS NOT NULL AND "
    "(s.model_id = " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
    " OR EXISTS (SELECT 1 FROM model_preferences mp "
    "WHERE mp.model_id = s.model_id AND mp.allowed = 1)) "
    "THEN s.model_id ELSE " STRAPPY_DB_DEFAULT_OPENROUTER_MODEL_SQL
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
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare session model lookup: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }
  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not bind session model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
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
      strappy_db_release(db);
      strappy_set_error(error_out, "Could not allocate session model id.");
      return 0;
    }
  } else if (rc == SQLITE_DONE) {
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    strappy_set_error(error_out, "Session was not found.");
    return 0;
  } else {
    strappy_set_formatted_error(error_out,
                                "Could not read session model lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
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

static void strappy_db_semantic_digest(const char *text, char output[17])
{
  const unsigned char *cursor;
  unsigned long long hash;

  hash = 1469598103934665603ULL;
  cursor = (const unsigned char *)((text != NULL) ? text : "");
  while (*cursor != '\0') {
    hash ^= (unsigned long long)(*cursor++);
    hash *= 1099511628211ULL;
  }
  snprintf(output, 17U, "%016llx", hash);
}

static int strappy_db_semantic_instruction_revision(sqlite3 *db,
                                                    const char *instructions,
                                                    long long *revision_id_out,
                                                    char **error_out)
{
  static const char *insert_sql =
    "INSERT OR IGNORE INTO instruction_revisions "
    "(digest, text, created_at_ms) VALUES (?, ?, ?);";
  static const char *select_sql =
    "SELECT id, text FROM instruction_revisions WHERE digest = ?;";
  sqlite3_stmt *stmt;
  char digest[17];
  char *stored_text;
  int rc;

  if (revision_id_out == NULL) {
    strappy_set_error(error_out, "Instruction revision output is missing.");
    return 0;
  }
  *revision_id_out = 0LL;
  if (instructions == NULL) {
    return 1;
  }
  strappy_db_semantic_digest(instructions, digest);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, digest, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, instructions, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)strappy_db_now_ms()) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save instruction revision: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, digest, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_ROW)) {
    strappy_set_formatted_error(error_out,
                                "Could not read instruction revision: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  *revision_id_out = (long long)sqlite3_column_int64(stmt, 0);
  stored_text = strappy_db_column_string(stmt, 1);
  sqlite3_finalize(stmt);
  if ((stored_text == NULL) || (strcmp(stored_text, instructions) != 0)) {
    free(stored_text);
    strappy_set_error(error_out, "Instruction revision digest collision.");
    return 0;
  }
  free(stored_text);
  return 1;
}

static int strappy_db_semantic_toolset_revision(sqlite3 *db,
                                                cJSON *tools,
                                                long long *revision_id_out,
                                                char **error_out)
{
  static const char *insert_revision_sql =
    "INSERT OR IGNORE INTO toolset_revisions "
    "(digest, resource_version, created_at_ms) VALUES (?, NULL, ?);";
  static const char *select_revision_sql =
    "SELECT id FROM toolset_revisions WHERE digest = ?;";
  static const char *insert_member_sql =
    "INSERT OR IGNORE INTO toolset_members "
    "(toolset_revision_id, ordinal, tool_name, definition_digest) "
    "VALUES (?, ?, ?, ?);";
  sqlite3_stmt *stmt;
  char digest[17];
  char member_digest[17];
  char *serialized;
  long long revision_id;
  int count;
  int index;
  int rc;

  if (revision_id_out == NULL) {
    strappy_set_error(error_out, "Toolset revision output is missing.");
    return 0;
  }
  *revision_id_out = 0LL;
  if (!cJSON_IsArray(tools)) {
    return 1;
  }
  serialized = cJSON_PrintUnformatted(tools);
  if (serialized == NULL) {
    strappy_set_error(error_out, "Could not fingerprint toolset revision.");
    return 0;
  }
  strappy_db_semantic_digest(serialized, digest);
  free(serialized);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_revision_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, digest, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)strappy_db_now_ms()) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save toolset revision: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, select_revision_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, digest, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_ROW)) {
    strappy_set_formatted_error(error_out,
                                "Could not read toolset revision: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  revision_id = (long long)sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);

  count = cJSON_GetArraySize(tools);
  for (index = 0; index < count; index++) {
    cJSON *tool;
    cJSON *name;
    cJSON *type;
    const char *tool_name;

    tool = cJSON_GetArrayItem(tools, index);
    name = cJSON_IsObject(tool) ? cJSON_GetObjectItem(tool, "name") : NULL;
    type = cJSON_IsObject(tool) ? cJSON_GetObjectItem(tool, "type") : NULL;
    tool_name = (cJSON_IsString(name) && (name->valuestring != NULL)) ?
      name->valuestring :
      ((cJSON_IsString(type) && (type->valuestring != NULL)) ?
        type->valuestring : "unknown");
    serialized = cJSON_PrintUnformatted(tool);
    if (serialized == NULL) {
      strappy_set_error(error_out, "Could not fingerprint tool definition.");
      return 0;
    }
    strappy_db_semantic_digest(serialized, member_digest);
    free(serialized);
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, insert_member_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)revision_id) != SQLITE_OK) ||
        (sqlite3_bind_int(stmt, 2, index) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 3, tool_name, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 4, member_digest, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not save toolset member: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
  }
  *revision_id_out = revision_id;
  return 1;
}

static int strappy_db_semantic_turn(sqlite3 *db,
                                    long long session_id,
                                    const char *prompt_group_key,
                                    long long *turn_id_out,
                                    char **error_out)
{
  static const char *select_sql =
    "SELECT id FROM turns WHERE session_id = ? AND prompt_group_key = ?;";
  static const char *insert_sql =
    "INSERT INTO turns "
    "(session_id, ordinal, prompt_group_key, state, created_at_ms) "
    "SELECT ?, COALESCE(MAX(ordinal), -1) + 1, ?, 'running', ? "
    "FROM turns WHERE session_id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if (turn_id_out == NULL) {
    strappy_set_error(error_out, "Turn output is missing.");
    return 0;
  }
  *turn_id_out = 0LL;
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, prompt_group_key, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare turn lookup: %s",
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
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read turn lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, prompt_group_key, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)strappy_db_now_ms()) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not create turn: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  *turn_id_out = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_semantic_insert_structured_node(
  sqlite3 *db,
  sqlite3_stmt *stmt,
  long long document_id,
  long long node_id,
  long long parent_node_id,
  long ordinal,
  const char *member_name,
  cJSON *value,
  long long *next_node_id_io,
  char **error_out)
{
  cJSON *child;
  const char *value_type;
  const char *text_value;
  char number_value[64];
  int boolean_value;
  int rc;
  int ok;
  long child_ordinal;

  text_value = NULL;
  number_value[0] = '\0';
  boolean_value = -1;
  if (cJSON_IsObject(value)) {
    value_type = "object";
  } else if (cJSON_IsArray(value)) {
    value_type = "array";
  } else if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    value_type = "string";
    text_value = value->valuestring;
  } else if (cJSON_IsNumber(value)) {
    value_type = "number";
    snprintf(number_value, sizeof(number_value), "%.17g", value->valuedouble);
  } else if (cJSON_IsBool(value)) {
    value_type = "boolean";
    boolean_value = cJSON_IsTrue(value) ? 1 : 0;
  } else if (cJSON_IsNull(value)) {
    value_type = "null";
  } else {
    strappy_set_error(error_out, "Structured value contains an unsupported type.");
    return 0;
  }

  rc = sqlite3_reset(stmt);
  if ((rc != SQLITE_OK) ||
      (sqlite3_clear_bindings(stmt) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not reset structured value insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  ok = (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)document_id) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)node_id) == SQLITE_OK) &&
       ((parent_node_id >= 0LL) ?
          (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)parent_node_id) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 3) == SQLITE_OK)) &&
       (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)ordinal) == SQLITE_OK) &&
       strappy_db_bind_nullable_text_value(stmt, 5, member_name) &&
       (sqlite3_bind_text(stmt, 6, value_type, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
       strappy_db_bind_nullable_text_value(stmt, 7, text_value) &&
       strappy_db_bind_nullable_text_value(
         stmt, 8, (number_value[0] != '\0') ? number_value : NULL) &&
       ((boolean_value >= 0) ?
          (sqlite3_bind_int(stmt, 9, boolean_value) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 9) == SQLITE_OK));
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save structured value: %s",
                                sqlite3_errmsg(db));
    sqlite3_reset(stmt);
    return 0;
  }
  if (sqlite3_reset(stmt) != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not reset structured value insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }

  child_ordinal = 0L;
  for (child = value->child; child != NULL; child = child->next) {
    long long child_node_id;
    const char *child_name;

    child_node_id = (*next_node_id_io)++;
    child_name = cJSON_IsObject(value) ? child->string : NULL;
    if (!strappy_db_semantic_insert_structured_node(db,
                                                    stmt,
                                                    document_id,
                                                    child_node_id,
                                                    node_id,
                                                    child_ordinal,
                                                    child_name,
                                                    child,
                                                    next_node_id_io,
                                                    error_out)) {
      return 0;
    }
    child_ordinal++;
  }
  return 1;
}

static int strappy_db_semantic_insert_document(sqlite3 *db,
                                               long long owner_item_id,
                                               const char *purpose,
                                               cJSON *root,
                                               char **error_out)
{
  static const char *sql =
    "INSERT INTO structured_documents (owner_item_id, purpose) VALUES (?, ?);";
  static const char *node_sql =
    "INSERT INTO structured_nodes "
    "(document_id, node_id, parent_node_id, ordinal, member_name, value_type, "
     "text_value, number_value, boolean_value) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt *stmt;
  sqlite3_stmt *node_stmt;
  long long document_id;
  long long next_node_id;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)owner_item_id) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, purpose, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not create structured document: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  document_id = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);
  node_stmt = NULL;
  rc = sqlite3_prepare_v2(db, node_sql, -1, &node_stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare structured value insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  next_node_id = 1LL;
  rc = strappy_db_semantic_insert_structured_node(db,
                                                  node_stmt,
                                                  document_id,
                                                  0LL,
                                                  -1LL,
                                                  0L,
                                                  NULL,
                                                  root,
                                                  &next_node_id,
                                                  error_out);
  sqlite3_finalize(node_stmt);
  return rc;
}

static int strappy_db_semantic_insert_citations(sqlite3 *db,
                                                long long text_part_id,
                                                cJSON *annotations,
                                                char **error_out)
{
  static const char *sql =
    "INSERT INTO item_citations "
    "(text_part_id, ordinal, citation_type, start_offset, end_offset, "
     "title, url, excerpt) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
  cJSON *annotation;
  long ordinal;

  if (!cJSON_IsArray(annotations)) {
    return 1;
  }
  ordinal = 0L;
  for (annotation = annotations->child;
       annotation != NULL;
       annotation = annotation->next) {
    sqlite3_stmt *stmt;
    cJSON *type;
    cJSON *start;
    cJSON *end;
    cJSON *title;
    cJSON *url;
    cJSON *excerpt;
    const char *type_text;
    int rc;
    int ok;

    if (!cJSON_IsObject(annotation)) {
      continue;
    }
    type = cJSON_GetObjectItem(annotation, "type");
    start = cJSON_GetObjectItem(annotation, "start_index");
    end = cJSON_GetObjectItem(annotation, "end_index");
    title = cJSON_GetObjectItem(annotation, "title");
    url = cJSON_GetObjectItem(annotation, "url");
    excerpt = cJSON_GetObjectItem(annotation, "content");
    type_text = (cJSON_IsString(type) && (type->valuestring != NULL)) ?
      type->valuestring : "citation";
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare citation insert: %s",
                                  sqlite3_errmsg(db));
      return 0;
    }
    ok = (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)text_part_id) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)ordinal) == SQLITE_OK) &&
         (sqlite3_bind_text(stmt, 3, type_text, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
         (cJSON_IsNumber(start) ?
            (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)start->valuedouble) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 4) == SQLITE_OK)) &&
         (cJSON_IsNumber(end) ?
            (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)end->valuedouble) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 5) == SQLITE_OK)) &&
         strappy_db_bind_nullable_text_value(
           stmt, 6, (cJSON_IsString(title) ? title->valuestring : NULL)) &&
         strappy_db_bind_nullable_text_value(
           stmt, 7, (cJSON_IsString(url) ? url->valuestring : NULL)) &&
         strappy_db_bind_nullable_text_value(
           stmt, 8, (cJSON_IsString(excerpt) ? excerpt->valuestring : NULL));
    if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not save citation: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
    ordinal++;
  }
  return 1;
}

static int strappy_db_semantic_insert_text_parts(sqlite3 *db,
                                                 long long item_id,
                                                 const char *collection_name,
                                                 cJSON *parts,
                                                 const char *default_part_type,
                                                 char **error_out)
{
  static const char *sql =
    "INSERT INTO item_text_parts "
    "(item_id, collection_name, ordinal, part_type, text) "
    "VALUES (?, ?, ?, ?, ?);";
  cJSON *part;
  long ordinal;

  if ((parts == NULL) || cJSON_IsNull(parts)) {
    return 1;
  }
  part = cJSON_IsArray(parts) ? parts->child : parts;
  ordinal = 0L;
  while (part != NULL) {
    sqlite3_stmt *stmt;
    cJSON *type;
    cJSON *text;
    cJSON *refusal;
    cJSON *annotations;
    const char *part_type;
    const char *part_text;
    long long text_part_id;
    int rc;
    int ok;

    type = cJSON_IsObject(part) ? cJSON_GetObjectItem(part, "type") : NULL;
    text = cJSON_IsObject(part) ? cJSON_GetObjectItem(part, "text") : NULL;
    refusal = cJSON_IsObject(part) ? cJSON_GetObjectItem(part, "refusal") : NULL;
    annotations = cJSON_IsObject(part) ?
      cJSON_GetObjectItem(part, "annotations") : NULL;
    part_type = (cJSON_IsString(type) && (type->valuestring != NULL)) ?
      type->valuestring : default_part_type;
    part_text = cJSON_IsString(part) ? part->valuestring :
      (cJSON_IsString(text) ? text->valuestring :
        (cJSON_IsString(refusal) ? refusal->valuestring : NULL));
    if (cJSON_IsString(refusal)) {
      part_type = "refusal";
    } else if ((strcmp(collection_name, "summary") == 0) &&
               (strcmp(part_type, "summary_text") != 0)) {
      part_type = "summary_text";
    } else if (strcmp(part_type, "text") == 0) {
      part_type = default_part_type;
    }
    if (part_text == NULL) {
      strappy_set_error(error_out, "Responses text part is missing text.");
      return 0;
    }
    if ((strcmp(part_type, "input_text") != 0) &&
        (strcmp(part_type, "output_text") != 0) &&
        (strcmp(part_type, "refusal") != 0) &&
        (strcmp(part_type, "reasoning_text") != 0) &&
        (strcmp(part_type, "summary_text") != 0)) {
      strappy_set_formatted_error(error_out,
                                  "Unsupported Responses text part type: %s",
                                  part_type);
      return 0;
    }
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 2, collection_name, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)ordinal) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 4, part_type, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 5, part_text, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare Responses text part: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    ok = sqlite3_step(stmt) == SQLITE_DONE;
    text_part_id = (long long)sqlite3_last_insert_rowid(db);
    if (!ok) {
      strappy_set_formatted_error(error_out,
                                  "Could not save Responses text part: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
    if (!strappy_db_semantic_insert_citations(db,
                                              text_part_id,
                                              annotations,
                                              error_out)) {
      return 0;
    }
    ordinal++;
    part = cJSON_IsArray(parts) ? part->next : NULL;
  }
  return 1;
}

static int strappy_db_semantic_insert_item(sqlite3 *db,
                                           long long session_id,
                                           long long turn_id,
                                           long long introduced_request_id,
                                           long long source_attempt_id,
                                           long item_index,
                                           int include_in_context,
                                           int timeline_visible,
                                           cJSON *item,
                                           long long *item_id_out,
                                           char **error_out)
{
  static const char *item_sql =
    "INSERT INTO conversation_items "
    "(session_id, turn_id, sequence, kind, introduced_request_id, "
     "source_attempt_id, source_item_index, provider_item_id, provider_status, "
     "include_in_context, timeline_visible, is_error, created_at_ms) "
    "SELECT ?, ?, COALESCE(MAX(sequence), 0) + 1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ? "
    "FROM conversation_items WHERE session_id = ?;";
  static const char *message_sql =
    "INSERT INTO message_items (item_id, role, phase) VALUES (?, ?, ?);";
  static const char *reasoning_sql =
    "INSERT INTO reasoning_items "
    "(item_id, encrypted_content, provider_format, provider_signature) "
    "VALUES (?, ?, ?, ?);";
  static const char *call_sql =
    "INSERT INTO function_calls "
    "(item_id, provider_call_id, tool_name, tool_namespace) "
    "VALUES (?, ?, ?, ?);";
  static const char *find_call_sql =
    "SELECT item_id FROM function_calls WHERE provider_call_id = ?;";
  static const char *output_sql =
    "INSERT INTO function_outputs "
    "(item_id, function_call_item_id, execution_state, started_at_ms, "
     "completed_at_ms, output_format, text_output, error_code, error_message) "
    "SELECT ?, f.item_id, COALESCE(e.state, 'completed'), e.started_at_ms, "
     "e.completed_at_ms, ?, ?, e.error_code, e.error_message "
    "FROM function_calls f LEFT JOIN tool_executions e "
     "ON e.function_call_item_id = f.item_id WHERE f.item_id = ?;";
  static const char *mark_output_error_sql =
    "UPDATE conversation_items SET is_error = 1 WHERE id = ? AND EXISTS ("
    "SELECT 1 FROM function_outputs o WHERE o.item_id = ? "
    "AND o.execution_state IN ('error','cancelled'));";
  static const char *search_sql =
    "INSERT INTO web_searches (item_id, action_type, query) VALUES (?, ?, ?);";
  static const char *source_sql =
    "INSERT INTO web_search_sources "
    "(web_search_item_id, ordinal, source_type, url) VALUES (?, ?, ?, ?);";
  static const char *fetch_sql =
    "INSERT INTO web_fetches (item_id, url, title, content, http_status) "
    "VALUES (?, ?, ?, ?, ?);";
  sqlite3_stmt *stmt;
  cJSON *type;
  cJSON *provider_id;
  cJSON *status;
  const char *type_text;
  const char *provider_id_text;
  const char *status_text;
  long long item_id;
  int rc;
  int ok;

  if (!cJSON_IsObject(item)) {
    strappy_set_error(error_out, "Responses item is not an object.");
    return 0;
  }
  type = cJSON_GetObjectItem(item, "type");
  provider_id = cJSON_GetObjectItem(item, "id");
  status = cJSON_GetObjectItem(item, "status");
  type_text = (cJSON_IsString(type) && (type->valuestring != NULL)) ?
    type->valuestring : NULL;
  provider_id_text = (cJSON_IsString(provider_id) &&
                      (provider_id->valuestring != NULL)) ?
    provider_id->valuestring : NULL;
  status_text = (cJSON_IsString(status) && (status->valuestring != NULL)) ?
    status->valuestring : NULL;
  if ((type_text == NULL) ||
      ((strcmp(type_text, "message") != 0) &&
       (strcmp(type_text, "reasoning") != 0) &&
       (strcmp(type_text, "function_call") != 0) &&
       (strcmp(type_text, "function_call_output") != 0) &&
       (strcmp(type_text, "openrouter:web_search") != 0) &&
       (strcmp(type_text, "openrouter:web_fetch") != 0))) {
    strappy_set_formatted_error(error_out,
                                "Unsupported context-bearing Responses item type: %s",
                                (type_text != NULL) ? type_text : "missing");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, item_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare conversation item insert: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  ok = (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)turn_id) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 3, type_text, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
       ((introduced_request_id > 0LL) ?
          (sqlite3_bind_int64(stmt, 4,
                              (sqlite3_int64)introduced_request_id) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 4) == SQLITE_OK)) &&
       ((source_attempt_id > 0LL) ?
          (sqlite3_bind_int64(stmt, 5,
                              (sqlite3_int64)source_attempt_id) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 5) == SQLITE_OK)) &&
       (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)item_index) == SQLITE_OK) &&
       strappy_db_bind_nullable_text_value(stmt, 7, provider_id_text) &&
       strappy_db_bind_nullable_text_value(stmt, 8, status_text) &&
       (sqlite3_bind_int(stmt, 9, include_in_context ? 1 : 0) == SQLITE_OK) &&
       (sqlite3_bind_int(stmt, 10, timeline_visible ? 1 : 0) == SQLITE_OK) &&
       (sqlite3_bind_int(stmt, 11,
                         strappy_db_response_item_is_error(item)) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 12,
                           (sqlite3_int64)strappy_db_now_ms()) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 13, (sqlite3_int64)session_id) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save conversation item: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  item_id = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);

  if (strcmp(type_text, "message") == 0) {
    cJSON *role;
    cJSON *content;
    cJSON *phase;
    const char *role_text;
    const char *default_part_type;

    role = cJSON_GetObjectItem(item, "role");
    content = cJSON_GetObjectItem(item, "content");
    phase = cJSON_GetObjectItem(item, "phase");
    role_text = (cJSON_IsString(role) && (role->valuestring != NULL)) ?
      role->valuestring : NULL;
    if ((role_text == NULL) ||
        ((strcmp(role_text, "user") != 0) &&
         (strcmp(role_text, "assistant") != 0) &&
         (strcmp(role_text, "developer") != 0) &&
         (strcmp(role_text, "system") != 0))) {
      strappy_set_error(error_out, "Responses message has an unsupported role.");
      return 0;
    }
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, message_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 2, role_text, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
        !strappy_db_bind_nullable_text_value(
          stmt, 3, cJSON_IsString(phase) ? phase->valuestring : NULL) ||
        (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not save message item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
    default_part_type = (strcmp(role_text, "assistant") == 0) ?
      "output_text" : "input_text";
    if (!strappy_db_semantic_insert_text_parts(db,
                                               item_id,
                                               "content",
                                               content,
                                               default_part_type,
                                               error_out)) {
      return 0;
    }
  } else if (strcmp(type_text, "reasoning") == 0) {
    cJSON *encrypted;
    cJSON *format;
    cJSON *signature;

    encrypted = cJSON_GetObjectItem(item, "encrypted_content");
    format = cJSON_GetObjectItem(item, "format");
    signature = cJSON_GetObjectItem(item, "signature");
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, reasoning_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        !strappy_db_bind_nullable_text_value(
          stmt, 2, cJSON_IsString(encrypted) ? encrypted->valuestring : NULL) ||
        !strappy_db_bind_nullable_text_value(
          stmt, 3, cJSON_IsString(format) ? format->valuestring : NULL) ||
        !strappy_db_bind_nullable_text_value(
          stmt, 4, cJSON_IsString(signature) ? signature->valuestring : NULL) ||
        (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not save reasoning item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
    if (!strappy_db_semantic_insert_text_parts(
          db, item_id, "content", cJSON_GetObjectItem(item, "content"),
          "reasoning_text", error_out) ||
        !strappy_db_semantic_insert_text_parts(
          db, item_id, "summary", cJSON_GetObjectItem(item, "summary"),
          "summary_text", error_out)) {
      return 0;
    }
  } else if (strcmp(type_text, "function_call") == 0) {
    cJSON *call_id;
    cJSON *name;
    cJSON *arguments;
    cJSON *tool_namespace;
    cJSON *document;

    call_id = cJSON_GetObjectItem(item, "call_id");
    name = cJSON_GetObjectItem(item, "name");
    arguments = cJSON_GetObjectItem(item, "arguments");
    tool_namespace = cJSON_GetObjectItem(item, "namespace");
    if (!cJSON_IsString(call_id) || !cJSON_IsString(name) ||
        !cJSON_IsString(arguments)) {
      strappy_set_error(error_out, "Function call item is incomplete.");
      return 0;
    }
    document = cJSON_Parse(arguments->valuestring);
    if (document == NULL) {
      strappy_set_error(error_out,
                        "Function call arguments are not valid structured data.");
      return 0;
    }
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, call_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 2, call_id->valuestring, -1,
                           SQLITE_TRANSIENT) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 3, name->valuestring, -1,
                           SQLITE_TRANSIENT) != SQLITE_OK) ||
        !strappy_db_bind_nullable_text_value(
          stmt, 4,
          cJSON_IsString(tool_namespace) ? tool_namespace->valuestring : NULL) ||
        (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not save function call item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      cJSON_Delete(document);
      return 0;
    }
    sqlite3_finalize(stmt);
    ok = strappy_db_semantic_insert_document(db,
                                              item_id,
                                              "arguments",
                                              document,
                                              error_out);
    cJSON_Delete(document);
    if (!ok) {
      return 0;
    }
  } else if (strcmp(type_text, "function_call_output") == 0) {
    cJSON *call_id;
    cJSON *output;
    cJSON *document;
    const char *output_text;
    const char *output_format;
    long long call_item_id;

    call_id = cJSON_GetObjectItem(item, "call_id");
    output = cJSON_GetObjectItem(item, "output");
    if (!cJSON_IsString(call_id) || (output == NULL)) {
      strappy_set_error(error_out, "Function output item is incomplete.");
      return 0;
    }
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, find_call_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 1, call_id->valuestring, -1,
                           SQLITE_TRANSIENT) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_ROW)) {
      strappy_set_formatted_error(error_out,
                                  "Function output call was not found: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    call_item_id = (long long)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    document = cJSON_IsString(output) ? cJSON_Parse(output->valuestring) :
      cJSON_Duplicate(output, 1);
    output_format = (document != NULL) ? "structured" : "text";
    output_text = (document == NULL) && cJSON_IsString(output) ?
      output->valuestring : NULL;
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, output_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 2, output_format, -1,
                           SQLITE_TRANSIENT) != SQLITE_OK) ||
        !strappy_db_bind_nullable_text_value(stmt, 3, output_text) ||
        (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)call_item_id) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not save function output item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      cJSON_Delete(document);
      return 0;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, mark_output_error_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not classify function output item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      cJSON_Delete(document);
      return 0;
    }
    sqlite3_finalize(stmt);
    if (document != NULL) {
      ok = strappy_db_semantic_insert_document(db,
                                                item_id,
                                                "output",
                                                document,
                                                error_out);
      cJSON_Delete(document);
      if (!ok) {
        return 0;
      }
    }
  } else if (strcmp(type_text, "openrouter:web_search") == 0) {
    cJSON *action;
    cJSON *action_type;
    cJSON *query;
    cJSON *sources;
    cJSON *source;
    const char *action_type_text;
    long source_ordinal;

    action = cJSON_GetObjectItem(item, "action");
    action_type = cJSON_IsObject(action) ?
      cJSON_GetObjectItem(action, "type") : NULL;
    query = cJSON_IsObject(action) ? cJSON_GetObjectItem(action, "query") : NULL;
    sources = cJSON_IsObject(action) ?
      cJSON_GetObjectItem(action, "sources") : NULL;
    action_type_text = (cJSON_IsString(action_type) &&
                        (action_type->valuestring != NULL)) ?
      action_type->valuestring : "search";
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, search_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 2, action_type_text, -1,
                           SQLITE_TRANSIENT) != SQLITE_OK) ||
        !strappy_db_bind_nullable_text_value(
          stmt, 3, cJSON_IsString(query) ? query->valuestring : NULL) ||
        (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not save web-search item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
    source_ordinal = 0L;
    for (source = cJSON_IsArray(sources) ? sources->child : NULL;
         source != NULL;
         source = source->next) {
      cJSON *source_type;
      cJSON *url;
      const char *source_type_text;

      source_type = cJSON_IsObject(source) ?
        cJSON_GetObjectItem(source, "type") : NULL;
      url = cJSON_IsObject(source) ? cJSON_GetObjectItem(source, "url") : NULL;
      if (!cJSON_IsString(url)) {
        continue;
      }
      source_type_text = cJSON_IsString(source_type) ?
        source_type->valuestring : "url";
      stmt = NULL;
      rc = sqlite3_prepare_v2(db, source_sql, -1, &stmt, NULL);
      if ((rc != SQLITE_OK) ||
          (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
          (sqlite3_bind_int64(stmt, 2,
                              (sqlite3_int64)source_ordinal) != SQLITE_OK) ||
          (sqlite3_bind_text(stmt, 3, source_type_text, -1,
                             SQLITE_TRANSIENT) != SQLITE_OK) ||
          (sqlite3_bind_text(stmt, 4, url->valuestring, -1,
                             SQLITE_TRANSIENT) != SQLITE_OK) ||
          (sqlite3_step(stmt) != SQLITE_DONE)) {
        strappy_set_formatted_error(error_out,
                                    "Could not save web-search source: %s",
                                    sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 0;
      }
      sqlite3_finalize(stmt);
      source_ordinal++;
    }
  } else {
    cJSON *url;
    cJSON *title;
    cJSON *content;
    cJSON *http_status;

    url = cJSON_GetObjectItem(item, "url");
    title = cJSON_GetObjectItem(item, "title");
    content = cJSON_GetObjectItem(item, "content");
    http_status = cJSON_GetObjectItem(item, "httpStatus");
    if (!cJSON_IsString(url)) {
      strappy_set_error(error_out, "Web-fetch item URL is missing.");
      return 0;
    }
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, fetch_sql, -1, &stmt, NULL);
    ok = (rc == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) == SQLITE_OK) &&
         (sqlite3_bind_text(stmt, 2, url->valuestring, -1,
                            SQLITE_TRANSIENT) == SQLITE_OK) &&
         strappy_db_bind_nullable_text_value(
           stmt, 3, cJSON_IsString(title) ? title->valuestring : NULL) &&
         strappy_db_bind_nullable_text_value(
           stmt, 4, cJSON_IsString(content) ? content->valuestring : NULL) &&
         (cJSON_IsNumber(http_status) ?
            (sqlite3_bind_int64(stmt, 5,
                                (sqlite3_int64)http_status->valuedouble) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 5) == SQLITE_OK));
    if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not save web-fetch item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
  }
  if (item_id_out != NULL) {
    *item_id_out = item_id;
  }
  return 1;
}

static int strappy_db_semantic_response_request_id(sqlite3 *db,
                                                   long long attempt_id,
                                                   long long session_id,
                                                   long long *request_id_out,
                                                   long long *turn_id_out,
                                                   char **error_out)
{
  static const char *sql =
    "SELECT a.request_id, r.turn_id "
    "FROM http_attempts a "
    "JOIN model_requests r ON r.id = a.request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "WHERE a.id = ? AND t.session_id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if ((request_id_out == NULL) || (turn_id_out == NULL)) {
    strappy_set_error(error_out, "Responses request lookup output is missing.");
    return 0;
  }
  *request_id_out = 0LL;
  *turn_id_out = 0LL;
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)attempt_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare Responses request lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    if (rc == SQLITE_DONE) {
      strappy_set_error(error_out,
                        "The previous Responses attempt was not found in this session.");
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not read Responses request lookup: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  *request_id_out = (long long)sqlite3_column_int64(stmt, 0);
  *turn_id_out = (long long)sqlite3_column_int64(stmt, 1);
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_semantic_request_web_provider(
  cJSON *tools,
  strappy_web_provider *provider_out,
  char **error_out)
{
  cJSON *tool;
  strappy_web_provider selected_provider;

  if (provider_out == NULL) {
    strappy_set_error(error_out, "Responses web provider output is missing.");
    return 0;
  }
  selected_provider = STRAPPY_WEB_PROVIDER_NONE;

  for (tool = cJSON_IsArray(tools) ? tools->child : NULL;
       tool != NULL;
       tool = tool->next) {
    cJSON *type;
    cJSON *parameters;
    cJSON *engine;
    const char *type_text;
    const char *engine_text;
    strappy_web_provider provider;

    type = cJSON_IsObject(tool) ? cJSON_GetObjectItem(tool, "type") : NULL;
    if ((type == NULL) || !cJSON_IsString(type)) {
      continue;
    }
    type_text = type->valuestring;
    if (type_text == NULL) {
      continue;
    }
    if ((strcmp(type_text, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) != 0) &&
        (strcmp(type_text, STRAPPY_TOOL_OPENROUTER_WEB_FETCH) != 0)) {
      continue;
    }
    parameters = cJSON_GetObjectItemCaseSensitive(tool, "parameters");
    engine = cJSON_IsObject(parameters) ?
      cJSON_GetObjectItemCaseSensitive(parameters, "engine") : NULL;
    if ((engine == NULL) || !cJSON_IsString(engine)) {
      strappy_set_error(error_out,
                        "Responses web tool engine is missing or invalid.");
      return 0;
    }
    engine_text = engine->valuestring;
    if ((engine_text == NULL) ||
        !strappy_web_provider_parse(engine_text, &provider) ||
        !strappy_web_provider_is_enabled(provider)) {
      strappy_set_error(error_out,
                        "Responses web tool engine is missing or invalid.");
      return 0;
    }
    if ((selected_provider != STRAPPY_WEB_PROVIDER_NONE) &&
        (selected_provider != provider)) {
      strappy_set_error(error_out,
                        "Responses web tools use different providers.");
      return 0;
    }
    selected_provider = provider;
  }
  *provider_out = selected_provider;
  return 1;
}

static int strappy_db_semantic_begin_response_call(
  const char *db_path,
  const strappy_response_call_begin_input *input,
  long long *call_id_out,
  char **error_out)
{
  static const char *insert_model_sql =
    "INSERT OR IGNORE INTO models "
    "(id, name, catalog_active, last_seen_at_ms) VALUES (?, ?, 1, ?);";
  static const char *insert_request_sql =
    "INSERT INTO model_requests "
    "(turn_id, previous_request_id, round_index, request_kind, model_id, "
     "instruction_revision_id, toolset_revision_id, input_from_sequence, "
     "input_through_sequence, new_input_from_sequence, max_output_tokens, "
     "temperature_millionths, web_provider, stream_enabled, "
     "reasoning_enabled, reasoning_summary, tool_calls_enabled, state, "
     "created_at_ms) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, NULL, ?, ?, ?, ?, ?, ?, ?, "
     "'running', ?);";
  static const char *update_request_sql =
    "UPDATE model_requests SET input_from_sequence = ?, "
    "input_through_sequence = ?, new_input_from_sequence = ? WHERE id = ?;";
  static const char *insert_attempt_sql =
    "INSERT INTO http_attempts "
    "(request_id, previous_attempt_id, attempt_index, state, method, endpoint, "
     "started_at_ms) VALUES (?, ?, ?, 'running', ?, ?, ?);";
  static const char *update_session_sql =
    "UPDATE sessions SET updated_at_ms = ? WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  cJSON *root;
  cJSON *request_input;
  cJSON *instructions;
  cJSON *tools;
  cJSON *model;
  cJSON *max_output_tokens;
  cJSON *temperature;
  cJSON *stream;
  cJSON *reasoning;
  cJSON *reasoning_summary;
  const char *model_id;
  const char *summary_text;
  const char *web_provider_name;
  long long now_ms;
  long long turn_id;
  long long request_id;
  long long previous_request_id;
  long long instruction_revision_id;
  long long toolset_revision_id;
  long long input_from_sequence;
  long long input_through_sequence;
  long long new_input_from_sequence;
  long long call_id;
  long item_index;
  strappy_web_provider web_provider;
  int rc;
  int ok;

  if (call_id_out != NULL) {
    *call_id_out = 0LL;
  }
  if ((input == NULL) || (input->session_id <= 0LL) ||
      (input->prompt_group_key == NULL) ||
      (input->prompt_group_key[0] == '\0') ||
      (input->request_kind == NULL) ||
      (input->request_kind[0] == '\0') ||
      (input->request_method == NULL) ||
      (input->request_method[0] == '\0') ||
      (input->request_url == NULL) ||
      (input->request_url[0] == '\0') ||
      (input->request_json == NULL) ||
      (input->request_json[0] == '\0') ||
      (input->round_index < 0L) || (input->attempt_index < 0L)) {
    strappy_set_error(error_out, "Responses call request is incomplete.");
    return 0;
  }
  root = cJSON_Parse(input->request_json);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_error(error_out, "Responses request is not valid JSON data.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    cJSON_Delete(root);
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_session_exists(db, input->session_id, error_out) ||
      !strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin Responses call",
                       error_out)) {
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }

  now_ms = strappy_db_now_ms();
  turn_id = 0LL;
  request_id = 0LL;
  previous_request_id = 0LL;
  instruction_revision_id = 0LL;
  toolset_revision_id = 0LL;
  if (input->attempt_index > 0L) {
    if ((input->previous_call_id <= 0LL) ||
        !strappy_db_semantic_response_request_id(db,
                                                 input->previous_call_id,
                                                 input->session_id,
                                                 &request_id,
                                                 &turn_id,
                                                 error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
      strappy_db_release(db);
      cJSON_Delete(root);
      return 0;
    }
  } else {
    instructions = cJSON_GetObjectItem(root, "instructions");
    tools = cJSON_GetObjectItem(root, "tools");
    model = cJSON_GetObjectItem(root, "model");
    max_output_tokens = cJSON_GetObjectItem(root, "max_output_tokens");
    temperature = cJSON_GetObjectItem(root, "temperature");
    stream = cJSON_GetObjectItem(root, "stream");
    reasoning = cJSON_GetObjectItem(root, "reasoning");
    reasoning_summary = cJSON_IsObject(reasoning) ?
      cJSON_GetObjectItem(reasoning, "summary") : NULL;
    model_id = (cJSON_IsString(model) && (model->valuestring != NULL) &&
                (model->valuestring[0] != '\0')) ?
      model->valuestring : STRAPPY_CONFIG_DEFAULT_API_MODEL;
    summary_text = (cJSON_IsString(reasoning_summary) &&
                    (reasoning_summary->valuestring != NULL)) ?
      reasoning_summary->valuestring : NULL;
    if (!strappy_db_semantic_request_web_provider(tools,
                                                   &web_provider,
                                                   error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
      strappy_db_release(db);
      cJSON_Delete(root);
      return 0;
    }
    web_provider_name = strappy_web_provider_name(web_provider);
    if ((web_provider_name == NULL) ||
        !strappy_db_semantic_turn(db,
                                  input->session_id,
                                  input->prompt_group_key,
                                  &turn_id,
                                  error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
      strappy_db_release(db);
      cJSON_Delete(root);
      return 0;
    }
    if ((input->previous_call_id > 0LL) &&
        !strappy_db_semantic_response_request_id(db,
                                                 input->previous_call_id,
                                                 input->session_id,
                                                 &previous_request_id,
                                                 &call_id,
                                                 error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
      strappy_db_release(db);
      cJSON_Delete(root);
      return 0;
    }
    if (!strappy_db_semantic_instruction_revision(
          db,
          cJSON_IsString(instructions) ? instructions->valuestring : NULL,
          &instruction_revision_id,
          error_out) ||
        !strappy_db_semantic_toolset_revision(db,
                                              tools,
                                              &toolset_revision_id,
                                              error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
      strappy_db_release(db);
      cJSON_Delete(root);
      return 0;
    }
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, insert_model_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 2, model_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ms) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not retain Responses model: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
      strappy_db_release(db);
      cJSON_Delete(root);
      return 0;
    }
    sqlite3_finalize(stmt);

    stmt = NULL;
    rc = sqlite3_prepare_v2(db, insert_request_sql, -1, &stmt, NULL);
    ok = (rc == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)turn_id) == SQLITE_OK) &&
         ((previous_request_id > 0LL) ?
            (sqlite3_bind_int64(stmt, 2,
                                (sqlite3_int64)previous_request_id) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 2) == SQLITE_OK)) &&
         (sqlite3_bind_int64(stmt, 3,
                             (sqlite3_int64)input->round_index) == SQLITE_OK) &&
         (sqlite3_bind_text(stmt, 4, input->request_kind, -1,
                            SQLITE_TRANSIENT) == SQLITE_OK) &&
         (sqlite3_bind_text(stmt, 5, model_id, -1, SQLITE_TRANSIENT) == SQLITE_OK) &&
         ((instruction_revision_id > 0LL) ?
            (sqlite3_bind_int64(stmt, 6,
                                (sqlite3_int64)instruction_revision_id) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 6) == SQLITE_OK)) &&
         ((toolset_revision_id > 0LL) ?
            (sqlite3_bind_int64(stmt, 7,
                                (sqlite3_int64)toolset_revision_id) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 7) == SQLITE_OK)) &&
         (sqlite3_bind_int64(stmt, 8, 1) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 9, 0) == SQLITE_OK) &&
         (cJSON_IsNumber(max_output_tokens) ?
            (sqlite3_bind_int64(stmt, 10,
                                (sqlite3_int64)max_output_tokens->valuedouble) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 10) == SQLITE_OK)) &&
         (cJSON_IsNumber(temperature) ?
            (sqlite3_bind_int64(stmt, 11,
                                (sqlite3_int64)(temperature->valuedouble *
                                                1000000.0)) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 11) == SQLITE_OK)) &&
         (sqlite3_bind_text(stmt,
                            12,
                            web_provider_name,
                            -1,
                            SQLITE_TRANSIENT) == SQLITE_OK) &&
         (sqlite3_bind_int(stmt, 13,
                           cJSON_IsTrue(stream) ? 1 : 0) == SQLITE_OK) &&
         (sqlite3_bind_int(stmt, 14,
                           (reasoning == NULL || cJSON_IsNull(reasoning)) ? 0 : 1) == SQLITE_OK) &&
         strappy_db_bind_nullable_text_value(stmt, 15, summary_text) &&
         (sqlite3_bind_int(stmt, 16,
                           cJSON_IsArray(tools) &&
                           (cJSON_GetArraySize(tools) > 0) ? 1 : 0) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 17, (sqlite3_int64)now_ms) == SQLITE_OK);
    if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not save semantic Responses request: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
      strappy_db_release(db);
      cJSON_Delete(root);
      return 0;
    }
    request_id = (long long)sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);

    request_input = cJSON_GetObjectItem(root, "input");
    item_index = 0L;
    if (cJSON_IsArray(request_input)) {
      cJSON *item;

      for (item = request_input->child; item != NULL; item = item->next) {
        if ((input->new_input_start_index >= 0L) &&
            (item_index >= input->new_input_start_index) &&
            !strappy_db_semantic_insert_item(db,
                                             input->session_id,
                                             turn_id,
                                             request_id,
                                             0LL,
                                             item_index,
                                             1,
                                             1,
                                             item,
                                             NULL,
                                             error_out)) {
          strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
          strappy_db_release(db);
          cJSON_Delete(root);
          return 0;
        }
        item_index++;
      }
    } else if ((request_input != NULL) &&
               (input->new_input_start_index == 0L) &&
               !strappy_db_semantic_insert_item(db,
                                                input->session_id,
                                                turn_id,
                                                request_id,
                                                0LL,
                                                0L,
                                                1,
                                                1,
                                                request_input,
                                                NULL,
                                                error_out)) {
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
      strappy_db_release(db);
      cJSON_Delete(root);
      return 0;
    }

    stmt = NULL;
    rc = sqlite3_prepare_v2(
      db,
      input->input_from_current_turn ?
        "SELECT COALESCE(MIN(i.sequence), 1), "
        "COALESCE(MAX(i.sequence), 0) FROM conversation_items i "
        "JOIN turns t ON t.id = i.turn_id "
        "WHERE i.session_id = ?1 AND t.prompt_group_key = ?2 "
        "AND i.include_in_context = 1;" :
        "SELECT 1, COALESCE(MAX(sequence), 0) FROM conversation_items "
        "WHERE session_id = ?1 AND include_in_context = 1;",
      -1,
      &stmt,
      NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1,
                            (sqlite3_int64)input->session_id) != SQLITE_OK) ||
        (input->input_from_current_turn &&
         (sqlite3_bind_text(stmt,
                            2,
                            input->prompt_group_key,
                            -1,
                            SQLITE_TRANSIENT) != SQLITE_OK)) ||
        (sqlite3_step(stmt) != SQLITE_ROW)) {
      strappy_set_formatted_error(error_out,
                                  "Could not measure Responses context: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
      strappy_db_release(db);
      cJSON_Delete(root);
      return 0;
    }
    input_from_sequence = (long long)sqlite3_column_int64(stmt, 0);
    input_through_sequence = (long long)sqlite3_column_int64(stmt, 1);
    sqlite3_finalize(stmt);
    new_input_from_sequence = (input->new_input_start_index >= 0L) ?
      input_through_sequence -
        ((item_index > input->new_input_start_index) ?
          (item_index - input->new_input_start_index) : 0L) + 1LL : 0LL;
    if ((input->new_input_start_index < 0L) ||
        (item_index <= input->new_input_start_index)) {
      new_input_from_sequence = 0LL;
    }
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, update_request_sql, -1, &stmt, NULL);
    ok = (rc == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 1,
                             (sqlite3_int64)input_from_sequence) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 2,
                             (sqlite3_int64)input_through_sequence) == SQLITE_OK) &&
         ((new_input_from_sequence > 0LL) ?
            (sqlite3_bind_int64(stmt, 3,
                                (sqlite3_int64)new_input_from_sequence) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 3) == SQLITE_OK)) &&
         (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)request_id) == SQLITE_OK);
    if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not finalize Responses context range: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
      strappy_db_release(db);
      cJSON_Delete(root);
      return 0;
    }
    sqlite3_finalize(stmt);
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_attempt_sql, -1, &stmt, NULL);
  ok = (rc == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)request_id) == SQLITE_OK) &&
       ((input->previous_call_id > 0LL) ?
          (sqlite3_bind_int64(stmt, 2,
                              (sqlite3_int64)input->previous_call_id) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 2) == SQLITE_OK)) &&
       (sqlite3_bind_int64(stmt, 3,
                           (sqlite3_int64)input->attempt_index) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 4, input->request_method, -1,
                          SQLITE_TRANSIENT) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 5, input->request_url, -1,
                          SQLITE_TRANSIENT) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 6, (sqlite3_int64)now_ms) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save Responses HTTP attempt: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }
  call_id = (long long)sqlite3_last_insert_rowid(db);
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_session_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)input->session_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not update Responses session: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }
  sqlite3_finalize(stmt);
  cJSON_Delete(root);
  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit Responses call",
                       error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
    strappy_db_release(db);
    return 0;
  }
  strappy_db_release(db);
  if (call_id_out != NULL) {
    *call_id_out = call_id;
  }
  return 1;
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

  return strappy_db_semantic_begin_response_call(db_path,
                                                 input,
                                                 call_id_out,
                                                 error_out);

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
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin Responses call",
                       error_out)) {
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
        strappy_db_release(db);
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
      strappy_db_release(db);
      return 0;
    }
  }
  cJSON_Delete(root);

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit Responses call",
                       error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses call", NULL);
    strappy_db_release(db);
    return 0;
  }
  strappy_db_release(db);
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

static const char *strappy_db_semantic_json_string(cJSON *root,
                                                   const char *path)
{
  cJSON *value;

  value = strappy_db_response_json_path(root, path);
  return (cJSON_IsString(value) && (value->valuestring != NULL)) ?
    value->valuestring : NULL;
}

static long long strappy_db_semantic_json_integer(cJSON *root,
                                                  const char *path)
{
  cJSON *value;

  value = strappy_db_response_json_path(root, path);
  return cJSON_IsNumber(value) ? (long long)value->valuedouble : 0LL;
}

static int strappy_db_semantic_json_nano_usd(cJSON *root,
                                             const char *path,
                                             long long *value_out)
{
  cJSON *value;
  double scaled;

  if (value_out == NULL) {
    return 0;
  }
  *value_out = 0LL;
  value = strappy_db_response_json_path(root, path);
  if (!cJSON_IsNumber(value)) {
    return 0;
  }
  scaled = value->valuedouble * 1000000000.0;
  *value_out = (long long)(scaled + ((scaled >= 0.0) ? 0.5 : -0.5));
  return 1;
}

static const char *strappy_db_semantic_attempt_state(const char *state)
{
  if (state == NULL) {
    return "transport_error";
  }
  if (strcmp(state, "client_error") == 0) {
    return "transport_error";
  }
  if (strcmp(state, "invalid_json") == 0) {
    return "parse_error";
  }
  if ((strcmp(state, "pending") == 0) ||
      (strcmp(state, "running") == 0) ||
      (strcmp(state, "completed") == 0) ||
      (strcmp(state, "http_error") == 0) ||
      (strcmp(state, "response_error") == 0) ||
      (strcmp(state, "transport_error") == 0) ||
      (strcmp(state, "parse_error") == 0) ||
      (strcmp(state, "cancelled") == 0)) {
    return state;
  }
  return "transport_error";
}

static int strappy_db_semantic_attempt_context(sqlite3 *db,
                                               long long attempt_id,
                                               long long *request_id_out,
                                               long long *turn_id_out,
                                               long long *session_id_out,
                                               char **error_out)
{
  static const char *sql =
    "SELECT a.request_id, r.turn_id, t.session_id "
    "FROM http_attempts a "
    "JOIN model_requests r ON r.id = a.request_id "
    "JOIN turns t ON t.id = r.turn_id WHERE a.id = ?;";
  sqlite3_stmt *stmt;
  int rc;

  if ((request_id_out == NULL) || (turn_id_out == NULL) ||
      (session_id_out == NULL)) {
    strappy_set_error(error_out, "Responses attempt context output is missing.");
    return 0;
  }
  *request_id_out = 0LL;
  *turn_id_out = 0LL;
  *session_id_out = 0LL;
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)attempt_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare Responses attempt context: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    if (rc == SQLITE_DONE) {
      strappy_set_error(error_out, "Responses attempt was not found.");
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not read Responses attempt context: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return 0;
  }
  *request_id_out = (long long)sqlite3_column_int64(stmt, 0);
  *turn_id_out = (long long)sqlite3_column_int64(stmt, 1);
  *session_id_out = (long long)sqlite3_column_int64(stmt, 2);
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_semantic_insert_answer_quality(
  sqlite3 *db,
  long long attempt_id,
  const strappy_answer_quality_audit_input *audit,
  char **error_out)
{
  static const char *insert_audit_sql =
    "INSERT INTO answer_quality_audits "
    "(response_attempt_id, outcome, guidance_version, evaluated_at_ms) "
    "VALUES (?, ?, ?, ?);";
  static const char *insert_check_sql =
    "INSERT INTO answer_quality_checks "
    "(audit_id, ordinal, check_key, check_kind, label, status, tool_name, "
     "detail, evidence_item_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt *stmt;
  sqlite3_int64 audit_id;
  size_t index;
  int rc;
  int ok;

  if (audit == NULL) {
    return 1;
  }
  if ((audit->outcome == NULL) || (audit->outcome[0] == '\0') ||
      (audit->evaluated_at_ms <= 0LL) || (audit->checks == NULL) ||
      (audit->check_count == 0U) ||
      (audit->check_count > (size_t)LLONG_MAX)) {
    strappy_set_error(error_out, "Answer quality result is incomplete.");
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_audit_sql, -1, &stmt, NULL);
  ok = (rc == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 1,
                           (sqlite3_int64)attempt_id) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 2, audit->outcome, -1,
                          SQLITE_TRANSIENT) == SQLITE_OK) &&
       strappy_db_bind_nullable_text_value(stmt,
                                           3,
                                           audit->guidance_version) &&
       (sqlite3_bind_int64(stmt, 4,
                           (sqlite3_int64)audit->evaluated_at_ms) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save answer quality audit: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  sqlite3_finalize(stmt);
  audit_id = sqlite3_last_insert_rowid(db);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_check_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare answer quality checks: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  for (index = 0U; index < audit->check_count; index++) {
    const strappy_answer_quality_check_input *check;

    check = &audit->checks[index];
    if ((check->check_key == NULL) || (check->check_key[0] == '\0') ||
        (check->check_kind == NULL) || (check->check_kind[0] == '\0') ||
        (check->label == NULL) || (check->label[0] == '\0') ||
        (check->status == NULL) || (check->status[0] == '\0')) {
      strappy_set_error(error_out, "Answer quality check is incomplete.");
      sqlite3_finalize(stmt);
      return 0;
    }
    ok = (sqlite3_reset(stmt) == SQLITE_OK) &&
         (sqlite3_clear_bindings(stmt) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 1, audit_id) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 2,
                             (sqlite3_int64)index) == SQLITE_OK) &&
         (sqlite3_bind_text(stmt, 3, check->check_key, -1,
                            SQLITE_TRANSIENT) == SQLITE_OK) &&
         (sqlite3_bind_text(stmt, 4, check->check_kind, -1,
                            SQLITE_TRANSIENT) == SQLITE_OK) &&
         (sqlite3_bind_text(stmt, 5, check->label, -1,
                            SQLITE_TRANSIENT) == SQLITE_OK) &&
         (sqlite3_bind_text(stmt, 6, check->status, -1,
                            SQLITE_TRANSIENT) == SQLITE_OK) &&
         strappy_db_bind_nullable_text_value(stmt, 7, check->tool_name) &&
         strappy_db_bind_nullable_text_value(stmt, 8, check->detail) &&
         ((check->evidence_item_id > 0LL) ?
            (sqlite3_bind_int64(
               stmt,
               9,
               (sqlite3_int64)check->evidence_item_id) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 9) == SQLITE_OK));
    if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not save answer quality check: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_semantic_finish_response_call(
  const char *db_path,
  const strappy_response_call_finish_input *input,
  char **error_out)
{
  static const char *update_attempt_sql =
    "UPDATE http_attempts SET state = ?, started_at_ms = ?, "
    "completed_at_ms = ?, http_status = ?, curl_code = ?, "
    "retry_after_seconds = ?, request_bytes = ?, response_bytes = ?, "
    "name_lookup_us = ?, connect_us = ?, start_transfer_us = ?, total_us = ?, "
    "effective_url = ?, transport_error = ?, content_type = ?, "
    "provider_request_id = ?, provider_generation_id = ?, "
    "rate_limit_limit = ?, rate_limit_remaining = ?, rate_limit_reset = ?, "
    "rate_limit_requests_limit = ?, rate_limit_requests_remaining = ?, "
    "rate_limit_requests_reset = ?, rate_limit_tokens_limit = ?, "
    "rate_limit_tokens_remaining = ?, rate_limit_tokens_reset = ? "
    "WHERE id = ?;";
  static const char *insert_result_sql =
    "INSERT OR REPLACE INTO api_results "
    "(attempt_id, provider_response_id, provider_model_id, provider_status, "
     "provider_created_at_s, provider_completed_at_s, incomplete_reason, "
     "error_type, error_code, error_message, error_parameter, parse_error) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  static const char *insert_usage_sql =
    "INSERT OR REPLACE INTO api_usage "
    "(attempt_id, input_tokens, cached_input_tokens, output_tokens, "
     "reasoning_tokens, total_tokens, cost_nano_usd, "
     "upstream_cost_nano_usd, upstream_input_cost_nano_usd, "
     "upstream_output_cost_nano_usd, is_byok) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  static const char *update_request_sql =
    "UPDATE model_requests SET state = ?, completed_at_ms = ? WHERE id = ?;";
  static const char *update_session_sql =
    "UPDATE sessions SET updated_at_ms = ? WHERE id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  cJSON *root;
  cJSON *output;
  cJSON *error;
  cJSON *error_parameter;
  cJSON *usage;
  const char *attempt_state;
  const char *request_state;
  const char *error_type;
  const char *parse_error;
  long long request_id;
  long long turn_id;
  long long session_id;
  long long cost;
  long long upstream_cost;
  long long upstream_input_cost;
  long long upstream_output_cost;
  int has_cost;
  int has_upstream_cost;
  int has_upstream_input_cost;
  int has_upstream_output_cost;
  int parameter;
  int rc;
  int ok;

  if ((input == NULL) || (input->call_id <= 0LL) ||
      (input->state == NULL) || (input->state[0] == '\0') ||
      (input->response_json == NULL)) {
    strappy_set_error(error_out, "Responses call result is incomplete.");
    return 0;
  }
  root = (input->response_json[0] != '\0') ?
    cJSON_Parse(input->response_json) : NULL;
  parse_error = ((input->response_json[0] != '\0') && (root == NULL)) ?
    "The response body was not valid JSON." : NULL;
  attempt_state = strappy_db_semantic_attempt_state(input->state);
  if (parse_error != NULL) {
    attempt_state = "parse_error";
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    cJSON_Delete(root);
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_semantic_attempt_context(db,
                                           input->call_id,
                                           &request_id,
                                           &turn_id,
                                           &session_id,
                                           error_out) ||
      !strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin Responses call result",
                       error_out)) {
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_attempt_sql, -1, &stmt, NULL);
  parameter = 1;
  ok = (rc == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, parameter++, attempt_state, -1,
                          SQLITE_TRANSIENT) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, parameter++,
                           (sqlite3_int64)input->started_at_ms) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, parameter++,
                           (sqlite3_int64)input->completed_at_ms) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, parameter++,
                           (sqlite3_int64)input->http_status) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, parameter++,
                           (sqlite3_int64)input->curl_code) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, parameter++,
                           (sqlite3_int64)input->retry_after_seconds) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, parameter++,
                           (sqlite3_int64)input->request_bytes) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, parameter++,
                           (sqlite3_int64)input->response_bytes) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, parameter++,
                           (sqlite3_int64)(input->name_lookup_seconds *
                                           1000000.0)) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, parameter++,
                           (sqlite3_int64)(input->connect_seconds *
                                           1000000.0)) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, parameter++,
                           (sqlite3_int64)(input->start_transfer_seconds *
                                           1000000.0)) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, parameter++,
                           (sqlite3_int64)(input->total_seconds *
                                           1000000.0)) == SQLITE_OK) &&
       strappy_db_bind_nullable_text_value(stmt, parameter++, input->effective_url) &&
       strappy_db_bind_nullable_text_value(stmt, parameter++, input->transport_error) &&
       strappy_db_bind_nullable_text_value(stmt, parameter++, input->content_type) &&
       strappy_db_bind_nullable_text_value(stmt, parameter++, input->request_id) &&
       strappy_db_bind_nullable_text_value(stmt, parameter++, input->generation_id) &&
       strappy_db_bind_nullable_text_value(stmt, parameter++, input->rate_limit_limit) &&
       strappy_db_bind_nullable_text_value(stmt, parameter++, input->rate_limit_remaining) &&
       strappy_db_bind_nullable_text_value(stmt, parameter++, input->rate_limit_reset) &&
       strappy_db_bind_nullable_text_value(
         stmt, parameter++, input->rate_limit_limit_requests) &&
       strappy_db_bind_nullable_text_value(
         stmt, parameter++, input->rate_limit_remaining_requests) &&
       strappy_db_bind_nullable_text_value(
         stmt, parameter++, input->rate_limit_reset_requests) &&
       strappy_db_bind_nullable_text_value(
         stmt, parameter++, input->rate_limit_limit_tokens) &&
       strappy_db_bind_nullable_text_value(
         stmt, parameter++, input->rate_limit_remaining_tokens) &&
       strappy_db_bind_nullable_text_value(
         stmt, parameter++, input->rate_limit_reset_tokens) &&
       (sqlite3_bind_int64(stmt, parameter++,
                           (sqlite3_int64)input->call_id) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save Responses HTTP result: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses result", NULL);
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }
  sqlite3_finalize(stmt);

  error = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "error") : NULL;
  error_parameter = cJSON_IsObject(error) ?
    cJSON_GetObjectItem(error, "param") : NULL;
  if (error_parameter == NULL) {
    error_parameter = cJSON_IsObject(error) ?
      cJSON_GetObjectItem(error, "parameter") : NULL;
  }
  error_type = strappy_db_semantic_json_string(root, "error.type");
  if (error_type == NULL) {
    error_type = strappy_db_semantic_json_string(root, "error_type");
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, insert_result_sql, -1, &stmt, NULL);
  ok = (rc == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)input->call_id) == SQLITE_OK) &&
       strappy_db_bind_nullable_text_value(
         stmt, 2, strappy_db_semantic_json_string(root, "id")) &&
       strappy_db_bind_nullable_text_value(
         stmt, 3, strappy_db_semantic_json_string(root, "model")) &&
       strappy_db_bind_nullable_text_value(
         stmt, 4, strappy_db_semantic_json_string(root, "status")) &&
       ((strappy_db_response_json_path(root, "created_at") != NULL) ?
          (sqlite3_bind_int64(
             stmt, 5,
             (sqlite3_int64)strappy_db_semantic_json_integer(
               root, "created_at")) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 5) == SQLITE_OK)) &&
       ((strappy_db_response_json_path(root, "completed_at") != NULL) ?
          (sqlite3_bind_int64(
             stmt, 6,
             (sqlite3_int64)strappy_db_semantic_json_integer(
               root, "completed_at")) == SQLITE_OK) :
          (sqlite3_bind_null(stmt, 6) == SQLITE_OK)) &&
       strappy_db_bind_nullable_text_value(
         stmt, 7, strappy_db_semantic_json_string(root,
                                                  "incomplete_details.reason")) &&
       strappy_db_bind_nullable_text_value(stmt, 8, error_type) &&
       strappy_db_bind_nullable_text_value(
         stmt, 9, strappy_db_semantic_json_string(root, "error.code")) &&
       strappy_db_bind_nullable_text_value(
         stmt, 10, strappy_db_semantic_json_string(root, "error.message")) &&
       strappy_db_bind_nullable_text_value(
         stmt, 11,
         cJSON_IsString(error_parameter) ? error_parameter->valuestring : NULL) &&
       strappy_db_bind_nullable_text_value(stmt, 12, parse_error);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save semantic Responses result: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses result", NULL);
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }
  sqlite3_finalize(stmt);

  usage = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "usage") : NULL;
  if (cJSON_IsObject(usage)) {
    has_cost = strappy_db_semantic_json_nano_usd(root, "usage.cost", &cost);
    has_upstream_cost = strappy_db_semantic_json_nano_usd(
      root, "usage.cost_details.upstream_inference_cost", &upstream_cost);
    has_upstream_input_cost = strappy_db_semantic_json_nano_usd(
      root,
      "usage.cost_details.upstream_inference_input_cost",
      &upstream_input_cost);
    has_upstream_output_cost = strappy_db_semantic_json_nano_usd(
      root,
      "usage.cost_details.upstream_inference_output_cost",
      &upstream_output_cost);
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, insert_usage_sql, -1, &stmt, NULL);
    ok = (rc == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 1,
                             (sqlite3_int64)input->call_id) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 2,
                             (sqlite3_int64)strappy_db_semantic_json_integer(
                               root, "usage.input_tokens")) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 3,
                             (sqlite3_int64)strappy_db_semantic_json_integer(
                               root,
                               "usage.input_tokens_details.cached_tokens")) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 4,
                             (sqlite3_int64)strappy_db_semantic_json_integer(
                               root, "usage.output_tokens")) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 5,
                             (sqlite3_int64)strappy_db_semantic_json_integer(
                               root,
                               "usage.output_tokens_details.reasoning_tokens")) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 6,
                             (sqlite3_int64)strappy_db_semantic_json_integer(
                               root, "usage.total_tokens")) == SQLITE_OK) &&
         (has_cost ?
            (sqlite3_bind_int64(stmt, 7, (sqlite3_int64)cost) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 7) == SQLITE_OK)) &&
         (has_upstream_cost ?
            (sqlite3_bind_int64(stmt, 8,
                                (sqlite3_int64)upstream_cost) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 8) == SQLITE_OK)) &&
         (has_upstream_input_cost ?
            (sqlite3_bind_int64(stmt, 9,
                                (sqlite3_int64)upstream_input_cost) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 9) == SQLITE_OK)) &&
         (has_upstream_output_cost ?
            (sqlite3_bind_int64(stmt, 10,
                                (sqlite3_int64)upstream_output_cost) == SQLITE_OK) :
            (sqlite3_bind_null(stmt, 10) == SQLITE_OK)) &&
         (sqlite3_bind_int(stmt, 11,
                           cJSON_IsTrue(strappy_db_response_json_path(
                             root, "usage.is_byok")) ? 1 : 0) == SQLITE_OK);
    if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not save Responses usage: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses result", NULL);
      strappy_db_release(db);
      cJSON_Delete(root);
      return 0;
    }
    sqlite3_finalize(stmt);
  }

  output = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "output") : NULL;
  if (cJSON_IsArray(output)) {
    cJSON *item;
    long item_index;

    item_index = 0L;
    for (item = output->child; item != NULL; item = item->next) {
      if (!strappy_db_semantic_insert_item(db,
                                           session_id,
                                           turn_id,
                                           0LL,
                                           input->call_id,
                                           item_index,
                                           input->output_is_canonical,
                                           1,
                                           item,
                                           NULL,
                                           error_out)) {
        strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses result", NULL);
        strappy_db_release(db);
        cJSON_Delete(root);
        return 0;
      }
      item_index++;
    }
  }

  if (!strappy_db_semantic_insert_answer_quality(
        db,
        input->call_id,
        input->answer_quality_audit,
        error_out)) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back Responses result",
                    NULL);
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }

  request_state = (strcmp(attempt_state, "completed") == 0) ? "completed" :
    ((strcmp(attempt_state, "cancelled") == 0) ? "cancelled" : "error");
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_request_sql, -1, &stmt, NULL);
  ok = (rc == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 1, request_state, -1,
                          SQLITE_TRANSIENT) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 2,
                           (sqlite3_int64)input->completed_at_ms) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)request_id) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not finalize Responses request: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses result", NULL);
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, update_session_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1,
                          (sqlite3_int64)input->completed_at_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not update Responses session result: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses result", NULL);
    strappy_db_release(db);
    cJSON_Delete(root);
    return 0;
  }
  sqlite3_finalize(stmt);
  cJSON_Delete(root);
  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit Responses call result",
                       error_out)) {
    strappy_db_exec(db, "ROLLBACK;", "Could not roll back Responses result", NULL);
    strappy_db_release(db);
    return 0;
  }
  strappy_db_release(db);
  return 1;
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

  return strappy_db_semantic_finish_response_call(db_path, input, error_out);

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
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin Responses call result",
                       error_out)) {
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
    strappy_db_release(db);
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
        strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }
  strappy_db_release(db);
  return 1;
}

typedef struct strappy_db_structured_load_context {
  sqlite3 *db;
  sqlite3_stmt *node_stmt;
  sqlite3_stmt *children_stmt;
  long long document_id;
} strappy_db_structured_load_context;

typedef struct strappy_db_structured_child {
  long long node_id;
  char *member_name;
} strappy_db_structured_child;

static void strappy_db_semantic_destroy_structured_children(
  strappy_db_structured_child *children,
  size_t child_count)
{
  size_t index;

  for (index = 0U; index < child_count; index++) {
    free(children[index].member_name);
  }
  free(children);
}

static cJSON *strappy_db_semantic_load_structured_node(
  strappy_db_structured_load_context *context,
  long long node_id,
  char **error_out)
{
  strappy_db_structured_child *children;
  size_t child_count;
  cJSON *value;
  char *value_type;
  char *text_value;
  char *number_value;
  int boolean_value;
  int is_container;
  int is_object;
  int rc;
  size_t index;

  if ((sqlite3_reset(context->node_stmt) != SQLITE_OK) ||
      (sqlite3_clear_bindings(context->node_stmt) != SQLITE_OK) ||
      (sqlite3_bind_int64(context->node_stmt,
                          1,
                          (sqlite3_int64)context->document_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(context->node_stmt,
                          2,
                          (sqlite3_int64)node_id) != SQLITE_OK) ||
      (sqlite3_step(context->node_stmt) != SQLITE_ROW)) {
    strappy_set_formatted_error(error_out,
                                "Could not read structured value: %s",
                                sqlite3_errmsg(context->db));
    sqlite3_reset(context->node_stmt);
    return NULL;
  }
  value_type = strappy_db_column_string(context->node_stmt, 0);
  text_value = strappy_db_column_string(context->node_stmt, 1);
  number_value = strappy_db_column_string(context->node_stmt, 2);
  boolean_value = sqlite3_column_type(context->node_stmt, 3) == SQLITE_NULL ?
    0 : sqlite3_column_int(context->node_stmt, 3);
  sqlite3_reset(context->node_stmt);
  if (value_type == NULL) {
    free(text_value);
    free(number_value);
    strappy_set_error(error_out, "Structured value type is missing.");
    return NULL;
  }

  is_object = (strcmp(value_type, "object") == 0) ? 1 : 0;
  is_container = is_object || (strcmp(value_type, "array") == 0);
  if (is_object) {
    value = cJSON_CreateObject();
  } else if (strcmp(value_type, "array") == 0) {
    value = cJSON_CreateArray();
  } else if (strcmp(value_type, "string") == 0) {
    value = cJSON_CreateString((text_value != NULL) ? text_value : "");
  } else if (strcmp(value_type, "number") == 0) {
    value = cJSON_CreateNumber(
      (number_value != NULL) ? strtod(number_value, NULL) : 0.0);
  } else if (strcmp(value_type, "boolean") == 0) {
    value = cJSON_CreateBool(boolean_value ? 1 : 0);
  } else {
    value = cJSON_CreateNull();
  }
  free(value_type);
  free(text_value);
  free(number_value);
  if (value == NULL) {
    strappy_set_error(error_out, "Could not allocate structured value.");
    return NULL;
  }
  if (!is_container) {
    return value;
  }

  children = NULL;
  child_count = 0U;
  if ((sqlite3_reset(context->children_stmt) != SQLITE_OK) ||
      (sqlite3_clear_bindings(context->children_stmt) != SQLITE_OK) ||
      (sqlite3_bind_int64(context->children_stmt,
                          1,
                          (sqlite3_int64)context->document_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(context->children_stmt,
                          2,
                          (sqlite3_int64)node_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare structured children: %s",
                                sqlite3_errmsg(context->db));
    cJSON_Delete(value);
    return NULL;
  }
  while ((rc = sqlite3_step(context->children_stmt)) == SQLITE_ROW) {
    strappy_db_structured_child *next_children;

    if (child_count >= (((size_t)-1) / sizeof(*children))) {
      strappy_set_error(error_out, "Structured value has too many children.");
      sqlite3_reset(context->children_stmt);
      strappy_db_semantic_destroy_structured_children(children, child_count);
      cJSON_Delete(value);
      return NULL;
    }
    next_children = (strappy_db_structured_child *)realloc(
      children, (child_count + 1U) * sizeof(*children));
    if (next_children == NULL) {
      strappy_set_error(error_out, "Could not allocate structured children.");
      sqlite3_reset(context->children_stmt);
      strappy_db_semantic_destroy_structured_children(children, child_count);
      cJSON_Delete(value);
      return NULL;
    }
    children = next_children;
    children[child_count].node_id =
      (long long)sqlite3_column_int64(context->children_stmt, 0);
    children[child_count].member_name =
      strappy_db_column_string(context->children_stmt, 1);
    child_count++;
  }
  sqlite3_reset(context->children_stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read structured children: %s",
                                sqlite3_errmsg(context->db));
    strappy_db_semantic_destroy_structured_children(children, child_count);
    cJSON_Delete(value);
    return NULL;
  }

  for (index = 0U; index < child_count; index++) {
    cJSON *child;

    if (is_object && (children[index].member_name == NULL)) {
      strappy_set_error(error_out,
                        "Structured object member name is missing.");
      strappy_db_semantic_destroy_structured_children(children, child_count);
      cJSON_Delete(value);
      return NULL;
    }
    child = strappy_db_semantic_load_structured_node(
      context, children[index].node_id, error_out);
    if (child == NULL) {
      strappy_db_semantic_destroy_structured_children(children, child_count);
      cJSON_Delete(value);
      return NULL;
    }
    if (is_object) {
      cJSON_AddItemToObject(value, children[index].member_name, child);
    } else {
      cJSON_AddItemToArray(value, child);
    }
  }
  strappy_db_semantic_destroy_structured_children(children, child_count);
  return value;
}

static cJSON *strappy_db_semantic_load_document(sqlite3 *db,
                                                long long owner_item_id,
                                                const char *purpose,
                                                char **error_out)
{
  static const char *sql =
    "SELECT id FROM structured_documents "
    "WHERE owner_item_id = ? AND purpose = ?;";
  static const char *node_sql =
    "SELECT value_type, text_value, number_value, boolean_value "
    "FROM structured_nodes WHERE document_id = ? AND node_id = ?;";
  static const char *children_sql =
    "SELECT node_id, member_name FROM structured_nodes "
    "WHERE document_id = ? AND parent_node_id = ? ORDER BY ordinal;";
  strappy_db_structured_load_context context;
  sqlite3_stmt *stmt;
  cJSON *root;
  long long document_id;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1,
                          (sqlite3_int64)owner_item_id) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, purpose, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare structured document lookup: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    if (rc == SQLITE_DONE) {
      strappy_set_error(error_out, "Structured document was not found.");
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not read structured document: %s",
                                  sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return NULL;
  }
  document_id = (long long)sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  context.db = db;
  context.node_stmt = NULL;
  context.children_stmt = NULL;
  context.document_id = document_id;
  rc = sqlite3_prepare_v2(db, node_sql, -1, &context.node_stmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_prepare_v2(db,
                            children_sql,
                            -1,
                            &context.children_stmt,
                            NULL);
  }
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare structured value load: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(context.node_stmt);
    sqlite3_finalize(context.children_stmt);
    return NULL;
  }
  root = strappy_db_semantic_load_structured_node(&context, 0LL, error_out);
  sqlite3_finalize(context.node_stmt);
  sqlite3_finalize(context.children_stmt);
  return root;
}

static cJSON *strappy_db_semantic_load_annotations(sqlite3 *db,
                                                   long long text_part_id,
                                                   char **error_out)
{
  static const char *sql =
    "SELECT citation_type, start_offset, end_offset, title, url, excerpt "
    "FROM item_citations WHERE text_part_id = ? ORDER BY ordinal;";
  sqlite3_stmt *stmt;
  cJSON *annotations;
  int rc;

  annotations = cJSON_CreateArray();
  if (annotations == NULL) {
    strappy_set_error(error_out, "Could not allocate citation list.");
    return NULL;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)text_part_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare citation query: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    cJSON_Delete(annotations);
    return NULL;
  }
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cJSON *annotation;
    const unsigned char *text;

    annotation = cJSON_CreateObject();
    if (annotation == NULL) {
      sqlite3_finalize(stmt);
      cJSON_Delete(annotations);
      strappy_set_error(error_out, "Could not allocate citation.");
      return NULL;
    }
    text = sqlite3_column_text(stmt, 0);
    cJSON_AddStringToObject(annotation,
                           "type",
                           (text != NULL) ? (const char *)text : "citation");
    if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
      cJSON_AddNumberToObject(annotation,
                             "start_index",
                             (double)sqlite3_column_int64(stmt, 1));
    }
    if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
      cJSON_AddNumberToObject(annotation,
                             "end_index",
                             (double)sqlite3_column_int64(stmt, 2));
    }
    text = sqlite3_column_text(stmt, 3);
    if (text != NULL) {
      cJSON_AddStringToObject(annotation, "title", (const char *)text);
    }
    text = sqlite3_column_text(stmt, 4);
    if (text != NULL) {
      cJSON_AddStringToObject(annotation, "url", (const char *)text);
    }
    text = sqlite3_column_text(stmt, 5);
    if (text != NULL) {
      cJSON_AddStringToObject(annotation, "content", (const char *)text);
    }
    cJSON_AddItemToArray(annotations, annotation);
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cJSON_Delete(annotations);
    strappy_set_formatted_error(error_out,
                                "Could not read citations: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }
  return annotations;
}

static cJSON *strappy_db_semantic_load_text_parts(sqlite3 *db,
                                                  long long item_id,
                                                  const char *collection_name,
                                                  char **error_out)
{
  static const char *sql =
    "SELECT id, part_type, text FROM item_text_parts "
    "WHERE item_id = ? AND collection_name = ? ORDER BY ordinal;";
  sqlite3_stmt *stmt;
  cJSON *parts;
  int rc;

  parts = cJSON_CreateArray();
  if (parts == NULL) {
    strappy_set_error(error_out, "Could not allocate Responses text parts.");
    return NULL;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 2, collection_name, -1,
                         SQLITE_TRANSIENT) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare Responses text query: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    cJSON_Delete(parts);
    return NULL;
  }
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cJSON *part;
    cJSON *annotations;
    long long text_part_id;
    const unsigned char *part_type;
    const unsigned char *text;

    text_part_id = (long long)sqlite3_column_int64(stmt, 0);
    part_type = sqlite3_column_text(stmt, 1);
    text = sqlite3_column_text(stmt, 2);
    part = cJSON_CreateObject();
    if (part == NULL) {
      sqlite3_finalize(stmt);
      cJSON_Delete(parts);
      strappy_set_error(error_out, "Could not allocate Responses text part.");
      return NULL;
    }
    cJSON_AddStringToObject(part,
                           "type",
                           (part_type != NULL) ?
                             (const char *)part_type : "output_text");
    if ((part_type != NULL) &&
        (strcmp((const char *)part_type, "refusal") == 0)) {
      cJSON_AddStringToObject(part,
                             "refusal",
                             (text != NULL) ? (const char *)text : "");
    } else {
      cJSON_AddStringToObject(part,
                             "text",
                             (text != NULL) ? (const char *)text : "");
    }
    annotations = strappy_db_semantic_load_annotations(db,
                                                       text_part_id,
                                                       error_out);
    if (annotations == NULL) {
      cJSON_Delete(part);
      sqlite3_finalize(stmt);
      cJSON_Delete(parts);
      return NULL;
    }
    if (cJSON_GetArraySize(annotations) > 0) {
      cJSON_AddItemToObject(part, "annotations", annotations);
    } else {
      cJSON_Delete(annotations);
    }
    cJSON_AddItemToArray(parts, part);
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cJSON_Delete(parts);
    strappy_set_formatted_error(error_out,
                                "Could not read Responses text parts: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }
  return parts;
}

static cJSON *strappy_db_semantic_load_item(sqlite3 *db,
                                            long long item_id,
                                            char **error_out)
{
  static const char *base_sql =
    "SELECT kind, provider_item_id, provider_status "
    "FROM conversation_items WHERE id = ?;";
  sqlite3_stmt *stmt;
  cJSON *item;
  cJSON *parts;
  cJSON *document;
  char *kind;
  char *provider_item_id;
  char *provider_status;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, base_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_ROW)) {
    strappy_set_formatted_error(error_out,
                                "Could not read conversation item: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  kind = strappy_db_column_string(stmt, 0);
  provider_item_id = strappy_db_column_string(stmt, 1);
  provider_status = strappy_db_column_string(stmt, 2);
  sqlite3_finalize(stmt);
  if (kind == NULL) {
    free(provider_item_id);
    free(provider_status);
    strappy_set_error(error_out, "Conversation item kind is missing.");
    return NULL;
  }
  item = cJSON_CreateObject();
  if (item == NULL) {
    free(kind);
    free(provider_item_id);
    free(provider_status);
    strappy_set_error(error_out, "Could not allocate conversation item.");
    return NULL;
  }
  cJSON_AddStringToObject(item, "type", kind);
  if (provider_item_id != NULL) {
    cJSON_AddStringToObject(item, "id", provider_item_id);
  }
  if (provider_status != NULL) {
    cJSON_AddStringToObject(item, "status", provider_status);
  }

  if (strcmp(kind, "message") == 0) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(
      db,
      "SELECT role, phase FROM message_items WHERE item_id = ?;",
      -1,
      &stmt,
      NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_ROW)) {
      strappy_set_formatted_error(error_out,
                                  "Could not read message item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      return NULL;
    }
    cJSON_AddStringToObject(item,
                           "role",
                           (const char *)sqlite3_column_text(stmt, 0));
    if (sqlite3_column_text(stmt, 1) != NULL) {
      cJSON_AddStringToObject(item,
                             "phase",
                             (const char *)sqlite3_column_text(stmt, 1));
    }
    sqlite3_finalize(stmt);
    parts = strappy_db_semantic_load_text_parts(db,
                                                item_id,
                                                "content",
                                                error_out);
    if (parts == NULL) {
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      return NULL;
    }
    cJSON_AddItemToObject(item, "content", parts);
  } else if (strcmp(kind, "reasoning") == 0) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(
      db,
      "SELECT encrypted_content, provider_format, provider_signature "
      "FROM reasoning_items "
      "WHERE item_id = ?;",
      -1,
      &stmt,
      NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_ROW)) {
      strappy_set_formatted_error(error_out,
                                  "Could not read reasoning item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      return NULL;
    }
    if (sqlite3_column_text(stmt, 0) != NULL) {
      cJSON_AddStringToObject(item,
                             "encrypted_content",
                             (const char *)sqlite3_column_text(stmt, 0));
    }
    if (sqlite3_column_text(stmt, 1) != NULL) {
      cJSON_AddStringToObject(item,
                             "format",
                             (const char *)sqlite3_column_text(stmt, 1));
    }
    if (sqlite3_column_text(stmt, 2) != NULL) {
      cJSON_AddStringToObject(item,
                             "signature",
                             (const char *)sqlite3_column_text(stmt, 2));
    }
    sqlite3_finalize(stmt);
    parts = strappy_db_semantic_load_text_parts(db,
                                                item_id,
                                                "content",
                                                error_out);
    if (parts == NULL) {
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      return NULL;
    }
    cJSON_AddItemToObject(item, "content", parts);
    parts = strappy_db_semantic_load_text_parts(db,
                                                item_id,
                                                "summary",
                                                error_out);
    if (parts == NULL) {
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      return NULL;
    }
    cJSON_AddItemToObject(item, "summary", parts);
  } else if (strcmp(kind, "function_call") == 0) {
    char *arguments;

    stmt = NULL;
    rc = sqlite3_prepare_v2(
      db,
      "SELECT provider_call_id, tool_name, tool_namespace "
      "FROM function_calls WHERE item_id = ?;",
      -1,
      &stmt,
      NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_ROW)) {
      strappy_set_formatted_error(error_out,
                                  "Could not read function call item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      return NULL;
    }
    cJSON_AddStringToObject(item,
                           "call_id",
                           (const char *)sqlite3_column_text(stmt, 0));
    cJSON_AddStringToObject(item,
                           "name",
                           (const char *)sqlite3_column_text(stmt, 1));
    if (sqlite3_column_text(stmt, 2) != NULL) {
      cJSON_AddStringToObject(item,
                             "namespace",
                             (const char *)sqlite3_column_text(stmt, 2));
    }
    sqlite3_finalize(stmt);
    document = strappy_db_semantic_load_document(db,
                                                 item_id,
                                                 "arguments",
                                                 error_out);
    if (document == NULL) {
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      return NULL;
    }
    arguments = cJSON_PrintUnformatted(document);
    cJSON_Delete(document);
    if (arguments == NULL) {
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      strappy_set_error(error_out, "Could not serialize function arguments.");
      return NULL;
    }
    cJSON_AddStringToObject(item, "arguments", arguments);
    free(arguments);
  } else if (strcmp(kind, "function_call_output") == 0) {
    char *output_text;
    const unsigned char *call_id;
    int output_is_structured;

    stmt = NULL;
    rc = sqlite3_prepare_v2(
      db,
      "SELECT c.provider_call_id, o.output_format, o.text_output "
      "FROM function_outputs o JOIN function_calls c "
      "ON c.item_id = o.function_call_item_id WHERE o.item_id = ?;",
      -1,
      &stmt,
      NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_ROW)) {
      strappy_set_formatted_error(error_out,
                                  "Could not read function output item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      return NULL;
    }
    call_id = sqlite3_column_text(stmt, 0);
    output_is_structured = (sqlite3_column_text(stmt, 1) != NULL) &&
      (strcmp((const char *)sqlite3_column_text(stmt, 1), "structured") == 0);
    output_text = strappy_db_column_string(stmt, 2);
    cJSON_AddStringToObject(item,
                           "call_id",
                           (call_id != NULL) ? (const char *)call_id : "");
    sqlite3_finalize(stmt);
    if (output_is_structured) {
      document = strappy_db_semantic_load_document(db,
                                                   item_id,
                                                   "output",
                                                   error_out);
      if (document == NULL) {
        cJSON_Delete(item);
        free(output_text);
        free(kind);
        free(provider_item_id);
        free(provider_status);
        return NULL;
      }
      free(output_text);
      output_text = cJSON_PrintUnformatted(document);
      cJSON_Delete(document);
    }
    if (output_text == NULL) {
      output_text = strappy_string_duplicate("");
    }
    if (output_text == NULL) {
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      strappy_set_error(error_out, "Could not allocate function output.");
      return NULL;
    }
    cJSON_AddStringToObject(item, "output", output_text);
    free(output_text);
  } else if (strcmp(kind, "openrouter:web_search") == 0) {
    cJSON *action;
    cJSON *sources;

    stmt = NULL;
    rc = sqlite3_prepare_v2(
      db,
      "SELECT action_type, query FROM web_searches WHERE item_id = ?;",
      -1,
      &stmt,
      NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_ROW)) {
      strappy_set_formatted_error(error_out,
                                  "Could not read web-search item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      return NULL;
    }
    action = cJSON_CreateObject();
    sources = cJSON_CreateArray();
    if ((action == NULL) || (sources == NULL)) {
      cJSON_Delete(action);
      cJSON_Delete(sources);
      sqlite3_finalize(stmt);
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      strappy_set_error(error_out, "Could not allocate web-search action.");
      return NULL;
    }
    cJSON_AddStringToObject(action,
                           "type",
                           (const char *)sqlite3_column_text(stmt, 0));
    if (sqlite3_column_text(stmt, 1) != NULL) {
      cJSON_AddStringToObject(action,
                             "query",
                             (const char *)sqlite3_column_text(stmt, 1));
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    rc = sqlite3_prepare_v2(
      db,
      "SELECT source_type, url FROM web_search_sources "
      "WHERE web_search_item_id = ? ORDER BY ordinal;",
      -1,
      &stmt,
      NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK)) {
      strappy_set_formatted_error(error_out,
                                  "Could not prepare web-search sources: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      cJSON_Delete(action);
      cJSON_Delete(sources);
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      return NULL;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      cJSON *source;

      source = cJSON_CreateObject();
      if (source == NULL) {
        sqlite3_finalize(stmt);
        cJSON_Delete(action);
        cJSON_Delete(sources);
        cJSON_Delete(item);
        free(kind);
        free(provider_item_id);
        free(provider_status);
        strappy_set_error(error_out, "Could not allocate web-search source.");
        return NULL;
      }
      cJSON_AddStringToObject(source,
                             "type",
                             (const char *)sqlite3_column_text(stmt, 0));
      cJSON_AddStringToObject(source,
                             "url",
                             (const char *)sqlite3_column_text(stmt, 1));
      cJSON_AddItemToArray(sources, source);
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
      cJSON_Delete(action);
      cJSON_Delete(sources);
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      strappy_set_formatted_error(error_out,
                                  "Could not read web-search sources: %s",
                                  sqlite3_errmsg(db));
      return NULL;
    }
    if (cJSON_GetArraySize(sources) > 0) {
      cJSON_AddItemToObject(action, "sources", sources);
    } else {
      cJSON_Delete(sources);
    }
    cJSON_AddItemToObject(item, "action", action);
  } else {
    stmt = NULL;
    rc = sqlite3_prepare_v2(
      db,
      "SELECT url, title, content, http_status FROM web_fetches "
      "WHERE item_id = ?;",
      -1,
      &stmt,
      NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_ROW)) {
      strappy_set_formatted_error(error_out,
                                  "Could not read web-fetch item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      cJSON_Delete(item);
      free(kind);
      free(provider_item_id);
      free(provider_status);
      return NULL;
    }
    cJSON_AddStringToObject(item,
                           "url",
                           (const char *)sqlite3_column_text(stmt, 0));
    if (sqlite3_column_text(stmt, 1) != NULL) {
      cJSON_AddStringToObject(item,
                             "title",
                             (const char *)sqlite3_column_text(stmt, 1));
    }
    if (sqlite3_column_text(stmt, 2) != NULL) {
      cJSON_AddStringToObject(item,
                             "content",
                             (const char *)sqlite3_column_text(stmt, 2));
    }
    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
      cJSON_AddNumberToObject(item,
                             "httpStatus",
                             (double)sqlite3_column_int64(stmt, 3));
    }
    sqlite3_finalize(stmt);
  }
  free(kind);
  free(provider_item_id);
  free(provider_status);
  return item;
}

static int strappy_db_semantic_list_canonical_response_items(
  const char *db_path,
  long long session_id,
  const char *prompt_group_key,
  strappy_response_item_raw_record_list *list,
  char **error_out)
{
  static const char *all_sql =
    "SELECT id FROM conversation_items "
    "WHERE session_id = ? AND include_in_context = 1 ORDER BY sequence;";
  static const char *prompt_group_sql =
    "SELECT i.id FROM conversation_items i "
    "JOIN turns t ON t.id = i.turn_id "
    "WHERE i.session_id = ?1 AND t.prompt_group_key = ?2 "
    "AND i.include_in_context = 1 ORDER BY i.sequence;";
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
    strappy_db_release(db);
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db,
                          (prompt_group_key != NULL) ?
                            prompt_group_sql : all_sql,
                          -1,
                          &stmt,
                          NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) ||
      ((prompt_group_key != NULL) &&
       ((prompt_group_key[0] == '\0') ||
        (sqlite3_bind_text(stmt,
                           2,
                           prompt_group_key,
                           -1,
                           SQLITE_TRANSIENT) != SQLITE_OK)))) {
    strappy_set_formatted_error(
      error_out,
      "Could not prepare canonical Responses item query: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_response_item_raw_record *records;
    strappy_response_item_raw_record *record;
    cJSON *item;
    long long item_id;

    item_id = (long long)sqlite3_column_int64(stmt, 0);
    item = strappy_db_semantic_load_item(db, item_id, error_out);
    if (item == NULL) {
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_response_item_raw_record_list_destroy(list);
      return 0;
    }
    if (list->count >= (((size_t)-1) /
                        sizeof(strappy_response_item_raw_record))) {
      cJSON_Delete(item);
      strappy_set_error(error_out,
                        "Canonical Responses item list is too large.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_response_item_raw_record_list_destroy(list);
      return 0;
    }
    records = (strappy_response_item_raw_record *)realloc(
      list->records,
      (list->count + 1U) * sizeof(*records));
    if (records == NULL) {
      cJSON_Delete(item);
      strappy_set_error(error_out,
                        "Could not allocate canonical Responses item list.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_response_item_raw_record_list_destroy(list);
      return 0;
    }
    list->records = records;
    record = &list->records[list->count];
    record->item_id = item_id;
    record->raw_json = cJSON_PrintUnformatted(item);
    cJSON_Delete(item);
    if (record->raw_json == NULL) {
      strappy_set_error(error_out,
                        "Could not serialize canonical Responses item.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_response_item_raw_record_list_destroy(list);
      return 0;
    }
    list->count++;
  }
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  if (rc != SQLITE_DONE) {
    strappy_response_item_raw_record_list_destroy(list);
    strappy_set_error(error_out, "Could not list canonical Responses items.");
    return 0;
  }
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

  return strappy_db_semantic_list_canonical_response_items(db_path,
                                                           session_id,
                                                           NULL,
                                                           list,
                                                           error_out);

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
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(
      error_out,
      "Could not prepare canonical Responses item query: %s",
      sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }
  rc = sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(
      error_out,
      "Could not bind canonical Responses item query: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
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
      strappy_db_release(db);
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
      strappy_db_release(db);
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
      strappy_db_release(db);
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
    strappy_db_release(db);
    strappy_response_item_raw_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_list_canonical_response_items_for_prompt_group(
  const char *db_path,
  long long session_id,
  const char *prompt_group_key,
  strappy_response_item_raw_record_list *list,
  char **error_out)
{
  if ((prompt_group_key == NULL) || (prompt_group_key[0] == '\0')) {
    strappy_set_error(error_out, "Responses prompt group key is empty.");
    return 0;
  }
  return strappy_db_semantic_list_canonical_response_items(db_path,
                                                           session_id,
                                                           prompt_group_key,
                                                           list,
                                                           error_out);
}

static int strappy_db_semantic_save_response_tool_execution(
  const char *db_path,
  const strappy_response_tool_execution_input *input,
  char **error_out)
{
  static const char *sql =
    "INSERT OR REPLACE INTO tool_executions "
    "(function_call_item_id, response_attempt_id, output_index, state, "
     "started_at_ms, completed_at_ms, error_message) "
    "SELECT f.item_id, ?, ?, ?, ?, ?, ? "
    "FROM function_calls f JOIN conversation_items i ON i.id = f.item_id "
    "WHERE f.provider_call_id = ? AND f.tool_name = ? "
    "AND i.source_attempt_id = ? AND i.session_id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if ((input == NULL) || (input->session_id <= 0LL) ||
      (input->response_call_id <= 0LL) || (input->output_index < 0L) ||
      (input->call_id == NULL) || (input->call_id[0] == '\0') ||
      (input->tool_name == NULL) || (input->tool_name[0] == '\0') ||
      (input->status == NULL) ||
      ((strcmp(input->status, "pending") != 0) &&
       (strcmp(input->status, "running") != 0) &&
       (strcmp(input->status, "completed") != 0) &&
       (strcmp(input->status, "error") != 0) &&
       (strcmp(input->status, "cancelled") != 0))) {
    strappy_set_error(error_out, "Responses tool execution is incomplete.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  ok = (rc == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 1,
                           (sqlite3_int64)input->response_call_id) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 2,
                           (sqlite3_int64)input->output_index) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 3, input->status, -1,
                          SQLITE_TRANSIENT) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 4,
                           (sqlite3_int64)input->started_at_ms) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 5,
                           (sqlite3_int64)input->completed_at_ms) == SQLITE_OK) &&
       strappy_db_bind_nullable_text_value(stmt, 6, input->error_text) &&
       (sqlite3_bind_text(stmt, 7, input->call_id, -1,
                          SQLITE_TRANSIENT) == SQLITE_OK) &&
       (sqlite3_bind_text(stmt, 8, input->tool_name, -1,
                          SQLITE_TRANSIENT) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 9,
                           (sqlite3_int64)input->response_call_id) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 10,
                           (sqlite3_int64)input->session_id) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not save Responses tool execution: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  sqlite3_finalize(stmt);
  if (sqlite3_changes(db) != 1) {
    strappy_set_error(error_out,
                      "Responses tool execution did not match its function call.");
    strappy_db_release(db);
    return 0;
  }
  strappy_db_release(db);
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

  return strappy_db_semantic_save_response_tool_execution(db_path,
                                                          input,
                                                          error_out);

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
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(
      error_out,
      "Could not prepare Responses tool execution insert: %s",
      sqlite3_errmsg(db));
    strappy_db_release(db);
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
    strappy_db_release(db);
    return 0;
  }
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not save Responses tool execution: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
    return 0;
  }
  strappy_db_release(db);
  return 1;
}

int strappy_db_finalize_cancelled_response_tool_outputs(
  const char *db_path,
  long long session_id,
  long long response_call_id,
  const char * const *item_jsons,
  size_t item_count,
  char **error_out)
{
  static const char *turn_sql =
    "SELECT i.turn_id FROM conversation_items i "
    "JOIN function_calls f ON f.item_id = i.id "
    "WHERE i.session_id = ? AND i.source_attempt_id = ? "
    "ORDER BY i.sequence LIMIT 1;";
  static const char *cancel_turn_sql =
    "UPDATE turns SET state = 'cancelled', completed_at_ms = ? "
    "WHERE id = ? AND session_id = ?;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  long long turn_id;
  size_t index;
  int rc;
  int ok;

  if ((session_id <= 0LL) || (response_call_id <= 0LL) ||
      (item_jsons == NULL) || (item_count == 0U) ||
      (item_count > (size_t)LONG_MAX)) {
    strappy_set_error(
      error_out,
      "Cancelled Responses tool outputs are incomplete.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin cancelled tool-output save",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, turn_sql, -1, &stmt, NULL);
  ok = (rc == SQLITE_OK) &&
    (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) == SQLITE_OK) &&
    (sqlite3_bind_int64(stmt, 2,
                        (sqlite3_int64)response_call_id) == SQLITE_OK) &&
    (sqlite3_step(stmt) == SQLITE_ROW);
  turn_id = ok ? (long long)sqlite3_column_int64(stmt, 0) : 0LL;
  sqlite3_finalize(stmt);
  if (!ok || (turn_id <= 0LL)) {
    strappy_set_formatted_error(
      error_out,
      "Could not find the cancelled Responses tool turn: %s",
      sqlite3_errmsg(db));
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back cancelled tool outputs",
                    NULL);
    strappy_db_release(db);
    return 0;
  }

  for (index = 0U; index < item_count; index++) {
    cJSON *item;
    cJSON *type;

    item = (item_jsons[index] != NULL) ?
      cJSON_Parse(item_jsons[index]) : NULL;
    type = cJSON_IsObject(item) ?
      cJSON_GetObjectItemCaseSensitive(item, "type") : NULL;
    if ((type == NULL) || !cJSON_IsString(type) ||
        (type->valuestring == NULL) ||
        (strcmp(type->valuestring, "function_call_output") != 0) ||
        !strappy_db_semantic_insert_item(db,
                                         session_id,
                                         turn_id,
                                         0LL,
                                         0LL,
                                         (long)index,
                                         1,
                                         1,
                                         item,
                                         NULL,
                                         error_out)) {
      cJSON_Delete(item);
      if ((error_out == NULL) || (*error_out == NULL)) {
        strappy_set_error(error_out,
                          "Cancelled tool output is not valid.");
      }
      strappy_db_exec(db,
                      "ROLLBACK;",
                      "Could not roll back cancelled tool outputs",
                      NULL);
      strappy_db_release(db);
      return 0;
    }
    cJSON_Delete(item);
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, cancel_turn_sql, -1, &stmt, NULL);
  ok = (rc == SQLITE_OK) &&
    (sqlite3_bind_int64(stmt, 1,
                        (sqlite3_int64)strappy_db_now_ms()) == SQLITE_OK) &&
    (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)turn_id) == SQLITE_OK) &&
    (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)session_id) == SQLITE_OK) &&
    (sqlite3_step(stmt) == SQLITE_DONE) && (sqlite3_changes(db) == 1);
  sqlite3_finalize(stmt);
  if (!ok ||
      !strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit cancelled tool outputs",
                       error_out)) {
    if (!ok && ((error_out == NULL) || (*error_out == NULL))) {
      strappy_set_formatted_error(
        error_out,
        "Could not finish the cancelled Responses turn: %s",
        sqlite3_errmsg(db));
    }
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back cancelled tool outputs",
                    NULL);
    strappy_db_release(db);
    return 0;
  }
  strappy_db_release(db);
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
  sqlite3_stmt *stmt;
  long long now_ms;
  int rc;
  int ok;

  (void)prompt;
  (void)response;
  (void)http_status;

  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  if (!strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin Responses session finalization",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }
  now_ms = strappy_db_now_ms();
  if ((model != NULL) && (model[0] != '\0')) {
    stmt = NULL;
    rc = sqlite3_prepare_v2(
      db,
      "INSERT OR IGNORE INTO models "
      "(id, name, catalog_active, last_seen_at_ms) VALUES (?, ?, 1, ?);",
      -1,
      &stmt,
      NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 1, model, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 2, model, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now_ms) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not retain session model: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      strappy_db_exec(db,
                      "ROLLBACK;",
                      "Could not roll back Responses session finalization",
                      NULL);
      strappy_db_release(db);
      return 0;
    }
    sqlite3_finalize(stmt);
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "UPDATE sessions SET model_id = CASE WHEN ? IS NULL OR ? = '' "
      "THEN model_id ELSE ? END, updated_at_ms = ? WHERE id = ?;",
    -1,
    &stmt,
    NULL);
  ok = (rc == SQLITE_OK) &&
       strappy_db_bind_nullable_text_value(stmt, 1, model) &&
       strappy_db_bind_nullable_text_value(stmt, 2, model) &&
       strappy_db_bind_nullable_text_value(stmt, 3, model) &&
       (sqlite3_bind_int64(stmt, 4, (sqlite3_int64)now_ms) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 5, (sqlite3_int64)session_id) == SQLITE_OK);
  if (!ok || (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(error_out,
                                "Could not finalize Responses session: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back Responses session finalization",
                    NULL);
    strappy_db_release(db);
    return 0;
  }
  sqlite3_finalize(stmt);
  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "UPDATE turns SET state = COALESCE((SELECT CASE r.state "
      "WHEN 'completed' THEN 'completed' WHEN 'cancelled' THEN 'cancelled' "
      "ELSE 'error' END FROM model_requests r WHERE r.turn_id = turns.id "
      "ORDER BY r.round_index DESC LIMIT 1), 'completed'), "
      "completed_at_ms = ? WHERE id = (SELECT id FROM turns "
      "WHERE session_id = ? ORDER BY ordinal DESC LIMIT 1);",
    -1,
    &stmt,
    NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE)) {
    strappy_set_formatted_error(error_out,
                                "Could not finalize Responses turn: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back Responses session finalization",
                    NULL);
    strappy_db_release(db);
    return 0;
  }
  sqlite3_finalize(stmt);
  ok = strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit Responses session finalization",
                       error_out);
  if (!ok) {
    strappy_db_exec(db,
                    "ROLLBACK;",
                    "Could not roll back Responses session finalization",
                    NULL);
  }
  strappy_db_release(db);
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

static char *strappy_db_semantic_attempt_metadata(sqlite3 *db,
                                                  long long attempt_id,
                                                  char **error_out)
{
  static const char *sql =
    "SELECT r.provider_response_id, r.provider_model_id, r.provider_status, "
    "r.provider_created_at_s, r.provider_completed_at_s, "
    "r.incomplete_reason, r.error_type, r.error_code, r.error_message, "
    "r.error_parameter, u.input_tokens, "
    "u.cached_input_tokens, u.output_tokens, u.reasoning_tokens, "
    "u.total_tokens, u.cost_nano_usd, u.upstream_cost_nano_usd, "
    "u.upstream_input_cost_nano_usd, u.upstream_output_cost_nano_usd, "
    "u.is_byok "
    "FROM http_attempts a LEFT JOIN api_results r ON r.attempt_id = a.id "
    "LEFT JOIN api_usage u ON u.attempt_id = a.id WHERE a.id = ?;";
  sqlite3_stmt *stmt;
  cJSON *root;
  cJSON *incomplete;
  cJSON *error;
  cJSON *usage;
  cJSON *details;
  const unsigned char *text;
  char *serialized;
  int has_metadata;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)attempt_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_ROW)) {
    strappy_set_formatted_error(error_out,
                                "Could not read Responses attempt metadata: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  root = cJSON_CreateObject();
  if (root == NULL) {
    sqlite3_finalize(stmt);
    strappy_set_error(error_out, "Could not allocate Responses metadata.");
    return NULL;
  }
  has_metadata = 0;
  text = sqlite3_column_text(stmt, 2);
  if (text != NULL) {
    cJSON_AddStringToObject(root, "status", (const char *)text);
    has_metadata = 1;
  }
  text = sqlite3_column_text(stmt, 0);
  if (text != NULL) {
    cJSON_AddStringToObject(root, "id", (const char *)text);
    has_metadata = 1;
  }
  text = sqlite3_column_text(stmt, 1);
  if (text != NULL) {
    cJSON_AddStringToObject(root, "model", (const char *)text);
    has_metadata = 1;
  }
  if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
    cJSON_AddNumberToObject(root,
                           "created_at",
                           (double)sqlite3_column_int64(stmt, 3));
    has_metadata = 1;
  }
  if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
    cJSON_AddNumberToObject(root,
                           "completed_at",
                           (double)sqlite3_column_int64(stmt, 4));
    has_metadata = 1;
  }
  text = sqlite3_column_text(stmt, 5);
  if (text != NULL) {
    incomplete = cJSON_CreateObject();
    if (incomplete != NULL) {
      cJSON_AddStringToObject(incomplete, "reason", (const char *)text);
      cJSON_AddItemToObject(root, "incomplete_details", incomplete);
      has_metadata = 1;
    }
  }
  if ((sqlite3_column_text(stmt, 6) != NULL) ||
      (sqlite3_column_text(stmt, 7) != NULL) ||
      (sqlite3_column_text(stmt, 8) != NULL) ||
      (sqlite3_column_text(stmt, 9) != NULL)) {
    error = cJSON_CreateObject();
    if (error != NULL) {
      text = sqlite3_column_text(stmt, 6);
      if (text != NULL) {
        cJSON_AddStringToObject(error, "type", (const char *)text);
      }
      text = sqlite3_column_text(stmt, 7);
      if (text != NULL) {
        cJSON_AddStringToObject(error, "code", (const char *)text);
      }
      text = sqlite3_column_text(stmt, 8);
      if (text != NULL) {
        cJSON_AddStringToObject(error, "message", (const char *)text);
      }
      text = sqlite3_column_text(stmt, 9);
      if (text != NULL) {
        cJSON_AddStringToObject(error, "param", (const char *)text);
      }
      cJSON_AddItemToObject(root, "error", error);
      has_metadata = 1;
    }
  }
  if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
    usage = cJSON_CreateObject();
    if (usage != NULL) {
      cJSON_AddNumberToObject(usage,
                             "input_tokens",
                             (double)sqlite3_column_int64(stmt, 10));
      cJSON_AddNumberToObject(usage,
                             "output_tokens",
                             (double)sqlite3_column_int64(stmt, 12));
      cJSON_AddNumberToObject(usage,
                             "total_tokens",
                             (double)sqlite3_column_int64(stmt, 14));
      if (sqlite3_column_int64(stmt, 11) != 0) {
        details = cJSON_CreateObject();
        if (details != NULL) {
          cJSON_AddNumberToObject(details,
                                 "cached_tokens",
                                 (double)sqlite3_column_int64(stmt, 11));
          cJSON_AddItemToObject(usage, "input_tokens_details", details);
        }
      }
      if (sqlite3_column_int64(stmt, 13) != 0) {
        details = cJSON_CreateObject();
        if (details != NULL) {
          cJSON_AddNumberToObject(details,
                                 "reasoning_tokens",
                                 (double)sqlite3_column_int64(stmt, 13));
          cJSON_AddItemToObject(usage, "output_tokens_details", details);
        }
      }
      if (sqlite3_column_type(stmt, 15) != SQLITE_NULL) {
        cJSON_AddNumberToObject(
          usage,
          "cost",
          (double)sqlite3_column_int64(stmt, 15) / 1000000000.0);
      }
      if ((sqlite3_column_type(stmt, 16) != SQLITE_NULL) ||
          (sqlite3_column_type(stmt, 17) != SQLITE_NULL) ||
          (sqlite3_column_type(stmt, 18) != SQLITE_NULL)) {
        details = cJSON_CreateObject();
        if (details != NULL) {
          if (sqlite3_column_type(stmt, 16) != SQLITE_NULL) {
            cJSON_AddNumberToObject(
              details,
              "upstream_inference_cost",
              (double)sqlite3_column_int64(stmt, 16) / 1000000000.0);
          }
          if (sqlite3_column_type(stmt, 17) != SQLITE_NULL) {
            cJSON_AddNumberToObject(
              details,
              "upstream_inference_input_cost",
              (double)sqlite3_column_int64(stmt, 17) / 1000000000.0);
          }
          if (sqlite3_column_type(stmt, 18) != SQLITE_NULL) {
            cJSON_AddNumberToObject(
              details,
              "upstream_inference_output_cost",
              (double)sqlite3_column_int64(stmt, 18) / 1000000000.0);
          }
          cJSON_AddItemToObject(usage, "cost_details", details);
        }
      }
      cJSON_AddBoolToObject(usage,
                            "is_byok",
                            sqlite3_column_int(stmt, 19) ? 1 : 0);
      cJSON_AddItemToObject(root, "usage", usage);
      has_metadata = 1;
    }
  }
  sqlite3_finalize(stmt);
  if (!has_metadata) {
    cJSON_Delete(root);
    serialized = strappy_string_duplicate("");
    if (serialized == NULL) {
      strappy_set_error(error_out, "Could not allocate Responses metadata.");
    }
    return serialized;
  }
  serialized = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (serialized == NULL) {
    strappy_set_error(error_out, "Could not serialize Responses metadata.");
  }
  return serialized;
}

static char *strappy_db_semantic_answer_quality_metadata(
  sqlite3 *db,
  long long audit_id,
  char **error_out)
{
  static const char *audit_sql =
    "SELECT outcome, guidance_version FROM answer_quality_audits "
    "WHERE id = ?;";
  static const char *checks_sql =
    "SELECT check_key, check_kind, label, status, tool_name, detail "
    "FROM answer_quality_checks WHERE audit_id = ? ORDER BY ordinal;";
  sqlite3_stmt *stmt;
  cJSON *root;
  cJSON *checks;
  char *serialized;
  const unsigned char *text;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, audit_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)audit_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_ROW)) {
    strappy_set_formatted_error(error_out,
                                "Could not read answer quality audit: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  root = cJSON_CreateObject();
  checks = cJSON_CreateArray();
  if ((root == NULL) || (checks == NULL)) {
    cJSON_Delete(root);
    cJSON_Delete(checks);
    sqlite3_finalize(stmt);
    strappy_set_error(error_out,
                      "Could not allocate answer quality metadata.");
    return NULL;
  }
  text = sqlite3_column_text(stmt, 0);
  cJSON_AddStringToObject(root,
                         "outcome",
                         (text != NULL) ? (const char *)text : "error");
  text = sqlite3_column_text(stmt, 1);
  if (text != NULL) {
    cJSON_AddStringToObject(root,
                           "guidance_version",
                           (const char *)text);
  }
  cJSON_AddItemToObject(root, "checks", checks);
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, checks_sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)audit_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not read answer quality checks: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    cJSON_Delete(root);
    return NULL;
  }
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    cJSON *check;

    check = cJSON_CreateObject();
    if (check == NULL) {
      sqlite3_finalize(stmt);
      cJSON_Delete(root);
      strappy_set_error(error_out,
                        "Could not allocate answer quality check metadata.");
      return NULL;
    }
    text = sqlite3_column_text(stmt, 0);
    cJSON_AddStringToObject(check,
                           "key",
                           (text != NULL) ? (const char *)text : "");
    text = sqlite3_column_text(stmt, 1);
    cJSON_AddStringToObject(check,
                           "kind",
                           (text != NULL) ? (const char *)text : "");
    text = sqlite3_column_text(stmt, 2);
    cJSON_AddStringToObject(check,
                           "label",
                           (text != NULL) ? (const char *)text : "");
    text = sqlite3_column_text(stmt, 3);
    cJSON_AddStringToObject(check,
                           "status",
                           (text != NULL) ? (const char *)text : "error");
    text = sqlite3_column_text(stmt, 4);
    if (text != NULL) {
      cJSON_AddStringToObject(check, "tool_name", (const char *)text);
    }
    text = sqlite3_column_text(stmt, 5);
    if (text != NULL) {
      cJSON_AddStringToObject(check, "detail", (const char *)text);
    }
    cJSON_AddItemToArray(checks, check);
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    cJSON_Delete(root);
    strappy_set_formatted_error(error_out,
                                "Could not read answer quality checks: %s",
                                sqlite3_errmsg(db));
    return NULL;
  }
  serialized = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (serialized == NULL) {
    strappy_set_error(error_out,
                      "Could not serialize answer quality metadata.");
  }
  return serialized;
}

static char *strappy_db_semantic_attempt_display_text(sqlite3_stmt *stmt)
{
  strappy_db_sql_buffer buffer;
  const unsigned char *method;
  const unsigned char *endpoint;
  const unsigned char *request_model;
  const unsigned char *started_at;
  const unsigned char *transport_error;
  const unsigned char *error_message;
  const unsigned char *incomplete_reason;
  char line[512];
  int written;

  memset(&buffer, 0, sizeof(buffer));
  method = sqlite3_column_text(stmt, 21);
  endpoint = sqlite3_column_text(stmt, 22);
  request_model = sqlite3_column_text(stmt, 27);
  started_at = sqlite3_column_text(stmt, 14);
  transport_error = sqlite3_column_text(stmt, 24);
  error_message = sqlite3_column_text(stmt, 25);
  incomplete_reason = sqlite3_column_text(stmt, 26);
  written = snprintf(line,
                     sizeof(line),
                     "Request: %s %s",
                     (method != NULL) ? (const char *)method : "POST",
                     (endpoint != NULL) ? (const char *)endpoint : "/responses");
  if ((written < 0) || ((size_t)written >= sizeof(line)) ||
      !strappy_db_sql_buffer_append(&buffer, line)) {
    strappy_db_sql_buffer_destroy(&buffer);
    return NULL;
  }
  if (request_model != NULL) {
    written = snprintf(line,
                       sizeof(line),
                       "\nModel: %s",
                       (const char *)request_model);
    if ((written < 0) || ((size_t)written >= sizeof(line)) ||
        !strappy_db_sql_buffer_append(&buffer, line)) {
      strappy_db_sql_buffer_destroy(&buffer);
      return NULL;
    }
  }
  if ((started_at != NULL) &&
      (!strappy_db_sql_buffer_append(&buffer, "\nStarted: ") ||
       !strappy_db_sql_buffer_append(&buffer, (const char *)started_at))) {
    strappy_db_sql_buffer_destroy(&buffer);
    return NULL;
  }
  if ((transport_error != NULL) &&
      (!strappy_db_sql_buffer_append(&buffer, "\nTransport error: ") ||
       !strappy_db_sql_buffer_append(&buffer,
                                     (const char *)transport_error))) {
    strappy_db_sql_buffer_destroy(&buffer);
    return NULL;
  }
  if ((error_message != NULL) &&
      (!strappy_db_sql_buffer_append(&buffer, "\nError: ") ||
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

static int strappy_db_semantic_timeline_append(
  strappy_session_message_record_list *list,
  strappy_session_message_record **record_out,
  char **error_out)
{
  strappy_session_message_record *records;

  if ((list == NULL) || (record_out == NULL) ||
      (list->count >= (((size_t)-1) /
                       sizeof(strappy_session_message_record)))) {
    strappy_set_error(error_out, "Responses timeline is too large.");
    return 0;
  }
  records = (strappy_session_message_record *)realloc(
    list->records,
    (list->count + 1U) * sizeof(*records));
  if (records == NULL) {
    strappy_set_error(error_out, "Could not allocate Responses timeline.");
    return 0;
  }
  list->records = records;
  *record_out = &list->records[list->count];
  strappy_session_message_record_init(*record_out);
  return 1;
}

static void strappy_db_semantic_finalize_timeline_costs(
  strappy_session_message_record_list *list)
{
  size_t group_start;
  double cumulative_cost;
  int has_cumulative_cost;

  if (list == NULL) {
    return;
  }
  group_start = 0U;
  cumulative_cost = 0.0;
  has_cumulative_cost = 0;
  while (group_start < list->count) {
    long long model_request_id;
    size_t group_end;
    size_t index;

    model_request_id = list->records[group_start].model_request_id;
    group_end = group_start;
    while ((group_end < list->count) &&
           (list->records[group_end].model_request_id == model_request_id)) {
      if ((list->records[group_end].kind != NULL) &&
          (strcmp(list->records[group_end].kind,
                  "response_api_call") == 0) &&
          list->records[group_end].has_cumulative_usage_cost) {
        cumulative_cost +=
          list->records[group_end].cumulative_usage_cost;
        has_cumulative_cost = 1;
      }
      group_end++;
    }
    for (index = group_start; index < group_end; index++) {
      list->records[index].cumulative_usage_cost = cumulative_cost;
      list->records[index].has_cumulative_usage_cost = has_cumulative_cost;
    }
    group_start = group_end;
  }
}

static int strappy_db_semantic_finalize_ranged_timeline_costs(
  sqlite3 *db,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out)
{
  static const char *sql =
    "SELECT SUM(u.cost_nano_usd) FROM api_usage u "
    "JOIN http_attempts a ON a.id = u.attempt_id "
    "JOIN model_requests r ON r.id = a.request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "WHERE t.session_id = ? AND r.id <= ? "
    "AND a.state NOT IN ('pending','running');";
  sqlite3_stmt *stmt;
  size_t group_start;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare ranged timeline cost: %s",
                                sqlite3_errmsg(db));
    return 0;
  }
  group_start = 0U;
  while (group_start < list->count) {
    long long model_request_id;
    double cumulative_cost;
    int has_cumulative_cost;
    size_t group_end;
    size_t index;

    model_request_id = list->records[group_start].model_request_id;
    if ((sqlite3_reset(stmt) != SQLITE_OK) ||
        (sqlite3_clear_bindings(stmt) != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt,
                            1,
                            (sqlite3_int64)session_id) != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt,
                            2,
                            (sqlite3_int64)model_request_id) != SQLITE_OK) ||
        (sqlite3_step(stmt) != SQLITE_ROW)) {
      strappy_set_formatted_error(error_out,
                                  "Could not read ranged timeline cost: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    has_cumulative_cost =
      (sqlite3_column_type(stmt, 0) != SQLITE_NULL) ? 1 : 0;
    cumulative_cost = has_cumulative_cost ?
      ((double)sqlite3_column_int64(stmt, 0) / 1000000000.0) : 0.0;
    group_end = group_start;
    while ((group_end < list->count) &&
           (list->records[group_end].model_request_id == model_request_id)) {
      group_end++;
    }
    for (index = group_start; index < group_end; index++) {
      list->records[index].cumulative_usage_cost = cumulative_cost;
      list->records[index].has_cumulative_usage_cost = has_cumulative_cost;
    }
    group_start = group_end;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static char *strappy_db_semantic_function_output_tool_name(
  sqlite3 *db,
  long long item_id,
  char **error_out)
{
  static const char *sql =
    "SELECT c.tool_name FROM function_outputs o "
    "JOIN function_calls c ON c.item_id = o.function_call_item_id "
    "WHERE o.item_id = ?;";
  sqlite3_stmt *stmt;
  char *tool_name;
  int rc;

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_ROW)) {
    strappy_set_formatted_error(error_out,
                                "Could not read function output tool name: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return NULL;
  }
  tool_name = strappy_db_column_string(stmt, 0);
  sqlite3_finalize(stmt);
  if (tool_name == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate function output tool name.");
  }
  return tool_name;
}

static int strappy_db_semantic_populate_timeline_item(
  sqlite3 *db,
  sqlite3_stmt *stmt,
  strappy_session_message_record *record,
  char **error_out)
{
  cJSON *item;
  cJSON *value;
  const char *role;

  item = strappy_db_semantic_load_item(
    db, (long long)sqlite3_column_int64(stmt, 5), error_out);
  if (item == NULL) {
    return 0;
  }
  role = strappy_db_response_item_display_role(
    item, (const char *)sqlite3_column_text(stmt, 6));
  record->kind = strappy_string_duplicate(
    strappy_db_semantic_json_string(item, "type"));
  record->render_role = strappy_string_duplicate(role);
  record->role = strappy_string_duplicate(role);
  record->content = strappy_db_response_item_display_text(item);
  record->message_json = cJSON_PrintUnformatted(item);
  record->direction = strappy_db_column_string(stmt, 6);
  record->message_key = strappy_db_response_timeline_key(
    "response-item", (long long)sqlite3_column_int64(stmt, 5));
  record->is_error = record->is_error ||
    strappy_db_response_item_is_error(item);
  value = cJSON_GetObjectItem(item, "call_id");
  if (cJSON_IsString(value)) {
    record->tool_call_id = strappy_string_duplicate(value->valuestring);
  }
  value = cJSON_GetObjectItem(item, "name");
  if (cJSON_IsString(value)) {
    record->tool_name = strappy_string_duplicate(value->valuestring);
  }
  if ((record->tool_name == NULL) &&
      (strcmp(role, "api_function_output") == 0)) {
    record->tool_name = strappy_db_semantic_function_output_tool_name(
      db,
      (long long)sqlite3_column_int64(stmt, 5),
      error_out);
    if (record->tool_name == NULL) {
      cJSON_Delete(item);
      return 0;
    }
  }
  value = cJSON_GetObjectItem(item, "arguments");
  if (cJSON_IsString(value)) {
    record->arguments_json = strappy_string_duplicate(value->valuestring);
  }
  value = cJSON_GetObjectItem(item, "output");
  if (cJSON_IsString(value)) {
    record->result_json = strappy_string_duplicate(value->valuestring);
  }
  value = cJSON_GetObjectItem(item, "action");
  if (value != NULL) {
    record->response_item_action_json = cJSON_PrintUnformatted(value);
  }
  value = cJSON_GetObjectItem(item, "url");
  if (cJSON_IsString(value)) {
    record->response_item_url = strappy_string_duplicate(value->valuestring);
  }
  value = cJSON_GetObjectItem(item, "title");
  if (cJSON_IsString(value)) {
    record->response_item_title = strappy_string_duplicate(value->valuestring);
  }
  value = cJSON_GetObjectItem(item, "status");
  if (cJSON_IsString(value)) {
    record->response_item_status = strappy_string_duplicate(value->valuestring);
  }
  value = cJSON_GetObjectItem(item, "httpStatus");
  if (cJSON_IsString(value)) {
    record->response_item_http_status = strappy_string_duplicate(value->valuestring);
  } else if (cJSON_IsNumber(value)) {
    char http_status[64];
    int written;

    written = snprintf(http_status,
                       sizeof(http_status),
                       "%.0f",
                       value->valuedouble);
    if ((written >= 0) && ((size_t)written < sizeof(http_status))) {
      record->response_item_http_status =
        strappy_string_duplicate(http_status);
    }
  }
  if (strcmp(role, "developer") == 0) {
    record->actor = strappy_string_duplicate("developer");
    record->api_role = strappy_string_duplicate("developer");
  } else if (strcmp(role, "harness") == 0) {
    record->actor = strappy_string_duplicate("harness");
    record->api_role = strappy_string_duplicate("developer");
  } else if (strcmp(role, "user") == 0) {
    record->actor = strappy_string_duplicate("user");
    record->api_role = strappy_string_duplicate("user");
  } else {
    record->actor = strappy_string_duplicate("assistant");
    record->api_role = strappy_string_duplicate("assistant");
  }
  cJSON_Delete(item);
  return (record->kind != NULL) && (record->render_role != NULL) &&
    (record->role != NULL) && (record->content != NULL) &&
    (record->message_json != NULL) && (record->direction != NULL) &&
    (record->message_key != NULL) && (record->actor != NULL) &&
    (record->api_role != NULL);
}

static int strappy_db_semantic_list_response_timeline(
  const char *db_path,
  long long session_id,
  size_t start_index,
  strappy_session_message_record_list *list,
  size_t *total_count_out,
  char **error_out)
{
  static const char *count_sql =
    "SELECT "
    "(SELECT COUNT(*) FROM http_attempts a "
      "JOIN model_requests r ON r.id = a.request_id "
      "JOIN turns t ON t.id = r.turn_id "
      "WHERE t.session_id = ? AND a.state NOT IN ('pending','running')) + "
    "(SELECT COUNT(*) FROM conversation_items i "
      "JOIN model_requests r ON r.id = i.introduced_request_id "
      "JOIN turns t ON t.id = r.turn_id "
      "WHERE i.session_id = ? AND i.timeline_visible = 1) + "
    "(SELECT COUNT(*) FROM conversation_items i "
      "JOIN http_attempts a ON a.id = i.source_attempt_id "
      "WHERE i.session_id = ? AND i.timeline_visible = 1 "
      "AND a.state NOT IN ('pending','running')) + "
    "(SELECT COUNT(*) FROM answer_quality_audits q "
      "JOIN http_attempts a ON a.id = q.response_attempt_id "
      "JOIN model_requests r ON r.id = a.request_id "
      "JOIN turns t ON t.id = r.turn_id "
      "WHERE t.session_id = ? "
      "AND a.state NOT IN ('pending','running'));";
  static const char *sql =
    "SELECT 0 AS entry_type, a.id AS row_id, t.id AS turn_id, "
    "r.id AS request_id, a.id AS attempt_id, NULL AS item_id, "
    "NULL AS direction, t.prompt_group_key, r.request_kind, "
    "r.round_index, a.attempt_index, a.state, a.http_status, "
    "COALESCE(ar.provider_model_id, r.model_id), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', a.started_at_ms / 1000.0, 'unixepoch'), "
    "CASE WHEN a.state <> 'completed' OR a.http_status >= 400 "
      "OR ar.error_message IS NOT NULL OR ar.parse_error IS NOT NULL "
      "THEN 1 ELSE 0 END, 0, u.cost_nano_usd, "
    "1 AS group_phase, 0 AS attempt_phase, -1 AS item_index, "
    "a.method, a.endpoint, ar.provider_status, a.transport_error, "
    "COALESCE(ar.error_message, ar.parse_error), ar.incomplete_reason, "
    "r.model_id, 0 AS can_include_in_context "
    "FROM http_attempts a JOIN model_requests r ON r.id = a.request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "LEFT JOIN api_results ar ON ar.attempt_id = a.id "
    "LEFT JOIN api_usage u ON u.attempt_id = a.id "
    "WHERE t.session_id = ? AND a.state NOT IN ('pending','running') "
    "UNION ALL "
    "SELECT 2, i.id, t.id, r.id, NULL, i.id, 'request', "
    "t.prompt_group_key, r.request_kind, r.round_index, -1, NULL, 0, "
    "r.model_id, "
    "strftime('%Y-%m-%dT%H:%M:%fZ', i.created_at_ms / 1000.0, 'unixepoch'), "
    "i.is_error, i.include_in_context, NULL, "
    "0, 0, i.source_item_index, NULL, NULL, NULL, NULL, NULL, NULL, "
    "r.model_id, 1 "
    "FROM conversation_items i "
    "JOIN model_requests r ON r.id = i.introduced_request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "WHERE i.session_id = ? AND i.timeline_visible = 1 "
    "UNION ALL "
    "SELECT 1, q.id, t.id, r.id, a.id, "
    "(SELECT ci.id FROM conversation_items ci "
      "JOIN message_items mi ON mi.item_id = ci.id "
      "WHERE ci.source_attempt_id = a.id AND mi.role = 'assistant' "
      "ORDER BY ci.source_item_index LIMIT 1), 'response', "
    "t.prompt_group_key, r.request_kind, r.round_index, a.attempt_index, "
    "a.state, a.http_status, COALESCE(ar.provider_model_id, r.model_id), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', q.evaluated_at_ms / 1000.0, 'unixepoch'), "
    "CASE WHEN q.outcome = 'passed' THEN 0 ELSE 1 END, 0, NULL, "
    "1, 1, COALESCE((SELECT MIN(ci.source_item_index) "
      "FROM conversation_items ci "
      "JOIN message_items mi ON mi.item_id = ci.id "
      "WHERE ci.source_attempt_id = a.id AND mi.role = 'assistant'), "
      "2147483647), "
    "a.method, a.endpoint, ar.provider_status, a.transport_error, "
    "COALESCE(ar.error_message, ar.parse_error), ar.incomplete_reason, "
    "r.model_id, 0 "
    "FROM answer_quality_audits q "
    "JOIN http_attempts a ON a.id = q.response_attempt_id "
    "JOIN model_requests r ON r.id = a.request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "LEFT JOIN api_results ar ON ar.attempt_id = a.id "
    "WHERE t.session_id = ? AND a.state NOT IN ('pending','running') "
    "UNION ALL "
    "SELECT 2, i.id, t.id, r.id, a.id, i.id, 'response', "
    "t.prompt_group_key, r.request_kind, r.round_index, a.attempt_index, a.state, "
    "a.http_status, COALESCE(ar.provider_model_id, r.model_id), "
    "strftime('%Y-%m-%dT%H:%M:%fZ', i.created_at_ms / 1000.0, 'unixepoch'), "
    "i.is_error, i.include_in_context, NULL, "
    "1, 1, i.source_item_index, "
    "a.method, a.endpoint, ar.provider_status, a.transport_error, "
    "COALESCE(ar.error_message, ar.parse_error), ar.incomplete_reason, "
    "r.model_id, CASE WHEN i.include_in_context = 1 OR (" \
      STRAPPY_DB_CONTEXT_ELIGIBLE_ATTEMPT_SQL \
    ") THEN 1 ELSE 0 END "
    "FROM conversation_items i "
    "JOIN http_attempts a ON a.id = i.source_attempt_id "
    "JOIN model_requests r ON r.id = a.request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "LEFT JOIN api_results ar ON ar.attempt_id = a.id "
    "WHERE i.session_id = ? AND i.timeline_visible = 1 "
    "AND a.state NOT IN ('pending','running') "
    "ORDER BY request_id, 19, attempt_index, 20, 21, entry_type "
    "LIMIT -1 OFFSET ?;";
  sqlite3 *db;
  sqlite3_stmt *count_stmt;
  sqlite3_stmt *stmt;
  sqlite3_int64 total_count;
  int rc;

  if (total_count_out != NULL) {
    *total_count_out = 0U;
  }
  if (list == NULL) {
    strappy_set_error(error_out, "Responses timeline has no output.");
    return 0;
  }
  strappy_session_message_record_list_init(list);
  if (session_id <= 0LL) {
    strappy_set_error(error_out, "Session id is not valid.");
    return 0;
  }
  if (start_index > (size_t)LLONG_MAX) {
    strappy_set_error(error_out, "Responses timeline range is too large.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  count_stmt = NULL;
  rc = sqlite3_prepare_v2(db, count_sql, -1, &count_stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(count_stmt, 1,
                          (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(count_stmt, 2,
                          (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(count_stmt, 3,
                          (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(count_stmt, 4,
                          (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not count semantic Responses timeline: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(count_stmt);
    strappy_db_release(db);
    return 0;
  }
  rc = sqlite3_step(count_stmt);
  if (rc != SQLITE_ROW) {
    strappy_set_formatted_error(error_out,
                                "Could not count semantic Responses timeline: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(count_stmt);
    strappy_db_release(db);
    return 0;
  }
  total_count = sqlite3_column_int64(count_stmt, 0);
  sqlite3_finalize(count_stmt);
  if ((total_count < 0) ||
      ((unsigned long long)total_count > (unsigned long long)((size_t)-1))) {
    strappy_set_error(error_out, "Responses timeline is too large.");
    strappy_db_release(db);
    return 0;
  }
  if (total_count_out != NULL) {
    *total_count_out = (size_t)total_count;
  }
  if (start_index > (size_t)total_count) {
    strappy_set_error(error_out,
                      "Responses timeline range exceeds stored messages.");
    strappy_db_release(db);
    return 0;
  }
  if (start_index == (size_t)total_count) {
    strappy_db_release(db);
    return 1;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 4,
                          (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 5,
                          (sqlite3_int64)start_index) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare semantic Responses timeline: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    strappy_session_message_record *record;
    long long attempt_id;
    long long row_id;
    int entry_type;

    if (!strappy_db_semantic_timeline_append(list, &record, error_out)) {
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }
    entry_type = sqlite3_column_int(stmt, 0);
    row_id = (long long)sqlite3_column_int64(stmt, 1);
    attempt_id = (sqlite3_column_type(stmt, 4) != SQLITE_NULL) ?
      (long long)sqlite3_column_int64(stmt, 4) : 0LL;
    record->message_id = (entry_type == 0) ?
      (attempt_id * 2LL) :
      ((entry_type == 1) ? 0LL : ((row_id * 2LL) + 1LL));
    record->session_id = session_id;
    record->turn_id = (long long)sqlite3_column_int64(stmt, 2);
    record->model_request_id = (long long)sqlite3_column_int64(stmt, 3);
    record->http_attempt_id = attempt_id;
    record->round_index = (long)sqlite3_column_int64(stmt, 9);
    record->attempt_index = (sqlite3_column_type(stmt, 10) != SQLITE_NULL) ?
      (long)sqlite3_column_int64(stmt, 10) : 0L;
    if ((entry_type == 0) &&
        (sqlite3_column_type(stmt, 17) != SQLITE_NULL)) {
      record->cumulative_usage_cost =
        (double)sqlite3_column_int64(stmt, 17) / 1000000000.0;
      record->has_cumulative_usage_cost = 1;
    }
    record->turn_key = strappy_db_column_string(stmt, 7);
    record->prompt_group_key = strappy_db_column_string(stmt, 7);
    record->model = strappy_db_column_string(stmt, 13);
    record->created_at = strappy_db_column_string(stmt, 14);
    record->http_status = (long)sqlite3_column_int64(stmt, 12);
    record->attempt_state = strappy_db_column_string(stmt, 11);
    record->is_error = sqlite3_column_int(stmt, 15) ? 1 : 0;
    record->can_include_in_context =
      sqlite3_column_int(stmt, 28) ? 1 : 0;
    record->include_in_context = sqlite3_column_int(stmt, 16) ? 1 : 0;
    if (entry_type == 0) {
      record->request_method = strappy_db_column_string(stmt, 21);
      record->request_endpoint = strappy_db_column_string(stmt, 22);
      record->actor = strappy_string_duplicate("api");
      record->kind = strappy_string_duplicate("response_api_call");
      record->render_role = strappy_string_duplicate(
        record->is_error ? "api_error" : "api_call");
      record->role = strappy_string_duplicate(
        record->is_error ? "api_error" : "api_call");
      record->content = strappy_db_semantic_attempt_display_text(stmt);
      record->metadata_json = strappy_db_semantic_attempt_metadata(
        db, attempt_id, error_out);
      record->message_json = (record->metadata_json != NULL) ?
        strappy_string_duplicate(record->metadata_json) : NULL;
      record->message_key =
        strappy_db_response_timeline_key("response-call", attempt_id);
    } else if (entry_type == 1) {
      long long target_item_id;

      target_item_id = (sqlite3_column_type(stmt, 5) != SQLITE_NULL) ?
        (long long)sqlite3_column_int64(stmt, 5) : 0LL;
      record->actor = strappy_string_duplicate("audit");
      record->kind = strappy_string_duplicate("answer_quality");
      record->render_role = strappy_string_duplicate("answer_quality");
      record->role = strappy_string_duplicate("answer_quality");
      record->content = strappy_string_duplicate("Answer Quality");
      record->direction = strappy_db_column_string(stmt, 6);
      record->metadata_json =
        strappy_db_semantic_answer_quality_metadata(db, row_id, error_out);
      record->message_json = (record->metadata_json != NULL) ?
        strappy_string_duplicate(record->metadata_json) : NULL;
      record->message_key =
        strappy_db_response_timeline_key("answer-quality", row_id);
      if (target_item_id > 0LL) {
        record->target_message_key =
          strappy_db_response_timeline_key("response-item", target_item_id);
      }
    } else if (!strappy_db_semantic_populate_timeline_item(db,
                                                           stmt,
                                                           record,
                                                           error_out)) {
      strappy_session_message_record_destroy(record);
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }
    if ((record->turn_key == NULL) || (record->prompt_group_key == NULL) ||
        (record->actor == NULL) || (record->kind == NULL) ||
        (record->render_role == NULL) || (record->role == NULL) ||
        (record->content == NULL) || (record->message_key == NULL) ||
        ((record->http_attempt_id > 0LL) &&
         (record->attempt_state == NULL)) ||
        ((entry_type == 0) && ((record->request_method == NULL) ||
                              (record->request_endpoint == NULL))) ||
        ((entry_type != 0) && (record->direction == NULL)) ||
        (record->created_at == NULL) ||
        ((entry_type == 0) && ((record->metadata_json == NULL) ||
                              (record->message_json == NULL))) ||
        ((entry_type == 1) && ((record->metadata_json == NULL) ||
                              (record->message_json == NULL)))) {
      strappy_session_message_record_destroy(record);
      strappy_set_error(error_out,
                        "Could not allocate semantic Responses timeline row.");
      sqlite3_finalize(stmt);
      strappy_db_release(db);
      strappy_session_message_record_list_destroy(list);
      return 0;
    }
    list->count++;
  }
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    strappy_db_release(db);
    strappy_session_message_record_list_destroy(list);
    strappy_set_error(error_out, "Could not read semantic Responses timeline.");
    return 0;
  }
  if (start_index == 0U) {
    strappy_db_semantic_finalize_timeline_costs(list);
  } else if (!strappy_db_semantic_finalize_ranged_timeline_costs(db,
                                                                 session_id,
                                                                 list,
                                                                 error_out)) {
    strappy_db_release(db);
    strappy_session_message_record_list_destroy(list);
    return 0;
  }
  strappy_db_release(db);
  return 1;
}

int strappy_db_list_response_timeline_range(
  const char *db_path,
  long long session_id,
  size_t start_index,
  strappy_session_message_record_list *list,
  size_t *total_count_out,
  char **error_out)
{
  return strappy_db_semantic_list_response_timeline(db_path,
                                                    session_id,
                                                    start_index,
                                                    list,
                                                    total_count_out,
                                                    error_out);
}

int strappy_db_update_model_request_include_in_context(
  const char *db_path,
  long long session_id,
  long long model_request_id,
  int include_in_context,
  char **error_out)
{
  static const char *sql =
    "UPDATE conversation_items SET include_in_context = ? "
    "WHERE session_id = ? AND (introduced_request_id = ? OR ("
      "source_attempt_id IS NOT NULL AND EXISTS ("
        "SELECT 1 FROM http_attempts a "
        "LEFT JOIN api_results ar ON ar.attempt_id = a.id "
        "WHERE a.id = conversation_items.source_attempt_id "
        "AND a.request_id = ? AND ("
          "conversation_items.include_in_context = 1 OR (" \
            STRAPPY_DB_CONTEXT_ELIGIBLE_ATTEMPT_SQL \
          ")"
        ")"
      ")"
    ")) AND EXISTS ("
      "SELECT 1 FROM model_requests r "
      "JOIN turns t ON t.id = r.turn_id "
      "WHERE r.id = ? AND t.session_id = ?"
    ");";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;
  int ok;

  if ((session_id <= 0LL) || (model_request_id <= 0LL) ||
      ((include_in_context != 0) && (include_in_context != 1))) {
    strappy_set_error(error_out,
                      "Context round inclusion update is not valid.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(
      error_out,
      "Could not prepare context round inclusion update: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  ok =
    (sqlite3_bind_int(stmt, 1, include_in_context) == SQLITE_OK) &&
    (sqlite3_bind_int64(stmt,
                        2,
                        (sqlite3_int64)session_id) == SQLITE_OK) &&
    (sqlite3_bind_int64(stmt,
                        3,
                        (sqlite3_int64)model_request_id) == SQLITE_OK) &&
    (sqlite3_bind_int64(stmt,
                        4,
                        (sqlite3_int64)model_request_id) == SQLITE_OK) &&
    (sqlite3_bind_int64(stmt,
                        5,
                        (sqlite3_int64)model_request_id) == SQLITE_OK) &&
    (sqlite3_bind_int64(stmt,
                        6,
                        (sqlite3_int64)session_id) == SQLITE_OK);
  if (!ok) {
    strappy_set_formatted_error(
      error_out,
      "Could not bind context round inclusion update: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(
      error_out,
      "Could not update context round inclusion: %s",
      sqlite3_errmsg(db));
    ok = 0;
  } else if (sqlite3_changes(db) <= 0) {
    strappy_set_error(
      error_out,
      "Context round was not found or has no context-eligible items.");
    ok = 0;
  } else {
    ok = 1;
  }
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return ok;
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

  return strappy_db_list_response_timeline_range(db_path,
                                                 session_id,
                                                 0U,
                                                 list,
                                                 NULL,
                                                 error_out);

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
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare Responses timeline: %s",
                                sqlite3_errmsg(db));
    strappy_db_release(db);
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
    strappy_db_release(db);
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
      strappy_db_release(db);
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
      strappy_db_release(db);
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
      strappy_db_release(db);
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
    strappy_db_release(db);
    strappy_session_message_record_list_destroy(list);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}
