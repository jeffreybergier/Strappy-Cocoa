#define _POSIX_C_SOURCE 200809L
#include "strappy_db.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

static char *error;
static void require(int ok, const char *message)
{
  if (!ok) { fprintf(stderr,"FAIL: %s: %s\n",message,error != NULL ? error : ""); exit(1); }
}
static void sql(sqlite3 *db, const char *statement)
{
  char *message = NULL;
  int rc = sqlite3_exec(db,statement,NULL,NULL,&message);
  if (rc != SQLITE_OK) fprintf(stderr,"SQL: %s\n",message);
  sqlite3_free(message);
  require(rc == SQLITE_OK,statement);
}
static char *field(const char *kind, const char *path, const char *name, const char *group)
{
  const char *slash = strrchr(path,'/');
  if (!strcmp(kind,"application")) return strdup(*name ? name : (*group ? group : "Other"));
  if (!strcmp(kind,"group_key")) return strdup(*group ? group : "path:/fixture");
  if (!strcmp(kind,"name")) return strdup(slash != NULL ? slash+1 : path);
  return strdup("/fixture");
}
static int contains(const char *text, const char *needle)
{
  size_t length = strlen(needle);
  do { if (!strncasecmp(text,needle,length)) return 1; } while (*text++);
  return 0;
}
static double seconds(void)
{
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC,&time);
  return (double)time.tv_sec + (double)time.tv_nsec / 1000000000.0;
}
static size_t group_count, group_rows;
static void group(void *context, const char *name, const char *key, const char *bundle,
                  size_t offset, size_t count, int disambiguate)
{
  (void)context;
  require(!strcmp(name,"Application") && *key && *bundle,"group metadata");
  require(disambiguate && offset == group_rows && count == 200U,"contiguous lazy group ranges");
  group_count++; group_rows += count;
}
int main(void)
{
  char directory[] = "/tmp/strappy-catalog-XXXXXX";
  char path[1024], sidecar[1100];
  sqlite3 *db = NULL;
  strappy_catalog_reader *reader = NULL, *other = NULL;
  strappy_catalog_text text = { field,strcasecmp,contains };
  strappy_catalog_sort sort[] = {{"application",1},{"group_key",1},{"database_priority",0},{"size",0},{"name",1}};
  strappy_catalog_sort size_sort[] = {{"size",1}};
  strappy_catalog_sort bad_sort[] = {{"unknown; DROP TABLE databases",1}};
  strappy_discovered_database_record_list page;
  sqlite3_stmt *stmt = NULL, *expected = NULL;
  size_t offset, index;
  unsigned long long max_steps=0, before, steps;
  double start, open_time, search_time;
  require(mkdtemp(directory) != NULL,"temporary directory");
  snprintf(path,sizeof(path),"%s/catalog.sqlite",directory);
  require(strappy_db_initialize(path,&error),"initialize");
  require(strappy_db_catalog_open(path,NULL,0,sort,5,&text,&reader,&error),"empty snapshot");
  require(strappy_db_catalog_count(reader)==0,"empty count");
  require(strappy_db_catalog_page(reader,0,&page,&error) && page.count==0,"empty page");
  strappy_discovered_database_record_list_destroy(&page);
  strappy_db_catalog_close(reader);
  require(sqlite3_open(path,&db)==SQLITE_OK,"fixture writer");
  sql(db,"PRAGMA foreign_keys=ON; BEGIN;");
  require(sqlite3_prepare_v2(db,"INSERT INTO applications(id,stable_key,name,bundle_id) VALUES(?1,?2,'Application',?2)",-1,&stmt,NULL)==SQLITE_OK,"prepare apps");
  for (index=1;index<=100U;index++) {
    char key[80]; snprintf(key,sizeof(key),"app-%03lu",(unsigned long)index);
    sqlite3_bind_int64(stmt,1,(sqlite3_int64)index); sqlite3_bind_text(stmt,2,key,-1,SQLITE_TRANSIENT);
    require(sqlite3_step(stmt)==SQLITE_DONE,"insert app"); sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  require(sqlite3_prepare_v2(db,"INSERT INTO databases(id,stable_key,application_id,first_seen_at_ms,last_seen_at_ms) VALUES(?1,?1,1+(?1-1)/200,1,1)",-1,&stmt,NULL)==SQLITE_OK,"prepare databases");
  for(index=1;index<=20000U;index++) {
    sqlite3_bind_int64(stmt,1,(sqlite3_int64)index); require(sqlite3_step(stmt)==SQLITE_DONE,"insert db"); sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  sql(db,"INSERT INTO database_locations(id,database_id,path,size_bytes,validation_state,first_seen_at_ms,last_seen_at_ms,last_scanned_at_ms) "
    "SELECT id,id,'/fixture/file-'||id||'.sqlite',id,'valid',1,1,1 FROM databases;"
    "INSERT INTO database_permissions(database_id,hidden,decision,updated_at_ms) "
    "SELECT id,CASE WHEN id%2=0 THEN 1 ELSE 0 END,CASE WHEN id%4=0 THEN 'allowed' ELSE 'unknown' END,1 FROM databases; COMMIT;");
  start=seconds();
  require(strappy_db_catalog_open(path,NULL,1,sort,5,&text,&reader,&error),"open large snapshot");
  open_time=seconds()-start;
  require(strappy_db_catalog_count(reader)==20000U,"large count");
  require(strappy_db_catalog_groups(reader,group,NULL,&error),"group metadata without pages");
  require(group_count==100U && group_rows==20000U,"all groups");
  require(sqlite3_prepare_v2(db,"SELECT d.id FROM databases d JOIN applications a ON a.id=d.application_id "
    "JOIN database_locations l ON l.database_id=d.id JOIN database_permissions p ON p.database_id=d.id "
    "ORDER BY a.name COLLATE NOCASE,a.stable_key COLLATE NOCASE,p.hidden,l.size_bytes DESC,l.path COLLATE NOCASE",-1,&expected,NULL)==SQLITE_OK,"reference ordering");
  for(offset=0;offset<20000U;offset+=32U) {
    before=strappy_db_catalog_page_steps(reader);
    require(strappy_db_catalog_page(reader,offset,&page,&error),"read bounded page");
    steps=strappy_db_catalog_page_steps(reader)-before;
    if (steps>max_steps) max_steps=steps;
    require(steps<6000ULL,"page query uses indexed ordinal range");
    require(page.count>0 && page.count<=32U,"page bound");
    for(index=0;index<page.count;index++) {
      require(sqlite3_step(expected)==SQLITE_ROW && sqlite3_column_int64(expected,0)==page.records[index].catalog_id,"full traversal order");
      require(page.records[index].is_valid_sqlite && page.records[index].path!=NULL,"hydrated record");
    }
    strappy_discovered_database_record_list_destroy(&page);
  }
  require(sqlite3_step(expected)==SQLITE_DONE,"exact traversal"); sqlite3_finalize(expected);
  require(strappy_db_catalog_index(reader,500,&index,&error) && index!=((size_t)-1),"identity lookup");
  require(strappy_db_catalog_page(reader,index,&page,&error) && page.records[0].catalog_id==500,"random jump");
  strappy_discovered_database_record_list_destroy(&page);
  require(strappy_db_catalog_index(reader,99999,&index,&error) && index==(size_t)-1,"missing identity");
  start=seconds();
  require(strappy_db_catalog_query(reader,"FILE-19999",0,sort,5,&error),"case-insensitive contains search");
  search_time=seconds()-start;
  require(strappy_db_catalog_count(reader)==1,"search count");
  require(strappy_db_catalog_page(reader,0,&page,&error) && page.records[0].catalog_id==19999,"search result");
  strappy_discovered_database_record_list_destroy(&page);
  require(!strappy_db_catalog_query(reader,NULL,1,bad_sort,1,&error),"reject unknown sort key");
  free(error); error=NULL;
  require(strappy_db_catalog_count(reader)==1,"failed query preserves results");
  require(strappy_db_catalog_query(reader,NULL,0,size_sort,1,&error),"reuse keys and change ordering");
  require(strappy_db_catalog_count(reader)==15000U,"hidden allowed rows remain visible");
  require(strappy_db_catalog_page(reader,0,&page,&error) && page.records[0].catalog_id==1 &&
    page.records[1].catalog_id==3 && page.records[2].catalog_id==4,"numeric sorting and hidden rules");
  strappy_discovered_database_record_list_destroy(&page);
  sql(db,"UPDATE database_permissions SET hidden=1 WHERE database_id=1; UPDATE database_locations SET size_bytes=999999 WHERE database_id=1;");
  require(strappy_db_catalog_query(reader,NULL,0,size_sort,1,&error),"old snapshot remains stable after writes");
  require(strappy_db_catalog_page(reader,0,&page,&error) && page.records[0].catalog_id==1 && page.records[0].size==1,"old row values and permissions");
  strappy_discovered_database_record_list_destroy(&page);
  require(strappy_db_catalog_open(path,NULL,0,size_sort,1,&text,&other,&error),"new snapshot");
  require(strappy_db_catalog_count(other)==14999U,"new permissions");
  require(strappy_db_catalog_index(other,1,&index,&error) && index==(size_t)-1,"hidden identity disappears");
  strappy_db_catalog_close(other);
  require(strappy_db_catalog_query(reader,"no-match",1,sort,5,&error) && strappy_db_catalog_count(reader)==0,"no-match results");
  strappy_db_catalog_close(reader);
  require(sqlite3_prepare_v2(db,"PRAGMA user_version",-1,&stmt,NULL)==SQLITE_OK && sqlite3_step(stmt)==SQLITE_ROW && sqlite3_column_int(stmt,0)==1,"schema version remains one");
  sqlite3_finalize(stmt); sqlite3_close(db);
  unlink(path); snprintf(sidecar,sizeof(sidecar),"%s-wal",path); unlink(sidecar);
  snprintf(sidecar,sizeof(sidecar),"%s-shm",path); unlink(sidecar); rmdir(directory);
  printf("catalog harness passed: 20000 rows; open %.2f ms, reused-key search %.2f ms, max page steps %llu\n",open_time*1000.0,search_time*1000.0,max_steps);
  return 0;
}
