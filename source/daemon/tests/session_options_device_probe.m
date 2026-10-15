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
static NSData *expectedPrompt;
static pthread_cond_t credentialCondition = PTHREAD_COND_INITIALIZER;
static int credentialReads, releaseCredential, credentialNotifications;
static unsigned int deleteStatus;
static int deletions;
char *strappy_test_credential(const cJSON *command, unsigned int *status)
{
  const char *account = cJSON_GetObjectItemCaseSensitive(command,"id")->valuestring;
  assert(![NSThread XP_isMainThread]);
  pthread_mutex_lock(&mutex); ++credentialReads;
  if (!strcmp(account,"blocked-cache"))
    while (!releaseCredential) pthread_cond_wait(&credentialCondition,&mutex);
  pthread_mutex_unlock(&mutex);
  *status = !strcmp(account,"unavailable-cache") ? STRAPPY_IPC_NOT_READY : STRAPPY_IPC_OK;
  if (*status != STRAPPY_IPC_OK) return NULL;
  return strdup("{\"present\":true,\"identity\":\"fixture-public-identity\",\"needs_refresh\":false}");
}

char *strappy_test_mutation(unsigned int operation, const cJSON *command, unsigned int *status)
{
  const cJSON *action = cJSON_GetObjectItemCaseSensitive(command, "action");
  assert([NSThread XP_isMainThread]);
  assert(operation == STRAPPY_IPC_MUTATE && cJSON_IsString(action));
  assert(!strcmp(action->valuestring, "delete_session"));
  ++deletions;
  *status = deleteStatus;
  return deleteStatus == STRAPPY_IPC_OK ? strdup("") : NULL;
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
  assert(size <= STRAPPY_IPC_MAX_OPTIONS_BYTES);
  assert(length-76-size == [expectedPrompt length]);
  assert(!memcmp(payload+76+size,[expectedPrompt bytes],[expectedPrompt length]));
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
- (void)credentialChanged:(NSNotification *)notification
{
  (void)notification;
  assert([NSThread XP_isMainThread]); ++credentialNotifications;
}
- (void)finish:(NSNotification *)notification
{
  assert([NSThread XP_isMainThread]);
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
  NSTimeInterval deadline = [NSDate timeIntervalSinceReferenceDate] + 30;
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
static void testDeletion(StrappySessionOptions *defaults)
{
  NSNumber *identifier = [NSNumber numberWithInt:987654322];
  NSNumber *otherIdentifier = [NSNumber numberWithInt:987654323];
  NSString *pendingKey = @"StrappyPendingPrompt-987654322";
  NSString *otherKey = @"StrappyPendingPrompt-987654323";
  NSUserDefaults *preferences = [NSUserDefaults standardUserDefaults];
  OptionsProbeSession *original = session(identifier, defaults);
  OptionsProbeSession *other = session(otherIdentifier, defaults);
  StrappySessionOptions *edit = [[defaults copy] autorelease];
  NSError *error = nil;
  [preferences removeObjectForKey:pendingKey];
  [preferences removeObjectForKey:otherKey];
  [edit setBashEnabled:YES];
  [edit setRoundLimit:9];
  assert([original updateOptions:edit changedFields:
    StrappySessionOptionBash | StrappySessionOptionRoundLimit error:NULL]);
  assert([other updateOptions:edit changedFields:StrappySessionOptionBash
    error:NULL]);
  [preferences setObject:[NSDictionary dictionaryWithObject:@"fixture"
    forKey:@"key"] forKey:pendingKey];
  [preferences setObject:@"other retry" forKey:otherKey];
  deleteStatus = STRAPPY_IPC_BUSY;
  assert(![StrappySession deleteSessionWithIdentifier:identifier error:&error]);
  assert(error != nil && deletions == 1);
  assert([[original optionsWithError:NULL] roundLimit] == 9);
  assert([preferences objectForKey:pendingKey] != nil);
  /* An uncertain acknowledgement must preserve edits too. */
  deleteStatus = STRAPPY_IPC_NOT_READY;
  assert(![StrappySession deleteSessionWithIdentifier:identifier error:NULL]);
  assert([[original optionsWithError:NULL] bashEnabled]);
  assert([preferences objectForKey:pendingKey] != nil);
  deleteStatus = STRAPPY_IPC_OK;
  assert([StrappySession deleteSessionWithIdentifier:identifier error:NULL]);
  assert(deletions == 3);
  assert([preferences objectForKey:pendingKey] == nil);
  assert(![[session(identifier, defaults) optionsWithError:NULL] bashEnabled]);
  assert([[session(identifier, defaults) optionsWithError:NULL] roundLimit] == 3);
  assert([[other optionsWithError:NULL] bashEnabled]);
  assert([preferences objectForKey:otherKey] != nil);
  assert([StrappySession deleteSessionWithIdentifier:otherIdentifier error:NULL]);
  puts("PASS deletion: confirmed delete clears draft and retry identity; "
    "failed/uncertain delete preserves both; reused ID gets defaults; "
    "other conversation unchanged");
}
static void testCredentialCache(OptionsProbeObserver *observer)
{
  int i, reads;
  NSTimeInterval deadline;
  NSDictionary *status;
  [[NSNotificationCenter defaultCenter] addObserver:observer selector:@selector(credentialChanged:)
    name:StrappyCredentialStatusDidChangeNotification object:nil];
  assert([StrappySession credentialStatusForProviderAccountIdentifier:@"blocked-cache" error:NULL] == nil);
  assert([StrappySession credentialStatusForProviderAccountIdentifier:@"other-cache" error:NULL] == nil);
  deadline=[NSDate timeIntervalSinceReferenceDate]+30;
  while (!credentialNotifications && [NSDate timeIntervalSinceReferenceDate]<deadline) pump();
  assert(credentialNotifications == 1);
  status=[StrappySession credentialStatusForProviderAccountIdentifier:@"other-cache" error:NULL];
  assert([[status objectForKey:@"present"] boolValue]);
  for (i=0;i<1000;++i)
    assert([StrappySession credentialStatusForProviderAccountIdentifier:@"blocked-cache" error:NULL] == nil);
  pthread_mutex_lock(&mutex); reads=credentialReads; releaseCredential=1;
  pthread_cond_broadcast(&credentialCondition); pthread_mutex_unlock(&mutex);
  assert(reads==2);
  while (credentialNotifications<2 && [NSDate timeIntervalSinceReferenceDate]<deadline) pump();
  assert(credentialNotifications==2);
  status=[StrappySession credentialStatusForProviderAccountIdentifier:@"blocked-cache" error:NULL];
  assert([[status objectForKey:@"present"] boolValue]);
  assert([StrappySession credentialStatusForProviderAccountIdentifier:@"unavailable-cache" error:NULL] == nil);
  while (credentialNotifications<3 && [NSDate timeIntervalSinceReferenceDate]<deadline) pump();
  assert(credentialNotifications==3);
  status=[StrappySession credentialStatusForProviderAccountIdentifier:@"unavailable-cache" error:NULL];
  assert([[status objectForKey:@"unavailable"] boolValue] && [status objectForKey:@"present"] == nil);
  puts("PASS credential cache: blocked reads leave main loop and other accounts responsive; reads coalesce; unknown differs from signed-out");
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
  NSMutableData *bytes = [NSMutableData dataWithLength:STRAPPY_IPC_MAX_PROMPT_BYTES];
  unsigned char *raw = [bytes mutableBytes];
  unsigned int byte;
  NSString *largePrompt;
  testDeletion(options);
  for (byte=0; byte<STRAPPY_IPC_MAX_PROMPT_BYTES; byte+=2) { raw[byte]=0xc3; raw[byte+1]=0xa9; }
  expectedPrompt = bytes;
  largePrompt = [[[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding] autorelease];
  assert(largePrompt);
  assert(![s beginResponsesPrompt:[largePrompt stringByAppendingString:@"x"] context:nil error:&error]);
  assert(error && saves == 0 && submissions == 0); error = nil;
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
  assert([s beginResponsesPrompt:largePrompt context:nil error:&error]);
  elapsed = [NSDate timeIntervalSinceReferenceDate]-start;
  printf("Send with a 300 ms settings transport: %.3f ms on main thread\n",elapsed*1000);
  assert(elapsed < 1.0);
  assert(![other updateOptions:options changedFields:StrappySessionOptionBash error:NULL]);
  while (!finished) { pump(); ++ticks; assert(ticks < 500); }
  printf("Delayed save: %d main-loop ticks, %d saves, %d submissions; %s\n", ticks, saves, submissions, [failure UTF8String]);
  assert(ticks >= 2 && failure != nil && submissions == 0);
  assert([[s optionsWithError:NULL] roundLimit] == 7);
  assert([[NSUserDefaults standardUserDefaults] objectForKey:pendingKey] == nil);
  failSave = 0; loseReply = 1; finished = 0;
  assert([s beginResponsesPrompt:largePrompt context:nil error:&error]);
  waitFinish();
  assert(saves == 2 && submissions == 1);
  assert(![s updateOptions:options changedFields:StrappySessionOptionRoundLimit error:NULL]);
  assert([[[NSUserDefaults standardUserDefaults] dictionaryForKey:pendingKey] objectForKey:@"optionsCommand"] != nil);
  loseReply = 0; accepted = 0;
  assert([s beginResponsesPrompt:largePrompt context:nil error:&error]);
  start = [NSDate timeIntervalSinceReferenceDate];
  while (!accepted && [NSDate timeIntervalSinceReferenceDate]-start < 30) pump();
  printf("Large retry: accepted=%d saves=%d submissions=%d wait=%.3f seconds\n",accepted,saves,submissions,[NSDate timeIntervalSinceReferenceDate]-start); fflush(stdout);
  assert(accepted && saves == 3 && submissions == 2);
  assert([[NSUserDefaults standardUserDefaults] objectForKey:pendingKey] == nil);
  testCredentialCache(observer);
  [[NSNotificationCenter defaultCenter] removeObserver:observer];
  [[NSUserDefaults standardUserDefaults] synchronize];
  puts("PASS: shared drafts, field merge, invalidation, responsive Send, failed save blocks submission, durable retry preserves 1 MiB Unicode payload; oversized prompt rejected");
  [failure release];
  [pool release];
  return 0;
}
