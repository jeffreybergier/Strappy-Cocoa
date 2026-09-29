#define _POSIX_C_SOURCE 200809L
#include "strappy_db.h"
#include <sqlite3.h>
#include <ctype.h>
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
  char *message=NULL;
  int rc=sqlite3_exec(db,statement,NULL,NULL,&message);
  if(rc!=SQLITE_OK) fprintf(stderr,"SQL: %s\n",message);
  sqlite3_free(message); require(rc==SQLITE_OK,statement);
}
static char *search_text(size_t count, const char *const *values)
{
  size_t index,length=1U,pos=0U;
  char *result;
  for(index=0U;index<count;index++) length+=strlen(values[index])+1U;
  result=malloc(length);
  if(result==NULL) return NULL;
  for(index=0U;index<count;index++) {
    const unsigned char *value=(const unsigned char *)values[index];
    if(!*value) continue;
    if(pos>0U) result[pos++]=' ';
    while(*value) result[pos++]=(char)tolower(*value++);
  }
  result[pos]='\0'; return result;
}
static int contains(const char *text, const char *needle) { return strstr(text,needle)!=NULL; }
static const char *provider_title(const char *provider)
{
  return !strcmp(provider,"openrouter") ? "OpenRouter" : (!strcmp(provider,"openai_chatgpt") ? "ChatGPT" : "Custom");
}
static const char *string(const char *value) { return value != NULL ? value : ""; }
static int compare(const void *a,const void *b)
{
  const strappy_model_record *left=a,*right=b;
  int result=strcasecmp(provider_title(left->provider_id),provider_title(right->provider_id));
  if(result) return result;
  if(left->allowed!=right->allowed) return right->allowed-left->allowed;
  return strcasecmp(left->wire_model_id,right->wire_model_id);
}
static size_t sort_key;
static int sort_direction;
static int compare_column(const void *a,const void *b)
{
  const strappy_model_record *left=a,*right=b;
  int result=strcasecmp(provider_title(left->provider_id),provider_title(right->provider_id));
  double x=0.0,y=0.0;
  if(result) return result;
  if(sort_key==0U) { x=(double)left->allowed; y=(double)right->allowed; }
  else if(sort_key==1U) result=strcasecmp(*left->name ? left->name : left->wire_model_id,*right->name ? right->name : right->wire_model_id);
  else if(sort_key==2U) result=strcasecmp(left->wire_model_id,right->wire_model_id);
  else if(sort_key==3U) { x=(double)left->context_length; y=(double)right->context_length; }
  else if(sort_key==4U) { x=strtod(string(left->pricing_prompt),NULL); y=strtod(string(right->pricing_prompt),NULL); }
  else { x=strtod(string(left->pricing_completion),NULL); y=strtod(string(right->pricing_completion),NULL); }
  if(x<y) result=-1;
  if(x>y) result=1;
  if(result) return sort_direction*result;
  return strcasecmp(left->wire_model_id,right->wire_model_id);
}
static size_t groups,group_rows;
static void group(void *context,const char *provider,const char *title,size_t offset,size_t count)
{
  (void)context;
  require(!strcmp(title,provider_title(provider)),"provider group title");
  require(offset==group_rows && count==7000U,"provider ranges cover unique models");
  group_rows+=count; groups++;
}
static double seconds(void)
{
  struct timespec time; clock_gettime(CLOCK_MONOTONIC,&time);
  return (double)time.tv_sec+(double)time.tv_nsec/1000000000.0;
}
int main(void)
{
  char directory[]="/tmp/strappy-model-list-XXXXXX",path[1024],sidecar[1100];
  const char *providers[]={"openrouter","openai_chatgpt","other"};
  sqlite3 *db=NULL;
  sqlite3_stmt *stmt=NULL;
  strappy_model_reader *reader=NULL,*fresh=NULL;
  strappy_model_record_list page,eager;
  strappy_model_list_text text={contains,search_text,strcasecmp};
  strappy_catalog_sort sort[]={{"model_provider",1},{"model_allowed",0},{"model_id",1},{"model_completion_price",1},{"model_prompt_price",1}};
  size_t provider,index,offset,position;
  unsigned long long before,steps,max_steps=0;
  double start,open_time,search_time;
  require(mkdtemp(directory)!=NULL,"fixture directory");
  snprintf(path,sizeof(path),"%s/models.sqlite",directory);
  require(strappy_db_initialize(path,&error),"initialize");
  require(strappy_db_model_list_open(path,"Custom",NULL,sort,5,&text,&reader,&error),"empty snapshot");
  require(strappy_db_model_list_count(reader)==0 && !strappy_db_model_list_has_accounts(reader),"no configured accounts");
  require(strappy_db_model_list_page(reader,0,&page,&error) && page.count==0,"empty page");
  strappy_model_record_list_destroy(&page); strappy_db_model_list_close(reader);
  require(sqlite3_open(path,&db)==SQLITE_OK,"fixture writer");
  sql(db,"PRAGMA foreign_keys=ON; UPDATE models SET catalog_active=0; BEGIN;"
    "INSERT INTO provider_accounts(id,provider_id,display_name,created_at_ms,updated_at_ms) VALUES"
    "('or-one','openrouter','First',1,1),('or-two','openrouter','Second',1,1),"
    "('gpt','openai_chatgpt','ChatGPT',1,1),('custom','other','Custom',1,1);");
  require(sqlite3_prepare_v2(db,"INSERT INTO models(id,provider_id,wire_model_id,name,description,context_length,created_at_s,provider_context_length,provider_max_completion_tokens,architecture_tokenizer,knowledge_cutoff,last_seen_at_ms) "
    "VALUES(?1,?2,?3,?4,'Search-only-description',?5,1700000000,1234567,98765,'tokenizer-unique','2031-09',1700000000000)",-1,&stmt,NULL)==SQLITE_OK,"prepare models");
  for(provider=0U;provider<3U;provider++) for(index=1U;index<=7000U;index++) {
    char id[128],wire[64],name[64];
    snprintf(wire,sizeof(wire),"model-%05lu",(unsigned long)index);
    snprintf(id,sizeof(id),"%s:%s",providers[provider],wire);
    snprintf(name,sizeof(name),"Name %05lu",(unsigned long)(7001U-index));
    sqlite3_bind_text(stmt,1,id,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,providers[provider],-1,SQLITE_STATIC);
    sqlite3_bind_text(stmt,3,wire,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,4,index==2U ? "" : name,-1,SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt,5,(sqlite3_int64)index);
    require(sqlite3_step(stmt)==SQLITE_DONE,"insert model"); sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt); stmt=NULL;
  sql(db,"INSERT INTO model_prices(model_id,price_kind,price_decimal) SELECT id,'prompt','0.000002' FROM models WHERE catalog_active=1 AND context_length%2=0;"
    "INSERT INTO model_prices(model_id,price_kind,price_decimal) SELECT id,'completion','0.00000987654' FROM models WHERE catalog_active=1;"
    "INSERT INTO model_preferences(provider_id,wire_model_id,allowed,updated_at_ms) SELECT provider_id,wire_model_id,1,1 FROM models WHERE catalog_active=1 AND context_length%2=0;"
    "UPDATE app_preferences SET default_model_id='openrouter:model-00001',default_provider_account_id='or-one' WHERE id=1; COMMIT;");
  start=seconds();
  require(strappy_db_model_list_open(path,"Custom",NULL,sort,5,&text,&reader,&error),"large snapshot");
  open_time=seconds()-start;
  require(strappy_db_model_list_count(reader)==21000U && strappy_db_model_list_has_accounts(reader),"one row per provider/model despite multiple accounts");
  require(strappy_db_model_list_total_count(reader)==21000U && strappy_db_model_list_allowed_count(reader)==10501U,"footer counts include default and all providers");
  require(strappy_db_model_list_groups(reader,group,NULL,&error) && groups==3U && group_rows==21000U,"metadata groups");
  require(strappy_db_list_models_for_configured_providers(path,&eager,&error) && eager.count==21000U,"legacy reference");
  qsort(eager.records,eager.count,sizeof(*eager.records),compare);
  for(offset=0U;offset<eager.count;offset+=32U) {
    before=strappy_db_model_list_page_steps(reader);
    require(strappy_db_model_list_page(reader,offset,&page,&error),"page");
    steps=strappy_db_model_list_page_steps(reader)-before;
    if(steps>max_steps) max_steps=steps;
    require(page.count>0 && page.count<=32U && steps<16000ULL,"bounded page size and indexed work");
    for(index=0U;index<page.count;index++) {
      strappy_model_record *expected=&eager.records[offset+index],*actual=&page.records[index];
      require(!strcmp(actual->model_id,expected->model_id),"same order as legacy UI");
      require(actual->allowed==expected->allowed && actual->selected==expected->selected && actual->context_length==expected->context_length &&
        !strcmp(string(actual->pricing_prompt),string(expected->pricing_prompt)) && !strcmp(string(actual->description),string(expected->description)) &&
        !strcmp(actual->billing_kind,expected->billing_kind),"same display, billing and permission fields");
    }
    strappy_model_record_list_destroy(&page);
  }
  /* Match every sortable UI column in both directions, with the original
   * Foundation comparator's numeric/missing-price and empty-name semantics. */
  {
    const char *keys[]={"model_allowed","model_name","model_id","model_context","model_prompt_price","model_completion_price"};
    for(sort_key=0U;sort_key<6U;sort_key++) for(sort_direction=-1;sort_direction<=1;sort_direction+=2) {
      strappy_catalog_sort columns[]={{"model_provider",1},{keys[sort_key],sort_direction==1},{"model_id",1}};
      qsort(eager.records,eager.count,sizeof(*eager.records),compare_column);
      require(strappy_db_model_list_query(reader,NULL,columns,3,&error),"column sort query");
      for(offset=0U;offset<eager.count;offset+=137U) {
        require(strappy_db_model_list_page(reader,offset,&page,&error),"random column-sorted page");
        for(index=0U;index<page.count;index++)
          require(!strcmp(page.records[index].model_id,eager.records[offset+index].model_id),"column order matches eager comparator");
        strappy_model_record_list_destroy(&page);
      }
    }
  }
  require(strappy_db_model_list_query(reader,NULL,sort,5,&error),"restore normal order");
  strappy_model_record_list_destroy(&eager);
  require(strappy_db_model_list_index(reader,"openrouter:model-00001",&position,&error) && position!=((size_t)-1),"identity index");
  require(strappy_db_model_list_page(reader,position,&page,&error) && page.records[0].selected && page.records[0].allowed,"default is always allowed");
  strappy_model_record_list_destroy(&page);
  require(!strappy_db_set_model_allowed(path,"openrouter:model-00001",0,&error),"cannot disable default"); free(error); error=NULL;
  start=seconds();
  require(strappy_db_model_list_query(reader,"MODEL-06999",sort,5,&error) && strappy_db_model_list_count(reader)==3U,"ID search across providers");
  search_time=seconds()-start;
  {
    const char *terms[]={"SEARCH-ONLY-DESCRIPTION","tokenizer-unique","0.00000987654","1234567","98765","2031-09","2023-11-14T22:13:20.000Z"};
    for(index=0U;index<sizeof(terms)/sizeof(terms[0]);index++)
      require(strappy_db_model_list_query(reader,terms[index],sort,5,&error) && strappy_db_model_list_count(reader)==21000U,"all legacy search fields retained");
  }
  require(strappy_db_model_list_query(reader,"ChatGPT",sort,5,&error) && strappy_db_model_list_count(reader)==7000U,"provider display-name search");
  require(strappy_db_model_list_query(reader,"or-two",sort,5,&error) && strappy_db_model_list_count(reader)==0U,"provider rows do not invent account metadata");
  require(strappy_db_model_list_query(reader,NULL,sort,5,&error),"clear search");
  sql(db,"UPDATE provider_accounts SET lifecycle_state='archived' WHERE provider_id='other';"
    "UPDATE models SET catalog_active=0 WHERE id='openrouter:model-00002';"
    "UPDATE models SET name='Changed' WHERE id='openrouter:model-00001';");
  require(strappy_db_model_list_query(reader,NULL,sort,5,&error) && strappy_db_model_list_count(reader)==21000U,"old snapshot stable after account/catalog edits");
  require(strappy_db_model_list_open(path,"Custom",NULL,sort,5,&text,&fresh,&error) && strappy_db_model_list_count(fresh)==13999U,"new snapshot sees archived accounts and removed model");
  require(strappy_db_model_list_index(fresh,"other:model-00001",&position,&error) && position==(size_t)-1,"archived provider missing from identity lookup");
  require(strappy_db_model_list_index(fresh,"openrouter:model-00001",&position,&error),"updated identity");
  require(strappy_db_model_list_page(fresh,position,&page,&error) && !strcmp(page.records[0].name,"Changed"),"fresh edited model");
  strappy_model_record_list_destroy(&page); strappy_db_model_list_close(fresh);
  {
    strappy_catalog_sort bad={"untrusted; DROP TABLE models",1};
    require(!strappy_db_model_list_query(reader,NULL,&bad,1,&error),"reject unknown sort key"); free(error); error=NULL;
    require(strappy_db_model_list_count(reader)==21000U,"bad query retains old ordering");
  }
  require(strappy_db_set_default_model(path,"openrouter:model-00003",&error),"change default");
  require(strappy_db_set_model_allowed(path,"openrouter:model-00005",1,&error),"allow a model");
  require(strappy_db_model_list_open(path,"Custom",NULL,sort,5,&text,&fresh,&error),"refresh changed permissions");
  require(strappy_db_model_list_index(fresh,"openrouter:model-00003",&position,&error) &&
    strappy_db_model_list_page(fresh,position,&page,&error) && page.records[0].selected && page.records[0].allowed,"new default selected and allowed");
  strappy_model_record_list_destroy(&page);
  require(strappy_db_model_list_index(fresh,"openrouter:model-00005",&position,&error) &&
    strappy_db_model_list_page(fresh,position,&page,&error) && page.records[0].allowed,"allowed edit persisted");
  strappy_model_record_list_destroy(&page); strappy_db_model_list_close(fresh);
  require(sqlite3_prepare_v2(db,"PRAGMA user_version",-1,&stmt,NULL)==SQLITE_OK &&
    sqlite3_step(stmt)==SQLITE_ROW && sqlite3_column_int(stmt,0)==1,"schema version unchanged");
  sqlite3_finalize(stmt);
  strappy_db_model_list_close(reader); sqlite3_close(db);
  unlink(path); snprintf(sidecar,sizeof(sidecar),"%s-wal",path); unlink(sidecar);
  snprintf(sidecar,sizeof(sidecar),"%s-shm",path); unlink(sidecar); rmdir(directory);
  printf("model list harness passed: 21000 models; open %.2f ms; reused search %.2f ms; max page steps %llu\n",open_time*1000.0,search_time*1000.0,max_steps);
  return 0;
}
