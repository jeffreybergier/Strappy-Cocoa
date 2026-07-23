#import <Foundation/Foundation.h>

extern NSString * const StrappySessionDidUpdateNotification;
extern NSString * const StrappySessionPromptDidStartNotification;
extern NSString * const StrappySessionPromptDidFinishNotification;
extern NSString * const StrappySessionStreamEventNotification;
extern NSString * const StrappySessionModelCatalogRefreshDidStartNotification;
extern NSString * const StrappySessionModelCatalogRefreshDidFinishNotification;
extern NSString * const StrappySessionModelCatalogDidChangeNotification;
extern NSString * const StrappySessionChangeKindKey;
extern NSString * const StrappySessionChangeKindActivity;
extern NSString * const StrappySessionChangeKindModel;
extern NSString * const StrappySessionChangeKindStreaming;
extern NSString * const StrappySessionChangeKindWebProvider;
extern NSString * const StrappySessionChangeKindBash;
extern NSString * const StrappySessionChangeKindWorkingDirectory;
extern NSString * const StrappySessionChangeKindAssistantSet;
extern NSString * const StrappyWebProviderNone;
extern NSString * const StrappyWebProviderAuto;
extern NSString * const StrappyWebProviderNative;
extern NSString * const StrappyWebProviderExa;
extern NSString * const StrappyWebProviderParallel;

@interface StrappySession : NSObject {
 @private
  NSNumber     *sessionIdentifier_;
  NSDictionary *cachedSummary_;
  NSString     *webProvider_;
  BOOL          bashEnabled_;
  BOOL          streamingEnabled_;
  BOOL          promptInFlight_;
  BOOL          promptCancellationRequested_;
}

+ (void)bootstrapProcessWithCACertPath:(NSString *)caCertPath;
+ (NSString *)sessionsDatabasePath;
+ (BOOL)initializeSessionStoreWithError:(NSError **)error;
+ (StrappySession *)createSessionWithError:(NSError **)error;
+ (NSArray *)codingWorkingDirectoryPaths;
+ (BOOL)deleteSessionWithIdentifier:(NSNumber *)sessionIdentifier
                               error:(NSError **)error;
+ (StrappySession *)sessionWithIdentifier:(NSNumber *)sessionIdentifier;
+ (StrappySession *)sessionWithSummary:(NSDictionary *)summary;
+ (NSUInteger)inFlightSessionCount;
+ (BOOL)hasInFlightSessions;
+ (BOOL)isPromptInFlightForSessionIdentifier:(NSNumber *)sessionIdentifier;
+ (BOOL)isModelCatalogRefreshInFlight;
+ (NSArray *)sessionSummariesWithError:(NSError **)error;
+ (NSDictionary *)sessionListSummaryForSessionIdentifier:
    (NSNumber *)sessionIdentifier error:(NSError **)error;
+ (NSDictionary *)sessionSummaryForSessionIdentifier:(NSNumber *)sessionIdentifier
                                               error:(NSError **)error;
+ (NSArray *)openRouterModelCatalogMatchingSearchText:(NSString *)searchText
                                                error:(NSError **)error;
+ (NSArray *)openRouterModelCatalogWithError:(NSError **)error;
+ (NSArray *)allowedOpenRouterModelCatalogWithError:(NSError **)error;
+ (NSArray *)assistantSetCatalog;
+ (NSString *)systemPromptForAssistantSetIdentifier:(NSString *)identifier
                                  webSearchEnabled:(BOOL)webSearchEnabled
                                             error:(NSError **)error;
+ (NSString *)defaultOpenRouterModelIdentifierWithError:(NSError **)error;
+ (BOOL)setDefaultOpenRouterModelIdentifier:(NSString *)modelIdentifier
                                      error:(NSError **)error;
+ (NSString *)selectedOpenRouterModelIdentifierWithError:(NSError **)error;
+ (BOOL)setSelectedOpenRouterModelIdentifier:(NSString *)modelIdentifier
                                       error:(NSError **)error;
+ (BOOL)setOpenRouterModelAllowed:(BOOL)allowed
                forModelIdentifier:(NSString *)modelIdentifier
                             error:(NSError **)error;
+ (BOOL)beginOpenRouterModelCatalogRefreshWithError:(NSError **)error;
+ (NSString *)databaseStudyJSONWithError:(NSError **)error;
+ (NSArray *)databaseStudyRowsWithError:(NSError **)error;
+ (BOOL)resetDatabaseStudyWithError:(NSError **)error;
+ (BOOL)databaseStudyProgressWithStudiedCount:(NSUInteger *)studiedCount
                                approvedCount:(NSUInteger *)approvedCount
                                        error:(NSError **)error;
+ (NSUInteger)databaseStudyPendingDatabaseCountWithError:(NSError **)error;
+ (StrappySession *)beginDatabaseStudyWithError:(NSError **)error;
+ (NSString *)webViewBatchedJavaScriptForJavaScript:(NSString *)javaScript;

- (id)initWithSessionIdentifier:(NSNumber *)sessionIdentifier
                        summary:(NSDictionary *)summary;
- (NSNumber *)sessionIdentifier;
- (NSDictionary *)cachedSummary;
- (NSDictionary *)summaryWithError:(NSError **)error;
- (NSArray *)messagesWithError:(NSError **)error;
- (NSString *)webViewMessagesPageHTMLWithErrorText:(NSString *)errorText
                                      messageCount:(NSUInteger *)messageCount
                                             error:(NSError **)error;
- (NSString *)webViewAppendMessagesJavaScriptFromIndex:(NSUInteger)startIndex
                                          messageCount:(NSUInteger *)messageCount
                                                 error:(NSError **)error;
- (NSString *)webViewJavaScriptForStreamEvent:(NSDictionary *)event
                                        error:(NSError **)error;
- (NSString *)webProvider;
- (BOOL)setWebProvider:(NSString *)webProvider error:(NSError **)error;
- (BOOL)bashEnabled;
- (BOOL)setBashEnabled:(BOOL)enabled error:(NSError **)error;
- (NSString *)workingDirectoryWithError:(NSError **)error;
- (BOOL)setWorkingDirectory:(NSString *)workingDirectory
                      error:(NSError **)error;
- (NSString *)assistantSetIdentifier;
- (BOOL)setAssistantSetIdentifier:(NSString *)assistantSetIdentifier
                            error:(NSError **)error;
- (BOOL)streamingEnabled;
- (BOOL)setStreamingEnabled:(BOOL)enabled error:(NSError **)error;
- (NSString *)selectedOpenRouterModelIdentifierWithError:(NSError **)error;
- (BOOL)setSelectedOpenRouterModelIdentifier:(NSString *)modelIdentifier
                                       error:(NSError **)error;
- (BOOL)isPromptInFlight;
- (BOOL)isDatabaseStudySession;
- (BOOL)promptCancellationRequested;
- (BOOL)beginStreamingPrompt:(NSString *)prompt
                     context:(NSDictionary *)context
                       error:(NSError **)error;
- (BOOL)beginNonStreamingPrompt:(NSString *)prompt
                         context:(NSDictionary *)context
                           error:(NSError **)error;
- (BOOL)beginResponsesPrompt:(NSString *)prompt
                     context:(NSDictionary *)context
                       error:(NSError **)error;
- (void)cancelPrompt;

@end
