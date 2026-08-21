#import "StrappySession.h"

#import "StrappyKeychain.h"
#import "strappy_core.h"
#import "strappy_model_catalog.h"
#import "strappy_openai_oauth.h"
#import "strappy_prompt.h"
#import "strappy_provider.h"
#import "strappy_responses.h"
#import "strappy_session.h"
#import "strappy_study.h"
#import "XPFoundation.h"
#import "cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static NSString *StrappyStageWebViewFonts(void)
{
  static NSString * const fontNames[] = {
    @"FA7-Solid-900.otf",
    @"FA7-Regular-400.otf",
    @"FA7-Brands-400.otf"
  };
  NSFileManager *fileManager;
  NSString *sourceDirectory;
  NSString *webViewDirectory;
  NSString *destinationDirectory;
  BOOL isDirectory;
  NSUInteger index;

  fileManager = [NSFileManager defaultManager];
  sourceDirectory = [[[NSBundle mainBundle] resourcePath]
    stringByAppendingPathComponent:@"Fonts"];
  webViewDirectory =
    [[StrappySession sessionsDatabasePath] stringByDeletingLastPathComponent];
  destinationDirectory =
    [webViewDirectory stringByAppendingPathComponent:@"Fonts"];
  isDirectory = NO;

  if (![fileManager fileExistsAtPath:webViewDirectory
                         isDirectory:&isDirectory]) {
    if (![fileManager XP_createDirectoryAtPath:webViewDirectory
                   withIntermediateDirectories:YES
                                    attributes:nil
                                         error:NULL]) {
      return nil;
    }
  } else if (!isDirectory) {
    return nil;
  }

  isDirectory = NO;
  if ([fileManager fileExistsAtPath:destinationDirectory
                        isDirectory:&isDirectory]) {
    if (!isDirectory) {
      return nil;
    }
  } else if (![fileManager XP_createDirectoryAtPath:destinationDirectory
                        withIntermediateDirectories:YES
                                         attributes:nil
                                              error:NULL]) {
    return nil;
  }

  for (index = 0U;
       index < (sizeof(fontNames) / sizeof(fontNames[0]));
       index++) {
    NSString *sourcePath;
    NSString *destinationPath;
    NSData *sourceData;
    NSData *destinationData;

    sourcePath = [sourceDirectory stringByAppendingPathComponent:fontNames[index]];
    destinationPath =
      [destinationDirectory stringByAppendingPathComponent:fontNames[index]];
    sourceData = [NSData dataWithContentsOfFile:sourcePath];
    if ((sourceData == nil) || ([sourceData length] == 0U)) {
      return nil;
    }

    destinationData = [NSData dataWithContentsOfFile:destinationPath];
    if ((destinationData != nil) && [destinationData isEqualToData:sourceData]) {
      continue;
    }
    if (![sourceData writeToFile:destinationPath atomically:YES]) {
      return nil;
    }
  }

  return destinationDirectory;
}

NSString * const StrappySessionDidUpdateNotification =
  @"StrappySessionDidUpdateNotification";
NSString * const StrappySessionPromptDidStartNotification =
  @"StrappySessionPromptDidStartNotification";
NSString * const StrappySessionPromptDidFinishNotification =
  @"StrappySessionPromptDidFinishNotification";
NSString * const StrappySessionStreamEventNotification =
  @"StrappySessionStreamEventNotification";
NSString * const StrappySessionModelCatalogRefreshDidStartNotification =
  @"StrappySessionModelCatalogRefreshDidStartNotification";
NSString * const StrappySessionModelCatalogRefreshDidFinishNotification =
  @"StrappySessionModelCatalogRefreshDidFinishNotification";
NSString * const StrappySessionModelCatalogDidChangeNotification =
  @"StrappySessionModelCatalogDidChangeNotification";
NSString * const StrappyProviderAccountsDidChangeNotification =
  @"StrappyProviderAccountsDidChangeNotification";
NSString * const StrappySessionChangeKindKey = @"change_kind";
NSString * const StrappySessionChangeKindActivity = @"activity";
NSString * const StrappySessionChangeKindName = @"name";
NSString * const StrappySessionChangeKindOptions = @"options";
NSString * const StrappySessionOptionsKey = @"options";
NSString * const StrappySessionChangedOptionsKey = @"changed_options";
NSString * const StrappyWebProviderNone = @"none";
NSString * const StrappyWebProviderAuto = @"auto";
NSString * const StrappyWebProviderNative = @"native";
NSString * const StrappyWebProviderExa = @"exa";
NSString * const StrappyWebProviderParallel = @"parallel";
const NSUInteger StrappySessionDefaultRoundLimit =
  (NSUInteger)STRAPPY_SESSION_DEFAULT_ROUND_LIMIT;
const NSUInteger StrappySessionMaximumLimit =
  (NSUInteger)STRAPPY_SESSION_MAX_LIMIT;

static NSMutableDictionary *StrappySessionInFlightSessions = nil;
static BOOL StrappySessionModelCatalogRefreshInFlight = NO;

static void StrappySessionSecureFreeCString(char *value)
{
  volatile unsigned char *bytes;
  size_t length;

  if (value == NULL) {
    return;
  }
  bytes = (volatile unsigned char *)value;
  length = strlen(value);
  while (length > 0U) {
    *bytes++ = 0U;
    length--;
  }
  free(value);
}

static long long StrappySessionNowMilliseconds(void)
{
  struct timeval now;

  if ((gettimeofday(&now, NULL) != 0) || (now.tv_sec < 0)) {
    return 0LL;
  }
  return ((long long)now.tv_sec * 1000LL) +
    ((long long)now.tv_usec / 1000LL);
}

static int StrappySessionCopyChatGPTCredentials(
  const char *providerID,
  const char *providerAccountID,
  int forceRefresh,
  char **accessTokenOut,
  char **accountIdentifierOut,
  void *userData,
  char **errorOut)
{
  static const long long refreshLeewayMilliseconds = 5LL * 60LL * 1000LL;
  StrappyKeychain *keychain;
  NSString *accessToken;
  NSString *refreshToken;
  NSString *accountIdentifier;
  long long expiresAtMilliseconds;
  long long nowMilliseconds;
  BOOL credentialReady;
  int ok;

  (void)userData;
  if ((providerID == NULL) || (providerID[0] == '\0') ||
      (providerAccountID == NULL) || (providerAccountID[0] == '\0') ||
      (accessTokenOut == NULL) || (accountIdentifierOut == NULL)) {
    strappy_set_error(errorOut, "Account credential inputs are missing.");
    return 0;
  }
  *accessTokenOut = NULL;
  *accountIdentifierOut = NULL;
  keychain = [StrappyKeychain sharedKeychain];
  accessToken = nil;
  refreshToken = nil;
  accountIdentifier = nil;
  expiresAtMilliseconds = 0LL;
  ok = 0;

  if (strcmp(providerID, STRAPPY_PROVIDER_OPENAI_CHATGPT) != 0) {
    NSString *providerIdentifier;
    NSString *providerAccountIdentifier;
    NSString *bearerToken;
    NSObject *credentialLock;

    providerIdentifier = [NSString stringWithUTF8String:providerID];
    providerAccountIdentifier = [NSString stringWithUTF8String:
      providerAccountID];
    bearerToken = nil;
    credentialLock = [keychain
      credentialLockForProviderIdentifier:providerIdentifier
      providerAccountIdentifier:providerAccountIdentifier];
    @synchronized(credentialLock) {
      if ([keychain loadBearerToken:&bearerToken
              forProviderIdentifier:providerIdentifier
          providerAccountIdentifier:providerAccountIdentifier]) {
        *accessTokenOut = strappy_string_duplicate([bearerToken UTF8String]);
        if (*accessTokenOut != NULL) {
          return 1;
        }
      }
    }
    strappy_set_error(errorOut, "The selected account has no bearer credential.");
    return 0;
  }

  {
    NSString *providerAccountIdentifier;
    NSObject *credentialLock;

    providerAccountIdentifier = [NSString stringWithUTF8String:
      providerAccountID];
    credentialLock = [keychain
      credentialLockForProviderIdentifier:@"openai_chatgpt"
      providerAccountIdentifier:providerAccountIdentifier];
  @synchronized(credentialLock) {
    if (![keychain loadChatGPTAccessToken:&accessToken
                              refreshToken:&refreshToken
                         accountIdentifier:&accountIdentifier
                      expiresAtMilliseconds:&expiresAtMilliseconds
                 providerAccountIdentifier:providerAccountIdentifier]) {
      strappy_set_error(errorOut, "Sign in to ChatGPT before sending a prompt.");
    } else {
      credentialReady = YES;
      nowMilliseconds = StrappySessionNowMilliseconds();
      if (forceRefresh || (nowMilliseconds <= 0LL) ||
          ((expiresAtMilliseconds - nowMilliseconds) <=
           refreshLeewayMilliseconds)) {
        strappy_openai_oauth_configuration configuration;
        strappy_openai_oauth_credentials credentials;
        NSString *nextAccessToken;
        NSString *nextRefreshToken;
        NSString *nextAccountIdentifier;

        strappy_openai_oauth_default_configuration(&configuration);
        strappy_openai_oauth_credentials_init(&credentials);
        credentialReady = NO;
        if (strappy_openai_oauth_refresh_credentials(
              &configuration,
              [refreshToken UTF8String],
              &credentials,
              NULL,
              NULL,
              errorOut)) {
          nextAccessToken = [NSString stringWithUTF8String:
            credentials.access_token];
          nextRefreshToken = [NSString stringWithUTF8String:
            credentials.refresh_token];
          nextAccountIdentifier = [NSString stringWithUTF8String:
            credentials.account_id];
          if ((nextAccessToken == nil) || (nextRefreshToken == nil) ||
              (nextAccountIdentifier == nil) ||
              ![nextAccountIdentifier isEqualToString:accountIdentifier]) {
            strappy_set_error(
              errorOut,
              "Refreshed ChatGPT credentials changed account identity.");
          } else if (![keychain
                       saveChatGPTAccessToken:nextAccessToken
                       refreshToken:nextRefreshToken
                       accountIdentifier:nextAccountIdentifier
                       expiresAtMilliseconds:
                         credentials.expires_at_milliseconds
                  providerAccountIdentifier:providerAccountIdentifier]) {
            strappy_set_error(
              errorOut,
              "The Keychain refused the refreshed ChatGPT credential.");
          } else {
            accessToken = nextAccessToken;
            refreshToken = nextRefreshToken;
            accountIdentifier = nextAccountIdentifier;
            expiresAtMilliseconds = credentials.expires_at_milliseconds;
            credentialReady = YES;
          }
        }
        strappy_openai_oauth_credentials_destroy(&credentials);
      }
      if (credentialReady && ((errorOut == NULL) || (*errorOut == NULL))) {
        *accessTokenOut = strappy_string_duplicate([accessToken UTF8String]);
        *accountIdentifierOut =
          strappy_string_duplicate([accountIdentifier UTF8String]);
        if ((*accessTokenOut == NULL) || (*accountIdentifierOut == NULL)) {
          StrappySessionSecureFreeCString(*accessTokenOut);
          StrappySessionSecureFreeCString(*accountIdentifierOut);
          *accessTokenOut = NULL;
          *accountIdentifierOut = NULL;
          strappy_set_error(errorOut,
                            "Could not allocate ChatGPT credential snapshot.");
        } else {
          ok = 1;
        }
      }
    }
  }
  }
  return ok;
}

typedef struct StrappySessionResponsesContext {
  StrappySession *session;
  NSDictionary *context;
  /* Presentation state belongs to the prompt worker, never to a WebView. */
  NSString *timelineCursor;
  strappy_session_webview_render_context *webViewRenderContext;
  NSString *terminalAppendJavaScript;
  NSString *terminalUpdateJavaScript;
  NSString *terminalMessageKey;
  BOOL presentationReloadRequired;
} StrappySessionResponsesContext;

static void StrappySessionResponsesContextClearTerminal(
  StrappySessionResponsesContext *context)
{
  if (context == NULL) {
    return;
  }
  [context->terminalAppendJavaScript release];
  context->terminalAppendJavaScript = nil;
  [context->terminalUpdateJavaScript release];
  context->terminalUpdateJavaScript = nil;
  [context->terminalMessageKey release];
  context->terminalMessageKey = nil;
}

static void StrappySessionResponsesContextDestroy(
  StrappySessionResponsesContext *context)
{
  if (context == NULL) {
    return;
  }
  [context->context release];
  context->context = nil;
  [context->timelineCursor release];
  context->timelineCursor = nil;
  strappy_session_webview_render_context_destroy(
    context->webViewRenderContext);
  context->webViewRenderContext = NULL;
  StrappySessionResponsesContextClearTerminal(context);
}

static const char *StrappySessionOptionalCString(NSString *string)
{
  if (![string isKindOfClass:[NSString class]] || ([string length] == 0U)) {
    return NULL;
  }
  return [string UTF8String];
}

static NSString *StrappySessionStringFromCString(char *value)
{
  char *sanitized;
  NSString *string;
  size_t length;

  if (value == NULL) {
    return @"";
  }

  string = [NSString stringWithUTF8String:value];
  if (string == nil) {
    length = strlen(value);
    sanitized = strappy_utf8_sanitized_string_duplicate(value, length);
    if (sanitized != NULL) {
      string = [NSString stringWithUTF8String:sanitized];
    }
    strappy_free_string(sanitized);
  }
  strappy_session_free_string(value);
  return (string != nil) ? string : @"";
}

@interface StrappySession ()
+ (NSMutableDictionary *)inFlightSessions;
+ (void)registerInFlightSession:(StrappySession *)session;
+ (void)unregisterInFlightSession:(StrappySession *)session;
+ (StrappySession *)inFlightSessionForIdentifier:(NSNumber *)identifier;
+ (NSArray *)messagesForSessionIdentifier:(NSNumber *)sessionIdentifier
                                    error:(NSError **)error;
+ (NSArray *)modelCatalogFromList:(const strappy_model_record_list *)list;
+ (BOOL)manualModelInput:(strappy_manual_model_input *)input
             wireModelID:(NSString *)wireModelID
             displayName:(NSString *)displayName
      contextWindowTokens:(long long)contextWindowTokens
          maxOutputTokens:(long long)maxOutputTokens
        reasoningEnabled:(BOOL)reasoningEnabled
       imageInputEnabled:(BOOL)imageInputEnabled
   localFunctionsEnabled:(BOOL)localFunctionsEnabled
       inputPricePerToken:(NSString *)inputPricePerToken
      outputPricePerToken:(NSString *)outputPricePerToken
   cacheReadPricePerToken:(NSString *)cacheReadPricePerToken
  cacheWritePricePerToken:(NSString *)cacheWritePricePerToken
                    error:(NSError **)error;
+ (NSDictionary *)dictionaryFromModelRecord:
    (const strappy_model_record *)record;
+ (NSDictionary *)dictionaryFromAssistantSetRecord:
    (const strappy_assistant_set_record *)record;
+ (NSDictionary *)dictionaryFromDatabaseStudyStatusRecord:
    (const strappy_study_database_status_record *)record;
+ (NSString *)guidanceResourceDirectoryWithError:(NSError **)error;
+ (void)refreshOpenRouterModelCatalogInBackground:(id)ignored;
+ (void)openRouterModelCatalogRefreshDidFinish:(NSDictionary *)result;
- (void)updateCachedSummary:(NSDictionary *)summary;
- (NSString *)currentProcessingStatusJSON;
- (void)updateProcessingStatusJSON:(NSString *)statusJSON;
- (int)handleResponsesEvent:(const strappy_responses_event *)event
           responsesContext:(StrappySessionResponsesContext *)context;
- (NSString *)webViewAppendMessagesJavaScriptAfterTimelineCursor:
                (NSString *)timelineCursor
                                      nextTimelineCursor:
                (NSString **)nextTimelineCursor
                                    appendedMessageCount:
                (NSUInteger *)appendedMessageCount
                                           renderContext:
                (const strappy_session_webview_render_context *)renderContext
                                                   error:(NSError **)error;
- (NSString *)webViewJavaScriptForStreamEvent:(NSDictionary *)event
                                renderContext:
                (const strappy_session_webview_render_context *)renderContext
                                        error:(NSError **)error;
- (void)postSessionUpdateAndRelease:(NSDictionary *)update;
- (void)postStreamEventAndRelease:(NSDictionary *)event;
- (NSDictionary *)submitPrompt:(NSString *)prompt
                       context:(NSDictionary *)context
                      isolated:(BOOL)isolated
                         error:(NSError **)error;
- (void)sendPromptInBackground:(NSDictionary *)request;
- (void)runDatabaseStudyInBackground:(id)ignored;
- (void)promptDidFinish:(NSDictionary *)result;
@end

static int StrappySessionHandleResponsesEvent(
  const strappy_responses_event *event,
  void *userData)
{
  StrappySessionResponsesContext *context;
  StrappySession *session;
  NSAutoreleasePool *pool;
  int result;

  if ((event == NULL) || (userData == NULL)) {
    return 1;
  }

  context = (StrappySessionResponsesContext *)userData;
  session = context->session;
  if (session == nil) {
    return 1;
  }

  pool = [[NSAutoreleasePool alloc] init];
  result = [session handleResponsesEvent:event responsesContext:context];
  [pool release];
  return result;
}

static NSString *StrappySessionWebProviderFromValue(NSString *value)
{
  strappy_web_provider provider;

  if (![value isKindOfClass:[NSString class]] ||
      !strappy_web_provider_parse([value UTF8String], &provider)) {
    return StrappyWebProviderNone;
  }
  switch (provider) {
    case STRAPPY_WEB_PROVIDER_AUTO:
      return StrappyWebProviderAuto;
    case STRAPPY_WEB_PROVIDER_NATIVE:
      return StrappyWebProviderNative;
    case STRAPPY_WEB_PROVIDER_EXA:
      return StrappyWebProviderExa;
    case STRAPPY_WEB_PROVIDER_PARALLEL:
      return StrappyWebProviderParallel;
    case STRAPPY_WEB_PROVIDER_NONE:
    default:
      return StrappyWebProviderNone;
  }
}

static NSString *StrappySessionWebProviderFromSummary(NSDictionary *summary)
{
  NSString *webProvider;

  if (![summary isKindOfClass:[NSDictionary class]]) {
    return StrappyWebProviderNone;
  }
  webProvider = [summary objectForKey:@"web_provider"];
  return StrappySessionWebProviderFromValue(webProvider);
}

static NSString *StrappySessionWebProviderFromRecord(
  strappy_web_provider provider)
{
  const char *name;

  name = strappy_web_provider_name(provider);
  if (name == NULL) {
    return StrappyWebProviderNone;
  }
  return StrappySessionWebProviderFromValue(
    [NSString stringWithUTF8String:name]);
}

static BOOL StrappySessionWebSearchEnabledFromSummary(NSDictionary *summary)
{
  NSNumber *webSearchEnabled;

  if (![summary isKindOfClass:[NSDictionary class]]) {
    return YES;
  }

  webSearchEnabled = [summary objectForKey:@"web_search_enabled"];
  return (![webSearchEnabled isKindOfClass:[NSNumber class]] ||
          [webSearchEnabled boolValue]) ? YES : NO;
}

static BOOL StrappySessionBashEnabledFromSummary(NSDictionary *summary)
{
  NSNumber *bashEnabled;

  if (![summary isKindOfClass:[NSDictionary class]]) {
    return NO;
  }

  bashEnabled = [summary objectForKey:@"bash_enabled"];
  return ([bashEnabled isKindOfClass:[NSNumber class]] &&
          [bashEnabled boolValue]) ? YES : NO;
}

static BOOL StrappySessionLimitToOneToolFromSummary(NSDictionary *summary)
{
  NSNumber *limitToOneTool;

  if (![summary isKindOfClass:[NSDictionary class]]) {
    return NO;
  }

  limitToOneTool = [summary objectForKey:@"limit_to_one_tool"];
  return ([limitToOneTool isKindOfClass:[NSNumber class]] &&
          [limitToOneTool boolValue]) ? YES : NO;
}

static BOOL StrappySessionAnswerQualityEnabledFromSummary(
  NSDictionary *summary)
{
  NSNumber *answerQualityEnabled;

  if (![summary isKindOfClass:[NSDictionary class]]) {
    return NO;
  }

  answerQualityEnabled = [summary objectForKey:@"answer_quality_enabled"];
  return ([answerQualityEnabled isKindOfClass:[NSNumber class]] &&
          [answerQualityEnabled boolValue]) ? YES : NO;
}

static NSUInteger StrappySessionLimitFromSummary(NSDictionary *summary,
                                                  NSString *key,
                                                  NSUInteger defaultValue)
{
  NSNumber *value;
  unsigned long long limit;

  if (![summary isKindOfClass:[NSDictionary class]]) {
    return defaultValue;
  }
  value = [summary objectForKey:key];
  if (![value isKindOfClass:[NSNumber class]]) {
    return defaultValue;
  }
  limit = [value unsignedLongLongValue];
  if ((limit == 0ULL) ||
      (limit > (unsigned long long)StrappySessionMaximumLimit)) {
    return defaultValue;
  }
  return (NSUInteger)limit;
}

@implementation StrappySessionOptions

- (id)initWithModelIdentifier:(NSString *)modelIdentifier
       assistantSetIdentifier:(NSString *)assistantSetIdentifier
                  webProvider:(NSString *)webProvider
             webSearchEnabled:(BOOL)webSearchEnabled
                  bashEnabled:(BOOL)bashEnabled
               limitToOneTool:(BOOL)limitToOneTool
                   roundLimit:(NSUInteger)roundLimit
             workingDirectory:(NSString *)workingDirectory
{
  return [self initWithModelIdentifier:modelIdentifier
             providerAccountIdentifier:@""
                assistantSetIdentifier:assistantSetIdentifier
                           webProvider:webProvider
                      webSearchEnabled:webSearchEnabled
                           bashEnabled:bashEnabled
                        limitToOneTool:limitToOneTool
                            roundLimit:roundLimit
                      workingDirectory:workingDirectory];
}

- (id)initWithModelIdentifier:(NSString *)modelIdentifier
    providerAccountIdentifier:(NSString *)providerAccountIdentifier
       assistantSetIdentifier:(NSString *)assistantSetIdentifier
                  webProvider:(NSString *)webProvider
             webSearchEnabled:(BOOL)webSearchEnabled
                  bashEnabled:(BOOL)bashEnabled
               limitToOneTool:(BOOL)limitToOneTool
                   roundLimit:(NSUInteger)roundLimit
             workingDirectory:(NSString *)workingDirectory
{
  if ((self = [super init])) {
    [self setModelIdentifier:modelIdentifier];
    [self setProviderAccountIdentifier:providerAccountIdentifier];
    [self setAssistantSetIdentifier:assistantSetIdentifier];
    [self setWebProvider:webProvider];
    [self setWebSearchEnabled:webSearchEnabled];
    [self setBashEnabled:bashEnabled];
    [self setLimitToOneTool:limitToOneTool];
    [self setAnswerQualityEnabled:NO];
    [self setRoundLimit:roundLimit];
    [self setWorkingDirectory:workingDirectory];
  }
  return self;
}

- (id)copyWithZone:(NSZone *)zone
{
  StrappySessionOptions *copy;

  copy = [[StrappySessionOptions allocWithZone:zone]
    initWithModelIdentifier:[self modelIdentifier]
     assistantSetIdentifier:[self assistantSetIdentifier]
                webProvider:[self webProvider]
           webSearchEnabled:[self webSearchEnabled]
                bashEnabled:[self bashEnabled]
             limitToOneTool:[self limitToOneTool]
                 roundLimit:[self roundLimit]
           workingDirectory:[self workingDirectory]];
  [copy setProviderAccountIdentifier:[self providerAccountIdentifier]];
  [copy setAnswerQualityEnabled:[self answerQualityEnabled]];
  return copy;
}

- (NSString *)modelIdentifier
{
  return modelIdentifier_;
}

- (void)setModelIdentifier:(NSString *)modelIdentifier
{
  NSString *value;

  value = [modelIdentifier isKindOfClass:[NSString class]] ?
    modelIdentifier : @"";
  if (modelIdentifier_ != value) {
    [modelIdentifier_ release];
    modelIdentifier_ = [value copy];
  }
}

- (NSString *)providerAccountIdentifier
{
  return providerAccountIdentifier_;
}

- (void)setProviderAccountIdentifier:(NSString *)providerAccountIdentifier
{
  NSString *value;

  value = [providerAccountIdentifier isKindOfClass:[NSString class]] ?
    providerAccountIdentifier : @"";
  if (providerAccountIdentifier_ != value) {
    [providerAccountIdentifier_ release];
    providerAccountIdentifier_ = [value copy];
  }
}

- (NSString *)assistantSetIdentifier
{
  return assistantSetIdentifier_;
}

- (void)setAssistantSetIdentifier:(NSString *)assistantSetIdentifier
{
  NSString *value;

  value = [assistantSetIdentifier isKindOfClass:[NSString class]] ?
    assistantSetIdentifier : @"";
  if (assistantSetIdentifier_ != value) {
    [assistantSetIdentifier_ release];
    assistantSetIdentifier_ = [value copy];
  }
}

- (NSString *)webProvider
{
  return webProvider_;
}

- (void)setWebProvider:(NSString *)webProvider
{
  NSString *value;

  value = StrappySessionWebProviderFromValue(webProvider);
  if (webProvider_ != value) {
    [webProvider_ release];
    webProvider_ = [value copy];
  }
}

- (BOOL)webSearchEnabled
{
  return webSearchEnabled_;
}

- (void)setWebSearchEnabled:(BOOL)enabled
{
  webSearchEnabled_ = enabled ? YES : NO;
}

- (BOOL)bashEnabled
{
  return bashEnabled_;
}

- (void)setBashEnabled:(BOOL)enabled
{
  bashEnabled_ = enabled ? YES : NO;
}

- (BOOL)limitToOneTool
{
  return limitToOneTool_;
}

- (void)setLimitToOneTool:(BOOL)enabled
{
  limitToOneTool_ = enabled ? YES : NO;
}

- (BOOL)answerQualityEnabled
{
  return answerQualityEnabled_;
}

- (void)setAnswerQualityEnabled:(BOOL)enabled
{
  answerQualityEnabled_ = enabled ? YES : NO;
}

- (NSUInteger)roundLimit
{
  return roundLimit_;
}

- (void)setRoundLimit:(NSUInteger)roundLimit
{
  roundLimit_ = roundLimit;
}

- (NSString *)workingDirectory
{
  return workingDirectory_;
}

- (void)setWorkingDirectory:(NSString *)workingDirectory
{
  NSString *value;

  value = [workingDirectory isKindOfClass:[NSString class]] ?
    workingDirectory : @"";
  if (workingDirectory_ != value) {
    [workingDirectory_ release];
    workingDirectory_ = [value copy];
  }
}

- (void)dealloc
{
  [modelIdentifier_ release];
  [providerAccountIdentifier_ release];
  [assistantSetIdentifier_ release];
  [webProvider_ release];
  [workingDirectory_ release];
  [super dealloc];
}

@end

static StrappySessionOptions *StrappySessionOptionsFromSummary(
  NSDictionary *summary,
  NSString *workingDirectory)
{
  NSString *modelIdentifier;
  NSString *providerAccountIdentifier;
  NSString *assistantSetIdentifier;
  StrappySessionOptions *options;

  modelIdentifier = [summary objectForKey:@"model"];
  if (![modelIdentifier isKindOfClass:[NSString class]]) {
    modelIdentifier = @"";
  }
  providerAccountIdentifier = [summary objectForKey:@"provider_account_id"];
  if (![providerAccountIdentifier isKindOfClass:[NSString class]]) {
    providerAccountIdentifier = @"";
  }
  assistantSetIdentifier = [summary objectForKey:@"assistant_set_id"];
  if (![assistantSetIdentifier isKindOfClass:[NSString class]] ||
      ([assistantSetIdentifier length] == 0U)) {
    assistantSetIdentifier = @"personal_assistant";
  }
  options = [[[StrappySessionOptions alloc]
    initWithModelIdentifier:modelIdentifier
     assistantSetIdentifier:assistantSetIdentifier
                webProvider:StrappySessionWebProviderFromSummary(summary)
           webSearchEnabled:StrappySessionWebSearchEnabledFromSummary(summary)
                bashEnabled:StrappySessionBashEnabledFromSummary(summary)
             limitToOneTool:StrappySessionLimitToOneToolFromSummary(summary)
                 roundLimit:StrappySessionLimitFromSummary(
                summary,
                @"round_limit",
                StrappySessionDefaultRoundLimit)
           workingDirectory:workingDirectory]
    autorelease];
  [options setProviderAccountIdentifier:providerAccountIdentifier];
  [options setAnswerQualityEnabled:
    StrappySessionAnswerQualityEnabledFromSummary(summary)];
  return options;
}

static StrappySessionOptions *StrappySessionOptionsFromRecord(
  const strappy_session_options *options)
{
  NSString *modelIdentifier;
  NSString *assistantSetIdentifier;
  NSString *workingDirectory;
  StrappySessionOptions *sessionOptions;

  if (options == NULL) {
    return nil;
  }
  modelIdentifier = (options->model_id != NULL) ?
    [NSString stringWithUTF8String:options->model_id] : @"";
  assistantSetIdentifier = (options->assistant_set_id != NULL) ?
    [NSString stringWithUTF8String:options->assistant_set_id] : @"";
  workingDirectory = (options->working_directory != NULL) ?
    [NSString stringWithUTF8String:options->working_directory] : @"";
  sessionOptions = [[[StrappySessionOptions alloc]
    initWithModelIdentifier:modelIdentifier
     assistantSetIdentifier:assistantSetIdentifier
                webProvider:StrappySessionWebProviderFromRecord(
                  options->web_provider)
           webSearchEnabled:(options->web_search_enabled ? YES : NO)
                bashEnabled:(options->bash_enabled ? YES : NO)
             limitToOneTool:(options->limit_to_one_tool ? YES : NO)
                 roundLimit:(NSUInteger)options->round_limit
           workingDirectory:workingDirectory]
    autorelease];
  [sessionOptions setProviderAccountIdentifier:
    ((options->provider_account_id != NULL) ?
      [NSString stringWithUTF8String:options->provider_account_id] : @"")];
  [sessionOptions setAnswerQualityEnabled:
    (options->answer_quality_enabled ? YES : NO)];
  return sessionOptions;
}

static BOOL StrappySessionRecordFromOptions(
  StrappySessionOptions *options,
  strappy_session_options *record,
  NSError **error)
{
  strappy_web_provider provider;
  NSUInteger roundLimit;

  roundLimit = [options isKindOfClass:[StrappySessionOptions class]] ?
    [options roundLimit] : 0U;
  if (![options isKindOfClass:[StrappySessionOptions class]] ||
      (record == NULL) ||
      (roundLimit == 0U) ||
      (roundLimit > StrappySessionMaximumLimit) ||
      !strappy_web_provider_parse([[options webProvider] UTF8String],
                                  &provider)) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Session options are invalid.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return NO;
  }
  record->model_id = (char *)[[options modelIdentifier] UTF8String];
  record->provider_account_id =
    (char *)[[options providerAccountIdentifier] UTF8String];
  record->assistant_set_id =
    (char *)[[options assistantSetIdentifier] UTF8String];
  record->working_directory =
    (char *)[[options workingDirectory] fileSystemRepresentation];
  record->web_provider = provider;
  record->web_search_enabled = [options webSearchEnabled] ? 1 : 0;
  record->bash_enabled = [options bashEnabled] ? 1 : 0;
  record->limit_to_one_tool = [options limitToOneTool] ? 1 : 0;
  record->answer_quality_enabled = [options answerQualityEnabled] ? 1 : 0;
  record->round_limit = (long)roundLimit;
  return YES;
}

@implementation StrappySession

+ (NSString *)webViewBatchedJavaScriptForJavaScript:(NSString *)javaScript
{
  if (![javaScript isKindOfClass:[NSString class]] ||
      ([javaScript length] == 0U)) {
    return @"";
  }

  return StrappySessionStringFromCString(
    strappy_session_webview_batched_js([javaScript UTF8String]));
}

+ (NSString *)webViewEmptyMessagesPageHTMLWithPalette:
    (StrappyWebViewPalette)palette
{
  strappy_webview_palette webViewPalette;

  webViewPalette =
    (palette == StrappyWebViewPaletteNeutral) ?
      STRAPPY_WEBVIEW_PALETTE_NEUTRAL :
      STRAPPY_WEBVIEW_PALETTE_APPLICATION_TINTED;
  return StrappySessionStringFromCString(
    strappy_webview_messages_page_html("",
                                       "{}",
                                       NULL,
                                       0U,
                                       NULL,
                                       NULL,
                                       webViewPalette));
}

- (int)handleResponsesEvent:(const strappy_responses_event *)event
           responsesContext:(StrappySessionResponsesContext *)responsesContext
{
  NSDictionary *contextDictionary;
  NSMutableDictionary *notification;
  NSMutableString *mutations;
  NSError *renderError;
  NSString *appendJavaScript;
  NSString *javaScript;
  NSString *messageKey;
  NSString *nextCursor;
  NSString *statusJSON;
  NSString *streamEvent;
  NSString *updateJavaScript;
  BOOL eventRenderFailed;
  BOOL terminalPending;

  if (event == NULL) {
    return 1;
  }
  if (event->type == STRAPPY_RESPONSES_EVENT_CANCELLATION_POLL) {
    return [self promptCancellationRequested] ? 0 : 1;
  }
  if (event->type == STRAPPY_RESPONSES_EVENT_SESSION_UPDATED) {
    NSDictionary *summary;
    NSDictionary *update;
    NSError *summaryError;

    /* Session metadata is native UI state. Reload it from SQLite and never
     * route this event through the WebView stream or JavaScript renderer. */
    summaryError = nil;
    summary = [StrappySession
      sessionSummaryForSessionIdentifier:[self sessionIdentifier]
                                   error:&summaryError];
    if ([summary isKindOfClass:[NSDictionary class]]) {
      update = [[NSDictionary alloc] initWithObjectsAndKeys:
        summary, @"session",
        StrappySessionChangeKindName, StrappySessionChangeKindKey,
        nil];
      [self performSelectorOnMainThread:@selector(postSessionUpdateAndRelease:)
                             withObject:update
                          waitUntilDone:NO];
    } else {
      NSLog(@"StrappyResponses could not refresh the renamed session: %@",
            ([summaryError localizedDescription] != nil) ?
              [summaryError localizedDescription] : @"unknown summary error");
    }
    return 1;
  }
  if ((event->type != STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) &&
      (event->type != STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED) &&
      (event->type != STRAPPY_RESPONSES_EVENT_LEDGER_UPDATED)) {
    return 1;
  }

  contextDictionary = (responsesContext != NULL) ?
    responsesContext->context : nil;
  notification = [[NSMutableDictionary alloc] init];
  appendJavaScript = nil;
  javaScript = nil;
  messageKey = nil;
  nextCursor = nil;
  renderError = nil;
  statusJSON = nil;
  streamEvent = (event->type == STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) ?
    @"processing_status" :
    ((event->type == STRAPPY_RESPONSES_EVENT_LEDGER_UPDATED) ?
      @"ledger_updated" :
      (event->coalesce_with_next_ledger_change ?
        @"ledger_changed_coalescible" : @"ledger_changed"));
  updateJavaScript = nil;
  eventRenderFailed = NO;
  if (contextDictionary != nil) {
    [notification setObject:contextDictionary forKey:@"context"];
  }
  if (event->message_key != NULL) {
    messageKey = [NSString stringWithUTF8String:event->message_key];
    if (messageKey != nil) {
      [notification setObject:messageKey forKey:@"message_key"];
    }
  }
  if (event->status_json != NULL) {
    statusJSON = [NSString stringWithUTF8String:event->status_json];
    if (statusJSON != nil) {
      [notification setObject:statusJSON forKey:@"status_json"];
    }
  }
  if (event->type == STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) {
    [self updateProcessingStatusJSON:statusJSON];
  }
  [notification setObject:streamEvent forKey:@"stream_event"];
  if (event->is_terminal) {
    [notification setObject:[NSNumber numberWithBool:YES]
                     forKey:@"is_terminal"];
  }

  if (event->type == STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED) {
    appendJavaScript = [self
      webViewAppendMessagesJavaScriptAfterTimelineCursor:
        ((responsesContext != NULL) ? responsesContext->timelineCursor : nil)
                                      nextTimelineCursor:&nextCursor
                                    appendedMessageCount:NULL
                                          renderContext:
        ((responsesContext != NULL) ?
          responsesContext->webViewRenderContext : NULL)
                                                   error:&renderError];
    if (![appendJavaScript isKindOfClass:[NSString class]]) {
      eventRenderFailed = YES;
      appendJavaScript = @"";
      NSLog(@"StrappyResponses could not render a WebView append: %@",
            ([renderError localizedDescription] != nil) ?
              [renderError localizedDescription] : @"unknown render error");
    }
    if ([nextCursor isKindOfClass:[NSString class]] &&
        (responsesContext != NULL)) {
      [responsesContext->timelineCursor release];
      responsesContext->timelineCursor = [nextCursor copy];
    }
    if (eventRenderFailed && (responsesContext != NULL)) {
      responsesContext->presentationReloadRequired = YES;
    }
    if (event->is_terminal && (responsesContext != NULL)) {
      /* Publish the final append only after its late status update arrives. */
      StrappySessionResponsesContextClearTerminal(responsesContext);
      responsesContext->terminalAppendJavaScript =
        [([appendJavaScript isKindOfClass:[NSString class]] ?
          appendJavaScript : @"") copy];
      responsesContext->terminalMessageKey =
        [([messageKey isKindOfClass:[NSString class]] ? messageKey : @"") copy];
      [notification release];
      return 1;
    }

    if (!strappy_session_webview_event_requires_message_update(event)) {
      /* The request is visible in the append above, but a running HTTP
       * attempt intentionally has no response-call timeline row yet. */
      updateJavaScript = @"";
    } else {
      renderError = nil;
      updateJavaScript = [self
        webViewJavaScriptForStreamEvent:notification
                          renderContext:
          ((responsesContext != NULL) ?
            responsesContext->webViewRenderContext : NULL)
                                  error:&renderError];
      if (![updateJavaScript isKindOfClass:[NSString class]]) {
        eventRenderFailed = YES;
        updateJavaScript = @"";
        NSLog(@"StrappyResponses could not render a WebView update: %@",
              ([renderError localizedDescription] != nil) ?
                [renderError localizedDescription] : @"unknown render error");
      }
    }
    mutations = [NSMutableString string];
    [mutations appendString:appendJavaScript];
    [mutations appendString:updateJavaScript];
    javaScript =
      [StrappySession webViewBatchedJavaScriptForJavaScript:mutations];
    if (([mutations length] > 0U) && ([javaScript length] == 0U)) {
      eventRenderFailed = YES;
    }
  } else {
    javaScript = [self
      webViewJavaScriptForStreamEvent:notification
                        renderContext:
        ((responsesContext != NULL) ?
          responsesContext->webViewRenderContext : NULL)
                                error:&renderError];
    if (![javaScript isKindOfClass:[NSString class]]) {
      eventRenderFailed = YES;
      javaScript = @"";
      NSLog(@"StrappyResponses could not render a WebView event: %@",
            ([renderError localizedDescription] != nil) ?
              [renderError localizedDescription] : @"unknown render error");
    }
  }

  if (eventRenderFailed && (responsesContext != NULL)) {
    responsesContext->presentationReloadRequired = YES;
  }

  terminalPending = (responsesContext != NULL) &&
    (responsesContext->terminalMessageKey != nil);
  if (terminalPending &&
      (event->type == STRAPPY_RESPONSES_EVENT_LEDGER_UPDATED) &&
      [messageKey isEqualToString:responsesContext->terminalMessageKey]) {
    [responsesContext->terminalUpdateJavaScript release];
    responsesContext->terminalUpdateJavaScript =
      [([javaScript isKindOfClass:[NSString class]] ? javaScript : @"") copy];
    [notification release];
    return 1;
  }

  if (terminalPending &&
      (event->type == STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) &&
      (event->status_kind == NULL)) {
    /* Keep rounds expanded through the final append and its scroll animation. */
    mutations = [NSMutableString string];
    if ([responsesContext->terminalUpdateJavaScript length] > 0U) {
      [mutations appendString:responsesContext->terminalUpdateJavaScript];
    }
    if ([responsesContext->terminalAppendJavaScript length] > 0U) {
      [mutations appendString:responsesContext->terminalAppendJavaScript];
    }
    if ([javaScript isKindOfClass:[NSString class]]) {
      [mutations appendString:javaScript];
    }
    javaScript =
      [StrappySession webViewBatchedJavaScriptForJavaScript:mutations];
    if (([mutations length] > 0U) && ([javaScript length] == 0U) &&
        (responsesContext != NULL)) {
      responsesContext->presentationReloadRequired = YES;
    }
    [notification setObject:@"terminal_delta" forKey:@"stream_event"];
    [notification setObject:[NSNumber numberWithBool:YES]
                     forKey:@"is_terminal"];
    [notification setObject:responsesContext->terminalMessageKey
                     forKey:@"message_key"];
  }

  if ([javaScript isKindOfClass:[NSString class]] &&
      ([javaScript length] > 0U)) {
    [notification setObject:javaScript forKey:@"webview_javascript"];
  }
  if ((responsesContext != NULL) &&
      [responsesContext->timelineCursor isKindOfClass:[NSString class]]) {
    [notification setObject:responsesContext->timelineCursor
                     forKey:@"timeline_cursor"];
  }
  if ((responsesContext != NULL) &&
      responsesContext->presentationReloadRequired) {
    [notification setObject:[NSNumber numberWithBool:YES]
                     forKey:@"webview_reload_required"];
  }
  [self performSelectorOnMainThread:@selector(postStreamEventAndRelease:)
                         withObject:notification
                      waitUntilDone:NO];
  if (terminalPending &&
      (event->type == STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) &&
      (event->status_kind == NULL)) {
    StrappySessionResponsesContextClearTerminal(responsesContext);
  }
  return 1;
}

+ (NSMutableDictionary *)inFlightSessions
{
  if (StrappySessionInFlightSessions == nil) {
    StrappySessionInFlightSessions = [[NSMutableDictionary alloc] init];
  }
  return StrappySessionInFlightSessions;
}

+ (void)registerInFlightSession:(StrappySession *)session
{
  NSNumber *identifier;

  if (![session isKindOfClass:[StrappySession class]]) {
    return;
  }

  identifier = [session sessionIdentifier];
  if (identifier == nil) {
    return;
  }

  @synchronized(self) {
    [[self inFlightSessions] setObject:session forKey:identifier];
  }
}

+ (void)unregisterInFlightSession:(StrappySession *)session
{
  NSNumber *identifier;

  if (![session isKindOfClass:[StrappySession class]]) {
    return;
  }

  identifier = [session sessionIdentifier];
  if (identifier == nil) {
    return;
  }

  @synchronized(self) {
    if ([[self inFlightSessions] objectForKey:identifier] == session) {
      [[self inFlightSessions] removeObjectForKey:identifier];
    }
  }
}

+ (StrappySession *)inFlightSessionForIdentifier:(NSNumber *)identifier
{
  StrappySession *session;

  if (![identifier isKindOfClass:[NSNumber class]]) {
    return nil;
  }

  @synchronized(self) {
    session = [[[self inFlightSessions] objectForKey:identifier] retain];
  }
  return [session autorelease];
}

+ (NSUInteger)inFlightSessionCount
{
  NSUInteger count;

  @synchronized(self) {
    count = [[self inFlightSessions] count];
  }
  return count;
}

+ (BOOL)hasInFlightSessions
{
  return ([self inFlightSessionCount] > 0U) ? YES : NO;
}

+ (BOOL)isPromptInFlightForSessionIdentifier:(NSNumber *)sessionIdentifier
{
  StrappySession *session;

  session = [self inFlightSessionForIdentifier:sessionIdentifier];
  return ((session != nil) && [session isPromptInFlight]) ? YES : NO;
}

+ (BOOL)isModelCatalogRefreshInFlight
{
  BOOL inFlight;

  @synchronized(self) {
    inFlight = StrappySessionModelCatalogRefreshInFlight;
  }
  return inFlight;
}

+ (StrappySession *)sessionWithIdentifier:(NSNumber *)sessionIdentifier
{
  StrappySession *session;

  if (![sessionIdentifier isKindOfClass:[NSNumber class]]) {
    return nil;
  }

  session = [self inFlightSessionForIdentifier:sessionIdentifier];
  if (session != nil) {
    return session;
  }

  return [[[self alloc] initWithSessionIdentifier:sessionIdentifier
                                         summary:nil] autorelease];
}

+ (StrappySession *)sessionWithSummary:(NSDictionary *)summary
{
  NSNumber *identifier;
  StrappySession *session;

  if (![summary isKindOfClass:[NSDictionary class]]) {
    return nil;
  }

  identifier = [summary objectForKey:@"id"];
  if (![identifier isKindOfClass:[NSNumber class]]) {
    return nil;
  }

  session = [self inFlightSessionForIdentifier:identifier];
  if (session != nil) {
    [session updateCachedSummary:summary];
    return session;
  }

  return [[[self alloc] initWithSessionIdentifier:identifier
                                         summary:summary] autorelease];
}

- (id)initWithSessionIdentifier:(NSNumber *)sessionIdentifier
                        summary:(NSDictionary *)summary
{
  if (![sessionIdentifier isKindOfClass:[NSNumber class]] ||
      ([sessionIdentifier longLongValue] <= 0LL)) {
    [self release];
    [NSException raise:NSInvalidArgumentException
                format:@"[StrappySession initWithSessionIdentifier:summary:] sessionIdentifier is required"];
    return nil;
  }

  if ((self = [super init])) {
    sessionIdentifier_ = [sessionIdentifier retain];
    options_ = [StrappySessionOptionsFromSummary(nil, @"") retain];
    optionsLoaded_ = NO;
    if ([summary isKindOfClass:[NSDictionary class]]) {
      cachedSummary_ = [summary retain];
      [options_ release];
      options_ = [StrappySessionOptionsFromSummary(summary, @"") retain];
    }
  }
  return self;
}

- (void)dealloc
{
  [StrappySession unregisterInFlightSession:self];
  [sessionIdentifier_ release];
  [cachedSummary_ release];
  [options_ release];
  [processingStatusJSON_ release];
  [super dealloc];
}

- (NSNumber *)sessionIdentifier
{
  return sessionIdentifier_;
}

- (NSDictionary *)cachedSummary
{
  return cachedSummary_;
}

- (void)updateCachedSummary:(NSDictionary *)summary
{
  StrappySessionOptions *updatedOptions;
  NSString *workingDirectory;

  if (![summary isKindOfClass:[NSDictionary class]]) {
    return;
  }

  @synchronized(self) {
    if (cachedSummary_ != summary) {
      [cachedSummary_ release];
      cachedSummary_ = [summary retain];
    }
    workingDirectory = optionsLoaded_ ? [options_ workingDirectory] : @"";
    updatedOptions = StrappySessionOptionsFromSummary(summary,
                                                      workingDirectory);
    [options_ release];
    options_ = [updatedOptions retain];
  }
}

- (NSString *)currentProcessingStatusJSON
{
  NSString *statusJSON;

  @synchronized(self) {
    statusJSON = [processingStatusJSON_ retain];
  }
  return [statusJSON autorelease];
}

- (void)updateProcessingStatusJSON:(NSString *)statusJSON
{
  if (![statusJSON isKindOfClass:[NSString class]] ||
      ([statusJSON length] == 0U)) {
    statusJSON = nil;
  }

  @synchronized(self) {
    if (processingStatusJSON_ != statusJSON) {
      [processingStatusJSON_ release];
      processingStatusJSON_ = [statusJSON copy];
    }
  }
}

- (BOOL)isPromptInFlight
{
  BOOL inFlight;

  @synchronized(self) {
    inFlight = promptInFlight_;
  }
  return inFlight;
}

- (BOOL)isDatabaseStudySession
{
  return [[[self optionsWithError:nil] assistantSetIdentifier] isEqualToString:
    [NSString stringWithUTF8String:STRAPPY_ASSISTANT_SET_DATABASE_STUDY]];
}

- (BOOL)promptCancellationRequested
{
  BOOL requested;

  @synchronized(self) {
    requested = promptCancellationRequested_;
  }
  return requested;
}

- (void)cancelPrompt
{
  @synchronized(self) {
    if (promptInFlight_) {
      promptCancellationRequested_ = YES;
    }
  }
}

- (void)postStreamEventAndRelease:(NSDictionary *)event
{
  if ([event isKindOfClass:[NSDictionary class]]) {
    [[NSNotificationCenter defaultCenter]
      postNotificationName:StrappySessionStreamEventNotification
                    object:self
                  userInfo:event];
  }
  [event release];
}

- (void)postSessionUpdateAndRelease:(NSDictionary *)update
{
  NSDictionary *summary;

  summary = [update objectForKey:@"session"];
  if ([summary isKindOfClass:[NSDictionary class]]) {
    [self updateCachedSummary:summary];
    [[NSNotificationCenter defaultCenter]
      postNotificationName:StrappySessionDidUpdateNotification
                    object:self
                  userInfo:update];
  }
  [update release];
}

+ (void)bootstrapProcessWithCACertPath:(NSString *)caCertPath
{
  char *strappyError;
  NSString *fontsPath;
  int ok;

  if (![caCertPath isKindOfClass:[NSString class]] || ([caCertPath length] == 0U)) {
    [NSException raise:NSInvalidArgumentException
                format:@"[StrappySession bootstrapProcessWithCACertPath:] caCertPath is required"];
  }

  fontsPath = [[[NSBundle mainBundle] resourcePath]
    stringByAppendingPathComponent:@"Fonts"];
  if ([[NSProcessInfo processInfo] XP_platformFamily] ==
      XPPlatformFamilyMacOS) {
    /* WKWebView may only read files below AIWebViewController's read-access
     * URL. Keep the web fonts beside session.html instead of pointing CSS at
     * the app bundle, which is outside that subtree and may also be mounted
     * at a transient App Translocation path. */
    fontsPath = StrappyStageWebViewFonts();
    if (fontsPath == nil) {
      [NSException raise:NSInvalidArgumentException
                  format:@"Could not stage Strappy WebView fonts."];
    }
  }

  strappyError = NULL;
  ok = strappy_session_configure_process([caCertPath fileSystemRepresentation],
                                         [fontsPath fileSystemRepresentation],
                                         &strappyError);
  if (ok) {
    ok = strappy_openai_oauth_set_cainfo(
      [caCertPath fileSystemRepresentation],
      &strappyError);
  }
  if (!ok) {
    NSString *message = nil;
    if (strappyError != NULL) {
      message = [NSString stringWithUTF8String:strappyError];
    }
    strappy_session_free_string(strappyError);
    [NSException raise:NSInvalidArgumentException
                format:@"%@", (message ? message : @"Could not bootstrap Strappy.")];
  }
  strappy_responses_set_provider_credentials_callback(
    StrappySessionCopyChatGPTCredentials,
    NULL);
  (void)[StrappySession assistantSetCatalog];
}

+ (NSString *)guidanceResourceDirectoryWithError:(NSError **)error
{
  NSString *resourcePath;

  resourcePath = [[NSBundle mainBundle] resourcePath];
  if ([resourcePath isKindOfClass:[NSString class]] &&
      ([resourcePath length] > 0U)) {
    return resourcePath;
  }

  if (error != nil) {
    NSDictionary *userInfo =
      [NSDictionary dictionaryWithObject:NSLocalizedString(@"Prompt guidance resources are missing from the app bundle.", nil)
                                  forKey:NSLocalizedDescriptionKey];
    *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                 code:7
                             userInfo:userInfo];
  }

  return nil;
}

+ (NSString *)stringFromCStringOrEmpty:(const char *)value
{
  NSString *string;

  if (value == NULL) {
    return @"";
  }

  string = [NSString stringWithUTF8String:value];
  if (string == nil) {
    return @"";
  }

  return string;
}

+ (NSDictionary *)dictionaryFromSessionRecord:(const strappy_session_record *)record
{
  NSNumber *sessionId;
  NSNumber *httpStatus;
  NSNumber *webSearchEnabled;
  NSNumber *bashEnabled;
  NSNumber *limitToOneTool;
  NSNumber *answerQualityEnabled;
  NSNumber *roundLimit;
  NSString *name;
  NSString *prompt;
  NSString *response;
  NSString *model;
  NSString *modelName;
  NSString *webProvider;
  NSString *assistantSetIdentifier;
  NSString *createdAt;
  NSString *lastActivityAt;

  if (record == NULL) {
    return nil;
  }

  sessionId = [NSNumber numberWithLongLong:record->session_id];
  httpStatus = [NSNumber numberWithLong:record->http_status];
  webProvider = StrappySessionWebProviderFromRecord(record->web_provider);
  webSearchEnabled =
    [NSNumber numberWithBool:(record->web_search_enabled ? YES : NO)];
  bashEnabled = [NSNumber numberWithBool:(record->bash_enabled ? YES : NO)];
  limitToOneTool =
    [NSNumber numberWithBool:(record->limit_to_one_tool ? YES : NO)];
  answerQualityEnabled =
    [NSNumber numberWithBool:(record->answer_quality_enabled ? YES : NO)];
  roundLimit = [NSNumber numberWithLong:record->round_limit];
  name = [StrappySession stringFromCStringOrEmpty:record->name];
  prompt = [StrappySession stringFromCStringOrEmpty:record->prompt];
  response = [StrappySession stringFromCStringOrEmpty:record->response];
  model = [StrappySession stringFromCStringOrEmpty:record->model];
  modelName = [StrappySession stringFromCStringOrEmpty:record->model_name];
  assistantSetIdentifier =
    [StrappySession stringFromCStringOrEmpty:record->assistant_set_id];
  createdAt = [StrappySession stringFromCStringOrEmpty:record->created_at];
  lastActivityAt =
    [StrappySession stringFromCStringOrEmpty:record->last_activity_at];

  return [NSDictionary dictionaryWithObjectsAndKeys:
    sessionId, @"id",
    name, @"name",
    prompt, @"prompt",
    response, @"response",
    model, @"model",
    modelName, @"model_name",
    assistantSetIdentifier, @"assistant_set_id",
    httpStatus, @"http_status",
    webProvider, @"web_provider",
    webSearchEnabled, @"web_search_enabled",
    bashEnabled, @"bash_enabled",
    limitToOneTool, @"limit_to_one_tool",
    answerQualityEnabled, @"answer_quality_enabled",
    roundLimit, @"round_limit",
    createdAt, @"created_at",
    lastActivityAt, @"last_message_at",
    lastActivityAt, @"last_activity_at",
    [NSNumber numberWithLongLong:record->last_activity_at_ms],
    @"last_activity_at_ms",
    nil];
}

+ (NSDictionary *)dictionaryFromAssistantSetRecord:
    (const strappy_assistant_set_record *)record
{
  NSString *identifier;
  NSString *displayName;
  NSString *detail;
  NSString *availability;
  BOOL available;

  if (record == NULL) {
    return nil;
  }
  identifier = [StrappySession stringFromCStringOrEmpty:record->identifier];
  displayName = [StrappySession stringFromCStringOrEmpty:record->display_name];
  detail = [StrappySession stringFromCStringOrEmpty:record->detail];
  availability =
    [StrappySession stringFromCStringOrEmpty:record->availability];
  available = [availability isEqualToString:@"available"] ? YES : NO;
  return [NSDictionary dictionaryWithObjectsAndKeys:
    identifier, @"id",
    NSLocalizedString(displayName, nil), @"name",
    NSLocalizedString(detail, nil), @"detail",
    availability, @"availability",
    [NSNumber numberWithBool:available], @"available",
    nil];
}

+ (NSDictionary *)dictionaryFromSessionMessageRecord:
    (const strappy_session_message_record *)record
{
  NSNumber *messageId;
  NSNumber *sessionId;
  NSNumber *turnId;
  NSNumber *modelRequestId;
  NSNumber *httpAttemptId;
  NSNumber *httpStatus;
  NSNumber *includeInContext;
  NSNumber *isError;
  NSNumber *promptIndex;
  NSNumber *roundIndex;
  NSNumber *attemptIndex;
  NSNumber *cumulativeUsageCost;
  NSNumber *hasCumulativeUsageCost;
  NSNumber *cumulativeWaitMilliseconds;
  NSNumber *hasCumulativeWaitMilliseconds;
  NSString *turnKey;
  NSString *promptGroupKey;
  NSString *actor;
  NSString *kind;
  NSString *apiRole;
  NSString *renderRole;
  NSString *role;
  NSString *content;
  NSString *model;
  NSString *metadataJSON;
  NSString *renderStateJSON;
  NSString *messageJSON;
  NSString *reasoning;
  NSString *messageKey;
  NSString *targetMessageKey;
  NSString *direction;
  NSString *toolCallId;
  NSString *toolName;
  NSString *argumentsJSON;
  NSString *resultJSON;
  NSString *responseItemActionJSON;
  NSString *responseItemURL;
  NSString *responseItemTitle;
  NSString *responseItemStatus;
  NSString *responseItemHTTPStatus;
  NSString *requestMethod;
  NSString *requestEndpoint;
  NSString *createdAt;
  NSString *attemptState;

  if (record == NULL) {
    return nil;
  }

  messageId = [NSNumber numberWithLongLong:record->message_id];
  sessionId = [NSNumber numberWithLongLong:record->session_id];
  turnId = [NSNumber numberWithLongLong:record->turn_id];
  modelRequestId = [NSNumber numberWithLongLong:record->model_request_id];
  httpAttemptId = [NSNumber numberWithLongLong:record->http_attempt_id];
  promptIndex = [NSNumber numberWithLong:record->prompt_index];
  roundIndex = [NSNumber numberWithLong:record->round_index];
  attemptIndex = [NSNumber numberWithLong:record->attempt_index];
  cumulativeUsageCost =
    [NSNumber numberWithDouble:record->cumulative_usage_cost];
  hasCumulativeUsageCost =
    [NSNumber numberWithBool:(record->has_cumulative_usage_cost ? YES : NO)];
  cumulativeWaitMilliseconds =
    [NSNumber numberWithLongLong:record->cumulative_wait_ms];
  hasCumulativeWaitMilliseconds =
    [NSNumber numberWithBool:(record->has_cumulative_wait_ms ? YES : NO)];
  httpStatus = [NSNumber numberWithLong:record->http_status];
  includeInContext = [NSNumber numberWithBool:(record->include_in_context ? YES : NO)];
  isError = [NSNumber numberWithBool:(record->is_error ? YES : NO)];
  turnKey = [StrappySession stringFromCStringOrEmpty:record->turn_key];
  promptGroupKey =
    [StrappySession stringFromCStringOrEmpty:record->prompt_group_key];
  actor = [StrappySession stringFromCStringOrEmpty:record->actor];
  kind = [StrappySession stringFromCStringOrEmpty:record->kind];
  apiRole = [StrappySession stringFromCStringOrEmpty:record->api_role];
  renderRole = [StrappySession stringFromCStringOrEmpty:record->render_role];
  role = [StrappySession stringFromCStringOrEmpty:record->role];
  content = [StrappySession stringFromCStringOrEmpty:record->content];
  model = [StrappySession stringFromCStringOrEmpty:record->model];
  metadataJSON = [StrappySession stringFromCStringOrEmpty:record->metadata_json];
  renderStateJSON =
    [StrappySession stringFromCStringOrEmpty:record->render_state_json];
  messageJSON = [StrappySession stringFromCStringOrEmpty:record->message_json];
  reasoning = [StrappySession stringFromCStringOrEmpty:record->reasoning];
  messageKey = [StrappySession stringFromCStringOrEmpty:record->message_key];
  targetMessageKey =
    [StrappySession stringFromCStringOrEmpty:record->target_message_key];
  direction = [StrappySession stringFromCStringOrEmpty:record->direction];
  toolCallId = [StrappySession stringFromCStringOrEmpty:record->tool_call_id];
  toolName = [StrappySession stringFromCStringOrEmpty:record->tool_name];
  argumentsJSON = [StrappySession stringFromCStringOrEmpty:record->arguments_json];
  resultJSON = [StrappySession stringFromCStringOrEmpty:record->result_json];
  responseItemActionJSON = [StrappySession stringFromCStringOrEmpty:
    record->response_item_action_json];
  responseItemURL = [StrappySession stringFromCStringOrEmpty:
    record->response_item_url];
  responseItemTitle = [StrappySession stringFromCStringOrEmpty:
    record->response_item_title];
  responseItemStatus = [StrappySession stringFromCStringOrEmpty:
    record->response_item_status];
  responseItemHTTPStatus = [StrappySession stringFromCStringOrEmpty:
    record->response_item_http_status];
  requestMethod = [StrappySession stringFromCStringOrEmpty:
    record->request_method];
  requestEndpoint = [StrappySession stringFromCStringOrEmpty:
    record->request_endpoint];
  createdAt = [StrappySession stringFromCStringOrEmpty:record->created_at];
  attemptState = [StrappySession stringFromCStringOrEmpty:record->attempt_state];

  return [NSDictionary dictionaryWithObjectsAndKeys:
    messageId, @"id",
    sessionId, @"session_id",
    turnId, @"turn_id",
    modelRequestId, @"model_request_id",
    httpAttemptId, @"http_attempt_id",
    promptIndex, @"prompt_index",
    roundIndex, @"round_index",
    attemptIndex, @"attempt_index",
    cumulativeUsageCost, @"cumulative_usage_cost",
    hasCumulativeUsageCost, @"has_cumulative_usage_cost",
    cumulativeWaitMilliseconds, @"cumulative_wait_ms",
    hasCumulativeWaitMilliseconds, @"has_cumulative_wait_ms",
    turnKey, @"turn_key",
    promptGroupKey, @"prompt_group_key",
    actor, @"actor",
    kind, @"kind",
    apiRole, @"api_role",
    renderRole, @"render_role",
    role, @"role",
    content, @"text",
    model, @"model",
    metadataJSON, @"metadata_json",
    renderStateJSON, @"render_state_json",
    messageJSON, @"message_json",
    reasoning, @"reasoning",
    messageKey, @"message_key",
    targetMessageKey, @"target_message_key",
    direction, @"direction",
    toolCallId, @"tool_call_id",
    toolName, @"tool_name",
    argumentsJSON, @"arguments_json",
    resultJSON, @"result_json",
    responseItemActionJSON, @"response_item_action_json",
    responseItemURL, @"response_item_url",
    responseItemTitle, @"response_item_title",
    responseItemStatus, @"response_item_status",
    responseItemHTTPStatus, @"response_item_http_status",
    requestMethod, @"request_method",
    requestEndpoint, @"request_endpoint",
    includeInContext, @"include_in_context",
    isError, @"is_error",
    httpStatus, @"http_status",
    attemptState, @"attempt_state",
    createdAt, @"created_at",
    nil];
}

+ (NSDictionary *)dictionaryFromModelRecord:(const strappy_model_record *)record
{
  NSString *modelId;
  NSString *providerAccountId;
  NSString *providerId;
  NSString *providerName;
  NSString *providerAccountName;
  NSString *wireModelId;
  NSString *billingKind;
  NSString *canonicalSlug;
  NSString *huggingFaceId;
  NSString *name;
  NSString *description;
  NSString *architectureModality;
  NSString *architectureTokenizer;
  NSString *architectureInstructType;
  NSString *pricingPrompt;
  NSString *pricingCompletion;
  NSString *pricingRequest;
  NSString *pricingImage;
  NSString *pricingAudio;
  NSString *pricingWebSearch;
  NSString *pricingInternalReasoning;
  NSString *pricingInputCacheRead;
  NSString *pricingInputCacheWrite;
  NSString *knowledgeCutoff;
  NSString *expirationDate;
  NSString *linksDetails;
  NSString *linksJSON;
  NSString *reasoningJSON;
  NSString *benchmarksJSON;
  NSString *defaultParametersJSON;
  NSString *perRequestLimitsJSON;
  NSString *rawJSON;
  NSString *fetchedAt;

  if (record == NULL) {
    return nil;
  }

  modelId = [StrappySession stringFromCStringOrEmpty:record->model_id];
  providerAccountId =
    [StrappySession stringFromCStringOrEmpty:record->provider_account_id];
  providerId = [StrappySession stringFromCStringOrEmpty:record->provider_id];
  providerName = providerId;
  {
    NSArray *providers;
    NSUInteger providerIndex;

    providers = [StrappySession providerCatalog];
    for (providerIndex = 0U; providerIndex < [providers count]; providerIndex++) {
      NSDictionary *provider;

      provider = [providers objectAtIndex:providerIndex];
      if ([[provider objectForKey:@"id"] isEqualToString:providerId]) {
        NSString *candidate;

        candidate = [provider objectForKey:@"name"];
        if ([candidate isKindOfClass:[NSString class]]) providerName = candidate;
        break;
      }
    }
  }
  providerAccountName =
    [StrappySession stringFromCStringOrEmpty:record->provider_account_name];
  wireModelId =
    [StrappySession stringFromCStringOrEmpty:record->wire_model_id];
  billingKind =
    [StrappySession stringFromCStringOrEmpty:record->billing_kind];
  canonicalSlug =
    [StrappySession stringFromCStringOrEmpty:record->canonical_slug];
  huggingFaceId =
    [StrappySession stringFromCStringOrEmpty:record->hugging_face_id];
  name = [StrappySession stringFromCStringOrEmpty:record->name];
  description = [StrappySession stringFromCStringOrEmpty:record->description];
  architectureModality =
    [StrappySession stringFromCStringOrEmpty:record->architecture_modality];
  architectureTokenizer =
    [StrappySession stringFromCStringOrEmpty:record->architecture_tokenizer];
  architectureInstructType =
    [StrappySession stringFromCStringOrEmpty:record->architecture_instruct_type];
  pricingPrompt =
    [StrappySession stringFromCStringOrEmpty:record->pricing_prompt];
  pricingCompletion =
    [StrappySession stringFromCStringOrEmpty:record->pricing_completion];
  pricingRequest =
    [StrappySession stringFromCStringOrEmpty:record->pricing_request];
  pricingImage =
    [StrappySession stringFromCStringOrEmpty:record->pricing_image];
  pricingAudio =
    [StrappySession stringFromCStringOrEmpty:record->pricing_audio];
  pricingWebSearch =
    [StrappySession stringFromCStringOrEmpty:record->pricing_web_search];
  pricingInternalReasoning =
    [StrappySession stringFromCStringOrEmpty:record->pricing_internal_reasoning];
  pricingInputCacheRead =
    [StrappySession stringFromCStringOrEmpty:record->pricing_input_cache_read];
  pricingInputCacheWrite =
    [StrappySession stringFromCStringOrEmpty:record->pricing_input_cache_write];
  knowledgeCutoff =
    [StrappySession stringFromCStringOrEmpty:record->knowledge_cutoff];
  expirationDate =
    [StrappySession stringFromCStringOrEmpty:record->expiration_date];
  linksDetails =
    [StrappySession stringFromCStringOrEmpty:record->links_details];
  linksJSON = [StrappySession stringFromCStringOrEmpty:record->links_json];
  reasoningJSON =
    [StrappySession stringFromCStringOrEmpty:record->reasoning_json];
  benchmarksJSON =
    [StrappySession stringFromCStringOrEmpty:record->benchmarks_json];
  defaultParametersJSON =
    [StrappySession stringFromCStringOrEmpty:record->default_parameters_json];
  perRequestLimitsJSON =
    [StrappySession stringFromCStringOrEmpty:record->per_request_limits_json];
  rawJSON = [StrappySession stringFromCStringOrEmpty:record->raw_json];
  fetchedAt = [StrappySession stringFromCStringOrEmpty:record->fetched_at];

  return [NSDictionary dictionaryWithObjectsAndKeys:
    modelId, @"id",
    providerAccountId, @"provider_account_id",
    providerId, @"provider_id",
    providerName, @"provider_name",
    providerAccountName, @"provider_account_name",
    wireModelId, @"wire_model_id",
    billingKind, @"billing_kind",
    [NSNumber numberWithBool:(record->reasoning_enabled ? YES : NO)],
      @"reasoning_enabled",
    [NSNumber numberWithBool:(record->local_functions_enabled ? YES : NO)],
      @"local_functions_enabled",
    [NSNumber numberWithBool:(record->hosted_tools_enabled ? YES : NO)],
      @"hosted_tools_enabled",
    [NSNumber numberWithBool:(record->image_input_enabled ? YES : NO)],
      @"image_input_enabled",
    canonicalSlug, @"canonical_slug",
    huggingFaceId, @"hugging_face_id",
    name, @"name",
    description, @"description",
    [NSNumber numberWithLongLong:record->context_length], @"context_length",
    [NSNumber numberWithLongLong:record->created], @"created",
    architectureModality, @"architecture_modality",
    architectureTokenizer, @"architecture_tokenizer",
    architectureInstructType, @"architecture_instruct_type",
    pricingPrompt, @"pricing_prompt",
    pricingCompletion, @"pricing_completion",
    pricingRequest, @"pricing_request",
    pricingImage, @"pricing_image",
    pricingAudio, @"pricing_audio",
    pricingWebSearch, @"pricing_web_search",
    pricingInternalReasoning, @"pricing_internal_reasoning",
    pricingInputCacheRead, @"pricing_input_cache_read",
    pricingInputCacheWrite, @"pricing_input_cache_write",
    [NSNumber numberWithLongLong:record->top_provider_context_length],
    @"top_provider_context_length",
    [NSNumber numberWithLongLong:record->top_provider_max_completion_tokens],
    @"top_provider_max_completion_tokens",
    [NSNumber numberWithBool:(record->top_provider_is_moderated ? YES : NO)],
    @"top_provider_is_moderated",
    knowledgeCutoff, @"knowledge_cutoff",
    expirationDate, @"expiration_date",
    linksDetails, @"links_details",
    linksJSON, @"links_json",
    reasoningJSON, @"reasoning_json",
    benchmarksJSON, @"benchmarks_json",
    defaultParametersJSON, @"default_parameters_json",
    perRequestLimitsJSON, @"per_request_limits_json",
    rawJSON, @"raw_json",
    fetchedAt, @"fetched_at",
    [NSNumber numberWithBool:(record->selected ? YES : NO)], @"selected",
    [NSNumber numberWithBool:(record->allowed ? YES : NO)], @"allowed",
    nil];
}

+ (NSError *)errorFromCString:(char *)message
{
  NSString *description;
  NSDictionary *userInfo;

  if (message != NULL) {
    description = [NSString stringWithUTF8String:message];
  } else {
    description = nil;
  }

  if (description == nil) {
    description = NSLocalizedString(@"Strappy request failed.", nil);
  }

  userInfo = [NSDictionary dictionaryWithObject:description
                                         forKey:NSLocalizedDescriptionKey];
  return [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                             code:1
                         userInfo:userInfo];
}

+ (NSString *)systemPromptForAssistantSetIdentifier:(NSString *)identifier
                                  webSearchEnabled:(BOOL)webSearchEnabled
                                             error:(NSError **)error
{
  NSString *resourcePath;
  strappy_assistant_set_profile profile;
  char *prompt;
  char *strappyError;
  NSString *result;

  resourcePath = [StrappySession guidanceResourceDirectoryWithError:error];
  if (resourcePath == nil) {
    return nil;
  }
  strappy_assistant_set_profile_init(&profile);
  strappyError = NULL;
  if (!strappy_assistant_sets_load_profile(
        [resourcePath fileSystemRepresentation],
        StrappySessionOptionalCString(identifier),
        &profile,
        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_assistant_set_profile_destroy(&profile);
    return nil;
  }
  prompt = strappy_prompt_build([resourcePath fileSystemRepresentation],
                                &profile,
                                webSearchEnabled ?
                                  STRAPPY_WEB_PROVIDER_AUTO :
                                  STRAPPY_WEB_PROVIDER_NONE,
                                &strappyError);
  strappy_assistant_set_profile_destroy(&profile);
  if (prompt == NULL) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return nil;
  }
  result = [NSString stringWithUTF8String:prompt];
  free(prompt);
  strappy_session_free_string(strappyError);
  if ((result == nil) && (error != nil)) {
    NSDictionary *userInfo;

    userInfo = [NSDictionary dictionaryWithObject:
      NSLocalizedString(@"Generated system prompt is not valid UTF-8.", nil)
                                           forKey:NSLocalizedDescriptionKey];
    *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                 code:7
                             userInfo:userInfo];
  }
  return result;
}

+ (BOOL)ensureSessionsDirectoryForDatabasePath:(NSString *)databasePath
                                         error:(NSError **)error
{
  NSFileManager *fileManager;
  NSString *directoryPath;
  BOOL isDirectory;

  fileManager = [NSFileManager defaultManager];
  directoryPath = [databasePath stringByDeletingLastPathComponent];
  isDirectory = NO;

  if ([fileManager fileExistsAtPath:directoryPath isDirectory:&isDirectory]) {
    if (isDirectory) {
      return YES;
    }

    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Session path is not a directory.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:2
                               userInfo:userInfo];
    }
    return NO;
  }

  if ([fileManager XP_createDirectoryAtPath:directoryPath
                withIntermediateDirectories:YES
                                 attributes:nil
                                      error:error]) {
    return YES;
  }

  if ((error != nil) && (*error == nil)) {
    NSDictionary *userInfo =
      [NSDictionary dictionaryWithObject:NSLocalizedString(@"Could not create session directory.", nil)
                                  forKey:NSLocalizedDescriptionKey];
    *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                 code:3
                             userInfo:userInfo];
  }

  return NO;
}

+ (NSString *)sessionsDatabasePath
{
  NSArray *paths;
  NSString *basePath;
  NSString *strappyDirectoryPath;

  paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,
                                              NSUserDomainMask,
                                              YES);
  if ([paths count] > 0U) {
    basePath = [paths objectAtIndex:0];
  } else {
    basePath = NSHomeDirectory();
  }

  strappyDirectoryPath = [basePath stringByAppendingPathComponent:@"Strappy"];
  return [strappyDirectoryPath stringByAppendingPathComponent:@"strappy.sqlite"];
}

+ (NSArray *)codingWorkingDirectoryPaths
{
  NSString *homeDirectory;
  NSString *developerDirectory;
  NSString *strappyDeveloperDirectory;

  homeDirectory = NSHomeDirectory();
  if (![homeDirectory isKindOfClass:[NSString class]] ||
      ([homeDirectory length] == 0U)) {
    return [NSArray array];
  }
  developerDirectory =
    [homeDirectory stringByAppendingPathComponent:@"Developer"];
  strappyDeveloperDirectory =
    [[[[homeDirectory stringByAppendingPathComponent:@"Library"]
       stringByAppendingPathComponent:@"Application Support"]
      stringByAppendingPathComponent:@"Strappy"]
     stringByAppendingPathComponent:@"Developer"];
  return [NSArray arrayWithObjects:
    developerDirectory,
    homeDirectory,
    strappyDeveloperDirectory,
    nil];
}

+ (BOOL)initializeSessionStoreWithError:(NSError **)error
{
  NSString *databasePath;
  char *strappyError;
  int ok;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return NO;
  }

  strappyError = NULL;
  ok = strappy_session_initialize_store([databasePath UTF8String],
                                        &strappyError);
  if (ok) {
    static const char *providerIDs[] = {
      STRAPPY_PROVIDER_OPENROUTER,
      STRAPPY_PROVIDER_OPENAI_CHATGPT
    };
    static const char *accountIDs[] = {
      STRAPPY_PROVIDER_ACCOUNT_OPENROUTER,
      STRAPPY_PROVIDER_ACCOUNT_OPENAI_CHATGPT
    };
    static const char *accountNames[] = {
      STRAPPY_PROVIDER_ACCOUNT_OPENROUTER_NAME,
      STRAPPY_PROVIDER_ACCOUNT_OPENAI_CHATGPT_NAME
    };
    size_t providerIndex;

    for (providerIndex = 0U;
         ok && (providerIndex < (sizeof(providerIDs) / sizeof(providerIDs[0])));
         providerIndex++) {
      strappy_provider_account_record_list existingAccounts;
      NSString *providerIdentifier;
      NSString *providerAccountIdentifier;
      StrappyKeychain *keychain;
      NSObject *credentialLock;
      NSString *legacyEndpoint;
      NSMutableArray *restoreIdentifiers;
      NSArray *credentialIdentifiers;
      NSUInteger credentialIndex;

      strappy_provider_account_record_list_init(&existingAccounts);
      if (!strappy_db_list_provider_accounts(
            [databasePath fileSystemRepresentation], providerIDs[providerIndex],
            0, &existingAccounts, &strappyError)) {
        ok = 0;
        break;
      }
      providerIdentifier = [NSString stringWithUTF8String:
        providerIDs[providerIndex]];
      providerAccountIdentifier = [NSString stringWithUTF8String:
        (existingAccounts.count > 0U) ?
          existingAccounts.records[0].account_id : accountIDs[providerIndex]];
      keychain = [StrappyKeychain sharedKeychain];
      credentialLock = [keychain
        credentialLockForProviderIdentifier:providerIdentifier
        providerAccountIdentifier:providerAccountIdentifier];
      legacyEndpoint = nil;
      @synchronized(credentialLock) {
        if (strcmp(providerIDs[providerIndex], STRAPPY_PROVIDER_OPENROUTER) == 0) {
          ok = [keychain
            migrateLegacyOpenRouterCredentialToProviderAccountIdentifier:
              providerAccountIdentifier
            endpoint:&legacyEndpoint] ? 1 : 0;
        } else {
          ok = [keychain
            migrateLegacyChatGPTCredentialToProviderAccountIdentifier:
              providerAccountIdentifier] ? 1 : 0;
        }
      }
      if (ok && (existingAccounts.count == 0U)) {
        restoreIdentifiers = [NSMutableArray array];
        credentialIdentifiers = [keychain
          credentialProviderAccountIdentifiersForProviderIdentifier:
            providerIdentifier];
        if ([credentialIdentifiers containsObject:providerAccountIdentifier]) {
          [restoreIdentifiers addObject:providerAccountIdentifier];
        }
        for (credentialIndex = 0U;
             credentialIndex < [credentialIdentifiers count];
             credentialIndex++) {
          NSString *identifier;

          identifier = [credentialIdentifiers objectAtIndex:credentialIndex];
          if (![restoreIdentifiers containsObject:identifier]) {
            [restoreIdentifiers addObject:identifier];
          }
        }
        for (credentialIndex = 0U;
             ok && (credentialIndex < [restoreIdentifiers count]);
             credentialIndex++) {
          NSString *identifier;
          NSString *displayName;
          NSString *savedDisplayName;
          const char *endpoint;

          identifier = [restoreIdentifiers objectAtIndex:credentialIndex];
          savedDisplayName = nil;
          displayName = [keychain loadDisplayName:&savedDisplayName
            forProviderIdentifier:providerIdentifier
            providerAccountIdentifier:identifier] ? savedDisplayName : nil;
          if ([displayName length] == 0U) {
            displayName = (credentialIndex == 0U) ?
              [NSString stringWithUTF8String:accountNames[providerIndex]] :
              [NSString stringWithFormat:@"%s %lu", accountNames[providerIndex],
                (unsigned long)(credentialIndex + 1U)];
          }
          endpoint = ([identifier isEqualToString:providerAccountIdentifier] &&
                      ([legacyEndpoint length] > 0U)) ?
            [legacyEndpoint UTF8String] : NULL;
          ok = strappy_db_restore_provider_account(
            [databasePath fileSystemRepresentation], [identifier UTF8String],
            providerIDs[providerIndex], [displayName UTF8String], endpoint,
            &strappyError);
          if (ok) {
            ok = [keychain saveDisplayName:displayName
              forProviderIdentifier:providerIdentifier
              providerAccountIdentifier:identifier] ? 1 : 0;
          }
        }
      } else if (ok &&
                 ([legacyEndpoint length] > 0U)) {
        ok = strappy_db_update_provider_account(
          [databasePath fileSystemRepresentation],
          existingAccounts.records[0].account_id,
          existingAccounts.records[0].display_name,
          [legacyEndpoint UTF8String], &strappyError);
      }
      if (ok && (existingAccounts.count > 0U)) {
        size_t existingIndex;

        for (existingIndex = 0U;
             ok && (existingIndex < existingAccounts.count);
             existingIndex++) {
          NSString *identifier;
          NSString *displayName;
          NSString *savedDisplayName;
          NSObject *nameCredentialLock;

          identifier = [NSString stringWithUTF8String:
            existingAccounts.records[existingIndex].account_id];
          displayName = [NSString stringWithUTF8String:
            existingAccounts.records[existingIndex].display_name];
          savedDisplayName = nil;
          nameCredentialLock = [keychain
            credentialLockForProviderIdentifier:providerIdentifier
            providerAccountIdentifier:identifier];
          @synchronized(nameCredentialLock) {
            if (![keychain loadDisplayName:&savedDisplayName
                     forProviderIdentifier:providerIdentifier
                 providerAccountIdentifier:identifier] ||
                ![savedDisplayName isEqualToString:displayName]) {
              ok = [keychain saveDisplayName:displayName
                forProviderIdentifier:providerIdentifier
                providerAccountIdentifier:identifier] ? 1 : 0;
            }
          }
        }
      }
      strappy_provider_account_record_list_destroy(&existingAccounts);
      if (!ok && (strappyError == NULL)) {
        strappy_set_error(&strappyError,
                          "Could not convert the saved account credential.");
      }
    }
  }
  if (ok) {
    NSString *resourcePath;

    resourcePath = [[NSBundle mainBundle] resourcePath];
    ok = [resourcePath isKindOfClass:[NSString class]] &&
      ([resourcePath length] > 0U) &&
      strappy_model_catalog_import_bundled_models(
        [resourcePath fileSystemRepresentation],
        [databasePath fileSystemRepresentation],
        &strappyError);
    if (!ok && (strappyError == NULL)) {
      strappy_set_error(&strappyError,
                        "Bundled model catalog resource is missing.");
    }
  }
  if (!ok) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return NO;
  }

  return YES;
}

+ (NSString *)designatedProviderAccountIdentifierForProviderIdentifier:
                (NSString *)providerIdentifier
                                                               error:
                (NSError **)error
{
  NSString *databasePath;
  char *accountID;
  char *strappyError;
  NSString *result;

  if (![providerIdentifier isKindOfClass:[NSString class]] ||
      ([providerIdentifier length] == 0U)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:
        "Provider identifier is missing."];
    }
    return nil;
  }
  databasePath = [StrappySession sessionsDatabasePath];
  accountID = NULL;
  strappyError = NULL;
  if (!strappy_db_get_designated_provider_account(
        [databasePath fileSystemRepresentation],
        [providerIdentifier UTF8String],
        &accountID,
        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return nil;
  }
  result = [NSString stringWithUTF8String:accountID];
  free(accountID);
  strappy_free_string(strappyError);
  return result;
}

+ (NSDictionary *)dictionaryFromProviderAccountRecord:
                    (const strappy_provider_account_record *)record
{
  NSString *accountIdentifier;
  NSString *providerIdentifier;
  NSString *displayName;
  NSString *lifecycleState;
  NSString *responsesEndpoint;

  if ((record == NULL) || (record->account_id == NULL) ||
      (record->provider_id == NULL) || (record->display_name == NULL) ||
      (record->lifecycle_state == NULL)) {
    return nil;
  }
  accountIdentifier = [NSString stringWithUTF8String:record->account_id];
  providerIdentifier = [NSString stringWithUTF8String:record->provider_id];
  displayName = [NSString stringWithUTF8String:record->display_name];
  lifecycleState = [NSString stringWithUTF8String:record->lifecycle_state];
  responsesEndpoint = (record->responses_endpoint != NULL) ?
    [NSString stringWithUTF8String:record->responses_endpoint] : @"";
  if ((accountIdentifier == nil) || (providerIdentifier == nil) ||
      (displayName == nil) || (lifecycleState == nil) ||
      (responsesEndpoint == nil)) {
    return nil;
  }
  return [NSDictionary dictionaryWithObjectsAndKeys:
    accountIdentifier, @"id",
    providerIdentifier, @"provider_id",
    displayName, @"name",
    lifecycleState, @"lifecycle_state",
    responsesEndpoint, @"responses_endpoint",
    [NSNumber numberWithLongLong:record->created_at_ms], @"created_at_ms",
    [NSNumber numberWithLongLong:record->updated_at_ms], @"updated_at_ms",
    [NSNumber numberWithBool:(record->has_last_used_at_ms ? YES : NO)],
      @"has_last_used_at_ms",
    [NSNumber numberWithLongLong:record->last_used_at_ms], @"last_used_at_ms",
    nil];
}

+ (NSArray *)providerCatalog
{
  NSMutableArray *providers;
  size_t index;

  providers = [NSMutableArray arrayWithCapacity:strappy_provider_count()];
  for (index = 0U; index < strappy_provider_count(); index++) {
    const strappy_provider_definition *definition;
    NSString *providerIdentifier;
    NSString *displayName;

    definition = strappy_provider_at(index);
    if ((definition == NULL) || (definition->provider_id == NULL) ||
        (definition->display_name == NULL)) {
      continue;
    }
    providerIdentifier = [NSString stringWithUTF8String:
      definition->provider_id];
    displayName = [NSString stringWithUTF8String:definition->display_name];
    if ((providerIdentifier == nil) || (displayName == nil)) {
      continue;
    }
    [providers addObject:[NSDictionary dictionaryWithObjectsAndKeys:
      providerIdentifier, @"id",
      displayName, @"name",
      [NSNumber XP_numberWithInteger:
        (XPInteger)definition->credential_kind],
        @"credential_kind",
      [NSNumber numberWithBool:
        (definition->requires_endpoint_override ? YES : NO)],
        @"requires_endpoint",
      [NSNumber numberWithBool:
        (strappy_provider_is_available(definition) ? YES : NO)],
        @"available",
      nil]];
  }
  return providers;
}

+ (NSArray *)providerAccountCatalogWithError:(NSError **)error
{
  NSString *databasePath;
  strappy_provider_account_record_list list;
  char *strappyError;
  NSMutableArray *accounts;
  size_t index;

  databasePath = [StrappySession sessionsDatabasePath];
  strappy_provider_account_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_db_list_provider_accounts(
        [databasePath fileSystemRepresentation], NULL, 0, &list,
        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return nil;
  }
  accounts = [NSMutableArray arrayWithCapacity:list.count];
  for (index = 0U; index < list.count; index++) {
    NSDictionary *account;
    NSMutableDictionary *availableAccount;
    NSString *accountIdentifier;
    NSString *providerIdentifier;
    NSString *responsesEndpoint;
    StrappyKeychain *keychain;
    NSObject *credentialLock;
    BOOL available;

    account = [StrappySession dictionaryFromProviderAccountRecord:
      &list.records[index]];
    if (account != nil) {
      accountIdentifier = [account objectForKey:@"id"];
      providerIdentifier = [account objectForKey:@"provider_id"];
      responsesEndpoint = [account objectForKey:@"responses_endpoint"];
      keychain = [StrappyKeychain sharedKeychain];
      available = NO;
      if ([providerIdentifier isEqualToString:@"other"]) {
        available = [responsesEndpoint length] > 0U;
      } else if ([providerIdentifier isEqualToString:@"openrouter"]) {
        credentialLock = [keychain
          credentialLockForProviderIdentifier:providerIdentifier
          providerAccountIdentifier:accountIdentifier];
        @synchronized(credentialLock) {
          available = [keychain
            hasBearerTokenForProviderIdentifier:providerIdentifier
            providerAccountIdentifier:accountIdentifier];
        }
      } else if ([providerIdentifier isEqualToString:@"openai_chatgpt"]) {
        credentialLock = [keychain
          credentialLockForProviderIdentifier:providerIdentifier
          providerAccountIdentifier:accountIdentifier];
        @synchronized(credentialLock) {
          available = [keychain
            hasChatGPTCredentialsForProviderAccountIdentifier:
              accountIdentifier];
        }
      }
      availableAccount = [[account mutableCopy] autorelease];
      [availableAccount setObject:[NSNumber numberWithBool:available]
                           forKey:@"available"];
      [accounts addObject:availableAccount];
    }
  }
  strappy_provider_account_record_list_destroy(&list);
  strappy_free_string(strappyError);
  return accounts;
}

+ (NSString *)defaultProviderAccountNameForProviderIdentifier:
                (NSString *)providerIdentifier
                                                    accounts:
                (NSArray *)accounts
{
  NSString *baseName;
  NSString *candidate;
  NSUInteger suffix;
  NSUInteger index;
  BOOL found;

  if ([providerIdentifier isEqualToString:@"openrouter"]) {
    baseName = @"OpenRouter";
  } else if ([providerIdentifier isEqualToString:@"openai_chatgpt"]) {
    baseName = @"ChatGPT";
  } else {
    baseName = @"Other";
  }
  candidate = baseName;
  suffix = 1U;
  do {
    found = NO;
    for (index = 0U; index < [accounts count]; index++) {
      NSDictionary *account;
      NSString *name;

      account = [accounts objectAtIndex:index];
      name = [account objectForKey:@"name"];
      if ([name isKindOfClass:[NSString class]] &&
          [name isEqualToString:candidate]) {
        found = YES;
        break;
      }
    }
    if (found) {
      suffix++;
      candidate = [NSString stringWithFormat:@"%@ %lu", baseName,
        (unsigned long)suffix];
    }
  } while (found);
  return candidate;
}

+ (NSDictionary *)createProviderAccountForProviderIdentifier:
                    (NSString *)providerIdentifier
                                                        error:
                    (NSError **)error
{
  NSString *databasePath;
  NSArray *accounts;
  NSString *displayName;
  char *accountID;
  char *strappyError;
  strappy_provider_account_record record;
  NSDictionary *account;

  if (![providerIdentifier isKindOfClass:[NSString class]] ||
      ([providerIdentifier length] == 0U)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:
        "Provider identifier is missing."];
    }
    return nil;
  }
  if (strappy_provider_find([providerIdentifier UTF8String]) == NULL) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:
        "Provider account type is not registered."];
    }
    return nil;
  }
  accounts = [StrappySession providerAccountCatalogWithError:error];
  if (accounts == nil) {
    return nil;
  }
  displayName = [StrappySession
    defaultProviderAccountNameForProviderIdentifier:providerIdentifier
                                           accounts:accounts];
  databasePath = [StrappySession sessionsDatabasePath];
  accountID = NULL;
  strappyError = NULL;
  if (!strappy_db_create_provider_account(
        [databasePath fileSystemRepresentation],
        [providerIdentifier UTF8String], [displayName UTF8String], NULL,
        &accountID, &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return nil;
  }
  strappy_provider_account_record_init(&record);
  account = nil;
  if (strappy_db_get_provider_account([databasePath fileSystemRepresentation],
                                      accountID, &record, &strappyError)) {
    account = [StrappySession dictionaryFromProviderAccountRecord:&record];
  }
  if (account != nil) {
    NSString *accountIdentifier;
    StrappyKeychain *keychain;
    NSObject *credentialLock;

    accountIdentifier = [account objectForKey:@"id"];
    keychain = [StrappyKeychain sharedKeychain];
    credentialLock = [keychain
      credentialLockForProviderIdentifier:providerIdentifier
      providerAccountIdentifier:accountIdentifier];
    @synchronized(credentialLock) {
      if (![keychain saveDisplayName:displayName
               forProviderIdentifier:providerIdentifier
           providerAccountIdentifier:accountIdentifier]) {
        (void)strappy_db_archive_provider_account(
          [databasePath fileSystemRepresentation], accountID, NULL);
        account = nil;
        strappy_set_error(&strappyError,
                          "Could not save the account name in Keychain.");
      }
    }
  }
  strappy_provider_account_record_destroy(&record);
  free(accountID);
  if (account == nil) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return nil;
  }
  strappy_free_string(strappyError);
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappyProviderAccountsDidChangeNotification
                  object:self
                userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
                  [account objectForKey:@"id"], @"account_id",
                  @"created", @"action",
                  nil]];
  return account;
}

+ (BOOL)updateProviderAccountIdentifier:(NSString *)providerAccountIdentifier
                            displayName:(NSString *)displayName
                      responsesEndpoint:(NSString *)responsesEndpoint
                                  error:(NSError **)error
{
  NSString *databasePath;
  NSString *trimmedName;
  NSString *trimmedEndpoint;
  strappy_provider_account_record existingRecord;
  NSString *providerIdentifier;
  NSString *previousName;
  StrappyKeychain *keychain;
  NSObject *credentialLock;
  char *strappyError;

  trimmedName = [displayName stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  trimmedEndpoint = [responsesEndpoint stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if (([providerAccountIdentifier length] == 0U) ||
      ([trimmedName length] == 0U)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:
        "Provider account update is incomplete."];
    }
    return NO;
  }
  databasePath = [StrappySession sessionsDatabasePath];
  strappy_provider_account_record_init(&existingRecord);
  strappyError = NULL;
  if (!strappy_db_get_provider_account(
        [databasePath fileSystemRepresentation],
        [providerAccountIdentifier UTF8String], &existingRecord,
        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_provider_account_record_destroy(&existingRecord);
    strappy_free_string(strappyError);
    return NO;
  }
  providerIdentifier = [NSString stringWithUTF8String:
    existingRecord.provider_id];
  previousName = [NSString stringWithUTF8String:existingRecord.display_name];
  keychain = [StrappyKeychain sharedKeychain];
  credentialLock = [keychain
    credentialLockForProviderIdentifier:providerIdentifier
    providerAccountIdentifier:providerAccountIdentifier];
  @synchronized(credentialLock) {
    if (![keychain saveDisplayName:trimmedName
             forProviderIdentifier:providerIdentifier
         providerAccountIdentifier:providerAccountIdentifier]) {
      if (error != nil) {
        *error = [StrappySession errorFromCString:
          "Could not save the account name in Keychain."];
      }
      strappy_provider_account_record_destroy(&existingRecord);
      strappy_free_string(strappyError);
      return NO;
    }
  }
  if (!strappy_db_update_provider_account(
        [databasePath fileSystemRepresentation],
        [providerAccountIdentifier UTF8String], [trimmedName UTF8String],
        StrappySessionOptionalCString(trimmedEndpoint), &strappyError)) {
    @synchronized(credentialLock) {
      (void)[keychain saveDisplayName:previousName
               forProviderIdentifier:providerIdentifier
           providerAccountIdentifier:providerAccountIdentifier];
    }
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_provider_account_record_destroy(&existingRecord);
    strappy_free_string(strappyError);
    return NO;
  }
  strappy_provider_account_record_destroy(&existingRecord);
  strappy_free_string(strappyError);
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappyProviderAccountsDidChangeNotification
                  object:self
                userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
                  providerAccountIdentifier, @"account_id",
                  @"updated", @"action",
                  nil]];
  return YES;
}

+ (BOOL)archiveProviderAccountIdentifier:(NSString *)providerAccountIdentifier
                                    error:(NSError **)error
{
  NSString *databasePath;
  strappy_provider_account_record record;
  NSString *providerIdentifier;
  StrappyKeychain *keychain;
  NSObject *credentialLock;
  char *strappyError;
  BOOL credentialDeleted;

  if ([providerAccountIdentifier length] == 0U) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:
        "Provider account identifier is missing."];
    }
    return NO;
  }
  databasePath = [StrappySession sessionsDatabasePath];
  strappy_provider_account_record_init(&record);
  strappyError = NULL;
  if (!strappy_db_get_provider_account([databasePath fileSystemRepresentation],
                                       [providerAccountIdentifier UTF8String],
                                       &record, &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_provider_account_record_destroy(&record);
    strappy_free_string(strappyError);
    return NO;
  }
  providerIdentifier = [NSString stringWithUTF8String:record.provider_id];
  strappy_provider_account_record_destroy(&record);
  keychain = [StrappyKeychain sharedKeychain];
  credentialLock = [keychain
    credentialLockForProviderIdentifier:providerIdentifier
    providerAccountIdentifier:providerAccountIdentifier];
  @synchronized(credentialLock) {
    if ([providerIdentifier isEqualToString:@"openai_chatgpt"]) {
      credentialDeleted = [keychain
        deleteChatGPTCredentialsForProviderAccountIdentifier:
          providerAccountIdentifier];
    } else {
      credentialDeleted = [keychain
        deleteBearerTokenForProviderIdentifier:providerIdentifier
        providerAccountIdentifier:providerAccountIdentifier];
    }
  }
  if (!credentialDeleted) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:
        "The Keychain refused to remove the account credential."];
    }
    return NO;
  }
  strappyError = NULL;
  if (!strappy_db_archive_provider_account(
        [databasePath fileSystemRepresentation],
        [providerAccountIdentifier UTF8String], &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return NO;
  }
  strappy_free_string(strappyError);
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappyProviderAccountsDidChangeNotification
                  object:self
                userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
                  providerAccountIdentifier, @"account_id",
                  @"archived", @"action",
                  nil]];
  return YES;
}

+ (NSArray *)modelCatalogFromList:(const strappy_model_record_list *)list
{
  NSMutableArray *models;
  size_t index;

  if (list == NULL) {
    return nil;
  }

  models = [NSMutableArray arrayWithCapacity:list->count];
  for (index = 0U; index < list->count; index++) {
    NSDictionary *model =
      [StrappySession dictionaryFromModelRecord:&list->records[index]];
    if (model != nil) {
      [models addObject:model];
    }
  }
  return models;
}

+ (NSArray *)modelCatalogMatchingSearchText:(NSString *)searchText
                                       error:(NSError **)error
{
  NSString *databasePath;
  strappy_model_record_list list;
  NSArray *models;
  char *strappyError;
  const char *searchCString;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappy_model_record_list_init(&list);
  strappyError = NULL;
  searchCString = ((searchText != nil) && ([searchText length] > 0U)) ?
    [searchText UTF8String] : NULL;
  if (!strappy_session_list_models_matching([databasePath UTF8String],
                                            searchCString,
                                            &list,
                                            &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_model_record_list_destroy(&list);
    return nil;
  }

  models = [StrappySession modelCatalogFromList:&list];
  strappy_model_record_list_destroy(&list);
  return models;
}

+ (NSArray *)modelCatalogWithError:(NSError **)error
{
  return [StrappySession modelCatalogMatchingSearchText:nil error:error];
}

+ (NSArray *)allowedModelCatalogWithError:(NSError **)error
{
  NSString *databasePath;
  strappy_model_record_list list;
  NSArray *models;
  char *strappyError;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappy_model_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_session_list_allowed_models([databasePath UTF8String],
                                           &list,
                                           &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_model_record_list_destroy(&list);
    return nil;
  }

  models = [StrappySession modelCatalogFromList:&list];
  strappy_model_record_list_destroy(&list);
  return models;
}

+ (NSArray *)bundledModelCatalogForProviderIdentifier:
               (NSString *)providerIdentifier
                                                 error:
               (NSError **)error
{
  NSString *path;
  NSData *data;
  char *json;
  cJSON *root;
  cJSON *models;
  NSMutableArray *result;
  int count;
  int index;

  if (![providerIdentifier isKindOfClass:[NSString class]] ||
      ([providerIdentifier length] == 0U)) {
    if (error != nil) {
      *error = [self errorFromCString:
        "Bundled model provider identifier is missing."];
    }
    return nil;
  }
  path = [[NSBundle mainBundle] pathForResource:@"BundledModels"
                                         ofType:@"json"];
  data = (path != nil) ? [NSData dataWithContentsOfFile:path] : nil;
  if ((data == nil) || ([data length] == 0U) ||
      ([data length] > (512U * 1024U))) {
    if (error != nil) {
      *error = [self errorFromCString:
        "Bundled model catalog could not be loaded."];
    }
    return nil;
  }
  json = (char *)malloc([data length] + 1U);
  if (json == NULL) {
    if (error != nil) {
      *error = [self errorFromCString:
        "Could not allocate bundled model catalog."];
    }
    return nil;
  }
  memcpy(json, [data bytes], [data length]);
  json[[data length]] = '\0';
  root = cJSON_Parse(json);
  free(json);
  models = (root != NULL) ?
    cJSON_GetObjectItemCaseSensitive(root, "models") : NULL;
  if (!cJSON_IsArray(models)) {
    cJSON_Delete(root);
    if (error != nil) {
      *error = [self errorFromCString:
        "Bundled model catalog is invalid."];
    }
    return nil;
  }
  result = [NSMutableArray array];
  count = cJSON_GetArraySize(models);
  for (index = 0; index < count; index++) {
    cJSON *model;
    cJSON *provider;
    cJSON *wireModelID;
    cJSON *displayName;
    cJSON *active;
    NSString *modelProviderIdentifier;
    NSString *modelWireID;
    NSString *modelDisplayName;

    model = cJSON_GetArrayItem(models, index);
    provider = cJSON_GetObjectItemCaseSensitive(model, "provider_id");
    wireModelID = cJSON_GetObjectItemCaseSensitive(model, "wire_model_id");
    displayName = cJSON_GetObjectItemCaseSensitive(model, "display_name");
    active = cJSON_GetObjectItemCaseSensitive(model, "catalog_active");
    if (!cJSON_IsString(provider) || (provider->valuestring == NULL) ||
        !cJSON_IsString(wireModelID) || (wireModelID->valuestring == NULL) ||
        !cJSON_IsString(displayName) || (displayName->valuestring == NULL) ||
        !cJSON_IsTrue(active)) {
      continue;
    }
    modelProviderIdentifier =
      [NSString stringWithUTF8String:provider->valuestring];
    if (![modelProviderIdentifier isEqualToString:providerIdentifier]) {
      continue;
    }
    modelWireID = [NSString stringWithUTF8String:wireModelID->valuestring];
    modelDisplayName = [NSString stringWithUTF8String:displayName->valuestring];
    if ((modelWireID != nil) && (modelDisplayName != nil)) {
      [result addObject:[NSDictionary dictionaryWithObjectsAndKeys:
        modelProviderIdentifier, @"provider_id",
        modelWireID, @"wire_model_id",
        modelDisplayName, @"name",
        nil]];
    }
  }
  cJSON_Delete(root);
  return result;
}

+ (NSArray *)openRouterModelCatalogMatchingSearchText:(NSString *)searchText
                                                error:(NSError **)error
{
  return [StrappySession modelCatalogMatchingSearchText:searchText error:error];
}

+ (NSArray *)openRouterModelCatalogWithError:(NSError **)error
{
  return [StrappySession modelCatalogWithError:error];
}

+ (NSArray *)allowedOpenRouterModelCatalogWithError:(NSError **)error
{
  return [StrappySession allowedModelCatalogWithError:error];
}

+ (NSArray *)assistantSetCatalog
{
  NSString *resourcePath;
  strappy_assistant_set_record_list list;
  NSMutableArray *sets;
  char *strappyError;
  size_t index;

  resourcePath = [[NSBundle mainBundle] resourcePath];
  if (![resourcePath isKindOfClass:[NSString class]] ||
      ([resourcePath length] == 0U)) {
    [NSException raise:NSInternalInconsistencyException
                format:@"Required assistant resource directory is missing from the app bundle."];
    return nil;
  }

  strappy_assistant_set_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_session_list_assistant_sets(
        [resourcePath fileSystemRepresentation],
        &list,
        &strappyError)) {
    NSString *message;

    message = (strappyError != NULL) ?
      [NSString stringWithUTF8String:strappyError] :
      @"Assistant manifest could not be loaded.";
    strappy_session_free_string(strappyError);
    strappy_assistant_set_record_list_destroy(&list);
    [NSException raise:NSInternalInconsistencyException
                format:@"Required assistant resources are invalid: %@",
                       message];
    return nil;
  }
  sets = [NSMutableArray arrayWithCapacity:list.count];
  for (index = 0U; index < list.count; index++) {
    NSDictionary *set;
    strappy_assistant_set_profile profile;
    int webProvider;

    strappy_assistant_set_profile_init(&profile);
    strappyError = NULL;
    if (!strappy_assistant_sets_load_profile(
          [resourcePath fileSystemRepresentation],
          list.records[index].identifier,
          &profile,
          &strappyError)) {
      NSString *message;

      message = (strappyError != NULL) ?
        [NSString stringWithUTF8String:strappyError] :
        @"Assistant profile could not be loaded.";
      strappy_session_free_string(strappyError);
      strappy_assistant_set_profile_destroy(&profile);
      strappy_assistant_set_record_list_destroy(&list);
      [NSException raise:NSInternalInconsistencyException
                  format:@"Required assistant resources are invalid: %@",
                         message];
      return nil;
    }
    for (webProvider = (int)STRAPPY_WEB_PROVIDER_NONE;
         webProvider <= (int)STRAPPY_WEB_PROVIDER_PARALLEL;
         webProvider++) {
      char *prompt;

      strappyError = NULL;
      prompt = strappy_prompt_build(
        [resourcePath fileSystemRepresentation],
        &profile,
        (strappy_web_provider)webProvider,
        &strappyError);
      if ((prompt == NULL) || (prompt[0] == '\0')) {
        NSString *message;

        message = (strappyError != NULL) ?
          [NSString stringWithUTF8String:strappyError] :
          @"Generated assistant prompt is empty.";
        free(prompt);
        strappy_session_free_string(strappyError);
        strappy_assistant_set_profile_destroy(&profile);
        strappy_assistant_set_record_list_destroy(&list);
        [NSException raise:NSInternalInconsistencyException
                    format:@"Required assistant resources are invalid: %@",
                           message];
        return nil;
      }
      free(prompt);
      strappy_session_free_string(strappyError);
    }
    strappy_assistant_set_profile_destroy(&profile);

    set = [StrappySession dictionaryFromAssistantSetRecord:&list.records[index]];
    if (set != nil) {
      [sets addObject:set];
    }
  }
  strappy_assistant_set_record_list_destroy(&list);
  return sets;
}

+ (NSString *)defaultModelIdentifierWithError:(NSError **)error
{
  NSString *databasePath;
  char *modelId;
  char *strappyError;
  NSString *result;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  modelId = NULL;
  strappyError = NULL;
  if (!strappy_session_get_default_model([databasePath UTF8String],
                                         &modelId,
                                         &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return nil;
  }

  result = nil;
  if (modelId != NULL) {
    result = [NSString stringWithUTF8String:modelId];
  }
  strappy_session_free_string(modelId);
  return result;
}

+ (BOOL)setDefaultModelIdentifier:(NSString *)modelIdentifier
                             error:(NSError **)error
{
  NSString *databasePath;
  char *strappyError;
  int ok;

  if (![modelIdentifier isKindOfClass:[NSString class]] ||
      ([modelIdentifier length] == 0U)) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Model is not selected.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:9
                               userInfo:userInfo];
    }
    return NO;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return NO;
  }

  strappyError = NULL;
  ok = strappy_session_set_default_model([databasePath UTF8String],
                                         [modelIdentifier UTF8String],
                                         &strappyError);
  if (!ok) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return NO;
  }

  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionModelCatalogDidChangeNotification
                  object:self
                userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
                  modelIdentifier, @"default_model_id",
                  modelIdentifier, @"selected_model_id",
                  nil]];
  return YES;
}

+ (NSString *)defaultOpenRouterModelIdentifierWithError:(NSError **)error
{
  return [StrappySession defaultModelIdentifierWithError:error];
}

+ (BOOL)setDefaultOpenRouterModelIdentifier:(NSString *)modelIdentifier
                                      error:(NSError **)error
{
  return [StrappySession setDefaultModelIdentifier:modelIdentifier error:error];
}

+ (StrappySessionOptions *)defaultSessionOptionsWithError:(NSError **)error
{
  NSString *databasePath;
  NSString *workingDirectory;
  NSArray *workingDirectories;
  StrappySessionOptions *options;
  char *strappyError;
  strappy_session_options record;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }
  workingDirectories = [StrappySession codingWorkingDirectoryPaths];
  workingDirectory = ([workingDirectories count] > 0U) ?
    [workingDirectories objectAtIndex:0U] : @"";
  if ([workingDirectory length] == 0U) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Default working directory is unavailable.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return nil;
  }

  strappy_session_options_init(&record);
  strappyError = NULL;
  if (!strappy_session_load_default_options(
        [databasePath UTF8String],
        [workingDirectory fileSystemRepresentation],
        &record,
        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_session_options_destroy(&record);
    return nil;
  }
  options = StrappySessionOptionsFromRecord(&record);
  strappy_session_options_destroy(&record);
  if (options == nil && (error != nil)) {
    *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                 code:6
                             userInfo:[NSDictionary dictionaryWithObject:
      NSLocalizedString(@"Default session options could not be loaded.", nil)
                                                          forKey:NSLocalizedDescriptionKey]];
  }
  return options;
}

+ (BOOL)updateDefaultSessionOptions:(StrappySessionOptions *)options
                       changedFields:(StrappySessionOptionMask)changedFields
                               error:(NSError **)error
{
  NSString *databasePath;
  NSString *resourcePath;
  NSString *workingDirectory;
  NSArray *workingDirectories;
  StrappySessionOptions *savedOptions;
  char *strappyError;
  strappy_session_option_mask actualChangedFields;
  strappy_session_options input;
  strappy_session_options saved;

  if (![options isKindOfClass:[StrappySessionOptions class]] ||
      ((changedFields & ~((StrappySessionOptionMask)StrappySessionOptionAll)) !=
       0U)) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Default session options are invalid.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return NO;
  }

  workingDirectories = [StrappySession codingWorkingDirectoryPaths];
  workingDirectory = ([workingDirectories count] > 0U) ?
    [workingDirectories objectAtIndex:0U] : @"";
  if (([workingDirectory length] == 0U) ||
      (((changedFields & StrappySessionOptionWorkingDirectory) != 0U) &&
       ![workingDirectories containsObject:[options workingDirectory]])) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:15
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Working directory selection is invalid.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return NO;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return NO;
  }
  resourcePath = nil;
  if ((changedFields & StrappySessionOptionAssistantSet) != 0U) {
    resourcePath = [StrappySession guidanceResourceDirectoryWithError:error];
    if (resourcePath == nil) {
      return NO;
    }
  }

  strappy_session_options_init(&input);
  if (!StrappySessionRecordFromOptions(options, &input, error)) {
    return NO;
  }
  strappy_session_options_init(&saved);
  actualChangedFields = 0U;
  strappyError = NULL;
  if (!strappy_session_update_default_options(
        [databasePath UTF8String],
        [workingDirectory fileSystemRepresentation],
        StrappySessionOptionalCString(resourcePath),
        &input,
        (strappy_session_option_mask)changedFields,
        &saved,
        &actualChangedFields,
        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_session_options_destroy(&saved);
    return NO;
  }

  savedOptions = StrappySessionOptionsFromRecord(&saved);
  strappy_session_options_destroy(&saved);
  if (savedOptions == nil) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Saved default session options could not be loaded.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return NO;
  }
  if ((actualChangedFields & StrappySessionOptionModel) != 0U) {
    NSString *modelIdentifier;

    modelIdentifier = [savedOptions modelIdentifier];
    [[NSNotificationCenter defaultCenter]
      postNotificationName:StrappySessionModelCatalogDidChangeNotification
                    object:self
                  userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
                    modelIdentifier, @"default_model_id",
                    modelIdentifier, @"selected_model_id",
                    nil]];
  }
  return YES;
}

+ (BOOL)setModelAllowed:(BOOL)allowed
     forModelIdentifier:(NSString *)modelIdentifier
                  error:(NSError **)error
{
  NSString *databasePath;
  char *strappyError;
  int ok;

  if (![modelIdentifier isKindOfClass:[NSString class]] ||
      ([modelIdentifier length] == 0U)) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Model is not selected.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:9
                               userInfo:userInfo];
    }
    return NO;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return NO;
  }

  strappyError = NULL;
  ok = strappy_session_set_model_allowed([databasePath UTF8String],
                                         [modelIdentifier UTF8String],
                                         allowed ? 1 : 0,
                                         &strappyError);
  if (!ok) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return NO;
  }

  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionModelCatalogDidChangeNotification
                  object:self
                userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
                  modelIdentifier, @"model_id",
                  [NSNumber numberWithBool:(allowed ? YES : NO)], @"allowed",
                  nil]];
  return YES;
}

+ (BOOL)setOpenRouterModelAllowed:(BOOL)allowed
                forModelIdentifier:(NSString *)modelIdentifier
                             error:(NSError **)error
{
  return [StrappySession setModelAllowed:allowed
                      forModelIdentifier:modelIdentifier
                                   error:error];
}

+ (BOOL)manualModelInput:(strappy_manual_model_input *)input
             wireModelID:(NSString *)wireModelID
             displayName:(NSString *)displayName
      contextWindowTokens:(long long)contextWindowTokens
          maxOutputTokens:(long long)maxOutputTokens
        reasoningEnabled:(BOOL)reasoningEnabled
       imageInputEnabled:(BOOL)imageInputEnabled
   localFunctionsEnabled:(BOOL)localFunctionsEnabled
       inputPricePerToken:(NSString *)inputPricePerToken
      outputPricePerToken:(NSString *)outputPricePerToken
   cacheReadPricePerToken:(NSString *)cacheReadPricePerToken
  cacheWritePricePerToken:(NSString *)cacheWritePricePerToken
                    error:(NSError **)error
{
  NSArray *prices;
  NSUInteger priceIndex;

  if ((input == NULL) ||
      ![wireModelID isKindOfClass:[NSString class]] ||
      ((displayName != nil) &&
       ![displayName isKindOfClass:[NSString class]]) ||
      ([wireModelID length] == 0U) || ([wireModelID length] > 128U) ||
      ([displayName length] > 160U) ||
      (contextWindowTokens < 0LL) || (maxOutputTokens < 0LL)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:
        "Manual model metadata is invalid."];
    }
    return NO;
  }
  prices = [NSArray arrayWithObjects:
    (inputPricePerToken != nil) ? (id)inputPricePerToken : (id)[NSNull null],
    (outputPricePerToken != nil) ? (id)outputPricePerToken : (id)[NSNull null],
    (cacheReadPricePerToken != nil) ?
      (id)cacheReadPricePerToken : (id)[NSNull null],
    (cacheWritePricePerToken != nil) ?
      (id)cacheWritePricePerToken : (id)[NSNull null],
    nil];
  for (priceIndex = 0U; priceIndex < [prices count]; priceIndex++) {
    id price;

    price = [prices objectAtIndex:priceIndex];
    if (![price isKindOfClass:[NSNull class]] &&
        (![price isKindOfClass:[NSString class]] ||
         ([(NSString *)price length] > 64U))) {
      if (error != nil) {
        *error = [StrappySession errorFromCString:
          "Manual model pricing is invalid."];
      }
      return NO;
    }
  }
  input->wire_model_id = [wireModelID UTF8String];
  input->display_name = ([displayName length] > 0U) ?
    [displayName UTF8String] : NULL;
  input->context_window_tokens = contextWindowTokens;
  input->max_output_tokens = maxOutputTokens;
  input->reasoning_enabled = reasoningEnabled ? 1 : 0;
  input->image_input_enabled = imageInputEnabled ? 1 : 0;
  input->local_functions_enabled = localFunctionsEnabled ? 1 : 0;
  input->pricing_prompt = ([inputPricePerToken length] > 0U) ?
    [inputPricePerToken UTF8String] : NULL;
  input->pricing_completion = ([outputPricePerToken length] > 0U) ?
    [outputPricePerToken UTF8String] : NULL;
  input->pricing_input_cache_read = ([cacheReadPricePerToken length] > 0U) ?
    [cacheReadPricePerToken UTF8String] : NULL;
  input->pricing_input_cache_write = ([cacheWritePricePerToken length] > 0U) ?
    [cacheWritePricePerToken UTF8String] : NULL;
  return YES;
}

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
                (NSError **)error
{
  NSString *databasePath;
  NSString *modelIdentifier;
  strappy_manual_model_input input;
  char *modelID;
  char *strappyError;

  if (![providerIdentifier isKindOfClass:[NSString class]] ||
      ([providerIdentifier length] == 0U) ||
      ![self manualModelInput:&input
                  wireModelID:wireModelID
                  displayName:displayName
           contextWindowTokens:contextWindowTokens
               maxOutputTokens:maxOutputTokens
             reasoningEnabled:reasoningEnabled
            imageInputEnabled:imageInputEnabled
        localFunctionsEnabled:localFunctionsEnabled
            inputPricePerToken:inputPricePerToken
           outputPricePerToken:outputPricePerToken
        cacheReadPricePerToken:cacheReadPricePerToken
       cacheWritePricePerToken:cacheWritePricePerToken
                         error:error]) {
    return nil;
  }
  databasePath = [self sessionsDatabasePath];
  if (![self ensureSessionsDirectoryForDatabasePath:databasePath error:error]) {
    return nil;
  }
  modelID = NULL;
  strappyError = NULL;
  if (!strappy_db_create_manual_model([databasePath fileSystemRepresentation],
                                      [providerIdentifier UTF8String],
                                      &input, &modelID, &strappyError)) {
    if (error != nil) {
      *error = [self errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return nil;
  }
  modelIdentifier = (modelID != NULL) ?
    [NSString stringWithUTF8String:modelID] : nil;
  strappy_session_free_string(modelID);
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionModelCatalogDidChangeNotification
                  object:self
                userInfo:(modelIdentifier != nil) ?
      [NSDictionary dictionaryWithObjectsAndKeys:
        modelIdentifier, @"model_id", @"created", @"action", nil] : nil];
  return modelIdentifier;
}

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
            (NSError **)error
{
  NSString *databasePath;
  strappy_manual_model_input input;
  char *strappyError;

  if (![providerIdentifier isKindOfClass:[NSString class]] ||
      ([providerIdentifier length] == 0U) ||
      ![self manualModelInput:&input
                  wireModelID:wireModelID
                  displayName:displayName
           contextWindowTokens:contextWindowTokens
               maxOutputTokens:maxOutputTokens
             reasoningEnabled:reasoningEnabled
            imageInputEnabled:imageInputEnabled
        localFunctionsEnabled:localFunctionsEnabled
            inputPricePerToken:inputPricePerToken
           outputPricePerToken:outputPricePerToken
        cacheReadPricePerToken:cacheReadPricePerToken
       cacheWritePricePerToken:cacheWritePricePerToken
                         error:error]) {
    return NO;
  }
  databasePath = [self sessionsDatabasePath];
  if (![self ensureSessionsDirectoryForDatabasePath:databasePath error:error]) {
    return NO;
  }
  strappyError = NULL;
  if (!strappy_db_update_manual_model([databasePath fileSystemRepresentation],
                                      [providerIdentifier UTF8String],
                                      &input, &strappyError)) {
    if (error != nil) {
      *error = [self errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return NO;
  }
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionModelCatalogDidChangeNotification
                  object:self
                userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
                  wireModelID, @"wire_model_id", @"updated", @"action", nil]];
  return YES;
}

+ (BOOL)archiveManualModelForProviderIdentifier:
            (NSString *)providerIdentifier
                                             wireModelID:
            (NSString *)wireModelID
                                                    error:
            (NSError **)error
{
  NSString *databasePath;
  char *strappyError;

  if (![providerIdentifier isKindOfClass:[NSString class]] ||
      ([providerIdentifier length] == 0U) ||
      ![wireModelID isKindOfClass:[NSString class]] ||
      ([wireModelID length] == 0U)) {
    if (error != nil) {
      *error = [self errorFromCString:"Manual model selection is invalid."];
    }
    return NO;
  }
  databasePath = [self sessionsDatabasePath];
  if (![self ensureSessionsDirectoryForDatabasePath:databasePath error:error]) {
    return NO;
  }
  strappyError = NULL;
  if (!strappy_db_archive_manual_model([databasePath fileSystemRepresentation],
                                       [providerIdentifier UTF8String],
                                       [wireModelID UTF8String],
                                       &strappyError)) {
    if (error != nil) {
      *error = [self errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return NO;
  }
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionModelCatalogDidChangeNotification
                  object:self
                userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
                  wireModelID, @"wire_model_id", @"archived", @"action", nil]];
  return YES;
}

+ (BOOL)beginOpenRouterModelCatalogRefreshWithError:(NSError **)error
{
  NSArray *accounts;
  NSString *designatedAccountIdentifier;
  NSString *providerAccountIdentifier;
  NSString *databasePath;
  NSUInteger index;
  BOOL hasOpenRouterAccount;

  @synchronized(self) {
    if (StrappySessionModelCatalogRefreshInFlight) {
      if (error != nil) {
        NSDictionary *userInfo =
          [NSDictionary dictionaryWithObject:NSLocalizedString(@"Model refresh is already running.", nil)
                                      forKey:NSLocalizedDescriptionKey];
        *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                     code:10
                                 userInfo:userInfo];
      }
      return NO;
    }
  }

  accounts = [StrappySession providerAccountCatalogWithError:error];
  if (accounts == nil) {
    return NO;
  }
  designatedAccountIdentifier = [StrappySession
    designatedProviderAccountIdentifierForProviderIdentifier:@"openrouter"
    error:nil];
  providerAccountIdentifier = nil;
  hasOpenRouterAccount = NO;
  for (index = 0U; index < [accounts count]; index++) {
    NSDictionary *account;
    NSString *accountIdentifier;

    account = [accounts objectAtIndex:index];
    if (![[account objectForKey:@"provider_id"]
          isEqualToString:@"openrouter"]) {
      continue;
    }
    hasOpenRouterAccount = YES;
    if (![[account objectForKey:@"available"] boolValue]) {
      continue;
    }
    accountIdentifier = [account objectForKey:@"id"];
    if (providerAccountIdentifier == nil) {
      providerAccountIdentifier = accountIdentifier;
    }
    if ([accountIdentifier isEqualToString:designatedAccountIdentifier]) {
      providerAccountIdentifier = accountIdentifier;
      break;
    }
  }
  if ([providerAccountIdentifier length] == 0U) {
    NSString *message;

    message = hasOpenRouterAccount ? NSLocalizedString(
      @"Enter an API key for an OpenRouter account before fetching models.",
      nil) : NSLocalizedString(
      @"Add an OpenRouter account before fetching models.", nil);
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:11
                               userInfo:[NSDictionary dictionaryWithObject:message
                                         forKey:NSLocalizedDescriptionKey]];
    }
    return NO;
  }

  @synchronized(self) {
    if (StrappySessionModelCatalogRefreshInFlight) {
      if (error != nil) {
        *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                     code:10
                                 userInfo:[NSDictionary dictionaryWithObject:
                                   NSLocalizedString(@"Model refresh is already running.", nil)
                                   forKey:NSLocalizedDescriptionKey]];
      }
      return NO;
    }
    StrappySessionModelCatalogRefreshInFlight = YES;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    @synchronized(self) {
      StrappySessionModelCatalogRefreshInFlight = NO;
    }
    return NO;
  }

  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionModelCatalogRefreshDidStartNotification
                  object:self];
  [NSThread detachNewThreadSelector:@selector(refreshOpenRouterModelCatalogInBackground:)
                           toTarget:self
                         withObject:providerAccountIdentifier];
  return YES;
}

+ (void)refreshOpenRouterModelCatalogInBackground:(id)ignored
{
  NSAutoreleasePool *pool;
  NSString *databasePath;
  NSString *apiEndpoint;
  NSString *apiToken;
  NSString *providerAccountIdentifier;
  NSMutableDictionary *result;
  char *strappyError;
  int ok;

  pool = [[NSAutoreleasePool alloc] init];
  databasePath = [StrappySession sessionsDatabasePath];
  providerAccountIdentifier = [ignored isKindOfClass:[NSString class]] ?
    ignored : nil;
  apiEndpoint = nil;
  apiToken = nil;
  if ([providerAccountIdentifier length] > 0U) {
    strappy_provider_account_record account;

    strappy_provider_account_record_init(&account);
    if (strappy_db_get_provider_account([databasePath fileSystemRepresentation],
                                        [providerAccountIdentifier UTF8String],
                                        &account,
                                        NULL)) {
      apiEndpoint = (account.responses_endpoint != NULL) ?
        [NSString stringWithUTF8String:account.responses_endpoint] : nil;
    }
    strappy_provider_account_record_destroy(&account);
    @synchronized([[StrappyKeychain sharedKeychain]
      credentialLockForProviderIdentifier:@"openrouter"
      providerAccountIdentifier:providerAccountIdentifier]) {
      [[StrappyKeychain sharedKeychain]
        loadBearerToken:&apiToken
        forProviderIdentifier:@"openrouter"
        providerAccountIdentifier:providerAccountIdentifier];
    }
  }
  if ([apiEndpoint length] == 0U) {
    apiEndpoint = [[StrappyKeychain sharedKeychain] apiEndpoint];
  }
  if ([apiToken length] == 0U) {
    apiToken = [[StrappyKeychain sharedKeychain] apiToken];
  }
  result = [[NSMutableDictionary alloc] init];

  strappyError = NULL;
  ok = strappy_model_catalog_update_for_account(
    StrappySessionOptionalCString(providerAccountIdentifier),
    NULL,
    StrappySessionOptionalCString(apiEndpoint),
    StrappySessionOptionalCString(apiToken),
    NULL,
    [databasePath UTF8String],
    &strappyError);
  if (!ok) {
    NSError *error;
    NSString *message;

    error = [StrappySession errorFromCString:strappyError];
    message = [error localizedDescription];
    if ([message length] == 0U) {
      message = NSLocalizedString(@"Model refresh failed.", nil);
    }
    [result setObject:message forKey:@"error"];
  } else {
    NSArray *models;

    models = [StrappySession modelCatalogWithError:nil];
    if (models != nil) {
      [result setObject:[NSNumber XP_numberWithUnsignedInteger:[models count]]
                 forKey:@"model_count"];
    }
  }
  strappy_session_free_string(strappyError);

  [self performSelectorOnMainThread:@selector(openRouterModelCatalogRefreshDidFinish:)
                         withObject:result
                      waitUntilDone:NO];
  [result release];
  [pool release];
}

+ (void)openRouterModelCatalogRefreshDidFinish:(NSDictionary *)result
{
  NSMutableDictionary *userInfo;

  userInfo = [[NSMutableDictionary alloc] init];
  if ([result isKindOfClass:[NSDictionary class]]) {
    [userInfo addEntriesFromDictionary:result];
  }

  @synchronized(self) {
    StrappySessionModelCatalogRefreshInFlight = NO;
  }

  if ([userInfo objectForKey:@"error"] == nil) {
    [[NSNotificationCenter defaultCenter]
      postNotificationName:StrappySessionModelCatalogDidChangeNotification
                    object:self
                  userInfo:userInfo];
  }

  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionModelCatalogRefreshDidFinishNotification
                  object:self
                userInfo:userInfo];
  [userInfo release];
}

+ (StrappySession *)createSessionWithError:(NSError **)error
{
  NSString *databasePath;
  NSString *workingDirectory;
  NSArray *workingDirectories;
  char *strappyError;
  long long sessionId;
  NSNumber *sessionIdentifier;
  NSDictionary *summary;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  workingDirectories = [StrappySession codingWorkingDirectoryPaths];
  workingDirectory = ([workingDirectories count] > 0U) ?
    [workingDirectories objectAtIndex:0U] : @"";
  sessionId = 0;
  strappyError = NULL;
  if (!strappy_session_create_with_working_directory(
        [databasePath UTF8String],
        [workingDirectory fileSystemRepresentation],
        &sessionId,
        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return nil;
  }

  sessionIdentifier = [NSNumber numberWithLongLong:sessionId];
  summary = [StrappySession sessionSummaryForSessionIdentifier:sessionIdentifier
                                                        error:error];
  if (summary == nil) {
    return nil;
  }
  return [[[StrappySession alloc] initWithSessionIdentifier:sessionIdentifier
                                                    summary:summary] autorelease];
}

+ (NSDictionary *)dictionaryFromDatabaseStudyStatusRecord:
    (const strappy_study_database_status_record *)record
{
  if (record == NULL) {
    return nil;
  }
  return [NSDictionary dictionaryWithObjectsAndKeys:
    [StrappySession stringFromCStringOrEmpty:record->database_id],
    @"database_id",
    [StrappySession stringFromCStringOrEmpty:record->path], @"path",
    [StrappySession stringFromCStringOrEmpty:record->app_group_key],
    @"app_group_key",
    [StrappySession stringFromCStringOrEmpty:record->app_name], @"app_name",
    [StrappySession stringFromCStringOrEmpty:record->app_bundle_id],
    @"app_bundle_id",
    [StrappySession stringFromCStringOrEmpty:record->description],
    @"description",
    [StrappySession stringFromCStringOrEmpty:record->context], @"context",
    [NSNumber numberWithBool:(record->studied ? YES : NO)], @"studied",
    [NSNumber numberWithLongLong:record->studied_at_ms], @"studied_at_ms",
    nil];
}

+ (NSArray *)databaseStudyRowsWithError:(NSError **)error
{
  NSString *databasePath;
  NSMutableArray *rows;
  strappy_study_database_status_record_list list;
  char *strappyError;
  size_t index;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }
  strappy_study_database_status_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_study_list_database_status_records([databasePath UTF8String],
                                                   &list,
                                                   &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_study_database_status_record_list_destroy(&list);
    return nil;
  }

  rows = [NSMutableArray arrayWithCapacity:(NSUInteger)list.count];
  for (index = 0U; index < list.count; index++) {
    NSDictionary *row;

    row = [StrappySession dictionaryFromDatabaseStudyStatusRecord:
      &list.records[index]];
    if (row != nil) {
      [rows addObject:row];
    }
  }
  strappy_study_database_status_record_list_destroy(&list);
  strappy_session_free_string(strappyError);
  return rows;
}

+ (BOOL)deleteDatabaseStudyValuesForDatabaseIdentifier:
    (NSString *)databaseIdentifier
                                                    error:(NSError **)error
{
  NSString *databasePath;
  char *strappyError;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return NO;
  }
  strappyError = NULL;
  if (!strappy_study_delete_database_values(
        [databasePath UTF8String],
        [databaseIdentifier UTF8String],
        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return NO;
  }
  strappy_session_free_string(strappyError);
  return YES;
}

+ (BOOL)resetDatabaseStudyWithError:(NSError **)error
{
  NSString *databasePath;
  char *strappyError;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return NO;
  }
  strappyError = NULL;
  if (!strappy_study_reset([databasePath UTF8String], &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return NO;
  }
  strappy_session_free_string(strappyError);
  return YES;
}

+ (StrappySession *)beginDatabaseStudyWithError:(NSError **)error
{
  NSString *databasePath;
  NSString *defaultModel;
  NSString *studyWorkingDirectory;
  NSArray *workingDirectories;
  StrappySession *session;
  StrappySessionOptions *studyOptions;
  strappy_study_database_id_list pending;
  char *strappyError;
  char *cleanupError;
  long long sessionId;
  BOOL studyAlreadyRunning;

  studyAlreadyRunning = NO;
  @synchronized(self) {
    NSEnumerator *enumerator;
    StrappySession *candidate;

    enumerator = [[[self inFlightSessions] allValues] objectEnumerator];
    while ((candidate = [enumerator nextObject]) != nil) {
      if ([candidate isDatabaseStudySession]) {
        studyAlreadyRunning = YES;
        break;
      }
    }
  }
  if (studyAlreadyRunning) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:13
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"A Database Study session is already running.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }
  strappy_study_database_id_list_init(&pending);
  strappyError = NULL;
  if (!strappy_study_list_unstudied_database_ids([databasePath UTF8String],
                                                  &pending,
                                                  &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return nil;
  }
  if (pending.count == 0U) {
    strappy_study_database_id_list_destroy(&pending);
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:14
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"All currently approved databases are studied.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return nil;
  }
  strappy_study_database_id_list_destroy(&pending);

  defaultModel = [StrappySession defaultModelIdentifierWithError:error];
  if (defaultModel == nil) {
    return nil;
  }
  workingDirectories = [StrappySession codingWorkingDirectoryPaths];
  studyWorkingDirectory = ([workingDirectories count] > 0U) ?
    [workingDirectories objectAtIndex:0U] : @"";
  if ([studyWorkingDirectory length] == 0U) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:15
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Database Study working directory is unavailable.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return nil;
  }
  session = [StrappySession createSessionWithError:error];
  if (session == nil) {
    return nil;
  }
  sessionId = [[session sessionIdentifier] longLongValue];
  strappyError = NULL;
  /* Database Study has no session_rename tool; persist its fixed name here. */
  if (!strappy_db_update_session_name(
        [databasePath UTF8String],
        sessionId,
        STRAPPY_ASSISTANT_SET_DATABASE_STUDY_SESSION_NAME,
        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    cleanupError = NULL;
    strappy_session_delete([databasePath UTF8String], sessionId, &cleanupError);
    strappy_session_free_string(cleanupError);
    return nil;
  }
  strappy_session_free_string(strappyError);
  studyOptions = [[session optionsWithError:error] copy];
  [studyOptions setAssistantSetIdentifier:
    [NSString stringWithUTF8String:STRAPPY_ASSISTANT_SET_DATABASE_STUDY]];
  [studyOptions setModelIdentifier:defaultModel];
  [studyOptions setWebProvider:StrappyWebProviderAuto];
  [studyOptions setWebSearchEnabled:YES];
  [studyOptions setBashEnabled:NO];
  [studyOptions setLimitToOneTool:NO];
  [studyOptions setRoundLimit:StrappySessionDefaultRoundLimit];
  [studyOptions setWorkingDirectory:studyWorkingDirectory];
  if ((studyOptions == nil) ||
      ![session updateOptions:studyOptions
                changedFields:StrappySessionOptionAll
                        error:error]) {
    [studyOptions release];
    cleanupError = NULL;
    strappy_session_delete([databasePath UTF8String], sessionId, &cleanupError);
    strappy_session_free_string(cleanupError);
    return nil;
  }
  [studyOptions release];

  @synchronized(session) {
    [session->processingStatusJSON_ release];
    session->processingStatusJSON_ = nil;
    session->promptInFlight_ = YES;
    session->promptCancellationRequested_ = NO;
  }
  [StrappySession registerInFlightSession:session];
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionPromptDidStartNotification
                  object:session];
  [NSThread detachNewThreadSelector:@selector(runDatabaseStudyInBackground:)
                           toTarget:session
                         withObject:nil];
  return session;
}

+ (BOOL)deleteSessionWithIdentifier:(NSNumber *)sessionIdentifier
                               error:(NSError **)error
{
  NSString *databasePath;
  char *strappyError;
  long long sessionId;

  sessionId = [sessionIdentifier isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier longLongValue] : 0LL;
  if (sessionId <= 0) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Session is not selected.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:userInfo];
    }
    return NO;
  }

  if ([StrappySession isPromptInFlightForSessionIdentifier:sessionIdentifier]) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Prompt request is already running.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:8
                               userInfo:userInfo];
    }
    return NO;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return NO;
  }

  strappyError = NULL;
  if (!strappy_session_delete([databasePath UTF8String],
                              sessionId,
                              &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return NO;
  }

  return YES;
}

+ (NSArray *)sessionSummariesWithError:(NSError **)error
{
  NSString *databasePath;
  strappy_session_record_list list;
  NSMutableArray *sessions;
  char *strappyError;
  size_t index;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappy_session_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_session_list_records([databasePath UTF8String], &list, &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_session_record_list_destroy(&list);
    return nil;
  }

  sessions = [NSMutableArray arrayWithCapacity:list.count];
  for (index = 0U; index < list.count; index++) {
    NSDictionary *session =
      [StrappySession dictionaryFromSessionRecord:&list.records[index]];
    if (session != nil) {
      [sessions addObject:session];
    }
  }

  strappy_session_record_list_destroy(&list);
  return sessions;
}

+ (NSDictionary *)sessionListSummaryForSessionIdentifier:
    (NSNumber *)sessionIdentifier error:(NSError **)error
{
  NSString *databasePath;
  strappy_session_record record;
  char *strappyError;
  long long sessionId;
  NSDictionary *session;

  sessionId = [sessionIdentifier isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier longLongValue] : 0LL;
  if (sessionId <= 0) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:
          NSLocalizedString(@"Session is not selected.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:userInfo];
    }
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappy_session_record_init(&record);
  strappyError = NULL;
  if (!strappy_session_load_list_record([databasePath UTF8String],
                                        sessionId,
                                        &record,
                                        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_session_record_destroy(&record);
    return nil;
  }

  session = [StrappySession dictionaryFromSessionRecord:&record];
  strappy_session_record_destroy(&record);
  return session;
}

+ (NSDictionary *)sessionSummaryForSessionIdentifier:(NSNumber *)sessionIdentifier
                                               error:(NSError **)error
{
  NSString *databasePath;
  strappy_session_record record;
  char *strappyError;
  long long sessionId;
  NSDictionary *session;

  if (sessionIdentifier == nil) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Session is not selected.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:userInfo];
    }
    return nil;
  }

  sessionId = [sessionIdentifier isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier longLongValue] : 0LL;
  if (sessionId <= 0) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Session is not selected.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:userInfo];
    }
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappy_session_record_init(&record);
  strappyError = NULL;
  if (!strappy_session_load_record([databasePath UTF8String],
                               sessionId,
                               &record,
                               &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_session_record_destroy(&record);
    return nil;
  }

  session = [StrappySession dictionaryFromSessionRecord:&record];
  strappy_session_record_destroy(&record);
  if (session == nil) {
    return nil;
  }

  return session;
}

+ (NSArray *)messagesForSessionIdentifier:(NSNumber *)sessionIdentifier
                                    error:(NSError **)error
{
  NSString *databasePath;
  strappy_session_message_record_list list;
  NSMutableArray *messages;
  char *strappyError;
  long long sessionId;
  size_t index;

  if (sessionIdentifier == nil) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Session is not selected.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:userInfo];
    }
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappy_session_message_record_list_init(&list);
  strappyError = NULL;
  sessionId = [sessionIdentifier isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier longLongValue] : 0LL;
  if (!strappy_session_list_message_records([databasePath UTF8String],
                                        sessionId,
                                        &list,
                                        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_session_message_record_list_destroy(&list);
    return nil;
  }

  messages = [NSMutableArray arrayWithCapacity:list.count];
  for (index = 0U; index < list.count; index++) {
    NSDictionary *message =
      [StrappySession dictionaryFromSessionMessageRecord:&list.records[index]];
    if (message != nil) {
      [messages addObject:message];
    }
  }

  strappy_session_message_record_list_destroy(&list);
  return messages;
}

- (NSDictionary *)summaryWithError:(NSError **)error
{
  NSDictionary *summary;

  summary = [StrappySession sessionSummaryForSessionIdentifier:sessionIdentifier_
                                                        error:error];
  if (summary != nil) {
    [self updateCachedSummary:summary];
  }
  return summary;
}

- (NSArray *)messagesWithError:(NSError **)error
{
  return [StrappySession messagesForSessionIdentifier:sessionIdentifier_
                                               error:error];
}

- (NSString *)webViewMessagesPageHTMLWithErrorText:(NSString *)errorText
                                           palette:(StrappyWebViewPalette)palette
                                      messageCount:(NSUInteger *)messageCount
                                    timelineCursor:(NSString **)timelineCursor
                                             error:(NSError **)error
{
  NSString *databasePath;
  NSString *processingStatusJSON;
  NSString *resourcePath;
  const char *displayErrorText;
  char *pageHTML;
  char *strappyError;
  char *storedTimelineCursor;
  long long sessionId;
  size_t storedMessageCount;
  strappy_webview_palette webViewPalette;

  if (messageCount != NULL) {
    *messageCount = 0U;
  }
  if (timelineCursor != NULL) {
    *timelineCursor = nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  resourcePath = [[NSBundle mainBundle] resourcePath];
  displayErrorText =
    ([errorText isKindOfClass:[NSString class]] && ([errorText length] > 0U)) ?
      [errorText UTF8String] : NULL;
  processingStatusJSON = [self currentProcessingStatusJSON];
  if (([processingStatusJSON length] == 0U) && [self isPromptInFlight]) {
    processingStatusJSON =
      @"{\"active\":true,\"status_kind\":\"thinking\"}";
  }
  sessionId = [sessionIdentifier_ isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier_ longLongValue] : 0LL;
  storedMessageCount = 0U;
  storedTimelineCursor = NULL;
  strappyError = NULL;
  webViewPalette =
    (palette == StrappyWebViewPaletteNeutral) ?
      STRAPPY_WEBVIEW_PALETTE_NEUTRAL :
      STRAPPY_WEBVIEW_PALETTE_APPLICATION_TINTED;
  pageHTML = strappy_session_webview_messages_page_html_for_session(
    [databasePath fileSystemRepresentation],
    sessionId,
    [resourcePath fileSystemRepresentation],
    displayErrorText,
    StrappySessionOptionalCString(processingStatusJSON),
    webViewPalette,
    &storedMessageCount,
    &storedTimelineCursor,
    &strappyError);
  if (messageCount != NULL) {
    *messageCount = (NSUInteger)storedMessageCount;
  }
  if (pageHTML == NULL) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(storedTimelineCursor);
    strappy_session_free_string(strappyError);
    return nil;
  }

  if (timelineCursor != NULL) {
    *timelineCursor = StrappySessionStringFromCString(storedTimelineCursor);
  } else {
    strappy_session_free_string(storedTimelineCursor);
  }
  strappy_session_free_string(strappyError);
  return StrappySessionStringFromCString(pageHTML);
}

- (NSString *)webViewAppendMessagesJavaScriptAfterTimelineCursor:
                (NSString *)timelineCursor
                                      nextTimelineCursor:
                (NSString **)nextTimelineCursor
                                    appendedMessageCount:
                (NSUInteger *)appendedMessageCount
                                                   error:(NSError **)error
{
  return [self
    webViewAppendMessagesJavaScriptAfterTimelineCursor:timelineCursor
                                    nextTimelineCursor:nextTimelineCursor
                                  appendedMessageCount:appendedMessageCount
                                         renderContext:NULL
                                                 error:error];
}

- (NSString *)webViewAppendMessagesJavaScriptAfterTimelineCursor:
                (NSString *)timelineCursor
                                      nextTimelineCursor:
                (NSString **)nextTimelineCursor
                                    appendedMessageCount:
                (NSUInteger *)appendedMessageCount
                                           renderContext:
                (const strappy_session_webview_render_context *)renderContext
                                                   error:(NSError **)error
{
  NSString *databasePath;
  NSString *resourcePath;
  char *javaScript;
  char *storedNextTimelineCursor;
  char *strappyError;
  long long sessionId;
  size_t storedAppendedMessageCount;

  if (appendedMessageCount != NULL) {
    *appendedMessageCount = 0U;
  }
  if (nextTimelineCursor != NULL) {
    *nextTimelineCursor = nil;
  }

  databasePath = nil;
  resourcePath = nil;
  if (renderContext == NULL) {
    databasePath = [StrappySession sessionsDatabasePath];
    if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                          error:error]) {
      return nil;
    }
    resourcePath = [[NSBundle mainBundle] resourcePath];
  }

  sessionId = [sessionIdentifier_ isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier_ longLongValue] : 0LL;
  storedAppendedMessageCount = 0U;
  storedNextTimelineCursor = NULL;
  strappyError = NULL;
  javaScript = (renderContext != NULL) ?
    strappy_session_webview_append_messages_js_with_render_context(
      renderContext,
      sessionId,
      StrappySessionOptionalCString(timelineCursor),
      &storedAppendedMessageCount,
      &storedNextTimelineCursor,
      &strappyError) :
    strappy_session_webview_append_messages_js_for_session(
      [databasePath fileSystemRepresentation],
      sessionId,
      [resourcePath fileSystemRepresentation],
      StrappySessionOptionalCString(timelineCursor),
      &storedAppendedMessageCount,
      &storedNextTimelineCursor,
      &strappyError);
  if (appendedMessageCount != NULL) {
    *appendedMessageCount = (NSUInteger)storedAppendedMessageCount;
  }
  if (javaScript == NULL) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(storedNextTimelineCursor);
    strappy_session_free_string(strappyError);
    return nil;
  }

  if (nextTimelineCursor != NULL) {
    *nextTimelineCursor =
      StrappySessionStringFromCString(storedNextTimelineCursor);
  } else {
    strappy_session_free_string(storedNextTimelineCursor);
  }
  strappy_session_free_string(strappyError);
  return StrappySessionStringFromCString(javaScript);
}

- (NSString *)webViewReconcileMessagesJavaScriptAfterTimelineCursor:
                (NSString *)timelineCursor
                                      nextTimelineCursor:
                (NSString **)nextTimelineCursor
                                  reconciledMessageCount:
                (NSUInteger *)reconciledMessageCount
                                                   error:(NSError **)error
{
  NSString *databasePath;
  NSString *resourcePath;
  char *javaScript;
  char *storedNextTimelineCursor;
  char *strappyError;
  long long sessionId;
  size_t storedReconciledMessageCount;

  if (reconciledMessageCount != NULL) {
    *reconciledMessageCount = 0U;
  }
  if (nextTimelineCursor != NULL) {
    *nextTimelineCursor = nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }
  resourcePath = [[NSBundle mainBundle] resourcePath];
  sessionId = [sessionIdentifier_ isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier_ longLongValue] : 0LL;
  storedReconciledMessageCount = 0U;
  storedNextTimelineCursor = NULL;
  strappyError = NULL;
  javaScript = strappy_session_webview_reconcile_messages_js_for_session(
    [databasePath fileSystemRepresentation],
    sessionId,
    [resourcePath fileSystemRepresentation],
    StrappySessionOptionalCString(timelineCursor),
    &storedReconciledMessageCount,
    &storedNextTimelineCursor,
    &strappyError);
  if (reconciledMessageCount != NULL) {
    *reconciledMessageCount = (NSUInteger)storedReconciledMessageCount;
  }
  if (javaScript == NULL) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(storedNextTimelineCursor);
    strappy_session_free_string(strappyError);
    return nil;
  }

  if (nextTimelineCursor != NULL) {
    *nextTimelineCursor =
      StrappySessionStringFromCString(storedNextTimelineCursor);
  } else {
    strappy_session_free_string(storedNextTimelineCursor);
  }
  strappy_session_free_string(strappyError);
  return StrappySessionStringFromCString(javaScript);
}

- (NSString *)webViewJavaScriptForStreamEvent:(NSDictionary *)event
                                        error:(NSError **)error
{
  return [self webViewJavaScriptForStreamEvent:event
                                  renderContext:NULL
                                          error:error];
}

- (NSString *)webViewClearProcessingStatusJavaScript
{
  char *javaScript;

  javaScript = strappy_session_webview_set_processing_status_js("");
  return (javaScript != NULL) ?
    StrappySessionStringFromCString(javaScript) : nil;
}

- (NSString *)webViewJavaScriptForStreamEvent:(NSDictionary *)event
                                renderContext:
                (const strappy_session_webview_render_context *)renderContext
                                        error:(NSError **)error
{
  NSString *databasePath;
  NSString *messageKey;
  NSString *resourcePath;
  NSString *statusJSON;
  NSString *streamEvent;
  char *strappyError;
  char *js;
  long long sessionId;

  if (![event isKindOfClass:[NSDictionary class]]) {
    return @"";
  }

  messageKey = [event objectForKey:@"message_key"];
  if (![messageKey isKindOfClass:[NSString class]] ||
      ([messageKey length] == 0U)) {
    return @"";
  }

  streamEvent = [event objectForKey:@"stream_event"];
  if ([streamEvent isEqualToString:@"processing_status"]) {
    statusJSON = [event objectForKey:@"status_json"];
    if (![statusJSON isKindOfClass:[NSString class]]) {
      statusJSON = @"";
    }
    js = strappy_session_webview_set_processing_status_js(
      [statusJSON UTF8String]);
    if (js == NULL) {
      if (error != nil) {
        *error = [StrappySession errorFromCString:NULL];
      }
      return nil;
    }
    return StrappySessionStringFromCString(js);
  }
  if (![streamEvent isEqualToString:@"ledger_changed"] &&
      ![streamEvent isEqualToString:@"ledger_updated"]) {
    return @"";
  }

  sessionId = [sessionIdentifier_ isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier_ longLongValue] : 0LL;
  if (sessionId <= 0) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:NULL];
    }
    return nil;
  }

  databasePath = nil;
  resourcePath = nil;
  if (renderContext == NULL) {
    databasePath = [StrappySession sessionsDatabasePath];
    if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                          error:error]) {
      return nil;
    }
    resourcePath = [[NSBundle mainBundle] resourcePath];
  }

  strappyError = NULL;
  js = (renderContext != NULL) ?
    strappy_session_webview_message_update_js_with_render_context(
      renderContext,
      sessionId,
      [messageKey UTF8String],
      &strappyError) :
    strappy_session_webview_message_update_js_for_key(
      [databasePath fileSystemRepresentation],
      sessionId,
      [resourcePath fileSystemRepresentation],
      [messageKey UTF8String],
      &strappyError);
  if (js == NULL) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return nil;
  }

  strappy_session_free_string(strappyError);
  return StrappySessionStringFromCString(js);
}

- (BOOL)setModelRequestIdentifier:(NSNumber *)modelRequestIdentifier
                includedInContext:(BOOL)includedInContext
                            error:(NSError **)error
{
  NSString *databasePath;
  char *strappyError;
  long long modelRequestId;
  long long sessionId;

  sessionId = [sessionIdentifier_ isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier_ longLongValue] : 0LL;
  modelRequestId =
    [modelRequestIdentifier isKindOfClass:[NSNumber class]] ?
      [modelRequestIdentifier longLongValue] : 0LL;
  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return NO;
  }

  strappyError = NULL;
  if (!strappy_session_update_model_request_include_in_context(
        [databasePath fileSystemRepresentation],
        sessionId,
        modelRequestId,
        includedInContext ? 1 : 0,
        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return NO;
  }
  strappy_session_free_string(strappyError);
  return YES;
}

- (NSString *)webViewJavaScriptForModelRequestIdentifier:
                (NSNumber *)modelRequestIdentifier
                                      includedInContext:(BOOL)includedInContext
                                                animated:(BOOL)animated
{
  long long modelRequestId;

  modelRequestId =
    [modelRequestIdentifier isKindOfClass:[NSNumber class]] ?
      [modelRequestIdentifier longLongValue] : 0LL;
  return StrappySessionStringFromCString(
    strappy_session_webview_set_round_context_inclusion_js(
      modelRequestId,
      includedInContext ? 1 : 0,
      animated ? 1 : 0));
}

- (StrappySessionOptions *)optionsWithError:(NSError **)error
{
  NSString *databasePath;
  StrappySessionOptions *options;
  char *strappyError;
  long long sessionId;
  strappy_session_options record;

  @synchronized(self) {
    if (optionsLoaded_ && (options_ != nil)) {
      return [[options_ copy] autorelease];
    }
  }

  sessionId = [sessionIdentifier_ isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier_ longLongValue] : 0LL;
  if (sessionId <= 0LL) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Session is not selected.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return nil;
  }
  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappy_session_options_init(&record);
  strappyError = NULL;
  if (!strappy_session_load_options([databasePath UTF8String],
                                    sessionId,
                                    &record,
                                    &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_session_options_destroy(&record);
    return nil;
  }
  options = StrappySessionOptionsFromRecord(&record);
  strappy_session_options_destroy(&record);
  if (options == nil) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Session options could not be loaded.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return nil;
  }

  @synchronized(self) {
    [options_ release];
    options_ = [options copy];
    optionsLoaded_ = YES;
    options = [[options_ copy] autorelease];
  }
  return options;
}

- (BOOL)updateOptions:(StrappySessionOptions *)options
        changedFields:(StrappySessionOptionMask)changedFields
                error:(NSError **)error
{
  NSString *databasePath;
  NSString *resourcePath;
  NSDictionary *summary;
  StrappySessionOptions *savedOptions;
  char *strappyError;
  long long sessionId;
  strappy_session_option_mask actualChangedFields;
  strappy_session_options input;
  strappy_session_options saved;

  if (![options isKindOfClass:[StrappySessionOptions class]] ||
      ((changedFields & ~((StrappySessionOptionMask)StrappySessionOptionAll)) !=
       0U)) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Session options are invalid.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return NO;
  }
  if (((changedFields & StrappySessionOptionWorkingDirectory) != 0U) &&
      ![[StrappySession codingWorkingDirectoryPaths]
        containsObject:[options workingDirectory]]) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:15
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Working directory selection is invalid.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return NO;
  }

  sessionId = [sessionIdentifier_ isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier_ longLongValue] : 0LL;
  if (sessionId <= 0LL) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Session is not selected.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return NO;
  }
  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return NO;
  }
  resourcePath = nil;
  if ((changedFields & StrappySessionOptionAssistantSet) != 0U) {
    resourcePath = [StrappySession guidanceResourceDirectoryWithError:error];
    if (resourcePath == nil) {
      return NO;
    }
  }

  strappy_session_options_init(&input);
  if (!StrappySessionRecordFromOptions(options, &input, error)) {
    return NO;
  }
  strappy_session_options_init(&saved);
  actualChangedFields = 0U;
  strappyError = NULL;
  if (!strappy_session_update_options(
        [databasePath UTF8String],
        sessionId,
        StrappySessionOptionalCString(resourcePath),
        &input,
        (strappy_session_option_mask)changedFields,
        &saved,
        &actualChangedFields,
        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_session_options_destroy(&saved);
    return NO;
  }

  savedOptions = StrappySessionOptionsFromRecord(&saved);
  strappy_session_options_destroy(&saved);
  if (savedOptions == nil) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Saved session options could not be loaded.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return NO;
  }
  @synchronized(self) {
    [options_ release];
    options_ = [savedOptions copy];
    optionsLoaded_ = YES;
  }

  summary =
    [StrappySession sessionSummaryForSessionIdentifier:sessionIdentifier_
                                                  error:nil];
  if (summary != nil) {
    [self updateCachedSummary:summary];
  } else {
    summary = [self cachedSummary];
  }
  if (actualChangedFields != 0U) {
    NSMutableDictionary *userInfo;

    userInfo = [NSMutableDictionary dictionaryWithObjectsAndKeys:
      savedOptions, StrappySessionOptionsKey,
      [NSNumber XP_numberWithUnsignedInteger:
        (NSUInteger)actualChangedFields], StrappySessionChangedOptionsKey,
      StrappySessionChangeKindOptions, StrappySessionChangeKindKey,
      nil];
    if ([summary isKindOfClass:[NSDictionary class]]) {
      [userInfo setObject:summary forKey:@"session"];
    }
    [[NSNotificationCenter defaultCenter]
      postNotificationName:StrappySessionDidUpdateNotification
                    object:self
                  userInfo:userInfo];
  }
  return YES;
}
- (NSDictionary *)submitPrompt:(NSString *)prompt
                       context:(NSDictionary *)context
                      isolated:(BOOL)isolated
                         error:(NSError **)error
{
  NSString *databasePath;
  NSString *guidanceResourceDirectory;
  NSString *apiEndpoint;
  NSString *apiToken;
  char *cursorError;
  char *renderContextError;
  char *strappyError;
  char *timelineCursor;
  long long sessionId;
  strappy_session_record record;
  NSDictionary *session;
  StrappySessionResponsesContext responsesContext;

  if ((prompt == nil) || ([prompt length] == 0U)) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Prompt is empty.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:4
                               userInfo:userInfo];
    }
    return nil;
  }

  if ((context != nil) && ![context isKindOfClass:[NSDictionary class]]) {
    context = nil;
  }

  sessionId = [sessionIdentifier_ isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier_ longLongValue] : 0LL;
  if (sessionId <= 0) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Session is not selected.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:6
                               userInfo:userInfo];
    }
    return nil;
  }

  guidanceResourceDirectory =
    [StrappySession guidanceResourceDirectoryWithError:error];
  if (guidanceResourceDirectory == nil) {
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  memset(&responsesContext, 0, sizeof(responsesContext));
  responsesContext.session = self;
  responsesContext.context = [context retain];
  cursorError = NULL;
  timelineCursor = strappy_session_timeline_cursor_for_session(
    [databasePath UTF8String],
    sessionId,
    &cursorError);
  if (timelineCursor != NULL) {
    responsesContext.timelineCursor =
      [StrappySessionStringFromCString(timelineCursor) copy];
  } else {
    if (cursorError != NULL) {
      NSLog(@"StrappyResponses could not prepare its presentation cursor: %s",
            cursorError);
    }
  }
  strappy_session_free_string(cursorError);
  renderContextError = NULL;
  responsesContext.webViewRenderContext =
    strappy_session_webview_render_context_create(
      [databasePath fileSystemRepresentation],
      [guidanceResourceDirectory fileSystemRepresentation],
      &renderContextError);
  if (responsesContext.webViewRenderContext == NULL) {
    NSLog(@"StrappyResponses could not cache its WebView render context: %s",
          (renderContextError != NULL) ? renderContextError : "unknown error");
  }
  strappy_session_free_string(renderContextError);
  apiEndpoint = [[StrappyKeychain sharedKeychain] apiEndpoint];
  apiToken = [[StrappyKeychain sharedKeychain] apiToken];

  strappy_session_record_init(&record);
  strappyError = NULL;
  if (!(isolated ?
        strappy_session_send_isolated_prompt_with_events_and_load(
          [prompt UTF8String],
          StrappySessionOptionalCString(apiEndpoint),
          StrappySessionOptionalCString(apiToken),
          [guidanceResourceDirectory fileSystemRepresentation],
          [databasePath UTF8String],
          sessionId,
          StrappySessionHandleResponsesEvent,
          &responsesContext,
          &record,
          &strappyError) :
        strappy_session_send_prompt_with_events_and_load(
          [prompt UTF8String],
          StrappySessionOptionalCString(apiEndpoint),
          StrappySessionOptionalCString(apiToken),
          [guidanceResourceDirectory fileSystemRepresentation],
          [databasePath UTF8String],
          sessionId,
          StrappySessionHandleResponsesEvent,
          &responsesContext,
          &record,
          &strappyError))) {
    StrappySessionResponsesContextDestroy(&responsesContext);
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_session_record_destroy(&record);
    return nil;
  }
  StrappySessionResponsesContextDestroy(&responsesContext);

  session = [StrappySession dictionaryFromSessionRecord:&record];
  strappy_session_record_destroy(&record);
  return session;
}

- (BOOL)beginResponsesPrompt:(NSString *)prompt
                     context:(NSDictionary *)context
                       error:(NSError **)error
{
  NSMutableDictionary *request;

  if (![prompt isKindOfClass:[NSString class]] || ([prompt length] == 0U)) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Prompt is empty.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:4
                               userInfo:userInfo];
    }
    return NO;
  }

  if ((context != nil) && ![context isKindOfClass:[NSDictionary class]]) {
    context = nil;
  }

  @synchronized(self) {
    if (promptInFlight_) {
      if (error != nil) {
        NSDictionary *userInfo =
          [NSDictionary dictionaryWithObject:NSLocalizedString(@"Prompt request is already running.", nil)
                                      forKey:NSLocalizedDescriptionKey];
        *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                     code:8
                                 userInfo:userInfo];
      }
      return NO;
    }

    [processingStatusJSON_ release];
    processingStatusJSON_ = nil;
    promptInFlight_ = YES;
    promptCancellationRequested_ = NO;
  }

  [StrappySession registerInFlightSession:self];
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionPromptDidStartNotification
                  object:self];

  request = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
    prompt, @"prompt",
    nil];
  if (context != nil) {
    [request setObject:context forKey:@"context"];
  }

  [NSThread detachNewThreadSelector:@selector(sendPromptInBackground:)
                           toTarget:self
                         withObject:request];
  [request release];
  return YES;
}

- (void)sendPromptInBackground:(NSDictionary *)request
{
  NSAutoreleasePool *pool;
  NSError *error;
  NSDictionary *session;
  NSDictionary *context;
  NSMutableDictionary *result;
  NSString *prompt;
  NSString *errorMessage;

  pool = [[NSAutoreleasePool alloc] init];

  prompt = [request objectForKey:@"prompt"];
  if (![prompt isKindOfClass:[NSString class]]) {
    prompt = @"";
  }
  context = [request objectForKey:@"context"];
  if (![context isKindOfClass:[NSDictionary class]]) {
    context = nil;
  }
  result = [[NSMutableDictionary alloc] init];
  if (context != nil) {
    [result addEntriesFromDictionary:context];
  }

  error = nil;
  session = [self submitPrompt:prompt
                       context:context
                      isolated:NO
                         error:&error];
  if (session != nil) {
    [result setObject:session forKey:@"session"];
  } else {
    errorMessage = [error localizedDescription];
    if ([errorMessage length] == 0U) {
      errorMessage = NSLocalizedString(@"Prompt failed.", nil);
    }
    [result setObject:errorMessage forKey:@"error"];
  }

  [self performSelectorOnMainThread:@selector(promptDidFinish:)
                         withObject:result
                      waitUntilDone:NO];
  [result release];

  [pool release];
}

- (void)runDatabaseStudyInBackground:(id)ignored
{
  NSAutoreleasePool *pool;
  NSString *databasePath;
  NSDictionary *eventContext;
  NSDictionary *lastSession;
  NSMutableDictionary *result;
  NSError *requestError;
  NSString *errorMessage;
  unsigned int consecutiveNoProgress;

  (void)ignored;
  pool = [[NSAutoreleasePool alloc] init];
  databasePath = [StrappySession sessionsDatabasePath];
  eventContext = [NSDictionary dictionaryWithObject:[NSNumber numberWithBool:YES]
                                             forKey:@"database_study"];
  lastSession = nil;
  result = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
    [NSNumber numberWithBool:YES], @"database_study", nil];
  errorMessage = nil;
  consecutiveNoProgress = 0U;

  while (![self promptCancellationRequested]) {
    strappy_study_batch batch;
    strappy_study_database_id_list after;
    char *strappyError;
    NSString *prompt;
    size_t index;
    int batchProgressed;

    strappy_study_batch_init(&batch);
    strappyError = NULL;
    if (!strappy_study_next_batch([databasePath UTF8String],
                                  &batch,
                                  &strappyError)) {
      requestError = [StrappySession errorFromCString:strappyError];
      errorMessage = [requestError localizedDescription];
      strappy_session_free_string(strappyError);
      strappy_study_batch_destroy(&batch);
      break;
    }
    if (batch.pending_database_ids.count == 0U) {
      strappy_study_batch_destroy(&batch);
      break;
    }

    prompt = (batch.prompt != NULL) ?
      [NSString stringWithUTF8String:batch.prompt] : nil;
    if (prompt == nil) {
      errorMessage = NSLocalizedString(@"Database Study prompt is not valid text.", nil);
      strappy_study_batch_destroy(&batch);
      break;
    }

    requestError = nil;
    lastSession = [self submitPrompt:prompt
                             context:eventContext
                            isolated:YES
                               error:&requestError];
    if (lastSession == nil) {
      errorMessage = [requestError localizedDescription];
      if ([errorMessage length] == 0U) {
        errorMessage = NSLocalizedString(@"Database Study request failed.", nil);
      }
      strappy_study_batch_destroy(&batch);
      break;
    }
    if ([self promptCancellationRequested]) {
      strappy_study_batch_destroy(&batch);
      break;
    }

    strappy_study_database_id_list_init(&after);
    strappyError = NULL;
    if (!strappy_study_list_unstudied_database_ids([databasePath UTF8String],
                                                    &after,
                                                    &strappyError)) {
      requestError = [StrappySession errorFromCString:strappyError];
      errorMessage = [requestError localizedDescription];
      strappy_session_free_string(strappyError);
      strappy_study_batch_destroy(&batch);
      break;
    }
    batchProgressed = 0;
    for (index = 0U;
         index < batch.pending_database_ids.count;
         index++) {
      size_t afterIndex;
      int stillPending;

      stillPending = 0;
      for (afterIndex = 0U; afterIndex < after.count; afterIndex++) {
        if (strcmp(batch.pending_database_ids.database_ids[index],
                   after.database_ids[afterIndex]) == 0) {
          stillPending = 1;
          break;
        }
      }
      if (!stillPending) {
        batchProgressed = 1;
      }
    }
    strappy_study_database_id_list_destroy(&after);
    strappy_study_batch_destroy(&batch);

    if (batchProgressed) {
      consecutiveNoProgress = 0U;
    } else {
      consecutiveNoProgress++;
      if (consecutiveNoProgress >= 3U) {
        errorMessage = NSLocalizedString(
          @"Database Study could not complete every database after three attempts.",
          nil);
        break;
      }
    }
  }

  if ([self promptCancellationRequested] && ([errorMessage length] == 0U)) {
    errorMessage = NSLocalizedString(@"Database Study was cancelled.", nil);
  }
  if ([errorMessage length] > 0U) {
    [result setObject:errorMessage forKey:@"error"];
  } else if (lastSession != nil) {
    [result setObject:lastSession forKey:@"session"];
  }
  [self performSelectorOnMainThread:@selector(promptDidFinish:)
                         withObject:result
                      waitUntilDone:NO];
  [result release];
  [pool release];
}

- (void)promptDidFinish:(NSDictionary *)result
{
  NSMutableDictionary *userInfo;
  NSDictionary *summary;

  userInfo = [[NSMutableDictionary alloc] init];
  if ([result isKindOfClass:[NSDictionary class]]) {
    [userInfo addEntriesFromDictionary:result];
  }

  summary = [userInfo objectForKey:@"session"];
  if ([summary isKindOfClass:[NSDictionary class]]) {
    [self updateCachedSummary:summary];
  } else {
    summary = nil;
  }

  @synchronized(self) {
    promptInFlight_ = NO;
    promptCancellationRequested_ = NO;
    [processingStatusJSON_ release];
    processingStatusJSON_ = nil;
  }
  [StrappySession unregisterInFlightSession:self];

  if (summary != nil) {
    [userInfo setObject:StrappySessionChangeKindActivity
                 forKey:StrappySessionChangeKindKey];
    [[NSNotificationCenter defaultCenter]
      postNotificationName:StrappySessionDidUpdateNotification
                    object:self
                  userInfo:userInfo];
  }

  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionPromptDidFinishNotification
                  object:self
                userInfo:userInfo];
  [userInfo release];
}

@end
