#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import "StrappyModelRows.h"
#import "XPFoundation.h"
#import "strappy_db.h"
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

@implementation NSNumber (ModelRowsHarness)
+ (NSNumber *)XP_numberWithUnsignedInteger:(XPUInteger)value { return [self numberWithUnsignedInteger:value]; }
@end
static void require(BOOL ok,NSString *message)
{
  if (!ok) { NSLog(@"FAIL: %@",message); exit(1); }
}
static char *search(size_t count,const char *const *values)
{
  NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
  NSMutableArray *parts=[NSMutableArray array];
  size_t index;
  char *text;
  for(index=0;index<count;index++) [parts addObject:[NSString stringWithUTF8String:values[index]]];
  text=strdup([[StrappyModelRows searchTextForValues:parts] UTF8String]);
  [pool drain]; return text;
}
static int compare(const char *a,const char *b)
{
  NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
  NSComparisonResult result=[[NSString stringWithUTF8String:a] caseInsensitiveCompare:[NSString stringWithUTF8String:b]];
  [pool drain]; return result<0 ? -1 : (result>0 ? 1 : 0);
}
static int contains(const char *text,const char *needle)
{
  NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
  NSRange range=[[NSString stringWithUTF8String:text] rangeOfString:[NSString stringWithUTF8String:needle]];
  int result=range.location!=NSNotFound;
  [pool drain]; return result;
}
static void group(void *context,const char *provider,const char *title,size_t offset,size_t count)
{
  [(NSMutableArray *)context addObject:[NSDictionary dictionaryWithObjectsAndKeys:
    [NSString stringWithUTF8String:provider],@"provider_id",[NSString stringWithUTF8String:title],@"title",
    [NSNumber numberWithUnsignedLong:offset],@"offset",[NSNumber numberWithUnsignedLong:count],@"count",nil]];
}
/* Exercise the real array against the C snapshot through its source protocol.
 * This test adapter also injects read failures without damaging a live store. */
@interface FixtureModelSource : NSObject <StrappyModelListSource> {
 @public
  strappy_model_reader *reader;
  NSUInteger pageReads;
  BOOL failReads;
}
@end
@implementation FixtureModelSource
- (NSUInteger)count { return strappy_db_model_list_count(reader); }
- (NSUInteger)totalCount { return strappy_db_model_list_total_count(reader); }
- (NSUInteger)allowedCount { return strappy_db_model_list_allowed_count(reader); }
- (BOOL)hasConfiguredAccounts { return strappy_db_model_list_has_accounts(reader) ? YES : NO; }
- (BOOL)filterWithSearch:(NSString *)text sortDescriptors:(NSArray *)descriptors error:(NSError **)error
{
  strappy_catalog_sort sort[]={{"model_provider",1},{"model_name",1},{"model_id",1}};
  (void)descriptors; (void)error;
  return strappy_db_model_list_query(reader,[text UTF8String],sort,3,NULL) ? YES : NO;
}
- (NSArray *)pageAtOffset:(NSUInteger)offset error:(NSError **)error
{
  strappy_model_record_list page;
  NSMutableArray *rows=[NSMutableArray array];
  size_t index;
  pageReads++;
  if(failReads) {
    *error=[NSError errorWithDomain:@"Fixture" code:1 userInfo:
      [NSDictionary dictionaryWithObject:@"Injected read failure" forKey:NSLocalizedDescriptionKey]];
    return nil;
  }
  require(strappy_db_model_list_page(reader,offset,&page,NULL),@"fixture page");
  for(index=0;index<page.count;index++) [rows addObject:[NSDictionary dictionaryWithObjectsAndKeys:
    [NSString stringWithUTF8String:page.records[index].model_id],@"id",
    [NSString stringWithUTF8String:page.records[index].name],@"name",nil]];
  strappy_model_record_list_destroy(&page); return rows;
}
- (NSArray *)providerSectionsWithError:(NSError **)error
{
  NSMutableArray *groups=[NSMutableArray array];
  (void)error;
  require(strappy_db_model_list_groups(reader,group,groups,NULL),@"fixture groups");
  return groups;
}
- (NSUInteger)indexForModelIdentifier:(NSString *)identifier error:(NSError **)error
{
  size_t index;
  (void)error;
  require(strappy_db_model_list_index(reader,[identifier UTF8String],&index,NULL),@"fixture identity");
  return index==(size_t)-1 ? NSNotFound : index;
}
- (void)dealloc { strappy_db_model_list_close(reader); [super dealloc]; }
@end
@interface ModelReadObserver : NSObject {
 @public
  NSUInteger errors;
}
- (void)failed:(NSNotification *)notification;
@end
@implementation ModelReadObserver
- (void)failed:(NSNotification *)notification { (void)notification; errors++; }
@end

int main(void)
{
  NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[[NSProcessInfo processInfo] globallyUniqueString]];
  sqlite3 *db;
  FixtureModelSource *source=[[FixtureModelSource alloc] init];
  StrappyModelRows *rows;
  strappy_model_list_text text={contains,search,compare};
  strappy_catalog_sort sort[]={{"model_provider",1},{"model_name",1},{"model_id",1}};
  NSMutableDictionary *pages;
  NSArray *sections;
  NSError *error=nil;
  NSUInteger index,before;
  ModelReadObserver *observer=[[[ModelReadObserver alloc] init] autorelease];
  require(strappy_db_initialize([path fileSystemRepresentation],NULL),@"schema");
  require(sqlite3_open([path fileSystemRepresentation],&db)==SQLITE_OK,@"fixture writer");
  require(sqlite3_exec(db,"UPDATE models SET catalog_active=0; INSERT INTO provider_accounts(id,provider_id,display_name,created_at_ms,updated_at_ms) VALUES('test','other','Account',1,1); BEGIN",NULL,NULL,NULL)==SQLITE_OK,@"fixture account");
  for(index=1;index<=320;index++) {
    NSString *name=index%2 ? @"Éclair" : @"Zulu";
    NSString *statement=[NSString stringWithFormat:@"INSERT INTO models(id,provider_id,wire_model_id,name,last_seen_at_ms) VALUES('other:model-%03lu','other','model-%03lu','%@',1)",(unsigned long)index,(unsigned long)index,name];
    require(sqlite3_exec(db,[statement UTF8String],NULL,NULL,NULL)==SQLITE_OK,@"fixture model");
  }
  require(sqlite3_exec(db,"COMMIT",NULL,NULL,NULL)==SQLITE_OK,@"commit");
  require(strappy_db_model_list_open([path fileSystemRepresentation],"Custom",NULL,sort,3,&text,&source->reader,NULL),@"snapshot");
  rows=[[StrappyModelRows alloc] initWithModelSource:source];
  pages=object_getIvar(rows,class_getInstanceVariable([StrappyModelRows class],"pages_"));
  require([rows count]==320 && [rows totalCount]==320 && [rows allowedCount]==0 && [rows hasConfiguredAccounts] && source->pageReads==0,@"count/account state are metadata");
  sections=[rows providerSectionsWithError:&error];
  require([sections count]==1 && [[[[sections objectAtIndex:0] objectForKey:@"count"] description] isEqualToString:@"320"] && source->pageReads==0,@"sections do not read rows");
  index=[rows indexForModelIdentifier:@"other:model-001"];
  require(index!=NSNotFound && source->pageReads==0,@"ID selection does not read rows");
  {
    NSArray *copy=[rows copy];
    require(copy==rows && source->pageReads==0,@"copy remains lazy"); [copy release];
  }
  [rows objectAtIndex:0]; [rows objectAtIndex:1];
  require(source->pageReads==1,@"neighboring cells share a page");
  for(index=32;index<128;index+=32) [rows objectAtIndex:index];
  [rows objectAtIndex:0]; [rows objectAtIndex:128];
  before=source->pageReads; [rows objectAtIndex:0];
  require(source->pageReads==before,@"recent page survives eviction");
  [rows objectAtIndex:32]; require(source->pageReads==before+1,@"oldest page evicted");
  for(index=0;index<320;index++) {
    [rows objectAtIndex:index]; require([pages count]<=4,@"cache holds at most 128 rows");
  }
  require([rows filterWithSearch:@"ÉCLAIR" sortDescriptors:nil error:&error] && [rows count]==160 && [pages count]==0,@"Unicode search resets cache");
  require([rows totalCount]==320 && [rows allowedCount]==0,@"unfiltered footer counts survive search");
  require([[[rows objectAtIndex:0] objectForKey:@"name"] isEqualToString:@"Éclair"],@"searched row");
  require([rows filterWithSearch:@"not found" sortDescriptors:nil error:&error] && [rows count]==0,@"empty search result");
  require([rows filterWithSearch:nil sortDescriptors:nil error:&error],@"clear search");
  [[NSNotificationCenter defaultCenter] addObserver:observer selector:@selector(failed:) name:StrappyModelListReadFailedNotification object:rows];
  source->failReads=YES;
  require([[rows objectAtIndex:0] objectForKey:@"id"]==nil && [rows readError]!=nil,@"unreadable row has no actionable identity");
  before=source->pageReads; [rows objectAtIndex:80];
  require(source->pageReads==before,@"failed snapshot does not repeatedly query");
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
  require(observer->errors==1,@"one deferred error notification");
  source->failReads=NO;
  require([rows filterWithSearch:nil sortDescriptors:nil error:&error] && [rows readError]==nil,@"successful query resets read error");
  require([[rows objectAtIndex:0] objectForKey:@"id"]!=nil,@"read recovers");
  require([[StrappyModelRows searchTextForValues:[NSArray arrayWithObjects:@"ABC",@"",[NSNumber numberWithInt:0],@"Éclair",nil]] isEqualToString:@"abc 0 éclair"],@"legacy search concatenation");
  [[NSNotificationCenter defaultCenter] removeObserver:observer];
  [rows release]; [source release]; sqlite3_close(db);
  [[NSFileManager defaultManager] removeItemAtPath:path error:NULL];
  [[NSFileManager defaultManager] removeItemAtPath:[path stringByAppendingString:@"-wal"] error:NULL];
  [[NSFileManager defaultManager] removeItemAtPath:[path stringByAppendingString:@"-shm"] error:NULL];
  NSLog(@"model rows Foundation harness passed: metadata, Unicode, LRU cache, identity, and read failures");
  [pool drain]; return 0;
}
