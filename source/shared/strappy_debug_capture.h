#ifndef STRAPPY_DEBUG_CAPTURE_H
#define STRAPPY_DEBUG_CAPTURE_H

#include "strappy_db.h"

#include <stddef.h>

#ifndef STRAPPY_RAW_JSON_DEBUG_CAPTURE
#define STRAPPY_RAW_JSON_DEBUG_CAPTURE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

void strappy_debug_capture_request(
  const char *db_path,
  const strappy_response_call_identity *identity,
  const char *provider_id,
  const char *provider_account_id,
  const char *model_id,
  const char *request_kind,
  const char *request_json);

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
  size_t raw_response_length);

#ifdef __cplusplus
}
#endif

#endif
