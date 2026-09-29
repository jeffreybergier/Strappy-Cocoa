#import "FileScanner.h"

#import "StrappySession.h"
#import "XPFoundation.h"
#import "strappy_core.h"
#import "strappy_db.h"
#import "strappy_file_scanner.h"
#include <stdlib.h>
#include <string.h>

NSString * const FileScannerDatabaseCatalogScanDidStartNotification =
  @"FileScannerDatabaseCatalogScanDidStartNotification";
NSString * const FileScannerDatabaseCatalogScanDidFinishNotification =
  @"FileScannerDatabaseCatalogScanDidFinishNotification";
NSString * const FileScannerDatabaseCatalogDidChangeNotification =
  @"FileScannerDatabaseCatalogDidChangeNotification";

NSString * const FileScannerCatalogReadFailedNotification = @"FileScannerCatalogReadFailedNotification";
static BOOL FileScannerCatalogUpdatePending = NO;

@interface FileScannerCatalogRows ()
- (id)initWithPath:(NSString *)path search:(NSString *)search showHidden:(BOOL)showHidden
  descriptors:(NSArray *)descriptors error:(NSError **)error;
@end

static BOOL FileScannerDatabaseCatalogScanInFlight = NO;
static const size_t StrappyFileScannerCatalogBatchSize = 100U;

static strappy_file_scanner_platform_profile
StrappyFileScannerPlatformProfile(void)
{
  XPPlatformFamily family;

  family = [[NSProcessInfo processInfo] XP_platformFamily];
  if (family == XPPlatformFamilyIOS) {
    return STRAPPY_FILE_SCANNER_PLATFORM_IOS;
  }
  if (family == XPPlatformFamilyMacOS) {
    return STRAPPY_FILE_SCANNER_PLATFORM_MACOS;
  }
  return STRAPPY_FILE_SCANNER_PLATFORM_GENERIC;
}

typedef struct StrappyFileScannerCatalogBatchContext {
  NSString *databasePath;
  const char *scanRoot;
} StrappyFileScannerCatalogBatchContext;

@interface FileScanner ()

+ (NSError *)errorFromCString:(char *)message;
+ (void)databaseCatalogDidChange:(NSDictionary *)result;
+ (void)databaseCatalogScanInBackground:(NSDictionary *)request;
+ (NSDictionary *)dictionaryFromDiscoveredDatabaseRecord:(const strappy_discovered_database_record *)record;
+ (void)queueCatalogUpdate;
+ (void)deliverCatalogUpdate;

@end

static int StrappyFileScannerSaveCatalogBatch(
  strappy_file_scanner_record_list *list,
  void *userData,
  char **error_out)
{
  StrappyFileScannerCatalogBatchContext *context;
  context = (StrappyFileScannerCatalogBatchContext *)userData;
  if (context == NULL) {
    strappy_set_error(error_out, "Database scan batch context is missing.");
    return 0;
  }

  if (!strappy_file_scanner_save_discovered_database_batch(
        [context->databasePath UTF8String],
        list,
        context->scanRoot,
        error_out)) {
    return 0;
  }

  [FileScanner queueCatalogUpdate];
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
  return [self beginDatabaseCatalogScanAtPath:path
                                     scanMode:FileScannerDatabaseScanModeFull
                                        error:error];
}

+ (BOOL)beginDatabaseCatalogScanAtPath:(NSString *)path
                              scanMode:(FileScannerDatabaseScanMode)scanMode
                                 error:(NSError **)error
{
  NSDictionary *scanRequest;
  NSDictionary *userInfo;

  if (![path isKindOfClass:[NSString class]] || ([path length] == 0U)) {
    if (error != NULL) {
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
      if (error != NULL) {
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

  scanMode = (scanMode == FileScannerDatabaseScanModeQuick) ?
    FileScannerDatabaseScanModeQuick : FileScannerDatabaseScanModeFull;
  scanRequest = [[NSDictionary alloc] initWithObjectsAndKeys:
    path, @"path",
    [NSNumber XP_numberWithInteger:(XPInteger)scanMode], @"scan_mode",
    nil];

  [[NSNotificationCenter defaultCenter]
    postNotificationName:FileScannerDatabaseCatalogScanDidStartNotification
                  object:self
                userInfo:scanRequest];

  [NSThread detachNewThreadSelector:@selector(databaseCatalogScanInBackground:)
                           toTarget:self
                         withObject:scanRequest];
  [scanRequest release];
  return YES;
}

+ (void)databaseCatalogScanInBackground:(NSDictionary *)request
{
  NSAutoreleasePool *pool;
  NSError *error;
  BOOL success;
  NSMutableDictionary *result;
  NSString *message;
  NSString *path;
  NSNumber *scanModeNumber;
  FileScannerDatabaseScanMode scanMode;

  pool = [[NSAutoreleasePool alloc] init];
  path = [request objectForKey:@"path"];
  scanModeNumber = [request objectForKey:@"scan_mode"];
  scanMode = ([scanModeNumber isKindOfClass:[NSNumber class]] &&
              ([scanModeNumber XP_integerValue] ==
               FileScannerDatabaseScanModeQuick)) ?
    FileScannerDatabaseScanModeQuick : FileScannerDatabaseScanModeFull;
  error = nil;
  success = [[FileScanner sharedScanner] scanAndSaveDatabasesAtPath:path
    scanMode:scanMode error:&error];

  result = [[NSMutableDictionary alloc] init];
  if ([path isKindOfClass:[NSString class]]) {
    [result setObject:path forKey:@"path"];
  }
  [result setObject:[NSNumber XP_numberWithInteger:(XPInteger)scanMode]
             forKey:@"scan_mode"];
  if (!success) {
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

  [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(deliverCatalogUpdate) object:nil];
  @synchronized(self) { FileScannerCatalogUpdatePending = NO; }
  /* Publish committed batches even when a later part of the scan failed. */
  [self databaseCatalogDidChange:userInfo];

  [[NSNotificationCenter defaultCenter]
    postNotificationName:FileScannerDatabaseCatalogScanDidFinishNotification
                  object:self
                userInfo:userInfo];
  [userInfo release];
}

+ (void)queueCatalogUpdate
{
  @synchronized(self) {
    if (FileScannerCatalogUpdatePending) return;
    FileScannerCatalogUpdatePending = YES;
  }
  [self performSelectorOnMainThread:@selector(scheduleCatalogUpdate)
    withObject:nil waitUntilDone:NO];
}

+ (void)scheduleCatalogUpdate
{
  @synchronized(self) { if (!FileScannerCatalogUpdatePending) return; }
  [self performSelector:@selector(deliverCatalogUpdate) withObject:nil afterDelay:0.25];
}

+ (void)deliverCatalogUpdate
{
  @synchronized(self) { FileScannerCatalogUpdatePending = NO; }
  [self databaseCatalogDidChange:nil];
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
  NSString *databasePath;
  StrappyFileScannerCatalogBatchContext batchContext;
  strappy_file_scanner_options options;
  strappy_file_scanner_record_list list;
  char *strappyError;

  if (![path isKindOfClass:[NSString class]] || ([path length] == 0U)) {
    if (error != NULL) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Scan path is empty.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"FileScannerErrorDomain"
                                   code:2
                               userInfo:userInfo];
    }
    return NO;
  }

  if (![StrappySession initializeSessionStoreWithError:error]) {
    return NO;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  batchContext.databasePath = databasePath;
  batchContext.scanRoot = [path fileSystemRepresentation];

  strappy_file_scanner_options_init(&options);
  options.root_path = batchContext.scanRoot;
  options.platform_profile = StrappyFileScannerPlatformProfile();
  options.validate_candidates = 1;
  options.use_filename_filter =
    (scanMode == FileScannerDatabaseScanModeQuick) ? 1 : 0;
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
    if (error != NULL) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    strappy_file_scanner_record_list_destroy(&list);
    return NO;
  }

  strappy_file_scanner_record_list_destroy(&list);
  return YES;
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

- (FileScannerCatalogRows *)catalogRowsMatchingSearch:(NSString *)search
  showHidden:(BOOL)showHidden sortDescriptors:(NSArray *)descriptors error:(NSError **)error
{
  if (![StrappySession initializeSessionStoreWithError:error]) return nil;
  return [[[FileScannerCatalogRows alloc] initWithPath:[StrappySession sessionsDatabasePath]
    search:search showHidden:showHidden descriptors:descriptors error:error] autorelease];
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
    if (error != NULL) {
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
    if (error != NULL) {
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
    if (error != NULL) {
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
    if (error != NULL) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return NO;
  }

  return YES;
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
