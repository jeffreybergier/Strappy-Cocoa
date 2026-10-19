#import <AICollectionController.h>
#import <AICollectionSnapshot.h>
#import <AIStoreObserver.h>
#import "StrappyAsync.h"
#include "strappy_run.h"
#import "strappy_store_client.h"
#import "FileScanner.h"
#import "StrappySession.h"

#import "strappy_core.h"
#import "strappy_prompt.h"
#import "strappy_provider.h"
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
NSString * const StrappyCredentialStatusDidChangeNotification = @"StrappyCredentialStatusDidChangeNotification";
static NSMutableDictionary *StrappyCredentialCache, *StrappyCredentialPending;
static NSUInteger StrappyCredentialGeneration;
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

static BOOL StrappySessionModelCatalogRefreshInFlight = NO;
static BOOL StrappySessionModelCatalogRefreshPending = NO;


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
+ (NSArray *)messagesForSessionIdentifier:(NSNumber *)sessionIdentifier
                                    error:(NSError **)error;
+ (NSArray *)modelCatalogFromList:(const strappy_model_record_list *)list;
+ (NSDictionary *)dictionaryFromModelRecord:
    (const strappy_model_record *)record;
+ (NSDictionary *)dictionaryFromAssistantSetRecord:
    (const strappy_assistant_set_record *)record;
+ (NSDictionary *)dictionaryFromDatabaseStudyStatusRecord:
    (const strappy_study_database_status_record *)record;
+ (NSString *)guidanceResourceDirectoryWithError:(NSError **)error;
+ (void)openRouterModelCatalogRefreshDidFinish:(NSDictionary *)result;
+ (void)waitForModelCatalogRefresh:(NSString *)key;
+ (void)modelCatalogJobDidFinish:(NSDictionary *)result;
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
- (BOOL)submitPromptRequest:(NSDictionary *)request error:(NSError **)error;
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

static NSString *StrappyServiceCommand(unsigned int operation, NSDictionary *values, NSError **error)
{
  cJSON *command = cJSON_CreateObject();
  NSEnumerator *keys = [values keyEnumerator];
  NSString *key;
  char *result;
  unsigned int status;
  NSString *text;
  while ((key = [keys nextObject]) != nil)
    cJSON_AddStringToObject(command,[key UTF8String],[[[values objectForKey:key] description] UTF8String]);
  NSString *action = [values objectForKey:@"action"];
  BOOL credential = [action isEqualToString:@"credential_status"] ||
    [action isEqualToString:@"update_account"] || [action isEqualToString:@"archive_account"] ||
    [action isEqualToString:@"delete_oauth"] || [action isEqualToString:@"refresh_oauth"];
  NSCAssert(!credential || ![NSThread XP_isMainThread], @"Credential jobs must wait off the main thread");
  result = credential ? strappy_store_client_credential(command,&status) :
    strappy_store_client_mutation(operation,command,&status);
  { cJSON *item; for (item=command->child; item; item=item->next)
      if (item->valuestring) memset(item->valuestring,0,strlen(item->valuestring)); }
  cJSON_Delete(command);
  text = result ? [NSString stringWithUTF8String:result] : nil;
  free(result);
  if (text == nil && error != nil) {
    NSString *message = status == STRAPPY_IPC_BUSY ?
      @"The background service is busy. Try again after the current work finishes." :
      (status == STRAPPY_IPC_NOT_READY ? @"The service did not acknowledge this change. Reload stored state before trying again." :
       @"The background service could not apply this change. Check the selected values and account credentials.");
    *error = [NSError errorWithDomain:@"StrappyServiceErrorDomain" code:(NSInteger)status
      userInfo:[NSDictionary dictionaryWithObject:message forKey:NSLocalizedDescriptionKey]];
  }
  return text;
}
static NSString *StrappyText(NSString *text) { return text ? text : @""; }
static NSString *StrappyNumber(long long value) { return [NSString stringWithFormat:@"%lld",value]; }
static NSString *StrappyWorkingDirectory(void)
{
  NSArray *paths = [StrappySession codingWorkingDirectoryPaths];
  return [paths count] ? [paths objectAtIndex:0] : NSHomeDirectory();
}
static NSDictionary *StrappyOptionsCommand(StrappySessionOptions *options,
    StrappySessionOptionMask mask, NSNumber *session, NSError **error)
{
  strappy_session_options record;
  if (!StrappySessionRecordFromOptions(options,&record,error)) return nil;
  return [NSDictionary dictionaryWithObjectsAndKeys:
    session ? @"options" : @"defaults",@"action",session ? [session stringValue] : @"0",@"session",
    StrappyNumber((long long)mask),@"mask",StrappyText([options modelIdentifier]),@"model",
    StrappyText([options providerAccountIdentifier]),@"account",StrappyText([options assistantSetIdentifier]),@"assistant",
    StrappyText([options workingDirectory]),@"directory",(session ? @"" : StrappyWorkingDirectory()),@"fallback",
    StrappyText([options webProvider]),@"web_provider",StrappyNumber([options webSearchEnabled]),@"web",
    StrappyNumber([options bashEnabled]),@"bash",StrappyNumber([options limitToOneTool]),@"one_tool",
    StrappyNumber([options answerQualityEnabled]),@"quality",StrappyNumber((long long)[options roundLimit]),@"rounds",nil];
}

/* UI drafts are process-local and shared by session identity, so changing
 * selection or recreating a controller does not lose unsent options. Only Send
 * persists them, on its worker. Never hold this lock across disk or IPC work. */
static NSMutableDictionary *StrappyOptionDrafts;
static NSMutableSet *StrappyPendingSubmissions;
static unsigned long long StrappyOptionDraftGeneration;
static NSString *StrappyOptionDraftProcess;

static NSDictionary *StrappyOptionDraft(NSNumber *identifier)
{
  @synchronized([StrappySession class]) {
    return [[[StrappyOptionDrafts objectForKey:identifier] retain] autorelease];
  }
  return nil;
}

static void StrappyMergeOptions(StrappySessionOptions *target,
    StrappySessionOptions *source, StrappySessionOptionMask mask)
{
  if (mask & StrappySessionOptionModel) [target setModelIdentifier:[source modelIdentifier]];
  if (mask & StrappySessionOptionProviderAccount) [target setProviderAccountIdentifier:[source providerAccountIdentifier]];
  if (mask & StrappySessionOptionAssistantSet) [target setAssistantSetIdentifier:[source assistantSetIdentifier]];
  if (mask & StrappySessionOptionWorkingDirectory) [target setWorkingDirectory:[source workingDirectory]];
  if (mask & StrappySessionOptionWebProvider) [target setWebProvider:[source webProvider]];
  if (mask & StrappySessionOptionWebSearch) [target setWebSearchEnabled:[source webSearchEnabled]];
  if (mask & StrappySessionOptionBash) [target setBashEnabled:[source bashEnabled]];
  if (mask & StrappySessionOptionLimitToOneTool) [target setLimitToOneTool:[source limitToOneTool]];
  if (mask & StrappySessionOptionAnswerQuality) [target setAnswerQualityEnabled:[source answerQualityEnabled]];
  if (mask & StrappySessionOptionRoundLimit) [target setRoundLimit:[source roundLimit]];
}

NSString * const StrappySessionListActivityDidChangeNotification =
  @"StrappySessionListActivityDidChangeNotification";
static AIStoreObserver *StrappyStoreObserver;
static void StrappyStoreChanged(unsigned int topics, long long session)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  /* The C feed runs off-main after commit. Read only the changed status,
   * once per push, never from a cell callback. Read failure forces resync. */
  if (topics == STRAPPY_STORE_RUN && session > 0) {
    int active;
    if (strappy_run_is_active([[StrappySession sessionsDatabasePath]
        fileSystemRepresentation], session, &active)) {
      [StrappyAsync postNotificationName:
        StrappySessionListActivityDidChangeNotification object:nil
        userInfo:[NSDictionary dictionaryWithObject:
          [NSNumber numberWithBool:active] forKey:
          [NSNumber numberWithLongLong:session]]];
    } else session = 0;
  }
  [StrappyStoreObserver publishTopics:topics
    scope:[NSNumber numberWithLongLong:session] generation:0];
  [pool release];
}

/* Main-thread observation owner; borrowed controller must invalidate before release. */
static id StrappyObservationOwner;
static NSNumber *StrappyObservedSession;
static NSTimer *StrappyObservationTimer;
static BOOL StrappyObservationActive;
static uint64_t StrappyObservationToken;

@implementation StrappySession

+ (void)renewSessionObservation:(NSTimer *)timer;
{
  (void)timer;
  if (StrappyObservationActive && StrappyObservedSession)
    (void)strappy_store_client_observe([StrappyObservedSession longLongValue],
      StrappyObservationToken,2);
}

+ (void)observeSessionIdentifier:(NSNumber *)identifier owner:(id)owner;
{
  if (!owner || !identifier || [identifier longLongValue]<=0) return;
  StrappyObservationOwner=owner;
  [StrappyObservedSession release]; StrappyObservedSession=[identifier copy];
  ++StrappyObservationToken;
  if (StrappyObservationActive)
    (void)strappy_store_client_observe([identifier longLongValue],StrappyObservationToken,1);
  if (!StrappyObservationTimer) {
    StrappyObservationTimer=[[NSTimer timerWithTimeInterval:5.0 target:self
      selector:@selector(renewSessionObservation:) userInfo:nil repeats:YES] retain];
    [[NSRunLoop mainRunLoop] addTimer:StrappyObservationTimer forMode:NSRunLoopCommonModes];
  }
}

+ (void)stopObservingSessionForOwner:(id)owner;
{
  if (owner!=StrappyObservationOwner) return;
  if (StrappyObservedSession)
    (void)strappy_store_client_observe([StrappyObservedSession longLongValue],StrappyObservationToken,0);
  StrappyObservationOwner=nil;
  [StrappyObservedSession release]; StrappyObservedSession=nil;
  [StrappyObservationTimer invalidate]; [StrappyObservationTimer release];
  StrappyObservationTimer=nil;
}

+ (void)setSessionObservationActive:(BOOL)active;
{
  if (active==StrappyObservationActive) return;
  StrappyObservationActive=active;
  if (!StrappyObservedSession) return;
  if (active) ++StrappyObservationToken;
  (void)strappy_store_client_observe([StrappyObservedSession longLongValue],
    StrappyObservationToken,active ? 1U : 0U);
}


+ (void)serviceStoreChanged:(NSDictionary *)change
{
  unsigned int topics = [[change objectForKey:@"topics"] unsignedIntValue];
  unsigned int globalTopics = [[change objectForKey:@"globalTopics"] unsignedIntValue];
  NSDictionary *scopes = [change objectForKey:@"scopes"];
  if (topics & (STRAPPY_STORE_CATALOG | STRAPPY_STORE_RUN)) {
    unsigned int active, last, result;
    strappy_store_client_work_status(&active,&last,&result);
    /* Locally submitted jobs report their keyed result independently of hints. */
    if (!StrappySessionModelCatalogRefreshPending &&
        [self isModelCatalogRefreshInFlight] && active != STRAPPY_IPC_REFRESH_CATALOG) {
      [self openRouterModelCatalogRefreshDidFinish:(last == STRAPPY_IPC_REFRESH_CATALOG && result == STRAPPY_RUN_COMPLETED) ?
        [NSDictionary dictionary] : [NSDictionary dictionaryWithObject:@"Model refresh failed or the service restarted." forKey:@"error"]];
    } else if (!StrappySessionModelCatalogRefreshPending &&
        ![self isModelCatalogRefreshInFlight] && active == STRAPPY_IPC_REFRESH_CATALOG) {
      @synchronized(self) { StrappySessionModelCatalogRefreshInFlight = YES; }
      [StrappyAsync postNotificationName:StrappySessionModelCatalogRefreshDidStartNotification object:self];
    }
    [FileScanner serviceWorkDidChange];
  }
  if (topics & (STRAPPY_STORE_SESSION | STRAPPY_STORE_LEDGER | STRAPPY_STORE_RUN)) {
    [StrappyAsync postNotificationName:@"StrappyServiceChanged" object:nil
      userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
        [NSNumber numberWithUnsignedInt:topics], @"topics",
        [NSNumber numberWithUnsignedInt:globalTopics], @"globalTopics",
        scopes ? scopes : [NSDictionary dictionary], @"scopes", nil]];
    if (globalTopics || (topics & (STRAPPY_STORE_SESSION | STRAPPY_STORE_LEDGER)))
      [StrappyAsync postNotificationName:StrappySessionDidUpdateNotification object:nil];
  }
  if (topics & STRAPPY_STORE_CATALOG) {
    @synchronized(self) { ++StrappyCredentialGeneration; [StrappyCredentialCache removeAllObjects]; }
    [StrappyAsync postNotificationName:StrappyProviderAccountsDidChangeNotification object:nil];
    [StrappyAsync postNotificationName:StrappySessionModelCatalogDidChangeNotification object:nil];
    [StrappyAsync postNotificationName:FileScannerDatabaseCatalogDidChangeNotification object:nil];
  }
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
  int active = 0;
  if ([sessionIdentifier isKindOfClass:[NSNumber class]])
    (void)strappy_run_is_active([[self sessionsDatabasePath] fileSystemRepresentation],
      [sessionIdentifier longLongValue], &active);
  return active ? YES : NO;
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

  if (![sessionIdentifier isKindOfClass:[NSNumber class]]) {
    return nil;
  }

  return [[[self alloc] initWithSessionIdentifier:sessionIdentifier
                                         summary:nil] autorelease];
}

+ (StrappySession *)sessionWithSummary:(NSDictionary *)summary
{
  NSNumber *identifier;

  if (![summary isKindOfClass:[NSDictionary class]]) {
    return nil;
  }

  identifier = [summary objectForKey:@"id"];
  if (![identifier isKindOfClass:[NSNumber class]]) {
    return nil;
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
    promptInFlight_ = [StrappySession isPromptInFlightForSessionIdentifier:sessionIdentifier_];
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
  @synchronized([StrappySession class]) {
    if ([StrappyPendingSubmissions containsObject:sessionIdentifier_]) return YES;
  }
  @synchronized(self) { return promptSubmissionPending_ || promptInFlight_; }
  return NO;
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
  BOOL terminalChanged = NO;
  unsigned int topics = ignored ? [[[ignored userInfo] objectForKey:@"topics"] unsignedIntValue] : STRAPPY_STORE_ALL;
  if (ignored) {
    NSDictionary *info = [ignored userInfo];
    topics = [[info objectForKey:@"globalTopics"] unsignedIntValue] |
      [[[info objectForKey:@"scopes"] objectForKey:sessionIdentifier_] unsignedIntValue];
    if (!(topics & (STRAPPY_STORE_SESSION | STRAPPY_STORE_LEDGER | STRAPPY_STORE_RUN))) return;
  }
  @synchronized(self) { optionsLoaded_ = NO; if (promptSubmissionPending_) return; }
  active = [self isPromptInFlight];
  if (topics & (STRAPPY_STORE_RUN | STRAPPY_STORE_LEDGER)) {
    if (!strappy_run_latest([[StrappySession sessionsDatabasePath] fileSystemRepresentation],
        [sessionIdentifier_ longLongValue], &run)) return;
    active = run.state == STRAPPY_RUN_ACCEPTED || run.state == STRAPPY_RUN_RUNNING;
    pendingKey = [NSString stringWithFormat:@"StrappyPendingPrompt-%@", sessionIdentifier_];
    pending = [[NSUserDefaults standardUserDefaults] dictionaryForKey:pendingKey];
    if (run.request_key && [[pending objectForKey:@"key"] isEqualToString:[NSString stringWithUTF8String:run.request_key]]) {
      @synchronized([StrappySession class]) {
        NSDictionary *draft = [StrappyOptionDrafts objectForKey:sessionIdentifier_];
        if ([[draft objectForKey:@"generation"] isEqual:[pending objectForKey:@"optionsGeneration"]])
          [StrappyOptionDrafts removeObjectForKey:sessionIdentifier_];
      }
      [[NSUserDefaults standardUserDefaults] removeObjectForKey:pendingKey];
      [[NSUserDefaults standardUserDefaults] synchronize];
    }
    /* Resolve persisted acceptance above, but leave an unknown submission for
     * an explicit Send retry. Refresh callbacks must not resubmit work. */
    terminalChanged = run.id > 0 && (observedServiceRun_ != run.id || observedServiceState_ != run.state);
    if (run.state == STRAPPY_RUN_FAILED) terminalError = @"The prompt failed. Check the provider account and stored request details.";
    else if (run.state == STRAPPY_RUN_INTERRUPTED) terminalError = @"The background service stopped during this prompt. It was not replayed; check any tool effects before retrying.";
    else if (run.state == STRAPPY_RUN_CANCELLED) terminalError = @"The prompt was cancelled.";
    observedServiceRun_ = run.id; observedServiceState_ = run.state;
    strappy_run_destroy(&run);
  }
  @synchronized(self) { previous = promptInFlight_; promptInFlight_ = active; }
  summary = (topics & (STRAPPY_STORE_SESSION | STRAPPY_STORE_LEDGER)) ?
    [StrappySession sessionSummaryForSessionIdentifier:sessionIdentifier_ error:nil] : nil;
  if (summary != nil) {
    StrappySessionOptions *storedOptions;
    [self updateCachedSummary:summary];
    storedOptions = [self optionsWithError:nil];
    if (storedOptions != nil) [StrappyAsync postNotificationName:StrappySessionDidUpdateNotification object:self
      userInfo:[NSDictionary dictionaryWithObjectsAndKeys:summary,@"session",
        storedOptions,StrappySessionOptionsKey,
        [NSNumber XP_numberWithUnsignedInteger:StrappySessionOptionAll],StrappySessionChangedOptionsKey,
        StrappySessionChangeKindOptions,StrappySessionChangeKindKey,nil]];
  }
  if (topics & (STRAPPY_STORE_LEDGER | STRAPPY_STORE_RUN)) [StrappyAsync postNotificationName:StrappySessionStreamEventNotification
      object:self userInfo:[NSDictionary dictionaryWithObject:[NSNumber numberWithBool:YES]
      forKey:@"service_invalidated"]];
  if (active && !previous) [StrappyAsync postNotificationName:StrappySessionPromptDidStartNotification object:self];
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

+ (BOOL)isBackgroundServiceReady;
{
  return strappy_db_service_ready([[self sessionsDatabasePath]
    fileSystemRepresentation]) ? YES : NO;
}

+ (void)bootstrapProcess
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
  if (!StrappyStoreObserver) StrappyStoreObserver = [[AIStoreObserver alloc]
    initWithStoreIdentity:[self sessionsDatabasePath] target:self
    selector:@selector(serviceStoreChanged:)];
  if (!strappy_store_client_start([[socketDirectory stringByAppendingPathComponent:@"service.sock"] fileSystemRepresentation],
      [[self sessionsDatabasePath] fileSystemRepresentation], StrappyStoreChanged)) {
    [NSException raise:NSInternalInconsistencyException format:@"Could not start the service monitor."];
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
  ok = strappy_session_configure_presentation([fontsPath fileSystemRepresentation],
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

+ (NSError *)errorFromCString:(const char *)message
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
    [StrappyAsync postNotificationName:StrappyProviderAccountsDidChangeNotification
                    object:self
                  userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
                    @"prepared", @"action", nil]];
  }
  return ok;
}

+ (BOOL)deleteChatGPTCredentialsForProviderAccountIdentifier:(NSString *)identifier error:(NSError **)error
{
  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"delete_oauth",@"action",StrappyText(identifier),@"id",nil],error) != nil;
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
                (NSString *)identifier
                observer:(id<StrappyChatGPTAuthorizationObserver>)observer
                context:(id)operationContext
                error:(NSError **)error
{
  char requestKey[65];
  if (!strappy_store_client_request_key(requestKey)) return nil;
  NSString *key = [NSString stringWithUTF8String:requestKey];
  if (error) *error = nil;
  NSString *action = @"auth_start";
  BOOL displayedCode = NO;
  NSUInteger missedReplies = 0;
  if (![identifier length] || observer == nil || key == nil) return nil;
  for (;;) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSString *reply;
    NSString *identity = nil;
    cJSON *snapshot;
    const cJSON *state, *url, *code, *account;
    BOOL cancelled = [observer strappyChatGPTAuthorizationShouldCancelWithContext:operationContext];
    if (error) *error = nil;
    reply = StrappyServiceCommand(STRAPPY_IPC_AUTH,
      [NSDictionary dictionaryWithObjectsAndKeys:cancelled ? @"auth_cancel" : action,@"action",
        identifier,@"id",key,@"key",nil],error);
    snapshot = reply ? cJSON_Parse([reply UTF8String]) : NULL;
    state = cJSON_GetObjectItemCaseSensitive(snapshot,"state");
    url = cJSON_GetObjectItemCaseSensitive(snapshot,"url");
    code = cJSON_GetObjectItemCaseSensitive(snapshot,"code");
    account = cJSON_GetObjectItemCaseSensitive(snapshot,"identity");
    if (!cancelled && cJSON_IsString(state) && !strcmp(state->valuestring,"awaiting_user") &&
        !displayedCode && cJSON_IsString(url) && cJSON_IsString(code)) {
      [observer strappyChatGPTAuthorizationDidReceiveVerificationURL:
          [NSString stringWithUTF8String:url->valuestring]
        userCode:[NSString stringWithUTF8String:code->valuestring] context:operationContext];
      displayedCode = YES;
    }
    if (!cancelled && cJSON_IsString(state) && !strcmp(state->valuestring,"signed_in") && cJSON_IsString(account))
      identity = [[NSString alloc] initWithUTF8String:account->valuestring];
    if (cJSON_IsString(state)) missedReplies = 0; else ++missedReplies;
    BOOL unknown = (cJSON_IsString(state) && !strcmp(state->valuestring,"unknown")) || missedReplies >= 3;
    BOOL refused = reply == nil && error && *error && [*error code] != STRAPPY_IPC_NOT_READY;
    BOOL finished = cancelled || unknown || refused || identity != nil ||
      (cJSON_IsString(state) && (!strcmp(state->valuestring,"failed") || !strcmp(state->valuestring,"cancelled")));
    if (unknown && error) *error = [self errorFromCString:
      "The service lost the sign-in status. Check credential state before starting another sign-in."];
    cJSON_Delete(snapshot);
    if (finished && !identity && error != NULL && *error == nil)
      *error = [self errorFromCString:cancelled ? "ChatGPT sign-in cancelled." : "ChatGPT sign-in failed."];
    NSError *retainedError = error ? [*error retain] : nil;
    [pool drain];
    if (error) *error = [retainedError autorelease];
    if (finished) return [identity autorelease];
    action = @"auth_status";
    [NSThread sleepForTimeInterval:0.25];
  }
}

+ (NSString *)refreshChatGPTCredentialsForProviderAccountIdentifier:
                (NSString *)providerAccountIdentifier
                expectedAccountIdentifier:(NSString *)expectedAccountIdentifier
                observer:(id<StrappyChatGPTAuthorizationObserver>)observer
                context:(id)operationContext
                error:(NSError **)error
{
  (void)observer; (void)operationContext;
  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"refresh_oauth",@"action",
    StrappyText(providerAccountIdentifier),@"id",
    StrappyText(expectedAccountIdentifier),@"identity",
    nil],error);
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
    BOOL hasCredential, credentialKnown, credentialUnavailable = NO;

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
      credentialKnown = hasCredential;
      if ((definition != NULL) && !hasCredential) {
        NSDictionary *status = [self credentialStatusForProviderAccountIdentifier:accountIdentifier error:NULL];
        credentialKnown = [status objectForKey:@"present"] != nil;
        credentialUnavailable = [[status objectForKey:@"unavailable"] boolValue];
        hasCredential = [[status objectForKey:@"present"] boolValue];
      }
      missingFields = strappy_provider_account_missing_required_fields(
        definition,
        [[account objectForKey:@"name"] UTF8String],
        [responsesEndpoint UTF8String],
        (hasCredential || !credentialKnown) ? 1 : 0);
      if ((missingFields & STRAPPY_PROVIDER_ACCOUNT_MISSING_ENDPOINT) != 0U) {
        available = NO;
      }
      if (verifyCredentials &&
          (!credentialKnown || (missingFields & STRAPPY_PROVIDER_ACCOUNT_MISSING_CREDENTIAL) != 0U)) {
        available = NO;
      }
      availableAccount = [[account mutableCopy] autorelease];
      [availableAccount setObject:[NSNumber numberWithBool:hasCredential] forKey:@"has_credential"];
      [availableAccount setObject:[NSNumber numberWithBool:credentialKnown] forKey:@"credential_status_known"];
      [availableAccount setObject:[NSNumber numberWithBool:credentialUnavailable] forKey:@"credential_status_unavailable"];
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
  NSArray *accounts = [self providerAccountCatalogWithError:error];
  NSString *identifier;
  NSEnumerator *enumerator;
  NSDictionary *account;
  if (!accounts) return nil;
  identifier = StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"create_account",@"action",
    StrappyText(providerIdentifier),@"id",
    [self defaultProviderAccountNameForProviderIdentifier:providerIdentifier accounts:accounts],@"name",
    nil],error);
  if (!identifier) return nil;
  enumerator = [[self providerAccountCatalogWithError:error] objectEnumerator];
  while ((account = [enumerator nextObject]) != nil)
    if ([[account objectForKey:@"id"] isEqualToString:identifier]) return account;
  return nil;
}

+ (BOOL)updateProviderAccountIdentifier:(NSString *)providerAccountIdentifier
                            displayName:(NSString *)displayName
                      responsesEndpoint:(NSString *)responsesEndpoint
                        maxOutputTokens:(long long)maxOutputTokens
                                  error:(NSError **)error
{
  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"update_account",@"action",
    StrappyText(providerAccountIdentifier),@"id",
    StrappyText(displayName),@"name",
    StrappyText(responsesEndpoint),@"endpoint",
    StrappyNumber(maxOutputTokens),@"output",
    nil],error) != nil;
}

+ (NSDictionary *)loadCredentialStatus:(NSString *)identifier error:(NSError **)error
{
  NSString *reply = StrappyServiceCommand(STRAPPY_IPC_AUTH,
    [NSDictionary dictionaryWithObjectsAndKeys:@"credential_status",@"action",
      StrappyText(identifier),@"id",nil],error);
  cJSON *snapshot = reply ? cJSON_Parse([reply UTF8String]) : NULL;
  const cJSON *present = cJSON_GetObjectItemCaseSensitive(snapshot,"present");
  const cJSON *identity = cJSON_GetObjectItemCaseSensitive(snapshot,"identity");
  const cJSON *refresh = cJSON_GetObjectItemCaseSensitive(snapshot,"needs_refresh");
  NSDictionary *result = nil;
  if (cJSON_IsBool(present) && cJSON_IsString(identity) && cJSON_IsBool(refresh))
    result = [NSDictionary dictionaryWithObjectsAndKeys:
      [NSNumber numberWithBool:cJSON_IsTrue(present) ? YES : NO],@"present",
      [NSString stringWithUTF8String:identity->valuestring],@"identity",
      [NSNumber numberWithBool:cJSON_IsTrue(refresh) ? YES : NO],@"needs_refresh",nil];
  cJSON_Delete(snapshot);
  return result;
}

+ (void)fetchCredentialStatus:(NSDictionary *)request
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSString *identifier = [request objectForKey:@"id"];
  NSDictionary *status = [self loadCredentialStatus:identifier error:NULL];
  if (!status) status = [NSDictionary dictionaryWithObject:[NSNumber numberWithBool:YES] forKey:@"unavailable"];
  BOOL current;
  @synchronized(self) {
    current = [[request objectForKey:@"generation"] XP_unsignedIntegerValue] == StrappyCredentialGeneration;
    [StrappyCredentialPending removeObjectForKey:identifier];
    if (current) [StrappyCredentialCache setObject:[NSDictionary dictionaryWithObjectsAndKeys:
      status,@"status",[NSDate date],@"date",nil] forKey:identifier];
  }
  /* Invalidation during a read discards it. The notification causes observers
   * to request a fresh snapshot, never publish an older credential state. */
  [StrappyAsync postNotificationName:StrappyCredentialStatusDidChangeNotification object:identifier];
  [StrappyAsync postNotificationName:StrappyProviderAccountsDidChangeNotification object:nil];
  [pool drain];
}
+ (NSDictionary *)credentialStatusForProviderAccountIdentifier:(NSString *)identifier error:(NSError **)error
{
  NSDictionary *entry, *request = nil, *status;
  if (error) *error = nil;
  if (![identifier length]) return nil;
  @synchronized(self) {
    if (!StrappyCredentialCache) {
      StrappyCredentialCache = [[NSMutableDictionary alloc] init];
      StrappyCredentialPending = [[NSMutableDictionary alloc] init];
    }
    entry = [StrappyCredentialCache objectForKey:identifier];
    status = [[[entry objectForKey:@"status"] retain] autorelease];
    if ((!entry || -[[entry objectForKey:@"date"] timeIntervalSinceNow] > 60.0) &&
        ![StrappyCredentialPending objectForKey:identifier]) {
      request = [NSDictionary dictionaryWithObjectsAndKeys:identifier,@"id",
        [NSNumber XP_numberWithUnsignedInteger:StrappyCredentialGeneration],@"generation",nil];
      [StrappyCredentialPending setObject:request forKey:identifier];
    }
  }
  if (request) [NSThread detachNewThreadSelector:@selector(fetchCredentialStatus:) toTarget:self withObject:request];
  return status;
}

+ (BOOL)updateProviderAccountIdentifier:(NSString *)providerAccountIdentifier
                            displayName:(NSString *)displayName
                      responsesEndpoint:(NSString *)responsesEndpoint
                        maxOutputTokens:(long long)maxOutputTokens
                            bearerToken:(NSString *)bearerToken
                                  error:(NSError **)error
{
  if (bearerToken == nil) return [self updateProviderAccountIdentifier:providerAccountIdentifier
    displayName:displayName responsesEndpoint:responsesEndpoint maxOutputTokens:maxOutputTokens error:error];

  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"update_account",@"action",
    StrappyText(providerAccountIdentifier),@"id",
    StrappyText(displayName),@"name",
    StrappyText(responsesEndpoint),@"endpoint",
    StrappyNumber(maxOutputTokens),@"output",
    StrappyText(bearerToken),@"token",
    nil],error) != nil;
}

+ (BOOL)archiveProviderAccountIdentifier:(NSString *)providerAccountIdentifier
                                    error:(NSError **)error
{
  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"archive_account",@"action",
    StrappyText(providerAccountIdentifier),@"id",
    nil],error) != nil;
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

+ (NSDictionary *)buildList:(NSDictionary *)request
{
  if ([[request objectForKey:@"kind"] isEqual:@"sidebar"]) {
    NSError *error = nil;
    StrappySessionRows *sessions = [self sidebarRowsWithError:&error];
    NSArray *sections = sessions ? [StrappySessionSections sectionsForSessions:sessions] : nil;
    if (sections) return [NSDictionary dictionaryWithObjectsAndKeys:
        sessions,@"sessions",sections,@"sections",nil];
    return [NSDictionary dictionaryWithObject:error ?: [sessions readError] ?:
        [NSError errorWithDomain:@"StrappySidebar" code:1 userInfo:nil] forKey:@"error"];
  }
  NSError *error = nil;
  NSDictionary *result = [self modelPreferenceSnapshotMatchingSearch:
    [request objectForKey:@"search"] sortDescriptors:[request objectForKey:@"sort"] error:&error];
  if (result && [request objectForKey:@"selection"]) {
    NSMutableDictionary *selected = [[result mutableCopy] autorelease];
    NSUInteger index = [[result objectForKey:@"rows"] indexForModelIdentifier:[request objectForKey:@"selection"]];
    [selected setObject:[NSNumber numberWithUnsignedLong:index] forKey:@"selectionIndex"];
    return selected;
  }
  return result ? result : [NSDictionary dictionaryWithObject:
    error ? error : [NSError errorWithDomain:@"Strappy.ModelList" code:1 userInfo:nil] forKey:@"error"];
}

+ (NSDictionary *)modelPreferenceSnapshotMatchingSearch:(NSString *)search
  sortDescriptors:(NSArray *)descriptors error:(NSError **)error
{
  StrappyModelRows *rows = [self modelPreferenceRowsMatchingSearch:search sortDescriptors:descriptors error:error];
  NSArray *sections = rows ? [rows providerSectionsWithError:error] : nil;
  if (!sections || [[AICollectionController currentController] isCancelled]) return nil;
  NSArray *ids = [rows collectionIdentifiersWithError:error];
  if (error && *error) return nil;
  NSMutableDictionary *flat = [NSMutableDictionary dictionaryWithObjectsAndKeys:
    @"models",@"id",rows,@"rows",nil];
  if (ids) [flat setObject:ids forKey:@"identifiers"];
  AICollectionSnapshot *flatSnapshot = [[[AICollectionSnapshot alloc] initWithSections:
    [NSArray arrayWithObject:flat]] autorelease];
  NSMutableArray *groups = [NSMutableArray array];
  NSEnumerator *e = [sections objectEnumerator]; NSDictionary *section;
  while ((section = [e nextObject])) {
    NSMutableDictionary *group = [[section mutableCopy] autorelease];
    [group setObject:[section objectForKey:@"provider_id"] forKey:@"id"];
    [group setObject:rows forKey:@"rows"];
    NSUInteger offset = [[section objectForKey:@"offset"] unsignedLongValue];
    NSUInteger count = [[section objectForKey:@"count"] unsignedLongValue];
    if (ids) [group setObject:[ids subarrayWithRange:NSMakeRange(offset,count)] forKey:@"identifiers"];
    [groups addObject:group];
  }
  AICollectionSnapshot *snapshot = [[[AICollectionSnapshot alloc] initWithSections:groups] autorelease];
  if ([rows count]) (void)[rows objectAtIndex:0];
  if ([rows readError]) { if (error) *error = [rows readError]; return nil; }
  return [NSDictionary dictionaryWithObjectsAndKeys:rows,@"rows",sections,@"sections",
    snapshot,@"collection",flatSnapshot,@"flatCollection",nil];
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
  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:@"default_model",@"action",StrappyText(modelIdentifier),@"id",nil],error) != nil;
}

+ (NSString *)defaultOpenRouterModelIdentifierWithError:(NSError **)error
{
  return [StrappySession defaultModelIdentifierWithError:error];
}

+ (BOOL)setDefaultOpenRouterModelIdentifier:(NSString *)modelIdentifier
                                      error:(NSError **)error
{
  return [self setDefaultModelIdentifier:modelIdentifier error:error];
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
  NSDictionary *command = StrappyOptionsCommand(options,changedFields,nil,error);
  return command && StrappyServiceCommand(STRAPPY_IPC_MUTATE,command,error) != nil;
}

+ (BOOL)setModelsAllowed:(BOOL)allowed identifiers:(NSArray *)identifiers error:(NSError **)error
{
  NSEnumerator *enumerator = [identifiers objectEnumerator];
  NSString *identifier;
  while ((identifier = [enumerator nextObject]) != nil)
    if (![self setModelAllowed:allowed forModelIdentifier:identifier error:error]) return NO;
  return YES;
}

+ (BOOL)setModelAllowed:(BOOL)allowed
     forModelIdentifier:(NSString *)modelIdentifier
                  error:(NSError **)error
{
  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"approve_model",@"action",
    StrappyText(modelIdentifier),@"id",
    StrappyNumber(allowed),@"value",
    nil],error) != nil;
}

+ (BOOL)setOpenRouterModelAllowed:(BOOL)allowed
                forModelIdentifier:(NSString *)modelIdentifier
                             error:(NSError **)error
{
  return [StrappySession setModelAllowed:allowed
                      forModelIdentifier:modelIdentifier
                                   error:error];
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
  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"create_model",@"action",
    StrappyText(providerIdentifier),@"id",
    StrappyText(wireModelID),@"wire",
    StrappyText(displayName),@"name",
    StrappyNumber(contextWindowTokens),@"context",
    StrappyNumber(maxOutputTokens),@"output",
    StrappyNumber(reasoningEnabled),@"reasoning",
    StrappyNumber(imageInputEnabled),@"image",
    StrappyNumber(localFunctionsEnabled),@"functions",
    StrappyText(inputPricePerToken),@"price_input",
    StrappyText(outputPricePerToken),@"price_output",
    StrappyText(cacheReadPricePerToken),@"price_read",
    StrappyText(cacheWritePricePerToken),@"price_write",
    nil],error);
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
  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"update_model",@"action",
    StrappyText(providerIdentifier),@"id",
    StrappyText(wireModelID),@"wire",
    StrappyText(displayName),@"name",
    StrappyNumber(contextWindowTokens),@"context",
    StrappyNumber(maxOutputTokens),@"output",
    StrappyNumber(reasoningEnabled),@"reasoning",
    StrappyNumber(imageInputEnabled),@"image",
    StrappyNumber(localFunctionsEnabled),@"functions",
    StrappyText(inputPricePerToken),@"price_input",
    StrappyText(outputPricePerToken),@"price_output",
    StrappyText(cacheReadPricePerToken),@"price_read",
    StrappyText(cacheWritePricePerToken),@"price_write",
    nil],error) != nil;
}

+ (BOOL)archiveManualModelForProviderIdentifier:
            (NSString *)providerIdentifier
                                             wireModelID:
            (NSString *)wireModelID
                                                    error:
            (NSError **)error
{
  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"archive_model",@"action",
    StrappyText(providerIdentifier),@"id",
    StrappyText(wireModelID),@"wire",
    nil],error) != nil;
}

+ (BOOL)beginOpenRouterModelCatalogRefreshWithError:(NSError **)error
{
  char key[65];
  NSAssert([NSThread XP_isMainThread], @"Catalog submission requires the main thread");
  if (StrappySessionModelCatalogRefreshPending || !strappy_store_client_request_key(key)) {
    if (error) *error = [self errorFromCString:"A model refresh is pending or could not be prepared."];
    return NO;
  }
  NSEnumerator *accounts = [[self providerAccountCatalogWithError:error] objectEnumerator];
  NSDictionary *account;
  NSString *identifier = nil;
  while ((account = [accounts nextObject]) != nil) {
    if ([[account objectForKey:@"provider_id"] isEqualToString:@"openrouter"] &&
        [[account objectForKey:@"available"] boolValue]) { identifier = [account objectForKey:@"id"]; break; }
  }
  if (!identifier) {
    if (error) *error = [self errorFromCString:"Add an OpenRouter account and API key before fetching models."];
    return NO;
  }
  NSString *jobKey = [NSString stringWithUTF8String:key];
  if (StrappyServiceCommand(STRAPPY_IPC_REFRESH_CATALOG,
      [NSDictionary dictionaryWithObjectsAndKeys:identifier,@"id",jobKey,@"key",nil],error) == nil) return NO;
  StrappySessionModelCatalogRefreshPending = YES;
  @synchronized(self) { StrappySessionModelCatalogRefreshInFlight = YES; }
  [StrappyAsync postNotificationName:StrappySessionModelCatalogRefreshDidStartNotification object:self];
  [NSThread detachNewThreadSelector:@selector(waitForModelCatalogRefresh:) toTarget:self withObject:jobKey];
  return YES;
}

+ (void)waitForModelCatalogRefresh:(NSString *)key;
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  unsigned int state = strappy_store_client_wait_maintenance([key UTF8String]);
  NSDictionary *result = state == STRAPPY_RUN_COMPLETED ? [NSDictionary dictionary] :
    [NSDictionary dictionaryWithObject:state == STRAPPY_RUN_FAILED ?
      @"Model refresh failed. Check the provider account and try again." :
      @"The model refresh outcome is unknown because the service became unavailable. Reload stored state before retrying."
      forKey:@"error"];
  [self performSelectorOnMainThread:@selector(modelCatalogJobDidFinish:) withObject:result waitUntilDone:NO];
  [pool release];
}

+ (void)modelCatalogJobDidFinish:(NSDictionary *)result;
{
  StrappySessionModelCatalogRefreshPending = NO;
  [self openRouterModelCatalogRefreshDidFinish:result];
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
    [StrappyAsync postNotificationName:StrappySessionModelCatalogDidChangeNotification
                    object:self
                  userInfo:userInfo];
  }

  [StrappyAsync postNotificationName:StrappySessionModelCatalogRefreshDidFinishNotification
                  object:self
                userInfo:userInfo];
  NSString *message = [userInfo objectForKey:@"error"];
  if ([message isKindOfClass:[NSString class]]) [StrappyAsync reportError:
    [NSError errorWithDomain:@"StrappyServiceErrorDomain" code:1 userInfo:
      [NSDictionary dictionaryWithObject:message forKey:NSLocalizedDescriptionKey]]
    title:NSLocalizedString(@"Fetch models",nil)];
  [userInfo release];
}

+ (StrappySession *)createSessionWithError:(NSError **)error
{
  NSString *identifier = StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"create_session",@"action",
    StrappyWorkingDirectory(),@"directory",
    nil],error);
  return identifier ? [self sessionWithIdentifier:[NSNumber numberWithLongLong:[identifier longLongValue]]] : nil;
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
  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:@"delete_study",@"action",StrappyText(databaseIdentifier),@"id",nil],error) != nil;
}

+ (BOOL)resetDatabaseStudyWithError:(NSError **)error
{
  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:@"reset_study",@"action",nil],error) != nil;
}

+ (StrappySession *)beginDatabaseStudyWithError:(NSError **)error
{
  char key[65];
  NSString *identifier;
  if (!strappy_store_client_request_key(key)) return nil;
  identifier = StrappyServiceCommand(STRAPPY_IPC_START_STUDY,
    [NSDictionary dictionaryWithObjectsAndKeys:StrappyWorkingDirectory(),@"directory",
      [NSString stringWithUTF8String:key],@"key",nil],error);
  return identifier ? [self sessionWithIdentifier:[NSNumber numberWithLongLong:[identifier longLongValue]]] : nil;
}

+ (BOOL)deleteSessionWithIdentifier:(NSNumber *)sessionIdentifier
                               error:(NSError **)error
{
  if (StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"delete_session",@"action",
    StrappyNumber([sessionIdentifier longLongValue]),@"session",
    nil],error) == nil) return NO;
  /* SQLite can reuse a deleted session ID. Discard its local edits and retry
   * identity only after the service confirms deletion; a failed command must
   * leave the existing conversation's state intact. */
  @synchronized(self) {
    [StrappyOptionDrafts removeObjectForKey:sessionIdentifier];
  }
  @synchronized([NSUserDefaults standardUserDefaults]) {
    [[NSUserDefaults standardUserDefaults] removeObjectForKey:
      [NSString stringWithFormat:@"StrappyPendingPrompt-%@", sessionIdentifier]];
    [[NSUserDefaults standardUserDefaults] synchronize];
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

- (NSString *)webViewRefreshMessagesJavaScriptWithError:(NSError **)error
{
  return [[self webViewRefreshMessagesAfterRevision:[NSNumber numberWithUnsignedLongLong:0]
    error:error] objectForKey:@"javaScript"];
}

- (NSDictionary *)webViewRefreshMessagesAfterRevision:(NSNumber *)revision error:(NSError **)error
{
  char *script;
  char *storedError = NULL;
  NSString *messages;
  NSString *status;
  long long requests[STRAPPY_STORE_MAX_CHANGES], minimum = 0;
  uint64_t next;
  int i, count = strappy_store_client_changes([sessionIdentifier_ longLongValue],
    [revision unsignedLongLongValue], &next, requests);
  for (i = 0; i < count; ++i) if (!minimum || requests[i] < minimum) minimum = requests[i];
  if (count < 0) script = strappy_session_webview_refresh_messages_js_for_session(
    [[StrappySession sessionsDatabasePath] fileSystemRepresentation],
    [sessionIdentifier_ longLongValue],
    [[[NSBundle mainBundle] resourcePath] fileSystemRepresentation], &storedError);
  else if (minimum) script = strappy_session_webview_refresh_from_request_js_for_session(
    [[StrappySession sessionsDatabasePath] fileSystemRepresentation],
    [sessionIdentifier_ longLongValue],
    [[[NSBundle mainBundle] resourcePath] fileSystemRepresentation], minimum, &storedError);
  else script = strappy_string_duplicate("");
  if (script == NULL && error != NULL) *error = [StrappySession errorFromCString:storedError];
  strappy_session_free_string(storedError);
  if (script == NULL) return nil;
  messages = StrappySessionStringFromCString(script);
  script = strappy_session_webview_set_processing_status_js(
    StrappySessionOptionalCString([self currentProcessingStatusJSON]));
  if (script == NULL) return nil;
  status = StrappySessionStringFromCString(script);
  return [NSDictionary dictionaryWithObjectsAndKeys:
    [StrappySession webViewBatchedJavaScriptForJavaScript:[messages stringByAppendingString:status]], @"javaScript",
    [NSNumber numberWithUnsignedLongLong:next], @"revision", nil];
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
  return StrappyServiceCommand(STRAPPY_IPC_MUTATE,[NSDictionary dictionaryWithObjectsAndKeys:
    @"include_context",@"action",
    StrappyNumber([sessionIdentifier_ longLongValue]),@"session",
    StrappyNumber([modelRequestIdentifier longLongValue]),@"id",
    StrappyNumber(includedInContext),@"value",
    nil],error) != nil;
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
  NSDictionary *draft = StrappyOptionDraft(sessionIdentifier_);
  if (draft != nil) return [[[draft objectForKey:@"options"] copy] autorelease];

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
  strappy_session_options record;
  StrappySessionOptions *merged;
  NSDictionary *previous;
  if (changedFields == 0 || (changedFields & ~(StrappySessionOptionMask)StrappySessionOptionAll) ||
      [self isPromptInFlight]) {
    if (error) *error = [StrappySession errorFromCString:"Session options cannot be edited during a prompt."];
    return NO;
  }
  if ([[NSUserDefaults standardUserDefaults] objectForKey:
      [NSString stringWithFormat:@"StrappyPendingPrompt-%@", sessionIdentifier_]] != nil) {
    if (error) *error = [StrappySession errorFromCString:"Retry the pending prompt before changing its options."];
    return NO;
  }
  if (!StrappySessionRecordFromOptions(options, &record, error)) return NO;
  merged = [[self optionsWithError:error] copy];
  if (!merged) return NO;
  StrappyMergeOptions(merged, options, changedFields);
  @synchronized([StrappySession class]) {
    if (!StrappyOptionDrafts) StrappyOptionDrafts = [[NSMutableDictionary alloc] init];
    if (!StrappyOptionDraftProcess) StrappyOptionDraftProcess = [[[NSProcessInfo processInfo] globallyUniqueString] copy];
    previous = [StrappyOptionDrafts objectForKey:sessionIdentifier_];
    changedFields |= [[previous objectForKey:@"mask"] unsignedIntValue];
    [StrappyOptionDrafts setObject:[NSDictionary dictionaryWithObjectsAndKeys:
      merged, @"options", [NSNumber XP_numberWithUnsignedInteger:changedFields], @"mask",
      [NSString stringWithFormat:@"%@:%llu", StrappyOptionDraftProcess, ++StrappyOptionDraftGeneration], @"generation", nil]
      forKey:sessionIdentifier_];
  }
  [merged release];
  return YES;
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
  if ([data length] == 0 || [data length] > STRAPPY_IPC_MAX_PROMPT_BYTES || [self isPromptInFlight]) {
    if (error) *error = [StrappySession errorFromCString:"Prompt is empty, exceeds 1 MiB of UTF-8 text, or a prompt is active."];
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
  request = pending ? [[pending mutableCopy] autorelease] :
    [NSMutableDictionary dictionaryWithObjectsAndKeys:prompt, @"prompt",
      [NSString stringWithUTF8String:key], @"key", pendingKey, @"pendingKey", nil];
  if (!pending) {
    NSDictionary *draft = StrappyOptionDraft(sessionIdentifier_);
    if (draft) {
      NSDictionary *command = StrappyOptionsCommand([draft objectForKey:@"options"],
        [[draft objectForKey:@"mask"] unsignedIntValue], sessionIdentifier_, error);
      if (!command) return NO;
      [request setObject:command forKey:@"optionsCommand"];
      [request setObject:[draft objectForKey:@"generation"] forKey:@"optionsGeneration"];
    }
  }
  @synchronized([StrappySession class]) {
    if (!StrappyPendingSubmissions) StrappyPendingSubmissions = [[NSMutableSet alloc] init];
    if ([StrappyPendingSubmissions containsObject:sessionIdentifier_]) return NO;
    [StrappyPendingSubmissions addObject:sessionIdentifier_];
  }
  @synchronized(self) { promptSubmissionPending_ = YES; promptInFlight_ = YES; }
  return [self submitPromptRequest:request error:error];
}

- (BOOL)submitPromptRequest:(NSDictionary *)request error:(NSError **)error
{
  unsigned char *payload = NULL;
  NSData *prompt = [[request objectForKey:@"prompt"] dataUsingEncoding:NSUTF8StringEncoding];
  uint64_t session = (uint64_t)[sessionIdentifier_ longLongValue];
  unsigned int status = STRAPPY_IPC_NOT_READY;
  BOOL received;
  NSMutableDictionary *savedRequest = [[request mutableCopy] autorelease];
  NSString *preflightError = nil;
  char *settingsJSON = NULL;
  unsigned int settingsLength = 0;
  NSDictionary *settings = [[request objectForKey:@"optionsFlushed"] boolValue] ? nil : [request objectForKey:@"optionsCommand"];
  if (settings) {
    cJSON *json = cJSON_CreateObject();
    NSEnumerator *keys = [settings keyEnumerator];
    NSString *key;
    while ((key = [keys nextObject]) != nil)
      cJSON_AddStringToObject(json,[key UTF8String],[[[settings objectForKey:key] description] UTF8String]);
    settingsJSON = cJSON_PrintUnformatted(json); cJSON_Delete(json);
    if (!settingsJSON) preflightError = @"Could not encode prompt options.";
    else settingsLength = (unsigned int)strlen(settingsJSON);
  }
  if ([prompt length] == 0 || [prompt length] > STRAPPY_IPC_MAX_PROMPT_BYTES)
    preflightError = @"The prompt must contain between 1 byte and 1 MiB of UTF-8 text.";
  if (settingsLength > STRAPPY_IPC_MAX_OPTIONS_BYTES)
    preflightError = @"The session options exceed the service message limit.";
  if (!preflightError) {
    payload = malloc([prompt length] + settingsLength + STRAPPY_IPC_OPTIONS_PREFIX);
    if (!payload) preflightError = @"Could not allocate the prompt submission.";
  }
  if (!preflightError) {
    @synchronized([NSUserDefaults standardUserDefaults]) {
      [[NSUserDefaults standardUserDefaults] setObject:savedRequest forKey:[request objectForKey:@"pendingKey"]];
      if (![[NSUserDefaults standardUserDefaults] synchronize])
        preflightError = @"Could not save the submission retry key. The prompt was not sent.";
    }
  }
  if (preflightError) {
    free(payload); free(settingsJSON);
    /* No submission was attempted. Keep the editable draft, and do not let a
     * reconnect silently retry a failed settings save. */
    @synchronized([NSUserDefaults standardUserDefaults]) {
      [[NSUserDefaults standardUserDefaults] removeObjectForKey:[request objectForKey:@"pendingKey"]];
      [[NSUserDefaults standardUserDefaults] synchronize];
    }
    @synchronized(self) { promptSubmissionPending_ = NO; }
    @synchronized([StrappySession class]) { [StrappyPendingSubmissions removeObject:sessionIdentifier_]; }
    @synchronized(self) { promptInFlight_ = NO; }
    if (error) *error = [StrappySession errorFromCString:[preflightError UTF8String]];
    return NO;
  }
  strappy_ipc_put32(payload, (uint32_t)(session >> 32)); strappy_ipc_put32(payload+4, (uint32_t)session);
  memcpy(payload+8, [[request objectForKey:@"key"] UTF8String], 64);
  strappy_ipc_put32(payload+72, settingsLength);
  if (settingsLength) memcpy(payload+76,settingsJSON,settingsLength);
  memcpy(payload+76+settingsLength,[prompt bytes],[prompt length]);
  free(settingsJSON);
  received = strappy_store_client_command(STRAPPY_IPC_SUBMIT_WITH_OPTIONS, payload,
      (unsigned int)[prompt length] + settingsLength + 76U, &status) ? YES : NO;
  free(payload);
  if (received && status == STRAPPY_IPC_OK) {
    @synchronized([StrappySession class]) {
      NSDictionary *draft = [StrappyOptionDrafts objectForKey:sessionIdentifier_];
      if ([[draft objectForKey:@"generation"] isEqual:[request objectForKey:@"optionsGeneration"]])
        [StrappyOptionDrafts removeObjectForKey:sessionIdentifier_];
    }
    @synchronized(self) { optionsLoaded_ = NO; }
  }
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
  @synchronized([StrappySession class]) { [StrappyPendingSubmissions removeObject:sessionIdentifier_]; }
  if (!received || status != STRAPPY_IPC_OK) {
    NSString *message = !received ? @"Acceptance is unknown. Retry the same prompt to reconnect safely." :
        (status == STRAPPY_IPC_BUSY ? @"This session already has an active prompt, or the service has reached its execution limit." : @"The service could not accept this prompt.");
    @synchronized(self) { promptInFlight_ = NO; }
    if (error) *error = [StrappySession errorFromCString:[message UTF8String]];
    return NO;
  } else {
    [StrappyAsync postNotificationName:StrappySessionPromptDidStartNotification object:self];
    [self performSelectorOnMainThread:@selector(serviceRunChanged:) withObject:nil waitUntilDone:NO];
  }
  return YES;
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

  if (summary != nil) {
    [userInfo setObject:StrappySessionChangeKindActivity
                 forKey:StrappySessionChangeKindKey];
    [StrappyAsync postNotificationName:StrappySessionDidUpdateNotification
                    object:self
                  userInfo:userInfo];
  }

  [StrappyAsync postNotificationName:StrappySessionPromptDidFinishNotification
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
  if (strappy_db_sidebar_count(reader) <= AICollectionIdentityLimit) {
    long long identities[AICollectionIdentityLimit];
    if (!strappy_db_sidebar_ids(reader, identities, AICollectionIdentityLimit, &message)) {
      if (error) *error = [StrappySession errorFromCString:message];
      strappy_free_string(message); [self release]; return nil;
    }
    NSMutableArray *ids = [NSMutableArray array]; NSUInteger index;
    for (index = 0; index < strappy_db_sidebar_count(reader); ++index)
      [ids addObject:[NSNumber numberWithLongLong:identities[index]]];
    collectionIdentifiers_ = [ids copy];
  }
  pages_ = [[NSMutableDictionary alloc] init];
  pageOrder_ = [[NSMutableArray alloc] init];
  promptStates_ = [[NSMutableDictionary alloc] init];
  return self;
}

- (void)dealloc
{
  [collectionIdentifiers_ release];
  strappy_db_sidebar_close((strappy_session_reader *)reader_);
  [pages_ release];
  [pageOrder_ release];
  [promptStates_ release];
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

- (NSArray *)collectionIdentifiers { return collectionIdentifiers_; }
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
  if (collectionIdentifiers_) return [collectionIdentifiers_ indexOfObject:identifier];
  message = NULL;
  if (!strappy_db_sidebar_index((strappy_session_reader *)reader_,
                                [identifier longLongValue], &index, &message)) {
    [self recordReadError:message];
    return NSNotFound;
  }
  return index == (size_t)-1 ? NSNotFound : (NSUInteger)index;
}

- (NSArray *)applyPromptStates:(NSDictionary *)states;
{
  NSMutableArray *changed = [NSMutableArray array];
  NSNumber *identifier;
  NSEnumerator *enumerator = [states keyEnumerator];
  while ((identifier = [enumerator nextObject])) {
    NSUInteger index = [self indexForSessionIdentifier:identifier];
    if (index == NSNotFound) continue;
    NSNumber *state = [states objectForKey:identifier];
    if ([[promptStates_ objectForKey:identifier] isEqual:state]) continue;
    [promptStates_ setObject:state forKey:identifier];
    [changed addObject:identifier];
    NSNumber *key = [NSNumber XP_numberWithUnsignedInteger:
      (index / STRAPPY_SIDEBAR_PAGE_SIZE) * STRAPPY_SIDEBAR_PAGE_SIZE];
    NSArray *page = [pages_ objectForKey:key];
    if (page) {
      NSUInteger row = index % STRAPPY_SIDEBAR_PAGE_SIZE;
      NSMutableDictionary *updated = [NSMutableDictionary
        dictionaryWithDictionary:[page objectAtIndex:row]];
      [updated setObject:state forKey:@"prompt_in_flight"];
      NSMutableArray *replacement = [NSMutableArray arrayWithArray:page];
      [replacement replaceObjectAtIndex:row withObject:updated];
      [pages_ setObject:replacement forKey:key];
    }
  }
  return changed;
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
          [NSNumber numberWithBool:record->has_new_answers], @"has_new_answers",
          ([promptStates_ objectForKey:[NSNumber numberWithLongLong:record->session_id]] ?:
            [NSNumber numberWithBool:record->prompt_in_flight]), @"prompt_in_flight",
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
static void StrappyModelListIdentifier(void *context, const char *identifier)
{ [(NSMutableArray *)context addObject:[NSString stringWithUTF8String:identifier]]; }
@implementation StrappyModelListSource
- (NSArray *)collectionIdentifiersWithError:(NSError **)error
{
  if ([self count] > AICollectionIdentityLimit) return nil;
  NSMutableArray *ids = [NSMutableArray array];
  char *message = NULL;
  if (!strappy_db_model_list_ids(reader_,AICollectionIdentityLimit,
      StrappyModelListIdentifier,ids,&message)) {
    if (error) *error = [StrappySession errorFromCString:message];
    strappy_free_string(message); return nil;
  }
  return ids;
}

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
