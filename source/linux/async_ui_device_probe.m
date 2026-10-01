/* Native, offline tests of the actual shared dispatcher. No provider requests,
 * credential access, or database mutations. Run as mobile on a test phone. */
#import "StrappyAsync.h"
#import "StrappySession.h"
#import "FileScanner.h"
#import "XPFoundation.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static BOOL releaseCommands, releaseQueries;
static unsigned int commandEntered, queryEntered, delivered, errors, queryResults;
static NSMutableArray *order;
static NSString *lastQuery;

@interface AsyncFixture : NSObject
+ (BOOL)command:(NSString *)text big:(long long)big flag:(BOOL)flag error:(NSError **)error;
+ (NSDictionary *)query:(NSString *)text error:(NSError **)error;
+ (BOOL)fail:(NSError **)error;
@end
@implementation AsyncFixture
+ (BOOL)command:(NSString *)text big:(long long)big flag:(BOOL)flag error:(NSError **)error
{
  (void)error;
  assert(![NSThread XP_isMainThread]);
  assert(big == 1099511627776LL && flag);
  pthread_mutex_lock(&lock);
  ++commandEntered;
  while (!releaseCommands) pthread_cond_wait(&condition,&lock);
  [order addObject:text];
  pthread_mutex_unlock(&lock);
  return YES;
}
+ (NSDictionary *)query:(NSString *)text error:(NSError **)error
{
  (void)error;
  assert(![NSThread XP_isMainThread]);
  pthread_mutex_lock(&lock);
  ++queryEntered;
  if ([text isEqualToString:@"old"]) while (!releaseQueries) pthread_cond_wait(&condition,&lock);
  pthread_mutex_unlock(&lock);
  return [NSDictionary dictionaryWithObject:text forKey:@"query"];
}
+ (BOOL)fail:(NSError **)error
{
  assert(![NSThread XP_isMainThread]);
  *error = [NSError errorWithDomain:@"Fixture" code:42 userInfo:
    [NSDictionary dictionaryWithObject:@"Expected offline failure" forKey:NSLocalizedDescriptionKey]];
  return NO;
}
@end

@interface AsyncObserver : NSObject
- (void)done:(StrappyAsyncResult *)result;
- (void)queryDone:(StrappyAsyncResult *)result;
- (void)failed:(NSNotification *)notification;
@end
@implementation AsyncObserver
- (void)done:(StrappyAsyncResult *)result
{
  assert([NSThread XP_isMainThread]);
  ++delivered;
  if ([[result context] isEqual:@"handled"]) { assert(![result succeeded]); [result handleError]; }
}
- (void)queryDone:(StrappyAsyncResult *)result
{
  assert([NSThread XP_isMainThread] && [result succeeded]);
  ++queryResults;
  [lastQuery release]; lastQuery = [[[result value] objectForKey:@"query"] copy];
}
- (void)failed:(NSNotification *)notification
{
  assert([NSThread XP_isMainThread]);
  NSInteger code = [[[notification userInfo] objectForKey:@"error"] code];
  assert(code == 42 || code == 4);
  ++errors;
}
@end

static void pump(void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
  [pool release];
}
static void waitFor(unsigned int *value, unsigned int expected)
{
  NSTimeInterval deadline = [NSDate timeIntervalSinceReferenceDate]+10;
  for (;;) {
    unsigned int found;
    pthread_mutex_lock(&lock); found = *value; pthread_mutex_unlock(&lock);
    if (found >= expected) return;
    assert([NSDate timeIntervalSinceReferenceDate] < deadline);
    pump();
  }
}
int main(void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  AsyncObserver *observer = [[[AsyncObserver alloc] init] autorelease];
  NSMutableString *text = [NSMutableString stringWithString:@"first"];
  NSError *error = nil;
  NSTimeInterval start, elapsed;
  order = [[NSMutableArray alloc] init];
  [[NSNotificationCenter defaultCenter] addObserver:observer selector:@selector(failed:)
    name:StrappyAsyncCommandFailedNotification object:nil];
  start = [NSDate timeIntervalSinceReferenceDate];
  [[StrappyAsync commandTarget:[AsyncFixture class] title:@"Fixture" receiver:observer completion:@selector(done:) context:nil]
    command:text big:1099511627776LL flag:YES error:NULL];
  elapsed = [NSDate timeIntervalSinceReferenceDate]-start;
  /* The first NSProxy invocation has cold-start cost on emulated Tiger.
   * Returning while the fixture stays blocked proves the caller does not wait. */
  [text setString:@"MUTATED AFTER ENQUEUE"];
  [[StrappyAsync commandTarget:[AsyncFixture class] title:@"Fixture" receiver:observer completion:@selector(done:) context:nil]
    command:@"second" big:1099511627776LL flag:YES error:NULL];
  waitFor(&commandEntered,1);
  assert(delivered == 0 && [StrappyAsync pendingCommandCount] == 2);
  /* Queries must complete even while the command worker is blocked. */
  [[StrappyAsync queryTarget:[AsyncFixture class] receiver:observer completion:@selector(queryDone:) context:nil] query:@"independent" error:NULL];
  waitFor(&queryResults,1);
  assert([lastQuery isEqual:@"independent"] && delivered == 0);
  /* A running old query and a queued middle query must never replace latest. */
  [[StrappyAsync queryTarget:[AsyncFixture class] receiver:observer completion:@selector(queryDone:) context:nil] query:@"old" error:NULL];
  waitFor(&queryEntered,2);
  [[StrappyAsync queryTarget:[AsyncFixture class] receiver:observer completion:@selector(queryDone:) context:nil] query:@"middle" error:NULL];
  [[StrappyAsync queryTarget:[AsyncFixture class] receiver:observer completion:@selector(queryDone:) context:nil] query:@"latest" error:NULL];
  pthread_mutex_lock(&lock); releaseQueries = YES; pthread_cond_broadcast(&condition); pthread_mutex_unlock(&lock);
  waitFor(&queryResults,2);
  assert([lastQuery isEqual:@"latest"] && queryEntered == 3);
  pthread_mutex_lock(&lock); releaseCommands = YES; pthread_cond_broadcast(&condition); pthread_mutex_unlock(&lock);
  waitFor(&delivered,2);
  assert(([order isEqual:[NSArray arrayWithObjects:@"first",@"second",nil]]));
  assert([StrappyAsync pendingCommandCount] == 0);
  [[StrappyAsync commandTarget:[AsyncFixture class] title:@"Fixture" receiver:observer completion:@selector(done:) context:nil] fail:NULL];
  [[StrappyAsync commandTarget:[AsyncFixture class] title:@"Fixture" receiver:observer completion:@selector(done:) context:@"handled"] fail:NULL];
  /* A failure without an originating view still reaches the common channel. */
  [[StrappyAsync commandTarget:[AsyncFixture class] title:@"Fixture" receiver:nil completion:NULL context:nil] fail:NULL];
  waitFor(&errors,2); waitFor(&delivered,4);
  assert(errors == 2 && [StrappyAsync pendingCommandCount] == 0);
  /* Queue overflow must complete asynchronously and release pending UI state. */
  pthread_mutex_lock(&lock); releaseCommands = NO; pthread_mutex_unlock(&lock);
  [[StrappyAsync commandTarget:[AsyncFixture class] title:@"Fixture" receiver:observer completion:@selector(done:) context:nil]
    command:@"blocking" big:1099511627776LL flag:YES error:NULL];
  waitFor(&commandEntered,3);
  unsigned int index;
  for (index = 0; index < 128; ++index)
    [[StrappyAsync commandTarget:[AsyncFixture class] title:@"Fixture" receiver:observer completion:@selector(done:) context:nil]
      command:@"queued" big:1099511627776LL flag:YES error:NULL];
  [[StrappyAsync commandTarget:[AsyncFixture class] title:@"Fixture" receiver:observer completion:@selector(done:) context:nil]
    command:@"rejected" big:1099511627776LL flag:YES error:NULL];
  waitFor(&errors,3);
  assert([StrappyAsync pendingCommandCount] == 129);
  pthread_mutex_lock(&lock); releaseCommands = YES; pthread_cond_broadcast(&condition); pthread_mutex_unlock(&lock);
  waitFor(&delivered,134);
  assert([StrappyAsync pendingCommandCount] == 0);
  assert(commandEntered == 131 && errors == 3);
  /* Both production bridges reject main-thread transport before connecting. */
  assert(![StrappySession setModelAllowed:YES forModelIdentifier:@"offline-fixture" error:&error]);
  assert(error && [error code] == 1001);
  error = nil;
  assert(![[FileScanner sharedScanner] setCatalogedDatabaseAllowed:YES forCatalogIdentifier:[NSNumber numberWithInt:1] error:&error]);
  assert(error != nil);
  printf("PASS async UI: enqueue %.3f ms; FIFO, immutable arguments, 64-bit scalars, independent query worker, latest-only results, main-thread completions, exactly-once errors, detached errors, queue overflow, IPC guards\n",elapsed*1000);
  [[NSNotificationCenter defaultCenter] removeObserver:observer];
  [order release]; [lastQuery release];
  [pool release];
  return 0;
}
