/* Offline shared cache regression and old/new read-path benchmark. */
#import "StrappySession.h"
#import "strappy_run.h"
#include <assert.h>

#import "XPFoundationScrollProbe.h"
@interface StrappySession (SessionScrollingProbe)
+ (BOOL)sessionScrollingForbiddenRead:(NSNumber *)identifier;
@end
@implementation StrappySession (SessionScrollingProbe)
+ (BOOL)sessionScrollingForbiddenRead:(NSNumber *)identifier;
{
  (void)identifier;
  assert(!"Session cell polled SQLite for processing state");
  return NO;
}
@end

StrappySessionRows *StrappyCheckSessionRows(NSString *path, NSString **report)
{
  static BOOL installed = NO;
  if (!installed) {
    XPScrollProbeExchangeMethods(class_getClassMethod([StrappySession class],
      @selector(isPromptInFlightForSessionIdentifier:)),
      class_getClassMethod([StrappySession class],
      @selector(sessionScrollingForbiddenRead:)));
    installed = YES;
  }
  strappy_db_set_service_ready([path fileSystemRepresentation], 1);
  NSError *error = nil;
  StrappySessionRows *rows = [[[StrappySessionRows alloc]
    initWithDatabasePath:path error:&error] autorelease];
  NSUInteger i;
  NSTimeInterval start, oldTime, cachedTime;
  if (!rows || error || [rows count] != 20000) NSLog(@"Fixture %@: %@, count %lu", path, error, (unsigned long)[rows count]);
  assert(rows && !error && [rows count] == 20000);
  NSNumber *first = [[rows objectAtIndex:0] objectForKey:@"id"];
  NSNumber *distant = [[rows objectAtIndex:1024] objectForKey:@"id"];
  NSDictionary *states = [NSDictionary dictionaryWithObjectsAndKeys:
    [NSNumber numberWithBool:YES], first,
    [NSNumber numberWithBool:YES], distant, nil];
  assert([[rows applyPromptStates:states] count] == 2);
  assert([[rows applyPromptStates:states] count] == 0);
  for (i=0; i<512; i+=32) (void)[rows objectAtIndex:i];
  assert([[[rows objectAtIndex:0] objectForKey:@"prompt_in_flight"] boolValue]);
  assert([[[rows objectAtIndex:1024] objectForKey:@"prompt_in_flight"] boolValue]);
  assert([[rows applyPromptStates:[NSDictionary dictionaryWithObject:
    [NSNumber numberWithBool:NO] forKey:first]] count] == 1);
  assert(![[[rows objectAtIndex:0] objectForKey:@"prompt_in_flight"] boolValue]);
  assert([[rows applyPromptStates:[NSDictionary dictionaryWithObject:
    [NSNumber numberWithBool:YES] forKey:[NSNumber numberWithInt:-1]]] count] == 0);
  /* The C row reader never reads prompt text. Compare the exact former hot path
   * with a repeated cached traversal, without setting a device-specific limit. */
  start = [NSDate timeIntervalSinceReferenceDate];
  for (i=0; i<128; ++i) {
    strappy_run_record run;
    assert(strappy_run_latest([path fileSystemRepresentation], 1, &run));
    strappy_run_destroy(&run);
  }
  oldTime = [NSDate timeIntervalSinceReferenceDate]-start;
  start = [NSDate timeIntervalSinceReferenceDate];
  for (i=0; i<128; ++i)
    (void)[[[rows objectAtIndex:i] objectForKey:@"prompt_in_flight"] boolValue];
  cachedTime = [NSDate timeIntervalSinceReferenceDate]-start;
  *report = [NSString stringWithFormat:
    @"PASS: 20000 rows; pushed status, duplicate suppression, absent IDs, eviction; 128 former status reads %.2f ms, paged cached reads %.2f ms",
    oldTime*1000, cachedTime*1000];
  return rows;
}
