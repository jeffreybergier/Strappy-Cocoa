#import <Foundation/Foundation.h>

@interface FileScanner : NSObject

+ (FileScanner *)sharedScanner;
- (NSArray *)scanHomeDirectoryForSQLiteDatabasesWithError:(NSError **)error;
- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                                             error:(NSError **)error;

@end
