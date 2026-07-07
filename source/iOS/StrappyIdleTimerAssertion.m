#import "StrappyIdleTimerAssertion.h"

static NSUInteger StrappyIdleTimerAssertionDisableCount = 0U;
static BOOL StrappyIdleTimerAssertionPreviousIdleTimerDisabled = NO;

void StrappyIdleTimerAssertionSetEnabled(BOOL enabled)
{
  UIApplication *application;

  application = [UIApplication sharedApplication];
  @synchronized(application) {
    if (enabled) {
      if (StrappyIdleTimerAssertionDisableCount == 0U) {
        StrappyIdleTimerAssertionPreviousIdleTimerDisabled =
          [application isIdleTimerDisabled];
        [application setIdleTimerDisabled:YES];
      }
      StrappyIdleTimerAssertionDisableCount++;
      return;
    }

    if (StrappyIdleTimerAssertionDisableCount == 0U) {
      return;
    }

    StrappyIdleTimerAssertionDisableCount--;
    if (StrappyIdleTimerAssertionDisableCount == 0U) {
      [application
        setIdleTimerDisabled:StrappyIdleTimerAssertionPreviousIdleTimerDisabled];
    }
  }
}
