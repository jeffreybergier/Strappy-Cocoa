#import "FileScanner.h"

#import "strappy_core.h"
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

@end
