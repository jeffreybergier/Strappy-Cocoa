/* Offline native reader/render and production IPC monitor probe. Synthetic DB
 * and a private fixture socket only; no account or provider access. */
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import "StrappySession.h"
#include "strappy_session.h"
#include "strappy_db.h"
#include "strappy_store_client.h"
#include "strappy_ipc.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
static NSString *fixtureDatabase;
/* Route the real bridge to this probe's synthetic store. */
@interface StrappySession (IncrementalFixture)
+ (NSString *)sessionsDatabasePath;
@end
@interface StrappySession (IncrementalFixturePrivate)
- (void)serviceRunChanged:(NSNotification *)notification;
@end
@implementation StrappySession (IncrementalFixture)
+ (NSString *)sessionsDatabasePath { return fixtureDatabase; }
@end
@interface IncrementalSessionProbe : StrappySession { @public int statusReads; }
@end
@implementation IncrementalSessionProbe
- (BOOL)isPromptInFlight { ++statusReads; return [super isPromptInFlight]; }
@end
static strappy_ipc_server *server;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int stopped, notifications;
static long long lastSession;
static void changed(unsigned int topics, long long session)
{
  (void)topics;
  pthread_mutex_lock(&lock); ++notifications; lastSession=session; pthread_mutex_unlock(&lock);
}
static void command(const strappy_ipc_message *request, strappy_ipc_message *reply, void *unused)
{
  (void)unused;
  if (request->operation == STRAPPY_IPC_WORK_STATUS) { reply->length=12; memset(reply->payload,0,12); }
}
static void *serve(void *unused)
{
  int done;
  (void)unused;
  for (;;) {
    pthread_mutex_lock(&lock); done=stopped; pthread_mutex_unlock(&lock);
    if (done) return NULL;
    assert(strappy_ipc_server_step(server,-1,20)==0);
  }
}
static void awaitNotification(int count, long long session)
{
  int i, found=0;
  for (i=0;i<500;++i) {
    pthread_mutex_lock(&lock); found=notifications>=count && lastSession==session; pthread_mutex_unlock(&lock);
    if (found) break;
    usleep(10000);
  }
  assert(found);
}
static double milliseconds(void)
{
  struct timeval now; gettimeofday(&now,NULL);
  return (double)now.tv_sec*1000.0+(double)now.tv_usec/1000.0;
}
@interface IncrementalWebProbe : NSObject { @public BOOL loaded; int ready; }
@end
@implementation IncrementalWebProbe
- (void)webView:(WebView *)view decidePolicyForNavigationAction:(NSDictionary *)action
    request:(NSURLRequest *)request frame:(WebFrame *)frame decisionListener:(id)listener
{
  (void)view; (void)action; (void)frame;
  if ([[[request URL] scheme] isEqualToString:@"strappy-action"]) { ++ready; [listener ignore]; }
  else [listener use];
}
- (void)webView:(WebView *)view didFinishLoadForFrame:(WebFrame *)frame
{ if (frame==[view mainFrame]) loaded=YES; }
@end
int main(int argc, char **argv)
{
  NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
  char socketPath[1024], *error=NULL, *full, *incremental, *page;
  pthread_t thread;
  strappy_session_message_record_list all, tail;
  uint64_t revision, next;
  long long requests[STRAPPY_STORE_MAX_CHANGES], request;
  double start, fullTime, incrementalTime;
  size_t i;
  assert(argc==5); /* private socket directory, database, resources, JS test */
  fixtureDatabase=[NSString stringWithUTF8String:argv[2]];
  server=strappy_ipc_server_open(argv[1]); assert(server);
  strappy_ipc_server_store_ready(server); strappy_ipc_server_handler(server,command,NULL);
  assert(pthread_create(&thread,NULL,serve,NULL)==0);
  snprintf(socketPath,sizeof(socketPath),"%s/service.sock",argv[1]);
  assert(strappy_store_client_start(socketPath,argv[2],changed));
  awaitNotification(1,0);
  assert(strappy_store_client_changes(1,0,&revision,requests)==-1);
  strappy_session_message_record_list_init(&all); strappy_session_message_record_list_init(&tail);
  assert(strappy_db_list_response_timeline(argv[2],1,&all,&error));
  assert(all.count==600);
  request=all.records[all.count-1].model_request_id;
  assert(strappy_db_list_response_timeline_from_request(argv[2],1,request,&tail,&error));
  assert(tail.count==2);
  for(i=0;i<tail.count;++i) {
    assert(!strcmp(tail.records[i].message_key,all.records[all.count-2+i].message_key));
    assert(tail.records[i].cumulative_wait_ms==all.records[all.count-2+i].cumulative_wait_ms);
    assert(tail.records[i].cumulative_usage_cost==all.records[all.count-2+i].cumulative_usage_cost);
  }
  strappy_session_message_record_list_destroy(&all); strappy_session_message_record_list_destroy(&tail);
  assert(strappy_ipc_server_committed_scope(server,STRAPPY_IPC_LEDGER_CHANGED,1,request)==0);
  awaitNotification(2,1);
  assert(strappy_store_client_changes(1,revision,&next,requests)==1 && requests[0]==request);
  assert(strappy_store_client_changes(2,revision,&next,requests)==0);
  {
    IncrementalSessionProbe *one=[[IncrementalSessionProbe alloc]
      initWithSessionIdentifier:[NSNumber numberWithInt:1] summary:nil];
    IncrementalSessionProbe *two=[[IncrementalSessionProbe alloc]
      initWithSessionIdentifier:[NSNumber numberWithInt:2] summary:nil];
    NSDictionary *info=[NSDictionary dictionaryWithObjectsAndKeys:
      [NSNumber numberWithUnsignedInt:STRAPPY_STORE_LEDGER], @"topics",
      [NSNumber numberWithInt:0], @"globalTopics",
      [NSDictionary dictionaryWithObject:[NSNumber numberWithUnsignedInt:STRAPPY_STORE_LEDGER]
        forKey:[NSNumber numberWithLongLong:1]], @"scopes", nil];
    NSNotification *notification=[NSNotification notificationWithName:@"StrappyServiceChanged" object:nil userInfo:info];
    int oneBefore=one->statusReads, twoBefore=two->statusReads;
    [one serviceRunChanged:notification]; [two serviceRunChanged:notification];
    assert(one->statusReads>oneBefore && two->statusReads==twoBefore);
    NSDictionary *update=[two webViewRefreshMessagesAfterRevision:
      [NSNumber numberWithUnsignedLongLong:revision] error:NULL];
    assert(update && [[update objectForKey:@"javaScript"] rangeOfString:@"syncMessages("].location==NSNotFound);
    assert([[update objectForKey:@"revision"] unsignedLongLongValue]==next);
    [one release]; [two release];
  }
  revision=next;
  start=milliseconds();
  full=strappy_session_webview_refresh_messages_js_for_session(argv[2],1,argv[3],&error); assert(full);
  fullTime=milliseconds()-start;
  start=milliseconds();
  incremental=strappy_session_webview_refresh_from_request_js_for_session(argv[2],1,argv[3],request,&error); assert(incremental);
  incrementalTime=milliseconds()-start;
  assert(strlen(incremental)<strlen(full)/100);
  printf("600 rows/session, 2 sessions: full %.1f ms / %lu bytes; incremental %.1f ms / %lu bytes; unrelated session 0 requests\n",
    fullTime,(unsigned long)strlen(full),incrementalTime,(unsigned long)strlen(incremental)); fflush(stdout);
  free(full); free(incremental);
  assert(strappy_ipc_server_committed(server,STRAPPY_IPC_LEDGER_CHANGED)==0);
  awaitNotification(3,0);
  assert(strappy_store_client_changes(1,revision,&next,requests)==-1);
  revision=next;
  pthread_mutex_lock(&lock); stopped=1; pthread_mutex_unlock(&lock);
  pthread_join(thread,NULL); strappy_ipc_server_close(server);
  awaitNotification(4,0);
  assert(strappy_store_client_changes(1,revision,&next,requests)==-1);
  revision=next;
  server=strappy_ipc_server_open(argv[1]); assert(server);
  strappy_ipc_server_store_ready(server); strappy_ipc_server_handler(server,command,NULL);
  pthread_mutex_lock(&lock); stopped=0; pthread_mutex_unlock(&lock);
  assert(pthread_create(&thread,NULL,serve,NULL)==0);
  awaitNotification(5,0);
  assert(strappy_store_client_changes(2,revision,&next,requests)==-1);
  page=strappy_session_webview_messages_page_html_for_session(argv[2],1,argv[3],NULL,NULL,
    STRAPPY_WEBVIEW_PALETTE_APPLICATION_TINTED,NULL,NULL,&error); assert(page);
  [NSApplication sharedApplication];
  WebView *view=[[WebView alloc] initWithFrame:NSMakeRect(0,0,800,600)];
  IncrementalWebProbe *probe=[[IncrementalWebProbe alloc] init];
  [view setFrameLoadDelegate:probe]; [view setPolicyDelegate:probe];
  [[view mainFrame] loadHTMLString:[NSString stringWithUTF8String:page] baseURL:nil]; free(page);
  NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:60];
  while((!probe->loaded || !probe->ready) && [deadline timeIntervalSinceNow]>0)
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
  assert(probe->loaded && probe->ready);
  NSString *script=[NSString stringWithContentsOfFile:[NSString stringWithUTF8String:argv[4]] encoding:NSUTF8StringEncoding error:NULL];
  NSString *result=[view stringByEvaluatingJavaScriptFromString:script];
  printf("Native WebView: %s\n",[result UTF8String]); assert([result isEqualToString:@"PASS"]);
  [view setFrameLoadDelegate:nil]; [view setPolicyDelegate:nil]; [view release]; [probe release];
  pthread_mutex_lock(&lock); stopped=1; pthread_mutex_unlock(&lock);
  pthread_join(thread,NULL); strappy_ipc_server_close(server);
  puts("incremental native: scoped IPC, independent revisions, recovery, bounded render, cumulative totals and DOM passed");
  [pool drain]; return 0;
}
