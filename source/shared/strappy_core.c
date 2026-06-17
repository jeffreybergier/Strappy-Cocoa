#include "strappy_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *strappy_string_duplicate_length(const char *value, size_t length)
{
  char *copy;

  if (value == NULL) {
    return NULL;
  }

  if (length == (size_t)-1) {
    return NULL;
  }

  copy = (char *)malloc(length + 1U);
  if (copy == NULL) {
    return NULL;
  }

  if (length > 0U) {
    memcpy(copy, value, length);
  }
  copy[length] = '\0';

  return copy;
}

char *strappy_string_duplicate(const char *value)
{
  if (value == NULL) {
    return NULL;
  }

  return strappy_string_duplicate_length(value, strlen(value));
}

void strappy_free_string(char *value)
{
  free(value);
}

void strappy_set_error(char **error_out, const char *message)
{
  if (error_out == NULL) {
    return;
  }

  if (message == NULL) {
    message = "Unknown Strappy error.";
  }

  *error_out = strappy_string_duplicate(message);
}

void strappy_set_formatted_error(char **error_out, const char *format, ...)
{
  char buffer[1024];
  va_list args;
  int result;

  if (error_out == NULL) {
    return;
  }

  if (format == NULL) {
    strappy_set_error(error_out, NULL);
    return;
  }

  va_start(args, format);
  result = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (result < 0) {
    strappy_set_error(error_out, "Could not format Strappy error.");
    return;
  }

  buffer[sizeof(buffer) - 1U] = '\0';
  strappy_set_error(error_out, buffer);
}
