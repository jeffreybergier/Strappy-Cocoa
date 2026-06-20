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
    strappy_set_error(error_out, "CoreFoundation returned no timestamp.");
    return NULL;
  }

  length = CFStringGetLength(string);
  max_size = CFStringGetMaximumSizeForEncoding(length,
                                               kCFStringEncodingUTF8);
  if (max_size < 0) {
    strappy_set_error(error_out, "Could not measure CoreFoundation timestamp.");
    return NULL;
  }

  result = (char *)malloc((size_t)max_size + 1U);
  if (result == NULL) {
    strappy_set_error(error_out, "Could not allocate CoreFoundation timestamp.");
    return NULL;
  }

  if (!CFStringGetCString(string,
                          result,
                          max_size + 1,
                          kCFStringEncodingUTF8)) {
    free(result);
    strappy_set_error(error_out, "Could not encode CoreFoundation timestamp.");
    return NULL;
  }

  return result;
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
