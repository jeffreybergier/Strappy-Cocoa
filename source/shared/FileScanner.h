#import <Foundation/Foundation.h>

extern NSString * const FileScannerDatabaseCatalogScanDidStartNotification;
extern NSString * const FileScannerDatabaseCatalogScanDidFinishNotification;
extern NSString * const FileScannerDatabaseCatalogDidChangeNotification;

@interface FileScanner : NSObject

+ (FileScanner *)sharedScanner;
+ (BOOL)isDatabaseCatalogScanInFlight;
+ (BOOL)beginDatabaseCatalogScanAtPath:(NSString *)path
                                 error:(NSError **)error;
- (NSArray *)scanHomeDirectoryForSQLiteDatabasesWithError:(NSError **)error;
- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                                             error:(NSError **)error;
- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                   savingResultsToCatalogWithError:(NSError **)error;
- (NSArray *)catalogedSQLiteDatabasesWithError:(NSError **)error;
- (BOOL)setCatalogedDatabaseAllowed:(BOOL)allowed
                forCatalogIdentifier:(NSNumber *)catalogIdentifier
                               error:(NSError **)error;
- (BOOL)setCatalogedDatabaseHidden:(BOOL)hidden
               forCatalogIdentifier:(NSNumber *)catalogIdentifier
                              error:(NSError **)error;

@end
