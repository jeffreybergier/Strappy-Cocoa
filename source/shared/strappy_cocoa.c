#include "strappy_cocoa.h"

#include "strappy_core.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#else
#include <time.h>
#endif

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRAPPY_COCOA_UNIX_MIN_SECONDS (-62167219200LL)
#define STRAPPY_COCOA_UNIX_MAX_SECONDS 253402300799LL
#define STRAPPY_COCOA_APPLE_EPOCH_OFFSET 978307200LL

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

static int strappy_cocoa_is_leap_year(int year)
{
  if ((year % 4) != 0) {
    return 0;
  }
  if ((year % 100) != 0) {
    return 1;
  }
  return ((year % 400) == 0) ? 1 : 0;
}

static int strappy_cocoa_days_in_month(int year, int month)
{
  static const int days_by_month[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };

  if ((month < 1) || (month > 12)) {
    return 0;
  }

  if ((month == 2) && strappy_cocoa_is_leap_year(year)) {
    return 29;
  }

  return days_by_month[month - 1];
}

static long long strappy_cocoa_days_from_civil(int year,
                                               unsigned int month,
                                               unsigned int day)
{
  long long adjusted_year;
  long long era;
  int adjusted_month;
  unsigned long long year_of_era;
  unsigned long long day_of_year;
  unsigned long long day_of_era;

  adjusted_year = (long long)year;
  if (month <= 2U) {
    adjusted_year--;
  }
  era = (adjusted_year >= 0LL) ? (adjusted_year / 400LL)
                               : ((adjusted_year - 399LL) / 400LL);
  year_of_era = (unsigned long long)(adjusted_year - (era * 400LL));
  adjusted_month = (int)month + ((month > 2U) ? -3 : 9);
  day_of_year =
    ((153ULL * (unsigned long long)adjusted_month + 2ULL) /
     5ULL) +
    (unsigned long long)day - 1ULL;
  day_of_era = (year_of_era * 365ULL) + (year_of_era / 4ULL) -
               (year_of_era / 100ULL) + day_of_year;
  return (era * 146097LL) + (long long)day_of_era - 719468LL;
}

static int strappy_cocoa_parse_iso8601_value(const char *iso8601,
                                             long long *unix_seconds_out,
                                             int *nanoseconds_out,
                                             char **error_out)
{
  const char *cursor;
  long long days;
  long long day_seconds;
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

  if ((month < 1) || (month > 12) || (day < 1) ||
      (day > strappy_cocoa_days_in_month(year, month))) {
    strappy_set_error(error_out, "ISO8601 date is invalid.");
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

  days = strappy_cocoa_days_from_civil(year,
                                       (unsigned int)month,
                                       (unsigned int)day);
  day_seconds = (days * 86400LL) + ((long long)hour * 3600LL) +
                ((long long)minute * 60LL) + (long long)second;
  if (!strappy_cocoa_add_long_long(day_seconds,
                                   -((long long)offset_seconds),
                                   &unix_seconds)) {
    strappy_set_error(error_out,
                      "ISO8601 datetime is outside the supported range.");
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

static char *strappy_cocoa_copy_base_iso8601_timestamp(
  long long unix_seconds,
  char **error_out)
{
  CFDateRef date;
  CFDateFormatterRef formatter;
  CFLocaleRef locale;
  CFTimeZoneRef time_zone;
  CFStringRef timestamp;
  char *result;
  CFAbsoluteTime absolute_time;

  absolute_time =
    (CFAbsoluteTime)((double)unix_seconds -
                     kCFAbsoluteTimeIntervalSince1970);
  date = CFDateCreate(kCFAllocatorDefault, absolute_time);
  if (date == NULL) {
    strappy_set_error(error_out, "Could not create CoreFoundation date.");
    return NULL;
  }

  locale = CFLocaleCreate(kCFAllocatorDefault, CFSTR("en_US_POSIX"));
  if (locale == NULL) {
    CFRelease(date);
    strappy_set_error(error_out, "Could not create CoreFoundation locale.");
    return NULL;
  }

  formatter = CFDateFormatterCreate(kCFAllocatorDefault,
                                    locale,
                                    kCFDateFormatterNoStyle,
                                    kCFDateFormatterNoStyle);
  CFRelease(locale);
  if (formatter == NULL) {
    CFRelease(date);
    strappy_set_error(error_out, "Could not create CoreFoundation formatter.");
    return NULL;
  }

  time_zone =
    CFTimeZoneCreateWithName(kCFAllocatorDefault, CFSTR("UTC"), 0);
  if (time_zone == NULL) {
    CFRelease(formatter);
    CFRelease(date);
    strappy_set_error(error_out, "Could not create CoreFoundation UTC zone.");
    return NULL;
  }

  CFDateFormatterSetProperty(formatter,
                             kCFDateFormatterTimeZone,
                             time_zone);
  CFRelease(time_zone);
  CFDateFormatterSetFormat(formatter,
                           CFSTR("yyyy-MM-dd'T'HH:mm:ss'Z'"));

  timestamp = CFDateFormatterCreateStringWithDate(kCFAllocatorDefault,
                                                  formatter,
                                                  date);
  result = strappy_cocoa_copy_cf_string_utf8(timestamp, error_out);

  if (timestamp != NULL) {
    CFRelease(timestamp);
  }
  CFRelease(formatter);
  CFRelease(date);
  return result;
}
#else
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

static char *strappy_cocoa_copy_base_iso8601_timestamp(
  long long unix_seconds,
  char **error_out)
{
  char buffer[32];
  struct tm *utc_time;
  time_t timestamp;

  timestamp = (time_t)unix_seconds;
  if ((long long)timestamp != unix_seconds) {
    strappy_set_error(error_out,
                      "Timestamp does not fit in this platform's time_t.");
    return NULL;
  }

  utc_time = gmtime(&timestamp);
  if (utc_time == NULL) {
    strappy_set_error(error_out, "Could not convert timestamp to UTC.");
    return NULL;
  }

  if (strftime(buffer,
               sizeof(buffer),
               "%Y-%m-%dT%H:%M:%SZ",
               utc_time) == 0U) {
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
