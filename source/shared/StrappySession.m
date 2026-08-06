#import "StrappySession.h"

#import "StrappyKeychain.h"
#import "strappy_core.h"
#import "strappy_prompt.h"
#import "strappy_session.h"
#import "strappy_study.h"
#import "XPFoundation.h"

#include <string.h>

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
NSString * const StrappySessionChangeKindKey = @"change_kind";
NSString * const StrappySessionChangeKindActivity = @"activity";
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

typedef struct StrappySessionResponsesContext {
  StrappySession *session;
  NSDictionary *context;
} StrappySessionResponsesContext;

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
+ (NSArray *)openRouterModelCatalogFromList:
    (const strappy_openrouter_model_record_list *)list;
+ (NSDictionary *)dictionaryFromOpenRouterModelRecord:
    (const strappy_openrouter_model_record *)record;
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
                    context:(NSDictionary *)context;
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
  result = [session handleResponsesEvent:event context:context->context];
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
  if ((self = [super init])) {
    [self setModelIdentifier:modelIdentifier];
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
  NSString *assistantSetIdentifier;
  StrappySessionOptions *options;

  modelIdentifier = [summary objectForKey:@"model"];
  if (![modelIdentifier isKindOfClass:[NSString class]]) {
    modelIdentifier = @"";
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

- (int)handleResponsesEvent:(const strappy_responses_event *)event
                    context:(NSDictionary *)contextDictionary
{
  NSMutableDictionary *notification;
  NSString *statusJSON;

  if (event == NULL) {
    return 1;
  }
  if (event->type == STRAPPY_RESPONSES_EVENT_CANCELLATION_POLL) {
    return [self promptCancellationRequested] ? 0 : 1;
  }
  if ((event->type != STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) &&
      (event->type != STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED) &&
      (event->type != STRAPPY_RESPONSES_EVENT_LEDGER_UPDATED)) {
    return 1;
  }

  notification = [[NSMutableDictionary alloc] init];
  statusJSON = nil;
  if (contextDictionary != nil) {
    [notification setObject:contextDictionary forKey:@"context"];
  }
  if (event->message_key != NULL) {
    NSString *messageKey;

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
  [notification setObject:
    (event->type == STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS) ?
      @"processing_status" :
      ((event->type == STRAPPY_RESPONSES_EVENT_LEDGER_UPDATED) ?
        @"ledger_updated" : @"ledger_changed")
                    forKey:@"stream_event"];
  [self performSelectorOnMainThread:@selector(postStreamEventAndRelease:)
                         withObject:notification
                      waitUntilDone:NO];
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

  strappyError = NULL;
  ok = strappy_session_configure_process([caCertPath fileSystemRepresentation],
                                         [fontsPath fileSystemRepresentation],
                                         &strappyError);
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

+ (NSDictionary *)dictionaryFromOpenRouterModelRecord:
    (const strappy_openrouter_model_record *)record
{
  NSString *modelId;
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
  typedef BOOL (*StrappyModernCreateDirectoryFunction)(id,
                                                       SEL,
                                                       NSString *,
                                                       BOOL,
                                                       NSDictionary *,
                                                       NSError **);
  typedef BOOL (*StrappyLegacyCreateDirectoryFunction)(id,
                                                       SEL,
                                                       NSString *,
                                                       NSDictionary *);
  NSFileManager *fileManager;
  NSString *directoryPath;
  BOOL isDirectory;
  SEL modernSelector;
  SEL legacySelector;

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

  modernSelector =
    @selector(createDirectoryAtPath:withIntermediateDirectories:attributes:error:);
  if ([fileManager respondsToSelector:modernSelector]) {
    StrappyModernCreateDirectoryFunction createDirectory;

    createDirectory =
      (StrappyModernCreateDirectoryFunction)[fileManager methodForSelector:modernSelector];
    if (createDirectory(fileManager,
                        modernSelector,
                        directoryPath,
                        YES,
                        nil,
                        error)) {
      return YES;
    }
    return NO;
  }

  legacySelector = @selector(createDirectoryAtPath:attributes:);
  if ([fileManager respondsToSelector:legacySelector]) {
    StrappyLegacyCreateDirectoryFunction createDirectory;

    createDirectory =
      (StrappyLegacyCreateDirectoryFunction)[fileManager methodForSelector:legacySelector];
    if (createDirectory(fileManager,
                        legacySelector,
                        directoryPath,
                        [NSDictionary dictionary])) {
      return YES;
    }
  }

  if (error != nil) {
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
  if (!ok) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return NO;
  }

  return YES;
}

+ (NSArray *)openRouterModelCatalogFromList:
    (const strappy_openrouter_model_record_list *)list
{
  NSMutableArray *models;
  size_t index;

  if (list == NULL) {
    return nil;
  }

  models = [NSMutableArray arrayWithCapacity:list->count];
  for (index = 0U; index < list->count; index++) {
    NSDictionary *model =
      [StrappySession dictionaryFromOpenRouterModelRecord:&list->records[index]];
    if (model != nil) {
      [models addObject:model];
    }
  }
  return models;
}

+ (NSArray *)openRouterModelCatalogMatchingSearchText:(NSString *)searchText
                                                error:(NSError **)error
{
  NSString *databasePath;
  strappy_openrouter_model_record_list list;
  NSArray *models;
  char *strappyError;
  const char *searchCString;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappy_openrouter_model_record_list_init(&list);
  strappyError = NULL;
  searchCString = ((searchText != nil) && ([searchText length] > 0U)) ?
    [searchText UTF8String] : NULL;
  if (!strappy_session_list_openrouter_models_matching([databasePath UTF8String],
                                                  searchCString,
                                                  &list,
                                                  &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_openrouter_model_record_list_destroy(&list);
    return nil;
  }

  models = [StrappySession openRouterModelCatalogFromList:&list];
  strappy_openrouter_model_record_list_destroy(&list);
  return models;
}

+ (NSArray *)openRouterModelCatalogWithError:(NSError **)error
{
  return [StrappySession openRouterModelCatalogMatchingSearchText:nil
                                                            error:error];
}

+ (NSArray *)allowedOpenRouterModelCatalogWithError:(NSError **)error
{
  NSString *databasePath;
  strappy_openrouter_model_record_list list;
  NSArray *models;
  char *strappyError;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappy_openrouter_model_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_session_list_allowed_openrouter_models([databasePath UTF8String],
                                                 &list,
                                                 &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_openrouter_model_record_list_destroy(&list);
    return nil;
  }

  models = [StrappySession openRouterModelCatalogFromList:&list];
  strappy_openrouter_model_record_list_destroy(&list);
  return models;
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

+ (NSString *)defaultOpenRouterModelIdentifierWithError:(NSError **)error
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
  if (!strappy_session_get_default_openrouter_model([databasePath UTF8String],
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

+ (BOOL)setDefaultOpenRouterModelIdentifier:(NSString *)modelIdentifier
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
  ok = strappy_session_set_default_openrouter_model([databasePath UTF8String],
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

+ (NSString *)selectedOpenRouterModelIdentifierWithError:(NSError **)error
{
  return [StrappySession defaultOpenRouterModelIdentifierWithError:error];
}

+ (BOOL)setSelectedOpenRouterModelIdentifier:(NSString *)modelIdentifier
                                       error:(NSError **)error
{
  return [StrappySession setDefaultOpenRouterModelIdentifier:modelIdentifier
                                                       error:error];
}

+ (BOOL)setOpenRouterModelAllowed:(BOOL)allowed
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
  ok = strappy_session_set_openrouter_model_allowed([databasePath UTF8String],
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

+ (BOOL)beginOpenRouterModelCatalogRefreshWithError:(NSError **)error
{
  NSString *databasePath;

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
                         withObject:nil];
  return YES;
}

+ (void)refreshOpenRouterModelCatalogInBackground:(id)ignored
{
  NSAutoreleasePool *pool;
  NSString *databasePath;
  NSString *apiEndpoint;
  NSString *apiToken;
  NSMutableDictionary *result;
  char *strappyError;
  int ok;

  (void)ignored;
  pool = [[NSAutoreleasePool alloc] init];
  databasePath = [StrappySession sessionsDatabasePath];
  apiEndpoint = [[StrappyKeychain sharedKeychain] apiEndpoint];
  apiToken = [[StrappyKeychain sharedKeychain] apiToken];
  result = [[NSMutableDictionary alloc] init];

  strappyError = NULL;
  ok = strappy_session_refresh_openrouter_user_models(
    StrappySessionOptionalCString(apiEndpoint),
    StrappySessionOptionalCString(apiToken),
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

    models = [StrappySession openRouterModelCatalogWithError:nil];
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

+ (NSString *)databaseStudyJSONWithError:(NSError **)error
{
  NSString *databasePath;
  char *json;
  char *strappyError;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }
  strappyError = NULL;
  json = strappy_study_status_json([databasePath UTF8String], &strappyError);
  if (json == NULL) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return nil;
  }
  strappy_session_free_string(strappyError);
  return StrappySessionStringFromCString(json);
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

+ (BOOL)databaseStudyProgressWithStudiedCount:(NSUInteger *)studiedCount
                                approvedCount:(NSUInteger *)approvedCount
                                        error:(NSError **)error
{
  NSString *databasePath;
  char *strappyError;
  size_t approved;
  size_t studied;

  if (studiedCount != NULL) {
    *studiedCount = 0U;
  }
  if (approvedCount != NULL) {
    *approvedCount = 0U;
  }
  if ((studiedCount == NULL) || (approvedCount == NULL)) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:15
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Database Study progress has no output.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return NO;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return NO;
  }
  approved = 0U;
  studied = 0U;
  strappyError = NULL;
  if (!strappy_study_progress([databasePath UTF8String],
                              &studied,
                              &approved,
                              &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return NO;
  }
  strappy_session_free_string(strappyError);
  *studiedCount = (NSUInteger)studied;
  *approvedCount = (NSUInteger)approved;
  return YES;
}

+ (NSUInteger)databaseStudyPendingDatabaseCountWithError:(NSError **)error
{
  NSString *databasePath;
  strappy_study_database_id_list list;
  char *strappyError;
  NSUInteger count;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return 0U;
  }
  strappy_study_database_id_list_init(&list);
  strappyError = NULL;
  if (!strappy_study_list_unstudied_database_ids([databasePath UTF8String],
                                                  &list,
                                                  &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return 0U;
  }
  count = (NSUInteger)list.count;
  strappy_study_database_id_list_destroy(&list);
  strappy_session_free_string(strappyError);
  return count;
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

  defaultModel = [StrappySession defaultOpenRouterModelIdentifierWithError:error];
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
  pageHTML = strappy_session_webview_messages_page_html_for_session(
    [databasePath fileSystemRepresentation],
    sessionId,
    [resourcePath fileSystemRepresentation],
    displayErrorText,
    StrappySessionOptionalCString(processingStatusJSON),
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
  NSString *databasePath;
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

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  sessionId = [sessionIdentifier_ isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier_ longLongValue] : 0LL;
  storedAppendedMessageCount = 0U;
  storedNextTimelineCursor = NULL;
  strappyError = NULL;
  javaScript = strappy_session_webview_append_messages_js_for_session(
    [databasePath fileSystemRepresentation],
    sessionId,
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

- (NSString *)webViewJavaScriptForStreamEvent:(NSDictionary *)event
                                        error:(NSError **)error
{
  NSString *databasePath;
  NSString *messageKey;
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
    return StrappySessionStringFromCString(
      strappy_session_webview_set_processing_status_js(
        [statusJSON UTF8String]));
  }
  if (![streamEvent isEqualToString:@"ledger_changed"] &&
      ![streamEvent isEqualToString:@"ledger_updated"]) {
    return @"";
  }

  sessionId = [sessionIdentifier_ isKindOfClass:[NSNumber class]] ?
    [sessionIdentifier_ longLongValue] : 0LL;
  if (sessionId <= 0) {
    return @"";
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return @"";
  }

  strappyError = NULL;
  js = strappy_session_webview_message_update_js_for_key(
    [databasePath UTF8String],
    sessionId,
    [messageKey UTF8String],
    &strappyError);
  if (js == NULL) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    return @"";
  }

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
      [NSNumber numberWithUnsignedInteger:
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
  char *strappyError;
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

  responsesContext.session = self;
  responsesContext.context = [context retain];
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
    [responsesContext.context release];
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_session_free_string(strappyError);
    strappy_session_record_destroy(&record);
    return nil;
  }
  [responsesContext.context release];

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

  summary = nil;
  if ([userInfo objectForKey:@"error"] == nil) {
    summary = [self summaryWithError:nil];
    if (summary != nil) {
      [userInfo setObject:summary forKey:@"session"];
    }
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
