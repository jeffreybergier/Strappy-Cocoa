#import <Foundation/Foundation.h>

@interface FileScanner : NSObject

+ (FileScanner *)sharedScanner;
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
