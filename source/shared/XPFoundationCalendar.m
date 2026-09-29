#import "XPFoundation.h"

@implementation NSCalendar (XPFoundation)

- (NSDate *)XP_startOfUnit:(NSCalendarUnit)unit forDate:(NSDate *)date
{
  SEL selector;
  NSInvocation *invocation;
  NSDate *start;
  NSDate **startPointer;
  NSTimeInterval interval;
  NSTimeInterval *intervalPointer;
  BOOL success;
  NSDateComponents *components;
  XPUInteger units;

  selector = @selector(rangeOfUnit:startDate:interval:forDate:);
  if ([self respondsToSelector:selector]) {
    invocation = [NSInvocation invocationWithMethodSignature:
      [self methodSignatureForSelector:selector]];
    start = nil;
    startPointer = &start;
    interval = 0.0;
    intervalPointer = &interval;
    success = NO;
    [invocation setTarget:self];
    [invocation setSelector:selector];
    [invocation setArgument:&unit atIndex:2];
    [invocation setArgument:&startPointer atIndex:3];
    [invocation setArgument:&intervalPointer atIndex:4];
    [invocation setArgument:&date atIndex:5];
    [invocation invoke];
    [invocation getReturnValue:&success];
    if (success) {
      return start;
    }
  }

  /* Tiger has calendar component arithmetic, but no date-interval API. */
  units = XPCalendarUnitEra | XPCalendarUnitYear | XPCalendarUnitMonth |
          XPCalendarUnitDay;
  components = [self components:units fromDate:date];
  if (unit == XPCalendarUnitYear) {
    [components setMonth:1];
    [components setDay:1];
  } else if (unit == XPCalendarUnitMonth) {
    [components setDay:1];
  }
  start = [self dateFromComponents:components];
  if (unit == XPCalendarUnitWeek) {
    NSDateComponents *offset;
    XPInteger weekday;

    weekday = [[self components:XPCalendarUnitWeekday fromDate:date] weekday];
    offset = [[[NSDateComponents alloc] init] autorelease];
    [offset setDay:-((weekday - (XPInteger)[self firstWeekday] + 7) % 7)];
    start = [self dateByAddingComponents:offset toDate:start options:0];
    /* Midnight can be skipped by a time-zone transition. Normalize again so
     * the week's first day does not inherit the current day's 01:00 start. */
    start = [self dateFromComponents:[self components:units fromDate:start]];
  }
  return start;
}

@end
