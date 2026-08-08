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

@interface FileScanner : NSObject

+ (FileScanner *)sharedScanner;
+ (BOOL)isDatabaseCatalogScanInFlight;
+ (BOOL)beginDatabaseCatalogScanAtPath:(NSString *)path
                                 error:(NSError **)error;
+ (BOOL)beginDatabaseCatalogScanAtPath:(NSString *)path
                              scanMode:(FileScannerDatabaseScanMode)scanMode
                                 error:(NSError **)error;
- (NSArray *)scanHomeDirectoryForSQLiteDatabasesWithError:(NSError **)error;
- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                                             error:(NSError **)error;
- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                                          scanMode:(FileScannerDatabaseScanMode)scanMode
                                             error:(NSError **)error;
- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                   savingResultsToCatalogWithError:(NSError **)error;
- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                                          scanMode:(FileScannerDatabaseScanMode)scanMode
                   savingResultsToCatalogWithError:(NSError **)error;
- (NSArray *)catalogedSQLiteDatabasesWithError:(NSError **)error;
- (BOOL)setCatalogedDatabaseAllowed:(BOOL)allowed
                forCatalogIdentifier:(NSNumber *)catalogIdentifier
                               error:(NSError **)error;
- (BOOL)setCatalogedDatabaseHidden:(BOOL)hidden
               forCatalogIdentifier:(NSNumber *)catalogIdentifier
                              error:(NSError **)error;

@end
