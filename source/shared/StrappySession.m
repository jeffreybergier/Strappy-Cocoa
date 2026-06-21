#import "StrappySession.h"

#import "strappy_assistant.h"
#import "strappy_client.h"
#import "strappy_core.h"
#import "strappy_db.h"
#import "strappy_prompt.h"
#import "XPFoundation.h"

typedef struct StrappySessionStreamContext {
  id delegate;
  NSDictionary *context;
} StrappySessionStreamContext;

static int StrappySessionHandleStreamEvent(
  const strappy_chat_stream_event *event,
  void *userData)
{
  StrappySessionStreamContext *context;
  NSString *text;
  NSMutableDictionary *delta;

  if ((event == NULL) || (userData == NULL)) {
    return 1;
  }

  context = (StrappySessionStreamContext *)userData;
  if (context->delegate == nil) {
    return 1;
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
  if (context->context != nil) {
    [delta setObject:context->context forKey:@"context"];
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

  if ((event->type == STRAPPY_CHAT_STREAM_EVENT_CONTENT_DELTA) &&
      [context->delegate respondsToSelector:
        @selector(strappySessionStreamDidReceiveContentDelta:)]) {
    [context->delegate strappySessionStreamDidReceiveContentDelta:delta];
  } else if ((event->type == STRAPPY_CHAT_STREAM_EVENT_REASONING_DELTA) &&
             [context->delegate respondsToSelector:
               @selector(strappySessionStreamDidReceiveReasoningDelta:)]) {
    [context->delegate strappySessionStreamDidReceiveReasoningDelta:delta];
  } else if ((event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_CALL) &&
             [context->delegate respondsToSelector:
               @selector(strappySessionStreamDidReceiveToolCall:)]) {
    [context->delegate strappySessionStreamDidReceiveToolCall:delta];
  } else if ((event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_RESULT) &&
             [context->delegate respondsToSelector:
               @selector(strappySessionStreamDidReceiveToolResult:)]) {
    [context->delegate strappySessionStreamDidReceiveToolResult:delta];
  } else if ((event->type == STRAPPY_CHAT_STREAM_EVENT_TOOL_ERROR) &&
             [context->delegate respondsToSelector:
               @selector(strappySessionStreamDidReceiveToolError:)]) {
    [context->delegate strappySessionStreamDidReceiveToolError:delta];
  } else if ((event->type == STRAPPY_CHAT_STREAM_EVENT_TURN_STARTED) &&
             [context->delegate respondsToSelector:
               @selector(strappySessionStreamDidStartTurn:)]) {
    [context->delegate strappySessionStreamDidStartTurn:delta];
  } else if ((event->type == STRAPPY_CHAT_STREAM_EVENT_TURN_FINISHED) &&
             [context->delegate respondsToSelector:
               @selector(strappySessionStreamDidFinishTurn:)]) {
    [context->delegate strappySessionStreamDidFinishTurn:delta];
  }

  [delta release];
  return 1;
}

@implementation StrappySession

+ (void)bootstrapProcessWithCACertPath:(NSString *)caCertPath
{
  char *strappyError;

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
  NSString *prompt;
  NSString *response;
  NSString *model;
  NSString *createdAt;

  if (record == NULL) {
    return nil;
  }

  sessionId = [NSNumber numberWithLongLong:record->session_id];
  httpStatus = [NSNumber numberWithLong:record->http_status];
  prompt = [StrappySession stringFromCStringOrEmpty:record->prompt];
  response = [StrappySession stringFromCStringOrEmpty:record->response];
  model = [StrappySession stringFromCStringOrEmpty:record->model];
  createdAt = [StrappySession stringFromCStringOrEmpty:record->created_at];

  return [NSDictionary dictionaryWithObjectsAndKeys:
    sessionId, @"id",
    prompt, @"prompt",
    response, @"response",
    model, @"model",
    httpStatus, @"http_status",
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

+ (NSDictionary *)createSessionWithError:(NSError **)error
{
  NSString *databasePath;
  char *strappyError;
  long long sessionId;
  NSNumber *sessionIdentifier;

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
  return [StrappySession sessionSummaryForSessionIdentifier:sessionIdentifier
                                                     error:error];
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

  sessionId = [sessionIdentifier longLongValue];
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
  sessionId = [sessionIdentifier longLongValue];
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

+ (NSString *)submitPromptSynchronously:(NSString *)prompt error:(NSError **)error
{
  NSString *databasePath;
  NSString *systemPromptTemplatePath;
  NSString *responseString;
  char *response;
  char *strappyError;

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
  response = strappy_assistant_send_prompt_and_store([prompt UTF8String],
                                                     NULL,
                                                     [systemPromptTemplatePath fileSystemRepresentation],
                                                     [databasePath UTF8String],
                                                     &strappyError);
  if (response == NULL) {
    if (error != nil) {
      *error = [StrappySession errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return nil;
  }

  responseString = [NSString stringWithUTF8String:response];
  strappy_free_string(response);

  if (responseString == nil) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Response was not valid UTF-8.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:5
                               userInfo:userInfo];
    }
  }

  return responseString;
}

+ (NSDictionary *)submitPromptAndReturnSessionSynchronously:(NSString *)prompt
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

  systemPromptTemplatePath = [StrappySession systemPromptTemplatePathWithError:error];
  if (systemPromptTemplatePath == nil) {
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  sessionId = 0;
  strappyError = NULL;
  response = strappy_assistant_send_prompt_and_store_with_id([prompt UTF8String],
                                                             NULL,
                                                             [systemPromptTemplatePath fileSystemRepresentation],
                                                             [databasePath UTF8String],
                                                             &sessionId,
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

+ (NSDictionary *)submitPrompt:(NSString *)prompt
           inSessionIdentifier:(NSNumber *)sessionIdentifier
                         error:(NSError **)error
{
  NSString *databasePath;
  NSString *systemPromptTemplatePath;
  char *response;
  char *strappyError;
  long long sessionId;
  strappy_session_record record;
  NSDictionary *session;

  if (sessionIdentifier == nil) {
    return [StrappySession submitPromptAndReturnSessionSynchronously:prompt
                                                               error:error];
  }

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

  sessionId = [sessionIdentifier longLongValue];
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

+ (NSDictionary *)submitPromptStreaming:(NSString *)prompt
                    inSessionIdentifier:(NSNumber *)sessionIdentifier
                                context:(NSDictionary *)context
                               delegate:(id<StrappySessionStreamDelegate>)delegate
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

  systemPromptTemplatePath = [StrappySession systemPromptTemplatePathWithError:error];
  if (systemPromptTemplatePath == nil) {
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  streamContext.delegate = [(id)delegate retain];
  streamContext.context = [context retain];

  if (sessionIdentifier == nil) {
    sessionId = 0;
    strappyError = NULL;
    response = strappy_assistant_stream_prompt_and_store_with_id(
      [prompt UTF8String],
      NULL,
      [systemPromptTemplatePath fileSystemRepresentation],
      [databasePath UTF8String],
      &sessionId,
      StrappySessionHandleStreamEvent,
      &streamContext,
      &strappyError);
  } else {
    sessionId = [sessionIdentifier longLongValue];
    if (sessionId <= 0) {
      [streamContext.context release];
      [streamContext.delegate release];
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

    strappyError = NULL;
    response = strappy_assistant_stream_prompt_for_session_and_store(
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
  [streamContext.delegate release];

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

@end
