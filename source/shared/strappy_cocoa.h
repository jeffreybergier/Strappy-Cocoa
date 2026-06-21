#ifndef STRAPPY_COCOA_H
#define STRAPPY_COCOA_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum strappy_cocoa_timestamp_unit {
  STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_SECONDS = 1,
  STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_MILLISECONDS = 2,
  STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_MICROSECONDS = 3,
  STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_NANOSECONDS = 4,
  STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_SECONDS = 5,
  STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_MILLISECONDS = 6,
  STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_MICROSECONDS = 7,
  STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_NANOSECONDS = 8
} strappy_cocoa_timestamp_unit;

int strappy_cocoa_parse_timestamp_unit(const char *value,
                                       strappy_cocoa_timestamp_unit *unit_out,
                                       char **error_out);
char *strappy_cocoa_copy_iso8601_timestamp(long long unix_seconds,
                                           int nanoseconds,
                                           char **error_out);
char *strappy_cocoa_copy_iso8601_timestamp_value(
  const char *timestamp,
  strappy_cocoa_timestamp_unit unit,
  char **error_out);
char *strappy_cocoa_copy_timestamp_value_from_iso8601(
  const char *iso8601,
  strappy_cocoa_timestamp_unit unit,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
