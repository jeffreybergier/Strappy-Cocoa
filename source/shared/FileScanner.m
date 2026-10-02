#import "StrappyAsync.h"
#import "FileScanner.h"

#import "StrappySession.h"
#import "XPFoundation.h"
#import "strappy_core.h"
#import "strappy_db.h"
#import "strappy_store_client.h"
#import "strappy_ipc.h"
#import "strappy_run.h"
#include <stdlib.h>
#include <string.h>

NSString * const FileScannerDatabaseCatalogScanDidStartNotification =
  @"FileScannerDatabaseCatalogScanDidStartNotification";
NSString * const FileScannerDatabaseCatalogScanDidFinishNotification =
  @"FileScannerDatabaseCatalogScanDidFinishNotification";
NSString * const FileScannerDatabaseCatalogDidChangeNotification =
  @"FileScannerDatabaseCatalogDidChangeNotification";

NSString * const FileScannerCatalogReadFailedNotification = @"FileScannerCatalogReadFailedNotification";


@interface FileScannerCatalogRows ()
- (id)initWithPath:(NSString *)path search:(NSString *)search showHidden:(BOOL)showHidden
  descriptors:(NSArray *)descriptors error:(NSError **)error;
@end

static BOOL FileScannerDatabaseCatalogScanInFlight = NO;
@interface FileScanner ()
+ (NSError *)errorFromCString:(char *)message;
+ (NSDictionary *)dictionaryFromDiscoveredDatabaseRecord:(const strappy_discovered_database_record *)record;
+ (void)databaseCatalogDidChange:(NSDictionary *)result;
@end

static BOOL FileScannerCommand(unsigned int operation, cJSON *command, NSError **error)
{
  unsigned int status;
  char *reply = strappy_store_client_mutation(operation,command,&status);
  BOOL ok = reply != NULL;
  free(reply); cJSON_Delete(command);
  if (!ok && error) *error = [FileScanner errorFromCString:status == STRAPPY_IPC_BUSY ?
    "The service is busy. Try again after the current work finishes." :
    "The service could not acknowledge the catalog change. Reload stored state before retrying."];
  return ok;
}

@implementation FileScanner

+ (FileScanner *)sharedScanner
{
  static FileScanner *scanner = nil;

  @synchronized(self) {
    if (scanner == nil) {
      scanner = [[FileScanner alloc] init];
    }
  }

  return scanner;
}

+ (BOOL)isDatabaseCatalogScanInFlight
{
  BOOL inFlight;

  @synchronized(self) {
    inFlight = FileScannerDatabaseCatalogScanInFlight;
  }
  return inFlight;
}

+ (BOOL)beginDatabaseCatalogScanAtPath:(NSString *)path
                                 error:(NSError **)error
{
  return [self beginDatabaseCatalogScanAtPath:path scanMode:FileScannerDatabaseScanModeFull error:error];
}

+ (BOOL)beginDatabaseCatalogScanAtPath:(NSString *)path
                              scanMode:(FileScannerDatabaseScanMode)scanMode
                                 error:(NSError **)error
{
  cJSON *command;
  if (![path length] || ![path isAbsolutePath]) {
    if (error) *error = [self errorFromCString:"Scan path must be absolute."];
    return NO;
  }
  command = cJSON_CreateObject();
  cJSON_AddStringToObject(command,"path",[path fileSystemRepresentation]);
  cJSON_AddStringToObject(command,"quick",scanMode == FileScannerDatabaseScanModeQuick ? "1" : "0");
  if (!FileScannerCommand(STRAPPY_IPC_START_SCAN,command,error)) return NO;
  /* The monitor owns activity state. A fast job can finish before this reply. */
  return YES;
}

+ (void)databaseCatalogScanDidFinish:(NSDictionary *)result
{
  NSMutableDictionary *userInfo;

  userInfo = [[NSMutableDictionary alloc] init];
  if ([result isKindOfClass:[NSDictionary class]]) {
    [userInfo addEntriesFromDictionary:result];
  }

  @synchronized(self) {
    FileScannerDatabaseCatalogScanInFlight = NO;
  }

  /* Publish committed batches even when a later part of the scan failed. */
  [self databaseCatalogDidChange:userInfo];

  [StrappyAsync postNotificationName:FileScannerDatabaseCatalogScanDidFinishNotification
                  object:self
                userInfo:userInfo];
  NSString *message = [userInfo objectForKey:@"error"];
  if ([message isKindOfClass:[NSString class]]) [StrappyAsync reportError:
    [NSError errorWithDomain:@"StrappyServiceErrorDomain" code:1 userInfo:
      [NSDictionary dictionaryWithObject:message forKey:NSLocalizedDescriptionKey]]
    title:NSLocalizedString(@"Scan databases",nil)];
  [userInfo release];
}

+ (void)databaseCatalogDidChange:(NSDictionary *)result
{
  NSDictionary *userInfo;

  userInfo = [result isKindOfClass:[NSDictionary class]] ?
    result : [NSDictionary dictionary];
  [StrappyAsync postNotificationName:FileScannerDatabaseCatalogDidChangeNotification
                  object:self
                userInfo:userInfo];
}

+ (void)serviceWorkDidChange
{
  unsigned int active, last, result;
  strappy_store_client_work_status(&active,&last,&result);
  if ([self isDatabaseCatalogScanInFlight] && active != STRAPPY_IPC_START_SCAN) {
    [self databaseCatalogScanDidFinish:(last == STRAPPY_IPC_START_SCAN && result == STRAPPY_RUN_COMPLETED) ?
      [NSDictionary dictionary] : [NSDictionary dictionaryWithObject:@"Scan failed or the service restarted." forKey:@"error"]];
  } else if (![self isDatabaseCatalogScanInFlight] && active == STRAPPY_IPC_START_SCAN) {
    @synchronized(self) { FileScannerDatabaseCatalogScanInFlight = YES; }
    [StrappyAsync postNotificationName:FileScannerDatabaseCatalogScanDidStartNotification object:self];
  }
}

+ (NSError *)errorFromCString:(char *)message
{
  NSString *description;
  NSDictionary *userInfo;

  if (message != NULL) {
    description = [NSString stringWithUTF8String:message];
  } else {
    description = nil;
  }

  if (description == nil) {
    description = NSLocalizedString(@"Filesystem scan failed.", nil);
  }

  userInfo = [NSDictionary dictionaryWithObject:description
                                         forKey:NSLocalizedDescriptionKey];
  return [NSError errorWithDomain:@"FileScannerErrorDomain"
                             code:1
                         userInfo:userInfo];
}

+ (NSString *)stringFromCStringOrEmpty:(const char *)value
{
  NSString *string;

  if (value == NULL) {
    return @"";
  }

  string = [NSString stringWithUTF8String:value];
  if (string == nil) {
    return @"";
  }

  return string;
}

+ (NSDictionary *)dictionaryFromDiscoveredDatabaseRecord:
    (const strappy_discovered_database_record *)record
{
  NSNumber *catalogId;
  NSNumber *size;
  NSNumber *modifiedAt;
  NSNumber *device;
  NSNumber *inode;
  NSNumber *isValidSQLite;
  NSNumber *hidden;
  NSNumber *autoHidden;
  NSNumber *hiddenOverride;
  NSString *assistantDatabaseId;
  NSString *path;
  NSString *validationError;
  NSString *scanStatus;
  NSString *userDecision;
  NSString *scanRoot;
  NSString *appGroupKey;
  NSString *appName;
  NSString *appBundleId;
  NSString *appContainerPath;
  NSString *appBundlePath;
  NSString *appSource;
  NSString *originKind;
  NSString *locationTail;
  NSString *hiddenReason;
  NSString *firstSeenAt;
  NSString *lastSeenAt;
  NSString *lastScannedAt;
  NSMutableDictionary *dictionary;

  if (record == NULL) {
    return nil;
  }

  catalogId = [NSNumber numberWithLongLong:record->catalog_id];
  size = [NSNumber numberWithLongLong:record->size];
  modifiedAt = [NSNumber numberWithLongLong:record->modified_at];
  device = [NSNumber numberWithUnsignedLongLong:record->device];
  inode = [NSNumber numberWithUnsignedLongLong:record->inode];
  isValidSQLite = [NSNumber numberWithBool:(record->is_valid_sqlite ? YES : NO)];
  hidden = [NSNumber numberWithBool:(record->hidden ? YES : NO)];
  autoHidden = [NSNumber numberWithBool:(record->auto_hidden ? YES : NO)];
  hiddenOverride = record->has_hidden_override ?
    [NSNumber numberWithBool:(record->hidden_override ? YES : NO)] : nil;
  assistantDatabaseId =
    [FileScanner stringFromCStringOrEmpty:record->assistant_database_id];
  path = [FileScanner stringFromCStringOrEmpty:record->path];
  validationError =
    [FileScanner stringFromCStringOrEmpty:record->validation_error];
  scanStatus = [FileScanner stringFromCStringOrEmpty:record->scan_status];
  userDecision = [FileScanner stringFromCStringOrEmpty:record->user_decision];
  scanRoot = [FileScanner stringFromCStringOrEmpty:record->scan_root];
  appGroupKey = [FileScanner stringFromCStringOrEmpty:record->app_group_key];
  appName = [FileScanner stringFromCStringOrEmpty:record->app_name];
  appBundleId = [FileScanner stringFromCStringOrEmpty:record->app_bundle_id];
  appContainerPath =
    [FileScanner stringFromCStringOrEmpty:record->app_container_path];
  appBundlePath =
    [FileScanner stringFromCStringOrEmpty:record->app_bundle_path];
  appSource = [FileScanner stringFromCStringOrEmpty:record->app_source];
  originKind = [FileScanner stringFromCStringOrEmpty:record->origin_kind];
  locationTail = [FileScanner stringFromCStringOrEmpty:record->location_tail];
  hiddenReason = [FileScanner stringFromCStringOrEmpty:record->hidden_reason];
  firstSeenAt = [FileScanner stringFromCStringOrEmpty:record->first_seen_at];
  lastSeenAt = [FileScanner stringFromCStringOrEmpty:record->last_seen_at];
  lastScannedAt =
    [FileScanner stringFromCStringOrEmpty:record->last_scanned_at];

  if ([path length] == 0U) {
    return nil;
  }

  dictionary = [NSMutableDictionary dictionaryWithObjectsAndKeys:
    catalogId, @"catalog_id",
    assistantDatabaseId, @"assistant_database_id",
    path, @"path",
    size, @"size",
    modifiedAt, @"modified_at",
    device, @"device",
    inode, @"inode",
    isValidSQLite, @"is_valid_sqlite",
    hidden, @"hidden",
    autoHidden, @"auto_hidden",
    scanStatus, @"scan_status",
    userDecision, @"user_decision",
    firstSeenAt, @"first_seen_at",
    lastSeenAt, @"last_seen_at",
    lastScannedAt, @"last_scanned_at",
    nil];
  if ([validationError length] > 0U) {
    [dictionary setObject:validationError forKey:@"validation_error"];
  }
  if ([scanRoot length] > 0U) {
    [dictionary setObject:scanRoot forKey:@"scan_root"];
  }
  if ([appGroupKey length] > 0U) {
    [dictionary setObject:appGroupKey forKey:@"app_group_key"];
  }
  if ([appName length] > 0U) {
    [dictionary setObject:appName forKey:@"app_name"];
  }
  if ([appBundleId length] > 0U) {
    [dictionary setObject:appBundleId forKey:@"app_bundle_id"];
  }
  if ([appContainerPath length] > 0U) {
    [dictionary setObject:appContainerPath forKey:@"app_container_path"];
  }
  if ([appBundlePath length] > 0U) {
    [dictionary setObject:appBundlePath forKey:@"app_bundle_path"];
  }
  if ([appSource length] > 0U) {
    [dictionary setObject:appSource forKey:@"app_source"];
  }
  if ([originKind length] > 0U) {
    [dictionary setObject:originKind forKey:@"origin_kind"];
  }
  if ([locationTail length] > 0U) {
    [dictionary setObject:locationTail forKey:@"location_tail"];
  }
  if (hiddenOverride != nil) {
    [dictionary setObject:hiddenOverride forKey:@"hidden_override"];
  }
  if ([hiddenReason length] > 0U) {
    [dictionary setObject:hiddenReason forKey:@"hidden_reason"];
  }

  return dictionary;
}

- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                   savingResultsToCatalogWithError:(NSError **)error
{
  return [self scanDirectoryForSQLiteDatabasesAtPath:path
                                            scanMode:FileScannerDatabaseScanModeFull
                     savingResultsToCatalogWithError:error];
}

- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                                          scanMode:(FileScannerDatabaseScanMode)scanMode
                   savingResultsToCatalogWithError:(NSError **)error
{
  if (![self scanAndSaveDatabasesAtPath:path scanMode:scanMode error:error]) return nil;
  return [self catalogedSQLiteDatabasesWithError:error];
}

- (BOOL)scanAndSaveDatabasesAtPath:(NSString *)path
  scanMode:(FileScannerDatabaseScanMode)scanMode error:(NSError **)error
{
  /* Legacy entry point now acknowledges service acceptance. Results arrive through catalog notifications. */
  return [FileScanner beginDatabaseCatalogScanAtPath:path scanMode:scanMode error:error];
}

- (NSArray *)catalogedSQLiteDatabasesWithError:(NSError **)error
{
  NSString *databasePath;
  strappy_discovered_database_record_list list;
  NSMutableArray *rows;
  char *strappyError;
  size_t index;

  if (![StrappySession initializeSessionStoreWithError:error]) {
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  strappy_discovered_database_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_db_list_discovered_databases([databasePath UTF8String],
                                            &list,
                                            &strappyError)) {
    if (error != NULL) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return nil;
  }

  rows = [NSMutableArray arrayWithCapacity:list.count];
  for (index = 0U; index < list.count; index++) {
    NSDictionary *row;

    row = [FileScanner dictionaryFromDiscoveredDatabaseRecord:&list.records[index]];
    if (row != nil) {
      [rows addObject:row];
    }
  }

  strappy_discovered_database_record_list_destroy(&list);
  return rows;
}

- (NSDictionary *)catalogSnapshotMatchingSearch:(NSString *)search
  showHidden:(BOOL)showHidden sortDescriptors:(NSArray *)descriptors error:(NSError **)error
{
  FileScannerCatalogRows *rows = [self catalogRowsMatchingSearch:search showHidden:showHidden sortDescriptors:descriptors error:error];
  NSArray *sections = rows ? [rows applicationSectionsWithError:error] : nil;
  if (!sections) return nil;
  if ([rows count]) (void)[rows objectAtIndex:0];
  if ([rows readError]) { if (error) *error = [rows readError]; return nil; }
  return [NSDictionary dictionaryWithObjectsAndKeys:rows,@"rows",sections,@"sections",nil];
}

- (FileScannerCatalogRows *)catalogRowsMatchingSearch:(NSString *)search
  showHidden:(BOOL)showHidden sortDescriptors:(NSArray *)descriptors error:(NSError **)error
{
  if (![StrappySession initializeSessionStoreWithError:error]) return nil;
  return [[[FileScannerCatalogRows alloc] initWithPath:[StrappySession sessionsDatabasePath]
    search:search showHidden:showHidden descriptors:descriptors error:error] autorelease];
}

- (BOOL)setCatalogedDatabasesAllowed:(BOOL)allowed identifiers:(NSArray *)identifiers error:(NSError **)error
{
  NSEnumerator *enumerator = [identifiers objectEnumerator];
  NSNumber *identifier;
  while ((identifier = [enumerator nextObject]) != nil)
    if (![self setCatalogedDatabaseAllowed:allowed forCatalogIdentifier:identifier error:error]) return NO;
  return YES;
}

- (BOOL)setCatalogedDatabaseAllowed:(BOOL)allowed
               forCatalogIdentifier:(NSNumber *)catalogIdentifier
                              error:(NSError **)error
{
  cJSON *command = cJSON_CreateObject();
  cJSON_AddStringToObject(command,"action","approve_database");
  cJSON_AddStringToObject(command,"id",[[catalogIdentifier stringValue] UTF8String]);
  cJSON_AddStringToObject(command,"value",allowed ? "1" : "0");
  return FileScannerCommand(STRAPPY_IPC_MUTATE,command,error);
}

- (BOOL)setCatalogedDatabaseHidden:(BOOL)hidden
               forCatalogIdentifier:(NSNumber *)catalogIdentifier
                              error:(NSError **)error
{
  cJSON *command = cJSON_CreateObject();
  cJSON_AddStringToObject(command,"action","hide_database");
  cJSON_AddStringToObject(command,"id",[[catalogIdentifier stringValue] UTF8String]);
  cJSON_AddStringToObject(command,"value",hidden ? "1" : "0");
  return FileScannerCommand(STRAPPY_IPC_MUTATE,command,error);
}

@end

static NSString *FileScannerCatalogString(const char *value)
{
  NSString *string = value != NULL ? [NSString stringWithUTF8String:value] : nil;
  return string != nil ? string : @"";
}

static NSString *FileScannerCatalogLocation(NSString *path)
{
  NSString *directory = [path stringByDeletingLastPathComponent];
  NSString *home = NSHomeDirectory();
  NSUInteger length = [home length];
  if ([directory length] == 0U || [directory isEqualToString:path]) return @"";
  if (length > 0U && [directory hasPrefix:home]) {
    if ([directory length] == length) return @"~";
    if ([directory characterAtIndex:length] == '/')
      return [@"~" stringByAppendingString:[directory substringFromIndex:length]];
  }
  return directory;
}

static char *FileScannerCatalogField(const char *kind, const char *pathValue,
  const char *nameValue, const char *groupValue)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSString *path = FileScannerCatalogString(pathValue);
  NSString *name = FileScannerCatalogString(nameValue);
  NSString *group = FileScannerCatalogString(groupValue);
  NSString *value = @"";
  char *result;
  if (!strcmp(kind,"name")) {
    value = [path lastPathComponent];
    if ([value length] == 0U) value = path;
  } else if (!strcmp(kind,"location")) {
    value = FileScannerCatalogLocation(path);
  } else if (!strcmp(kind,"application")) {
    value = [name length] > 0U ? name :
      ([group length] > 0U ? group : NSLocalizedString(@"Other",nil));
  } else if (!strcmp(kind,"group_key")) {
    value = [group length] > 0U ? group :
      [@"path:" stringByAppendingString:[FileScannerCatalogLocation(path) lowercaseString]];
  }
  result = strdup([value UTF8String]);
  [pool release];
  return result;
}

static int FileScannerCatalogCompare(const char *left, const char *right)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSComparisonResult result = [FileScannerCatalogString(left)
    caseInsensitiveCompare:FileScannerCatalogString(right)];
  [pool release];
  return result < 0 ? -1 : (result > 0 ? 1 : 0);
}

static int FileScannerCatalogContains(const char *text, const char *needle)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSRange range = [FileScannerCatalogString(text) rangeOfString:FileScannerCatalogString(needle)
    options:NSCaseInsensitiveSearch];
  int result = range.location != NSNotFound;
  [pool release];
  return result;
}

@interface FileScannerCatalogRange : NSArray {
  NSArray *source_;
  NSUInteger offset_;
  NSUInteger count_;
}
- (id)initWithSource:(NSArray *)source offset:(NSUInteger)offset count:(NSUInteger)count;
@end

@implementation FileScannerCatalogRange
- (id)initWithSource:(NSArray *)source offset:(NSUInteger)offset count:(NSUInteger)count
{
  if ((self = [super init])) {
    source_ = [source retain]; offset_ = offset; count_ = count;
  }
  return self;
}
- (NSUInteger)count { return count_; }
- (id)objectAtIndex:(NSUInteger)index
{
  if (index >= count_) [NSException raise:NSRangeException format:@"Catalog section index outside snapshot"];
  return [source_ objectAtIndex:offset_ + index];
}
- (id)copyWithZone:(NSZone *)zone { (void)zone; return [self retain]; }
- (void)dealloc { [source_ release]; [super dealloc]; }
@end

typedef struct FileScannerCatalogGroupContext {
  FileScannerCatalogRows *source;
  NSMutableArray *sections;
} FileScannerCatalogGroupContext;

static void FileScannerCatalogGroup(void *context, const char *name,
  const char *group, const char *bundle, size_t offset, size_t count, int disambiguate)
{
  FileScannerCatalogGroupContext *groups = context;
  NSString *title = FileScannerCatalogString(name);
  NSArray *rows = [[[FileScannerCatalogRange alloc] initWithSource:groups->source
    offset:(NSUInteger)offset count:(NSUInteger)count] autorelease];
  if (disambiguate && bundle != NULL && *bundle != '\0')
    title = [NSString stringWithFormat:@"%@ (%@)",title,FileScannerCatalogString(bundle)];
  [groups->sections addObject:[NSDictionary dictionaryWithObjectsAndKeys:
    title,@"title",FileScannerCatalogString(group),@"app_group_key",rows,@"rows",nil]];
}

@implementation FileScannerCatalogRows
- (id)initWithPath:(NSString *)path search:(NSString *)search showHidden:(BOOL)showHidden
  descriptors:(NSArray *)descriptors error:(NSError **)error
{
  strappy_catalog_reader *reader = NULL;
  strappy_catalog_sort sort[12];
  strappy_catalog_text text = { FileScannerCatalogField,FileScannerCatalogCompare,FileScannerCatalogContains };
  char *message = NULL;
  NSUInteger index;
  self = [super init];
  if (self == nil) return nil;
  if ([descriptors count] > 12U) {
    if (error != NULL) *error = [FileScanner errorFromCString:"Too many catalog sort keys."];
    [self release]; return nil;
  }
  for (index = 0U; index < [descriptors count]; index++) {
    NSSortDescriptor *descriptor = [descriptors objectAtIndex:index];
    sort[index].key = [[descriptor key] UTF8String];
    sort[index].ascending = [descriptor ascending] ? 1 : 0;
  }
  if (!strappy_db_catalog_open([path fileSystemRepresentation],[search UTF8String],
      showHidden ? 1 : 0,sort,(size_t)[descriptors count],&text,&reader,&message)) {
    if (error != NULL) *error = [FileScanner errorFromCString:message];
    strappy_free_string(message); [self release]; return nil;
  }
  reader_ = reader;
  pages_ = [[NSMutableDictionary alloc] init];
  pageOrder_ = [[NSMutableArray alloc] init];
  return self;
}
- (void)dealloc
{
  strappy_db_catalog_close(reader_);
  [pages_ release]; [pageOrder_ release]; [readError_ release]; [super dealloc];
}
- (id)copyWithZone:(NSZone *)zone { (void)zone; return [self retain]; }
- (NSUInteger)count { return (NSUInteger)strappy_db_catalog_count(reader_); }
- (NSUInteger)totalCount { return (NSUInteger)strappy_db_catalog_total_count(reader_); }
- (NSUInteger)allowedCount { return (NSUInteger)strappy_db_catalog_allowed_count(reader_); }
- (NSUInteger)hiddenCount { return (NSUInteger)strappy_db_catalog_hidden_count(reader_); }
- (NSError *)readError { return readError_; }
- (void)recordReadError:(char *)message
{
  if (readError_ == nil) {
    readError_ = [[FileScanner errorFromCString:message] retain];
    [[NSNotificationQueue defaultQueue] enqueueNotification:
      [NSNotification notificationWithName:FileScannerCatalogReadFailedNotification object:self
        userInfo:[NSDictionary dictionaryWithObject:readError_ forKey:@"error"]]
      postingStyle:NSPostASAP];
  }
  strappy_free_string(message);
}
- (NSUInteger)indexForCatalogIdentifier:(NSNumber *)identifier
{
  size_t index;
  char *message = NULL;
  if (![identifier isKindOfClass:[NSNumber class]]) return NSNotFound;
  if (!strappy_db_catalog_index(reader_,[identifier longLongValue],&index,&message)) {
    [self recordReadError:message]; return NSNotFound;
  }
  return index == (size_t)-1 ? NSNotFound : (NSUInteger)index;
}
- (BOOL)filterWithSearch:(NSString *)search showHidden:(BOOL)showHidden
  sortDescriptors:(NSArray *)descriptors error:(NSError **)error
{
  strappy_catalog_sort sort[12];
  NSUInteger index;
  char *message = NULL;
  if ([descriptors count] > 12U) {
    if (error != NULL) *error = [FileScanner errorFromCString:"Too many catalog sort keys."];
    return NO;
  }
  for (index = 0U; index < [descriptors count]; index++) {
    NSSortDescriptor *descriptor = [descriptors objectAtIndex:index];
    sort[index].key = [[descriptor key] UTF8String];
    sort[index].ascending = [descriptor ascending] ? 1 : 0;
  }
  if (!strappy_db_catalog_query(reader_,[search UTF8String],showHidden ? 1 : 0,
      sort,(size_t)[descriptors count],&message)) {
    if (error != NULL) *error = [FileScanner errorFromCString:message];
    strappy_free_string(message); return NO;
  }
  [pages_ removeAllObjects]; [pageOrder_ removeAllObjects];
  [readError_ release]; readError_ = nil;
  return YES;
}
- (NSArray *)applicationSectionsWithError:(NSError **)error
{
  FileScannerCatalogGroupContext groups;
  char *message = NULL;
  groups.source = self;
  groups.sections = [NSMutableArray array];
  if (!strappy_db_catalog_groups(reader_,FileScannerCatalogGroup,&groups,&message)) {
    if (error != NULL) *error = [FileScanner errorFromCString:message];
    strappy_free_string(message); return nil;
  }
  return groups.sections;
}
- (id)objectAtIndex:(NSUInteger)index
{
  NSUInteger offset = (index / 32U) * 32U;
  NSNumber *key = [NSNumber XP_numberWithUnsignedInteger:offset];
  NSArray *page = [pages_ objectForKey:key];
  if (index >= [self count]) [NSException raise:NSRangeException format:@"Catalog index outside snapshot"];
  if (page == nil && readError_ == nil) {
    strappy_discovered_database_record_list records;
    char *message = NULL;
    if (!strappy_db_catalog_page(reader_,(size_t)offset,&records,&message)) {
      [self recordReadError:message];
    } else {
      NSMutableArray *rows = [NSMutableArray arrayWithCapacity:records.count];
      size_t row;
      for (row = 0U; row < records.count; row++)
        [rows addObject:[FileScanner dictionaryFromDiscoveredDatabaseRecord:&records.records[row]]];
      strappy_discovered_database_record_list_destroy(&records);
      if ([pageOrder_ count] >= 4U) {
        [pages_ removeObjectForKey:[pageOrder_ objectAtIndex:0]];
        [pageOrder_ removeObjectAtIndex:0];
      }
      page = rows;
      [pages_ setObject:page forKey:key];
    }
  }
  if (page != nil) {
    [pageOrder_ removeObject:key]; [pageOrder_ addObject:key];
    if (index - offset < [page count]) return [page objectAtIndex:index - offset];
  }
  /* No catalog ID: an unreadable row cannot accidentally authorize a database. */
  return [NSDictionary dictionaryWithObject:NSLocalizedString(@"Rows could not be loaded.",nil) forKey:@"path"];
}
@end
