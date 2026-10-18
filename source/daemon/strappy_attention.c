#include "strappy_attention.h"
#include "strappy_notifications.h"
#include "../shared/strappy_db_internal.h"
#include "../shared/strappy_run.h"
#include "../shared/XPFoundation/strappy_ipc_platform.h"
#include "../shared/XPFoundation/strappy_notification_platform.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static unsigned long generation;
static int started, stopping;
static char *database;
static strappy_ipc_server *server;
static struct { uint64_t peer, token; long long session, deadline; }
  observers[STRAPPY_IPC_MAX_CLIENTS];

void strappy_attention_lock(void) { pthread_mutex_lock(&mutex); }
void strappy_attention_unlock(void) { pthread_mutex_unlock(&mutex); }

static void wake_badge(void)
{
  ++generation; pthread_cond_signal(&changed);
}
void strappy_attention_deleted(long long session)
{
  unsigned int i;
  for (i=0;i<STRAPPY_IPC_MAX_CLIENTS;++i)
    if (observers[i].session==session) memset(&observers[i],0,sizeof(observers[i]));
  wake_badge();
}
static int active(unsigned int i, long long now)
{
  if (observers[i].peer && (now < 0 || observers[i].deadline <= now ||
      !strappy_ipc_server_peer_alive(server, observers[i].peer)))
    memset(&observers[i], 0, sizeof(observers[i]));
  return observers[i].peer != 0;
}
static int observed(long long session)
{
  unsigned int i;
  long long now = strappy_ipc_platform_milliseconds();
  for (i=0;i<STRAPPY_IPC_MAX_CLIENTS;++i)
    if (active(i,now) && observers[i].session==session) return 1;
  return 0;
}
static int unread(sqlite3 *db, long long session, int value)
{
  sqlite3_stmt *q=NULL;
  int ok=sqlite3_prepare_v2(db,
    "INSERT OR REPLACE INTO session_attention(session_id,has_new_answers) "
    "SELECT id,? FROM sessions WHERE id=?",-1,&q,NULL)==SQLITE_OK;
  if (ok) {
    sqlite3_bind_int(q,1,value); sqlite3_bind_int64(q,2,session);
    ok=sqlite3_step(q)==SQLITE_DONE && sqlite3_changes(db)==1;
  }
  sqlite3_finalize(q); return ok;
}
unsigned int strappy_attention_observe(const strappy_ipc_message *r)
{
  const unsigned char *p=strappy_ipc_payload(r);
  uint64_t session, token;
  unsigned int mode, i, slot=STRAPPY_IPC_MAX_CLIENTS, status=STRAPPY_IPC_INVALID;
  sqlite3 *db=NULL;
  long long now=strappy_ipc_platform_milliseconds();
  if (r->length!=20 || !r->peer_id || now<0) return status;
  session=((uint64_t)strappy_ipc_get32(p)<<32)|strappy_ipc_get32(p+4);
  token=((uint64_t)strappy_ipc_get32(p+8)<<32)|strappy_ipc_get32(p+12);
  mode=strappy_ipc_get32(p+16);
  if (!session || session>INT64_MAX || !token || mode>2) return status;
  strappy_attention_lock();
  if (!database || !strappy_ipc_server_peer_alive(server,r->peer_id)) goto done;
  for (i=0;i<STRAPPY_IPC_MAX_CLIENTS;++i) {
    if (!active(i,now)) { if (slot==STRAPPY_IPC_MAX_CLIENTS) slot=i; }
    else if (observers[i].peer==r->peer_id) { slot=i; break; }
  }
  if (slot==STRAPPY_IPC_MAX_CLIENTS) { status=STRAPPY_IPC_BUSY; goto done; }
  status=STRAPPY_IPC_OK;
  if (!mode) {
    if (observers[slot].peer==r->peer_id && observers[slot].token==token &&
        observers[slot].session==(long long)session)
      memset(&observers[slot],0,sizeof(observers[slot]));
    goto done;
  }
  if (observers[slot].peer==r->peer_id && observers[slot].token>token) goto done;
  /* Every explicit open acknowledges and publishes an absolute badge snapshot,
   * even when already read. Renewals only acknowledge when reacquiring a lease. */
  if (mode==1 || observers[slot].peer!=r->peer_id ||
      observers[slot].token!=token || observers[slot].session!=(long long)session) {
    status=STRAPPY_IPC_STORAGE_ERROR;
    if (!strappy_db_open(database,&db,NULL) ||
        sqlite3_exec(db,"BEGIN IMMEDIATE",NULL,NULL,NULL)!=SQLITE_OK) goto done;
    if (!unread(db,(long long)session,0) ||
        sqlite3_exec(db,"COMMIT",NULL,NULL,NULL)!=SQLITE_OK) {
      sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL); goto done;
    }
    status=STRAPPY_IPC_OK;
    wake_badge();
    strappy_ipc_server_committed_scope(server,STRAPPY_IPC_SESSION_CHANGED,(long long)session,0);
  }
  observers[slot].peer=r->peer_id; observers[slot].token=token;
  observers[slot].session=(long long)session; observers[slot].deadline=now+15000;
done:
  if (db) strappy_db_release(db);
  strappy_attention_unlock(); return status;
}

int strappy_attention_finish(long long run, long long session, int state,
  int cancel, const char *title, const char *response, const char *error)
{
  sqlite3 *db=NULL; sqlite3_stmt *q=NULL;
  int ok=0, notify=0, changed=0;
  strappy_attention_lock();
  if (!database || !strappy_db_open(database,&db,NULL) ||
      sqlite3_exec(db,"BEGIN IMMEDIATE",NULL,NULL,NULL)!=SQLITE_OK) goto done;
  if (sqlite3_prepare_v2(db,"UPDATE prompt_runs SET state=?,cancel_requested=MAX(cancel_requested,?),"
      "status_kind='',status_reason='',status_started_ms=0,retry_until_ms=0,updated_at_ms=? "
      "WHERE id=? AND session_id=? AND state IN (1,2)",-1,&q,NULL)!=SQLITE_OK) goto done;
  sqlite3_bind_int(q,1,state); sqlite3_bind_int(q,2,cancel);
  sqlite3_bind_int64(q,3,strappy_db_now_ms()); sqlite3_bind_int64(q,4,run);
  sqlite3_bind_int64(q,5,session);
  if (sqlite3_step(q)!=SQLITE_DONE) goto done;
  changed=sqlite3_changes(db); sqlite3_finalize(q); q=NULL;
  if (changed && (state==STRAPPY_RUN_COMPLETED || state==STRAPPY_RUN_FAILED)) {
    notify=!observed(session);
    if (!unread(db,session,notify)) goto done;
  }
  ok=sqlite3_exec(db,"COMMIT",NULL,NULL,NULL)==SQLITE_OK;
  /* Nonblocking transport under the ordering lock: an open/delete that wins
   * this lock first suppresses the completion; later opens cannot retract it. */
  if (ok && changed) wake_badge();
  if (ok && changed && notify)
    strappy_notifications_finished(run,state,title,response,error);
done:
  if (q) sqlite3_finalize(q);
  if (db) {
    if (!ok) sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL);
    strappy_db_release(db);
  }
  strappy_attention_unlock(); return ok;
}

/* One latest-value badge channel. Acknowledgments only clear the exact durable
 * revision; a concurrent open/completion can never have its update swallowed.
 * No polling/resend after ACK, and no automatic refresh on SpringBoard restart. */
static void *badge_worker(void *unused)
{
  int fd=-1;
  struct sockaddr_un destination, reply;
  char reply_path[sizeof(reply.sun_path)];
  dev_t device=0; ino_t inode=0;
  const char *path=strappy_notification_platform_socket();
  (void)unused;
  memset(&destination,0,sizeof(destination)); destination.sun_family=AF_UNIX;
  memset(&reply,0,sizeof(reply)); reply.sun_family=AF_UNIX;
  reply_path[0]=0;
  if (path && strlen(path)<sizeof(destination.sun_path) &&
      snprintf(reply_path,sizeof(reply_path),"%s.ack",path)<(int)sizeof(reply_path)) {
    strcpy(destination.sun_path,path); strcpy(reply.sun_path,reply_path);
  }
  for (;;) {
    sqlite3 *db=NULL; sqlite3_stmt *q=NULL;
    strappy_badge_packet packet;
    long long revision=0, count=0;
    int pending=0, stop, retry=path != NULL;
    unsigned long snapshot;
    strappy_attention_lock(); stop=stopping; snapshot=generation;
    if (!stop && path && strappy_db_open(database,&db,NULL)) {
      if (sqlite3_prepare_v2(db,"SELECT revision,pending,"
          "(SELECT COUNT(*) FROM session_attention WHERE has_new_answers=1) "
          "FROM notification_badge WHERE id=1",-1,&q,NULL)==SQLITE_OK && sqlite3_step(q)==SQLITE_ROW) {
        revision=sqlite3_column_int64(q,0); pending=sqlite3_column_int(q,1);
        count=sqlite3_column_int64(q,2);
        retry=0;
      }
      sqlite3_finalize(q); q=NULL; strappy_db_release(db); db=NULL;
    }
    strappy_attention_unlock();
    if (stop) break;
    if (pending && destination.sun_path[0] && fd<0) {
      struct stat st;
      /* Parent is the provider's private mobile-owned directory. Refuse any
       * substituted directory, regular file, symlink or foreign-owned endpoint. */
      char directory[sizeof(reply_path)], *slash;
      strcpy(directory,reply_path); slash=strrchr(directory,'/'); if (slash) *slash=0;
      if (!lstat(directory,&st) && S_ISDIR(st.st_mode) && st.st_uid==geteuid() && !(st.st_mode&077)) {
        int safe=1;
        if (!lstat(reply_path,&st)) {
          safe=S_ISSOCK(st.st_mode) && st.st_uid==geteuid();
          if (safe) safe=unlink(reply_path)==0; /* Exclusive daemon lifetime. */
        } else safe=errno==ENOENT;
        if (safe) {
          fd=socket(AF_UNIX,SOCK_DGRAM,0);
          if (fd>=0 && (fcntl(fd,F_SETFL,O_NONBLOCK) || fcntl(fd,F_SETFD,FD_CLOEXEC) ||
              bind(fd,(struct sockaddr *)&reply,sizeof(reply)) || chmod(reply_path,0600))) {
            close(fd); fd=-1;
          }
          if (fd>=0 && !lstat(reply_path,&st)) { device=st.st_dev; inode=st.st_ino; }
        }
      }
    }
    memset(&packet,0,sizeof(packet)); memcpy(packet.magic,"STB1",4);
    snprintf(packet.revision,sizeof(packet.revision),"%lld",revision);
    snprintf(packet.count,sizeof(packet.count),"%lld",count);
    if (fd>=0) {
      strappy_badge_packet ack;
      struct sockaddr_un from; socklen_t size=sizeof(from);
      ssize_t n;
      memset(&from,0,sizeof(from));
      n=recvfrom(fd,&ack,sizeof(ack),0,(struct sockaddr *)&from,&size);
      if (pending && n==sizeof(ack) && !memcmp(ack.magic,"STA1",4) &&
          !memcmp(ack.revision,packet.revision,sizeof(packet.revision)) &&
          !memcmp(ack.count,packet.count,sizeof(packet.count)) &&
          !memcmp(from.sun_path,destination.sun_path,sizeof(from.sun_path))) {
        strappy_attention_lock();
        if (strappy_db_open(database,&db,NULL)) {
          if (sqlite3_prepare_v2(db,"UPDATE notification_badge SET pending=0 WHERE id=1 AND revision=?",-1,&q,NULL)==SQLITE_OK) {
            sqlite3_bind_int64(q,1,revision);
            if (sqlite3_step(q)==SQLITE_DONE) pending=0;
          }
          sqlite3_finalize(q); q=NULL; strappy_db_release(db); db=NULL;
        }
        strappy_attention_unlock();
      }
    }
    if (pending && fd>=0)
      (void)sendto(fd,&packet,sizeof(packet),0,(struct sockaddr *)&destination,sizeof(destination));
    strappy_attention_lock();
    if (!stopping && snapshot==generation) {
      if (pending || retry) {
        struct timespec until;
        until.tv_sec=time(NULL)+1; until.tv_nsec=0;
        pthread_cond_timedwait(&changed,&mutex,&until);
      } else pthread_cond_wait(&changed,&mutex);
    }
    strappy_attention_unlock();
  }
  if (fd>=0) close(fd);
  if (inode) {
    struct stat st;
    if (!lstat(reply_path,&st) && S_ISSOCK(st.st_mode) && st.st_uid==geteuid() &&
        st.st_dev==device && st.st_ino==inode) unlink(reply_path);
  }
  strappy_db_shutdown_connection();
  return NULL;
}
int strappy_attention_start(const char *path, strappy_ipc_server *s)
{
  if (started || !path || !s) return 0;
  database=strdup(path); if (!database) return 0;
  server=s; stopping=0; memset(observers,0,sizeof(observers));
  if (pthread_create(&worker,NULL,badge_worker,NULL)) {
    free(database); database=NULL; server=NULL; return 0;
  }
  started=1; return 1;
}
void strappy_attention_stop(void)
{
  if (!started) return;
  strappy_attention_lock(); stopping=1; wake_badge(); strappy_attention_unlock();
  pthread_join(worker,NULL); free(database); database=NULL; server=NULL; started=0;
}
