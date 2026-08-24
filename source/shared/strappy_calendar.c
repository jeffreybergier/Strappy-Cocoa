#include "strappy_calendar.h"

#include "strappy_core.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TODO: Replace the local Gregorian arithmetic with namespaced copies of
 * musl's MIT-licensed __tm_to_secs/__secs_to_tm helpers and their two small
 * dependencies after verifying astronomical year zero and retaining the
 * explicit 0000-9999 range contract in this module's tests. */

static int strappy_calendar_is_leap_year(int year)
{
  if ((year % 4) != 0) {
    return 0;
  }
  if ((year % 100) != 0) {
    return 1;
  }
  return ((year % 400) == 0) ? 1 : 0;
}

static int strappy_calendar_days_in_month(int year, int month)
{
  static const int days_by_month[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };

  if ((month < 1) || (month > 12)) {
    return 0;
  }
  if ((month == 2) && strappy_calendar_is_leap_year(year)) {
    return 29;
  }
  return days_by_month[month - 1];
}

static long long strappy_calendar_days_from_civil(int year,
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
    ((153ULL * (unsigned long long)adjusted_month + 2ULL) / 5ULL) +
    (unsigned long long)day - 1ULL;
  day_of_era = (year_of_era * 365ULL) + (year_of_era / 4ULL) -
               (year_of_era / 100ULL) + day_of_year;
  return (era * 146097LL) + (long long)day_of_era - 719468LL;
}

static void strappy_calendar_civil_from_days(long long days,
                                             int *year_out,
                                             int *month_out,
                                             int *day_out)
{
  long long shifted_days;
  long long era;
  long long year;
  unsigned long long day_of_era;
  unsigned long long year_of_era;
  unsigned long long day_of_year;
  unsigned long long adjusted_month;
  unsigned long long day;
  int month;

  shifted_days = days + 719468LL;
  era = (shifted_days >= 0LL) ? (shifted_days / 146097LL)
                              : ((shifted_days - 146096LL) / 146097LL);
  day_of_era = (unsigned long long)(shifted_days - (era * 146097LL));
  year_of_era =
    (day_of_era - (day_of_era / 1460ULL) +
     (day_of_era / 36524ULL) - (day_of_era / 146096ULL)) / 365ULL;
  year = (long long)year_of_era + (era * 400LL);
  day_of_year = day_of_era -
    ((365ULL * year_of_era) + (year_of_era / 4ULL) -
     (year_of_era / 100ULL));
  adjusted_month = ((5ULL * day_of_year) + 2ULL) / 153ULL;
  day = day_of_year - (((153ULL * adjusted_month) + 2ULL) / 5ULL) +
        1ULL;
  month = (int)adjusted_month + ((adjusted_month < 10ULL) ? 3 : -9);
  if (month <= 2) {
    year++;
  }

  *year_out = (int)year;
  *month_out = month;
  *day_out = (int)day;
}

static int strappy_calendar_add_long_long(long long left,
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

int strappy_calendar_unix_seconds_from_components(
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
  long long days;
  long long day_seconds;

  if ((unix_seconds_out == NULL) ||
      (month < 1) || (month > 12) || (day < 1) ||
      (day > strappy_calendar_days_in_month(year, month))) {
    strappy_set_error(error_out, "ISO8601 date is invalid.");
    return 0;
  }
  if ((hour < 0) || (hour > 23) ||
      (minute < 0) || (minute > 59) ||
      (second < 0) || (second > 59) ||
      (offset_seconds < -86399) || (offset_seconds > 86399)) {
    strappy_set_error(error_out, "ISO8601 time is invalid.");
    return 0;
  }

  days = strappy_calendar_days_from_civil(year,
                                          (unsigned int)month,
                                          (unsigned int)day);
  day_seconds = (days * 86400LL) + ((long long)hour * 3600LL) +
                ((long long)minute * 60LL) + (long long)second;
  if (!strappy_calendar_add_long_long(day_seconds,
                                      -((long long)offset_seconds),
                                      unix_seconds_out)) {
    strappy_set_error(error_out,
                      "ISO8601 datetime is outside the supported range.");
    return 0;
  }
  if ((*unix_seconds_out < STRAPPY_CALENDAR_UNIX_MIN_SECONDS) ||
      (*unix_seconds_out > STRAPPY_CALENDAR_UNIX_MAX_SECONDS)) {
    strappy_set_error(error_out,
                      "ISO8601 datetime must be between years 0000 and 9999.");
    return 0;
  }
  return 1;
}

static char *strappy_calendar_copy_base_iso8601_timestamp(
  long long unix_seconds,
  char **error_out)
{
  char buffer[32];
  long long days;
  long long second_of_day;
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  int written;

  if ((unix_seconds < STRAPPY_CALENDAR_UNIX_MIN_SECONDS) ||
      (unix_seconds > STRAPPY_CALENDAR_UNIX_MAX_SECONDS)) {
    strappy_set_error(error_out,
                      "Timestamp output must be between years 0000 and 9999.");
    return NULL;
  }

  days = unix_seconds / 86400LL;
  second_of_day = unix_seconds % 86400LL;
  if (second_of_day < 0LL) {
    second_of_day += 86400LL;
    days--;
  }
  strappy_calendar_civil_from_days(days, &year, &month, &day);
  hour = (int)(second_of_day / 3600LL);
  minute = (int)((second_of_day % 3600LL) / 60LL);
  second = (int)(second_of_day % 60LL);

  written = snprintf(buffer,
                     sizeof(buffer),
                     "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     year,
                     month,
                     day,
                     hour,
                     minute,
                     second);
  if ((written < 0) || ((size_t)written >= sizeof(buffer))) {
    strappy_set_error(error_out, "Could not format UTC timestamp.");
    return NULL;
  }
  return strappy_string_duplicate(buffer);
}

static char *strappy_calendar_copy_with_fraction(const char *base_iso8601,
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

char *strappy_calendar_copy_iso8601_timestamp(long long unix_seconds,
                                               int nanoseconds,
                                               char **error_out)
{
  char *base;
  char *result;

  if ((nanoseconds < 0) || (nanoseconds >= 1000000000)) {
    strappy_set_error(error_out, "Timestamp nanoseconds are invalid.");
    return NULL;
  }

  base = strappy_calendar_copy_base_iso8601_timestamp(unix_seconds, error_out);
  if (base == NULL) {
    return NULL;
  }
  result = strappy_calendar_copy_with_fraction(base, nanoseconds, error_out);
  free(base);
  return result;
}
