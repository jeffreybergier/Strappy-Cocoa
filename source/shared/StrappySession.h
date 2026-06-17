#import <Foundation/Foundation.h>

@interface StrappySession : NSObject

+ (void)bootstrapProcessWithCACertPath:(NSString *)caCertPath;
+ (NSString *)sessionsDatabasePath;
+ (BOOL)initializeSessionStoreWithError:(NSError **)error;
+ (NSArray *)sessionSummariesWithError:(NSError **)error;
+ (NSArray *)messagesForSessionIdentifier:(NSNumber *)sessionIdentifier
                                    error:(NSError **)error;
+ (NSString *)submitPromptSynchronously:(NSString *)prompt error:(NSError **)error;
+ (NSDictionary *)submitPromptAndReturnSessionSynchronously:(NSString *)prompt
                                                      error:(NSError **)error;

@end
