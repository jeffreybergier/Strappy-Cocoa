/* Offline native/host integration: real SQLite, IPC peers and badge transport.
 * All files are fixtures under the fresh directory supplied by the caller. */
#include "../strappy_attention.h"
#include "../strappy_notifications.h"
#include "../../shared/strappy_db.h"
#include "../../shared/strappy_run.h"
#include <sqlite3.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

static char dbpath[1024], endpoint[104], service[104];
static strappy_ipc_server *server;
static pthread_mutex_t stop_mutex=PTHREAD_MUTEX_INITIALIZER;
static int stopping, receiver;
static unsigned int alerts, badge_packets;
static long long badge_count=-1, badge_revision;
static void delay_ms(unsigned int ms)
{
  struct timespec t; t.tv_sec=(time_t)(ms/1000); t.tv_nsec=(long)(ms%1000)*1000000;
  nanosleep(&t,NULL);
}
static void sql(const char *text)
{
  sqlite3 *db=NULL;
  assert(sqlite3_open(dbpath,&db)==SQLITE_OK); sqlite3_busy_timeout(db,5000);
  assert(sqlite3_exec(db,"PRAGMA foreign_keys=ON",NULL,NULL,NULL)==SQLITE_OK);
  assert(sqlite3_exec(db,text,NULL,NULL,NULL)==SQLITE_OK); sqlite3_close(db);
}
static long long scalar(const char *text)
{
  sqlite3 *db=NULL; sqlite3_stmt *q=NULL; long long result;
  assert(sqlite3_open(dbpath,&db)==SQLITE_OK); sqlite3_busy_timeout(db,5000);
  assert(sqlite3_prepare_v2(db,text,-1,&q,NULL)==SQLITE_OK);
  assert(sqlite3_step(q)==SQLITE_ROW); result=sqlite3_column_int64(q,0);
  sqlite3_finalize(q); sqlite3_close(db); return result;
}
static long long count(void)
{ return scalar("SELECT COUNT(*) FROM session_attention WHERE has_new_answers=1"); }
static void handler(const strappy_ipc_message *r,strappy_ipc_message *reply,void *unused)
{
  (void)unused;
  reply->status=r->operation==STRAPPY_IPC_OBSERVE_SESSION ?
    strappy_attention_observe(r) : STRAPPY_IPC_UNKNOWN_OPERATION;
}
static void *loop(void *unused)
{
  (void)unused;
  for (;;) {
    int stop;
    pthread_mutex_lock(&stop_mutex); stop=stopping; pthread_mutex_unlock(&stop_mutex);
    if (stop) break;
    assert(!strappy_ipc_server_step(server,-1,100));
  }
  return NULL;
}
static int connect_client(void)
{
  strappy_ipc_message m; int fd=strappy_ipc_connect(service,geteuid(),1000);
  assert(fd>=0); memset(&m,0,sizeof(m)); m.kind=STRAPPY_IPC_REQUEST;
  m.operation=STRAPPY_IPC_HELLO; m.request_id=1; m.length=8;
  strappy_ipc_put32(m.payload,STRAPPY_IPC_VERSION); strappy_ipc_put32(m.payload+4,STRAPPY_IPC_VERSION);
  assert(!strappy_ipc_send(fd,&m,1000)); assert(!strappy_ipc_receive(fd,&m,1000));
  assert(m.status==STRAPPY_IPC_OK); return fd;
}
static void observe(int fd,unsigned int sequence,long long session,unsigned int token,unsigned int mode)
{
  strappy_ipc_message m; memset(&m,0,sizeof(m));
  m.kind=STRAPPY_IPC_REQUEST; m.operation=STRAPPY_IPC_OBSERVE_SESSION;
  m.request_id=sequence; m.length=20;
  strappy_ipc_put32(m.payload,(uint32_t)((uint64_t)session>>32));
  strappy_ipc_put32(m.payload+4,(uint32_t)session);
  strappy_ipc_put32(m.payload+12,token); strappy_ipc_put32(m.payload+16,mode);
  assert(!strappy_ipc_send(fd,&m,1000)); assert(!strappy_ipc_receive(fd,&m,2000));
  assert(m.status==STRAPPY_IPC_OK);
}
static void finish(long long session,int state)
{
  static unsigned int run=0;
  char text[512]; ++run;
  snprintf(text,sizeof(text),"INSERT INTO prompt_runs(id,session_id,request_key,prompt,state,created_at_ms,updated_at_ms) VALUES(%u,%lld,'run-%u','fixture',2,1,1)",run,session,run);
  sql(text);
  assert(strappy_attention_finish(run,session,state,0,"Fixture","Answer",NULL));
  /* An already committed terminal run must not publish again or change unread. */
  assert(strappy_attention_finish(run,session,state,0,"Fixture","Answer",NULL));
}
static void pump(unsigned int milliseconds,int acknowledge)
{
  unsigned int elapsed;
  for (elapsed=0;elapsed<milliseconds;elapsed+=20) {
    unsigned char bytes[4096]; struct sockaddr_un from; socklen_t size=sizeof(from);
    ssize_t n; memset(&from,0,sizeof(from));
    while ((n=recvfrom(receiver,bytes,sizeof(bytes),0,(struct sockaddr *)&from,&size))>0) {
      if (n==sizeof(strappy_badge_packet) && !memcmp(bytes,"STB1",4)) {
        strappy_badge_packet p; memcpy(&p,bytes,sizeof(p)); ++badge_packets;
        badge_revision=strtoll(p.revision,NULL,10); badge_count=strtoll(p.count,NULL,10);
        if (acknowledge) {
          memcpy(p.magic,"STA1",4);
          assert(sendto(receiver,&p,sizeof(p),0,(struct sockaddr *)&from,size)==sizeof(p));
        }
      } else { assert(n==sizeof(strappy_notification_packet) && !memcmp(bytes,"STN1",4)); ++alerts; }
      memset(&from,0,sizeof(from)); size=sizeof(from);
    }
    delay_ms(20);
  }
}
static void badge(long long expected)
{
  unsigned int i;
  for(i=0;i<150;++i) {
    pump(20,1);
    if (!scalar("SELECT pending FROM notification_badge") && badge_count==expected) return;
  }
  assert(0 && "badge did not converge");
}
int main(int argc,char **argv)
{
  pthread_t thread; struct sockaddr_un address; int fd; long long revision;
  unsigned int before;
  assert(argc==2 && argv[1][0]=='/'); assert(mkdir(argv[1],0700)==0);
  assert(snprintf(dbpath,sizeof(dbpath),"%s/strappy.sqlite",argv[1])<(int)sizeof(dbpath));
  assert(snprintf(endpoint,sizeof(endpoint),"%s/notifications.sock",argv[1])<(int)sizeof(endpoint));
  assert(snprintf(service,sizeof(service),"%s/service.sock",argv[1])<(int)sizeof(service));
  assert(!setenv("STRAPPY_TEST_NOTIFICATION_SOCKET",endpoint,1));
  assert(strappy_db_initialize(dbpath,NULL));
  sql("INSERT INTO sessions(id,name,created_at_ms,updated_at_ms) VALUES(1,'A',1,1),(2,'B',1,1)");
  assert(scalar("PRAGMA user_version")==1 && count()==0);
  receiver=socket(AF_UNIX,SOCK_DGRAM,0); assert(receiver>=0);
  memset(&address,0,sizeof(address)); address.sun_family=AF_UNIX; strcpy(address.sun_path,endpoint);
  assert(!bind(receiver,(struct sockaddr *)&address,sizeof(address)));
  assert(!fcntl(receiver,F_SETFL,O_NONBLOCK));
  server=strappy_ipc_server_open(argv[1]); assert(server);
  assert(strappy_attention_start(dbpath,server));
  strappy_ipc_server_handler(server,handler,NULL); strappy_ipc_server_store_ready(server);
  assert(!pthread_create(&thread,NULL,loop,NULL));
  finish(1,STRAPPY_RUN_COMPLETED); assert(count()==1); badge(1); assert(alerts==1);
  fd=connect_client(); observe(fd,2,1,1,1); assert(count()==0); badge(0);
  revision=badge_revision; observe(fd,3,1,2,1); badge(0); assert(badge_revision>revision);
  finish(1,STRAPPY_RUN_COMPLETED); assert(count()==0); badge(0); assert(alerts==1);
  finish(2,STRAPPY_RUN_FAILED); assert(count()==1); badge(1); assert(alerts==2);
  finish(2,STRAPPY_RUN_COMPLETED); assert(count()==1); badge(1); assert(alerts==3);
  observe(fd,4,2,3,1); badge(0);
  observe(fd,5,1,2,0); /* Stale close cannot close the newly opened session. */
  finish(2,STRAPPY_RUN_COMPLETED); badge(0); assert(alerts==3);
  observe(fd,6,2,3,0); finish(2,STRAPPY_RUN_COMPLETED); badge(1); assert(alerts==4);
  observe(fd,7,1,4,1); badge(1); close(fd); delay_ms(200);
  finish(1,STRAPPY_RUN_COMPLETED); badge(2); assert(alerts==5);
  fd=connect_client(); observe(fd,2,1,1,1); badge(1);
  delay_ms(15100); finish(1,STRAPPY_RUN_COMPLETED); badge(2); assert(alerts==6);
  observe(fd,3,1,1,2); badge(1); /* Expired observation reacquires and acknowledges. */
  observe(fd,4,1,1,0);
  finish(1,STRAPPY_RUN_CANCELLED); finish(1,STRAPPY_RUN_INTERRUPTED);
  pump(200,1); assert(count()==1 && alerts==6);
  strappy_attention_lock(); sql("DELETE FROM sessions WHERE id=2");
  strappy_attention_deleted(2); strappy_attention_unlock(); badge(0);
  before=badge_packets; observe(fd,5,1,2,1); pump(1800,0);
  assert(badge_packets>before && scalar("SELECT pending FROM notification_badge")==1);
  badge(0); before=badge_packets; pump(2200,1); assert(badge_packets==before);
  /* A missing receiver preserves a durable pending badge across daemon restart. */
  close(receiver); unlink(endpoint); observe(fd,6,1,3,1); delay_ms(200);
  assert(scalar("SELECT pending FROM notification_badge")==1);
  close(fd); pthread_mutex_lock(&stop_mutex); stopping=1; pthread_mutex_unlock(&stop_mutex);
  pthread_join(thread,NULL); strappy_ipc_server_drain(server); strappy_attention_stop(); strappy_ipc_server_close(server);
  receiver=socket(AF_UNIX,SOCK_DGRAM,0); assert(receiver>=0);
  assert(!bind(receiver,(struct sockaddr *)&address,sizeof(address))); assert(!fcntl(receiver,F_SETFL,O_NONBLOCK));
  server=strappy_ipc_server_open(argv[1]); assert(server); assert(strappy_attention_start(dbpath,server));
  badge(0); assert(count()==0); strappy_attention_stop(); strappy_ipc_server_close(server);
  close(receiver); unlink(endpoint); strappy_db_shutdown_connection();
  puts("PASS: unread, repeated-open snapshots, per-session suppression, stale close, disconnect, lease expiry, deletion, retry, ACK, restart, idle channel");
  return 0;
}
