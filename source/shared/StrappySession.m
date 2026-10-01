#include "strappy_run.h"
#import "strappy_store_client.h"
#import "FileScanner.h"
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

@interface StrappyModelListSource : NSObject <StrappyModelListSource> {
  strappy_model_reader *reader_;
}
- (id)initWithPath:(NSString *)path search:(NSString *)search
  sortDescriptors:(NSArray *)descriptors error:(NSError **)error;
@end

typedef struct StrappyChatGPTAuthorizationCancellationContext {
  id<StrappyChatGPTAuthorizationObserver> observer;
  id context;
} StrappyChatGPTAuthorizationCancellationContext;

static int StrappyChatGPTAuthorizationShouldCancel(void *userData)
{
  StrappyChatGPTAuthorizationCancellationContext *context;

  context = (StrappyChatGPTAuthorizationCancellationContext *)userData;
  if ((context == NULL) || (context->observer == nil)) {
    return 1;
  }
  return [context->observer
    strappyChatGPTAuthorizationShouldCancelWithContext:context->context] ?
    1 : 0;
}

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
+ (BOOL)initializeSessionStoreAndPrepareCredentials:(BOOL)prepareCredentials
                                               error:(NSError **)error;
+ (NSArray *)providerAccountCatalogVerifyingCredentials:
               (BOOL)verifyCredentials
                                                   error:(NSError **)error;
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

static void StrappyStoreChanged(void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  [StrappySession performSelectorOnMainThread:@selector(serviceStoreChanged:)
                                 withObject:nil waitUntilDone:NO];
  [pool release];
}

@implementation StrappySession

+ (void)serviceStoreChanged:(id)ignored
{
  (void)ignored;
  [[NSNotificationCenter defaultCenter] postNotificationName:@"StrappyServiceChanged" object:nil];
  [[NSNotificationCenter defaultCenter] postNotificationName:StrappySessionDidUpdateNotification object:nil];
  [[NSNotificationCenter defaultCenter] postNotificationName:StrappySessionModelCatalogDidChangeNotification object:nil];
  [[NSNotificationCenter defaultCenter] postNotificationName:FileScannerDatabaseCatalogDidChangeNotification object:nil];
}


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
  return (NSUInteger)strappy_run_active_count([[self sessionsDatabasePath] fileSystemRepresentation]);
}

+ (BOOL)hasInFlightSessions
{
  return ([self inFlightSessionCount] > 0U) ? YES : NO;
}

+ (BOOL)isPromptInFlightForSessionIdentifier:(NSNumber *)sessionIdentifier
{
  strappy_run_record run;
  BOOL active = NO;
  if ([sessionIdentifier isKindOfClass:[NSNumber class]] &&
      strappy_run_latest([[self sessionsDatabasePath] fileSystemRepresentation],
        [sessionIdentifier longLongValue], &run)) {
    active = run.state == STRAPPY_RUN_ACCEPTED || run.state == STRAPPY_RUN_RUNNING;
    strappy_run_destroy(&run);
  }
  return active;
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
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(serviceRunChanged:)
        name:@"StrappyServiceChanged" object:nil];
    options_ = [StrappySessionOptionsFromSummary(nil, @"") retain];
    optionsLoaded_ = NO;
    [self performSelectorOnMainThread:@selector(serviceRunChanged:) withObject:nil waitUntilDone:NO];
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
  [[NSNotificationCenter defaultCenter] removeObserver:self];
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
  strappy_run_record run;
  NSString *result = nil;
  if (strappy_run_latest([[StrappySession sessionsDatabasePath] fileSystemRepresentation],
      [sessionIdentifier_ longLongValue], &run)) {
    char *json = strappy_run_status_json(&run);
    if (json) result = [NSString stringWithUTF8String:json];
    free(json); strappy_run_destroy(&run);
  }
  return result;
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
  strappy_run_record run;
  BOOL active = NO;
  @synchronized(self) { if (promptSubmissionPending_) return YES; }
  if (strappy_run_latest([[StrappySession sessionsDatabasePath] fileSystemRepresentation],
      [sessionIdentifier_ longLongValue], &run)) {
    active = run.state == STRAPPY_RUN_ACCEPTED || run.state == STRAPPY_RUN_RUNNING;
    strappy_run_destroy(&run);
  }
  return active;
}

- (void)serviceRunChanged:(id)ignored
{
  BOOL active;
  BOOL previous;
  NSDictionary *summary;
  strappy_run_record run;
  NSString *pendingKey;
  NSDictionary *pending;
  NSString *terminalError = nil;
  BOOL terminalChanged;
  (void)ignored;
  @synchronized(self) { if (promptSubmissionPending_) return; }
  if (!strappy_run_latest([[StrappySession sessionsDatabasePath] fileSystemRepresentation],
      [sessionIdentifier_ longLongValue], &run)) return;
  active = run.state == STRAPPY_RUN_ACCEPTED || run.state == STRAPPY_RUN_RUNNING;
  pendingKey = [NSString stringWithFormat:@"StrappyPendingPrompt-%@", sessionIdentifier_];
  pending = [[NSUserDefaults standardUserDefaults] dictionaryForKey:pendingKey];
  if (run.request_key && [[pending objectForKey:@"key"] isEqualToString:[NSString stringWithUTF8String:run.request_key]]) {
    [[NSUserDefaults standardUserDefaults] removeObjectForKey:pendingKey];
    [[NSUserDefaults standardUserDefaults] synchronize];
  }
  else if (!active && [[pending objectForKey:@"prompt"] isKindOfClass:[NSString class]] &&
      [[pending objectForKey:@"key"] isKindOfClass:[NSString class]] &&
      [[pending objectForKey:@"key"] lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 64) {
    /* Resume only the saved submission identity. A committed retry returns its
     * original run, including interrupted/terminal states; it never replays it. */
    strappy_run_destroy(&run);
    (void)[self beginResponsesPrompt:[pending objectForKey:@"prompt"] context:nil error:nil];
    return;
  }
  terminalChanged = run.id > 0 && (observedServiceRun_ != run.id || observedServiceState_ != run.state);
  if (run.state == STRAPPY_RUN_FAILED) terminalError = @"The prompt failed. Check the provider account and stored request details.";
  else if (run.state == STRAPPY_RUN_INTERRUPTED) terminalError = @"The background service stopped during this prompt. It was not replayed; check any tool effects before retrying.";
  else if (run.state == STRAPPY_RUN_CANCELLED) terminalError = @"The prompt was cancelled.";
  observedServiceRun_ = run.id; observedServiceState_ = run.state;
  strappy_run_destroy(&run);
  @synchronized(self) { previous = promptInFlight_; promptInFlight_ = active; }
  summary = [StrappySession sessionSummaryForSessionIdentifier:sessionIdentifier_ error:nil];
  if (summary != nil) [self updateCachedSummary:summary];
  [[NSNotificationCenter defaultCenter] postNotificationName:StrappySessionStreamEventNotification
      object:self userInfo:[NSDictionary dictionaryWithObject:[NSNumber numberWithBool:YES]
      forKey:@"service_invalidated"]];
  if (active && !previous) [[NSNotificationCenter defaultCenter]
      postNotificationName:StrappySessionPromptDidStartNotification object:self];
  if (!active && (previous || (terminalChanged && terminalError))) {
    NSMutableDictionary *result = [NSMutableDictionary dictionary];
    if (summary) [result setObject:summary forKey:@"session"];
    if (terminalError) [result setObject:terminalError forKey:@"error"];
    [self promptDidFinish:result];
  }
}

- (BOOL)isDatabaseStudySession
{
  return [[[self optionsWithError:nil] assistantSetIdentifier] isEqualToString:
    [NSString stringWithUTF8String:STRAPPY_ASSISTANT_SET_DATABASE_STUDY]];
}

- (BOOL)promptCancellationRequested
{
  strappy_run_record run;
  BOOL requested = NO;
  if (strappy_run_latest([[StrappySession sessionsDatabasePath] fileSystemRepresentation],
      [sessionIdentifier_ longLongValue], &run)) {
    requested = (run.state == STRAPPY_RUN_ACCEPTED || run.state == STRAPPY_RUN_RUNNING) && run.cancel_requested;
    strappy_run_destroy(&run);
  }
  return requested;
}

- (void)cancelPrompt
{
  [NSThread detachNewThreadSelector:@selector(cancelServicePrompt:) toTarget:self withObject:nil];
}
- (void)cancelServicePrompt:(id)ignored
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  strappy_run_record run;
  unsigned char payload[16];
  unsigned int status = STRAPPY_IPC_NOT_READY;
  BOOL delivered = NO;
  unsigned int attempt;
  uint64_t session = (uint64_t)[sessionIdentifier_ longLongValue];
  (void)ignored;
  for (attempt = 0; attempt < 400; ++attempt) {
    BOOL pending;
    @synchronized(self) { pending = promptSubmissionPending_; }
    if (!pending) break;
    [NSThread sleepUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
  }
  if (strappy_run_latest([[StrappySession sessionsDatabasePath] fileSystemRepresentation],
      (long long)session, &run)) {
    strappy_ipc_put32(payload, (uint32_t)(session >> 32)); strappy_ipc_put32(payload+4, (uint32_t)session);
    strappy_ipc_put32(payload+8, (uint32_t)((uint64_t)run.id >> 32)); strappy_ipc_put32(payload+12, (uint32_t)run.id);
    delivered = strappy_store_client_command(STRAPPY_IPC_CANCEL_RUN, payload, 16, &status) && status == STRAPPY_IPC_OK ? YES : NO;
    strappy_run_destroy(&run);
  }
  if (!delivered) [self performSelectorOnMainThread:@selector(promptDidFinish:)
      withObject:[NSDictionary dictionaryWithObject:@"Cancellation could not be confirmed. Reconnect and try again." forKey:@"error"] waitUntilDone:NO];
  [pool release];
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
  NSString *socketDirectory;

  if ([[NSProcessInfo processInfo] XP_platformFamily] == XPPlatformFamilyIOS) {
    socketDirectory = [[NSFileManager defaultManager] fileExistsAtPath:@"/var/jb/Applications/Strappy.app"] ?
      @"/var/jb/var/log/strappy" : @"/var/log/strappy";
  } else {
    socketDirectory = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs/Strappy"];
  }
  if (!strappy_store_client_start([[socketDirectory stringByAppendingPathComponent:@"service.sock"] fileSystemRepresentation],
      [[self sessionsDatabasePath] fileSystemRepresentation], StrappyStoreChanged)) {
    [NSException raise:NSInternalInconsistencyException format:@"Could not start the service monitor."];
  }


  if (![caCertPath isKindOfClass:[NSString class]] || ([caCertPath length] == 0U)) {
    [NSException raise:NSInvalidArgumentException
                format:@"[StrappySession bootstrapProcessWithCACertPath:] caCertPath is required"];
  }

  /* WKWebView may only read files below its read-access URL. Keep the web
   * fonts beside the session HTML on every platform. Legacy WebViews can read
   * the staged files as well, so this needs no platform or OS-version check. */
  fontsPath = StrappyStageWebViewFonts();
  if (fontsPath == nil) {
    [NSException raise:NSInvalidArgumentException
                format:@"Could not stage Strappy WebView fonts."];
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
  NSString *providerAccountIdentifier;
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
  providerAccountIdentifier =
    [StrappySession stringFromCStringOrEmpty:record->provider_account_id];
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
    providerAccountIdentifier, @"provider_account_id",
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
  NSNumber *reasoningEncrypted;
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
  reasoningEncrypted = [NSNumber numberWithBool:
    (record->reasoning_encrypted ? YES : NO)];
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
    reasoningEncrypted, @"reasoning_encrypted",
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

+ (BOOL)validateSessionStoreForDatabasePath:(NSString *)databasePath
                                         error:(NSError **)error
{
  char *message = NULL;
  int ok = strappy_db_check_reader([databasePath fileSystemRepresentation], &message);
  if (!ok && error != nil) *error = [self errorFromCString:message];
  strappy_free_string(message);
  return ok ? YES : NO;
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
  return [self initializeSessionStoreAndPrepareCredentials:NO error:error];
}

+ (BOOL)prepareProviderCredentialsWithError:(NSError **)error
{
  static BOOL credentialsPrepared = NO;
  BOOL ok;
  BOOL didPrepare;

  didPrepare = NO;
  @synchronized(self) {
    if (credentialsPrepared) {
      return YES;
    }
    ok = [self initializeSessionStoreAndPrepareCredentials:YES error:error];
    if (ok) {
      credentialsPrepared = YES;
      didPrepare = YES;
    }
  }
  if (didPrepare) {
    [[NSNotificationCenter defaultCenter]
      postNotificationName:StrappyProviderAccountsDidChangeNotification
                    object:self
                  userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
                    @"prepared", @"action", nil]];
  }
  return ok;
}

+ (BOOL)isChatGPTProviderEnabled
{
  return strappy_provider_chatgpt_is_enabled() ? YES : NO;
}

+ (long long)currentTimestampMilliseconds
{
  return strappy_unix_milliseconds();
}

+ (NSString *)performChatGPTDeviceAuthorizationForProviderAccountIdentifier:
                (NSString *)providerAccountIdentifier
                observer:(id<StrappyChatGPTAuthorizationObserver>)observer
                context:(id)operationContext
                error:(NSError **)error
{
  StrappyChatGPTAuthorizationCancellationContext cancellationContext;
  strappy_openai_oauth_configuration configuration;
  strappy_openai_oauth_device device;
  strappy_openai_oauth_credentials credentials;
  StrappyKeychain *keychain;
  NSObject *credentialLock;
  NSString *verificationURL;
  NSString *userCode;
  NSString *accessToken;
  NSString *refreshToken;
  NSString *accountIdentifier;
  char *strappyError;
  int ok;

  if (([providerAccountIdentifier length] == 0U) || (observer == nil)) {
    if (error != nil) {
      *error = [self errorFromCString:
        "ChatGPT authorization inputs are incomplete."];
    }
    return nil;
  }
  cancellationContext.observer = observer;
  cancellationContext.context = operationContext;
  strappy_openai_oauth_default_configuration(&configuration);
  strappy_openai_oauth_device_init(&device);
  strappy_openai_oauth_credentials_init(&credentials);
  strappyError = NULL;
  ok = strappy_openai_oauth_start_device_authorization(
    &configuration, &device, StrappyChatGPTAuthorizationShouldCancel,
    &cancellationContext, &strappyError);
  if (ok) {
    verificationURL = [NSString stringWithUTF8String:
      configuration.verification_url];
    userCode = [NSString stringWithUTF8String:device.user_code];
    if ((verificationURL == nil) || (userCode == nil)) {
      ok = 0;
      strappy_set_error(&strappyError,
                        "ChatGPT authorization response is invalid.");
    } else {
      [observer
        strappyChatGPTAuthorizationDidReceiveVerificationURL:verificationURL
                                                     userCode:userCode
                                                      context:operationContext];
    }
  }
  if (ok && !StrappyChatGPTAuthorizationShouldCancel(&cancellationContext)) {
    ok = strappy_openai_oauth_complete_device_authorization(
      &configuration, &device, &credentials,
      StrappyChatGPTAuthorizationShouldCancel, &cancellationContext,
      &strappyError);
  } else {
    ok = 0;
  }
  accountIdentifier = nil;
  if (ok) {
    accessToken = [NSString stringWithUTF8String:credentials.access_token];
    refreshToken = [NSString stringWithUTF8String:credentials.refresh_token];
    accountIdentifier = [NSString stringWithUTF8String:credentials.account_id];
    keychain = [StrappyKeychain sharedKeychain];
    credentialLock = [keychain
      credentialLockForProviderIdentifier:@"openai_chatgpt"
      providerAccountIdentifier:providerAccountIdentifier];
    @synchronized(credentialLock) {
      if ((accessToken == nil) || (refreshToken == nil) ||
          (accountIdentifier == nil) ||
          StrappyChatGPTAuthorizationShouldCancel(&cancellationContext) ||
          ![keychain saveChatGPTAccessToken:accessToken
                                refreshToken:refreshToken
                           accountIdentifier:accountIdentifier
                        expiresAtMilliseconds:
                          credentials.expires_at_milliseconds
                   providerAccountIdentifier:providerAccountIdentifier]) {
        accountIdentifier = nil;
      }
    }
    if (accountIdentifier == nil) {
      ok = 0;
      strappy_set_error(&strappyError,
                        "The Keychain refused the ChatGPT credential write.");
    }
  }
  if (!ok && (error != nil)) {
    *error = [self errorFromCString:(strappyError != NULL) ? strappyError :
      "ChatGPT sign-in failed."];
  }
  strappy_free_string(strappyError);
  strappy_openai_oauth_credentials_destroy(&credentials);
  strappy_openai_oauth_device_destroy(&device);
  return accountIdentifier;
}

+ (NSString *)refreshChatGPTCredentialsForProviderAccountIdentifier:
                (NSString *)providerAccountIdentifier
                expectedAccountIdentifier:(NSString *)expectedAccountIdentifier
                observer:(id<StrappyChatGPTAuthorizationObserver>)observer
                context:(id)operationContext
                error:(NSError **)error
{
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return nil;
  }

  StrappyChatGPTAuthorizationCancellationContext cancellationContext;
  strappy_openai_oauth_configuration configuration;
  strappy_openai_oauth_credentials credentials;
  StrappyKeychain *keychain;
  NSObject *credentialLock;
  NSString *currentRefreshToken;
  NSString *currentAccountIdentifier;
  NSString *accessToken;
  NSString *nextRefreshToken;
  NSString *accountIdentifier;
  char *strappyError;
  int ok;

  if (([providerAccountIdentifier length] == 0U) ||
      ([expectedAccountIdentifier length] == 0U) || (observer == nil)) {
    if (error != nil) {
      *error = [self errorFromCString:
        "ChatGPT refresh inputs are incomplete."];
    }
    return nil;
  }
  cancellationContext.observer = observer;
  cancellationContext.context = operationContext;
  strappy_openai_oauth_default_configuration(&configuration);
  strappy_openai_oauth_credentials_init(&credentials);
  keychain = [StrappyKeychain sharedKeychain];
  credentialLock = [keychain
    credentialLockForProviderIdentifier:@"openai_chatgpt"
    providerAccountIdentifier:providerAccountIdentifier];
  currentRefreshToken = nil;
  currentAccountIdentifier = nil;
  accountIdentifier = nil;
  strappyError = NULL;
  ok = 0;
  @synchronized(credentialLock) {
    if (![keychain loadChatGPTAccessToken:NULL
                                refreshToken:&currentRefreshToken
                           accountIdentifier:&currentAccountIdentifier
                        expiresAtMilliseconds:NULL
                   providerAccountIdentifier:providerAccountIdentifier]) {
      strappy_set_error(&strappyError,
                        "Stored ChatGPT credentials are unavailable.");
    } else if (![currentAccountIdentifier
                 isEqualToString:expectedAccountIdentifier]) {
      strappy_set_error(&strappyError,
                        "The refreshed ChatGPT credential changed accounts.");
    } else {
      ok = strappy_openai_oauth_refresh_credentials(
        &configuration, [currentRefreshToken UTF8String], &credentials,
        StrappyChatGPTAuthorizationShouldCancel, &cancellationContext,
        &strappyError);
      if (ok) {
        accessToken = [NSString stringWithUTF8String:credentials.access_token];
        nextRefreshToken = [NSString stringWithUTF8String:
          credentials.refresh_token];
        accountIdentifier = [NSString stringWithUTF8String:
          credentials.account_id];
        if ((accountIdentifier == nil) ||
            ![accountIdentifier isEqualToString:expectedAccountIdentifier]) {
          accountIdentifier = nil;
          ok = 0;
          strappy_set_error(&strappyError,
            "The refreshed ChatGPT credential changed accounts.");
        } else if ((accessToken == nil) || (nextRefreshToken == nil) ||
                   StrappyChatGPTAuthorizationShouldCancel(
                     &cancellationContext) ||
                   ![keychain saveChatGPTAccessToken:accessToken
                                          refreshToken:nextRefreshToken
                                     accountIdentifier:accountIdentifier
                                  expiresAtMilliseconds:
                                    credentials.expires_at_milliseconds
                             providerAccountIdentifier:
                               providerAccountIdentifier]) {
          accountIdentifier = nil;
          ok = 0;
          strappy_set_error(&strappyError,
            "The Keychain refused the refreshed ChatGPT credential write.");
        }
      }
    }
  }
  if (!ok && (error != nil)) {
    *error = [self errorFromCString:(strappyError != NULL) ? strappyError :
      "ChatGPT credential refresh failed."];
  }
  strappy_free_string(strappyError);
  strappy_openai_oauth_credentials_destroy(&credentials);
  return accountIdentifier;
}

+ (BOOL)initializeSessionStoreAndPrepareCredentials:(BOOL)prepareCredentials
                                               error:(NSError **)error
{
  (void)prepareCredentials;
  return [self validateSessionStoreForDatabasePath:[self sessionsDatabasePath] error:error];
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
    [NSNumber numberWithLongLong:record->max_output_tokens],
      @"max_output_tokens",
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
  return [self providerAccountCatalogVerifyingCredentials:NO error:error];
}

+ (NSArray *)verifiedProviderAccountCatalogWithError:(NSError **)error
{
  return [self providerAccountCatalogVerifyingCredentials:YES error:error];
}

+ (BOOL)parseOptionalPositiveIntegerText:(NSString *)text
                                   value:(long long *)value
{
  if (![text isKindOfClass:[NSString class]]) {
    text = @"";
  }
  return strappy_provider_parse_optional_positive_integer(
    [text UTF8String], value) ? YES : NO;
}

+ (NSString *)pricePerTokenForPricePerMillionText:(NSString *)text
                                             valid:(BOOL *)valid
{
  char *price;
  NSString *result;
  int ok;

  if (![text isKindOfClass:[NSString class]]) {
    text = @"";
  }
  price = NULL;
  ok = strappy_provider_price_per_million_to_per_token(
    [text UTF8String], &price);
  if (valid != NULL) {
    *valid = ok ? YES : NO;
  }
  result = (price != NULL) ? [NSString stringWithUTF8String:price] : nil;
  strappy_free_string(price);
  return result;
}

+ (NSArray *)providerAccountCatalogVerifyingCredentials:
               (BOOL)verifyCredentials
                                                   error:(NSError **)error
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
    const strappy_provider_definition *definition;
    NSString *accountIdentifier;
    NSString *providerIdentifier;
    NSString *responsesEndpoint;
    unsigned int missingFields;
    BOOL available;
    BOOL hasCredential;

    account = [StrappySession dictionaryFromProviderAccountRecord:
      &list.records[index]];
    if (account != nil) {
      accountIdentifier = [account objectForKey:@"id"];
      providerIdentifier = [account objectForKey:@"provider_id"];
      responsesEndpoint = [account objectForKey:@"responses_endpoint"];
      definition = strappy_provider_find([providerIdentifier UTF8String]);
      available = (definition != NULL) &&
        strappy_provider_is_available(definition);
      hasCredential = (definition != NULL) &&
        (definition->credential_kind ==
          STRAPPY_PROVIDER_CREDENTIAL_OPTIONAL_BEARER);
      if ((definition != NULL) && !hasCredential) {
        StrappyKeychain *keychain;
        NSObject *credentialLock;

        keychain = [StrappyKeychain sharedKeychain];
        credentialLock = [keychain
          credentialLockForProviderIdentifier:providerIdentifier
          providerAccountIdentifier:accountIdentifier];
        @synchronized(credentialLock) {
          if ([providerIdentifier isEqualToString:@"openrouter"]) {
            hasCredential = [keychain
              hasBearerTokenForProviderIdentifier:providerIdentifier
              providerAccountIdentifier:accountIdentifier];
          } else {
            hasCredential = [keychain
              hasChatGPTCredentialsForProviderAccountIdentifier:
                accountIdentifier];
          }
        }
      }
      missingFields = strappy_provider_account_missing_required_fields(
        definition,
        [[account objectForKey:@"name"] UTF8String],
        [responsesEndpoint UTF8String],
        hasCredential ? 1 : 0);
      if ((missingFields & STRAPPY_PROVIDER_ACCOUNT_MISSING_ENDPOINT) != 0U) {
        available = NO;
      }
      if (verifyCredentials &&
          ((missingFields &
            STRAPPY_PROVIDER_ACCOUNT_MISSING_CREDENTIAL) != 0U)) {
        available = NO;
      }
      availableAccount = [[account mutableCopy] autorelease];
      [availableAccount setObject:[NSNumber numberWithBool:available]
                           forKey:@"available"];
      [availableAccount setObject:[NSNumber numberWithUnsignedInt:missingFields]
                           forKey:@"missing_required_fields"];
      [availableAccount setObject:[NSNumber numberWithBool:
        (missingFields != 0U)] forKey:@"has_missing_required_fields"];
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
    baseName = @"Custom";
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return nil;
  }

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
                        maxOutputTokens:(long long)maxOutputTokens
                                  error:(NSError **)error
{
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

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
      ([trimmedName length] == 0U) || (maxOutputTokens < 0LL)) {
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
        StrappySessionOptionalCString(trimmedEndpoint), maxOutputTokens,
        &strappyError)) {
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

+ (NSString *)bearerTokenForProviderAccountIdentifier:
                (NSString *)providerAccountIdentifier
                                                  error:(NSError **)error
{
  NSString *databasePath;
  strappy_provider_account_record record;
  NSString *providerIdentifier;
  NSString *token;
  StrappyKeychain *keychain;
  NSObject *credentialLock;
  char *strappyError;

  if ([providerAccountIdentifier length] == 0U) {
    if (error != nil) {
      *error = [self errorFromCString:
        "Provider account identifier is missing."];
    }
    return nil;
  }
  databasePath = [self sessionsDatabasePath];
  strappy_provider_account_record_init(&record);
  strappyError = NULL;
  if (!strappy_db_get_provider_account([databasePath fileSystemRepresentation],
                                       [providerAccountIdentifier UTF8String],
                                       &record, &strappyError)) {
    if (error != nil) {
      *error = [self errorFromCString:strappyError];
    }
    strappy_provider_account_record_destroy(&record);
    strappy_free_string(strappyError);
    return nil;
  }
  providerIdentifier = [NSString stringWithUTF8String:record.provider_id];
  strappy_provider_account_record_destroy(&record);
  strappy_free_string(strappyError);
  keychain = [StrappyKeychain sharedKeychain];
  credentialLock = [keychain
    credentialLockForProviderIdentifier:providerIdentifier
    providerAccountIdentifier:providerAccountIdentifier];
  token = nil;
  @synchronized(credentialLock) {
    (void)[keychain loadBearerToken:&token
      forProviderIdentifier:providerIdentifier
      providerAccountIdentifier:providerAccountIdentifier];
  }
  return token;
}

+ (BOOL)updateProviderAccountIdentifier:(NSString *)providerAccountIdentifier
                            displayName:(NSString *)displayName
                      responsesEndpoint:(NSString *)responsesEndpoint
                        maxOutputTokens:(long long)maxOutputTokens
                            bearerToken:(NSString *)bearerToken
                                  error:(NSError **)error
{
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

  NSString *databasePath;
  strappy_provider_account_record record;
  NSString *providerIdentifier;
  NSString *trimmedToken;
  NSString *previousToken;
  StrappyKeychain *keychain;
  NSObject *credentialLock;
  char *strappyError;
  BOOL credentialSaved;
  BOOL hadPreviousToken;

  databasePath = [self sessionsDatabasePath];
  strappy_provider_account_record_init(&record);
  strappyError = NULL;
  if (!strappy_db_get_provider_account([databasePath fileSystemRepresentation],
                                       [providerAccountIdentifier UTF8String],
                                       &record, &strappyError)) {
    if (error != nil) {
      *error = [self errorFromCString:strappyError];
    }
    strappy_provider_account_record_destroy(&record);
    strappy_free_string(strappyError);
    return NO;
  }
  providerIdentifier = [NSString stringWithUTF8String:record.provider_id];
  strappy_provider_account_record_destroy(&record);
  strappy_free_string(strappyError);
  if ([providerIdentifier isEqualToString:@"openai_chatgpt"]) {
    return [self updateProviderAccountIdentifier:providerAccountIdentifier
                                     displayName:displayName
                               responsesEndpoint:responsesEndpoint
                                 maxOutputTokens:maxOutputTokens
                                           error:error];
  }
  trimmedToken = [bearerToken stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if ([providerIdentifier isEqualToString:@"openrouter"] &&
      ([trimmedToken length] == 0U)) {
    if (error != nil) {
      *error = [self errorFromCString:"OpenRouter API key is required."];
    }
    return NO;
  }
  keychain = [StrappyKeychain sharedKeychain];
  credentialLock = [keychain
    credentialLockForProviderIdentifier:providerIdentifier
    providerAccountIdentifier:providerAccountIdentifier];
  previousToken = nil;
  @synchronized(credentialLock) {
    hadPreviousToken = [keychain loadBearerToken:&previousToken
      forProviderIdentifier:providerIdentifier
      providerAccountIdentifier:providerAccountIdentifier];
    credentialSaved = ([trimmedToken length] > 0U) ?
      [keychain saveBearerToken:trimmedToken
          forProviderIdentifier:providerIdentifier
          providerAccountIdentifier:providerAccountIdentifier] :
      [keychain deleteBearerTokenForProviderIdentifier:providerIdentifier
          providerAccountIdentifier:providerAccountIdentifier];
  }
  if (!credentialSaved) {
    if (error != nil) {
      *error = [self errorFromCString:
        "The Keychain refused the provider credential write."];
    }
    return NO;
  }
  if ([self updateProviderAccountIdentifier:providerAccountIdentifier
                                displayName:displayName
                          responsesEndpoint:responsesEndpoint
                            maxOutputTokens:maxOutputTokens
                                      error:error]) {
    return YES;
  }
  @synchronized(credentialLock) {
    if (hadPreviousToken) {
      (void)[keychain saveBearerToken:previousToken
        forProviderIdentifier:providerIdentifier
        providerAccountIdentifier:providerAccountIdentifier];
    } else {
      (void)[keychain deleteBearerTokenForProviderIdentifier:
        providerIdentifier
        providerAccountIdentifier:providerAccountIdentifier];
    }
  }
  return NO;
}

+ (BOOL)archiveProviderAccountIdentifier:(NSString *)providerAccountIdentifier
                                    error:(NSError **)error
{
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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

+ (StrappyModelRows *)modelPreferenceRowsMatchingSearch:(NSString *)search
  sortDescriptors:(NSArray *)descriptors error:(NSError **)error
{
  StrappyModelListSource *source;
  StrappyModelRows *rows;
  if (![self initializeSessionStoreWithError:error]) return nil;
  source = [[StrappyModelListSource alloc] initWithPath:[self sessionsDatabasePath]
    search:search sortDescriptors:descriptors error:error];
  if (source == nil) return nil;
  rows = [[[StrappyModelRows alloc] initWithModelSource:source] autorelease];
  [source release];
  return rows;
}

+ (NSArray *)configuredProviderModelCatalogWithError:(NSError **)error
{
  NSString *databasePath;
  strappy_model_record_list list;
  NSArray *models;
  char *strappyError;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappy_model_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_session_list_models_for_configured_providers(
        [databasePath UTF8String], &list, &strappyError)) {
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

+ (NSArray *)allowedModelCatalogWithError:(NSError **)error
{
  NSString *databasePath;
  strappy_model_record_list list;
  NSArray *models;
  char *strappyError;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return nil;
  }

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
  if (![self validateSessionStoreForDatabasePath:databasePath error:error]) {
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

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
  if (![self validateSessionStoreForDatabasePath:databasePath error:error]) {
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

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
  if (![self validateSessionStoreForDatabasePath:databasePath error:error]) {
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

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

  accounts = [StrappySession verifiedProviderAccountCatalogWithError:error];
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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return nil;
  }

  NSString *databasePath;
  NSString *workingDirectory;
  NSArray *workingDirectories;
  char *strappyError;
  long long sessionId;
  NSNumber *sessionIdentifier;
  NSDictionary *summary;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

  NSString *databasePath;
  char *strappyError;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

  NSString *databasePath;
  char *strappyError;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return nil;
  }

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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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

+ (StrappySessionRows *)sidebarRowsWithError:(NSError **)error
{
  NSString *path;

  path = [self sessionsDatabasePath];
  if (![self validateSessionStoreForDatabasePath:path error:error]) return nil;
  return [[[StrappySessionRows alloc] initWithDatabasePath:path error:error] autorelease];
}

+ (NSArray *)sessionSummariesWithError:(NSError **)error
{
  NSString *databasePath;
  strappy_session_record_list list;
  NSMutableArray *sessions;
  char *strappyError;
  size_t index;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
    if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
    if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
  if (!strappy_db_writer_available(NULL)) {
    if (error != nil) *error = [StrappySession errorFromCString:"This action is unavailable until daemon execution is implemented."];
    return NO;
  }

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
  if (![StrappySession validateSessionStoreForDatabasePath:databasePath
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
- (NSDictionary *)submitPrompt:(NSString *)prompt context:(NSDictionary *)context
                      isolated:(BOOL)isolated error:(NSError **)error
{
  (void)prompt; (void)context; (void)isolated;
  if (error) *error = [StrappySession errorFromCString:"Database Study execution is unavailable until its daemon worker is implemented."];
  return nil;
}

- (BOOL)beginResponsesPrompt:(NSString *)prompt context:(NSDictionary *)context error:(NSError **)error
{
  NSData *data = [prompt isKindOfClass:[NSString class]] ?
      [prompt dataUsingEncoding:NSUTF8StringEncoding] : nil;
  NSMutableDictionary *request;
  NSString *pendingKey = [NSString stringWithFormat:@"StrappyPendingPrompt-%@", sessionIdentifier_];
  NSDictionary *pending = [[NSUserDefaults standardUserDefaults] dictionaryForKey:pendingKey];
  char key[65];
  (void)context;
  if ([data length] == 0 || [data length] > STRAPPY_IPC_MAX_PAYLOAD - 72 || [self isPromptInFlight]) {
    if (error) *error = [StrappySession errorFromCString:"Prompt is empty, exceeds 4024 UTF-8 bytes, or a prompt is active."];
    return NO;
  }
  if (pending && (![[pending objectForKey:@"key"] isKindOfClass:[NSString class]] ||
      [[pending objectForKey:@"key"] lengthOfBytesUsingEncoding:NSUTF8StringEncoding] != 64)) {
    if (error) *error = [StrappySession errorFromCString:"The saved submission retry key is invalid."];
    return NO;
  }
  if (pending && ![[pending objectForKey:@"prompt"] isEqual:prompt]) {
    if (error) *error = [StrappySession errorFromCString:"Retry the previous prompt first to resolve its acceptance."];
    return NO;
  }
  if (!strappy_store_client_request_key(key)) return NO;
  request = [NSMutableDictionary dictionaryWithObjectsAndKeys:prompt, @"prompt",
      pending ? [pending objectForKey:@"key"] : [NSString stringWithUTF8String:key], @"key", pendingKey, @"pendingKey", nil];
  @synchronized([NSUserDefaults standardUserDefaults]) {
    [[NSUserDefaults standardUserDefaults] setObject:request forKey:pendingKey];
    if (![[NSUserDefaults standardUserDefaults] synchronize]) {
      if (error) *error = [StrappySession errorFromCString:"Could not save the submission retry key."];
      return NO;
    }
  }
  @synchronized(self) { promptSubmissionPending_ = YES; promptInFlight_ = YES; }
  [[NSNotificationCenter defaultCenter] postNotificationName:StrappySessionPromptDidStartNotification object:self];
  [NSThread detachNewThreadSelector:@selector(sendPromptInBackground:) toTarget:self withObject:request];
  return YES;
}

- (void)sendPromptInBackground:(NSDictionary *)request
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  unsigned char payload[STRAPPY_IPC_MAX_PAYLOAD];
  NSData *prompt = [[request objectForKey:@"prompt"] dataUsingEncoding:NSUTF8StringEncoding];
  uint64_t session = (uint64_t)[sessionIdentifier_ longLongValue];
  unsigned int status = STRAPPY_IPC_NOT_READY;
  BOOL received;
  strappy_ipc_put32(payload, (uint32_t)(session >> 32)); strappy_ipc_put32(payload+4, (uint32_t)session);
  memcpy(payload+8, [[request objectForKey:@"key"] UTF8String], 64);
  memcpy(payload+72, [prompt bytes], [prompt length]);
  received = strappy_store_client_command(STRAPPY_IPC_SUBMIT_PROMPT, payload,
      (unsigned int)[prompt length] + 72U, &status) ? YES : NO;
  if (received) {
    @synchronized([NSUserDefaults standardUserDefaults]) {
      NSDictionary *saved = [[NSUserDefaults standardUserDefaults] dictionaryForKey:[request objectForKey:@"pendingKey"]];
      if ([[saved objectForKey:@"key"] isEqual:[request objectForKey:@"key"]]) {
        [[NSUserDefaults standardUserDefaults] removeObjectForKey:[request objectForKey:@"pendingKey"]];
        [[NSUserDefaults standardUserDefaults] synchronize];
      }
    }
  }
  @synchronized(self) { promptSubmissionPending_ = NO; }
  if (!received || status != STRAPPY_IPC_OK) {
    NSString *message = !received ? @"Acceptance is unknown. Retry the same prompt to reconnect safely." :
        (status == STRAPPY_IPC_BUSY ? @"The service is busy with another prompt. Try again when it finishes." : @"The service could not accept this prompt.");
    [self performSelectorOnMainThread:@selector(promptDidFinish:)
        withObject:[NSDictionary dictionaryWithObject:message forKey:@"error"] waitUntilDone:NO];
  } else {
    [self performSelectorOnMainThread:@selector(serviceRunChanged:) withObject:nil waitUntilDone:NO];
  }
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

NSString * const StrappySessionListReadFailedNotification =
  @"StrappySessionListReadFailedNotification";

@interface StrappySessionRows ()
- (void)recordReadError:(char *)message;
@end

@implementation StrappySessionRows
- (id)initWithDatabasePath:(NSString *)path error:(NSError **)error
{
  strappy_session_reader *reader;
  char *message;

  self = [super init];
  if (self == nil) return nil;
  reader = NULL;
  message = NULL;
  if (!strappy_db_sidebar_open([path fileSystemRepresentation], &reader, &message)) {
    if (error != nil) *error = [StrappySession errorFromCString:message];
    strappy_free_string(message);
    [self release];
    return nil;
  }
  reader_ = reader;
  pages_ = [[NSMutableDictionary alloc] init];
  pageOrder_ = [[NSMutableArray alloc] init];
  return self;
}

- (void)dealloc
{
  strappy_db_sidebar_close((strappy_session_reader *)reader_);
  [pages_ release];
  [pageOrder_ release];
  [readError_ release];
  [super dealloc];
}

- (id)copyWithZone:(NSZone *)zone
{
  (void)zone;
  return [self retain];
}

- (NSUInteger)count
{
  return (NSUInteger)strappy_db_sidebar_count((strappy_session_reader *)reader_);
}

- (NSError *)readError { return readError_; }

- (void)recordReadError:(char *)message
{
  if (readError_ == nil) {
    readError_ = [[StrappySession errorFromCString:message] retain];
    /* Defer delivery until the table callback finishes. Failed rows carry no
     * session ID, so selecting or deleting them cannot affect another row. */
    [[NSNotificationQueue defaultQueue] enqueueNotification:
      [NSNotification notificationWithName:StrappySessionListReadFailedNotification
        object:self userInfo:[NSDictionary dictionaryWithObject:readError_ forKey:@"error"]]
      postingStyle:NSPostASAP];
  }
  strappy_free_string(message);
}

- (NSUInteger)countSinceTimestamp:(long long)timestamp
{
  size_t count;
  char *message;

  message = NULL;
  if (!strappy_db_sidebar_count_since((strappy_session_reader *)reader_, timestamp,
                                      &count, &message)) {
    [self recordReadError:message];
    return NSNotFound;
  }
  return (NSUInteger)count;
}

- (NSUInteger)indexForSessionIdentifier:(NSNumber *)identifier
{
  size_t index;
  char *message;

  if (![identifier isKindOfClass:[NSNumber class]]) return NSNotFound;
  message = NULL;
  if (!strappy_db_sidebar_index((strappy_session_reader *)reader_,
                                [identifier longLongValue], &index, &message)) {
    [self recordReadError:message];
    return NSNotFound;
  }
  return index == (size_t)-1 ? NSNotFound : (NSUInteger)index;
}

- (id)objectAtIndex:(NSUInteger)index
{
  NSUInteger offset;
  NSNumber *key;
  NSArray *page;

  if (index >= [self count]) {
    [NSException raise:NSRangeException format:@"Session index outside snapshot"];
  }
  offset = (index / STRAPPY_SIDEBAR_PAGE_SIZE) * STRAPPY_SIDEBAR_PAGE_SIZE;
  key = [NSNumber XP_numberWithUnsignedInteger:offset];
  page = [pages_ objectForKey:key];
  if (page == nil && readError_ == nil) {
    strappy_sidebar_page records;
    char *message;
    NSMutableArray *rows;
    size_t row;

    message = NULL;
    if (!strappy_db_sidebar_read((strappy_session_reader *)reader_, (size_t)offset,
                                 &records, &message)) {
      [self recordReadError:message];
    } else {
      rows = [NSMutableArray arrayWithCapacity:records.count];
      for (row = 0; row < records.count; row++) {
        strappy_sidebar_record *record;

        record = &records.records[row];
        [rows addObject:[NSDictionary dictionaryWithObjectsAndKeys:
          [NSNumber numberWithLongLong:record->session_id], @"id",
          [NSNumber numberWithLongLong:record->last_activity_at_ms], @"last_activity_at_ms",
          [StrappySession stringFromCStringOrEmpty:record->name], @"name",
          [StrappySession stringFromCStringOrEmpty:record->model_name], @"model_name",
          @"session", @"row_type", nil]];
      }
      strappy_db_sidebar_page_destroy(&records);
      /* Four 32-row pages, evicting the least recently used page. */
      if ([pageOrder_ count] >= 4U) {
        [pages_ removeObjectForKey:[pageOrder_ objectAtIndex:0]];
        [pageOrder_ removeObjectAtIndex:0];
      }
      page = rows;
      [pages_ setObject:page forKey:key];
    }
  }
  if (page != nil) {
    [pageOrder_ removeObject:key];
    [pageOrder_ addObject:key];
    if (index - offset < [page count]) return [page objectAtIndex:index - offset];
  }
  return [NSDictionary dictionaryWithObject:
    NSLocalizedString(@"Could not load conversation", nil) forKey:@"name"];
}
@end

static char *StrappyModelListSearchText(size_t count, const char *const *values)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSMutableArray *parts = [NSMutableArray arrayWithCapacity:count];
  size_t index;
  char *result;
  for (index = 0U; index < count; index++)
    [parts addObject:[StrappySession stringFromCStringOrEmpty:values[index]]];
  result = strdup([[StrappyModelRows searchTextForValues:parts] UTF8String]);
  [pool release];
  return result;
}
static int StrappyModelListCompare(const char *a, const char *b)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSComparisonResult result = [[StrappySession stringFromCStringOrEmpty:a]
    caseInsensitiveCompare:[StrappySession stringFromCStringOrEmpty:b]];
  [pool release];
  return result < 0 ? -1 : (result > 0 ? 1 : 0);
}
static int StrappyModelListContains(const char *text, const char *needle)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSRange range = [[StrappySession stringFromCStringOrEmpty:text]
    rangeOfString:[StrappySession stringFromCStringOrEmpty:needle]];
  int result = range.location != NSNotFound;
  [pool release];
  return result;
}
static void StrappyModelListGroup(void *context, const char *provider, const char *title,
  size_t offset, size_t count)
{
  NSMutableArray *groups = (NSMutableArray *)context;
  [groups addObject:[NSDictionary dictionaryWithObjectsAndKeys:
    [StrappySession stringFromCStringOrEmpty:provider],@"provider_id",
    [StrappySession stringFromCStringOrEmpty:title],@"title",
    [NSNumber XP_numberWithUnsignedInteger:(XPUInteger)offset],@"offset",
    [NSNumber XP_numberWithUnsignedInteger:(XPUInteger)count],@"count",nil]];
}
static BOOL StrappyModelListSort(NSArray *descriptors, strappy_catalog_sort *sort, NSError **error)
{
  NSUInteger index;
  if ([descriptors count] > 12U) {
    if (error != NULL) *error = [StrappySession errorFromCString:"Too many model sort keys."];
    return NO;
  }
  for (index = 0U; index < [descriptors count]; index++) {
    NSSortDescriptor *descriptor = [descriptors objectAtIndex:index];
    sort[index].key = [[descriptor key] UTF8String];
    sort[index].ascending = [descriptor ascending] ? 1 : 0;
  }
  return YES;
}
@implementation StrappyModelListSource
- (id)initWithPath:(NSString *)path search:(NSString *)search
  sortDescriptors:(NSArray *)descriptors error:(NSError **)error
{
  strappy_catalog_sort sort[12];
  strappy_model_list_text text = { StrappyModelListContains,StrappyModelListSearchText,StrappyModelListCompare };
  char *message = NULL;
  self = [super init];
  if (self == nil) return nil;
  if (!StrappyModelListSort(descriptors,sort,error)) { [self release]; return nil; }
  if (!strappy_db_model_list_open([path fileSystemRepresentation],
      [NSLocalizedString(@"Custom",nil) UTF8String],[search UTF8String],sort,
      (size_t)[descriptors count],&text,&reader_,&message)) {
    if (error != NULL) *error = [StrappySession errorFromCString:message];
    strappy_free_string(message); [self release]; return nil;
  }
  return self;
}
- (void)dealloc { strappy_db_model_list_close(reader_); [super dealloc]; }
- (NSUInteger)count { return (NSUInteger)strappy_db_model_list_count(reader_); }
- (NSUInteger)totalCount { return (NSUInteger)strappy_db_model_list_total_count(reader_); }
- (NSUInteger)allowedCount { return (NSUInteger)strappy_db_model_list_allowed_count(reader_); }
- (BOOL)hasConfiguredAccounts { return strappy_db_model_list_has_accounts(reader_) ? YES : NO; }
- (BOOL)filterWithSearch:(NSString *)search sortDescriptors:(NSArray *)descriptors error:(NSError **)error
{
  strappy_catalog_sort sort[12];
  char *message = NULL;
  if (!StrappyModelListSort(descriptors,sort,error)) return NO;
  if (!strappy_db_model_list_query(reader_,[search UTF8String],sort,(size_t)[descriptors count],&message)) {
    if (error != NULL) *error = [StrappySession errorFromCString:message];
    strappy_free_string(message); return NO;
  }
  return YES;
}
- (NSUInteger)indexForModelIdentifier:(NSString *)identifier error:(NSError **)error
{
  size_t index;
  char *message = NULL;
  if (!strappy_db_model_list_index(reader_,[identifier UTF8String],&index,&message)) {
    if (error != NULL) *error = [StrappySession errorFromCString:message];
    strappy_free_string(message); return NSNotFound;
  }
  return index == (size_t)-1 ? NSNotFound : (NSUInteger)index;
}
- (NSArray *)providerSectionsWithError:(NSError **)error
{
  NSMutableArray *groups = [NSMutableArray array];
  char *message = NULL;
  if (!strappy_db_model_list_groups(reader_,StrappyModelListGroup,groups,&message)) {
    if (error != NULL) *error = [StrappySession errorFromCString:message];
    strappy_free_string(message); return nil;
  }
  return groups;
}
- (NSArray *)pageAtOffset:(NSUInteger)offset error:(NSError **)error
{
  strappy_model_record_list records;
  char *message = NULL;
  NSMutableArray *rows;
  size_t index;
  if (!strappy_db_model_list_page(reader_,(size_t)offset,&records,&message)) {
    if (error != NULL) *error = [StrappySession errorFromCString:message];
    strappy_free_string(message); return nil;
  }
  rows = [NSMutableArray arrayWithCapacity:records.count];
  for (index = 0U; index < records.count; index++) {
    NSMutableDictionary *row = [NSMutableDictionary dictionaryWithDictionary:
      [StrappySession dictionaryFromModelRecord:&records.records[index]]];
    NSString *provider = [row objectForKey:@"provider_id"];
    NSString *name = [provider isEqualToString:@"openrouter"] ? @"OpenRouter" :
      ([provider isEqualToString:@"openai_chatgpt"] ? @"ChatGPT" : NSLocalizedString(@"Custom",nil));
    [row setObject:name forKey:@"provider_name"];
    [rows addObject:row];
  }
  strappy_model_record_list_destroy(&records);
  return rows;
}
@end
