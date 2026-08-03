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
extern NSString * const StrappySessionChangeKindOptions;
extern NSString * const StrappySessionOptionsKey;
extern NSString * const StrappySessionChangedOptionsKey;
extern NSString * const StrappyWebProviderNone;
extern NSString * const StrappyWebProviderAuto;
extern NSString * const StrappyWebProviderNative;
extern NSString * const StrappyWebProviderExa;
extern NSString * const StrappyWebProviderParallel;
extern const NSUInteger StrappySessionDefaultRoundLimit;
extern const NSUInteger StrappySessionMaximumLimit;

typedef NSUInteger StrappySessionOptionMask;

enum {
  StrappySessionOptionModel = 1U << 0,
  StrappySessionOptionAssistantSet = 1U << 1,
  StrappySessionOptionWebProvider = 1U << 2,
  StrappySessionOptionWebSearch = 1U << 3,
  StrappySessionOptionBash = 1U << 4,
  StrappySessionOptionLimitToOneTool = 1U << 5,
  StrappySessionOptionWorkingDirectory = 1U << 6,
  StrappySessionOptionRoundLimit = 1U << 8,
  StrappySessionOptionAll =
    StrappySessionOptionModel |
    StrappySessionOptionAssistantSet |
    StrappySessionOptionWebProvider |
    StrappySessionOptionWebSearch |
    StrappySessionOptionBash |
    StrappySessionOptionLimitToOneTool |
    StrappySessionOptionWorkingDirectory |
    StrappySessionOptionRoundLimit
};

@interface StrappySessionOptions : NSObject <NSCopying> {
 @private
  NSString *modelIdentifier_;
  NSString *assistantSetIdentifier_;
  NSString *webProvider_;
  NSString *workingDirectory_;
  BOOL webSearchEnabled_;
  BOOL bashEnabled_;
  BOOL limitToOneTool_;
  NSUInteger roundLimit_;
}

- (id)initWithModelIdentifier:(NSString *)modelIdentifier
       assistantSetIdentifier:(NSString *)assistantSetIdentifier
                  webProvider:(NSString *)webProvider
             webSearchEnabled:(BOOL)webSearchEnabled
                  bashEnabled:(BOOL)bashEnabled
               limitToOneTool:(BOOL)limitToOneTool
                   roundLimit:(NSUInteger)roundLimit
             workingDirectory:(NSString *)workingDirectory;
- (NSString *)modelIdentifier;
- (void)setModelIdentifier:(NSString *)modelIdentifier;
- (NSString *)assistantSetIdentifier;
- (void)setAssistantSetIdentifier:(NSString *)assistantSetIdentifier;
- (NSString *)webProvider;
- (void)setWebProvider:(NSString *)webProvider;
- (BOOL)webSearchEnabled;
- (void)setWebSearchEnabled:(BOOL)enabled;
- (BOOL)bashEnabled;
- (void)setBashEnabled:(BOOL)enabled;
- (BOOL)limitToOneTool;
- (void)setLimitToOneTool:(BOOL)enabled;
- (NSUInteger)roundLimit;
- (void)setRoundLimit:(NSUInteger)roundLimit;
- (NSString *)workingDirectory;
- (void)setWorkingDirectory:(NSString *)workingDirectory;

@end

@interface StrappySession : NSObject {
 @private
  NSNumber     *sessionIdentifier_;
  NSDictionary *cachedSummary_;
  StrappySessionOptions *options_;
  NSString     *processingStatusJSON_;
  BOOL          optionsLoaded_;
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
+ (StrappySessionOptions *)defaultSessionOptionsWithError:(NSError **)error;
+ (BOOL)updateDefaultSessionOptions:(StrappySessionOptions *)options
                       changedFields:(StrappySessionOptionMask)changedFields
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
+ (BOOL)deleteDatabaseStudyValuesForDatabaseIdentifier:
    (NSString *)databaseIdentifier
                                                    error:(NSError **)error;
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
                                    timelineCursor:(NSString **)timelineCursor
                                             error:(NSError **)error;
- (NSString *)webViewAppendMessagesJavaScriptAfterTimelineCursor:
                (NSString *)timelineCursor
                                      nextTimelineCursor:
                (NSString **)nextTimelineCursor
                                    appendedMessageCount:
                (NSUInteger *)appendedMessageCount
                                                   error:(NSError **)error;
- (NSString *)webViewJavaScriptForStreamEvent:(NSDictionary *)event
                                        error:(NSError **)error;
- (NSString *)webViewJavaScriptForModelRequestIdentifier:
                (NSNumber *)modelRequestIdentifier
                                      includedInContext:(BOOL)includedInContext
                                                animated:(BOOL)animated;
- (BOOL)setModelRequestIdentifier:(NSNumber *)modelRequestIdentifier
                includedInContext:(BOOL)includedInContext
                            error:(NSError **)error;
- (StrappySessionOptions *)optionsWithError:(NSError **)error;
- (BOOL)updateOptions:(StrappySessionOptions *)options
        changedFields:(StrappySessionOptionMask)changedFields
                error:(NSError **)error;
- (BOOL)isPromptInFlight;
- (BOOL)isDatabaseStudySession;
- (BOOL)promptCancellationRequested;
- (BOOL)beginResponsesPrompt:(NSString *)prompt
                     context:(NSDictionary *)context
                       error:(NSError **)error;
- (void)cancelPrompt;

@end
