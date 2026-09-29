#import <Foundation/Foundation.h>
#import "StrappySessionSections.h"
#import "XPFoundation.h"
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>

/* Exercise the real Tiger branch while using the host calendar arithmetic. */
@interface LegacyCalendar : NSCalendar {
  NSCalendar *calendar_;
}
- (id)initWithCalendar:(NSCalendar *)calendar;
@end
@implementation LegacyCalendar
- (id)initWithCalendar:(NSCalendar *)calendar
{
  calendar_ = [calendar retain];
  return self;
}
- (BOOL)respondsToSelector:(SEL)selector
{
  if (sel_isEqual(selector, @selector(rangeOfUnit:startDate:interval:forDate:))) {
    return NO;
  }
  return [super respondsToSelector:selector];
}
- (NSDateComponents *)components:(NSUInteger)units fromDate:(NSDate *)date
{
  return [calendar_ components:units fromDate:date];
}
- (NSDate *)dateFromComponents:(NSDateComponents *)components
{
  return [calendar_ dateFromComponents:components];
}
- (NSDate *)dateByAddingComponents:(NSDateComponents *)components
                          toDate:(NSDate *)date options:(NSUInteger)options
{
  return [calendar_ dateByAddingComponents:components toDate:date options:options];
}
- (NSUInteger)firstWeekday { return [calendar_ firstWeekday]; }
- (void)dealloc { [calendar_ release]; [super dealloc]; }
@end

static void require(BOOL condition, const char *message)
{
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
  }
}

static NSDate *date(NSString *text)
{
  NSDateFormatter *formatter;
  NSDate *result;

  formatter = [[[NSDateFormatter alloc] init] autorelease];
  [formatter setLocale:[[[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"] autorelease]];
  [formatter setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
  [formatter setDateFormat:@"yyyy-MM-dd HH:mm:ss"];
  result = [formatter dateFromString:text];
  require(result != nil, "fixture date parses");
  return result;
}

static NSDictionary *session(NSString *timestamp)
{
  return [NSDictionary dictionaryWithObject:
    [NSNumber numberWithDouble:[date(timestamp) timeIntervalSince1970] * 1000.0]
    forKey:@"last_activity_at_ms"];
}

static NSString *group(NSCalendar *calendar, NSString *now, NSString *activity)
{
  NSArray *sections;

  sections = [StrappySessionSections sectionsForSessions:
    [NSArray arrayWithObject:session(activity)] date:date(now) calendar:calendar];
  require([sections count] == 1, "one session occupies exactly one section");
  return [[sections objectAtIndex:0] objectForKey:@"title"];
}

static void checkCalendar(NSCalendar *calendar)
{
  NSArray *sessions;
  NSArray *sections;
  NSArray *expected;
  NSUInteger index;

  sessions = [NSArray arrayWithObjects:session(@"2026-09-29 12:00:00"),
    session(@"2026-09-29 00:00:00"), session(@"2026-09-28 00:00:00"),
    session(@"2026-09-01 00:00:00"), session(@"2026-01-01 00:00:00"),
    session(@"2025-12-31 23:59:59"), nil];
  sections = [StrappySessionSections sectionsForSessions:sessions
    date:date(@"2026-09-29 15:00:00") calendar:calendar];
  expected = [NSArray arrayWithObjects:@"Today", @"This Week", @"This Month",
    @"This Year", @"Older", nil];
  require([[sections valueForKey:@"title"] isEqual:expected], "all buckets in order");
  for (index = 0; index < [sections count]; index++) {
    NSArray *members;
    members = [[sections objectAtIndex:index] objectForKey:@"sessions"];
    require([members count] == (index == 0 ? 2 : 1), "each session appears once");
    require([members objectAtIndex:0] == [sessions objectAtIndex:(index == 0 ? 0 : index + 1)],
      "preserve source objects and ordering");
  }
  require([[StrappySessionSections sectionsForSessions:[NSArray array]
    date:date(@"2026-09-29 15:00:00") calendar:calendar] count] == 0,
    "empty list has no headers");
  require([group(calendar, @"2026-09-29 15:00:00", @"2026-09-27 23:59:59")
    isEqual:@"This Month"], "week begins Monday");
  require([group(calendar, @"2026-10-01 12:00:00", @"2026-09-30 12:00:00")
    isEqual:@"This Week"], "week spans month boundary");
  require([group(calendar, @"2027-01-01 12:00:00", @"2026-12-31 12:00:00")
    isEqual:@"This Week"], "week spans year boundary");
  require([group(calendar, @"2026-09-30 00:00:00", @"2026-09-29 12:00:00")
    isEqual:@"This Week"], "midnight moves yesterday out of Today");
  require([group(calendar, @"2026-09-29 15:00:00", @"2026-09-30 12:00:00")
    isEqual:@"Today"], "future timestamps remain visible at top after clock rollback");
  sections = [StrappySessionSections sectionsForSessions:
    [NSArray arrayWithObject:[NSDictionary dictionary]]
    date:date(@"2026-09-29 15:00:00") calendar:calendar];
  require([[[sections objectAtIndex:0] objectForKey:@"title"] isEqual:@"Older"],
    "missing activity uses Older");
}

int main(void)
{
  NSAutoreleasePool *pool;
  NSCalendar *calendar;
  NSCalendar *legacy;
  NSUInteger index;

  pool = [[NSAutoreleasePool alloc] init];
  calendar = [[[NSCalendar alloc] initWithCalendarIdentifier:NSGregorianCalendar] autorelease];
  [calendar setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
  [calendar setFirstWeekday:2];
  legacy = [[[LegacyCalendar alloc] initWithCalendar:calendar] autorelease];
  checkCalendar(calendar);
  checkCalendar(legacy);
  [calendar setTimeZone:[NSTimeZone timeZoneWithName:@"America/Los_Angeles"]];
  for (index = 0; index < 2; index++) {
    NSCalendar *candidate;
    candidate = index == 0 ? calendar : legacy;
    require([group(candidate, @"2026-03-08 12:00:00", @"2026-03-08 08:00:00")
      isEqual:@"Today"], "spring DST day starts at local midnight");
    require([group(candidate, @"2026-03-08 12:00:00", @"2026-03-08 07:59:59")
      isEqual:@"This Week"], "UTC date alone does not determine Today");
    require([group(candidate, @"2026-11-01 12:00:00", @"2026-11-01 07:00:00")
      isEqual:@"Today"], "fall DST day includes first local midnight");
    require([group(candidate, @"2026-11-01 12:00:00", @"2026-11-01 06:59:59")
      isEqual:@"This Week"], "fall DST boundary excludes preceding day");
  }
  [calendar setTimeZone:[NSTimeZone timeZoneWithName:@"America/Havana"]];
  require([group(calendar, @"2026-03-08 12:00:00", @"2026-03-02 05:30:00")
    isEqual:@"This Week"], "week boundary survives a skipped midnight");
  require([group(legacy, @"2026-03-08 12:00:00", @"2026-03-02 05:30:00")
    isEqual:@"This Week"], "Tiger week boundary survives a skipped midnight");
  [calendar setTimeZone:[NSTimeZone timeZoneForSecondsFromGMT:0]];
  [calendar setFirstWeekday:1];
  require([group(calendar, @"2026-09-29 12:00:00", @"2026-09-27 00:00:00")
    isEqual:@"This Week"], "locale may begin week Sunday");
  require([group(legacy, @"2026-09-29 12:00:00", @"2026-09-27 00:00:00")
    isEqual:@"This Week"], "Tiger respects first weekday");
  {
    NSMutableArray *many;
    NSArray *sections;
    NSDictionary *summary;

    summary = session(@"2026-09-29 12:00:00");
    many = [NSMutableArray arrayWithCapacity:100000];
    for (index = 0; index < 100000; index++) {
      [many addObject:summary];
    }
    sections = [StrappySessionSections sectionsForSessions:many
      date:date(@"2026-09-29 15:00:00") calendar:calendar];
    require([sections count] == 1 &&
      [[[sections objectAtIndex:0] objectForKey:@"sessions"] count] == 100000,
      "large list preserves all rows without additional headers");
  }
  puts("Session sections: calendar boundaries, ordering, empty groups, DST and Tiger fallback passed.");
  [pool drain];
  return 0;
}
