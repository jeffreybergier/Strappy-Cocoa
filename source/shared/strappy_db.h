#ifndef STRAPPY_DB_H
#define STRAPPY_DB_H

#include "strappy_config.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct strappy_session_record {
  long long session_id;
  char *name;
  char *prompt;
  char *response;
  char *model;
  char *model_name;
  char *assistant_set_id;
  char *created_at;
  char *last_activity_at;
  long long last_activity_at_ms;
  strappy_web_provider web_provider;
  int bash_enabled;
  int streaming_enabled;
  long http_status;
} strappy_session_record;

typedef struct strappy_session_record_list {
  strappy_session_record *records;
  size_t count;
} strappy_session_record_list;

typedef struct strappy_response_timeline_cursor {
  long long session_id;
  long long request_id;
  long long group_phase;
  long long attempt_index;
  long long attempt_phase;
  long long item_index;
  long long entry_type;
  long long row_id;
  int valid;
} strappy_response_timeline_cursor;

typedef struct strappy_session_message_record {
  long long message_id;
  long long session_id;
  long long turn_id;
  long long model_request_id;
  long long http_attempt_id;
  long round_index;
  long attempt_index;
  double cumulative_usage_cost;
  int has_cumulative_usage_cost;
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
  char *direction;
  char *tool_call_id;
  char *tool_name;
  char *arguments_json;
  char *result_json;
  char *response_item_action_json;
  char *response_item_url;
  char *response_item_title;
  char *response_item_status;
  char *response_item_http_status;
  char *request_method;
  char *request_endpoint;
  char *created_at;
  char *attempt_state;
  int can_include_in_context;
  int include_in_context;
  int is_error;
  long http_status;
  strappy_response_timeline_cursor timeline_cursor;
} strappy_session_message_record;

typedef struct strappy_session_message_record_list {
  strappy_session_message_record *records;
  size_t count;
} strappy_session_message_record_list;

typedef struct strappy_session_message_input {
  const char *turn_key;
  const char *prompt_group_key;
  const char *actor;
  const char *context_policy;
  const char *kind;
  const char *api_role;
  const char *render_role;
  const char *role;
  const char *content;
  const char *model;
  long http_status;
  const char *metadata_json;
  const char *render_state_json;
  const char *message_json;
  const char *reasoning;
  const char *message_key;
  const char *target_message_key;
  const char *tool_call_id;
  const char *tool_name;
  const char *arguments_json;
  const char *result_json;
  int include_in_context;
  int is_error;
} strappy_session_message_input;

typedef struct strappy_response_call_begin_input {
  long long session_id;
  long long previous_call_id;
  const char *prompt_group_key;
  const char *request_kind;
  long round_index;
  long attempt_index;
  long new_input_start_index;
  int input_from_current_turn;
  const char *request_method;
  const char *request_url;
  const char *request_headers_json;
  const char *request_json;
} strappy_response_call_begin_input;

typedef struct strappy_answer_quality_check_input {
  const char *check_key;
  const char *check_kind;
  const char *label;
  const char *status;
  const char *tool_name;
  const char *detail;
  long long evidence_item_id;
} strappy_answer_quality_check_input;

typedef struct strappy_answer_quality_audit_input {
  const char *outcome;
  const char *guidance_version;
  long long evaluated_at_ms;
  const strappy_answer_quality_check_input *checks;
  size_t check_count;
} strappy_answer_quality_audit_input;

typedef struct strappy_response_call_finish_input {
  long long call_id;
  const char *state;
  int is_error;
  int output_is_canonical;
  long http_status;
  long retry_after_seconds;
  long curl_code;
  long long started_at_ms;
  long long completed_at_ms;
  long long request_bytes;
  long long response_bytes;
  double name_lookup_seconds;
  double connect_seconds;
  double start_transfer_seconds;
  double total_seconds;
  const char *effective_url;
  const char *transport_error;
  const char *content_type;
  const char *request_id;
  const char *generation_id;
  const char *rate_limit_limit;
  const char *rate_limit_remaining;
  const char *rate_limit_reset;
  const char *rate_limit_limit_requests;
  const char *rate_limit_remaining_requests;
  const char *rate_limit_reset_requests;
  const char *rate_limit_limit_tokens;
  const char *rate_limit_remaining_tokens;
  const char *rate_limit_reset_tokens;
  const char *response_headers;
  const char *response_json;
  const strappy_answer_quality_audit_input *answer_quality_audit;
} strappy_response_call_finish_input;

typedef struct strappy_response_item_raw_record {
  long long item_id;
  char *raw_json;
} strappy_response_item_raw_record;

typedef struct strappy_response_item_raw_record_list {
  strappy_response_item_raw_record *records;
  size_t count;
} strappy_response_item_raw_record_list;

typedef struct strappy_response_tool_execution_input {
  long long session_id;
  long long response_call_id;
  long output_index;
  const char *call_id;
  const char *tool_name;
  const char *arguments_json;
  const char *status;
  const char *output_json;
  const char *error_text;
  long long started_at_ms;
  long long completed_at_ms;
} strappy_response_tool_execution_input;

typedef struct strappy_discovered_database_input {
  const char *path;
  long long size;
  long long modified_at;
  unsigned long long device;
  unsigned long long inode;
  int is_valid_sqlite;
  const char *validation_error;
  const char *scan_root;
  const char *app_group_key;
  const char *app_name;
  const char *app_bundle_id;
  const char *app_container_path;
  const char *app_bundle_path;
  const char *app_source;
  const char *origin_kind;
  const char *location_tail;
  int hidden;
} strappy_discovered_database_input;

typedef struct strappy_discovered_database_record {
  long long catalog_id;
  char *assistant_database_id;
  char *path;
  long long size;
  long long modified_at;
  unsigned long long device;
  unsigned long long inode;
  int is_valid_sqlite;
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
  int hidden;
  char *first_seen_at;
  char *last_seen_at;
  char *last_scanned_at;
} strappy_discovered_database_record;

typedef struct strappy_discovered_database_record_list {
  strappy_discovered_database_record *records;
  size_t count;
} strappy_discovered_database_record_list;

typedef struct strappy_database_display_name_record {
  char *assistant_database_id;
  char *filename;
} strappy_database_display_name_record;

typedef struct strappy_database_display_name_record_list {
  strappy_database_display_name_record *records;
  size_t count;
} strappy_database_display_name_record_list;

typedef struct strappy_openrouter_model_record {
  char *model_id;
  char *canonical_slug;
  char *hugging_face_id;
  char *name;
  char *description;
  long long context_length;
  long long created;
  char *architecture_modality;
  char *architecture_tokenizer;
  char *architecture_instruct_type;
  char *pricing_prompt;
  char *pricing_completion;
  char *pricing_request;
  char *pricing_image;
  char *pricing_audio;
  char *pricing_web_search;
  char *pricing_internal_reasoning;
  char *pricing_input_cache_read;
  char *pricing_input_cache_write;
  long long top_provider_context_length;
  long long top_provider_max_completion_tokens;
  int top_provider_is_moderated;
  char *knowledge_cutoff;
  char *expiration_date;
  char *links_details;
  char *links_json;
  char *reasoning_json;
  char *benchmarks_json;
  char *default_parameters_json;
  char *per_request_limits_json;
  char *raw_json;
  char *fetched_at;
  int selected;
  int allowed;
} strappy_openrouter_model_record;

typedef struct strappy_openrouter_model_record_list {
  strappy_openrouter_model_record *records;
  size_t count;
} strappy_openrouter_model_record_list;

void strappy_session_record_init(strappy_session_record *record);
void strappy_session_record_destroy(strappy_session_record *record);
void strappy_session_record_list_init(strappy_session_record_list *list);
void strappy_session_record_list_destroy(strappy_session_record_list *list);
void strappy_response_timeline_cursor_init(
  strappy_response_timeline_cursor *cursor);
void strappy_session_message_record_init(strappy_session_message_record *record);
void strappy_session_message_record_destroy(strappy_session_message_record *record);
void strappy_session_message_record_list_init(strappy_session_message_record_list *list);
void strappy_session_message_record_list_destroy(strappy_session_message_record_list *list);
void strappy_response_item_raw_record_list_init(
  strappy_response_item_raw_record_list *list);
void strappy_response_item_raw_record_list_destroy(
  strappy_response_item_raw_record_list *list);
void strappy_discovered_database_record_init(strappy_discovered_database_record *record);
void strappy_discovered_database_record_destroy(strappy_discovered_database_record *record);
void strappy_discovered_database_record_list_init(strappy_discovered_database_record_list *list);
void strappy_discovered_database_record_list_destroy(strappy_discovered_database_record_list *list);
void strappy_database_display_name_record_list_init(
  strappy_database_display_name_record_list *list);
void strappy_database_display_name_record_list_destroy(
  strappy_database_display_name_record_list *list);
void strappy_openrouter_model_record_init(strappy_openrouter_model_record *record);
void strappy_openrouter_model_record_destroy(strappy_openrouter_model_record *record);
void strappy_openrouter_model_record_list_init(strappy_openrouter_model_record_list *list);
void strappy_openrouter_model_record_list_destroy(strappy_openrouter_model_record_list *list);

int strappy_db_initialize(const char *db_path, char **error_out);
int strappy_db_save_discovered_databases(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  char **error_out);
int strappy_db_save_discovered_databases_for_scan_run(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  long long scan_run_id,
  char **error_out);
int strappy_db_begin_discovered_database_scan(
  const char *db_path,
  const char *scan_root,
  long long *scan_run_id_out,
  char **error_out);
int strappy_db_finish_discovered_database_scan(
  const char *db_path,
  long long scan_run_id,
  const char *state,
  const char *error_message,
  char **error_out);
int strappy_db_replace_discovered_databases_for_scan_root(
  const char *db_path,
  const strappy_discovered_database_input *records,
  size_t count,
  const char *scan_root,
  char **error_out);
int strappy_db_list_discovered_databases(
  const char *db_path,
  strappy_discovered_database_record_list *list,
  char **error_out);
int strappy_db_list_approved_database_display_names(
  const char *db_path,
  strappy_database_display_name_record_list *list,
  char **error_out);
int strappy_db_update_discovered_database_decision(
  const char *db_path,
  long long catalog_id,
  const char *user_decision,
  char **error_out);
int strappy_db_update_discovered_database_hidden(const char *db_path,
                                                 long long catalog_id,
                                                 int hidden,
                                                 char **error_out);
int strappy_db_save_exchange(const char *db_path,
                             const char *prompt,
                             const char *response,
                             const char *model,
                             long http_status,
                             const char *metadata_json,
                             const char *message_json,
                             const char *reasoning,
                             char **error_out);
int strappy_db_save_exchange_with_id(const char *db_path,
                                     const char *prompt,
                                     const char *response,
                                     const char *model,
                                     long http_status,
                                     const char *metadata_json,
                                     const char *message_json,
                                     const char *reasoning,
                                     long long *session_id_out,
                                     char **error_out);
int strappy_db_create_session(const char *db_path,
                              long long *session_id_out,
                              char **error_out);
int strappy_db_create_session_with_working_directory(
  const char *db_path,
  const char *working_directory,
  long long *session_id_out,
  char **error_out);
int strappy_db_get_session_working_directory(
  const char *db_path,
  long long session_id,
  char **working_directory_out,
  char **error_out);
int strappy_db_update_session_working_directory(
  const char *db_path,
  long long session_id,
  const char *working_directory,
  char **error_out);
int strappy_db_update_session_name(const char *db_path,
                                   long long session_id,
                                   const char *name,
                                   char **error_out);
int strappy_db_update_session_streaming_enabled(const char *db_path,
                                                long long session_id,
                                                int streaming_enabled,
                                                char **error_out);
int strappy_db_update_session_web_provider(
  const char *db_path,
  long long session_id,
  strappy_web_provider web_provider,
  char **error_out);
int strappy_db_get_session_bash_enabled(const char *db_path,
                                        long long session_id,
                                        int *bash_enabled_out,
                                        char **error_out);
int strappy_db_update_session_bash_enabled(const char *db_path,
                                           long long session_id,
                                           int bash_enabled,
                                           char **error_out);
int strappy_db_get_session_assistant_set(const char *db_path,
                                         long long session_id,
                                         char **assistant_set_id_out,
                                         char **error_out);
int strappy_db_update_session_assistant_set(const char *db_path,
                                            long long session_id,
                                            const char *assistant_set_id,
                                            char **error_out);
int strappy_db_list_sessions(const char *db_path,
                             strappy_session_record_list *list,
                             char **error_out);
int strappy_db_load_session(const char *db_path,
                            long long session_id,
                            strappy_session_record *record,
                            char **error_out);
int strappy_db_load_session_list_record(const char *db_path,
                                        long long session_id,
                                        strappy_session_record *record,
                                        char **error_out);
int strappy_db_delete_session(const char *db_path,
                              long long session_id,
                              char **error_out);
int strappy_db_append_exchange_to_session(const char *db_path,
                                          long long session_id,
                                          const char *prompt,
                                          const char *response,
                                          const char *model,
                                          long http_status,
                                          const char *metadata_json,
                                          const char *message_json,
                                          const char *reasoning,
                                          char **error_out);
int strappy_db_save_message_sequence_with_id(
  const char *db_path,
  const char *prompt,
  const char *response,
  const char *model,
  long http_status,
  const strappy_session_message_input *messages,
  size_t message_count,
  long long *session_id_out,
  char **error_out);
int strappy_db_append_message_sequence_to_session(
  const char *db_path,
  long long session_id,
  const char *prompt,
  const char *response,
  const char *model,
  long http_status,
  const strappy_session_message_input *messages,
  size_t message_count,
  char **error_out);
int strappy_db_upsert_session_message(
  const char *db_path,
  long long session_id,
  const strappy_session_message_input *message,
  char **error_out);
int strappy_db_append_session_message_content(
  const char *db_path,
  long long session_id,
  const strappy_session_message_input *message,
  const char *content_delta,
  const char *reasoning_delta,
  char **error_out);
int strappy_db_move_session_message_content_to_reasoning(
  const char *db_path,
  long long session_id,
  const strappy_session_message_input *message,
  char **error_out);
int strappy_db_update_session_message_render_state(
  const char *db_path,
  long long session_id,
  const char *message_key,
  const char *render_state_json,
  char **error_out);
int strappy_db_list_session_messages(const char *db_path,
                                     long long session_id,
                                     strappy_session_message_record_list *list,
                                     char **error_out);
int strappy_db_load_session_message_by_key(
  const char *db_path,
  long long session_id,
  const char *message_key,
  strappy_session_message_record *record,
  char **error_out);
int strappy_db_list_session_context_messages(
  const char *db_path,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out);
int strappy_db_begin_response_call(
  const char *db_path,
  const strappy_response_call_begin_input *input,
  long long *call_id_out,
  char **error_out);
int strappy_db_finish_response_call(
  const char *db_path,
  const strappy_response_call_finish_input *input,
  char **error_out);
int strappy_db_list_canonical_response_items(
  const char *db_path,
  long long session_id,
  strappy_response_item_raw_record_list *list,
  char **error_out);
int strappy_db_list_canonical_response_items_for_prompt_group(
  const char *db_path,
  long long session_id,
  const char *prompt_group_key,
  strappy_response_item_raw_record_list *list,
  char **error_out);
int strappy_db_save_response_tool_execution(
  const char *db_path,
  const strappy_response_tool_execution_input *input,
  char **error_out);
int strappy_db_finalize_cancelled_response_tool_outputs(
  const char *db_path,
  long long session_id,
  long long response_call_id,
  const char * const *item_jsons,
  size_t item_count,
  char **error_out);
int strappy_db_update_response_session_summary(
  const char *db_path,
  long long session_id,
  const char *prompt,
  const char *response,
  const char *model,
  long http_status,
  char **error_out);
int strappy_db_list_response_timeline(
  const char *db_path,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out);
int strappy_db_list_response_timeline_after(
  const char *db_path,
  long long session_id,
  const strappy_response_timeline_cursor *after_cursor,
  strappy_session_message_record_list *list,
  strappy_response_timeline_cursor *next_cursor_out,
  char **error_out);
int strappy_db_update_model_request_include_in_context(
  const char *db_path,
  long long session_id,
  long long model_request_id,
  int include_in_context,
  char **error_out);
int strappy_db_save_openrouter_models_json(const char *db_path,
                                           const char *json,
                                           char **error_out);
int strappy_db_list_openrouter_models_matching(
  const char *db_path,
  const char *search_text,
  strappy_openrouter_model_record_list *list,
  char **error_out);
int strappy_db_list_openrouter_models(
  const char *db_path,
  strappy_openrouter_model_record_list *list,
  char **error_out);
int strappy_db_list_allowed_openrouter_models(
  const char *db_path,
  strappy_openrouter_model_record_list *list,
  char **error_out);
int strappy_db_set_openrouter_model_allowed(const char *db_path,
                                            const char *model_id,
                                            int allowed,
                                            char **error_out);
int strappy_db_set_default_openrouter_model(const char *db_path,
                                            const char *model_id,
                                            char **error_out);
int strappy_db_get_default_openrouter_model(const char *db_path,
                                            char **model_id_out,
                                            char **error_out);
int strappy_db_set_selected_openrouter_model(const char *db_path,
                                             const char *model_id,
                                             char **error_out);
int strappy_db_get_selected_openrouter_model(const char *db_path,
                                             char **model_id_out,
                                             char **error_out);
int strappy_db_update_session_model(const char *db_path,
                                    long long session_id,
                                    const char *model_id,
                                    char **error_out);
int strappy_db_get_session_model(const char *db_path,
                                 long long session_id,
                                 char **model_id_out,
                                 char **error_out);

#ifdef __cplusplus
}
#endif

#endif
