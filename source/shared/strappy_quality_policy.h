#ifndef STRAPPY_QUALITY_POLICY_H
#define STRAPPY_QUALITY_POLICY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_QUALITY_POLICY_GUIDANCE_VERSION "4"

typedef enum strappy_quality_check_evaluation_kind {
  STRAPPY_QUALITY_CHECK_ANSWER_NON_EMPTY = 1,
  STRAPPY_QUALITY_CHECK_WEB_REFERENCE,
  STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
} strappy_quality_check_evaluation_kind;

typedef struct strappy_quality_check_definition {
  const char *check_key;
  const char *check_kind;
  const char *label;
  const char *tool_name;
  strappy_quality_check_evaluation_kind evaluation_kind;
} strappy_quality_check_definition;

const strappy_quality_check_definition *strappy_quality_policy_find(
  const char *check_key);
size_t strappy_quality_policy_count(void);
const strappy_quality_check_definition *strappy_quality_policy_at(
  size_t index);

#ifdef __cplusplus
}
#endif

#endif
