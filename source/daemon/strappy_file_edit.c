#include "strappy_file_edit.h"

#include "strappy_core.h"
#include "strappy_file_mutation.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

typedef struct strappy_file_edit_arguments {
  char *path;
  char *old_text;
  size_t old_text_length;
  char *new_text;
  size_t new_text_length;
} strappy_file_edit_arguments;

static void strappy_file_edit_arguments_init(
  strappy_file_edit_arguments *arguments)
{
  arguments->path = NULL;
  arguments->old_text = NULL;
  arguments->old_text_length = 0U;
  arguments->new_text = NULL;
  arguments->new_text_length = 0U;
}

static void strappy_file_edit_arguments_destroy(
  strappy_file_edit_arguments *arguments)
{
  free(arguments->path);
  free(arguments->old_text);
  free(arguments->new_text);
  strappy_file_edit_arguments_init(arguments);
}

static int strappy_file_edit_copy_argument(
  cJSON *item,
  const char *name,
  int allow_empty,
  char **value_out,
  size_t *length_out,
  char **error_out)
{
  size_t length;

  length = (cJSON_IsString(item) && (item->valuestring != NULL)) ?
    strlen(item->valuestring) : 0U;
  if (!cJSON_IsString(item) || (item->valuestring == NULL) ||
      (!allow_empty && (length == 0U)) ||
      (length > STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES) ||
      !strappy_file_mutation_validate_utf8(item->valuestring, length)) {
    strappy_set_formatted_error(
      error_out,
      "file_edit %s must be a valid UTF-8 string%s of at most 1 MiB.",
      name,
      allow_empty ? "" : " that is not empty");
    return 0;
  }
  *value_out = strappy_string_duplicate(item->valuestring);
  if (*value_out == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not allocate file_edit %s.",
                                name);
    return 0;
  }
  *length_out = length;
  return 1;
}

static int strappy_file_edit_parse_arguments(
  const char *arguments_json,
  strappy_file_edit_arguments *arguments,
  char **error_out)
{
  cJSON *root;
  cJSON *item;
  int path_count;
  int old_text_count;
  int new_text_count;

  strappy_file_edit_arguments_init(arguments);
  if (strappy_file_mutation_json_has_null_escape(arguments_json)) {
    strappy_set_error(error_out,
                      "file_edit arguments cannot contain U+0000.");
    return 0;
  }
  root = cJSON_Parse((arguments_json != NULL) ? arguments_json : "");
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    strappy_set_error(error_out,
                      "file_edit arguments must be a JSON object.");
    return 0;
  }

  path_count = 0;
  old_text_count = 0;
  new_text_count = 0;
  for (item = root->child; item != NULL; item = item->next) {
    if ((item->string != NULL) && (strcmp(item->string, "path") == 0)) {
      size_t path_length;

      path_count++;
      path_length = (cJSON_IsString(item) && (item->valuestring != NULL)) ?
        strlen(item->valuestring) : 0U;
      if ((path_count != 1) || !cJSON_IsString(item) ||
          (item->valuestring == NULL) || (path_length == 0U) ||
          (path_length > STRAPPY_FILE_MUTATION_MAX_PATH_BYTES) ||
          !strappy_file_mutation_validate_utf8(item->valuestring,
                                               path_length)) {
        cJSON_Delete(root);
        strappy_file_edit_arguments_destroy(arguments);
        strappy_set_error(
          error_out,
          "file_edit path must be one non-empty UTF-8 string of at most "
          "4096 bytes.");
        return 0;
      }
      arguments->path = strappy_string_duplicate(item->valuestring);
      if (arguments->path == NULL) {
        cJSON_Delete(root);
        strappy_file_edit_arguments_destroy(arguments);
        strappy_set_error(error_out,
                          "Could not allocate the file_edit path.");
        return 0;
      }
      continue;
    }
    if ((item->string != NULL) &&
        (strcmp(item->string, "old_text") == 0)) {
      old_text_count++;
      if (old_text_count != 1) {
        cJSON_Delete(root);
        strappy_file_edit_arguments_destroy(arguments);
        strappy_set_error(error_out,
                          "file_edit old_text may be provided only once.");
        return 0;
      }
      if (!strappy_file_edit_copy_argument(item,
                                           "old_text",
                                           0,
                                           &arguments->old_text,
                                           &arguments->old_text_length,
                                           error_out)) {
        cJSON_Delete(root);
        strappy_file_edit_arguments_destroy(arguments);
        return 0;
      }
      continue;
    }
    if ((item->string != NULL) &&
        (strcmp(item->string, "new_text") == 0)) {
      new_text_count++;
      if (new_text_count != 1) {
        cJSON_Delete(root);
        strappy_file_edit_arguments_destroy(arguments);
        strappy_set_error(error_out,
                          "file_edit new_text may be provided only once.");
        return 0;
      }
      if (!strappy_file_edit_copy_argument(item,
                                           "new_text",
                                           1,
                                           &arguments->new_text,
                                           &arguments->new_text_length,
                                           error_out)) {
        cJSON_Delete(root);
        strappy_file_edit_arguments_destroy(arguments);
        return 0;
      }
      continue;
    }

    cJSON_Delete(root);
    strappy_file_edit_arguments_destroy(arguments);
    strappy_set_error(error_out,
                      "file_edit accepts only path, old_text, and new_text.");
    return 0;
  }

  cJSON_Delete(root);
  if ((path_count != 1) || (old_text_count != 1) ||
      (new_text_count != 1)) {
    strappy_file_edit_arguments_destroy(arguments);
    strappy_set_error(
      error_out,
      "file_edit requires path, old_text, and new_text strings.");
    return 0;
  }
  return 1;
}

static int strappy_file_edit_normalize_newlines(
  const char *input,
  size_t input_length,
  int include_position_map,
  char **normalized_out,
  size_t **position_map_out,
  size_t *normalized_length_out,
  char **error_out)
{
  char *normalized;
  size_t *position_map;
  size_t input_index;
  size_t output_index;

  normalized = (char *)malloc(input_length + 1U);
  position_map = include_position_map ?
    (size_t *)malloc((input_length + 1U) * sizeof(size_t)) : NULL;
  if ((normalized == NULL) ||
      (include_position_map && (position_map == NULL))) {
    free(normalized);
    free(position_map);
    strappy_set_error(error_out,
                      "Could not allocate normalized file_edit text.");
    return 0;
  }

  input_index = 0U;
  output_index = 0U;
  while (input_index < input_length) {
    if (include_position_map) {
      position_map[output_index] = input_index;
    }
    if ((input[input_index] == '\r') &&
        ((input_index + 1U) < input_length) &&
        (input[input_index + 1U] == '\n')) {
      normalized[output_index++] = '\n';
      input_index += 2U;
    } else {
      normalized[output_index++] = input[input_index++];
    }
  }
  normalized[output_index] = '\0';
  if (include_position_map) {
    position_map[output_index] = input_index;
  }
  *normalized_out = normalized;
  if (position_map_out != NULL) {
    *position_map_out = position_map;
  } else {
    free(position_map);
  }
  *normalized_length_out = output_index;
  return 1;
}

static int strappy_file_edit_uses_crlf(const char *content, size_t length)
{
  size_t index;

  for (index = 0U; index < length; index++) {
    if ((content[index] == '\r') && ((index + 1U) < length) &&
        (content[index + 1U] == '\n')) {
      return 1;
    }
    if (content[index] == '\n') {
      return 0;
    }
  }
  return 0;
}

static char *strappy_file_edit_replacement_for_style(
  const char *new_text,
  size_t new_text_length,
  int use_crlf,
  size_t *replacement_length_out,
  char **error_out)
{
  char *normalized;
  size_t normalized_length;
  char *replacement;
  size_t newline_count;
  size_t index;
  size_t output_index;

  normalized = NULL;
  if (!strappy_file_edit_normalize_newlines(new_text,
                                            new_text_length,
                                            0,
                                            &normalized,
                                            NULL,
                                            &normalized_length,
                                            error_out)) {
    return NULL;
  }
  newline_count = 0U;
  if (use_crlf) {
    for (index = 0U; index < normalized_length; index++) {
      if (normalized[index] == '\n') {
        newline_count++;
      }
    }
  }
  if (normalized_length >
      STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES - newline_count) {
    free(normalized);
    strappy_set_error(error_out,
                      "file_edit replacement exceeds the 1 MiB limit.");
    return NULL;
  }

  *replacement_length_out = normalized_length + newline_count;
  replacement = (char *)malloc(*replacement_length_out + 1U);
  if (replacement == NULL) {
    free(normalized);
    strappy_set_error(error_out,
                      "Could not allocate the file_edit replacement.");
    return NULL;
  }
  output_index = 0U;
  for (index = 0U; index < normalized_length; index++) {
    if (use_crlf && (normalized[index] == '\n')) {
      replacement[output_index++] = '\r';
    }
    replacement[output_index++] = normalized[index];
  }
  replacement[output_index] = '\0';
  free(normalized);
  return replacement;
}

static char *strappy_file_edit_build_result(
  const char *resolved_path,
  const char *content,
  size_t content_length,
  const strappy_file_edit_arguments *arguments,
  size_t *result_length_out,
  char **error_out)
{
  size_t body_offset;
  const char *body;
  size_t body_length;
  char *normalized_body;
  size_t *position_map;
  size_t normalized_body_length;
  char *normalized_old_text;
  size_t normalized_old_text_length;
  size_t match_count;
  size_t match_start;
  size_t index;
  size_t original_match_start;
  size_t original_match_end;
  char *replacement;
  size_t replacement_length;
  char *result;
  size_t result_length;
  size_t suffix_length;

  body_offset = ((content_length >= 3U) &&
                 ((unsigned char)content[0] == 0xEFU) &&
                 ((unsigned char)content[1] == 0xBBU) &&
                 ((unsigned char)content[2] == 0xBFU)) ? 3U : 0U;
  body = content + body_offset;
  body_length = content_length - body_offset;
  normalized_body = NULL;
  position_map = NULL;
  normalized_old_text = NULL;
  replacement = NULL;

  if (!strappy_file_edit_normalize_newlines(body,
                                            body_length,
                                            1,
                                            &normalized_body,
                                            &position_map,
                                            &normalized_body_length,
                                            error_out) ||
      !strappy_file_edit_normalize_newlines(
        arguments->old_text,
        arguments->old_text_length,
        0,
        &normalized_old_text,
        NULL,
        &normalized_old_text_length,
        error_out)) {
    free(normalized_body);
    free(position_map);
    free(normalized_old_text);
    return NULL;
  }

  match_count = 0U;
  match_start = 0U;
  if (normalized_old_text_length <= normalized_body_length) {
    for (index = 0U;
         index <= normalized_body_length - normalized_old_text_length;
         index++) {
      if (memcmp(normalized_body + index,
                 normalized_old_text,
                 normalized_old_text_length) == 0) {
        match_start = index;
        match_count++;
        if (match_count > 1U) {
          break;
        }
      }
    }
  }
  free(normalized_body);
  free(normalized_old_text);
  if (match_count == 0U) {
    free(position_map);
    strappy_set_formatted_error(
      error_out,
      "Could not find file_edit old_text in %s. It must match exactly "
      "except for CRLF/LF line endings.",
      resolved_path);
    return NULL;
  }
  if (match_count > 1U) {
    free(position_map);
    strappy_set_formatted_error(
      error_out,
      "file_edit old_text matches more than once in %s; it must be unique.",
      resolved_path);
    return NULL;
  }

  original_match_start = position_map[match_start];
  original_match_end =
    position_map[match_start + normalized_old_text_length];
  free(position_map);
  replacement = strappy_file_edit_replacement_for_style(
    arguments->new_text,
    arguments->new_text_length,
    strappy_file_edit_uses_crlf(body, body_length),
    &replacement_length,
    error_out);
  if (replacement == NULL) {
    return NULL;
  }

  suffix_length = body_length - original_match_end;
  if ((body_offset > STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES) ||
      (original_match_start >
       STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES - body_offset) ||
      (replacement_length >
       STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES - body_offset -
       original_match_start) ||
      (suffix_length >
       STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES - body_offset -
       original_match_start - replacement_length)) {
    free(replacement);
    strappy_set_error(error_out,
                      "file_edit result exceeds the 1 MiB limit.");
    return NULL;
  }
  result_length = body_offset + original_match_start +
    replacement_length + suffix_length;
  result = (char *)malloc(result_length + 1U);
  if (result == NULL) {
    free(replacement);
    strappy_set_error(error_out,
                      "Could not allocate the file_edit result content.");
    return NULL;
  }
  memcpy(result, content, body_offset + original_match_start);
  memcpy(result + body_offset + original_match_start,
         replacement,
         replacement_length);
  memcpy(result + body_offset + original_match_start + replacement_length,
         body + original_match_end,
         suffix_length);
  result[result_length] = '\0';
  free(replacement);

  if ((result_length == content_length) &&
      (memcmp(result, content, result_length) == 0)) {
    free(result);
    strappy_set_formatted_error(error_out,
                                "file_edit would not change %s.",
                                resolved_path);
    return NULL;
  }
  *result_length_out = result_length;
  return result;
}

char *strappy_file_edit_execute(const char *session_db_path,
                                long long session_id,
                                const char *arguments_json,
                                char **error_out)
{
  strappy_file_edit_arguments arguments;
  char *resolved_path;
  char *result;
  char *content;
  size_t content_length;
  char *edited_content;
  size_t edited_content_length;
  int locked;
  int ok;

  if (!strappy_file_mutation_require_coding_session(session_db_path,
                                                    session_id,
                                                    "file_edit",
                                                    error_out)) {
    return NULL;
  }
  if (!strappy_file_edit_parse_arguments(arguments_json,
                                          &arguments,
                                          error_out)) {
    return NULL;
  }
  resolved_path = strappy_file_mutation_resolve_session_path(
    session_db_path,
    session_id,
    "file_edit",
    arguments.path,
    error_out);
  if (resolved_path == NULL) {
    strappy_file_edit_arguments_destroy(&arguments);
    return NULL;
  }
  result = strappy_string_duplicate("{}");
  if (result == NULL) {
    free(resolved_path);
    strappy_file_edit_arguments_destroy(&arguments);
    strappy_set_error(error_out,
                      "Could not allocate the file_edit result.");
    return NULL;
  }

  content = NULL;
  content_length = 0U;
  edited_content = NULL;
  edited_content_length = 0U;
  locked = strappy_file_mutation_lock(error_out);
  ok = locked;
  if (ok) {
    content = strappy_file_mutation_read_text("file_edit",
                                              resolved_path,
                                              &content_length,
                                              error_out);
    ok = (content != NULL) ? 1 : 0;
  }
  if (ok) {
    edited_content = strappy_file_edit_build_result(resolved_path,
                                                    content,
                                                    content_length,
                                                    &arguments,
                                                    &edited_content_length,
                                                    error_out);
    ok = (edited_content != NULL) ? 1 : 0;
  }
  if (ok) {
    ok = strappy_file_mutation_write_text("file_edit",
                                          resolved_path,
                                          edited_content,
                                          edited_content_length,
                                          0,
                                          1,
                                          error_out);
  }
  if (locked) {
    strappy_file_mutation_unlock();
  }

  free(content);
  free(edited_content);
  free(resolved_path);
  strappy_file_edit_arguments_destroy(&arguments);
  if (!ok) {
    free(result);
    return NULL;
  }
  return result;
}
