#import "StrappySession.h"

#import "strappy_assistant.h"
#import "strappy_client.h"
#import "strappy_core.h"
#import "strappy_db.h"
#import "XPFoundation.h"

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
  NSNumber *httpStatus;
  NSString *role;
  NSString *content;
  NSString *model;
  NSString *createdAt;

  if (record == NULL) {
    return nil;
  }

  messageId = [NSNumber numberWithLongLong:record->message_id];
  sessionId = [NSNumber numberWithLongLong:record->session_id];
  httpStatus = [NSNumber numberWithLong:record->http_status];
  role = [StrappySession stringFromCStringOrEmpty:record->role];
  content = [StrappySession stringFromCStringOrEmpty:record->content];
  model = [StrappySession stringFromCStringOrEmpty:record->model];
  createdAt = [StrappySession stringFromCStringOrEmpty:record->created_at];

  return [NSDictionary dictionaryWithObjectsAndKeys:
    messageId, @"id",
    sessionId, @"session_id",
    role, @"role",
    content, @"text",
    model, @"model",
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
  return [strappyDirectoryPath stringByAppendingPathComponent:@"sessions.sqlite3"];
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

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappyError = NULL;
  response = strappy_assistant_send_prompt_and_store([prompt UTF8String],
                                                     NULL,
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

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  sessionId = 0;
  strappyError = NULL;
  response = strappy_assistant_send_prompt_and_store_with_id([prompt UTF8String],
                                                             NULL,
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

  databasePath = [StrappySession sessionsDatabasePath];
  if (![StrappySession ensureSessionsDirectoryForDatabasePath:databasePath
                                                        error:error]) {
    return nil;
  }

  strappyError = NULL;
  response =
    strappy_assistant_send_prompt_for_session_and_store([prompt UTF8String],
                                                        NULL,
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

@end
