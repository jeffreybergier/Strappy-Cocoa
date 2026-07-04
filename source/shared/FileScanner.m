#import "FileScanner.h"

#import "StrappySession.h"
#import "strappy_core.h"
#import "strappy_db.h"
#import "strappy_file_scanner.h"

#include <string.h>

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

  dictionary = [NSMutableDictionary dictionaryWithObjectsAndKeys:
    path, @"path",
    size, @"size",
    modifiedAt, @"modified_at",
    device, @"device",
    inode, @"inode",
    isValidSQLite, @"is_valid_sqlite",
    nil];
  if ([validationError length] > 0U) {
    [dictionary setObject:validationError forKey:@"validation_error"];
  }

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
  NSString *assistantDatabaseId;
  NSString *path;
  NSString *validationError;
  NSString *scanStatus;
  NSString *userDecision;
  NSString *scanRoot;
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
  assistantDatabaseId =
    [FileScanner stringFromCStringOrEmpty:record->assistant_database_id];
  path = [FileScanner stringFromCStringOrEmpty:record->path];
  validationError =
    [FileScanner stringFromCStringOrEmpty:record->validation_error];
  scanStatus = [FileScanner stringFromCStringOrEmpty:record->scan_status];
  userDecision = [FileScanner stringFromCStringOrEmpty:record->user_decision];
  scanRoot = [FileScanner stringFromCStringOrEmpty:record->scan_root];
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
  strappy_file_scanner_options_init(&options);
  options.root_path = [path fileSystemRepresentation];
  options.validate_candidates = 1;

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

@end
