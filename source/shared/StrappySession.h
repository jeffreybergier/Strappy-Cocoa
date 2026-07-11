#import <Foundation/Foundation.h>

extern NSString * const StrappySessionDidUpdateNotification;
extern NSString * const StrappySessionPromptDidStartNotification;
extern NSString * const StrappySessionPromptDidFinishNotification;
extern NSString * const StrappySessionStreamEventNotification;
extern NSString * const StrappySessionModelCatalogRefreshDidStartNotification;
extern NSString * const StrappySessionModelCatalogRefreshDidFinishNotification;
extern NSString * const StrappySessionModelCatalogDidChangeNotification;

@interface StrappySession : NSObject {
 @private
  NSNumber     *sessionIdentifier_;
  NSDictionary *cachedSummary_;
  BOOL          webSearchEnabled_;
  BOOL          streamingEnabled_;
  BOOL          promptInFlight_;
  BOOL          promptCancellationRequested_;
}

+ (void)bootstrapProcessWithCACertPath:(NSString *)caCertPath;
+ (NSString *)sessionsDatabasePath;
+ (BOOL)initializeSessionStoreWithError:(NSError **)error;
+ (StrappySession *)createSessionWithError:(NSError **)error;
+ (BOOL)deleteSessionWithIdentifier:(NSNumber *)sessionIdentifier
                               error:(NSError **)error;
+ (StrappySession *)sessionWithIdentifier:(NSNumber *)sessionIdentifier;
+ (StrappySession *)sessionWithSummary:(NSDictionary *)summary;
+ (NSUInteger)inFlightSessionCount;
+ (BOOL)hasInFlightSessions;
+ (BOOL)isPromptInFlightForSessionIdentifier:(NSNumber *)sessionIdentifier;
+ (BOOL)isModelCatalogRefreshInFlight;
+ (NSArray *)sessionSummariesWithError:(NSError **)error;
+ (NSDictionary *)sessionSummaryForSessionIdentifier:(NSNumber *)sessionIdentifier
                                               error:(NSError **)error;
+ (NSArray *)openRouterModelCatalogMatchingSearchText:(NSString *)searchText
                                                error:(NSError **)error;
+ (NSArray *)openRouterModelCatalogWithError:(NSError **)error;
+ (NSArray *)allowedOpenRouterModelCatalogWithError:(NSError **)error;
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
+ (NSString *)webViewMessageHTMLForMessage:(NSDictionary *)message
                         elementIdentifier:(NSString *)elementIdentifier
                                      state:(NSString *)state
                                 statusHTML:(NSString *)statusHTML;
+ (NSString *)webViewMessagesHTMLForMessages:(NSArray *)messages
                                  startIndex:(NSUInteger)start
                                    endIndex:(NSUInteger)end;
+ (NSString *)webViewAppendMessagesJavaScriptForHTML:(NSString *)messagesHTML;
+ (NSString *)webViewBatchedJavaScriptForJavaScript:(NSString *)javaScript;
+ (NSString *)webViewMessagesPageHTMLForMessagesHTML:(NSString *)messagesHTML
                                           errorText:(NSString *)errorText;

- (id)initWithSessionIdentifier:(NSNumber *)sessionIdentifier
                        summary:(NSDictionary *)summary;
- (NSNumber *)sessionIdentifier;
- (NSDictionary *)cachedSummary;
- (NSDictionary *)summaryWithError:(NSError **)error;
- (NSArray *)messagesWithError:(NSError **)error;
- (NSString *)webViewJavaScriptForStreamEvent:(NSDictionary *)event
                                        error:(NSError **)error;
- (BOOL)webSearchEnabled;
- (BOOL)setWebSearchEnabled:(BOOL)enabled error:(NSError **)error;
- (BOOL)streamingEnabled;
- (BOOL)setStreamingEnabled:(BOOL)enabled error:(NSError **)error;
- (NSString *)selectedOpenRouterModelIdentifierWithError:(NSError **)error;
- (BOOL)setSelectedOpenRouterModelIdentifier:(NSString *)modelIdentifier
                                       error:(NSError **)error;
- (BOOL)isPromptInFlight;
- (BOOL)promptCancellationRequested;
- (BOOL)beginStreamingPrompt:(NSString *)prompt
                     context:(NSDictionary *)context
                       error:(NSError **)error;
- (BOOL)beginNonStreamingPrompt:(NSString *)prompt
                         context:(NSDictionary *)context
                           error:(NSError **)error;
- (void)cancelPrompt;

@end
