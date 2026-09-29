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

@interface StrappySessionRangeRows : NSArray {
  id<StrappySessionListSource> source_;
  NSUInteger offset_;
  NSUInteger count_;
}
- (id)initWithSource:(id<StrappySessionListSource>)source
             offset:(NSUInteger)offset count:(NSUInteger)count;
@end
@implementation StrappySessionRangeRows
- (id)initWithSource:(id<StrappySessionListSource>)source
             offset:(NSUInteger)offset count:(NSUInteger)count
{
  self = [super init];
  if (self != nil) {
    source_ = [source retain];
    offset_ = offset;
    count_ = count;
  }
  return self;
}
- (void)dealloc { [source_ release]; [super dealloc]; }
- (NSUInteger)count { return count_; }
- (id)copyWithZone:(NSZone *)zone { (void)zone; return [self retain]; }
- (id)objectAtIndex:(NSUInteger)index
{
  if (index >= count_) [NSException raise:NSRangeException format:@"Session outside section"];
  return [source_ objectAtIndex:offset_ + index];
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

+ (NSArray *)sectionsForSessions:(id<StrappySessionListSource>)sessions
{
  return [self sectionsForSessions:sessions date:[NSDate date]
                         calendar:[NSCalendar currentCalendar]];
}

+ (NSArray *)sectionsForSessions:(id<StrappySessionListSource>)sessions
                           date:(NSDate *)date
                       calendar:(NSCalendar *)calendar
{
  NSCalendarUnit units[4] = { XPCalendarUnitDay, XPCalendarUnitWeek,
    XPCalendarUnitMonth, XPCalendarUnitYear };
  NSArray *titles;
  NSMutableArray *sections;
  NSUInteger offset;
  unsigned int index;
  long long earliest;

  titles = [NSArray arrayWithObjects:NSLocalizedString(@"Today", nil),
    NSLocalizedString(@"This Week", nil), NSLocalizedString(@"This Month", nil),
    NSLocalizedString(@"This Year", nil), NSLocalizedString(@"Older", nil), nil];
  sections = [NSMutableArray array];
  offset = 0;
  earliest = 0;
  for (index = 0; index < 5; index++) {
    NSUInteger end;

    end = [sessions count];
    if (index < 4) {
      long long boundary;

      boundary = (long long)([[calendar XP_startOfUnit:units[index] forDate:date]
        timeIntervalSince1970] * 1000.0);
      /* A week may span the preceding month/year. Earlier buckets win. */
      if (index == 0 || boundary < earliest) earliest = boundary;
      end = [sessions countSinceTimestamp:earliest];
      if (end == NSNotFound) return nil;
    }
    if (end > offset) {
      NSArray *rows;

      rows = [[[StrappySessionRangeRows alloc] initWithSource:sessions
        offset:offset count:end - offset] autorelease];
      [sections addObject:[NSDictionary dictionaryWithObjectsAndKeys:
        [titles objectAtIndex:index], @"title", rows, @"sessions",
        [NSNumber numberWithUnsignedLong:offset], @"offset", nil]];
    }
    offset = end;
  }
  return sections;
}
@end

@implementation StrappySessionTableRows
- (id)initWithSessions:(id<StrappySessionListSource>)sessions sections:(NSArray *)sections
{
  self = [super init];
  if (self != nil) {
    source_ = [sessions retain];
    sections_ = [sections copy];
  }
  return self;
}
- (void)dealloc { [source_ release]; [sections_ release]; [super dealloc]; }
- (id)copyWithZone:(NSZone *)zone { (void)zone; return [self retain]; }
- (NSUInteger)count
{
  return [source_ count] == 0 ? 1 : [source_ count] + [sections_ count];
}
- (BOOL)isSectionAtIndex:(NSUInteger)index
{
  NSUInteger section;
  for (section = 0; section < [sections_ count]; section++) {
    NSUInteger offset;
    offset = [[[sections_ objectAtIndex:section] objectForKey:@"offset"] unsignedLongValue];
    if (index == offset + section) return YES;
  }
  return NO;
}
- (NSUInteger)indexForSessionIdentifier:(NSNumber *)identifier
{
  NSUInteger index;
  NSUInteger section;

  index = [source_ indexForSessionIdentifier:identifier];
  if (index == NSNotFound) return NSNotFound;
  for (section = 0; section < [sections_ count]; section++) {
    NSDictionary *group;
    NSUInteger end;

    group = [sections_ objectAtIndex:section];
    end = [[group objectForKey:@"offset"] unsignedLongValue] +
      [[group objectForKey:@"sessions"] count];
    if (index < end) return index + section + 1;
  }
  return NSNotFound;
}
- (id)objectAtIndex:(NSUInteger)index
{
  NSUInteger section;

  if (index >= [self count]) [NSException raise:NSRangeException format:@"Session outside table"];
  if ([source_ count] == 0) {
    return [NSDictionary dictionaryWithObjectsAndKeys:@"empty", @"row_type",
      NSLocalizedString(@"No conversations yet", nil), @"name",
      NSLocalizedString(@"Create a conversation to begin.", nil), @"last_message_text", nil];
  }
  for (section = 0; section < [sections_ count]; section++) {
    NSDictionary *group;
    NSUInteger header;

    group = [sections_ objectAtIndex:section];
    header = [[group objectForKey:@"offset"] unsignedLongValue] + section;
    if (index == header) return [NSDictionary dictionaryWithObjectsAndKeys:
      @"section", @"row_type", [group objectForKey:@"title"], @"section_title", nil];
    if (index < header + 1 + [[group objectForKey:@"sessions"] count])
      return [source_ objectAtIndex:index - section - 1];
  }
  [NSException raise:NSInternalInconsistencyException format:@"Inconsistent session sections"];
  return nil;
}
@end
