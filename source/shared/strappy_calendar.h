#ifndef STRAPPY_CALENDAR_H
#define STRAPPY_CALENDAR_H

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_CALENDAR_UNIX_MIN_SECONDS (-62167219200LL)
#define STRAPPY_CALENDAR_UNIX_MAX_SECONDS 253402300799LL

int strappy_calendar_unix_seconds_from_components(
  int year,
  int month,
  int day,
  int hour,
  int minute,
  int second,
  int offset_seconds,
  long long *unix_seconds_out,
  char **error_out);
char *strappy_calendar_copy_iso8601_timestamp(long long unix_seconds,
                                               int nanoseconds,
                                               char **error_out);

#ifdef __cplusplus
}
#endif

#endif
