/* Native Foundation test for the real Cocoa bridge, with only its mutation and
 * prompt transports replaced. Never contacts a provider or writes the app DB.
 * Build using session_options_device_probe.mk after the normal iOS build. */
#import "StrappySession.h"
#import "XPFoundation.h"
#include "strappy_store_client.h"
#include "strappy_ipc.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int saves, submissions, failSave, loseReply, finished, accepted;
static char submittedKey[65];
static NSString *failure;

char *strappy_test_mutation(unsigned int operation, const cJSON *command, unsigned int *status)
{
  (void)operation; (void)command; (void)status;
  assert(0 && "Send must use one combined command"); return NULL;
}
int strappy_test_command(unsigned int operation, const unsigned char *payload,
    unsigned int length, unsigned int *status)
{
  int lost, reject;
  unsigned int size;
  char json[4097];
  cJSON *settings;
  assert(![NSThread XP_isMainThread]);
  assert(operation == STRAPPY_IPC_SUBMIT_WITH_OPTIONS && length > 76);
  size = strappy_ipc_get32(payload+72);
  assert(size > 0 && size < length-76);
  memcpy(json,payload+76,size); json[size] = 0;
  settings = cJSON_Parse(json);
  assert(settings && !strcmp(cJSON_GetObjectItemCaseSensitive(settings,"rounds")->valuestring,"7"));
  cJSON_Delete(settings);
  pthread_mutex_lock(&mutex);
  ++saves; reject = failSave; lost = loseReply;
  if (!reject) {
    ++submissions;
    if (submittedKey[0]) assert(!memcmp(submittedKey,payload+8,64));
    else { memcpy(submittedKey,payload+8,64); submittedKey[64] = 0; }
  }
  pthread_mutex_unlock(&mutex);
  usleep(300000);
  *status = reject ? STRAPPY_IPC_INVALID : STRAPPY_IPC_OK;
  return !lost;
}

/* Suppress live-store refresh in this transport test. The production method is
 * separately tested by the daemon event suite; public edit/send methods run
 * unmodified. A test-only subclass marks simulated acceptance on the UI loop. */
@interface OptionsProbeSession : StrappySession
@end
@implementation OptionsProbeSession
- (void)serviceRunChanged:(id)ignored
{
  (void)ignored;
  pthread_mutex_lock(&mutex); accepted = 1; pthread_mutex_unlock(&mutex);
}
@end
@interface OptionsProbeObserver : NSObject
- (void)finish:(NSNotification *)notification;
@end
@implementation OptionsProbeObserver
- (void)finish:(NSNotification *)notification
{
  assert([NSThread isMainThread]);
  [failure release]; failure = [[[notification userInfo] objectForKey:@"error"] copy];
  finished = 1;
}
@end
static void pump(void)
{
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
}
static void waitFinish(void)
{
  NSTimeInterval deadline = [NSDate timeIntervalSinceReferenceDate] + 5;
  while (!finished && [NSDate timeIntervalSinceReferenceDate] < deadline) pump();
  assert(finished);
}
static OptionsProbeSession *session(NSNumber *identifier, StrappySessionOptions *options)
{
  /* Avoid initialization's DB read and queued service refresh: this fixture is
   * an already-loaded UI session, exactly the state of an open options panel. */
  OptionsProbeSession *s = [[OptionsProbeSession alloc] init];
  [s setValue:identifier forKey:@"sessionIdentifier_"];
  [s setValue:options forKey:@"options_"];
  [s setValue:[NSNumber numberWithBool:YES] forKey:@"optionsLoaded_"];
  return [s autorelease];
}
int main(void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSNumber *identifier = [NSNumber numberWithInt:987654321];
  NSString *pendingKey = @"StrappyPendingPrompt-987654321";
  StrappySessionOptions *options = [[[StrappySessionOptions alloc]
    initWithModelIdentifier:@"fixture" providerAccountIdentifier:@"fixture"
    assistantSetIdentifier:@"world_knowledge" webProvider:StrappyWebProviderNone
    webSearchEnabled:NO bashEnabled:NO limitToOneTool:NO roundLimit:3
    workingDirectory:@"/tmp"] autorelease];
  OptionsProbeSession *s = session(identifier,options), *other;
  OptionsProbeObserver *observer = [[[OptionsProbeObserver alloc] init] autorelease];
  NSTimeInterval start, elapsed;
  int i, ticks = 0;
  NSError *error = nil;
  [[NSUserDefaults standardUserDefaults] removeObjectForKey:pendingKey];
  [[NSNotificationCenter defaultCenter] addObserver:observer selector:@selector(finish:)
    name:StrappySessionPromptDidFinishNotification object:s];
  start = [NSDate timeIntervalSinceReferenceDate];
  for (i = 0; i < 1000; ++i) {
    StrappySessionOptions *edit = [s optionsWithError:NULL];
    [edit setBashEnabled:(i % 2) != 0];
    assert([s updateOptions:edit changedFields:StrappySessionOptionBash error:NULL]);
  }
  elapsed = [NSDate timeIntervalSinceReferenceDate] - start;
  assert(saves == 0 && submissions == 0);
  printf("1000 local option edits: %.3f ms total, %.3f ms/edit; zero IPC\n",elapsed*1000,elapsed);
  assert(elapsed < 1.0);
  /* Independent controllers share drafts, and stale copies only replace the
   * explicitly edited fields. The initial options still have Bash disabled. */
  other = session(identifier,options);
  assert([[other optionsWithError:NULL] bashEnabled]);
  [options setRoundLimit:7];
  assert([other updateOptions:options changedFields:StrappySessionOptionRoundLimit error:NULL]);
  assert([[s optionsWithError:NULL] bashEnabled]);
  assert([[s optionsWithError:NULL] roundLimit] == 7);
  [s setValue:[NSNumber numberWithBool:NO] forKey:@"optionsLoaded_"];
  assert([[s optionsWithError:NULL] roundLimit] == 7); /* invalidation preserves draft */
  failSave = 1;
  start = [NSDate timeIntervalSinceReferenceDate];
  assert([s beginResponsesPrompt:@"offline probe" context:nil error:&error]);
  elapsed = [NSDate timeIntervalSinceReferenceDate]-start;
  printf("Send with a 300 ms settings transport: %.3f ms on main thread\n",elapsed*1000);
  assert(elapsed < .1);
  assert(![other updateOptions:options changedFields:StrappySessionOptionBash error:NULL]);
  while (!finished) { pump(); ++ticks; assert(ticks < 500); }
  printf("Delayed save: %d main-loop ticks, %d saves, %d submissions; %s\n", ticks, saves, submissions, [failure UTF8String]);
  assert(ticks >= 2 && failure != nil && submissions == 0);
  assert([[s optionsWithError:NULL] roundLimit] == 7);
  assert([[NSUserDefaults standardUserDefaults] objectForKey:pendingKey] == nil);
  failSave = 0; loseReply = 1; finished = 0;
  assert([s beginResponsesPrompt:@"offline probe" context:nil error:&error]);
  waitFinish();
  assert(saves == 2 && submissions == 1);
  assert(![s updateOptions:options changedFields:StrappySessionOptionRoundLimit error:NULL]);
  assert([[[NSUserDefaults standardUserDefaults] dictionaryForKey:pendingKey] objectForKey:@"optionsCommand"] != nil);
  loseReply = 0; accepted = 0;
  assert([s beginResponsesPrompt:@"offline probe" context:nil error:&error]);
  start = [NSDate timeIntervalSinceReferenceDate];
  while (!accepted && [NSDate timeIntervalSinceReferenceDate]-start < 5) pump();
  assert(accepted && saves == 3 && submissions == 2);
  assert([[NSUserDefaults standardUserDefaults] objectForKey:pendingKey] == nil);
  [[NSNotificationCenter defaultCenter] removeObserver:observer];
  [[NSUserDefaults standardUserDefaults] synchronize];
  puts("PASS: shared drafts, field merge, invalidation, responsive Send, failed save blocks submission, durable retry preserves combined payload");
  [failure release];
  [pool release];
  return 0;
}
