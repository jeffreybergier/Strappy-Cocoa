#ifndef STRAPPY_CORE_H
#define STRAPPY_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

char *strappy_string_duplicate(const char *value);
char *strappy_string_duplicate_length(const char *value, size_t length);
int strappy_utf8_validate(const char *value, size_t length);
char *strappy_utf8_sanitized_string_duplicate(const char *value,
                                              size_t length);
void strappy_free_string(char *value);
void strappy_secure_free_string(char *value);
long long strappy_unix_milliseconds(void);
void strappy_set_error(char **error_out, const char *message);
void strappy_set_formatted_error(char **error_out, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
