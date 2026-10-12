#import <AIPagedRows.h>
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

@interface FileScannerCatalogRows : AIPagedRows {
 @private
  void *reader_;
  NSError *readError_;
}
/* Reorders the existing snapshot atomically and clears its row cache. */
- (BOOL)filterWithSearch:(NSString *)search showHidden:(BOOL)showHidden
  sortDescriptors:(NSArray *)descriptors error:(NSError **)error;
- (NSArray *)applicationSectionsWithError:(NSError **)error;
- (NSUInteger)indexForCatalogIdentifier:(NSNumber *)identifier;
- (NSError *)readError;
- (NSUInteger)totalCount;
- (NSUInteger)allowedCount;
- (NSUInteger)hiddenCount;
@end

@interface FileScanner : NSObject

+ (FileScanner *)sharedScanner;
/* Service monitor delivery on the UI thread. */
+ (void)serviceWorkDidChange;
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
/* Create and warm a private snapshot on a query worker before UI handoff. */
- (NSDictionary *)catalogSnapshotMatchingSearch:(NSString *)search
  showHidden:(BOOL)showHidden sortDescriptors:(NSArray *)descriptors error:(NSError **)error;
- (FileScannerCatalogRows *)catalogRowsMatchingSearch:(NSString *)search
  showHidden:(BOOL)showHidden sortDescriptors:(NSArray *)descriptors error:(NSError **)error;
- (BOOL)scanAndSaveDatabasesAtPath:(NSString *)path
  scanMode:(FileScannerDatabaseScanMode)scanMode error:(NSError **)error;
- (BOOL)setCatalogedDatabasesAllowed:(BOOL)allowed identifiers:(NSArray *)identifiers error:(NSError **)error;
- (BOOL)setCatalogedDatabaseAllowed:(BOOL)allowed
                forCatalogIdentifier:(NSNumber *)catalogIdentifier
                               error:(NSError **)error;
- (BOOL)setCatalogedDatabaseHidden:(BOOL)hidden
               forCatalogIdentifier:(NSNumber *)catalogIdentifier
                              error:(NSError **)error;

@end
