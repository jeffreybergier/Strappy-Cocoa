#include "strappy_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static int strappy_utf8_is_continuation(unsigned char value)
{
  return ((value & 0xC0U) == 0x80U) ? 1 : 0;
}

static size_t strappy_utf8_sequence_length(const unsigned char *value,
                                           size_t length)
{
  unsigned char first;

  if ((value == NULL) || (length == 0U)) {
    return 0U;
  }

  first = value[0];
  if (first <= 0x7FU) {
    return 1U;
  }
  if ((first >= 0xC2U) && (first <= 0xDFU)) {
    return ((length >= 2U) && strappy_utf8_is_continuation(value[1])) ?
      2U : 0U;
  }
  if (first == 0xE0U) {
    return ((length >= 3U) &&
            (value[1] >= 0xA0U) && (value[1] <= 0xBFU) &&
            strappy_utf8_is_continuation(value[2])) ? 3U : 0U;
  }
  if (((first >= 0xE1U) && (first <= 0xECU)) ||
      ((first >= 0xEEU) && (first <= 0xEFU))) {
    return ((length >= 3U) &&
            strappy_utf8_is_continuation(value[1]) &&
            strappy_utf8_is_continuation(value[2])) ? 3U : 0U;
  }
  if (first == 0xEDU) {
    return ((length >= 3U) &&
            (value[1] >= 0x80U) && (value[1] <= 0x9FU) &&
            strappy_utf8_is_continuation(value[2])) ? 3U : 0U;
  }
  if (first == 0xF0U) {
    return ((length >= 4U) &&
            (value[1] >= 0x90U) && (value[1] <= 0xBFU) &&
            strappy_utf8_is_continuation(value[2]) &&
            strappy_utf8_is_continuation(value[3])) ? 4U : 0U;
  }
  if ((first >= 0xF1U) && (first <= 0xF3U)) {
    return ((length >= 4U) &&
            strappy_utf8_is_continuation(value[1]) &&
            strappy_utf8_is_continuation(value[2]) &&
            strappy_utf8_is_continuation(value[3])) ? 4U : 0U;
  }
  if (first == 0xF4U) {
    return ((length >= 4U) &&
            (value[1] >= 0x80U) && (value[1] <= 0x8FU) &&
            strappy_utf8_is_continuation(value[2]) &&
            strappy_utf8_is_continuation(value[3])) ? 4U : 0U;
  }
  return 0U;
}

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

int strappy_utf8_validate(const char *value, size_t length)
{
  const unsigned char *bytes;
  size_t index;

  if (value == NULL) {
    return (length == 0U) ? 1 : 0;
  }

  bytes = (const unsigned char *)value;
  index = 0U;
  while (index < length) {
    size_t sequence_length;

    sequence_length = strappy_utf8_sequence_length(bytes + index,
                                                    length - index);
    if (sequence_length == 0U) {
      return 0;
    }
    index += sequence_length;
  }
  return 1;
}

char *strappy_utf8_sanitized_string_duplicate(const char *value,
                                              size_t length)
{
  static const unsigned char replacement[] = { 0xEFU, 0xBFU, 0xBDU };
  const unsigned char *bytes;
  char *copy;
  size_t capacity;
  size_t input_index;
  size_t output_index;

  if (value == NULL) {
    return (length == 0U) ? strappy_string_duplicate("") : NULL;
  }
  if (length > ((((size_t)-1) - 1U) / 3U)) {
    return NULL;
  }

  capacity = (length * 3U) + 1U;
  copy = (char *)malloc(capacity);
  if (copy == NULL) {
    return NULL;
  }

  bytes = (const unsigned char *)value;
  input_index = 0U;
  output_index = 0U;
  while (input_index < length) {
    size_t sequence_length;

    sequence_length = strappy_utf8_sequence_length(bytes + input_index,
                                                    length - input_index);
    if ((sequence_length == 0U) || (bytes[input_index] == 0U)) {
      memcpy(copy + output_index, replacement, sizeof(replacement));
      output_index += sizeof(replacement);
      input_index++;
    } else {
      memcpy(copy + output_index, bytes + input_index, sequence_length);
      output_index += sequence_length;
      input_index += sequence_length;
    }
  }
  copy[output_index] = '\0';
  return copy;
}

void strappy_free_string(char *value)
{
  free(value);
}

void strappy_secure_free_string(char *value)
{
  volatile unsigned char *bytes;
  size_t length;

  if (value == NULL) {
    return;
  }
  bytes = (volatile unsigned char *)value;
  length = strlen(value);
  while (length > 0U) {
    *bytes++ = 0U;
    length--;
  }
  free(value);
}

long long strappy_unix_milliseconds(void)
{
  struct timeval now;

  if ((gettimeofday(&now, NULL) != 0) || (now.tv_sec < 0)) {
    return 0LL;
  }
  return ((long long)now.tv_sec * 1000LL) +
    ((long long)now.tv_usec / 1000LL);
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
