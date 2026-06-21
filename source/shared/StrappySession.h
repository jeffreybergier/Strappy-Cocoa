#import <Foundation/Foundation.h>

@protocol StrappySessionStreamDelegate
- (void)strappySessionStreamDidReceiveContentDelta:(NSDictionary *)delta;
- (void)strappySessionStreamDidReceiveReasoningDelta:(NSDictionary *)delta;
- (void)strappySessionStreamDidReceiveToolCall:(NSDictionary *)event;
- (void)strappySessionStreamDidReceiveToolResult:(NSDictionary *)event;
- (void)strappySessionStreamDidReceiveToolError:(NSDictionary *)event;
- (void)strappySessionStreamDidStartTurn:(NSDictionary *)event;
- (void)strappySessionStreamDidFinishTurn:(NSDictionary *)event;
@end

@interface StrappySession : NSObject

+ (void)bootstrapProcessWithCACertPath:(NSString *)caCertPath;
+ (NSString *)sessionsDatabasePath;
+ (BOOL)initializeSessionStoreWithError:(NSError **)error;
+ (NSDictionary *)createSessionWithError:(NSError **)error;
+ (NSArray *)sessionSummariesWithError:(NSError **)error;
+ (NSDictionary *)sessionSummaryForSessionIdentifier:(NSNumber *)sessionIdentifier
                                               error:(NSError **)error;
+ (NSArray *)messagesForSessionIdentifier:(NSNumber *)sessionIdentifier
                                    error:(NSError **)error;
+ (NSString *)submitPromptSynchronously:(NSString *)prompt error:(NSError **)error;
+ (NSDictionary *)submitPromptAndReturnSessionSynchronously:(NSString *)prompt
                                                      error:(NSError **)error;
+ (NSDictionary *)submitPrompt:(NSString *)prompt
           inSessionIdentifier:(NSNumber *)sessionIdentifier
                         error:(NSError **)error;
+ (NSDictionary *)submitPromptStreaming:(NSString *)prompt
                    inSessionIdentifier:(NSNumber *)sessionIdentifier
                                context:(NSDictionary *)context
                               delegate:(id<StrappySessionStreamDelegate>)delegate
                                  error:(NSError **)error;

@end
