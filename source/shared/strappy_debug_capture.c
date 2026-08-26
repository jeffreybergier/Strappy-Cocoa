#include "strappy_debug_capture.h"

#if STRAPPY_RAW_JSON_DEBUG_CAPTURE

#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char *strappy_debug_format(const char *format, ...)
{
  va_list arguments;
  va_list copy;
  char *value;
  int length;

  va_start(arguments, format);
  va_copy(copy, arguments);
  length = vsnprintf(NULL, 0U, format, copy);
  va_end(copy);
  if (length < 0) {
    va_end(arguments);
    return NULL;
  }
  value = (char *)malloc((size_t)length + 1U);
  if (value != NULL) {
    (void)vsnprintf(value, (size_t)length + 1U, format, arguments);
  }
  va_end(arguments);
  return value;
}

static int strappy_debug_ensure_directory(const char *path)
{
  char *copy;
  char *cursor;
  int ok;

  if ((path == NULL) || (path[0] == '\0')) {
    return 0;
  }
  copy = (char *)malloc(strlen(path) + 1U);
  if (copy == NULL) {
    return 0;
  }
  memcpy(copy, path, strlen(path) + 1U);
  ok = 1;
  for (cursor = copy + 1; ok && (*cursor != '\0'); cursor++) {
    int created;

    if (*cursor != '/') {
      continue;
    }
    *cursor = '\0';
    created = mkdir(copy, (mode_t)0700);
    if ((created != 0) && (errno != EEXIST)) {
      ok = 0;
    } else if (created == 0) {
      (void)chmod(copy, (mode_t)0700);
    }
    *cursor = '/';
  }
  if (ok) {
    int created;

    created = mkdir(copy, (mode_t)0700);
    if ((created != 0) && (errno != EEXIST)) {
      ok = 0;
    } else if (created == 0) {
      (void)chmod(copy, (mode_t)0700);
    }
  }
  free(copy);
  return ok;
}

static int strappy_debug_write_atomic(const char *path,
                                      const char *bytes,
                                      size_t length)
{
  char *temporary_path;
  size_t offset;
  int descriptor;
  int ok;

  if ((path == NULL) || (bytes == NULL)) {
    return 0;
  }
  temporary_path = strappy_debug_format("%s.tmp-%ld", path, (long)getpid());
  if (temporary_path == NULL) {
    return 0;
  }
  descriptor = open(temporary_path,
                    O_WRONLY | O_CREAT | O_TRUNC,
                    (mode_t)0600);
  if (descriptor < 0) {
    free(temporary_path);
    return 0;
  }
  (void)fchmod(descriptor, (mode_t)0600);
  offset = 0U;
  ok = 1;
  while (ok && (offset < length)) {
    ssize_t written;

    written = write(descriptor, bytes + offset, length - offset);
    if (written > 0) {
      offset += (size_t)written;
    } else if ((written < 0) && (errno == EINTR)) {
      continue;
    } else {
      ok = 0;
    }
  }
  if (ok && (fsync(descriptor) != 0)) {
    ok = 0;
  }
  if (close(descriptor) != 0) {
    ok = 0;
  }
  if (ok && (rename(temporary_path, path) != 0)) {
    ok = 0;
  }
  if (!ok) {
    (void)unlink(temporary_path);
  }
  free(temporary_path);
  return ok;
}

static char *strappy_debug_parent_directory(const char *db_path)
{
  const char *slash;
  char *parent;
  size_t length;

  slash = (db_path != NULL) ? strrchr(db_path, '/') : NULL;
  if (slash == NULL) {
    return strappy_debug_format(".");
  }
  length = (size_t)(slash - db_path);
  if (length == 0U) {
    return strappy_debug_format("/");
  }
  parent = (char *)malloc(length + 1U);
  if (parent != NULL) {
    memcpy(parent, db_path, length);
    parent[length] = '\0';
  }
  return parent;
}

static int strappy_debug_db_path_is_store(const char *db_path)
{
  const char *name;

  name = (db_path != NULL) ? strrchr(db_path, '/') : NULL;
  name = (name != NULL) ? name + 1 : db_path;
  return (name != NULL) && (strcmp(name, "strappy.sqlite") == 0);
}

static char *strappy_debug_session_directory(
  const char *db_path,
  const strappy_response_call_identity *identity)
{
  char *parent;
  char *path;

  parent = strappy_debug_parent_directory(db_path);
  path = (parent != NULL) ?
    strappy_debug_format("%s/debug/sessions/session-%lld",
                         parent,
                         identity->session_id) : NULL;
  free(parent);
  return path;
}

static char *strappy_debug_attempt_directory(
  const char *db_path,
  const strappy_response_call_identity *identity)
{
  char *session;
  char *path;

  session = strappy_debug_session_directory(db_path, identity);
  path = (session != NULL) ?
    strappy_debug_format(
      "%s/turns/turn-%lld-ordinal-%06ld/request-%lld-round-%03ld/"
      "attempt-%lld-index-%03ld",
      session,
      identity->turn_id,
      identity->turn_ordinal,
      identity->model_request_id,
      identity->round_index,
      identity->http_attempt_id,
      identity->attempt_index) : NULL;
  free(session);
  return path;
}

static void strappy_debug_add_string(cJSON *object,
                                     const char *name,
                                     const char *value)
{
  if ((object != NULL) && (name != NULL)) {
    cJSON_AddStringToObject(object, name, (value != NULL) ? value : "");
  }
}

static char *strappy_debug_exchange_json(
  const strappy_response_call_identity *identity,
  const char *provider_id,
  const char *provider_account_id,
  const char *model_id,
  const char *request_kind,
  const char *state,
  int output_is_canonical,
  long http_status,
  long long started_at_ms,
  long long completed_at_ms,
  const char *content_type)
{
  cJSON *root;
  char *json;

  root = cJSON_CreateObject();
  if (root == NULL) {
    return NULL;
  }
  cJSON_AddNumberToObject(root, "format_version", 1.0);
  cJSON_AddNumberToObject(root, "session_id", (double)identity->session_id);
  cJSON_AddNumberToObject(root,
                         "session_created_at_ms",
                         (double)identity->session_created_at_ms);
  cJSON_AddNumberToObject(root, "turn_id", (double)identity->turn_id);
  cJSON_AddNumberToObject(root,
                         "turn_ordinal",
                         (double)identity->turn_ordinal);
  cJSON_AddNumberToObject(root,
                         "model_request_id",
                         (double)identity->model_request_id);
  cJSON_AddNumberToObject(root,
                         "round_index",
                         (double)identity->round_index);
  cJSON_AddNumberToObject(root,
                         "http_attempt_id",
                         (double)identity->http_attempt_id);
  cJSON_AddNumberToObject(root,
                         "attempt_index",
                         (double)identity->attempt_index);
  strappy_debug_add_string(root, "provider_id", provider_id);
  strappy_debug_add_string(root, "provider_account_id", provider_account_id);
  strappy_debug_add_string(root, "model_id", model_id);
  strappy_debug_add_string(root, "request_kind", request_kind);
  strappy_debug_add_string(root, "state", state);
  cJSON_AddBoolToObject(root,
                       "output_is_canonical",
                       output_is_canonical ? 1 : 0);
  cJSON_AddNumberToObject(root, "http_status", (double)http_status);
  cJSON_AddNumberToObject(root, "started_at_ms", (double)started_at_ms);
  cJSON_AddNumberToObject(root, "completed_at_ms", (double)completed_at_ms);
  strappy_debug_add_string(root, "content_type", content_type);
  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return json;
}

static void strappy_debug_write_session_json(
  const char *session_directory,
  const strappy_response_call_identity *identity)
{
  cJSON *root;
  char *json;
  char *path;

  root = cJSON_CreateObject();
  if (root == NULL) {
    return;
  }
  cJSON_AddNumberToObject(root, "format_version", 1.0);
  cJSON_AddNumberToObject(root, "session_id", (double)identity->session_id);
  cJSON_AddNumberToObject(root,
                         "created_at_ms",
                         (double)identity->session_created_at_ms);
  json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  path = strappy_debug_format("%s/session.json", session_directory);
  if ((json != NULL) && (path != NULL)) {
    (void)strappy_debug_write_atomic(path, json, strlen(json));
  }
  free(path);
  free(json);
}

void strappy_debug_capture_request(
  const char *db_path,
  const strappy_response_call_identity *identity,
  const char *provider_id,
  const char *provider_account_id,
  const char *model_id,
  const char *request_kind,
  const char *request_json)
{
  char *attempt_directory;
  char *exchange_json;
  char *path;
  char *session_directory;

  if (!strappy_debug_db_path_is_store(db_path) || (identity == NULL) ||
      (request_json == NULL)) {
    return;
  }
  session_directory = strappy_debug_session_directory(db_path, identity);
  attempt_directory = strappy_debug_attempt_directory(db_path, identity);
  if ((session_directory == NULL) || (attempt_directory == NULL) ||
      !strappy_debug_ensure_directory(attempt_directory)) {
    free(session_directory);
    free(attempt_directory);
    return;
  }
  strappy_debug_write_session_json(session_directory, identity);
  path = strappy_debug_format("%s/request.json", attempt_directory);
  if (path != NULL) {
    (void)strappy_debug_write_atomic(path,
                                     request_json,
                                     strlen(request_json));
  }
  free(path);
  exchange_json = strappy_debug_exchange_json(identity,
                                               provider_id,
                                               provider_account_id,
                                               model_id,
                                               request_kind,
                                               "running",
                                               0,
                                               0L,
                                               0LL,
                                               0LL,
                                               "");
  path = strappy_debug_format("%s/exchange.json", attempt_directory);
  if ((path != NULL) && (exchange_json != NULL)) {
    (void)strappy_debug_write_atomic(path,
                                     exchange_json,
                                     strlen(exchange_json));
  }
  free(path);
  free(exchange_json);
  free(session_directory);
  free(attempt_directory);
}

void strappy_debug_capture_response(
  const char *db_path,
  const strappy_response_call_identity *identity,
  const char *provider_id,
  const char *provider_account_id,
  const char *model_id,
  const char *request_kind,
  const char *state,
  int output_is_canonical,
  long http_status,
  long long started_at_ms,
  long long completed_at_ms,
  const char *content_type,
  const char *raw_response,
  size_t raw_response_length)
{
  char *attempt_directory;
  char *exchange_json;
  char *path;

  if (!strappy_debug_db_path_is_store(db_path) || (identity == NULL)) {
    return;
  }
  attempt_directory = strappy_debug_attempt_directory(db_path, identity);
  if ((attempt_directory == NULL) ||
      !strappy_debug_ensure_directory(attempt_directory)) {
    free(attempt_directory);
    return;
  }
  if (raw_response != NULL) {
    path = strappy_debug_format("%s/response-body.json", attempt_directory);
    if (path != NULL) {
      (void)strappy_debug_write_atomic(path,
                                       raw_response,
                                       raw_response_length);
    }
    free(path);
  }
  exchange_json = strappy_debug_exchange_json(identity,
                                               provider_id,
                                               provider_account_id,
                                               model_id,
                                               request_kind,
                                               state,
                                               output_is_canonical,
                                               http_status,
                                               started_at_ms,
                                               completed_at_ms,
                                               content_type);
  path = strappy_debug_format("%s/exchange.json", attempt_directory);
  if ((path != NULL) && (exchange_json != NULL)) {
    (void)strappy_debug_write_atomic(path,
                                     exchange_json,
                                     strlen(exchange_json));
  }
  free(path);
  free(exchange_json);
  free(attempt_directory);
}

#else

void strappy_debug_capture_request(
  const char *db_path,
  const strappy_response_call_identity *identity,
  const char *provider_id,
  const char *provider_account_id,
  const char *model_id,
  const char *request_kind,
  const char *request_json)
{
  (void)db_path;
  (void)identity;
  (void)provider_id;
  (void)provider_account_id;
  (void)model_id;
  (void)request_kind;
  (void)request_json;
}

void strappy_debug_capture_response(
  const char *db_path,
  const strappy_response_call_identity *identity,
  const char *provider_id,
  const char *provider_account_id,
  const char *model_id,
  const char *request_kind,
  const char *state,
  int output_is_canonical,
  long http_status,
  long long started_at_ms,
  long long completed_at_ms,
  const char *content_type,
  const char *raw_response,
  size_t raw_response_length)
{
  (void)db_path;
  (void)identity;
  (void)provider_id;
  (void)provider_account_id;
  (void)model_id;
  (void)request_kind;
  (void)state;
  (void)output_is_canonical;
  (void)http_status;
  (void)started_at_ms;
  (void)completed_at_ms;
  (void)content_type;
  (void)raw_response;
  (void)raw_response_length;
}

#endif
