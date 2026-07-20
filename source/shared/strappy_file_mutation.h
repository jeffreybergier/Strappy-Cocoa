#ifndef STRAPPY_FILE_MUTATION_H
#define STRAPPY_FILE_MUTATION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_FILE_MUTATION_MAX_PATH_BYTES 4096U
#define STRAPPY_FILE_MUTATION_MAX_CONTENT_BYTES (1024U * 1024U)

int strappy_file_mutation_require_coding_session(
  const char *session_db_path,
  long long session_id,
  const char *tool_name,
  char **error_out);
char *strappy_file_mutation_resolve_session_path(
  const char *session_db_path,
  long long session_id,
  const char *tool_name,
  const char *path,
  char **error_out);
int strappy_file_mutation_validate_utf8(const char *text,
                                        size_t length);
int strappy_file_mutation_json_has_null_escape(const char *json);
int strappy_file_mutation_lock(char **error_out);
void strappy_file_mutation_unlock(void);
char *strappy_file_mutation_read_text(const char *tool_name,
                                      const char *resolved_path,
                                      size_t *length_out,
                                      char **error_out);
int strappy_file_mutation_write_text(const char *tool_name,
                                     const char *resolved_path,
                                     const char *text,
                                     size_t length,
                                     int create_parent_directories,
                                     int require_existing_file,
                                     char **error_out);

#ifdef __cplusplus
}
#endif

#endif
