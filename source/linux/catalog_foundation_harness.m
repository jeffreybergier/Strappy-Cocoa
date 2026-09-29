#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import "FileScanner.h"
#import "XPFoundation.h"
#import "strappy_db.h"
#include <sqlite3.h>
#include <unistd.h>

/* Host-only boundary stubs; the production reader and Foundation callbacks are
 * compiled from FileScanner.m. No provider credentials or network calls. */
static NSString *fixturePath;
@interface StrappySession : NSObject
+ (NSString *)sessionsDatabasePath;
+ (BOOL)initializeSessionStoreWithError:(NSError **)error;
@end
@implementation StrappySession
+ (NSString *)sessionsDatabasePath { return fixturePath; }
+ (BOOL)initializeSessionStoreWithError:(NSError **)error
{
  (void)error;
  return strappy_db_initialize([fixturePath fileSystemRepresentation],NULL) ? YES : NO;
}
@end
@implementation NSNumber (CatalogHarness)
+ (NSNumber *)XP_numberWithUnsignedInteger:(XPUInteger)value { return [self numberWithUnsignedInteger:value]; }
+ (NSNumber *)XP_numberWithInteger:(XPInteger)value { return [self numberWithInteger:value]; }
- (XPInteger)XP_integerValue { return [self integerValue]; }
@end
@implementation NSProcessInfo (CatalogHarness)
- (XPPlatformFamily)XP_platformFamily { return XPPlatformFamilyGeneric; }
@end

@interface FileScanner (CatalogHarness)
+ (void)queueCatalogUpdate;
+ (void)databaseCatalogScanDidFinish:(NSDictionary *)result;
@end

static void require(BOOL ok, NSString *message)
{
  if (!ok) { NSLog(@"FAIL: %@",message); exit(1); }
}
static void sql(sqlite3 *db, NSString *statement)
{
  char *error = NULL;
  int result = sqlite3_exec(db,[statement UTF8String],NULL,NULL,&error);
  if (result != SQLITE_OK) NSLog(@"SQL failed: %s",error);
  sqlite3_free(error);
  require(result==SQLITE_OK,statement);
}
@interface CatalogObserver : NSObject {
 @public
  NSUInteger changes;
}
- (void)changed:(NSNotification *)notification;
@end
@implementation CatalogObserver
- (void)changed:(NSNotification *)notification
{
  require([[notification userInfo] objectForKey:@"rows"]==nil,@"notifications carry no catalog arrays");
  changes++;
}
@end

int main(void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSString *directory = [NSTemporaryDirectory() stringByAppendingPathComponent:
    [[NSProcessInfo processInfo] globallyUniqueString]];
  sqlite3 *db;
  NSError *error = nil;
  FileScannerCatalogRows *rows;
  NSArray *sort = [NSArray arrayWithObjects:
    [[[NSSortDescriptor alloc] initWithKey:@"application" ascending:YES] autorelease],
    [[[NSSortDescriptor alloc] initWithKey:@"group_key" ascending:YES] autorelease],
    [[[NSSortDescriptor alloc] initWithKey:@"size" ascending:NO] autorelease],nil];
  NSArray *sections;
  NSMutableDictionary *pages;
  NSUInteger index;
  NSTimeInterval start, openTime, searchTime;
  CatalogObserver *observer = [[[CatalogObserver alloc] init] autorelease];
  require([[NSFileManager defaultManager] createDirectoryAtPath:directory
    withIntermediateDirectories:YES attributes:nil error:&error],@"fixture directory");
  fixturePath = [directory stringByAppendingPathComponent:@"catalog.sqlite"];
  require([StrappySession initializeSessionStoreWithError:&error],@"schema");
  require(sqlite3_open([fixturePath fileSystemRepresentation],&db)==SQLITE_OK,@"writer");
  sql(db,@"BEGIN; INSERT INTO applications(id,stable_key,name,bundle_id) VALUES"
    "(1,'one','Éclair','org.one'),(2,'two','Éclair','org.two');");
  for (index=1;index<=20000U;index++) {
    sql(db,[NSString stringWithFormat:@"INSERT INTO databases(id,stable_key,application_id,first_seen_at_ms,last_seen_at_ms) VALUES(%lu,'%lu',%lu,1,1);"
      "INSERT INTO database_locations(database_id,path,size_bytes,validation_state,first_seen_at_ms,last_seen_at_ms,last_scanned_at_ms) VALUES(%lu,'/fixture/Café-%lu.sqlite',%lu,'valid',1,1,1);"
      "INSERT INTO database_permissions(database_id,updated_at_ms) VALUES(%lu,1);",
      (unsigned long)index,(unsigned long)index,(unsigned long)(1U+(index%2U)),
      (unsigned long)index,(unsigned long)index,(unsigned long)index,(unsigned long)index]);
  }
  sql(db,@"COMMIT;");
  start=[NSDate timeIntervalSinceReferenceDate];
  rows=[[[FileScanner sharedScanner] catalogRowsMatchingSearch:nil showHidden:NO sortDescriptors:sort error:&error] retain];
  openTime=[NSDate timeIntervalSinceReferenceDate]-start;
  require(rows!=nil,[error description]);
  pages=object_getIvar(rows,class_getInstanceVariable([FileScannerCatalogRows class],"pages_"));
  require([rows count]==20000U && [rows totalCount]==20000U && [rows allowedCount]==0U && [rows hiddenCount]==0U && [pages count]==0U,@"count does not hydrate rows");
  sections=[rows applicationSectionsWithError:&error];
  require([sections count]==2U && [pages count]==0U,@"groups do not hydrate rows");
  require([[[sections objectAtIndex:0] objectForKey:@"title"] isEqualToString:@"Éclair (org.one)"],@"duplicate app names disambiguate");
  require([[[sections objectAtIndex:0] objectForKey:@"rows"] count]==10000U,@"range count");
  require([rows indexForCatalogIdentifier:[NSNumber numberWithInt:20000]]==0U && [pages count]==0U,@"identity rank avoids row reads");
  {
    NSArray *copy=[rows copy];
    require(copy==rows && [pages count]==0U,@"NSArray copy remains lazy");
    [copy release];
  }
  require([[[[[sections objectAtIndex:0] objectForKey:@"rows"] objectAtIndex:0]
    objectForKey:@"catalog_id"] intValue]==20000,@"lazy section mapping");
  for (index=0;index<20000U;index+=32U) {
    [rows objectAtIndex:index];
    require([pages count]<=4U,@"bounded 128-row cache");
  }
  start=[NSDate timeIntervalSinceReferenceDate];
  require([rows filterWithSearch:@"CAFÉ-19999" showHidden:NO sortDescriptors:sort error:&error],@"Unicode search");
  searchTime=[NSDate timeIntervalSinceReferenceDate]-start;
  require([rows count]==1U && [pages count]==0U,@"query invalidates pages");
  require([[[rows objectAtIndex:0] objectForKey:@"catalog_id"] intValue]==19999,@"Foundation case-insensitive Unicode result");
  require([rows filterWithSearch:@"ÉCLAIR" showHidden:NO sortDescriptors:sort error:&error] && [rows count]==20000U,@"Unicode application search");
  require([rows filterWithSearch:@"ORG.ONE" showHidden:NO sortDescriptors:sort error:&error] && [rows count]==10000U,@"bundle search");
  [rows release];
  [[NSNotificationCenter defaultCenter] addObserver:observer selector:@selector(changed:)
    name:FileScannerDatabaseCatalogDidChangeNotification object:nil];
  for(index=0;index<100U;index++) [FileScanner queueCatalogUpdate];
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.4]];
  require(observer->changes==1U,@"scan batch updates coalesce");
  [FileScanner queueCatalogUpdate];
  [FileScanner databaseCatalogScanDidFinish:[NSDictionary dictionaryWithObject:@"test failure" forKey:@"error"]];
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.4]];
  require(observer->changes==2U,@"scan completion flushes partial commits and cancels pending update");
  [[NSNotificationCenter defaultCenter] removeObserver:observer];
  sqlite3_close(db);
  [[NSFileManager defaultManager] removeItemAtPath:directory error:NULL];
  NSLog(@"catalog Foundation harness passed: 20000 rows, open %.2f ms, Unicode search %.2f ms",openTime*1000.0,searchTime*1000.0);
  [pool drain];
  return 0;
}
