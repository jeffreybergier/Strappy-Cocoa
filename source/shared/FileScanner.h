#import <Foundation/Foundation.h>

extern NSString * const FileScannerDatabaseCatalogScanDidStartNotification;
extern NSString * const FileScannerDatabaseCatalogScanDidFinishNotification;
extern NSString * const FileScannerDatabaseCatalogDidChangeNotification;

typedef enum FileScannerDatabaseScanMode {
  /* Exhaustive and authoritative: unseen catalog locations become inactive. */
  FileScannerDatabaseScanModeFull = 0,
  /* Filename-filtered and incremental: prior full-scan locations remain. */
  FileScannerDatabaseScanModeQuick = 1
} FileScannerDatabaseScanMode;

extern NSString * const FileScannerCatalogReadFailedNotification;

@interface FileScannerCatalogRows : NSArray {
 @private
  void *reader_;
  NSMutableDictionary *pages_;
  NSMutableArray *pageOrder_;
  NSError *readError_;
}
/* Reorders the existing snapshot atomically and clears its row cache. */
- (BOOL)filterWithSearch:(NSString *)search showHidden:(BOOL)showHidden
  sortDescriptors:(NSArray *)descriptors error:(NSError **)error;
- (NSArray *)applicationSectionsWithError:(NSError **)error;
- (NSUInteger)indexForCatalogIdentifier:(NSNumber *)identifier;
- (NSError *)readError;
@end

@interface FileScanner : NSObject

+ (FileScanner *)sharedScanner;
+ (BOOL)isDatabaseCatalogScanInFlight;
+ (BOOL)beginDatabaseCatalogScanAtPath:(NSString *)path
                                 error:(NSError **)error;
+ (BOOL)beginDatabaseCatalogScanAtPath:(NSString *)path
                              scanMode:(FileScannerDatabaseScanMode)scanMode
                                 error:(NSError **)error;
- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                   savingResultsToCatalogWithError:(NSError **)error;
- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                                          scanMode:(FileScannerDatabaseScanMode)scanMode
                   savingResultsToCatalogWithError:(NSError **)error;
- (NSArray *)catalogedSQLiteDatabasesWithError:(NSError **)error;
/* Sort descriptors use application, group_key, name, location, size, allowed,
 * hidden, or database_priority. All filtering/order belongs to this snapshot. */
- (FileScannerCatalogRows *)catalogRowsMatchingSearch:(NSString *)search
  showHidden:(BOOL)showHidden sortDescriptors:(NSArray *)descriptors error:(NSError **)error;
- (BOOL)scanAndSaveDatabasesAtPath:(NSString *)path
  scanMode:(FileScannerDatabaseScanMode)scanMode error:(NSError **)error;
- (BOOL)setCatalogedDatabaseAllowed:(BOOL)allowed
                forCatalogIdentifier:(NSNumber *)catalogIdentifier
                               error:(NSError **)error;
- (BOOL)setCatalogedDatabaseHidden:(BOOL)hidden
               forCatalogIdentifier:(NSNumber *)catalogIdentifier
                              error:(NSError **)error;

@end
