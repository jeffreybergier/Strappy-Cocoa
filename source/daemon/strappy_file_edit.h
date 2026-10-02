#ifndef STRAPPY_FILE_EDIT_H
#define STRAPPY_FILE_EDIT_H

#ifdef __cplusplus
extern "C" {
#endif

char *strappy_file_edit_execute(const char *session_db_path,
                                long long session_id,
                                const char *arguments_json,
                                char **error_out);

#ifdef __cplusplus
}
#endif

#endif
