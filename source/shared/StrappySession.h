#import <Foundation/Foundation.h>

extern NSString * const StrappySessionDidUpdateNotification;
extern NSString * const StrappySessionPromptDidStartNotification;
extern NSString * const StrappySessionPromptDidFinishNotification;
extern NSString * const StrappySessionStreamEventNotification;
extern NSString * const StrappySessionModelCatalogRefreshDidStartNotification;
extern NSString * const StrappySessionModelCatalogRefreshDidFinishNotification;
extern NSString * const StrappySessionModelCatalogDidChangeNotification;
extern NSString * const StrappyProviderAccountsDidChangeNotification;
extern NSString * const StrappySessionChangeKindKey;
extern NSString * const StrappySessionChangeKindActivity;
extern NSString * const StrappySessionChangeKindName;
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

typedef enum StrappyWebViewPalette {
  StrappyWebViewPaletteApplicationTinted = 0,
  StrappyWebViewPaletteNeutral
} StrappyWebViewPalette;

@protocol StrappyChatGPTAuthorizationObserver <NSObject>
- (BOOL)strappyChatGPTAuthorizationShouldCancelWithContext:(id)context;
- (void)strappyChatGPTAuthorizationDidReceiveVerificationURL:
          (NSString *)verificationURL
                                                       userCode:
          (NSString *)userCode
                                                        context:
          (id)context;
@end

enum {
  StrappySessionOptionModel = 1U << 0,
  StrappySessionOptionAssistantSet = 1U << 1,
  StrappySessionOptionWebProvider = 1U << 2,
  StrappySessionOptionWebSearch = 1U << 3,
  StrappySessionOptionBash = 1U << 4,
  StrappySessionOptionLimitToOneTool = 1U << 5,
  StrappySessionOptionWorkingDirectory = 1U << 6,
  StrappySessionOptionProviderAccount = 1U << 7,
  StrappySessionOptionRoundLimit = 1U << 8,
  StrappySessionOptionAnswerQuality = 1U << 9,
  StrappySessionOptionAll =
    StrappySessionOptionModel |
    StrappySessionOptionAssistantSet |
    StrappySessionOptionWebProvider |
    StrappySessionOptionWebSearch |
    StrappySessionOptionBash |
    StrappySessionOptionLimitToOneTool |
    StrappySessionOptionWorkingDirectory |
    StrappySessionOptionProviderAccount |
    StrappySessionOptionRoundLimit |
    StrappySessionOptionAnswerQuality
};

@interface StrappySessionOptions : NSObject <NSCopying> {
 @private
  NSString *modelIdentifier_;
  NSString *providerAccountIdentifier_;
  NSString *assistantSetIdentifier_;
  NSString *webProvider_;
  NSString *workingDirectory_;
  BOOL webSearchEnabled_;
  BOOL bashEnabled_;
  BOOL limitToOneTool_;
  BOOL answerQualityEnabled_;
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
- (id)initWithModelIdentifier:(NSString *)modelIdentifier
    providerAccountIdentifier:(NSString *)providerAccountIdentifier
       assistantSetIdentifier:(NSString *)assistantSetIdentifier
                  webProvider:(NSString *)webProvider
             webSearchEnabled:(BOOL)webSearchEnabled
                  bashEnabled:(BOOL)bashEnabled
               limitToOneTool:(BOOL)limitToOneTool
                   roundLimit:(NSUInteger)roundLimit
             workingDirectory:(NSString *)workingDirectory;
- (NSString *)modelIdentifier;
- (void)setModelIdentifier:(NSString *)modelIdentifier;
- (NSString *)providerAccountIdentifier;
- (void)setProviderAccountIdentifier:(NSString *)providerAccountIdentifier;
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
- (BOOL)answerQualityEnabled;
- (void)setAnswerQualityEnabled:(BOOL)enabled;
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
+ (BOOL)prepareProviderCredentialsWithError:(NSError **)error;
+ (BOOL)isChatGPTProviderEnabled;
+ (long long)currentTimestampMilliseconds;
+ (NSString *)performChatGPTDeviceAuthorizationForProviderAccountIdentifier:
                (NSString *)providerAccountIdentifier
                observer:(id<StrappyChatGPTAuthorizationObserver>)observer
                context:(id)context
                error:(NSError **)error;
+ (NSString *)refreshChatGPTCredentialsForProviderAccountIdentifier:
                (NSString *)providerAccountIdentifier
                expectedAccountIdentifier:(NSString *)expectedAccountIdentifier
                observer:(id<StrappyChatGPTAuthorizationObserver>)observer
                context:(id)context
                error:(NSError **)error;
+ (NSString *)designatedProviderAccountIdentifierForProviderIdentifier:
                (NSString *)providerIdentifier
                                                               error:
                (NSError **)error;
+ (NSArray *)providerCatalog;
+ (NSArray *)providerAccountCatalogWithError:(NSError **)error;
+ (NSArray *)verifiedProviderAccountCatalogWithError:(NSError **)error;
+ (BOOL)parseOptionalPositiveIntegerText:(NSString *)text
                                   value:(long long *)value;
+ (NSString *)pricePerTokenForPricePerMillionText:(NSString *)text
                                             valid:(BOOL *)valid;
+ (NSDictionary *)createProviderAccountForProviderIdentifier:
                    (NSString *)providerIdentifier
                                                        error:
                    (NSError **)error;
+ (BOOL)updateProviderAccountIdentifier:(NSString *)providerAccountIdentifier
                            displayName:(NSString *)displayName
                      responsesEndpoint:(NSString *)responsesEndpoint
                        maxOutputTokens:(long long)maxOutputTokens
                                  error:(NSError **)error;
+ (NSString *)bearerTokenForProviderAccountIdentifier:
                (NSString *)providerAccountIdentifier
                                                  error:(NSError **)error;
+ (BOOL)updateProviderAccountIdentifier:(NSString *)providerAccountIdentifier
                            displayName:(NSString *)displayName
                      responsesEndpoint:(NSString *)responsesEndpoint
                        maxOutputTokens:(long long)maxOutputTokens
                            bearerToken:(NSString *)bearerToken
                                  error:(NSError **)error;
+ (BOOL)archiveProviderAccountIdentifier:(NSString *)providerAccountIdentifier
                                    error:(NSError **)error;
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
+ (NSArray *)modelCatalogMatchingSearchText:(NSString *)searchText
                                       error:(NSError **)error;
+ (NSArray *)modelCatalogWithError:(NSError **)error;
+ (NSArray *)configuredProviderModelCatalogWithError:(NSError **)error;
+ (NSArray *)allowedModelCatalogWithError:(NSError **)error;
+ (NSArray *)bundledModelCatalogForProviderIdentifier:
               (NSString *)providerIdentifier
                                                 error:
               (NSError **)error;
+ (NSArray *)openRouterModelCatalogMatchingSearchText:(NSString *)searchText
                                                error:(NSError **)error;
+ (NSArray *)openRouterModelCatalogWithError:(NSError **)error;
+ (NSArray *)allowedOpenRouterModelCatalogWithError:(NSError **)error;
+ (NSArray *)assistantSetCatalog;
+ (NSString *)systemPromptForAssistantSetIdentifier:(NSString *)identifier
                                  webSearchEnabled:(BOOL)webSearchEnabled
                                             error:(NSError **)error;
+ (NSString *)defaultOpenRouterModelIdentifierWithError:(NSError **)error;
+ (NSString *)defaultModelIdentifierWithError:(NSError **)error;
+ (BOOL)setDefaultModelIdentifier:(NSString *)modelIdentifier
                             error:(NSError **)error;
+ (BOOL)setDefaultOpenRouterModelIdentifier:(NSString *)modelIdentifier
                                      error:(NSError **)error;
+ (StrappySessionOptions *)defaultSessionOptionsWithError:(NSError **)error;
+ (BOOL)updateDefaultSessionOptions:(StrappySessionOptions *)options
                       changedFields:(StrappySessionOptionMask)changedFields
                               error:(NSError **)error;
+ (BOOL)setOpenRouterModelAllowed:(BOOL)allowed
                forModelIdentifier:(NSString *)modelIdentifier
                             error:(NSError **)error;
+ (BOOL)setModelAllowed:(BOOL)allowed
     forModelIdentifier:(NSString *)modelIdentifier
                  error:(NSError **)error;
+ (NSString *)createManualModelForProviderIdentifier:
                (NSString *)providerIdentifier
                                                wireModelID:
                (NSString *)wireModelID
                                                displayName:
                (NSString *)displayName
                                         contextWindowTokens:
                (long long)contextWindowTokens
                                             maxOutputTokens:
                (long long)maxOutputTokens
                                           reasoningEnabled:
                (BOOL)reasoningEnabled
                                          imageInputEnabled:
                (BOOL)imageInputEnabled
                                      localFunctionsEnabled:
                (BOOL)localFunctionsEnabled
                                          inputPricePerToken:
                (NSString *)inputPricePerToken
                                         outputPricePerToken:
                (NSString *)outputPricePerToken
                                      cacheReadPricePerToken:
                (NSString *)cacheReadPricePerToken
                                     cacheWritePricePerToken:
                (NSString *)cacheWritePricePerToken
                                                       error:
                (NSError **)error;
+ (BOOL)updateManualModelForProviderIdentifier:
            (NSString *)providerIdentifier
                                            wireModelID:
            (NSString *)wireModelID
                                            displayName:
            (NSString *)displayName
                                     contextWindowTokens:
            (long long)contextWindowTokens
                                         maxOutputTokens:
            (long long)maxOutputTokens
                                       reasoningEnabled:
            (BOOL)reasoningEnabled
                                      imageInputEnabled:
            (BOOL)imageInputEnabled
                                  localFunctionsEnabled:
            (BOOL)localFunctionsEnabled
                                      inputPricePerToken:
            (NSString *)inputPricePerToken
                                     outputPricePerToken:
            (NSString *)outputPricePerToken
                                  cacheReadPricePerToken:
            (NSString *)cacheReadPricePerToken
                                 cacheWritePricePerToken:
            (NSString *)cacheWritePricePerToken
                                                   error:
            (NSError **)error;
+ (BOOL)archiveManualModelForProviderIdentifier:
            (NSString *)providerIdentifier
                                             wireModelID:
            (NSString *)wireModelID
                                                    error:
            (NSError **)error;
+ (BOOL)beginOpenRouterModelCatalogRefreshWithError:(NSError **)error;
+ (NSArray *)databaseStudyRowsWithError:(NSError **)error;
+ (BOOL)deleteDatabaseStudyValuesForDatabaseIdentifier:
    (NSString *)databaseIdentifier
                                                    error:(NSError **)error;
+ (BOOL)resetDatabaseStudyWithError:(NSError **)error;
+ (StrappySession *)beginDatabaseStudyWithError:(NSError **)error;
+ (NSString *)webViewBatchedJavaScriptForJavaScript:(NSString *)javaScript;
+ (NSString *)webViewEmptyMessagesPageHTMLWithPalette:
    (StrappyWebViewPalette)palette;

- (id)initWithSessionIdentifier:(NSNumber *)sessionIdentifier
                        summary:(NSDictionary *)summary;
- (NSNumber *)sessionIdentifier;
- (NSDictionary *)cachedSummary;
- (NSDictionary *)summaryWithError:(NSError **)error;
- (NSArray *)messagesWithError:(NSError **)error;
- (NSString *)webViewMessagesPageHTMLWithErrorText:(NSString *)errorText
                                           palette:(StrappyWebViewPalette)palette
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
- (NSString *)webViewReconcileMessagesJavaScriptAfterTimelineCursor:
                (NSString *)timelineCursor
                                      nextTimelineCursor:
                (NSString **)nextTimelineCursor
                                  reconciledMessageCount:
                (NSUInteger *)reconciledMessageCount
                                                   error:(NSError **)error;
- (NSString *)webViewClearProcessingStatusJavaScript;
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
