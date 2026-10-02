#define _POSIX_C_SOURCE 200809L
#include "strappy_db.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char *error;
static void require(int ok, const char *message)
{
  if (!ok) {
    fprintf(stderr, "FAIL: %s: %s\n", message, error != NULL ? error : "");
    exit(1);
  }
}
static void sql(sqlite3 *db, const char *statement)
{
  char *message = NULL;
  int rc = sqlite3_exec(db, statement, NULL, NULL, &message);
  if (rc != SQLITE_OK) fprintf(stderr, "%s\n", message != NULL ? message : "SQL error");
  sqlite3_free(message);
  require(rc == SQLITE_OK, statement);
}
static long long integer(sqlite3 *db, const char *query)
{
  sqlite3_stmt *stmt = NULL;
  long long value;
  require(sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK, query);
  require(sqlite3_step(stmt) == SQLITE_ROW, query);
  value = (long long)sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return value;
}
static void activity(sqlite3 *db, long long expected)
{
  require(integer(db, "SELECT last_activity_at_ms FROM session_activity WHERE session_id=1;") == expected,
          "activity follows last sequence, with creation fallback");
}
static double seconds(void)
{
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return (double)time.tv_sec + (double)time.tv_nsec / 1000000000.0;
}
static void remove_database(const char *path)
{
  char sidecar[1200];
  unlink(path);
  snprintf(sidecar, sizeof(sidecar), "%s-wal", path); unlink(sidecar);
  snprintf(sidecar, sizeof(sidecar), "%s-shm", path); unlink(sidecar);
}
int main(void)
{
  char directory[] = "/tmp/strappy-sidebar-XXXXXX";
  char path[1024];
  char other_path[1024];
  sqlite3 *db = NULL;
  strappy_session_reader *reader = NULL;
  strappy_session_reader *new_reader = NULL;
  strappy_sidebar_page page;
  size_t count;
  size_t index;
  size_t offset;
  unsigned long long before;
  unsigned long long max_steps = 0;
  long long previous = 0;
  long long session_id;
  double start;
  double lazy_time;
  double eager_time;
  strappy_session_record_list eager;
  sqlite3_stmt *insert = NULL;
  sqlite3_stmt *expected = NULL;

  require(mkdtemp(directory) != NULL, "temporary directory");
  snprintf(path, sizeof(path), "%s/sidebar.sqlite", directory);
  snprintf(other_path, sizeof(other_path), "%s/other.sqlite", directory);
  require(strappy_db_initialize(path, &error), "initialize");
  require(sqlite3_open(path, &db) == SQLITE_OK, "open fixture writer");
  sql(db, "PRAGMA foreign_keys=ON;"
    "INSERT INTO provider_accounts(id,provider_id,display_name,created_at_ms,updated_at_ms) "
      "VALUES('sidebar-test','openrouter','Sidebar test',1,1);"
    "INSERT INTO sessions(id,name,provider_account_id,created_at_ms,updated_at_ms) VALUES"
      "(1,'First','sidebar-test',100,100),(2,'Second','sidebar-test',200,200);"
    "INSERT INTO turns(id,session_id,ordinal,prompt_group_key,state,created_at_ms) VALUES"
      "(1,1,0,'one','completed',100),(2,2,0,'two','completed',200);");
  activity(db,100);
  sql(db,"INSERT INTO conversation_items(id,session_id,turn_id,sequence,kind,created_at_ms) "
         "VALUES(1,1,1,1,'message',300),(2,1,1,2,'message',150);");
  activity(db,150);
  sql(db,"UPDATE conversation_items SET created_at_ms=400 WHERE id=1;"); activity(db,150);
  sql(db,"UPDATE conversation_items SET created_at_ms=250 WHERE id=2;"); activity(db,250);
  sql(db,"UPDATE conversation_items SET sequence=3 WHERE id=1;"); activity(db,400);
  sql(db,"BEGIN; DELETE FROM conversation_items WHERE id=1;"); activity(db,250);
  sql(db,"ROLLBACK;"); activity(db,400);
  sql(db,"DELETE FROM conversation_items WHERE id=1;"); activity(db,250);
  sql(db,"UPDATE sessions SET name='Renamed',updated_at_ms=9999 WHERE id=1;"); activity(db,250);
  sql(db,"UPDATE conversation_items SET session_id=2,turn_id=2 WHERE id=2;"); activity(db,100);
  require(integer(db,"SELECT last_activity_at_ms FROM session_activity WHERE session_id=2;")==250,"moving item updates both sessions");
  sql(db,"DELETE FROM turns WHERE id=2;");
  require(integer(db,"SELECT last_activity_at_ms FROM session_activity WHERE session_id=2;")==200,"cascading item deletion restores creation");
  sql(db,"UPDATE sessions SET created_at_ms=110 WHERE id=1;"); activity(db,110);
  require(strappy_db_sidebar_open(path,&reader,&error),"open read snapshot");
  require(strappy_db_sidebar_count(reader)==2,"snapshot count");
  sql(db,"UPDATE sessions SET name='Changed',created_at_ms=500 WHERE id=1;");
  require(strappy_db_sidebar_read(reader,0,&page,&error),"read old snapshot after write");
  require(page.count==2 && page.records[0].session_id==2 &&
    strcmp(page.records[1].name,"Renamed")==0,"snapshot preserves order and values");
  strappy_db_sidebar_page_destroy(&page);
  require(strappy_db_sidebar_count_since(reader,200,&count,&error) && count==1,"snapshot counts remain consistent");
  require(strappy_db_sidebar_open(path,&new_reader,&error),"refresh snapshot");
  require(strappy_db_sidebar_read(new_reader,0,&page,&error) && page.records[0].session_id==1 &&
    strcmp(page.records[0].name,"Changed")==0,"new snapshot sees new order and title");
  strappy_db_sidebar_page_destroy(&page);
  strappy_db_sidebar_close(reader); strappy_db_sidebar_close(new_reader);
  reader=NULL; new_reader=NULL;
  sql(db,"DELETE FROM sessions WHERE id=2;");
  require(integer(db,"SELECT count(*) FROM session_activity WHERE session_id=2;")==0,"session deletion removes index entry");

  /* Simulate a pre-feature database with an existing ledger. */
  sql(db,"INSERT INTO conversation_items(id,session_id,turn_id,sequence,kind,created_at_ms) "
         "VALUES(3,1,1,1,'message',700),(4,1,1,2,'message',600);"
         "DROP TRIGGER session_activity_insert; DROP TRIGGER session_activity_created;"
         "DROP TRIGGER session_activity_item_insert; DROP TRIGGER session_activity_item_delete;"
         "DROP TRIGGER session_activity_item_update; DROP TABLE session_activity;");
  require(strappy_db_initialize(other_path,&error) && strappy_db_initialize(path,&error),"initialize existing history");
  activity(db,600);
  require(integer(db,"PRAGMA user_version;")==1,"schema version stays pinned");
  sql(db,"DELETE FROM turns WHERE id=1;"); activity(db,500);
  sql(db,"DELETE FROM sessions;");
  require(strappy_db_sidebar_open(path,&reader,&error) && strappy_db_sidebar_count(reader)==0,"empty snapshot");
  require(strappy_db_sidebar_read(reader,0,&page,&error) && page.count==0,"empty page");
  require(strappy_db_sidebar_index(reader,1,&index,&error) && index==(size_t)-1,"absent identity");
  strappy_db_sidebar_close(reader); reader=NULL;

  /* Distinct rows, large timestamp ties, multiple date buckets and real item
   * history exercise the index and the old eager-loader comparison. */
  sql(db,"BEGIN;");
  require(sqlite3_prepare_v2(db,"INSERT INTO sessions(id,name,provider_account_id,created_at_ms,updated_at_ms) "
    "VALUES(?1,'Benchmark session '||?1,'sidebar-test',?2,?2);",-1,&insert,NULL)==SQLITE_OK,"prepare many sessions");
  for(session_id=1;session_id<=20000;session_id++) {
    sqlite3_bind_int64(insert,1,(sqlite3_int64)session_id);
    sqlite3_bind_int64(insert,2,(sqlite3_int64)(session_id<=10000?2000:1000));
    require(sqlite3_step(insert)==SQLITE_DONE,"insert benchmark session");
    sqlite3_reset(insert);
  }
  sqlite3_finalize(insert);
  sql(db,"INSERT INTO turns(session_id,ordinal,prompt_group_key,state,created_at_ms) "
    "SELECT id,0,'benchmark','completed',created_at_ms FROM sessions;"
    "INSERT INTO conversation_items(session_id,turn_id,sequence,kind,created_at_ms) "
    "SELECT session_id,id,1,'message',created_at_ms FROM turns; COMMIT;");

  start=seconds();
  require(strappy_db_sidebar_open(path,&reader,&error),"large snapshot");
  for(session_id=1;session_id<=4;session_id++)
    require(strappy_db_sidebar_count_since(reader,session_id*500,&count,&error),"section count");
  require(strappy_db_sidebar_read(reader,0,&page,&error) && page.count==32,"first page");
  lazy_time=seconds()-start;
  strappy_db_sidebar_page_destroy(&page);
  start=seconds();
  strappy_session_record_list_init(&eager);
  require(strappy_db_list_sessions(path,&eager,&error) && eager.count==20000,"eager comparison");
  eager_time=seconds()-start;
  strappy_session_record_list_destroy(&eager);

  require(sqlite3_prepare_v2(db,"SELECT session_id FROM session_activity ORDER BY last_activity_at_ms DESC,session_id DESC;",-1,&expected,NULL)==SQLITE_OK,"expected order");
  for(offset=0;offset<20000;offset+=count) {
    before=strappy_db_sidebar_steps(reader);
    require(strappy_db_sidebar_read(reader,offset,&page,&error),"sequential page");
    if(offset>0) {
      unsigned long long steps=strappy_db_sidebar_steps(reader)-before;
      if(steps>max_steps) max_steps=steps;
      require(steps<6000ULL,"sequential seeks do not scan preceding timestamp ties");
    }
    require(page.count>0 && page.count<=STRAPPY_SIDEBAR_PAGE_SIZE,"bounded page");
    for(index=0;index<page.count;index++) {
      require(sqlite3_step(expected)==SQLITE_ROW &&
        page.records[index].session_id==(long long)sqlite3_column_int64(expected,0),"all rows in exact order without omissions");
      previous=page.records[index].session_id;
    }
    /* count is needed by the loop increment after freeing the page. */
    count=page.count;
    strappy_db_sidebar_page_destroy(&page);
  }
  memset(&page,0,sizeof(page));
  require(sqlite3_step(expected)==SQLITE_DONE && previous==10001,"all fixture rows read");
  sqlite3_finalize(expected);
  require(strappy_db_sidebar_index(reader,10000,&index,&error) && index==0,"first identity rank");
  require(strappy_db_sidebar_index(reader,1,&index,&error) && index==9999,"tie identity rank");
  require(strappy_db_sidebar_index(reader,20000,&index,&error) && index==10000,"second bucket identity rank");
  require(strappy_db_sidebar_read(reader,15000,&page,&error) && page.records[0].session_id==15000,"random page access");
  strappy_db_sidebar_page_destroy(&page);
  require(integer(db,"SELECT count(*) FROM pragma_foreign_key_check;")==0,"foreign keys valid");
  strappy_db_sidebar_close(reader);
  sqlite3_close(db);
  printf("Sidebar harness passed: 20,000 sessions; eager %.2f ms; lazy snapshot + 4 counts + first page %.2f ms; max sequential page %llu VM steps.\n",
    eager_time*1000.0,lazy_time*1000.0,max_steps);
  remove_database(path); remove_database(other_path); rmdir(directory);
  free(error);
  return 0;
}
