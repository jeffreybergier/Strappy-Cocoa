#import "StrappySession.h"

#import "strappy_assistant.h"
#import "strappy_client.h"
#import "strappy_core.h"
#import "strappy_db.h"
#import "strappy_model_catalog.h"
#import "strappy_prompt.h"
#import "strappy_webview.h"
#import "XPFoundation.h"

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

static NSMutableDictionary *StrappySessionInFlightSessions = nil;
static BOOL StrappySessionModelCatalogRefreshInFlight = NO;

typedef struct StrappySessionStreamContext {
  StrappySession *session;
  NSDictionary *context;
} StrappySessionStreamContext;

static const char *StrappySessionCString(NSString *string)
{
  if (![string isKindOfClass:[NSString class]]) {
    return "";
  }
  return [string UTF8String];
}

static NSString *StrappySessionStringFromWebViewCString(char *value)
{
  NSString *string;

  if (value == NULL) {
    return @"";
  }

  string = [NSString stringWithUTF8String:value];
  strappy_webview_free(value);
  return (string != nil) ? string : @"";
}

static strappy_webview_labels StrappySessionWebViewLabels(void)
{
  strappy_webview_labels labels;

  labels.agent = StrappySessionCString(NSLocalizedString(@"Agent", nil));
  labels.you = StrappySessionCString(NSLocalizedString(@"You", nil));
  labels.harness = StrappySessionCString(NSLocalizedString(@"Harness", nil));
  labels.thinking = StrappySessionCString(NSLocalizedString(@"Thinking", nil));
  labels.request_metadata =
    StrappySessionCString(NSLocalizedString(@"Request Metadata", nil));
  labels.tool_call = StrappySessionCString(NSLocalizedString(@"Tool Call", nil));
  labels.tool_result =
    StrappySessionCString(NSLocalizedString(@"Tool Result", nil));
  labels.retry = StrappySessionCString(NSLocalizedString(@"Retry", nil));
  return labels;
}

static void StrappySessionWebViewMessageFromRecord(
  const strappy_session_message_record *record,
  strappy_webview_message *message)
{
  if (message == NULL) {
    return;
  }

  memset(message, 0, sizeof(*message));
  if (record == NULL) {
    return;
  }

  message->message_id = record->message_id;
  message->http_status = record->http_status;
  message->role = record->role;
  message->kind = record->kind;
  message->actor = record->actor;
  message->prompt_group_key = record->prompt_group_key;
  message->message_key = record->message_key;
  message->target_message_key = record->target_message_key;
  message->text = record->content;
  message->reasoning = record->reasoning;
  message->metadata_json = record->metadata_json;
  message->render_state_json = record->render_state_json;
  message->created_at = record->created_at;
  message->is_error = record->is_error ? 1 : 0;
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
+ (void)refreshOpenRouterModelCatalogInBackground:(id)ignored;
+ (void)openRouterModelCatalogRefreshDidFinish:(NSDictionary *)result;
- (void)updateCachedSummary:(NSDictionary *)summary;
- (BOOL)shouldCancelStreamEventOfType:(strappy_chat_stream_event_type)eventType;
- (int)handleStreamEvent:(const strappy_chat_stream_event *)event
                 context:(NSDictionary *)context;
- (void)postStreamEventAndRelease:(NSDictionary *)event;
- (NSDictionary *)submitPrompt:(NSString *)prompt
                         error:(NSError **)error;
- (NSDictionary *)submitPrompt:(NSString *)prompt
                      streaming:(BOOL)streaming
                        context:(NSDictionary *)context
                          error:(NSError **)error;
- (BOOL)beginPrompt:(NSString *)prompt
            context:(NSDictionary *)context
          streaming:(BOOL)streaming
              error:(NSError **)error;
- (void)sendPromptInBackground:(NSDictionary *)request;
- (void)streamingPromptDidFinish:(NSDictionary *)result;
@end

static int StrappySessionHandleStreamEvent(
  const strappy_chat_stream_event *event,
  void *userData)
{
  StrappySessionStreamContext *context;
  StrappySession *session;

  if ((event == NULL) || (userData == NULL)) {
    return 1;
  }

  context = (StrappySessionStreamContext *)userData;
  session = context->session;
  if (session == nil) {
    return 1;
  }

  return [session handleStreamEvent:event context:context->context];
}

static BOOL StrappySessionStreamingEnabledFromSummary(NSDictionary *summary)
{
  NSNumber *streamingEnabled;

  if (![summary isKindOfClass:[NSDictionary class]]) {
    return NO;
  }

  streamingEnabled = [summary objectForKey:@"streaming_enabled"];
  return ([streamingEnabled isKindOfClass:[NSNumber class]] &&
          [streamingEnabled boolValue]) ? YES : NO;
}

@implementation StrappySession

- (int)handleStreamEvent:(const strappy_chat_stream_event *)event
                 context:(NSDictionary *)contextDictionary
{
  NSString *text;
  NSMutableDictionary *delta;

  if (event == NULL) {
    return 1;
  }

  if ([self shouldCancelStreamEventOfType:event->type]) {
    return 0;
  }

  text = nil;
  if (event->text != NULL) {
    text = [NSString stringWithUTF8String:event->text];
  }
  if (text == nil) {
    text = @"";
  }

  delta = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
    text, @"delta",
    nil];
  if (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL) {
    [delta setObject:@"call" forKey:@"event_type"];
  } else if (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_RESULT) {
    [delta setObject:@"result" forKey:@"event_type"];
  } else if (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR) {
    [delta setObject:@"error" forKey:@"event_type"];
  }
  if (contextDictionary != nil) {
    [delta setObject:contextDictionary forKey:@"context"];
  }

  if (([text length] == 0U) &&
      ((event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA) ||
       (event->type == STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA))) {
    [delta release];
    return 1;
  }

  if (event->tool_call_id != NULL) {
    NSString *toolCallId;

    toolCallId = [NSString stringWithUTF8String:event->tool_call_id];
    if (toolCallId != nil) {
      [delta setObject:toolCallId forKey:@"tool_call_id"];
    }
  }
  if (event->tool_name != NULL) {
    NSString *toolName;

    toolName = [NSString stringWithUTF8String:event->tool_name];
    if (toolName != nil) {
      [delta setObject:toolName forKey:@"tool_name"];
    }
  }
  if (event->arguments_json != NULL) {
    NSString *argumentsJSON;

    argumentsJSON = [NSString stringWithUTF8String:event->arguments_json];
    if (argumentsJSON != nil) {
      [delta setObject:argumentsJSON forKey:@"arguments_json"];
    }
  }
  if (event->result_json != NULL) {
    NSString *resultJSON;

    resultJSON = [NSString stringWithUTF8String:event->result_json];
    if (resultJSON != nil) {
      [delta setObject:resultJSON forKey:@"result_json"];
    }
  }
  if (event->message_json != NULL) {
    NSString *messageJSON;

    messageJSON = [NSString stringWithUTF8String:event->message_json];
    if (messageJSON != nil) {
      [delta setObject:messageJSON forKey:@"message_json"];
    }
  }
  if (event->turn_key != NULL) {
    NSString *turnKey;

    turnKey = [NSString stringWithUTF8String:event->turn_key];
    if (turnKey != nil) {
      [delta setObject:turnKey forKey:@"turn_key"];
    }
  }
  if (event->prompt_group_key != NULL) {
    NSString *promptGroupKey;

    promptGroupKey = [NSString stringWithUTF8String:event->prompt_group_key];
    if (promptGroupKey != nil) {
      [delta setObject:promptGroupKey forKey:@"prompt_group_key"];
    }
  }
  if (event->actor != NULL) {
    NSString *actor;

    actor = [NSString stringWithUTF8String:event->actor];
    if (actor != nil) {
      [delta setObject:actor forKey:@"actor"];
    }
  }
  if (event->kind != NULL) {
    NSString *kind;

    kind = [NSString stringWithUTF8String:event->kind];
    if (kind != nil) {
      [delta setObject:kind forKey:@"kind"];
    }
  }
  if (event->message_key != NULL) {
    NSString *messageKey;

    messageKey = [NSString stringWithUTF8String:event->message_key];
    if (messageKey != nil) {
      [delta setObject:messageKey forKey:@"message_key"];
    }
  }
  if (event->target_message_key != NULL) {
    NSString *targetMessageKey;

    targetMessageKey = [NSString stringWithUTF8String:event->target_message_key];
    if (targetMessageKey != nil) {
      [delta setObject:targetMessageKey forKey:@"target_message_key"];
    }
  }
  if (event->render_role != NULL) {
    NSString *renderRole;

    renderRole = [NSString stringWithUTF8String:event->render_role];
    if (renderRole != nil) {
      [delta setObject:renderRole forKey:@"render_role"];
    }
  }
  if (event->api_role != NULL) {
    NSString *apiRole;

    apiRole = [NSString stringWithUTF8String:event->api_role];
    if (apiRole != nil) {
      [delta setObject:apiRole forKey:@"api_role"];
    }
  }

  if (event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA) {
    [delta setObject:@"content_delta" forKey:@"stream_event"];
  } else if (event->type == STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA) {
    [delta setObject:@"reasoning_delta" forKey:@"stream_event"];
  } else if (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL) {
    [delta setObject:@"tool_call" forKey:@"stream_event"];
  } else if (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_RESULT) {
    [delta setObject:@"tool_result" forKey:@"stream_event"];
  } else if (event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR) {
    [delta setObject:@"tool_error" forKey:@"stream_event"];
  } else if (event->type == STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED) {
    [delta setObject:@"turn_started" forKey:@"stream_event"];
  } else if (event->type == STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED) {
    [delta setObject:@"turn_finished" forKey:@"stream_event"];
  } else if (event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_RETRACTED) {
    [delta setObject:@"content_retracted" forKey:@"stream_event"];
  }

  [self performSelectorOnMainThread:@selector(postStreamEventAndRelease:)
                         withObject:delta
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
    if ([summary isKindOfClass:[NSDictionary class]]) {
      cachedSummary_ = [summary retain];
      streamingEnabled_ = StrappySessionStreamingEnabledFromSummary(summary);
    }
  }
  return self;
}

- (void)dealloc
{
  [StrappySession unregisterInFlightSession:self];
  [sessionIdentifier_ release];
  [cachedSummary_ release];
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
  if (![summary isKindOfClass:[NSDictionary class]]) {
    return;
  }

  @synchronized(self) {
    if (cachedSummary_ != summary) {
      [cachedSummary_ release];
      cachedSummary_ = [summary retain];
    }
    streamingEnabled_ = StrappySessionStreamingEnabledFromSummary(summary);
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

- (BOOL)shouldCancelStreamEventOfType:(strappy_chat_stream_event_type)eventType
{
  if ((eventType != STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA) &&
      (eventType != STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA)) {
    return NO;
  }

  return [self promptCancellationRequested];
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

  if (![caCertPath isKindOfClass:[NSString class]] || ([caCertPath length] == 0U)) {
    [NSException raise:NSInvalidArgumentException
                format:@"[StrappySession bootstrapProcessWithCACertPath:] caCertPath is required"];
  }

  strappyError = NULL;
  if (!strappy_client_set_cainfo([caCertPath fileSystemRepresentation],
                                 &strappyError)) {
    NSString *message = nil;
    if (strappyError != NULL) {
      message = [NSString stringWithUTF8String:strappyError];
    }
    strappy_free_string(strappyError);
    [NSException raise:NSInvalidArgumentException
                format:@"%@", (message ? message : @"Could not configure CA certificate path.")];
  }

  fontsPath = [[[NSBundle mainBundle] resourcePath]
    stringByAppendingPathComponent:@"Fonts"];
  strappy_webview_set_font_dir([fontsPath fileSystemRepresentation]);
}

+ (NSString *)systemPromptTemplatePathWithError:(NSError **)error
{
  NSString *resourceName;
  NSString *resourceType;
  NSString *path;

  resourceName =
    [NSString stringWithUTF8String:STRAPPY_PROMPT_TEMPLATE_RESOURCE_NAME];
  resourceType =
    [NSString stringWithUTF8String:STRAPPY_PROMPT_TEMPLATE_RESOURCE_TYPE];
  path = [[NSBundle mainBundle] pathForResource:resourceName
                                         ofType:resourceType];
  if ([path isKindOfClass:[NSString class]] && ([path length] > 0U)) {
    return path;
  }

  if (error != nil) {
    NSDictionary *userInfo =
      [NSDictionary dictionaryWithObject:NSLocalizedString(@"System prompt template is missing from the app bundle.", nil)
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
  NSNumber *streamingEnabled;
  NSString *name;
  NSString *prompt;
  NSString *response;
  NSString *model;
  NSString *createdAt;

  if (record == NULL) {
    return nil;
  }

  sessionId = [NSNumber numberWithLongLong:record->session_id];
  httpStatus = [NSNumber numberWithLong:record->http_status];
  streamingEnabled =
    [NSNumber numberWithBool:(record->streaming_enabled ? YES : NO)];
  name = [StrappySession stringFromCStringOrEmpty:record->name];
  prompt = [StrappySession stringFromCStringOrEmpty:record->prompt];
  response = [StrappySession stringFromCStringOrEmpty:record->response];
  model = [StrappySession stringFromCStringOrEmpty:record->model];
  createdAt = [StrappySession stringFromCStringOrEmpty:record->created_at];

  return [NSDictionary dictionaryWithObjectsAndKeys:
    sessionId, @"id",
    name, @"name",
    prompt, @"prompt",
    response, @"response",
    model, @"model",
    httpStatus, @"http_status",
    streamingEnabled, @"streaming_enabled",
    createdAt, @"created_at",
    nil];
}

+ (NSDictionary *)dictionaryFromSessionMessageRecord:
    (const strappy_session_message_record *)record
{
  NSNumber *messageId;
  NSNumber *sessionId;
  NSNumber *turnId;
  NSNumber *httpStatus;
  NSNumber *includeInContext;
  NSNumber *isError;
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
  NSString *toolCallId;
  NSString *toolName;
  NSString *argumentsJSON;
  NSString *resultJSON;
  NSString *createdAt;

  if (record == NULL) {
    return nil;
  }

  messageId = [NSNumber numberWithLongLong:record->message_id];
  sessionId = [NSNumber numberWithLongLong:record->session_id];
  turnId = [NSNumber numberWithLongLong:record->turn_id];
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
  toolCallId = [StrappySession stringFromCStringOrEmpty:record->tool_call_id];
  toolName = [StrappySession stringFromCStringOrEmpty:record->tool_name];
  argumentsJSON = [StrappySession stringFromCStringOrEmpty:record->arguments_json];
  resultJSON = [StrappySession stringFromCStringOrEmpty:record->result_json];
  createdAt = [StrappySession stringFromCStringOrEmpty:record->created_at];

  return [NSDictionary dictionaryWithObjectsAndKeys:
    messageId, @"id",
    sessionId, @"session_id",
    turnId, @"turn_id",
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
    toolCallId, @"tool_call_id",
    toolName, @"tool_name",
    argumentsJSON, @"arguments_json",
    resultJSON, @"result_json",
    includeInContext, @"include_in_context",
    isError, @"is_error",
    httpStatus, @"http_status",
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

+ (NSDictionary *)enrichedSummaryFromSession:(NSDictionary *)session
                                    messages:(NSArray *)messages
{
  NSMutableDictionary *summary;
  NSDictionary *lastMessage;
  NSString *lastText;
  NSString *lastRole;
  NSString *lastCreatedAt;
  NSNumber *httpStatus;
  NSUInteger index;
  NSUInteger assistantCount;
  NSUInteger userCount;
  BOOL hasError;

  if (![session isKindOfClass:[NSDictionary class]]) {
    return nil;
  }

  summary = [NSMutableDictionary dictionaryWithDictionary:session];
  lastMessage = nil;
  assistantCount = 0U;
  userCount = 0U;
  hasError = NO;

  httpStatus = [session objectForKey:@"http_status"];
  if ([httpStatus isKindOfClass:[NSNumber class]] &&
      ([httpStatus longValue] >= 400L)) {
    hasError = YES;
  }

  for (index = 0U; index < [messages count]; index++) {
    NSDictionary *message;
    NSString *role;
    NSNumber *messageStatus;

    message = [messages objectAtIndex:index];
    if (![message isKindOfClass:[NSDictionary class]]) {
      continue;
    }

    role = [message objectForKey:@"role"];
    if ([role isEqualToString:@"assistant"]) {
      assistantCount++;
    } else if ([role isEqualToString:@"user"]) {
      userCount++;
    }

    messageStatus = [message objectForKey:@"http_status"];
    if ([messageStatus isKindOfClass:[NSNumber class]] &&
        ([messageStatus longValue] >= 400L)) {
      hasError = YES;
    }

    lastMessage = message;
  }

  if (lastMessage != nil) {
    lastText = [lastMessage objectForKey:@"text"];
    lastRole = [lastMessage objectForKey:@"role"];
    lastCreatedAt = [lastMessage objectForKey:@"created_at"];
  } else {
    lastText = [session objectForKey:@"prompt"];
    lastRole = @"user";
    lastCreatedAt = [session objectForKey:@"created_at"];
  }

  if (![lastText isKindOfClass:[NSString class]]) {
    lastText = @"";
  }
  if (![lastRole isKindOfClass:[NSString class]]) {
    lastRole = @"assistant";
  }
  if (![lastCreatedAt isKindOfClass:[NSString class]]) {
    lastCreatedAt = @"";
  }

  [summary setObject:lastText forKey:@"last_message_text"];
  [summary setObject:lastRole forKey:@"last_message_role"];
  [summary setObject:lastCreatedAt forKey:@"last_message_at"];
  [summary setObject:[NSNumber XP_numberWithUnsignedInteger:[messages count]]
              forKey:@"message_count"];
  [summary setObject:[NSNumber XP_numberWithUnsignedInteger:userCount]
              forKey:@"user_message_count"];
  [summary setObject:[NSNumber XP_numberWithUnsignedInteger:assistantCount]
              forKey:@"assistant_message_count"];
  [summary setObject:(hasError ? @"error" : @"ready") forKey:@"state"];

  return summary;
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
  ok = strappy_db_initialize([databasePath UTF8String], &strappyError);
  if (!ok) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
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
  if (!strappy_db_list_openrouter_models_matching([databasePath UTF8String],
                                                  searchCString,
                                                  &list,
                                                  &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
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
  if (!strappy_db_list_allowed_openrouter_models([databasePath UTF8String],
                                                 &list,
                                                 &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    strappy_openrouter_model_record_list_destroy(&list);
    return nil;
  }

  models = [StrappySession openRouterModelCatalogFromList:&list];
  strappy_openrouter_model_record_list_destroy(&list);
  return models;
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
  if (!strappy_db_get_default_openrouter_model([databasePath UTF8String],
                                               &modelId,
                                               &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return nil;
  }

  result = nil;
  if (modelId != NULL) {
    result = [NSString stringWithUTF8String:modelId];
  }
  strappy_free_string(modelId);
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
  ok = strappy_db_set_default_openrouter_model([databasePath UTF8String],
                                               [modelIdentifier UTF8String],
                                               &strappyError);
  if (!ok) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
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
  ok = strappy_db_set_openrouter_model_allowed([databasePath UTF8String],
                                               [modelIdentifier UTF8String],
                                               allowed ? 1 : 0,
                                               &strappyError);
  if (!ok) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
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
  NSMutableDictionary *result;
  char *strappyError;
  int ok;

  (void)ignored;
  pool = [[NSAutoreleasePool alloc] init];
  databasePath = [StrappySession sessionsDatabasePath];
  result = [[NSMutableDictionary alloc] init];

  strappyError = NULL;
  ok = strappy_model_catalog_refresh_openrouter_user_models(
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

    models = [StrappySession openRouterModelCatalogWithError:nil];
    if (models != nil) {
      [result setObject:[NSNumber XP_numberWithUnsignedInteger:[models count]]
                 forKey:@"model_count"];
    }
  }
  strappy_free_string(strappyError);

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
  char *strappyError;
  long long sessionId;
  NSNumber *sessionIdentifier;
  NSDictionary *summary;

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  sessionId = 0;
  strappyError = NULL;
  if (!strappy_db_create_session([databasePath UTF8String],
                                 &sessionId,
                                 &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
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
  if (!strappy_db_list_sessions([databasePath UTF8String], &list, &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    strappy_session_record_list_destroy(&list);
    return nil;
  }

  sessions = [NSMutableArray arrayWithCapacity:list.count];
  for (index = 0U; index < list.count; index++) {
    NSDictionary *session =
      [StrappySession dictionaryFromSessionRecord:&list.records[index]];
    if (session != nil) {
      NSError *messagesError;
      NSArray *messages;
      NSDictionary *summary;

      messagesError = nil;
      messages = [StrappySession messagesForSessionIdentifier:[session objectForKey:@"id"]
                                                        error:&messagesError];
      if (messages == nil) {
        messages = [NSArray array];
      }

      summary = [StrappySession enrichedSummaryFromSession:session
                                                  messages:messages];
      if (summary != nil) {
        [sessions addObject:summary];
      } else {
        [sessions addObject:session];
      }
    }
  }

  strappy_session_record_list_destroy(&list);
  return sessions;
}

+ (NSDictionary *)sessionSummaryForSessionIdentifier:(NSNumber *)sessionIdentifier
                                               error:(NSError **)error
{
  NSString *databasePath;
  strappy_session_record record;
  char *strappyError;
  long long sessionId;
  NSDictionary *session;
  NSArray *messages;
  NSDictionary *summary;

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
  if (!strappy_db_load_session([databasePath UTF8String],
                               sessionId,
                               &record,
                               &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    strappy_session_record_destroy(&record);
    return nil;
  }

  session = [StrappySession dictionaryFromSessionRecord:&record];
  strappy_session_record_destroy(&record);
  if (session == nil) {
    return nil;
  }

  messages = [StrappySession messagesForSessionIdentifier:sessionIdentifier
                                                    error:error];
  if (messages == nil) {
    return nil;
  }

  summary = [StrappySession enrichedSummaryFromSession:session
                                              messages:messages];
  return summary;
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
  if (!strappy_db_list_session_messages([databasePath UTF8String],
                                        sessionId,
                                        &list,
                                        &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
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

- (NSString *)webViewJavaScriptForStreamEvent:(NSDictionary *)event
                                        error:(NSError **)error
{
  NSString *databasePath;
  NSString *messageKey;
  NSString *streamEvent;
  NSString *delta;
  char *strappyError;
  char *js;
  long long sessionId;
  strappy_session_message_record record;
  strappy_webview_message message;
  strappy_webview_labels labels;

  if (![event isKindOfClass:[NSDictionary class]]) {
    return @"";
  }

  messageKey = [event objectForKey:@"message_key"];
  if (![messageKey isKindOfClass:[NSString class]] ||
      ([messageKey length] == 0U)) {
    return @"";
  }

  streamEvent = [event objectForKey:@"stream_event"];
  delta = [event objectForKey:@"delta"];
  if (![delta isKindOfClass:[NSString class]]) {
    delta = @"";
  }

  if ([streamEvent isEqualToString:@"content_delta"]) {
    return StrappySessionStringFromWebViewCString(
      strappy_webview_append_message_text_by_key_js(
        [messageKey UTF8String],
        [delta UTF8String]));
  }
  if ([streamEvent isEqualToString:@"reasoning_delta"]) {
    return StrappySessionStringFromWebViewCString(
      strappy_webview_append_reasoning_text_by_key_js(
        [messageKey UTF8String],
        [delta UTF8String]));
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

  strappy_session_message_record_init(&record);
  strappyError = NULL;
  if (!strappy_db_load_session_message_by_key([databasePath UTF8String],
                                              sessionId,
                                              [messageKey UTF8String],
                                              &record,
                                              &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    strappy_session_message_record_destroy(&record);
    return @"";
  }

  labels = StrappySessionWebViewLabels();
  StrappySessionWebViewMessageFromRecord(&record, &message);
  js = strappy_webview_message_update_js(&message, &labels);
  strappy_session_message_record_destroy(&record);
  return StrappySessionStringFromWebViewCString(js);
}

- (BOOL)streamingEnabled
{
  BOOL enabled;

  @synchronized(self) {
    enabled = streamingEnabled_;
  }
  return enabled;
}

- (BOOL)setStreamingEnabled:(BOOL)enabled error:(NSError **)error
{
  NSString *databasePath;
  NSNumber *streamingEnabled;
  NSDictionary *notificationSession;
  char *strappyError;
  long long sessionId;

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
    return NO;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return NO;
  }

  strappyError = NULL;
  if (!strappy_db_update_session_streaming_enabled([databasePath UTF8String],
                                                   sessionId,
                                                   enabled ? 1 : 0,
                                                   &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return NO;
  }

  streamingEnabled = [NSNumber numberWithBool:(enabled ? YES : NO)];
  notificationSession = nil;
  @synchronized(self) {
    NSMutableDictionary *summary;

    streamingEnabled_ = enabled ? YES : NO;
    if (cachedSummary_ != nil) {
      summary = [[NSMutableDictionary alloc] initWithDictionary:cachedSummary_];
      [summary setObject:streamingEnabled forKey:@"streaming_enabled"];
      [cachedSummary_ release];
      cachedSummary_ = summary;
      notificationSession = [cachedSummary_ retain];
    } else {
      notificationSession =
        [[NSDictionary alloc] initWithObjectsAndKeys:
          sessionIdentifier_, @"id",
          streamingEnabled, @"streaming_enabled",
          nil];
    }
  }

  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionDidUpdateNotification
                  object:self
                userInfo:[NSDictionary dictionaryWithObject:notificationSession
                                                     forKey:@"session"]];
  [notificationSession release];
  return YES;
}

- (NSString *)selectedOpenRouterModelIdentifierWithError:(NSError **)error
{
  NSString *databasePath;
  char *modelId;
  char *strappyError;
  NSString *result;
  long long sessionId;

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

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  modelId = NULL;
  strappyError = NULL;
  if (!strappy_db_get_session_model([databasePath UTF8String],
                                    sessionId,
                                    &modelId,
                                    &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return nil;
  }

  result = nil;
  if (modelId != NULL) {
    result = [NSString stringWithUTF8String:modelId];
  }
  strappy_free_string(modelId);
  return result;
}

- (BOOL)setSelectedOpenRouterModelIdentifier:(NSString *)modelIdentifier
                                       error:(NSError **)error
{
  NSString *databasePath;
  NSDictionary *notificationSession;
  char *strappyError;
  long long sessionId;

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
    return NO;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return NO;
  }

  strappyError = NULL;
  if (!strappy_db_update_session_model([databasePath UTF8String],
                                       sessionId,
                                       [modelIdentifier UTF8String],
                                       &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return NO;
  }

  notificationSession = nil;
  @synchronized(self) {
    NSMutableDictionary *summary;

    if (cachedSummary_ != nil) {
      summary = [[NSMutableDictionary alloc] initWithDictionary:cachedSummary_];
      [summary setObject:modelIdentifier forKey:@"model"];
      [cachedSummary_ release];
      cachedSummary_ = summary;
      notificationSession = [cachedSummary_ retain];
    } else {
      notificationSession =
        [[NSDictionary alloc] initWithObjectsAndKeys:
          sessionIdentifier_, @"id",
          modelIdentifier, @"model",
          nil];
    }
  }

  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionDidUpdateNotification
                  object:self
                userInfo:[NSDictionary dictionaryWithObject:notificationSession
                                                     forKey:@"session"]];
  [notificationSession release];
  return YES;
}

- (NSDictionary *)submitPrompt:(NSString *)prompt
                         error:(NSError **)error
{
  NSString *databasePath;
  NSString *systemPromptTemplatePath;
  char *response;
  char *strappyError;
  long long sessionId;
  strappy_session_record record;
  NSDictionary *session;

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

  systemPromptTemplatePath = [StrappySession systemPromptTemplatePathWithError:error];
  if (systemPromptTemplatePath == nil) {
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappyError = NULL;
  response =
    strappy_assistant_send_prompt_for_session_and_store([prompt UTF8String],
                                                        NULL,
                                                        [systemPromptTemplatePath fileSystemRepresentation],
                                                        [databasePath UTF8String],
                                                        sessionId,
                                                        &strappyError);
  if (response == NULL) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return nil;
  }
  strappy_free_string(response);

  strappy_session_record_init(&record);
  strappyError = NULL;
  if (!strappy_db_load_session([databasePath UTF8String],
                               sessionId,
                               &record,
                               &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    strappy_session_record_destroy(&record);
    return nil;
  }

  session = [StrappySession dictionaryFromSessionRecord:&record];
  strappy_session_record_destroy(&record);
  return session;
}

- (NSDictionary *)submitPrompt:(NSString *)prompt
                      streaming:(BOOL)streaming
                        context:(NSDictionary *)context
                          error:(NSError **)error
{
  NSString *databasePath;
  NSString *systemPromptTemplatePath;
  char *response;
  char *strappyError;
  long long sessionId;
  strappy_session_record record;
  NSDictionary *session;
  StrappySessionStreamContext streamContext;

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

  systemPromptTemplatePath = [StrappySession systemPromptTemplatePathWithError:error];
  if (systemPromptTemplatePath == nil) {
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  streamContext.session = self;
  streamContext.context = [context retain];

  strappyError = NULL;
  if (streaming) {
    response = strappy_assistant_stream_prompt_for_session_and_store(
      [prompt UTF8String],
      NULL,
      [systemPromptTemplatePath fileSystemRepresentation],
      [databasePath UTF8String],
      sessionId,
      StrappySessionHandleStreamEvent,
      &streamContext,
      &strappyError);
  } else {
    response = strappy_assistant_send_prompt_for_session_and_store_with_events(
      [prompt UTF8String],
      NULL,
      [systemPromptTemplatePath fileSystemRepresentation],
      [databasePath UTF8String],
      sessionId,
      StrappySessionHandleStreamEvent,
      &streamContext,
      &strappyError);
  }

  [streamContext.context release];
  if (response == NULL) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return nil;
  }
  strappy_free_string(response);

  strappy_session_record_init(&record);
  strappyError = NULL;
  if (!strappy_db_load_session([databasePath UTF8String],
                               sessionId,
                               &record,
                               &strappyError)) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    strappy_session_record_destroy(&record);
    return nil;
  }

  session = [StrappySession dictionaryFromSessionRecord:&record];
  strappy_session_record_destroy(&record);
  return session;
}

- (BOOL)beginPrompt:(NSString *)prompt
            context:(NSDictionary *)context
          streaming:(BOOL)streaming
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

    promptInFlight_ = YES;
    promptCancellationRequested_ = NO;
  }

  [StrappySession registerInFlightSession:self];
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappySessionPromptDidStartNotification
                  object:self];

  request = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
    prompt, @"prompt",
    [NSNumber numberWithBool:streaming], @"streaming",
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

- (BOOL)beginStreamingPrompt:(NSString *)prompt
                     context:(NSDictionary *)context
                       error:(NSError **)error
{
  return [self beginPrompt:prompt context:context streaming:YES error:error];
}

- (BOOL)beginNonStreamingPrompt:(NSString *)prompt
                         context:(NSDictionary *)context
                           error:(NSError **)error
{
  return [self beginPrompt:prompt context:context streaming:NO error:error];
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
  NSNumber *streamingValue;
  BOOL shouldStream;

  pool = [[NSAutoreleasePool alloc] init];

  prompt = [request objectForKey:@"prompt"];
  if (![prompt isKindOfClass:[NSString class]]) {
    prompt = @"";
  }
  context = [request objectForKey:@"context"];
  if (![context isKindOfClass:[NSDictionary class]]) {
    context = nil;
  }
  streamingValue = [request objectForKey:@"streaming"];
  shouldStream = (![streamingValue isKindOfClass:[NSNumber class]] ||
                  [streamingValue boolValue]) ? YES : NO;

  result = [[NSMutableDictionary alloc] init];
  if (context != nil) {
    [result addEntriesFromDictionary:context];
  }

  error = nil;
  session = [self submitPrompt:prompt
                     streaming:shouldStream
                       context:context
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

  [self performSelectorOnMainThread:@selector(streamingPromptDidFinish:)
                         withObject:result
                      waitUntilDone:NO];
  [result release];

  [pool release];
}

- (void)streamingPromptDidFinish:(NSDictionary *)result
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
  }
  [StrappySession unregisterInFlightSession:self];

  if (summary != nil) {
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
