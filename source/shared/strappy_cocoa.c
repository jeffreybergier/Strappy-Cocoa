#if !defined(__APPLE__)
#define _GNU_SOURCE
#endif

#include "strappy_cocoa.h"

#include "strappy_core.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define STRAPPY_COCOA_UNIX_MIN_SECONDS (-62167219200LL)
#define STRAPPY_COCOA_UNIX_MAX_SECONDS 253402300799LL
#define STRAPPY_COCOA_APPLE_EPOCH_OFFSET 978307200LL
#define STRAPPY_COCOA_MAX_INFO_PLIST_BYTES (1024U * 1024U)

long long strappy_cocoa_monotonic_milliseconds(void)
{
#ifdef __APPLE__
  mach_timebase_info_data_t timebase;
  uint64_t absolute;
  long double milliseconds;

  if ((mach_timebase_info(&timebase) == KERN_SUCCESS) &&
      (timebase.denom != 0U)) {
    absolute = mach_absolute_time();
    milliseconds = ((long double)absolute * (long double)timebase.numer) /
      ((long double)timebase.denom * 1000000.0L);
    return (milliseconds >= (long double)LLONG_MAX) ?
      LLONG_MAX : (long long)milliseconds;
  }
#else
  struct timespec value;

  if (clock_gettime(CLOCK_MONOTONIC, &value) == 0) {
    return ((long long)value.tv_sec * 1000LL) +
      ((long long)value.tv_nsec / 1000000LL);
  }
#endif
  {
    struct timeval value;

    if (gettimeofday(&value, NULL) != 0) {
      return 0LL;
    }
    return ((long long)value.tv_sec * 1000LL) +
      ((long long)value.tv_usec / 1000LL);
  }
}

static int strappy_cocoa_string_has_value(const char *value)
{
  return ((value != NULL) && (value[0] != '\0')) ? 1 : 0;
}

static int strappy_cocoa_ascii_tolower(int character)
{
  if ((character >= 'A') && (character <= 'Z')) {
    return character + ('a' - 'A');
  }
  return character;
}

static char *strappy_cocoa_copy_bundle_fallback_name(const char *bundle_path,
                                                     char **error_out)
{
  const char *name;
  size_t length;
  char *result;

  if (!strappy_cocoa_string_has_value(bundle_path)) {
    strappy_set_error(error_out, "Bundle path is empty.");
    return NULL;
  }
  name = strrchr(bundle_path, '/');
  name = (name != NULL) ? name + 1 : bundle_path;
  length = strlen(name);
  if ((length >= 4U) &&
      (strappy_cocoa_ascii_tolower((unsigned char)name[length - 4U]) == '.') &&
      (strappy_cocoa_ascii_tolower((unsigned char)name[length - 3U]) == 'a') &&
      (strappy_cocoa_ascii_tolower((unsigned char)name[length - 2U]) == 'p') &&
      (strappy_cocoa_ascii_tolower((unsigned char)name[length - 1U]) == 'p')) {
    length -= 4U;
  }
  result = (char *)malloc(length + 1U);
  if (result == NULL) {
    strappy_set_error(error_out, "Could not allocate bundle name.");
    return NULL;
  }
  memcpy(result, name, length);
  result[length] = '\0';
  return result;
}

int strappy_cocoa_parse_timestamp_unit(const char *value,
                                       strappy_cocoa_timestamp_unit *unit_out,
                                       char **error_out)
{
  if (unit_out == NULL) {
    strappy_set_error(error_out, "Timestamp unit output is missing.");
    return 0;
  }

  if (!strappy_cocoa_string_has_value(value) ||
      (strcmp(value, "unix_seconds") == 0)) {
    *unit_out = STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_SECONDS;
    return 1;
  }

  if (strcmp(value, "unix_milliseconds") == 0) {
    *unit_out = STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_MILLISECONDS;
    return 1;
  }

  if (strcmp(value, "unix_microseconds") == 0) {
    *unit_out = STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_MICROSECONDS;
    return 1;
  }

  if (strcmp(value, "unix_nanoseconds") == 0) {
    *unit_out = STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_NANOSECONDS;
    return 1;
  }

  if ((strcmp(value, "apple_seconds") == 0) ||
      (strcmp(value, "cocoa_seconds") == 0) ||
      (strcmp(value, "cfabsolute_seconds") == 0)) {
    *unit_out = STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_SECONDS;
    return 1;
  }

  if ((strcmp(value, "apple_milliseconds") == 0) ||
      (strcmp(value, "cocoa_milliseconds") == 0) ||
      (strcmp(value, "cfabsolute_milliseconds") == 0)) {
    *unit_out = STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_MILLISECONDS;
    return 1;
  }

  if ((strcmp(value, "apple_microseconds") == 0) ||
      (strcmp(value, "cocoa_microseconds") == 0) ||
      (strcmp(value, "cfabsolute_microseconds") == 0)) {
    *unit_out = STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_MICROSECONDS;
    return 1;
  }

  if ((strcmp(value, "apple_nanoseconds") == 0) ||
      (strcmp(value, "cocoa_nanoseconds") == 0) ||
      (strcmp(value, "cfabsolute_nanoseconds") == 0)) {
    *unit_out = STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_NANOSECONDS;
    return 1;
  }

  strappy_set_formatted_error(error_out,
                              "Timestamp unit is not supported: %s",
                              value);
  return 0;
}

static long long strappy_cocoa_pow10(int exponent)
{
  long long value;

  value = 1LL;
  while (exponent > 0) {
    value *= 10LL;
    exponent--;
  }

  return value;
}

static int strappy_cocoa_timestamp_unit_config(
  strappy_cocoa_timestamp_unit unit,
  long long *epoch_offset_seconds_out,
  unsigned long long *units_per_second_out,
  long long *nanos_per_unit_out,
  int *max_fractional_digits_out)
{
  long long epoch_offset_seconds;
  unsigned long long units_per_second;
  long long nanos_per_unit;
  int max_fractional_digits;

  epoch_offset_seconds = 0LL;
  units_per_second = 1ULL;
  nanos_per_unit = 1000000000LL;
  max_fractional_digits = 9;

  switch (unit) {
    case STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_SECONDS:
      break;
    case STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_MILLISECONDS:
      units_per_second = 1000ULL;
      nanos_per_unit = 1000000LL;
      max_fractional_digits = 6;
      break;
    case STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_MICROSECONDS:
      units_per_second = 1000000ULL;
      nanos_per_unit = 1000LL;
      max_fractional_digits = 3;
      break;
    case STRAPPY_COCOA_TIMESTAMP_UNIT_UNIX_NANOSECONDS:
      units_per_second = 1000000000ULL;
      nanos_per_unit = 1LL;
      max_fractional_digits = 0;
      break;
    case STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_SECONDS:
      epoch_offset_seconds = STRAPPY_COCOA_APPLE_EPOCH_OFFSET;
      break;
    case STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_MILLISECONDS:
      epoch_offset_seconds = STRAPPY_COCOA_APPLE_EPOCH_OFFSET;
      units_per_second = 1000ULL;
      nanos_per_unit = 1000000LL;
      max_fractional_digits = 6;
      break;
    case STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_MICROSECONDS:
      epoch_offset_seconds = STRAPPY_COCOA_APPLE_EPOCH_OFFSET;
      units_per_second = 1000000ULL;
      nanos_per_unit = 1000LL;
      max_fractional_digits = 3;
      break;
    case STRAPPY_COCOA_TIMESTAMP_UNIT_APPLE_NANOSECONDS:
      epoch_offset_seconds = STRAPPY_COCOA_APPLE_EPOCH_OFFSET;
      units_per_second = 1000000000ULL;
      nanos_per_unit = 1LL;
      max_fractional_digits = 0;
      break;
    default:
      return 0;
  }

  if (epoch_offset_seconds_out != NULL) {
    *epoch_offset_seconds_out = epoch_offset_seconds;
  }
  if (units_per_second_out != NULL) {
    *units_per_second_out = units_per_second;
  }
  if (nanos_per_unit_out != NULL) {
    *nanos_per_unit_out = nanos_per_unit;
  }
  if (max_fractional_digits_out != NULL) {
    *max_fractional_digits_out = max_fractional_digits;
  }

  return 1;
}

static int strappy_cocoa_add_long_long(long long left,
                                       long long right,
                                       long long *sum_out)
{
  if (sum_out == NULL) {
    return 0;
  }

  if (((right > 0LL) && (left > (LLONG_MAX - right))) ||
      ((right == LLONG_MIN) && (left < 0LL)) ||
      ((right < 0LL) && (right != LLONG_MIN) &&
       (left < (LLONG_MIN - right)))) {
    return 0;
  }

  *sum_out = left + right;
  return 1;
}

static int strappy_cocoa_parse_timestamp_value(
  const char *timestamp,
  strappy_cocoa_timestamp_unit unit,
  long long *unix_seconds_out,
  int *nanoseconds_out,
  char **error_out)
{
  const char *cursor;
  int negative;
  int max_fractional_digits;
  unsigned long long whole_abs;
  unsigned long long units_per_second;
  unsigned long long seconds_abs;
  unsigned long long remainder_units;
  long long nanos_per_unit;
  long long epoch_offset_seconds;
  long long fractional_nanos;
  long long duration_seconds;
  long long unix_seconds;
  long long absolute_nanos;
  int fractional_digits;
  int saw_digit;

  if (!strappy_cocoa_string_has_value(timestamp) ||
      (unix_seconds_out == NULL) || (nanoseconds_out == NULL)) {
    strappy_set_error(error_out, "Timestamp is empty.");
    return 0;
  }

  if (!strappy_cocoa_timestamp_unit_config(unit,
                                           &epoch_offset_seconds,
                                           &units_per_second,
                                           &nanos_per_unit,
                                           &max_fractional_digits)) {
    strappy_set_error(error_out, "Timestamp unit configuration is invalid.");
    return 0;
  }

  cursor = timestamp;
  negative = 0;
  if ((*cursor == '+') || (*cursor == '-')) {
    negative = (*cursor == '-') ? 1 : 0;
    cursor++;
  }

  whole_abs = 0ULL;
  saw_digit = 0;
  while ((*cursor >= '0') && (*cursor <= '9')) {
    unsigned int digit;

    digit = (unsigned int)(*cursor - '0');
    if (whole_abs > ((ULLONG_MAX - (unsigned long long)digit) / 10ULL)) {
      strappy_set_error(error_out, "Timestamp is too large.");
      return 0;
    }
    whole_abs = (whole_abs * 10ULL) + (unsigned long long)digit;
    saw_digit = 1;
    cursor++;
  }

  if (!saw_digit) {
    strappy_set_error(error_out, "Timestamp must be numeric.");
    return 0;
  }

  fractional_nanos = 0LL;
  fractional_digits = 0;
  if (*cursor == '.') {
    cursor++;
    if ((*cursor < '0') || (*cursor > '9')) {
      strappy_set_error(error_out, "Timestamp fraction is empty.");
      return 0;
    }

    while ((*cursor >= '0') && (*cursor <= '9')) {
      if (fractional_digits >= max_fractional_digits) {
        strappy_set_error(
          error_out,
          "Timestamp has too many fractional digits for the selected unit.");
        return 0;
      }

      fractional_nanos =
        (fractional_nanos * 10LL) + (long long)(*cursor - '0');
      fractional_digits++;
      cursor++;
    }
  }

  if (*cursor != '\0') {
    strappy_set_error(error_out, "Timestamp contains invalid text.");
    return 0;
  }

  if (fractional_digits > 0) {
    fractional_nanos *=
      strappy_cocoa_pow10(max_fractional_digits - fractional_digits);
  }

  seconds_abs = whole_abs / units_per_second;
  remainder_units = whole_abs % units_per_second;
  if (seconds_abs > (unsigned long long)LLONG_MAX) {
    strappy_set_error(error_out, "Timestamp is too large.");
    return 0;
  }

  absolute_nanos =
    ((long long)remainder_units * nanos_per_unit) + fractional_nanos;
  if ((absolute_nanos < 0LL) || (absolute_nanos >= 1000000000LL)) {
    strappy_set_error(error_out, "Timestamp fraction is invalid.");
    return 0;
  }

  if (negative) {
    duration_seconds = -((long long)seconds_abs);
    if (absolute_nanos > 0LL) {
      if (duration_seconds == LLONG_MIN) {
        strappy_set_error(error_out, "Timestamp is too large.");
        return 0;
      }
      duration_seconds--;
      *nanoseconds_out = (int)(1000000000LL - absolute_nanos);
    } else {
      *nanoseconds_out = 0;
    }
  } else {
    duration_seconds = (long long)seconds_abs;
    *nanoseconds_out = (int)absolute_nanos;
  }

  if (!strappy_cocoa_add_long_long(epoch_offset_seconds,
                                   duration_seconds,
                                   &unix_seconds)) {
    strappy_set_error(error_out, "Timestamp is outside the supported range.");
    return 0;
  }

  if ((unix_seconds < STRAPPY_COCOA_UNIX_MIN_SECONDS) ||
      (unix_seconds > STRAPPY_COCOA_UNIX_MAX_SECONDS)) {
    strappy_set_error(error_out,
                      "Timestamp output must be between years 0000 and 9999.");
    return 0;
  }

  *unix_seconds_out = unix_seconds;
  return 1;
}

static int strappy_cocoa_parse_fixed_digits(const char **cursor_in_out,
                                            int digit_count,
                                            int *value_out)
{
  const char *cursor;
  int value;
  int index;

  if ((cursor_in_out == NULL) || (*cursor_in_out == NULL) ||
      (digit_count <= 0) || (value_out == NULL)) {
    return 0;
  }

  cursor = *cursor_in_out;
  value = 0;
  for (index = 0; index < digit_count; index++) {
    if ((cursor[index] < '0') || (cursor[index] > '9')) {
      return 0;
    }
    value = (value * 10) + (int)(cursor[index] - '0');
  }

  *cursor_in_out = cursor + digit_count;
  *value_out = value;
  return 1;
}

#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
/*
 * These Core Foundation Gregorian utilities preserve ISO 8601's proleptic
 * Gregorian calendar and astronomical year zero on every supported Apple OS.
 */
static int strappy_cocoa_calendar_components_to_unix_seconds(
  int year,
  int month,
  int day,
  int hour,
  int minute,
  int second,
  int offset_seconds,
  long long *unix_seconds_out,
  char **error_out)
{
#ifdef __APPLE__
  CFGregorianDate calendar_date;
  CFGregorianDate normalized_date;
  CFTimeZoneRef time_zone;
  CFAbsoluteTime absolute_time;
  double unix_seconds;

  time_zone = CFTimeZoneCreateWithTimeIntervalFromGMT(
    kCFAllocatorDefault,
    (CFTimeInterval)offset_seconds);
  if (time_zone == NULL) {
    strappy_set_error(error_out, "Could not create ISO8601 timezone.");
    return 0;
  }

  calendar_date.year = (SInt32)year;
  calendar_date.month = (SInt8)month;
  calendar_date.day = (SInt8)day;
  calendar_date.hour = (SInt8)hour;
  calendar_date.minute = (SInt8)minute;
  calendar_date.second = (double)second;
  absolute_time = CFGregorianDateGetAbsoluteTime(calendar_date, time_zone);
  /* A component round trip rejects normalized dates such as February 30. */
  normalized_date = CFAbsoluteTimeGetGregorianDate(absolute_time, time_zone);
  CFRelease(time_zone);

  if ((normalized_date.year != calendar_date.year) ||
      (normalized_date.month != calendar_date.month) ||
      (normalized_date.day != calendar_date.day) ||
      (normalized_date.hour != calendar_date.hour) ||
      (normalized_date.minute != calendar_date.minute) ||
      (normalized_date.second != calendar_date.second)) {
    strappy_set_error(error_out, "ISO8601 date is invalid.");
    return 0;
  }
  unix_seconds = absolute_time + kCFAbsoluteTimeIntervalSince1970;
  if ((unix_seconds < (double)LLONG_MIN) ||
      (unix_seconds > (double)LLONG_MAX)) {
    strappy_set_error(error_out,
                      "ISO8601 datetime is outside the supported range.");
    return 0;
  }
  *unix_seconds_out = (long long)unix_seconds;
#else
  struct tm local_time;
  struct tm normalized_time;
  time_t local_timestamp;
  time_t timestamp;

  memset(&local_time, 0, sizeof(local_time));
  local_time.tm_year = year - 1900;
  local_time.tm_mon = month - 1;
  local_time.tm_mday = day;
  local_time.tm_hour = hour;
  local_time.tm_min = minute;
  local_time.tm_sec = second;
  local_time.tm_isdst = 0;

  errno = 0;
  local_timestamp = timegm(&local_time);
  if (((local_timestamp == (time_t)-1) && (errno == EOVERFLOW)) ||
      (gmtime_r(&local_timestamp, &normalized_time) == NULL) ||
      (normalized_time.tm_year != year - 1900) ||
      (normalized_time.tm_mon != month - 1) ||
      (normalized_time.tm_mday != day) ||
      (normalized_time.tm_hour != hour) ||
      (normalized_time.tm_min != minute) ||
      (normalized_time.tm_sec != second)) {
    strappy_set_error(error_out, "ISO8601 date is invalid.");
    return 0;
  }

  normalized_time.tm_sec -= offset_seconds;
  errno = 0;
  timestamp = timegm(&normalized_time);
  if ((timestamp == (time_t)-1) && (errno == EOVERFLOW)) {
    strappy_set_error(error_out,
                      "ISO8601 datetime is outside the supported range.");
    return 0;
  }
  if ((time_t)(long long)timestamp != timestamp) {
    strappy_set_error(error_out,
                      "ISO8601 datetime does not fit in a long long.");
    return 0;
  }
  *unix_seconds_out = (long long)timestamp;
#endif
  return 1;
}
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif

static int strappy_cocoa_parse_iso8601_value(const char *iso8601,
                                             long long *unix_seconds_out,
                                             int *nanoseconds_out,
                                             char **error_out)
{
  const char *cursor;
  long long unix_seconds;
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  int nanoseconds;
  int fractional_digits;
  int offset_sign;
  int offset_hour;
  int offset_minute;
  int offset_seconds;

  if (!strappy_cocoa_string_has_value(iso8601) ||
      (unix_seconds_out == NULL) || (nanoseconds_out == NULL)) {
    strappy_set_error(error_out, "ISO8601 datetime is empty.");
    return 0;
  }

  cursor = iso8601;
  if (!strappy_cocoa_parse_fixed_digits(&cursor, 4, &year) ||
      (*cursor != '-')) {
    strappy_set_error(error_out,
                      "ISO8601 datetime must start with YYYY-MM-DD.");
    return 0;
  }
  cursor++;
  if (!strappy_cocoa_parse_fixed_digits(&cursor, 2, &month) ||
      (*cursor != '-')) {
    strappy_set_error(error_out,
                      "ISO8601 datetime must start with YYYY-MM-DD.");
    return 0;
  }
  cursor++;
  if (!strappy_cocoa_parse_fixed_digits(&cursor, 2, &day)) {
    strappy_set_error(error_out,
                      "ISO8601 datetime must start with YYYY-MM-DD.");
    return 0;
  }

  hour = 0;
  minute = 0;
  second = 0;
  nanoseconds = 0;
  offset_seconds = 0;
  if (*cursor != '\0') {
    if ((*cursor != 'T') && (*cursor != ' ')) {
      strappy_set_error(error_out,
                        "ISO8601 datetime must use T or space before time.");
      return 0;
    }
    cursor++;

    if (!strappy_cocoa_parse_fixed_digits(&cursor, 2, &hour) ||
        (*cursor != ':')) {
      strappy_set_error(error_out,
                        "ISO8601 time must use HH:MM:SS.");
      return 0;
    }
    cursor++;
    if (!strappy_cocoa_parse_fixed_digits(&cursor, 2, &minute) ||
        (*cursor != ':')) {
      strappy_set_error(error_out,
                        "ISO8601 time must use HH:MM:SS.");
      return 0;
    }
    cursor++;
    if (!strappy_cocoa_parse_fixed_digits(&cursor, 2, &second)) {
      strappy_set_error(error_out,
                        "ISO8601 time must use HH:MM:SS.");
      return 0;
    }

    if ((hour > 23) || (minute > 59) || (second > 59)) {
      strappy_set_error(error_out, "ISO8601 time is invalid.");
      return 0;
    }

    if (*cursor == '.') {
      cursor++;
      if ((*cursor < '0') || (*cursor > '9')) {
        strappy_set_error(error_out,
                          "ISO8601 fractional seconds are empty.");
        return 0;
      }

      fractional_digits = 0;
      while ((*cursor >= '0') && (*cursor <= '9')) {
        if (fractional_digits >= 9) {
          strappy_set_error(error_out,
                            "ISO8601 fractional seconds exceed nanosecond precision.");
          return 0;
        }
        nanoseconds = (nanoseconds * 10) + (int)(*cursor - '0');
        fractional_digits++;
        cursor++;
      }
      while (fractional_digits < 9) {
        nanoseconds *= 10;
        fractional_digits++;
      }
    }

    if ((*cursor == 'Z') || (*cursor == 'z')) {
      cursor++;
      if (*cursor != '\0') {
        strappy_set_error(error_out,
                          "ISO8601 datetime contains trailing text.");
        return 0;
      }
    } else if ((*cursor == '+') || (*cursor == '-')) {
      offset_sign = (*cursor == '-') ? -1 : 1;
      cursor++;
      if (!strappy_cocoa_parse_fixed_digits(&cursor, 2, &offset_hour)) {
        strappy_set_error(error_out,
                          "ISO8601 timezone offset must use HH:MM.");
        return 0;
      }
      offset_minute = 0;
      if (*cursor == ':') {
        cursor++;
        if (!strappy_cocoa_parse_fixed_digits(&cursor, 2, &offset_minute)) {
          strappy_set_error(error_out,
                            "ISO8601 timezone offset must use HH:MM.");
          return 0;
        }
      } else if ((*cursor >= '0') && (*cursor <= '9')) {
        if (!strappy_cocoa_parse_fixed_digits(&cursor, 2, &offset_minute)) {
          strappy_set_error(error_out,
                            "ISO8601 timezone offset must use HHMM.");
          return 0;
        }
      }

      if ((*cursor != '\0') || (offset_hour > 23) ||
          (offset_minute > 59)) {
        strappy_set_error(error_out, "ISO8601 timezone offset is invalid.");
        return 0;
      }
      offset_seconds =
        offset_sign * ((offset_hour * 3600) + (offset_minute * 60));
    } else if (*cursor != '\0') {
      strappy_set_error(error_out,
                        "ISO8601 datetime timezone must be Z or +/-HH:MM.");
      return 0;
    }
  }

  if (!strappy_cocoa_calendar_components_to_unix_seconds(year,
                                                          month,
                                                          day,
                                                          hour,
                                                          minute,
                                                          second,
                                                          offset_seconds,
                                                          &unix_seconds,
                                                          error_out)) {
    return 0;
  }

  if ((unix_seconds < STRAPPY_COCOA_UNIX_MIN_SECONDS) ||
      (unix_seconds > STRAPPY_COCOA_UNIX_MAX_SECONDS)) {
    strappy_set_error(error_out,
                      "ISO8601 datetime must be between years 0000 and 9999.");
    return 0;
  }

  *unix_seconds_out = unix_seconds;
  *nanoseconds_out = nanoseconds;
  return 1;
}

static char *strappy_cocoa_copy_timestamp_value_from_parts(
  long long unix_seconds,
  int nanoseconds,
  strappy_cocoa_timestamp_unit unit,
  char **error_out)
{
  char buffer[96];
  char fraction[10];
  long long epoch_offset_seconds;
  long long duration_seconds;
  long long nanos_per_unit;
  unsigned long long units_per_second;
  unsigned long long magnitude_seconds;
  unsigned long long magnitude_nanos;
  unsigned long long whole_units;
  unsigned long long unit_remainder;
  unsigned long long remainder_nanos;
  int max_fractional_digits;
  int negative;
  int fraction_length;
  int written;

  if ((nanoseconds < 0) || (nanoseconds >= 1000000000)) {
    strappy_set_error(error_out, "Timestamp nanoseconds are invalid.");
    return NULL;
  }

  if (!strappy_cocoa_timestamp_unit_config(unit,
                                           &epoch_offset_seconds,
                                           &units_per_second,
                                           &nanos_per_unit,
                                           &max_fractional_digits)) {
    strappy_set_error(error_out, "Timestamp unit configuration is invalid.");
    return NULL;
  }

  if (!strappy_cocoa_add_long_long(unix_seconds,
                                   -epoch_offset_seconds,
                                   &duration_seconds)) {
    strappy_set_error(error_out,
                      "ISO8601 datetime is outside the selected timestamp epoch.");
    return NULL;
  }

  negative = 0;
  magnitude_nanos = 0ULL;
  if (duration_seconds < 0LL) {
    negative = 1;
    if (nanoseconds > 0) {
      magnitude_seconds = (unsigned long long)(-(duration_seconds + 1LL));
      magnitude_nanos = (unsigned long long)(1000000000 - nanoseconds);
    } else {
      if (duration_seconds == LLONG_MIN) {
        strappy_set_error(error_out, "Timestamp is too large.");
        return NULL;
      }
      magnitude_seconds = (unsigned long long)(-duration_seconds);
    }
  } else {
    magnitude_seconds = (unsigned long long)duration_seconds;
    magnitude_nanos = (unsigned long long)nanoseconds;
  }

  if ((magnitude_seconds == 0ULL) && (magnitude_nanos == 0ULL)) {
    negative = 0;
  }

  if (magnitude_seconds > (ULLONG_MAX / units_per_second)) {
    strappy_set_error(error_out,
                      "Timestamp is too large for the selected unit.");
    return NULL;
  }
  whole_units = magnitude_seconds * units_per_second;
  unit_remainder = magnitude_nanos / (unsigned long long)nanos_per_unit;
  remainder_nanos = magnitude_nanos % (unsigned long long)nanos_per_unit;
  if (whole_units > (ULLONG_MAX - unit_remainder)) {
    strappy_set_error(error_out,
                      "Timestamp is too large for the selected unit.");
    return NULL;
  }
  whole_units += unit_remainder;

  if ((remainder_nanos > 0ULL) && (max_fractional_digits > 0)) {
    written = snprintf(fraction,
                       sizeof(fraction),
                       "%0*llu",
                       max_fractional_digits,
                       remainder_nanos);
    if ((written <= 0) || ((size_t)written >= sizeof(fraction))) {
      strappy_set_error(error_out, "Could not format timestamp fraction.");
      return NULL;
    }
    fraction_length = written;
    while ((fraction_length > 0) &&
           (fraction[fraction_length - 1] == '0')) {
      fraction_length--;
    }
    fraction[fraction_length] = '\0';
    written = snprintf(buffer,
                       sizeof(buffer),
                       "%s%llu.%s",
                       negative ? "-" : "",
                       whole_units,
                       fraction);
  } else {
    written = snprintf(buffer,
                       sizeof(buffer),
                       "%s%llu",
                       negative ? "-" : "",
                       whole_units);
  }

  if ((written <= 0) || ((size_t)written >= sizeof(buffer))) {
    strappy_set_error(error_out, "Could not format timestamp value.");
    return NULL;
  }

  return strappy_string_duplicate(buffer);
}

static char *strappy_cocoa_copy_with_fraction(const char *base_iso8601,
                                              int nanoseconds,
                                              char **error_out)
{
  char fraction[10];
  char *result;
  size_t base_length;
  int fraction_length;

  if ((base_iso8601 == NULL) ||
      (nanoseconds < 0) ||
      (nanoseconds >= 1000000000)) {
    strappy_set_error(error_out, "ISO8601 timestamp request is invalid.");
    return NULL;
  }

  if (nanoseconds == 0) {
    return strappy_string_duplicate(base_iso8601);
  }

  base_length = strlen(base_iso8601);
  if ((base_length == 0U) || (base_iso8601[base_length - 1U] != 'Z')) {
    strappy_set_error(error_out, "ISO8601 timestamp base is invalid.");
    return NULL;
  }

  snprintf(fraction, sizeof(fraction), "%09d", nanoseconds);
  fraction_length = 9;
  while ((fraction_length > 0) &&
         (fraction[fraction_length - 1] == '0')) {
    fraction_length--;
  }
  fraction[fraction_length] = '\0';

  if (base_length > (((size_t)-1) - (size_t)fraction_length - 2U)) {
    strappy_set_error(error_out, "ISO8601 timestamp is too large.");
    return NULL;
  }

  result = (char *)malloc(base_length + (size_t)fraction_length + 2U);
  if (result == NULL) {
    strappy_set_error(error_out, "Could not allocate ISO8601 timestamp.");
    return NULL;
  }

  memcpy(result, base_iso8601, base_length - 1U);
  result[base_length - 1U] = '.';
  memcpy(result + base_length, fraction, (size_t)fraction_length);
  result[base_length + (size_t)fraction_length] = 'Z';
  result[base_length + (size_t)fraction_length + 1U] = '\0';
  return result;
}

#ifdef __APPLE__
static char *strappy_cocoa_copy_cf_string_utf8(CFStringRef string,
                                               char **error_out)
{
  CFIndex length;
  CFIndex max_size;
  char *result;

  if (string == NULL) {
    strappy_set_error(error_out, "CoreFoundation returned no string.");
    return NULL;
  }

  length = CFStringGetLength(string);
  max_size = CFStringGetMaximumSizeForEncoding(length,
                                               kCFStringEncodingUTF8);
  if (max_size < 0) {
    strappy_set_error(error_out, "Could not measure CoreFoundation string.");
    return NULL;
  }

  result = (char *)malloc((size_t)max_size + 1U);
  if (result == NULL) {
    strappy_set_error(error_out, "Could not allocate CoreFoundation string.");
    return NULL;
  }

  if (!CFStringGetCString(string,
                          result,
                          max_size + 1,
                          kCFStringEncodingUTF8)) {
    free(result);
    strappy_set_error(error_out, "Could not encode CoreFoundation string.");
    return NULL;
  }

  return result;
}

char *strappy_cocoa_copy_app_version(char **error_out)
{
  CFBundleRef bundle;
  CFTypeRef version;

  bundle = CFBundleGetMainBundle();
  if (bundle == NULL) {
    strappy_set_error(error_out, "Could not load the main application bundle.");
    return NULL;
  }
  version = CFBundleGetValueForInfoDictionaryKey(
    bundle,
    CFSTR("CFBundleShortVersionString"));
  if ((version == NULL) ||
      (CFGetTypeID(version) != CFStringGetTypeID()) ||
      (CFStringGetLength((CFStringRef)version) == 0)) {
    strappy_set_error(
      error_out,
      "The main application Info.plist has no CFBundleShortVersionString.");
    return NULL;
  }
  return strappy_cocoa_copy_cf_string_utf8((CFStringRef)version, error_out);
}

char *strappy_cocoa_copy_localized_string(const char *key,
                                           char **error_out)
{
  CFBundleRef bundle;
  CFStringRef key_string;
  CFStringRef localized_string;
  char *result;

  if (!strappy_cocoa_string_has_value(key)) {
    strappy_set_error(error_out, "Localization key is empty.");
    return NULL;
  }

  bundle = CFBundleGetMainBundle();
  if (bundle == NULL) {
    strappy_set_error(error_out, "Could not load the main application bundle.");
    return NULL;
  }

  key_string = CFStringCreateWithCString(kCFAllocatorDefault,
                                         key,
                                         kCFStringEncodingUTF8);
  if (key_string == NULL) {
    strappy_set_error(error_out, "Could not encode localization key.");
    return NULL;
  }

  localized_string = CFBundleCopyLocalizedString(bundle,
                                                 key_string,
                                                 key_string,
                                                 NULL);
  result = strappy_cocoa_copy_cf_string_utf8(localized_string, error_out);
  if (localized_string != NULL) {
    CFRelease(localized_string);
  }
  CFRelease(key_string);
  return result;
}

int strappy_cocoa_copy_bundle_info(const char *bundle_path,
                                   char **name_out,
                                   char **bundle_identifier_out,
                                   char **error_out)
{
  CFBundleRef bundle;
  CFStringRef bundle_identifier;
  CFTypeRef bundle_name;
  CFURLRef bundle_url;
  char *identifier;
  char *name;

  if ((name_out == NULL) || (bundle_identifier_out == NULL)) {
    strappy_set_error(error_out, "Bundle info output is missing.");
    return 0;
  }
  *name_out = NULL;
  *bundle_identifier_out = NULL;
  if (!strappy_cocoa_string_has_value(bundle_path)) {
    strappy_set_error(error_out, "Bundle path is empty.");
    return 0;
  }

  name = NULL;
  identifier = NULL;
  bundle_url = CFURLCreateFromFileSystemRepresentation(
    kCFAllocatorDefault,
    (const UInt8 *)bundle_path,
    (CFIndex)strlen(bundle_path),
    1);
  bundle = (bundle_url != NULL) ?
    CFBundleCreate(kCFAllocatorDefault, bundle_url) : NULL;
  if (bundle_url != NULL) {
    CFRelease(bundle_url);
  }
  if (bundle != NULL) {
    bundle_name = CFBundleGetValueForInfoDictionaryKey(
      bundle,
      CFSTR("CFBundleDisplayName"));
    if ((bundle_name == NULL) ||
        (CFGetTypeID(bundle_name) != CFStringGetTypeID()) ||
        (CFStringGetLength((CFStringRef)bundle_name) == 0)) {
      bundle_name = CFBundleGetValueForInfoDictionaryKey(
        bundle,
        CFSTR("CFBundleName"));
    }
    if ((bundle_name != NULL) &&
        (CFGetTypeID(bundle_name) == CFStringGetTypeID()) &&
        (CFStringGetLength((CFStringRef)bundle_name) > 0)) {
      name = strappy_cocoa_copy_cf_string_utf8((CFStringRef)bundle_name,
                                               error_out);
      if (name == NULL) {
        CFRelease(bundle);
        return 0;
      }
    }

    bundle_identifier = CFBundleGetIdentifier(bundle);
    if ((bundle_identifier == NULL) ||
        (CFStringGetLength(bundle_identifier) == 0)) {
      bundle_name = CFBundleGetValueForInfoDictionaryKey(
        bundle,
        CFSTR("CFBundleIdentifier"));
      bundle_identifier = ((bundle_name != NULL) &&
                           (CFGetTypeID(bundle_name) == CFStringGetTypeID())) ?
        (CFStringRef)bundle_name : NULL;
    }
    if ((bundle_identifier != NULL) &&
        (CFStringGetLength(bundle_identifier) > 0)) {
      identifier = strappy_cocoa_copy_cf_string_utf8(bundle_identifier,
                                                     error_out);
      if (identifier == NULL) {
        free(name);
        CFRelease(bundle);
        return 0;
      }
    }
    CFRelease(bundle);
  }

  if (name == NULL) {
    name = strappy_cocoa_copy_bundle_fallback_name(bundle_path, error_out);
    if (name == NULL) {
      free(identifier);
      return 0;
    }
  }
  *name_out = name;
  *bundle_identifier_out = identifier;
  return 1;
}

static CFTypeRef strappy_cocoa_dictionary_value(CFDictionaryRef dictionary,
                                                CFStringRef key)
{
  if ((dictionary == NULL) ||
      (CFGetTypeID(dictionary) != CFDictionaryGetTypeID())) {
    return NULL;
  }
  return CFDictionaryGetValue(dictionary, key);
}

static char *strappy_cocoa_copy_optional_dictionary_string(
  CFDictionaryRef dictionary,
  CFStringRef key,
  char **error_out)
{
  CFTypeRef value;

  value = strappy_cocoa_dictionary_value(dictionary, key);
  if ((value == NULL) || (CFGetTypeID(value) != CFStringGetTypeID()) ||
      (CFStringGetLength((CFStringRef)value) == 0)) {
    return NULL;
  }
  return strappy_cocoa_copy_cf_string_utf8((CFStringRef)value, error_out);
}

int strappy_cocoa_copy_container_info(const char *container_path,
                                      char **identifier_out,
                                      char **creator_out,
                                      char **bundle_path_out,
                                      char **error_out)
{
  static const char metadata_name[] =
    "/.com.apple.containermanagerd.metadata.plist";
  CFDictionaryRef info;
  CFDictionaryRef parameters;
  CFDictionaryRef profile_info;
  CFDictionaryRef validation_info;
  CFPropertyListRef property_list;
  CFDataRef data;
  CFURLRef url;
  CFStringRef property_error;
  char *bundle_path;
  char *creator;
  char *identifier;
  char *metadata_path;
  size_t container_length;
  size_t metadata_length;
  SInt32 resource_error;

  if ((identifier_out == NULL) || (creator_out == NULL) ||
      (bundle_path_out == NULL)) {
    strappy_set_error(error_out, "Container info output is missing.");
    return 0;
  }
  *identifier_out = NULL;
  *creator_out = NULL;
  *bundle_path_out = NULL;
  if (!strappy_cocoa_string_has_value(container_path)) {
    return 1;
  }

  container_length = strlen(container_path);
  if (container_length > (((size_t)-1) - sizeof(metadata_name))) {
    strappy_set_error(error_out, "Container metadata path is too large.");
    return 0;
  }
  metadata_length = container_length + sizeof(metadata_name);
  metadata_path = (char *)malloc(metadata_length);
  if (metadata_path == NULL) {
    strappy_set_error(error_out, "Could not allocate container metadata path.");
    return 0;
  }
  snprintf(metadata_path,
           metadata_length,
           "%s%s",
           container_path,
           metadata_name);

  url = CFURLCreateFromFileSystemRepresentation(
    kCFAllocatorDefault,
    (const UInt8 *)metadata_path,
    (CFIndex)strlen(metadata_path),
    0);
  free(metadata_path);
  if (url == NULL) {
    return 1;
  }
  data = NULL;
  resource_error = 0;
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  if (!CFURLCreateDataAndPropertiesFromResource(kCFAllocatorDefault,
                                                 url,
                                                 &data,
                                                 NULL,
                                                 NULL,
                                                 &resource_error)) {
    CFRelease(url);
    return 1;
  }
  property_error = NULL;
  property_list = CFPropertyListCreateFromXMLData(kCFAllocatorDefault,
                                                   data,
                                                   kCFPropertyListImmutable,
                                                   &property_error);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  if (property_error != NULL) {
    CFRelease(property_error);
  }
  CFRelease(data);
  CFRelease(url);
  if ((property_list == NULL) ||
      (CFGetTypeID(property_list) != CFDictionaryGetTypeID())) {
    if (property_list != NULL) {
      CFRelease(property_list);
    }
    return 1;
  }

  info = (CFDictionaryRef)property_list;
  identifier = strappy_cocoa_copy_optional_dictionary_string(
    info,
    CFSTR("MCMMetadataIdentifier"),
    error_out);
  if ((identifier == NULL) && (error_out != NULL) && (*error_out != NULL)) {
    CFRelease(property_list);
    return 0;
  }
  creator = strappy_cocoa_copy_optional_dictionary_string(
    info,
    CFSTR("MCMMetadataCreator"),
    error_out);
  if ((creator == NULL) && (error_out != NULL) && (*error_out != NULL)) {
    free(identifier);
    CFRelease(property_list);
    return 0;
  }

  profile_info = (CFDictionaryRef)strappy_cocoa_dictionary_value(
    info,
    CFSTR("MCMMetadataInfo"));
  validation_info = (CFDictionaryRef)strappy_cocoa_dictionary_value(
    profile_info,
    CFSTR("SandboxProfileDataValidationInfo"));
  parameters = (CFDictionaryRef)strappy_cocoa_dictionary_value(
    validation_info,
    CFSTR("Parameters"));
  bundle_path = strappy_cocoa_copy_optional_dictionary_string(
    parameters,
    CFSTR("application_bundle"),
    error_out);
  if ((bundle_path == NULL) && (error_out != NULL) && (*error_out != NULL)) {
    free(identifier);
    free(creator);
    CFRelease(property_list);
    return 0;
  }

  CFRelease(property_list);
  *identifier_out = identifier;
  *creator_out = creator;
  *bundle_path_out = bundle_path;
  return 1;
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
static char *strappy_cocoa_copy_base_iso8601_timestamp(
  long long unix_seconds,
  char **error_out)
{
  char buffer[32];
  CFGregorianDate utc_date;
  CFAbsoluteTime absolute_time;
  int written;

  absolute_time =
    (CFAbsoluteTime)((double)unix_seconds -
                     kCFAbsoluteTimeIntervalSince1970);
  utc_date = CFAbsoluteTimeGetGregorianDate(absolute_time, NULL);
  written = snprintf(buffer,
                     sizeof(buffer),
                     "%04ld-%02d-%02dT%02d:%02d:%02dZ",
                     (long)utc_date.year,
                     (int)utc_date.month,
                     (int)utc_date.day,
                     (int)utc_date.hour,
                     (int)utc_date.minute,
                     (int)utc_date.second);
  if ((written < 0) || ((size_t)written >= sizeof(buffer))) {
    strappy_set_error(error_out, "Could not format UTC timestamp.");
    return NULL;
  }
  return strappy_string_duplicate(buffer);
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#else
static char *strappy_cocoa_copy_file_contents(const char *path,
                                               char **error_out)
{
  FILE *file;
  char *contents;
  long file_length;
  size_t length;

  file = fopen(path, "rb");
  if (file == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not open Info.plist at %s.",
                                path);
    return NULL;
  }
  if ((fseek(file, 0L, SEEK_END) != 0) ||
      ((file_length = ftell(file)) < 0L) ||
      ((unsigned long)file_length >
       (unsigned long)STRAPPY_COCOA_MAX_INFO_PLIST_BYTES) ||
      (fseek(file, 0L, SEEK_SET) != 0)) {
    fclose(file);
    strappy_set_error(error_out, "Could not measure the application Info.plist.");
    return NULL;
  }

  length = (size_t)file_length;
  contents = (char *)malloc(length + 1U);
  if (contents == NULL) {
    fclose(file);
    strappy_set_error(error_out, "Could not allocate the application Info.plist.");
    return NULL;
  }
  if ((length > 0U) && (fread(contents, 1U, length, file) != length)) {
    free(contents);
    fclose(file);
    strappy_set_error(error_out, "Could not read the application Info.plist.");
    return NULL;
  }
  contents[length] = '\0';
  if (fclose(file) != 0) {
    free(contents);
    strappy_set_error(error_out, "Could not close the application Info.plist.");
    return NULL;
  }
  return contents;
}

static char *strappy_cocoa_copy_source_info_plist_version(
  const char *path,
  char **error_out)
{
  static const char version_key[] =
    "<key>CFBundleShortVersionString</key>";
  static const char string_start[] = "<string>";
  static const char string_end[] = "</string>";
  char *contents;
  char *cursor;
  char *end;
  char *version;

  contents = strappy_cocoa_copy_file_contents(path, error_out);
  if (contents == NULL) {
    return NULL;
  }
  cursor = strstr(contents, version_key);
  if (cursor != NULL) {
    cursor += sizeof(version_key) - 1U;
    while ((*cursor == ' ') || (*cursor == '\t') ||
           (*cursor == '\r') || (*cursor == '\n')) {
      cursor++;
    }
  }
  if ((cursor == NULL) ||
      (strncmp(cursor, string_start, sizeof(string_start) - 1U) != 0)) {
    free(contents);
    strappy_set_error(
      error_out,
      "The application Info.plist has no CFBundleShortVersionString.");
    return NULL;
  }
  cursor += sizeof(string_start) - 1U;
  end = strstr(cursor, string_end);
  if ((end == NULL) || (end == cursor)) {
    free(contents);
    strappy_set_error(
      error_out,
      "The application Info.plist has an invalid "
      "CFBundleShortVersionString.");
    return NULL;
  }

  version = strappy_string_duplicate_length(cursor,
                                             (size_t)(end - cursor));
  free(contents);
  if (version == NULL) {
    strappy_set_error(error_out, "Could not allocate the application version.");
  }
  return version;
}

char *strappy_cocoa_copy_app_version(char **error_out)
{
  const char *info_plist_path;

  info_plist_path = getenv("STRAPPY_INFO_PLIST_PATH");
  if (!strappy_cocoa_string_has_value(info_plist_path)) {
    strappy_set_error(
      error_out,
      "STRAPPY_INFO_PLIST_PATH is required outside an application bundle.");
    return NULL;
  }
  return strappy_cocoa_copy_source_info_plist_version(info_plist_path,
                                                       error_out);
}

char *strappy_cocoa_copy_localized_string(const char *key,
                                           char **error_out)
{
  char *result;

  if (!strappy_cocoa_string_has_value(key)) {
    strappy_set_error(error_out, "Localization key is empty.");
    return NULL;
  }

  result = strappy_string_duplicate(key);
  if (result == NULL) {
    strappy_set_error(error_out, "Could not allocate localized string.");
  }
  return result;
}

int strappy_cocoa_copy_bundle_info(const char *bundle_path,
                                   char **name_out,
                                   char **bundle_identifier_out,
                                   char **error_out)
{
  if ((name_out == NULL) || (bundle_identifier_out == NULL)) {
    strappy_set_error(error_out, "Bundle info output is missing.");
    return 0;
  }
  *name_out = strappy_cocoa_copy_bundle_fallback_name(bundle_path, error_out);
  *bundle_identifier_out = NULL;
  return (*name_out != NULL) ? 1 : 0;
}

int strappy_cocoa_copy_container_info(const char *container_path,
                                      char **identifier_out,
                                      char **creator_out,
                                      char **bundle_path_out,
                                      char **error_out)
{
  (void)container_path;
  (void)error_out;
  if ((identifier_out == NULL) || (creator_out == NULL) ||
      (bundle_path_out == NULL)) {
    return 0;
  }
  *identifier_out = NULL;
  *creator_out = NULL;
  *bundle_path_out = NULL;
  return 1;
}

static char *strappy_cocoa_copy_base_iso8601_timestamp(
  long long unix_seconds,
  char **error_out)
{
  char buffer[32];
  struct tm utc_time;
  time_t timestamp;
  int written;

  timestamp = (time_t)unix_seconds;
  if ((long long)timestamp != unix_seconds) {
    strappy_set_error(error_out,
                      "Timestamp does not fit in this platform's time_t.");
    return NULL;
  }

  if (gmtime_r(&timestamp, &utc_time) == NULL) {
    strappy_set_error(error_out, "Could not convert timestamp to UTC.");
    return NULL;
  }

  written = snprintf(buffer,
                     sizeof(buffer),
                     "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     utc_time.tm_year + 1900,
                     utc_time.tm_mon + 1,
                     utc_time.tm_mday,
                     utc_time.tm_hour,
                     utc_time.tm_min,
                     utc_time.tm_sec);
  if ((written < 0) || ((size_t)written >= sizeof(buffer))) {
    strappy_set_error(error_out, "Could not format UTC timestamp.");
    return NULL;
  }

  return strappy_string_duplicate(buffer);
}
#endif

char *strappy_cocoa_copy_iso8601_timestamp(long long unix_seconds,
                                           int nanoseconds,
                                           char **error_out)
{
  char *base;
  char *result;

  if ((nanoseconds < 0) || (nanoseconds >= 1000000000)) {
    strappy_set_error(error_out, "Timestamp nanoseconds are invalid.");
    return NULL;
  }

  base = strappy_cocoa_copy_base_iso8601_timestamp(unix_seconds, error_out);
  if (base == NULL) {
    return NULL;
  }

  result = strappy_cocoa_copy_with_fraction(base, nanoseconds, error_out);
  free(base);
  return result;
}

char *strappy_cocoa_copy_iso8601_timestamp_value(
  const char *timestamp,
  strappy_cocoa_timestamp_unit unit,
  char **error_out)
{
  long long unix_seconds;
  int nanoseconds;

  unix_seconds = 0LL;
  nanoseconds = 0;
  if (!strappy_cocoa_parse_timestamp_value(timestamp,
                                           unit,
                                           &unix_seconds,
                                           &nanoseconds,
                                           error_out)) {
    return NULL;
  }

  return strappy_cocoa_copy_iso8601_timestamp(unix_seconds,
                                              nanoseconds,
                                              error_out);
}

char *strappy_cocoa_copy_timestamp_value_from_iso8601(
  const char *iso8601,
  strappy_cocoa_timestamp_unit unit,
  char **error_out)
{
  long long unix_seconds;
  int nanoseconds;

  unix_seconds = 0LL;
  nanoseconds = 0;
  if (!strappy_cocoa_parse_iso8601_value(iso8601,
                                         &unix_seconds,
                                         &nanoseconds,
                                         error_out)) {
    return NULL;
  }

  return strappy_cocoa_copy_timestamp_value_from_parts(unix_seconds,
                                                       nanoseconds,
                                                       unit,
                                                       error_out);
}
