/* Native production bridge/daemon regression against a fresh fixture only.
 * No credentials, provider requests, installed services, or user defaults.
 * Delay the acceptance return until the real daemon job has finished. Suppress
 * monitor hints entirely so the keyed waiter must deliver completion. */
#import "StrappySession.h"
#import "FileScanner.h"
#import "StrappyAsync.h"
#import "XPFoundation.h"
#include "strappy_store_client.h"
#include "strappy_ipc.h"
#include "strappy_run.h"
#include <sqlite3.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *fixtureSocket;
static char previousKey[65];
static unsigned int expectedState;
static int starts, finishes, errors;

@interface MaintenanceSession : StrappySession
@end
@implementation MaintenanceSession
+ (NSArray *)providerAccountCatalogWithError:(NSError **)error;
{
  (void)error;
  return [NSArray arrayWithObject:[NSDictionary dictionaryWithObjectsAndKeys:
    @"openrouter", @"provider_id", @"missing-fixture-account", @"id",
    [NSNumber numberWithBool:YES], @"available", nil]];
}
@end

static unsigned int outcome(const char *key)
{
  int fd = strappy_ipc_connect(fixtureSocket,geteuid(),3000);
  strappy_ipc_message message;
  assert(fd >= 0);
  memset(&message,0,sizeof(message));
  message.kind = STRAPPY_IPC_REQUEST; message.operation = STRAPPY_IPC_HELLO;
  message.request_id = 1; message.length = 8;
  strappy_ipc_put32(message.payload,STRAPPY_IPC_VERSION);
  strappy_ipc_put32(message.payload+4,STRAPPY_IPC_VERSION);
  assert(!strappy_ipc_send(fd,&message,3000));
  assert(!strappy_ipc_receive(fd,&message,3000) && !message.status);
  memset(&message,0,sizeof(message));
  message.kind = STRAPPY_IPC_REQUEST; message.operation = STRAPPY_IPC_WORK_STATUS;
  message.request_id = 2; message.length = 64; memcpy(message.payload,key,64);
  assert(!strappy_ipc_send(fd,&message,3000));
  assert(!strappy_ipc_receive(fd,&message,3000) && !message.status && message.length == 4);
  close(fd);
  return strappy_ipc_get32(message.payload);
}

char *strappy_test_mutation(unsigned int operation, const cJSON *command,
                            unsigned int *status)
{
  const cJSON *key = cJSON_GetObjectItemCaseSensitive(command,"key");
  unsigned int state = 0;
  int attempt;
  char *reply;
  assert([NSThread XP_isMainThread]);
  assert(operation == STRAPPY_IPC_START_SCAN || operation == STRAPPY_IPC_REFRESH_CATALOG);
  assert(cJSON_IsString(key) && strlen(key->valuestring) == 64);
  assert(strcmp(previousKey,key->valuestring));
  memcpy(previousKey,key->valuestring,65);
  reply = strappy_store_client_mutation(operation,command,status);
  assert(reply && *status == STRAPPY_IPC_OK);
  for (attempt = 0; attempt < 200; ++attempt) {
    state = outcome(previousKey);
    if (state != STRAPPY_RUN_RUNNING) break;
    usleep(100000);
  }
  assert(state == expectedState);
  return reply;
}

static void changed(unsigned int topics, long long session)
{
  /* Intentionally never forward a running/finished hint to either bridge. */
  (void)topics; (void)session;
}

@interface MaintenanceObserver : NSObject
@end
@implementation MaintenanceObserver
- (void)started:(NSNotification *)notification;
{
  (void)notification; assert([NSThread XP_isMainThread]); ++starts;
}
- (void)finished:(NSNotification *)notification;
{
  assert([NSThread XP_isMainThread]); ++finishes;
  assert(([[notification userInfo] objectForKey:@"error"] != nil) ==
    (expectedState != STRAPPY_RUN_COMPLETED));
}
- (void)failed:(NSNotification *)notification;
{
  (void)notification; assert([NSThread XP_isMainThread]); ++errors;
}
@end

static void drain(double seconds)
{
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
}
static void awaitCompletion(int count, int failures)
{
  NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:15];
  while (finishes < count && [deadline timeIntervalSinceNow] > 0) drain(0.02);
  drain(0.4); /* Repeated polling/queued delivery must not duplicate the result. */
  assert(starts == count && finishes == count && errors == failures);
  assert(![StrappySession isModelCatalogRefreshInFlight]);
  assert(![FileScanner isDatabaseCatalogScanInFlight]);
}

int main(int argc, char **argv)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  sqlite3 *db = NULL;
  NSError *error = nil;
  MaintenanceObserver *observer = [[MaintenanceObserver alloc] init];
  NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
  assert(argc == 4);
  fixtureSocket = argv[1];
  assert(sqlite3_open(argv[2],&db) == SQLITE_OK);
  sqlite3_busy_timeout(db,3000);
  assert(strappy_store_client_start(argv[1],argv[2],changed));
  [center addObserver:observer selector:@selector(started:) name:StrappySessionModelCatalogRefreshDidStartNotification object:nil];
  [center addObserver:observer selector:@selector(started:) name:FileScannerDatabaseCatalogScanDidStartNotification object:nil];
  [center addObserver:observer selector:@selector(finished:) name:StrappySessionModelCatalogRefreshDidFinishNotification object:nil];
  [center addObserver:observer selector:@selector(finished:) name:FileScannerDatabaseCatalogScanDidFinishNotification object:nil];
  [center addObserver:observer selector:@selector(failed:) name:StrappyAsyncCommandFailedNotification object:nil];
  expectedState = STRAPPY_RUN_FAILED;
  assert([MaintenanceSession beginOpenRouterModelCatalogRefreshWithError:&error]);
  assert(![MaintenanceSession beginOpenRouterModelCatalogRefreshWithError:&error]);
  awaitCompletion(1,1);
  assert([MaintenanceSession beginOpenRouterModelCatalogRefreshWithError:&error]);
  awaitCompletion(2,2);
  puts("PASS: two immediate catalog failures, distinct keys, one main-thread error per job");
  expectedState = STRAPPY_RUN_COMPLETED;
  assert([FileScanner beginDatabaseCatalogScanAtPath:[NSString stringWithUTF8String:argv[3]] error:&error]);
  assert(![FileScanner beginDatabaseCatalogScanAtPath:[NSString stringWithUTF8String:argv[3]] error:&error]);
  awaitCompletion(3,2);
  puts("PASS: immediate scan success without a running hint or error");
  assert(sqlite3_exec(db,"CREATE TRIGGER fail_scan BEFORE INSERT ON scan_runs "
    "BEGIN SELECT RAISE(ABORT,'fixture scan failure'); END",NULL,NULL,NULL) == SQLITE_OK);
  expectedState = STRAPPY_RUN_FAILED;
  assert([FileScanner beginDatabaseCatalogScanAtPath:[NSString stringWithUTF8String:argv[3]] error:&error]);
  awaitCompletion(4,3);
  assert(sqlite3_exec(db,"DROP TRIGGER fail_scan",NULL,NULL,NULL) == SQLITE_OK);
  assert(sqlite3_close(db) == SQLITE_OK);
  [center removeObserver:observer]; [observer release];
  printf("PASS: immediate scan failure; starts=%d finishes=%d errors=%d uid=%ld\n",
    starts,finishes,errors,(long)geteuid());
  [pool drain];
  return 0;
}
