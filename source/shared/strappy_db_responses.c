#include "strappy_db_internal.h"

#include "strappy_core.h"
#include "strappy_tools.h"

#include <cJSON.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Normalized response recording, reconstruction, and message timelines. */

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

void strappy_response_timeline_cursor_init(
  strappy_response_timeline_cursor *cursor)
{
  if (cursor == NULL) {
    return;
  }

  cursor->session_id = 0LL;
  cursor->request_id = 0LL;
  cursor->group_phase = 0LL;
  cursor->attempt_index = 0LL;
  cursor->attempt_phase = 0LL;
  cursor->item_index = 0LL;
  cursor->entry_type = 0LL;
  cursor->row_id = 0LL;
  cursor->valid = 0;
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
  record->prompt_index = 0L;
  record->round_index = 0L;
  record->attempt_index = 0L;
  record->cumulative_usage_cost = 0.0;
  record->has_cumulative_usage_cost = 0;
  record->cumulative_wait_ms = 0LL;
  record->has_cumulative_wait_ms = 0;
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
  strappy_response_timeline_cursor_init(&record->timeline_cursor);
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

int strappy_db_load_session_message_by_key(
  const char *db_path,
  long long session_id,
  const char *message_key,
  strappy_session_message_record *record,
  char **error_out)
{
  strappy_session_message_record_list timeline;
  size_t index;

  if (record == NULL) {
    strappy_set_error(error_out, "Session message output is missing.");
    return 0;
  }
  strappy_session_message_record_init(record);

  if ((message_key == NULL) || (message_key[0] == '\0')) {
    strappy_set_error(error_out, "Session message key is missing.");
    return 0;
  }

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
    tool_name = ((name != NULL) && cJSON_IsString(name) &&
                 (name->valuestring != NULL)) ?
      name->valuestring :
      (((type != NULL) && cJSON_IsString(type) &&
        (type->valuestring != NULL)) ?
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

  if ((annotations == NULL) || !cJSON_IsArray(annotations)) {
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
    part_type = ((type != NULL) && cJSON_IsString(type) &&
                 (type->valuestring != NULL)) ?
      type->valuestring : default_part_type;
    part_text = cJSON_IsString(part) ? part->valuestring :
      (((text != NULL) && cJSON_IsString(text)) ? text->valuestring :
        (((refusal != NULL) && cJSON_IsString(refusal)) ?
          refusal->valuestring : NULL));
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
    action_type_text = ((action_type != NULL) &&
                        cJSON_IsString(action_type) &&
                        (action_type->valuestring != NULL)) ?
      action_type->valuestring : "search";
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, search_sql, -1, &stmt, NULL);
    if ((rc != SQLITE_OK) ||
        (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
        (sqlite3_bind_text(stmt, 2, action_type_text, -1,
                           SQLITE_TRANSIENT) != SQLITE_OK) ||
        !strappy_db_bind_nullable_text_value(
          stmt, 3,
          ((query != NULL) && cJSON_IsString(query)) ?
            query->valuestring : NULL) ||
        (sqlite3_step(stmt) != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not save web-search item: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    sqlite3_finalize(stmt);
    source_ordinal = 0L;
    for (source = ((sources != NULL) && cJSON_IsArray(sources)) ?
           sources->child : NULL;
         source != NULL;
         source = source->next) {
      cJSON *source_type;
      cJSON *url;
      const char *source_type_text;

      source_type = cJSON_IsObject(source) ?
        cJSON_GetObjectItem(source, "type") : NULL;
      url = cJSON_IsObject(source) ? cJSON_GetObjectItem(source, "url") : NULL;
      if ((url == NULL) || !cJSON_IsString(url)) {
        continue;
      }
      source_type_text = ((source_type != NULL) &&
                          cJSON_IsString(source_type)) ?
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
     "reasoning_enabled, reasoning_summary, parallel_tool_calls, "
     "tool_calls_enabled, state, created_at_ms) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, NULL, ?, ?, ?, ?, ?, ?, ?, ?, "
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
  cJSON *parallel_tool_calls;
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
    parallel_tool_calls =
      cJSON_GetObjectItem(root, "parallel_tool_calls");
    model_id = (cJSON_IsString(model) && (model->valuestring != NULL) &&
                (model->valuestring[0] != '\0')) ?
      model->valuestring : STRAPPY_CONFIG_DEFAULT_API_MODEL;
    summary_text = ((reasoning_summary != NULL) &&
                    cJSON_IsString(reasoning_summary) &&
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
                           cJSON_IsFalse(parallel_tool_calls) ? 0 : 1) == SQLITE_OK) &&
         (sqlite3_bind_int(stmt, 17,
                           cJSON_IsArray(tools) &&
                           (cJSON_GetArraySize(tools) > 0) ? 1 : 0) == SQLITE_OK) &&
         (sqlite3_bind_int64(stmt, 18, (sqlite3_int64)now_ms) == SQLITE_OK);
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
  return strappy_db_semantic_begin_response_call(db_path,
                                                 input,
                                                 call_id_out,
                                                 error_out);
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
         ((error_parameter != NULL) && cJSON_IsString(error_parameter)) ?
           error_parameter->valuestring : NULL) &&
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
  if ((output != NULL) && cJSON_IsArray(output)) {
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
  return strappy_db_semantic_finish_response_call(db_path, input, error_out);
}

int strappy_db_set_response_round_wall_duration(
  const char *db_path,
  long long response_call_id,
  long long wall_duration_ms,
  char **error_out)
{
  static const char *sql =
    "UPDATE model_requests SET wall_duration_ms = ? "
    "WHERE id = (SELECT request_id FROM http_attempts WHERE id = ?);";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if ((response_call_id <= 0LL) || (wall_duration_ms < 0LL)) {
    strappy_set_error(error_out,
                      "Responses round wall duration is incomplete.");
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
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt,
                          1,
                          (sqlite3_int64)wall_duration_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt,
                          2,
                          (sqlite3_int64)response_call_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(
      error_out,
      "Could not save Responses round wall duration: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_mark_response_call_round_limit(
  const char *db_path,
  long long call_id,
  const char *message,
  char **error_out)
{
  sqlite3 *db;
  sqlite3_stmt *stmt;
  long long request_id;
  long long turn_id;
  long long session_id;
  long long now_ms;
  int rc;

  if ((call_id <= 0LL) || (message == NULL) || (message[0] == '\0')) {
    strappy_set_error(error_out,
                      "Responses round-limit result is incomplete.");
    return 0;
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out) ||
      !strappy_db_semantic_attempt_context(db,
                                           call_id,
                                           &request_id,
                                           &turn_id,
                                           &session_id,
                                           error_out) ||
      !strappy_db_exec(db,
                       "BEGIN IMMEDIATE;",
                       "Could not begin Responses round-limit result",
                       error_out)) {
    strappy_db_release(db);
    return 0;
  }

  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "UPDATE http_attempts SET state = 'response_error' "
    "WHERE id = ? AND state = 'completed';",
    -1,
    &stmt,
    NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)call_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(
      error_out,
      "Could not mark the round-limited Responses attempt: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    goto round_limit_rollback;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "UPDATE api_results SET error_type = 'harness_error', "
    "error_code = 'round_limit_reached', error_message = ? "
    "WHERE attempt_id = ?;",
    -1,
    &stmt,
    NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_text(stmt, 1, message, -1, SQLITE_TRANSIENT) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)call_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(
      error_out,
      "Could not save the Responses round-limit reason: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    goto round_limit_rollback;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "UPDATE model_requests SET state = 'error' WHERE id = ?;",
    -1,
    &stmt,
    NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1,
                          (sqlite3_int64)request_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(
      error_out,
      "Could not mark the round-limited model request: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    goto round_limit_rollback;
  }
  sqlite3_finalize(stmt);

  now_ms = strappy_db_now_ms();
  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "UPDATE turns SET state = 'error', completed_at_ms = ? "
    "WHERE id = ? AND session_id = ?;",
    -1,
    &stmt,
    NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)turn_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 3,
                          (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(
      error_out,
      "Could not mark the round-limited Responses turn: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    goto round_limit_rollback;
  }
  sqlite3_finalize(stmt);

  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "UPDATE sessions SET updated_at_ms = ? WHERE id = ?;",
    -1,
    &stmt,
    NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ms) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2,
                          (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_DONE) ||
      (sqlite3_changes(db) != 1)) {
    strappy_set_formatted_error(
      error_out,
      "Could not update the round-limited Responses session: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    goto round_limit_rollback;
  }
  sqlite3_finalize(stmt);

  if (!strappy_db_exec(db,
                       "COMMIT;",
                       "Could not commit Responses round-limit result",
                       error_out)) {
    goto round_limit_rollback;
  }
  strappy_db_release(db);
  return 1;

round_limit_rollback:
  strappy_db_exec(db,
                  "ROLLBACK;",
                  "Could not roll back Responses round-limit result",
                  NULL);
  strappy_db_release(db);
  return 0;
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

static int strappy_db_semantic_sanitize_utf8(char **value_io,
                                             char **error_out)
{
  char *sanitized;
  size_t length;

  if ((value_io == NULL) || (*value_io == NULL)) {
    return 1;
  }

  length = strlen(*value_io);
  if (strappy_utf8_validate(*value_io, length)) {
    return 1;
  }

  sanitized = strappy_utf8_sanitized_string_duplicate(*value_io, length);
  if (sanitized == NULL) {
    strappy_set_error(error_out,
                      "Could not sanitize structured UTF-8 text.");
    return 0;
  }
  free(*value_io);
  *value_io = sanitized;
  return 1;
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
  if (!strappy_db_semantic_sanitize_utf8(&text_value, error_out)) {
    free(value_type);
    free(text_value);
    free(number_value);
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
    if (!strappy_db_semantic_sanitize_utf8(
          &children[child_count - 1U].member_name,
          error_out)) {
      sqlite3_reset(context->children_stmt);
      strappy_db_semantic_destroy_structured_children(children, child_count);
      cJSON_Delete(value);
      return NULL;
    }
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
  strappy_response_item_raw_record_list *list,
  char **error_out)
{
  static const char *sql =
    "SELECT id FROM conversation_items "
    "WHERE session_id = ? AND include_in_context = 1 ORDER BY sequence;";
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
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK)) {
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
  return strappy_db_semantic_list_canonical_response_items(db_path,
                                                           session_id,
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
  return strappy_db_semantic_save_response_tool_execution(db_path,
                                                          input,
                                                          error_out);
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
  /* Provider model metadata belongs to the response ledger. The session model
     is a user-selected option and must not be changed during finalization. */
  (void)model;
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
  stmt = NULL;
  rc = sqlite3_prepare_v2(
    db,
    "UPDATE sessions SET updated_at_ms = ? WHERE id = ?;",
    -1,
    &stmt,
    NULL);
  ok = (rc == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now_ms) == SQLITE_OK) &&
       (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) == SQLITE_OK);
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

static void strappy_db_semantic_finalize_timeline_totals(
  strappy_session_message_record_list *list)
{
  size_t group_start;
  double cumulative_cost;
  long long cumulative_wait_ms;
  int has_cumulative_cost;
  int has_cumulative_wait;

  if (list == NULL) {
    return;
  }
  group_start = 0U;
  cumulative_cost = 0.0;
  cumulative_wait_ms = 0LL;
  has_cumulative_cost = 0;
  has_cumulative_wait = 0;
  while (group_start < list->count) {
    long long model_request_id;
    size_t group_end;
    size_t index;
    int group_has_finalized_attempt;

    model_request_id = list->records[group_start].model_request_id;
    group_end = group_start;
    group_has_finalized_attempt = 0;
    while ((group_end < list->count) &&
           (list->records[group_end].model_request_id == model_request_id)) {
      if ((list->records[group_end].kind != NULL) &&
          (strcmp(list->records[group_end].kind,
                  "response_api_call") == 0)) {
        group_has_finalized_attempt = 1;
        if (list->records[group_end].has_cumulative_usage_cost) {
          cumulative_cost +=
            list->records[group_end].cumulative_usage_cost;
          has_cumulative_cost = 1;
        }
        if (list->records[group_end].has_cumulative_wait_ms) {
          long long wait_ms;

          wait_ms = list->records[group_end].cumulative_wait_ms;
          cumulative_wait_ms =
            (wait_ms > (LLONG_MAX - cumulative_wait_ms)) ?
              LLONG_MAX : cumulative_wait_ms + wait_ms;
          has_cumulative_wait = 1;
        }
      }
      group_end++;
    }
    for (index = group_start; index < group_end; index++) {
      list->records[index].cumulative_usage_cost =
        group_has_finalized_attempt ? cumulative_cost : 0.0;
      list->records[index].has_cumulative_usage_cost =
        group_has_finalized_attempt && has_cumulative_cost;
      list->records[index].cumulative_wait_ms =
        group_has_finalized_attempt ? cumulative_wait_ms : 0LL;
      list->records[index].has_cumulative_wait_ms =
        group_has_finalized_attempt && has_cumulative_wait;
    }
    group_start = group_end;
  }
}

static int strappy_db_semantic_finalize_ranged_timeline_totals(
  sqlite3 *db,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out)
{
  static const char *sql =
    "SELECT r.id, SUM(u.cost_nano_usd), "
    "COALESCE(MAX(r.wall_duration_ms), "
      "SUM(CASE WHEN a.completed_at_ms IS NOT NULL "
        "AND a.completed_at_ms >= a.started_at_ms "
        "THEN a.completed_at_ms - a.started_at_ms END)) "
    "FROM model_requests r "
    "JOIN turns t ON t.id = r.turn_id "
    "JOIN http_attempts a ON a.request_id = r.id "
    "LEFT JOIN api_usage u ON u.attempt_id = a.id "
    "WHERE t.session_id = ? AND r.id <= ? "
    "AND a.state NOT IN ('pending','running') "
    "GROUP BY r.id ORDER BY r.id;";
  sqlite3_stmt *stmt;
  long long maximum_request_id;
  long long cumulative_wait_ms;
  double cumulative_cost;
  size_t group_start;
  int has_cumulative_cost;
  int has_cumulative_wait;
  int rc;

  if ((list == NULL) || (list->count == 0U)) {
    return 1;
  }
  maximum_request_id = list->records[list->count - 1U].model_request_id;
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt,
                          1,
                          (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt,
                          2,
                          (sqlite3_int64)maximum_request_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare ranged timeline totals: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  group_start = 0U;
  cumulative_cost = 0.0;
  cumulative_wait_ms = 0LL;
  has_cumulative_cost = 0;
  has_cumulative_wait = 0;
  rc = sqlite3_step(stmt);
  while (group_start < list->count) {
    long long model_request_id;
    size_t group_end;
    size_t index;
    int group_has_finalized_attempt;

    model_request_id = list->records[group_start].model_request_id;
    group_has_finalized_attempt = 0;
    while ((rc == SQLITE_ROW) &&
           ((long long)sqlite3_column_int64(stmt, 0) <= model_request_id)) {
      if ((long long)sqlite3_column_int64(stmt, 0) == model_request_id) {
        group_has_finalized_attempt = 1;
      }
      if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
        cumulative_cost +=
          (double)sqlite3_column_int64(stmt, 1) / 1000000000.0;
        has_cumulative_cost = 1;
      }
      if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
        long long wait_ms;

        wait_ms = (long long)sqlite3_column_int64(stmt, 2);
        cumulative_wait_ms =
          (wait_ms > (LLONG_MAX - cumulative_wait_ms)) ?
            LLONG_MAX : cumulative_wait_ms + wait_ms;
        has_cumulative_wait = 1;
      }
      rc = sqlite3_step(stmt);
    }
    if ((rc != SQLITE_ROW) && (rc != SQLITE_DONE)) {
      strappy_set_formatted_error(error_out,
                                  "Could not read ranged timeline totals: %s",
                                  sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return 0;
    }
    group_end = group_start;
    while ((group_end < list->count) &&
           (list->records[group_end].model_request_id == model_request_id)) {
      group_end++;
    }
    for (index = group_start; index < group_end; index++) {
      list->records[index].cumulative_usage_cost =
        group_has_finalized_attempt ? cumulative_cost : 0.0;
      list->records[index].has_cumulative_usage_cost =
        group_has_finalized_attempt && has_cumulative_cost;
      list->records[index].cumulative_wait_ms =
        group_has_finalized_attempt ? cumulative_wait_ms : 0LL;
      list->records[index].has_cumulative_wait_ms =
        group_has_finalized_attempt && has_cumulative_wait;
    }
    group_start = group_end;
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int strappy_db_semantic_function_output_tool_context(
  sqlite3 *db,
  long long item_id,
  char **tool_name_out,
  char **arguments_json_out,
  char **error_out)
{
  static const char *sql =
    "SELECT c.item_id, c.tool_name FROM function_outputs o "
    "JOIN function_calls c ON c.item_id = o.function_call_item_id "
    "WHERE o.item_id = ?;";
  sqlite3_stmt *stmt;
  cJSON *arguments;
  long long function_call_item_id;
  char *arguments_json;
  char *tool_name;
  int rc;

  if ((tool_name_out == NULL) || (arguments_json_out == NULL)) {
    strappy_set_error(error_out,
                      "Function output tool context has no output.");
    return 0;
  }
  *tool_name_out = NULL;
  *arguments_json_out = NULL;
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)item_id) != SQLITE_OK) ||
      (sqlite3_step(stmt) != SQLITE_ROW)) {
    strappy_set_formatted_error(error_out,
                                "Could not read function output tool context: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return 0;
  }
  function_call_item_id = (long long)sqlite3_column_int64(stmt, 0);
  tool_name = strappy_db_column_string(stmt, 1);
  sqlite3_finalize(stmt);
  if (tool_name == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate function output tool name.");
    return 0;
  }
  arguments = strappy_db_semantic_load_document(db,
                                                function_call_item_id,
                                                "arguments",
                                                error_out);
  if (arguments == NULL) {
    free(tool_name);
    return 0;
  }
  arguments_json = cJSON_PrintUnformatted(arguments);
  cJSON_Delete(arguments);
  if (arguments_json == NULL) {
    free(tool_name);
    strappy_set_error(error_out,
                      "Could not serialize function output tool arguments.");
    return 0;
  }
  *tool_name_out = tool_name;
  *arguments_json_out = arguments_json;
  return 1;
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
  char *output_arguments_json;
  char *output_tool_name;

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
  if (strcmp(role, "api_function_output") == 0) {
    output_arguments_json = NULL;
    output_tool_name = NULL;
    if (!strappy_db_semantic_function_output_tool_context(
          db,
          (long long)sqlite3_column_int64(stmt, 5),
          &output_tool_name,
          &output_arguments_json,
          error_out)) {
      cJSON_Delete(item);
      return 0;
    }
    free(record->tool_name);
    record->tool_name = output_tool_name;
    record->arguments_json = output_arguments_json;
  }
  value = cJSON_GetObjectItem(item, "arguments");
  if ((record->arguments_json == NULL) && cJSON_IsString(value)) {
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
  const strappy_response_timeline_cursor *after_cursor,
  strappy_session_message_record_list *list,
  strappy_response_timeline_cursor *next_cursor_out,
  char **error_out)
{
  static const char *sql_part_one =
    "SELECT * FROM ("
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
    "r.model_id, 0 AS can_include_in_context, t.ordinal, "
    "CASE WHEN r.wall_duration_ms IS NOT NULL THEN "
      "CASE WHEN NOT EXISTS (SELECT 1 FROM http_attempts a2 "
        "WHERE a2.request_id = r.id "
        "AND a2.attempt_index > a.attempt_index) "
        "THEN r.wall_duration_ms END "
      "WHEN a.completed_at_ms IS NOT NULL "
        "AND a.completed_at_ms >= a.started_at_ms "
        "THEN a.completed_at_ms - a.started_at_ms END "
    "FROM http_attempts a JOIN model_requests r ON r.id = a.request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "LEFT JOIN api_results ar ON ar.attempt_id = a.id "
    "LEFT JOIN api_usage u ON u.attempt_id = a.id "
    "WHERE t.session_id = ?1 AND a.state NOT IN ('pending','running') "
    "AND (?5 = 0 OR r.id >= ?6) "
    "UNION ALL "
    "SELECT 2, i.id, t.id, r.id, NULL, i.id, 'request', "
    "t.prompt_group_key, r.request_kind, r.round_index, -1, NULL, 0, "
    "r.model_id, "
    "strftime('%Y-%m-%dT%H:%M:%fZ', i.created_at_ms / 1000.0, 'unixepoch'), "
    "i.is_error, i.include_in_context, NULL, "
    "0, 0, i.source_item_index, NULL, NULL, NULL, NULL, NULL, NULL, "
    "r.model_id, 1, t.ordinal, NULL "
    "FROM conversation_items i "
    "JOIN model_requests r ON r.id = i.introduced_request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "WHERE i.session_id = ?2 AND i.timeline_visible = 1 "
    "AND (?5 = 0 OR r.id >= ?6) ";
  static const char *sql_part_two =
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
    "r.model_id, 0, t.ordinal, NULL "
    "FROM answer_quality_audits q "
    "JOIN http_attempts a ON a.id = q.response_attempt_id "
    "JOIN model_requests r ON r.id = a.request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "LEFT JOIN api_results ar ON ar.attempt_id = a.id "
    "WHERE t.session_id = ?3 AND a.state NOT IN ('pending','running') "
    "AND (?5 = 0 OR r.id >= ?6) "
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
    ") THEN 1 ELSE 0 END, t.ordinal, NULL "
    "FROM conversation_items i "
    "JOIN http_attempts a ON a.id = i.source_attempt_id "
    "JOIN model_requests r ON r.id = a.request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "LEFT JOIN api_results ar ON ar.attempt_id = a.id "
    "WHERE i.session_id = ?4 AND i.timeline_visible = 1 "
    "AND a.state NOT IN ('pending','running') "
    "AND (?5 = 0 OR r.id >= ?6)"
    ") AS timeline "
    "WHERE ?5 = 0 "
    "OR request_id > ?6 "
    "OR (request_id = ?6 AND group_phase > ?7) "
    "OR (request_id = ?6 AND group_phase = ?7 "
      "AND attempt_index > ?8) "
    "OR (request_id = ?6 AND group_phase = ?7 "
      "AND attempt_index = ?8 AND attempt_phase > ?9) "
    "OR (request_id = ?6 AND group_phase = ?7 "
      "AND attempt_index = ?8 AND attempt_phase = ?9 "
      "AND item_index > ?10) "
    "OR (request_id = ?6 AND group_phase = ?7 "
      "AND attempt_index = ?8 AND attempt_phase = ?9 "
      "AND item_index = ?10 AND entry_type > ?11) "
    "OR (request_id = ?6 AND group_phase = ?7 "
      "AND attempt_index = ?8 AND attempt_phase = ?9 "
      "AND item_index = ?10 AND entry_type = ?11 AND row_id > ?12) "
    "ORDER BY request_id, group_phase, attempt_index, attempt_phase, "
    "item_index, entry_type, row_id;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  char *sql;
  strappy_response_timeline_cursor cursor;
  int rc;

  strappy_response_timeline_cursor_init(&cursor);
  cursor.session_id = session_id;
  if (next_cursor_out != NULL) {
    *next_cursor_out = cursor;
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
  if (after_cursor != NULL) {
    if (!after_cursor->valid ||
        (after_cursor->session_id != session_id) ||
        (after_cursor->request_id <= 0LL) ||
        (after_cursor->group_phase < 0LL) ||
        (after_cursor->attempt_index < -1LL) ||
        (after_cursor->attempt_phase < 0LL) ||
        (after_cursor->item_index < -1LL) ||
        (after_cursor->entry_type < 0LL) ||
        (after_cursor->entry_type > 2LL) ||
        (after_cursor->row_id <= 0LL)) {
      strappy_set_error(error_out, "Responses timeline cursor is not valid.");
      return 0;
    }
    cursor = *after_cursor;
    if (next_cursor_out != NULL) {
      *next_cursor_out = cursor;
    }
  }
  if (!strappy_db_open(db_path, &db, error_out)) {
    return 0;
  }
  if (!strappy_db_ensure_schema(db, error_out)) {
    strappy_db_release(db);
    return 0;
  }
  sql = sqlite3_mprintf("%s%s", sql_part_one, sql_part_two);
  if (sql == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate semantic Responses timeline query.");
    strappy_db_release(db);
    return 0;
  }
  stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  sqlite3_free(sql);
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 4,
                          (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int(stmt, 5, cursor.valid ? 1 : 0) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 6,
                          (sqlite3_int64)cursor.request_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 7,
                          (sqlite3_int64)cursor.group_phase) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 8,
                          (sqlite3_int64)cursor.attempt_index) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 9,
                          (sqlite3_int64)cursor.attempt_phase) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 10,
                          (sqlite3_int64)cursor.item_index) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 11,
                          (sqlite3_int64)cursor.entry_type) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 12,
                          (sqlite3_int64)cursor.row_id) != SQLITE_OK)) {
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
    record->prompt_index = (long)sqlite3_column_int64(stmt, 29);
    record->round_index = (long)sqlite3_column_int64(stmt, 9);
    record->attempt_index = (sqlite3_column_type(stmt, 10) != SQLITE_NULL) ?
      (long)sqlite3_column_int64(stmt, 10) : 0L;
    record->timeline_cursor.session_id = session_id;
    record->timeline_cursor.request_id = record->model_request_id;
    record->timeline_cursor.group_phase =
      (long long)sqlite3_column_int64(stmt, 18);
    record->timeline_cursor.attempt_index =
      (long long)sqlite3_column_int64(stmt, 10);
    record->timeline_cursor.attempt_phase =
      (long long)sqlite3_column_int64(stmt, 19);
    record->timeline_cursor.item_index =
      (long long)sqlite3_column_int64(stmt, 20);
    record->timeline_cursor.entry_type = (long long)entry_type;
    record->timeline_cursor.row_id = row_id;
    record->timeline_cursor.valid = 1;
    if ((entry_type == 0) &&
        (sqlite3_column_type(stmt, 17) != SQLITE_NULL)) {
      record->cumulative_usage_cost =
        (double)sqlite3_column_int64(stmt, 17) / 1000000000.0;
      record->has_cumulative_usage_cost = 1;
    }
    if ((entry_type == 0) &&
        (sqlite3_column_type(stmt, 30) != SQLITE_NULL)) {
      record->cumulative_wait_ms =
        (long long)sqlite3_column_int64(stmt, 30);
      record->has_cumulative_wait_ms = 1;
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
    cursor = record->timeline_cursor;
    if (next_cursor_out != NULL) {
      *next_cursor_out = cursor;
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
  if (after_cursor == NULL) {
    strappy_db_semantic_finalize_timeline_totals(list);
  } else if (!strappy_db_semantic_finalize_ranged_timeline_totals(db,
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

int strappy_db_list_response_timeline_after(
  const char *db_path,
  long long session_id,
  const strappy_response_timeline_cursor *after_cursor,
  strappy_session_message_record_list *list,
  strappy_response_timeline_cursor *next_cursor_out,
  char **error_out)
{
  return strappy_db_semantic_list_response_timeline(db_path,
                                                    session_id,
                                                    after_cursor,
                                                    list,
                                                    next_cursor_out,
                                                    error_out);
}

int strappy_db_load_response_timeline_cursor(
  const char *db_path,
  long long session_id,
  strappy_response_timeline_cursor *cursor_out,
  char **error_out)
{
  /* Keep these cursor fields and ordering aligned with the timeline query. */
  static const char *sql =
    "SELECT entry_type, row_id, request_id, group_phase, attempt_index, "
    "attempt_phase, item_index FROM ("
    "SELECT 0 AS entry_type, a.id AS row_id, r.id AS request_id, "
    "1 AS group_phase, a.attempt_index, 0 AS attempt_phase, "
    "-1 AS item_index "
    "FROM http_attempts a JOIN model_requests r ON r.id = a.request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "WHERE t.session_id = ?1 AND a.state NOT IN ('pending','running') "
    "UNION ALL "
    "SELECT 2, i.id, r.id, 0, -1, 0, i.source_item_index "
    "FROM conversation_items i "
    "JOIN model_requests r ON r.id = i.introduced_request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "WHERE i.session_id = ?2 AND i.timeline_visible = 1 "
    "UNION ALL "
    "SELECT 1, q.id, r.id, 1, a.attempt_index, 1, "
    "COALESCE((SELECT MIN(ci.source_item_index) "
      "FROM conversation_items ci "
      "JOIN message_items mi ON mi.item_id = ci.id "
      "WHERE ci.source_attempt_id = a.id AND mi.role = 'assistant'), "
      "2147483647) "
    "FROM answer_quality_audits q "
    "JOIN http_attempts a ON a.id = q.response_attempt_id "
    "JOIN model_requests r ON r.id = a.request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "WHERE t.session_id = ?3 AND a.state NOT IN ('pending','running') "
    "UNION ALL "
    "SELECT 2, i.id, r.id, 1, a.attempt_index, 1, i.source_item_index "
    "FROM conversation_items i "
    "JOIN http_attempts a ON a.id = i.source_attempt_id "
    "JOIN model_requests r ON r.id = a.request_id "
    "JOIN turns t ON t.id = r.turn_id "
    "WHERE i.session_id = ?4 AND i.timeline_visible = 1 "
    "AND a.state NOT IN ('pending','running')"
    ") AS timeline ORDER BY request_id DESC, group_phase DESC, "
    "attempt_index DESC, attempt_phase DESC, item_index DESC, "
    "entry_type DESC, row_id DESC LIMIT 1;";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if (cursor_out == NULL) {
    strappy_set_error(error_out, "Responses timeline cursor has no output.");
    return 0;
  }
  strappy_response_timeline_cursor_init(cursor_out);
  cursor_out->session_id = session_id;
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
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 2, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 3, (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt, 4,
                          (sqlite3_int64)session_id) != SQLITE_OK)) {
    strappy_set_formatted_error(error_out,
                                "Could not prepare timeline cursor query: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    cursor_out->entry_type = (long long)sqlite3_column_int64(stmt, 0);
    cursor_out->row_id = (long long)sqlite3_column_int64(stmt, 1);
    cursor_out->request_id = (long long)sqlite3_column_int64(stmt, 2);
    cursor_out->group_phase = (long long)sqlite3_column_int64(stmt, 3);
    cursor_out->attempt_index = (long long)sqlite3_column_int64(stmt, 4);
    cursor_out->attempt_phase = (long long)sqlite3_column_int64(stmt, 5);
    cursor_out->item_index = (long long)sqlite3_column_int64(stmt, 6);
    cursor_out->valid = 1;
  } else if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(error_out,
                                "Could not read timeline cursor: %s",
                                sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }

  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
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

int strappy_db_exclude_prompt_group_from_context(
  const char *db_path,
  long long session_id,
  const char *prompt_group_key,
  char **error_out)
{
  static const char *sql =
    "UPDATE conversation_items SET include_in_context = 0 "
    "WHERE session_id = ?1 AND include_in_context = 1 AND turn_id IN ("
      "SELECT id FROM turns "
      "WHERE session_id = ?1 AND prompt_group_key = ?2"
    ");";
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int rc;

  if ((session_id <= 0LL) || (prompt_group_key == NULL) ||
      (prompt_group_key[0] == '\0')) {
    strappy_set_error(error_out,
                      "Context prompt group exclusion is not valid.");
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
  if ((rc != SQLITE_OK) ||
      (sqlite3_bind_int64(stmt,
                          1,
                          (sqlite3_int64)session_id) != SQLITE_OK) ||
      (sqlite3_bind_text(stmt,
                         2,
                         prompt_group_key,
                         -1,
                         SQLITE_TRANSIENT) != SQLITE_OK)) {
    strappy_set_formatted_error(
      error_out,
      "Could not prepare context prompt group exclusion: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    strappy_set_formatted_error(
      error_out,
      "Could not exclude context prompt group: %s",
      sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    strappy_db_release(db);
    return 0;
  }
  sqlite3_finalize(stmt);
  strappy_db_release(db);
  return 1;
}

int strappy_db_list_response_timeline(
  const char *db_path,
  long long session_id,
  strappy_session_message_record_list *list,
  char **error_out)
{
  return strappy_db_list_response_timeline_after(db_path,
                                                 session_id,
                                                 NULL,
                                                 list,
                                                 NULL,
                                                 error_out);
}
