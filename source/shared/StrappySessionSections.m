#import "StrappySessionSections.h"
#import "XPFoundation.h"

NSString * const StrappySessionDateBoundariesDidChangeNotification =
  @"StrappySessionDateBoundariesDidChangeNotification";

/* macOS-only process-lifetime timer; never retains a view controller. */
@interface StrappySessionDateMonitor : NSObject {
  NSTimer *timer_;
}
- (void)refresh:(id)sender;
@end

@implementation StrappySessionDateMonitor
- (void)refresh:(id)sender
{
  NSCalendar *calendar;
  NSDate *now;
  NSDate *tomorrow;
  NSDateComponents *offset;

  [timer_ invalidate];
  [timer_ release];
  calendar = [NSCalendar currentCalendar];
  now = [NSDate date];
  offset = [[[NSDateComponents alloc] init] autorelease];
  [offset setDay:1];
  tomorrow = [calendar dateByAddingComponents:offset
    toDate:[calendar XP_startOfUnit:XPCalendarUnitDay forDate:now] options:0];
  tomorrow = [calendar XP_startOfUnit:XPCalendarUnitDay forDate:tomorrow];
  timer_ = [[NSTimer timerWithTimeInterval:
    MAX(1.0, [tomorrow timeIntervalSinceDate:now])
    target:self selector:@selector(refresh:) userInfo:nil repeats:NO] retain];
  [[NSRunLoop currentRunLoop] addTimer:timer_ forMode:NSDefaultRunLoopMode];
  if (sender != nil) {
    [[NSNotificationCenter defaultCenter]
      postNotificationName:StrappySessionDateBoundariesDidChangeNotification
                    object:nil];
  }
}
@end

@implementation StrappySessionSections
+ (void)startMidnightTimer
{
  static StrappySessionDateMonitor *monitor = nil;
  if (monitor == nil) {
    monitor = [[StrappySessionDateMonitor alloc] init];
    [monitor refresh:nil];
  }
}

+ (NSArray *)sectionsForSessions:(NSArray *)sessions
{
  return [self sectionsForSessions:sessions date:[NSDate date]
                         calendar:[NSCalendar currentCalendar]];
}

+ (NSArray *)sectionsForSessions:(NSArray *)sessions
                           date:(NSDate *)date
                       calendar:(NSCalendar *)calendar
{
  NSCalendarUnit units[4] = { XPCalendarUnitDay, XPCalendarUnitWeek,
    XPCalendarUnitMonth, XPCalendarUnitYear };
  NSTimeInterval boundaries[4];
  NSMutableArray *buckets[5];
  NSArray *titles;
  NSMutableArray *sections;
  NSUInteger sessionIndex;
  unsigned int index;

  titles = [NSArray arrayWithObjects:NSLocalizedString(@"Today", nil),
    NSLocalizedString(@"This Week", nil), NSLocalizedString(@"This Month", nil),
    NSLocalizedString(@"This Year", nil), NSLocalizedString(@"Older", nil), nil];
  for (index = 0; index < 4; index++) {
    boundaries[index] = [[calendar XP_startOfUnit:units[index] forDate:date]
      timeIntervalSince1970] * 1000.0;
  }
  for (index = 0; index < 5; index++) {
    buckets[index] = [NSMutableArray array];
  }
  for (sessionIndex = 0; sessionIndex < [sessions count]; sessionIndex++) {
    NSDictionary *session;
    NSNumber *activity;
    unsigned int bucket;

    session = [sessions objectAtIndex:sessionIndex];
    activity = [session objectForKey:@"last_activity_at_ms"];
    bucket = 4;
    if ([activity isKindOfClass:[NSNumber class]]) {
      for (index = 0; index < 4; index++) {
        if ([activity doubleValue] >= boundaries[index]) {
          bucket = index;
          break;
        }
      }
    }
    [buckets[bucket] addObject:session];
  }
  sections = [NSMutableArray array];
  for (index = 0; index < 5; index++) {
    if ([buckets[index] count] > 0) {
      [sections addObject:[NSDictionary dictionaryWithObjectsAndKeys:
        [titles objectAtIndex:index], @"title", buckets[index], @"sessions", nil]];
    }
  }
  return sections;
}
@end
