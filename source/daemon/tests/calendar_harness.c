#include "strappy_calendar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int harness_is_leap_year(int year)
{
  return (((year % 4) == 0) &&
          (((year % 100) != 0) || ((year % 400) == 0))) ? 1 : 0;
}

static int harness_days_in_month(int year, int month)
{
  static const int days_by_month[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };

  if ((month == 2) && harness_is_leap_year(year)) {
    return 29;
  }
  return days_by_month[month - 1];
}

static int harness_run_exhaustive_date_round_trips(void)
{
  char expected[32];
  char *actual;
  char *error;
  long long unix_seconds;
  long long previous_seconds;
  long long date_count;
  int year;
  int month;
  int day;

  previous_seconds = -62167305600LL;
  date_count = 0LL;
  for (year = 0; year <= 9999; year++) {
    for (month = 1; month <= 12; month++) {
      int month_days;

      month_days = harness_days_in_month(year, month);
      for (day = 1; day <= month_days; day++) {
        error = NULL;
        unix_seconds = 0LL;
        if (!strappy_calendar_unix_seconds_from_components(year,
                                                            month,
                                                            day,
                                                            0,
                                                            0,
                                                            0,
                                                            0,
                                                            &unix_seconds,
                                                            &error) ||
            (unix_seconds != (previous_seconds + 86400LL))) {
          fprintf(stderr,
                  "Calendar sequence failed at %04d-%02d-%02d: %s\n",
                  year,
                  month,
                  day,
                  (error != NULL) ? error : "unexpected timestamp");
          free(error);
          return 0;
        }
        free(error);

        snprintf(expected,
                 sizeof(expected),
                 "%04d-%02d-%02dT00:00:00Z",
                 year,
                 month,
                 day);
        error = NULL;
        actual = strappy_calendar_copy_iso8601_timestamp(unix_seconds,
                                                         0,
                                                         &error);
        if ((actual == NULL) || (strcmp(actual, expected) != 0)) {
          fprintf(stderr,
                  "Calendar round trip failed at %s: %s\n",
                  expected,
                  (error != NULL) ? error :
                    ((actual != NULL) ? actual : "no timestamp"));
          free(actual);
          free(error);
          return 0;
        }
        free(actual);
        free(error);
        previous_seconds = unix_seconds;
        date_count++;
      }
    }
  }

  return ((date_count == 3652425LL) &&
          (previous_seconds == 253402214400LL)) ? 1 : 0;
}

static int harness_run_boundary_tests(void)
{
  char *value;
  char *error;
  long long unix_seconds;
  int ok;

  error = NULL;
  value = strappy_calendar_copy_iso8601_timestamp(-1LL, 500000000, &error);
  ok = (value != NULL) &&
       (strcmp(value, "1969-12-31T23:59:59.5Z") == 0) &&
       (error == NULL);
  free(value);
  free(error);

  if (ok) {
    error = NULL;
    value = strappy_calendar_copy_iso8601_timestamp(-62167219201LL,
                                                    0,
                                                    &error);
    ok = (value == NULL) && (error != NULL);
    free(value);
    free(error);
  }
  if (ok) {
    error = NULL;
    value = strappy_calendar_copy_iso8601_timestamp(253402300800LL,
                                                    0,
                                                    &error);
    ok = (value == NULL) && (error != NULL);
    free(value);
    free(error);
  }
  if (ok) {
    error = NULL;
    unix_seconds = 0LL;
    ok = !strappy_calendar_unix_seconds_from_components(1900,
                                                         2,
                                                         29,
                                                         0,
                                                         0,
                                                         0,
                                                         0,
                                                         &unix_seconds,
                                                         &error) &&
         (error != NULL);
    free(error);
  }
  if (ok) {
    error = NULL;
    unix_seconds = 0LL;
    ok = !strappy_calendar_unix_seconds_from_components(0,
                                                         1,
                                                         1,
                                                         0,
                                                         0,
                                                         0,
                                                         60,
                                                         &unix_seconds,
                                                         &error) &&
         (error != NULL);
    free(error);
  }
  if (ok) {
    error = NULL;
    unix_seconds = 0LL;
    ok = !strappy_calendar_unix_seconds_from_components(9999,
                                                         12,
                                                         31,
                                                         23,
                                                         59,
                                                         59,
                                                         -60,
                                                         &unix_seconds,
                                                         &error) &&
         (error != NULL);
    free(error);
  }
  return ok;
}

int main(void)
{
  if (!harness_run_boundary_tests() ||
      !harness_run_exhaustive_date_round_trips()) {
    return 1;
  }
  printf("calendar_harness passed (3,652,425 dates).\n");
  return 0;
}
