#import "FileScanner.h"

#import "StrappySession.h"
#import "strappy_core.h"
#import "strappy_db.h"
#import "strappy_file_scanner.h"

#include <string.h>

NSString * const FileScannerDatabaseCatalogScanDidStartNotification =
  @"FileScannerDatabaseCatalogScanDidStartNotification";
NSString * const FileScannerDatabaseCatalogScanDidFinishNotification =
  @"FileScannerDatabaseCatalogScanDidFinishNotification";
NSString * const FileScannerDatabaseCatalogDidChangeNotification =
  @"FileScannerDatabaseCatalogDidChangeNotification";

static BOOL FileScannerDatabaseCatalogScanInFlight = NO;
static const size_t StrappyFileScannerCatalogBatchSize = 100U;

typedef struct StrappyFileScannerCatalogBatchContext {
  NSString *databasePath;
  NSString *rootPath;
  const char *scanRoot;
} StrappyFileScannerCatalogBatchContext;

@interface FileScanner ()

+ (NSError *)errorFromCString:(char *)message;
+ (void)databaseCatalogDidChange:(NSDictionary *)result;

@end

static void StrappyFileScannerAddCString(NSMutableDictionary *dictionary,
                                         NSString *key,
                                         const char *value)
{
  NSString *string;

  if ((dictionary == nil) || ![key isKindOfClass:[NSString class]] ||
      (value == NULL) || (value[0] == '\0')) {
    return;
  }

  string = [NSString stringWithUTF8String:value];
  if ([string length] > 0U) {
    [dictionary setObject:string forKey:key];
  }
}

static int StrappyFileScannerSaveCatalogBatch(
  strappy_file_scanner_record_list *list,
  void *userData,
  char **error_out)
{
  StrappyFileScannerCatalogBatchContext *context;
  NSMutableDictionary *result;
  NSAutoreleasePool *pool;
  NSArray *rows;

  context = (StrappyFileScannerCatalogBatchContext *)userData;
  if (context == NULL) {
    strappy_set_error(error_out, "Database scan batch context is missing.");
    return 0;
  }

  pool = [[NSAutoreleasePool alloc] init];
  if (!strappy_file_scanner_save_discovered_database_batch(
        [context->databasePath UTF8String],
        list,
        context->scanRoot,
        error_out)) {
    [pool release];
    return 0;
  }

  rows = [[FileScanner sharedScanner]
    catalogedSQLiteDatabasesWithError:nil];
  result = [[NSMutableDictionary alloc] init];
  if ([context->rootPath isKindOfClass:[NSString class]]) {
    [result setObject:context->rootPath forKey:@"path"];
  }
  if ([rows isKindOfClass:[NSArray class]]) {
    [result setObject:rows forKey:@"rows"];
  }
  if ([NSThread currentThread] == [NSThread mainThread]) {
    [FileScanner databaseCatalogDidChange:result];
  } else {
    [FileScanner performSelectorOnMainThread:@selector(databaseCatalogDidChange:)
                                  withObject:result
                               waitUntilDone:YES];
  }
  [result release];
  [pool release];
  return 1;
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
  NSString *scanPath;
  NSDictionary *userInfo;

  if (![path isKindOfClass:[NSString class]] || ([path length] == 0U)) {
    if (error != nil) {
      userInfo = [NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Scan path is empty.", nil)
                                             forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"FileScannerErrorDomain"
                                   code:2
                               userInfo:userInfo];
    }
    return NO;
  }

  @synchronized(self) {
    if (FileScannerDatabaseCatalogScanInFlight) {
      if (error != nil) {
        userInfo = [NSDictionary dictionaryWithObject:
          NSLocalizedString(@"Database scan is already running.", nil)
                                               forKey:NSLocalizedDescriptionKey];
        *error = [NSError errorWithDomain:@"FileScannerErrorDomain"
                                     code:3
                                 userInfo:userInfo];
      }
      return NO;
    }
    FileScannerDatabaseCatalogScanInFlight = YES;
  }

  [[NSNotificationCenter defaultCenter]
    postNotificationName:FileScannerDatabaseCatalogScanDidStartNotification
                  object:self
                userInfo:[NSDictionary dictionaryWithObject:path forKey:@"path"]];

  scanPath = [path copy];
  [NSThread detachNewThreadSelector:@selector(databaseCatalogScanInBackground:)
                           toTarget:self
                         withObject:scanPath];
  [scanPath release];
  return YES;
}

+ (void)databaseCatalogScanInBackground:(NSString *)path
{
  NSAutoreleasePool *pool;
  NSError *error;
  NSArray *rows;
  NSMutableDictionary *result;
  NSString *message;

  pool = [[NSAutoreleasePool alloc] init];
  error = nil;
  rows = [[FileScanner sharedScanner]
    scanDirectoryForSQLiteDatabasesAtPath:path
           savingResultsToCatalogWithError:&error];

  result = [[NSMutableDictionary alloc] init];
  if ([path isKindOfClass:[NSString class]]) {
    [result setObject:path forKey:@"path"];
  }
  if (rows != nil) {
    [result setObject:rows forKey:@"rows"];
  } else {
    message = [error localizedDescription];
    if ([message length] == 0U) {
      message = NSLocalizedString(@"Database scan failed.", nil);
    }
    [result setObject:message forKey:@"error"];
  }

  [self performSelectorOnMainThread:@selector(databaseCatalogScanDidFinish:)
                         withObject:result
                      waitUntilDone:NO];
  [result release];
  [pool release];
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

  if ([userInfo objectForKey:@"error"] == nil) {
    [self databaseCatalogDidChange:userInfo];
  }

  [[NSNotificationCenter defaultCenter]
    postNotificationName:FileScannerDatabaseCatalogScanDidFinishNotification
                  object:self
                userInfo:userInfo];
  [userInfo release];
}

+ (void)databaseCatalogDidChange:(NSDictionary *)result
{
  NSDictionary *userInfo;

  userInfo = [result isKindOfClass:[NSDictionary class]] ?
    result : [NSDictionary dictionary];
  [[NSNotificationCenter defaultCenter]
    postNotificationName:FileScannerDatabaseCatalogDidChangeNotification
                  object:self
                userInfo:userInfo];
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

+ (NSString *)stringFromFileSystemPath:(const char *)path
{
  NSFileManager *fileManager;

  if (path == NULL) {
    return nil;
  }

  fileManager = [NSFileManager defaultManager];
  return [fileManager stringWithFileSystemRepresentation:path
                                                  length:strlen(path)];
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

+ (NSDictionary *)dictionaryFromScannerRecord:
    (const strappy_file_scanner_record *)record
{
  NSString *path;
  NSString *validationError;
  NSNumber *size;
  NSNumber *modifiedAt;
  NSNumber *device;
  NSNumber *inode;
  NSNumber *isValidSQLite;
  NSNumber *hidden;
  NSMutableDictionary *dictionary;

  if (record == NULL) {
    return nil;
  }

  path = [FileScanner stringFromFileSystemPath:record->path];
  if (path == nil) {
    return nil;
  }

  validationError = nil;
  if (record->validation_error != NULL) {
    validationError = [NSString stringWithUTF8String:record->validation_error];
  }
  if (validationError == nil) {
    validationError = @"";
  }

  size = [NSNumber numberWithLongLong:record->size];
  modifiedAt = [NSNumber numberWithLongLong:record->modified_at];
  device = [NSNumber numberWithUnsignedLongLong:record->device];
  inode = [NSNumber numberWithUnsignedLongLong:record->inode];
  isValidSQLite = [NSNumber numberWithBool:(record->is_valid_sqlite ? YES : NO)];
  hidden = [NSNumber numberWithBool:(record->hidden ? YES : NO)];

  dictionary = [NSMutableDictionary dictionaryWithObjectsAndKeys:
    path, @"path",
    size, @"size",
    modifiedAt, @"modified_at",
    device, @"device",
    inode, @"inode",
    isValidSQLite, @"is_valid_sqlite",
    hidden, @"hidden",
    nil];
  if ([validationError length] > 0U) {
    [dictionary setObject:validationError forKey:@"validation_error"];
  }
  StrappyFileScannerAddCString(dictionary,
                               @"app_group_key",
                               record->app_group_key);
  StrappyFileScannerAddCString(dictionary, @"app_name", record->app_name);
  StrappyFileScannerAddCString(dictionary,
                               @"app_bundle_id",
                               record->app_bundle_id);
  StrappyFileScannerAddCString(dictionary,
                               @"app_container_path",
                               record->app_container_path);
  StrappyFileScannerAddCString(dictionary,
                               @"app_bundle_path",
                               record->app_bundle_path);
  StrappyFileScannerAddCString(dictionary, @"app_source", record->app_source);
  StrappyFileScannerAddCString(dictionary, @"origin_kind", record->origin_kind);
  StrappyFileScannerAddCString(dictionary,
                               @"location_tail",
                               record->location_tail);

  return dictionary;
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

  return dictionary;
}

- (NSArray *)scanHomeDirectoryForSQLiteDatabasesWithError:(NSError **)error
{
  NSString *homeDirectory;

  homeDirectory = NSHomeDirectory();
  return [self scanDirectoryForSQLiteDatabasesAtPath:homeDirectory
                                               error:error];
}

- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                                             error:(NSError **)error
{
  strappy_file_scanner_options options;
  strappy_file_scanner_record_list list;
  NSMutableArray *records;
  char *strappyError;
  size_t index;

  if (![path isKindOfClass:[NSString class]] || ([path length] == 0U)) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Scan path is empty.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"FileScannerErrorDomain"
                                   code:2
                               userInfo:userInfo];
    }
    return nil;
  }

  strappy_file_scanner_options_init(&options);
  options.root_path = [path fileSystemRepresentation];
  options.validate_candidates = 1;

  strappy_file_scanner_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_file_scanner_scan(&options, &list, &strappyError)) {
    if (error != nil) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    strappy_file_scanner_record_list_destroy(&list);
    return nil;
  }
  records = [NSMutableArray arrayWithCapacity:list.count];
  for (index = 0U; index < list.count; index++) {
    NSDictionary *record =
      [FileScanner dictionaryFromScannerRecord:&list.records[index]];
    if (record != nil) {
      [records addObject:record];
    }
  }

  strappy_file_scanner_record_list_destroy(&list);
  return records;
}

- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                   savingResultsToCatalogWithError:(NSError **)error
{
  NSString *databasePath;
  StrappyFileScannerCatalogBatchContext batchContext;
  strappy_file_scanner_options options;
  strappy_file_scanner_record_list list;
  char *strappyError;

  if (![path isKindOfClass:[NSString class]] || ([path length] == 0U)) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Scan path is empty.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"FileScannerErrorDomain"
                                   code:2
                               userInfo:userInfo];
    }
    return nil;
  }

  if (![StrappySession initializeSessionStoreWithError:error]) {
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  batchContext.databasePath = databasePath;
  batchContext.rootPath = path;
  batchContext.scanRoot = [path fileSystemRepresentation];

  strappy_file_scanner_options_init(&options);
  options.root_path = batchContext.scanRoot;
  options.validate_candidates = 1;
  options.record_batch_size = StrappyFileScannerCatalogBatchSize;
  options.record_batch_callback = StrappyFileScannerSaveCatalogBatch;
  options.record_batch_user_data = &batchContext;

  strappy_file_scanner_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_file_scanner_scan_and_save_discovered_databases(
        [databasePath UTF8String],
        &options,
        &list,
        &strappyError)) {
    if (error != nil) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    strappy_file_scanner_record_list_destroy(&list);
    return nil;
  }

  strappy_file_scanner_record_list_destroy(&list);
  return [self catalogedSQLiteDatabasesWithError:error];
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
    if (error != nil) {
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

- (BOOL)setCatalogedDatabaseAllowed:(BOOL)allowed
               forCatalogIdentifier:(NSNumber *)catalogIdentifier
                              error:(NSError **)error
{
  NSString *databasePath;
  const char *decision;
  char *strappyError;
  int ok;

  if (![catalogIdentifier isKindOfClass:[NSNumber class]] ||
      ([catalogIdentifier longLongValue] <= 0LL)) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Database catalog id is missing.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"FileScannerErrorDomain"
                                   code:6
                               userInfo:userInfo];
    }
    return NO;
  }

  if (![StrappySession initializeSessionStoreWithError:error]) {
    return NO;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  decision = (allowed ? "allowed" : "unknown");
  strappyError = NULL;
  ok = strappy_db_update_discovered_database_decision(
    [databasePath UTF8String],
    [catalogIdentifier longLongValue],
    decision,
    &strappyError);
  if (!ok) {
    if (error != nil) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return NO;
  }

  return YES;
}

- (BOOL)setCatalogedDatabaseHidden:(BOOL)hidden
               forCatalogIdentifier:(NSNumber *)catalogIdentifier
                              error:(NSError **)error
{
  NSString *databasePath;
  char *strappyError;
  int ok;

  if (![catalogIdentifier isKindOfClass:[NSNumber class]] ||
      ([catalogIdentifier longLongValue] <= 0LL)) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Database catalog id is missing.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"FileScannerErrorDomain"
                                   code:6
                               userInfo:userInfo];
    }
    return NO;
  }

  if (![StrappySession initializeSessionStoreWithError:error]) {
    return NO;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  strappyError = NULL;
  ok = strappy_db_update_discovered_database_hidden(
    [databasePath UTF8String],
    [catalogIdentifier longLongValue],
    hidden ? 1 : 0,
    &strappyError);
  if (!ok) {
    if (error != nil) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return NO;
  }

  return YES;
}

@end
