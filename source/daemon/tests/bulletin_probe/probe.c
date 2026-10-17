#include "probe.h"
#include <errno.h>
#include <notify.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int strappy_probe_sleep(long nanoseconds, time_t seconds)
{
  struct timespec remaining;
  remaining.tv_sec = seconds;
  remaining.tv_nsec = nanoseconds;
  while (nanosleep(&remaining, &remaining) != 0) {
    if (errno != EINTR) return 0;
  }
  return 1;
}

int strappy_bulletin_probe_run(void)
{
  int token = 0;
  int changed = 0;
  int attempt;
  int result = EXIT_FAILURE;
  if (notify_register_check(STRAPPY_BULLETIN_ACK, &token) != NOTIFY_STATUS_OK)
    return EXIT_FAILURE;
  /* The first check is always true. Clear it before sending our request. */
  if (notify_check(token, &changed) != NOTIFY_STATUS_OK) goto done;
  puts("bulletin-probe: started; waiting five seconds");
  (void)fflush(stdout);
  if (!strappy_probe_sleep(0, 5)) goto done;
  if (notify_post(STRAPPY_BULLETIN_REQUEST) != NOTIFY_STATUS_OK) goto done;
  puts("bulletin-probe: request posted; waiting for SpringBoard dispatch");
  (void)fflush(stdout);
  for (attempt = 0; attempt < 50; ++attempt) {
    if (!strappy_probe_sleep(100000000L, 0)) goto done;
    if (notify_check(token, &changed) != NOTIFY_STATUS_OK) goto done;
    if (changed) {
      puts("bulletin-probe: SpringBoard dispatch acknowledged; "
           "visible display requires observation");
      result = EXIT_SUCCESS;
      goto done;
    }
  }
  fputs("bulletin-probe: no dispatch acknowledgement within five seconds\n",
        stderr);
done:
  (void)notify_cancel(token);
  return result;
}
