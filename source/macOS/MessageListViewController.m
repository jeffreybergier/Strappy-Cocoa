#import "MessageListViewController.h"
#import "StrappySession.h"
#import "strappy_webview.h"

#include <string.h>

static const NSUInteger kStrappyInitialMessageLimit = 80U;
static const NSUInteger kStrappyMessagePageSize = 40U;

static NSString *StrappyHTMLCacheDirectory(void)
{
  NSString *basePath;

  basePath = [[StrappySession sessionsDatabasePath] stringByDeletingLastPathComponent];
  return [basePath stringByAppendingPathComponent:@"WebView"];
}

static BOOL StrappyEnsureDirectory(NSString *path)
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
  BOOL isDirectory;
  SEL modernSelector;
  SEL legacySelector;

  if ([path length] == 0U) {
    return NO;
  }

  fileManager = [NSFileManager defaultManager];
  isDirectory = NO;
  if ([fileManager fileExistsAtPath:path isDirectory:&isDirectory]) {
    return isDirectory ? YES : NO;
  }

  {
    NSString *parentPath;

    parentPath = [path stringByDeletingLastPathComponent];
    if (([parentPath length] > 0U) && ![parentPath isEqualToString:path]) {
      if (!StrappyEnsureDirectory(parentPath)) {
        return NO;
      }
    }
  }

  modernSelector =
    @selector(createDirectoryAtPath:withIntermediateDirectories:attributes:error:);
  if ([fileManager respondsToSelector:modernSelector]) {
    StrappyModernCreateDirectoryFunction createDirectory;

    createDirectory =
      (StrappyModernCreateDirectoryFunction)[fileManager methodForSelector:modernSelector];
    return createDirectory(fileManager,
                           modernSelector,
                           path,
                           YES,
                           nil,
                           nil);
  }

  legacySelector = @selector(createDirectoryAtPath:attributes:);
  if ([fileManager respondsToSelector:legacySelector]) {
    StrappyLegacyCreateDirectoryFunction createDirectory;

    createDirectory =
      (StrappyLegacyCreateDirectoryFunction)[fileManager methodForSelector:legacySelector];
    return createDirectory(fileManager,
                           legacySelector,
                           path,
                           [NSDictionary dictionary]);
  }

  return NO;
}

static long long StrappyMessageNumericIdentifier(NSDictionary *message)
{
  NSNumber *identifier;

  identifier = [message objectForKey:@"id"];
  if (![identifier isKindOfClass:[NSNumber class]]) {
    return 0LL;
  }
  return [identifier longLongValue];
}

static NSString *StrappyMessageRole(NSDictionary *message)
{
  NSString *role;

  if (![message isKindOfClass:[NSDictionary class]]) {
    return @"";
  }

  role = [message objectForKey:@"role"];
  if (![role isKindOfClass:[NSString class]]) {
    return @"";
  }
  return role;
}

static BOOL StrappyMessageHasRole(NSDictionary *message, NSString *role)
{
  return [StrappyMessageRole(message) isEqualToString:role];
}

static const char *StrappyCString(NSString *string)
{
  if (![string isKindOfClass:[NSString class]]) {
    return "";
  }
  return [string UTF8String];
}

static NSString *StrappyStringFromWebViewCString(char *value)
{
  NSString *string;

  if (value == NULL) {
    return @"";
  }
  string = [NSString stringWithUTF8String:value];
  strappy_webview_free(value);
  return (string != nil) ? string : @"";
}

static strappy_webview_labels StrappyWebViewLabels(void)
{
  strappy_webview_labels labels;

  labels.agent = StrappyCString(NSLocalizedString(@"Agent", nil));
  labels.you = StrappyCString(NSLocalizedString(@"You", nil));
  labels.harness = StrappyCString(NSLocalizedString(@"Harness", nil));
  labels.thinking = StrappyCString(NSLocalizedString(@"Thinking", nil));
  labels.request_metadata =
    StrappyCString(NSLocalizedString(@"Request Metadata", nil));
  labels.tool_call = StrappyCString(NSLocalizedString(@"Tool Call", nil));
  labels.tool_result = StrappyCString(NSLocalizedString(@"Tool Result", nil));
  labels.retry = StrappyCString(NSLocalizedString(@"Retry", nil));
  return labels;
}

static void StrappyWebViewMessageFromDictionary(
  NSDictionary *dictionary,
  strappy_webview_message *message)
{
  NSString *role;
  NSString *kind;
  NSString *actor;
  NSString *messageKey;
  NSString *targetMessageKey;
  NSString *text;
  NSString *reasoning;
  NSString *metadataJSON;
  NSString *createdAt;
  NSNumber *httpStatus;

  if (message == NULL) {
    return;
  }
  memset(message, 0, sizeof(*message));

  if (![dictionary isKindOfClass:[NSDictionary class]]) {
    return;
  }

  role = [dictionary objectForKey:@"role"];
  kind = [dictionary objectForKey:@"kind"];
  actor = [dictionary objectForKey:@"actor"];
  messageKey = [dictionary objectForKey:@"message_key"];
  targetMessageKey = [dictionary objectForKey:@"target_message_key"];
  text = [dictionary objectForKey:@"text"];
  reasoning = [dictionary objectForKey:@"reasoning"];
  metadataJSON = [dictionary objectForKey:@"metadata_json"];
  createdAt = [dictionary objectForKey:@"created_at"];
  httpStatus = [dictionary objectForKey:@"http_status"];

  message->message_id = StrappyMessageNumericIdentifier(dictionary);
  message->http_status =
    [httpStatus isKindOfClass:[NSNumber class]] ? [httpStatus longValue] : 0L;
  message->role = StrappyCString(role);
  message->kind = StrappyCString(kind);
  message->actor = StrappyCString(actor);
  message->message_key = StrappyCString(messageKey);
  message->target_message_key = StrappyCString(targetMessageKey);
  message->text = StrappyCString(text);
  message->reasoning = StrappyCString(reasoning);
  message->metadata_json = StrappyCString(metadataJSON);
  message->created_at = StrappyCString(createdAt);
}

static NSString *StrappyStatusHTML(NSString *text, BOOL retry)
{
  strappy_webview_labels labels;

  labels = StrappyWebViewLabels();
  return StrappyStringFromWebViewCString(
    strappy_webview_status_html(StrappyCString(text), retry ? 1 : 0, &labels));
}

static NSString *StrappyMessageHTML(NSDictionary *message,
                                    NSString *elementIdentifier,
                                    NSString *state,
                                    NSString *statusHTML)
{
  strappy_webview_labels labels;
  strappy_webview_message webMessage;

  labels = StrappyWebViewLabels();
  StrappyWebViewMessageFromDictionary(message, &webMessage);
  webMessage.element_id = StrappyCString(elementIdentifier);
  return StrappyStringFromWebViewCString(
    strappy_webview_message_html(&webMessage,
                                 &labels,
                                 StrappyCString(state),
                                 StrappyCString(statusHTML)));
}

static NSString *StrappyPendingMessageHTML(NSString *prompt,
                                           NSString *elementIdentifier,
                                           NSString *state,
                                           NSString *statusHTML)
{
  strappy_webview_labels labels;

  labels = StrappyWebViewLabels();
  return StrappyStringFromWebViewCString(
    strappy_webview_pending_message_html(StrappyCString(prompt),
                                         StrappyCString(elementIdentifier),
                                         StrappyCString(state),
                                         StrappyCString(statusHTML),
                                         &labels));
}

static NSString *StrappyHarnessMessageHTML(NSString *prompt,
                                           NSString *elementIdentifier,
                                           NSString *state,
                                           NSString *statusHTML)
{
  strappy_webview_labels labels;
  strappy_webview_message message;

  labels = StrappyWebViewLabels();
  memset(&message, 0, sizeof(message));
  message.element_id = StrappyCString(elementIdentifier);
  message.role = "harness";
  message.text = StrappyCString(prompt);
  return StrappyStringFromWebViewCString(
    strappy_webview_message_html(&message,
                                 &labels,
                                 StrappyCString(state),
                                 StrappyCString(statusHTML)));
}

static NSString *StrappyStreamingAssistantMessageHTML(NSString *elementIdentifier,
                                                      NSString *text,
                                                      NSString *reasoning,
                                                      NSString *state,
                                                      NSString *statusHTML)
{
  strappy_webview_labels labels;

  labels = StrappyWebViewLabels();
  return StrappyStringFromWebViewCString(
    strappy_webview_streaming_assistant_message_html(
      StrappyCString(elementIdentifier),
      StrappyCString(text),
      StrappyCString(reasoning),
      StrappyCString(state),
      StrappyCString(statusHTML),
      &labels));
}

static NSString *StrappyToolActivityMessageHTML(NSString *elementIdentifier,
                                                NSString *text,
                                                NSString *state,
                                                NSString *statusHTML)
{
  strappy_webview_labels labels;

  labels = StrappyWebViewLabels();
  return StrappyStringFromWebViewCString(
    strappy_webview_tool_activity_message_html(StrappyCString(elementIdentifier),
                                               StrappyCString(text),
                                               StrappyCString(state),
                                               StrappyCString(statusHTML),
                                               &labels));
}

static NSString *StrappyMessageHTMLWithReasoning(NSDictionary *message,
                                                 NSString *reasoning)
{
  strappy_webview_labels labels;
  strappy_webview_message webMessage;

  labels = StrappyWebViewLabels();
  StrappyWebViewMessageFromDictionary(message, &webMessage);
  return StrappyStringFromWebViewCString(
    strappy_webview_message_html_with_reasoning(&webMessage,
                                                StrappyCString(reasoning),
                                                &labels));
}

static NSString *StrappyMessagesHTMLForRange(NSArray *messages,
                                             NSUInteger start,
                                             NSUInteger end)
{
  NSMutableString *html;
  NSUInteger index;

  html = [NSMutableString string];
  if (end > [messages count]) {
    end = [messages count];
  }
  for (index = start; index < end; index++) {
    NSDictionary *message;

    message = [messages objectAtIndex:index];
    if (![message isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    [html appendString:StrappyMessageHTML(message, nil, nil, nil)];
  }
  return html;
}

static NSString *StrappyAppendMessageJavaScript(NSString *messageHTML)
{
  return StrappyStringFromWebViewCString(
    strappy_webview_append_message_js(StrappyCString(messageHTML)));
}

static NSString *StrappyReplaceMessageJavaScript(NSString *elementIdentifier,
                                                 NSString *messageHTML)
{
  return StrappyStringFromWebViewCString(
    strappy_webview_replace_message_js(StrappyCString(elementIdentifier),
                                       StrappyCString(messageHTML)));
}

static NSString *StrappyInsertMessageBeforeJavaScript(NSString *beforeElementIdentifier,
                                                      NSString *messageHTML)
{
  return StrappyStringFromWebViewCString(
    strappy_webview_insert_message_before_js(StrappyCString(beforeElementIdentifier),
                                             StrappyCString(messageHTML)));
}

static NSString *StrappySetMessageStateJavaScript(NSString *elementIdentifier,
                                                  NSString *statusHTML,
                                                  NSString *state)
{
  return StrappyStringFromWebViewCString(
    strappy_webview_set_message_state_js(StrappyCString(elementIdentifier),
                                         StrappyCString(statusHTML),
                                         StrappyCString(state)));
}

static NSString *StrappyAppendMessageTextJavaScript(NSString *elementIdentifier,
                                                    NSString *delta)
{
  return StrappyStringFromWebViewCString(
    strappy_webview_append_message_text_js(StrappyCString(elementIdentifier),
                                           StrappyCString(delta)));
}

static NSString *StrappyAppendReasoningTextJavaScript(NSString *elementIdentifier,
                                                      NSString *delta)
{
  return StrappyStringFromWebViewCString(
    strappy_webview_append_reasoning_text_js(StrappyCString(elementIdentifier),
                                             StrappyCString(delta)));
}

static NSString *StrappyMoveMessageTextToReasoningJavaScript(
  NSString *elementIdentifier)
{
  return StrappyStringFromWebViewCString(
    strappy_webview_move_message_text_to_reasoning_js(
      StrappyCString(elementIdentifier)));
}

static NSString *StrappyToolEventText(NSString *eventType,
                                      NSString *toolCallIdentifier,
                                      NSString *toolName,
                                      NSString *argumentsJSON,
                                      NSString *resultJSON)
{
  return StrappyStringFromWebViewCString(
    strappy_webview_tool_event_text(StrappyCString(eventType),
                                    StrappyCString(toolCallIdentifier),
                                    StrappyCString(toolName),
                                    StrappyCString(argumentsJSON),
                                    StrappyCString(resultJSON)));
}

static NSString *StrappyAppendToolEventTextJavaScript(NSString *elementIdentifier,
                                                      NSString *eventText)
{
  return StrappyStringFromWebViewCString(
    strappy_webview_append_tool_event_text_js(StrappyCString(elementIdentifier),
                                              StrappyCString(eventText)));
}

static NSString *StrappyRemoveMessageJavaScript(NSString *elementIdentifier)
{
  return StrappyStringFromWebViewCString(
    strappy_webview_remove_message_js(StrappyCString(elementIdentifier)));
}

static NSString *StrappyPrependMessagesJavaScript(NSString *messagesHTML,
                                                  BOOL hasMore)
{
  return StrappyStringFromWebViewCString(
    strappy_webview_prepend_messages_js(StrappyCString(messagesHTML),
                                        hasMore ? 1 : 0));
}

static NSString *StrappyMessagesPageHTML(NSString *messagesHTML,
                                         NSString *emptyText,
                                         BOOL hasMessages,
                                         BOOL hasMore)
{
  return StrappyStringFromWebViewCString(
    strappy_webview_messages_page_html(
      StrappyCString(messagesHTML),
      StrappyCString(emptyText),
      hasMessages ? 1 : 0,
      hasMore ? 1 : 0,
      StrappyCString(NSLocalizedString(@"Show Earlier Messages", nil))));
}

@interface MessageListViewController () <StrappySessionStreamDelegate>
- (void)sendPromptInBackground:(NSDictionary *)request;
- (void)sendPromptDidFinish:(NSDictionary *)result;
- (void)streamContentDeltaDidArrive:(NSDictionary *)delta;
- (void)streamReasoningDeltaDidArrive:(NSDictionary *)delta;
- (void)streamToolCallDidArrive:(NSDictionary *)event;
- (void)streamToolResultDidArrive:(NSDictionary *)event;
- (void)streamToolErrorDidArrive:(NSDictionary *)event;
- (void)streamTurnDidStart:(NSDictionary *)event;
- (void)streamTurnDidFinish:(NSDictionary *)event;
- (void)applyStreamContentDeltaAndRelease:(NSDictionary *)delta;
- (void)applyStreamReasoningDeltaAndRelease:(NSDictionary *)delta;
- (void)applyStreamToolEventAndRelease:(NSDictionary *)event;
- (void)applyStreamTurnStartAndRelease:(NSDictionary *)event;
- (void)applyStreamTurnFinishAndRelease:(NSDictionary *)event;
- (void)ensurePendingHarnessTurnWithPrompt:(NSString *)prompt;
- (BOOL)streamEventUsesHarnessTarget:(NSDictionary *)event;
- (void)scheduleStreamFlush;
- (void)flushStreamDeltas;
- (void)flushStreamDeltasFromTimer:(NSTimer *)timer;
- (void)beginSendingPrompt:(NSString *)prompt reusingPendingMessage:(BOOL)reuse;
- (void)retryFailedPrompt;
- (void)loadEarlierMessages;
- (NSString *)writeCurrentHTML;
- (NSString *)htmlForMessages:(NSArray *)messages error:(NSError *)error;
- (void)layoutWebViewAndPromptBar;
- (BOOL)pendingAppliesToSessionIdentifier:(NSNumber *)identifier;
- (BOOL)pendingAppliesToCurrentSession;
- (void)clearPendingMessageState;
@end

@implementation MessageListViewController

- (id)init
{
  NSString *directoryPath;
  NSURL *baseURL;

  directoryPath = StrappyHTMLCacheDirectory();
  StrappyEnsureDirectory(directoryPath);
  baseURL = [NSURL fileURLWithPath:[directoryPath stringByAppendingString:@"/"]];

  if ((self = [super initWithBaseURL:baseURL])) {
    htmlDirectoryPath_ = [directoryPath copy];
    sendController_ = [[PromptSendViewController alloc] init];
    [sendController_ setDelegate:self];
    [self setDrawsBackground:NO];
  }
  return self;
}

- (void)setDelegate:(id<MessageListViewControllerDelegate>)delegate
{
  delegate_ = delegate;
}

- (id<MessageListViewControllerDelegate>)delegate
{
  return delegate_;
}

- (void)viewDidLoad
{
  [super viewDidLoad];

  [self AI_addChildViewController:sendController_];
  [[sendController_ view] setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
  [[self view] addSubview:[sendController_ view]];
  [sendController_ setEnabled:YES];
  [sendController_ setSending:sending_];

  [self reloadContent];
}

- (void)layoutWebViewAndPromptBar
{
  NSRect bounds;
  CGFloat barHeight;
  CGFloat webHeight;

  if (sendController_ != nil) {
    bounds = [[self view] bounds];
    barHeight = [sendController_ preferredHeight];
    webHeight = bounds.size.height - barHeight;
    if (webHeight < 0.0) {
      webHeight = 0.0;
    }

    [[sendController_ view] setFrame:NSMakeRect(0.0,
                                                0.0,
                                                bounds.size.width,
                                                barHeight)];
    [(NSView *)[self webView] setFrame:NSMakeRect(0.0,
                                                 barHeight,
                                                 bounds.size.width,
                                                 webHeight)];
  }
}

- (void)viewDidLayout
{
  [self layoutWebViewAndPromptBar];
  [super viewDidLayout];
}

- (BOOL)pendingAppliesToSessionIdentifier:(NSNumber *)identifier
{
  if (pendingMessageIdentifier_ == nil) {
    return NO;
  }
  if ((sendingSessionId_ == nil) && (identifier == nil)) {
    return YES;
  }
  if ((sendingSessionId_ != nil) && (identifier != nil) &&
      [sendingSessionId_ isEqualToNumber:identifier]) {
    return YES;
  }
  return NO;
}

- (BOOL)pendingAppliesToCurrentSession
{
  return [self pendingAppliesToSessionIdentifier:sessionId_];
}

- (void)clearPendingMessageState
{
  [streamFlushTimer_ invalidate];
  [streamFlushTimer_ release];
  streamFlushTimer_ = nil;
  [pendingMessageIdentifier_ release];
  pendingMessageIdentifier_ = nil;
  [pendingAssistantMessageIdentifier_ release];
  pendingAssistantMessageIdentifier_ = nil;
  [pendingToolActivityIdentifier_ release];
  pendingToolActivityIdentifier_ = nil;
  [pendingHarnessMessageIdentifier_ release];
  pendingHarnessMessageIdentifier_ = nil;
  [pendingHarnessAssistantMessageIdentifier_ release];
  pendingHarnessAssistantMessageIdentifier_ = nil;
  [pendingHarnessToolActivityIdentifier_ release];
  pendingHarnessToolActivityIdentifier_ = nil;
  [pendingHarnessPrompt_ release];
  pendingHarnessPrompt_ = nil;
  [pendingPrompt_ release];
  pendingPrompt_ = nil;
  [sendingSessionId_ release];
  sendingSessionId_ = nil;
  [streamingAssistantText_ release];
  streamingAssistantText_ = nil;
  [streamingReasoningText_ release];
  streamingReasoningText_ = nil;
  [streamingToolActivityText_ release];
  streamingToolActivityText_ = nil;
  [streamingHarnessAssistantText_ release];
  streamingHarnessAssistantText_ = nil;
  [streamingHarnessReasoningText_ release];
  streamingHarnessReasoningText_ = nil;
  [streamingHarnessToolActivityText_ release];
  streamingHarnessToolActivityText_ = nil;
  [pendingAssistantTextDelta_ release];
  pendingAssistantTextDelta_ = nil;
  [pendingReasoningTextDelta_ release];
  pendingReasoningTextDelta_ = nil;
  [pendingToolActivityTextDelta_ release];
  pendingToolActivityTextDelta_ = nil;
  [pendingHarnessAssistantTextDelta_ release];
  pendingHarnessAssistantTextDelta_ = nil;
  [pendingHarnessReasoningTextDelta_ release];
  pendingHarnessReasoningTextDelta_ = nil;
  [pendingHarnessToolActivityTextDelta_ release];
  pendingHarnessToolActivityTextDelta_ = nil;
}

- (void)scheduleStreamFlush
{
  if (streamFlushTimer_ != nil) {
    return;
  }

  streamFlushTimer_ =
    [[NSTimer scheduledTimerWithTimeInterval:2.0
                                      target:self
                                    selector:@selector(flushStreamDeltasFromTimer:)
                                    userInfo:nil
                                     repeats:NO] retain];
}

- (void)flushStreamDeltasFromTimer:(NSTimer *)timer
{
  if (timer != streamFlushTimer_) {
    return;
  }

  [streamFlushTimer_ release];
  streamFlushTimer_ = nil;
  [self flushStreamDeltas];
}

- (void)flushStreamDeltas
{
  NSMutableString *js;

  if (streamFlushTimer_ != nil) {
    [streamFlushTimer_ invalidate];
    [streamFlushTimer_ release];
    streamFlushTimer_ = nil;
  }

  js = [NSMutableString string];
  if ((pendingAssistantMessageIdentifier_ != nil) &&
      ([pendingAssistantTextDelta_ length] > 0U)) {
    [js appendString:
      StrappyAppendMessageTextJavaScript(pendingAssistantMessageIdentifier_,
                                         pendingAssistantTextDelta_)];
    [pendingAssistantTextDelta_ setString:@""];
  }
  if ((pendingAssistantMessageIdentifier_ != nil) &&
      ([pendingReasoningTextDelta_ length] > 0U)) {
    [js appendString:
      StrappyAppendReasoningTextJavaScript(pendingAssistantMessageIdentifier_,
                                           pendingReasoningTextDelta_)];
    [pendingReasoningTextDelta_ setString:@""];
  }
  if ((pendingToolActivityIdentifier_ != nil) &&
      ([pendingToolActivityTextDelta_ length] > 0U)) {
    [js appendString:
      StrappyAppendToolEventTextJavaScript(pendingToolActivityIdentifier_,
                                           pendingToolActivityTextDelta_)];
    [pendingToolActivityTextDelta_ setString:@""];
  }
  if ((pendingHarnessAssistantMessageIdentifier_ != nil) &&
      ([pendingHarnessAssistantTextDelta_ length] > 0U)) {
    [js appendString:
      StrappyAppendMessageTextJavaScript(pendingHarnessAssistantMessageIdentifier_,
                                         pendingHarnessAssistantTextDelta_)];
    [pendingHarnessAssistantTextDelta_ setString:@""];
  }
  if ((pendingHarnessAssistantMessageIdentifier_ != nil) &&
      ([pendingHarnessReasoningTextDelta_ length] > 0U)) {
    [js appendString:
      StrappyAppendReasoningTextJavaScript(pendingHarnessAssistantMessageIdentifier_,
                                           pendingHarnessReasoningTextDelta_)];
    [pendingHarnessReasoningTextDelta_ setString:@""];
  }
  if ((pendingHarnessToolActivityIdentifier_ != nil) &&
      ([pendingHarnessToolActivityTextDelta_ length] > 0U)) {
    [js appendString:
      StrappyAppendToolEventTextJavaScript(pendingHarnessToolActivityIdentifier_,
                                           pendingHarnessToolActivityTextDelta_)];
    [pendingHarnessToolActivityTextDelta_ setString:@""];
  }

  if ([js length] > 0U) {
    [self pushJavaScript:js];
  }
}

- (void)reloadWithSession:(NSDictionary *)session
{
  NSNumber *identifier;

  identifier = [session objectForKey:@"id"];
  if (![identifier isKindOfClass:[NSNumber class]]) {
    identifier = nil;
  }

  if (![self pendingAppliesToSessionIdentifier:identifier]) {
    [self clearPendingMessageState];
  }

  if (sessionId_ != identifier) {
    [sessionId_ release];
    sessionId_ = [identifier retain];
  }

  [self reloadContent];
}

- (void)reloadData
{
  [self reloadContent];
}

- (BOOL)canSendCurrentPrompt
{
  return [sendController_ canSendCurrentPrompt];
}

- (void)sendCurrentPrompt:(id)sender
{
  [sendController_ performSend:sender];
}

+ (NSArray *)handledURLSchemes
{
  return [NSArray arrayWithObject:@"strappy-action"];
}

- (void)handleActionURL:(NSURL *)url
{
  NSString *host;

  host = [url host];
  if ([host isEqualToString:@"retry"]) {
    [self retryFailedPrompt];
  } else if ([host isEqualToString:@"load-more"]) {
    [self loadEarlierMessages];
  }
}

- (NSURL *)contentURL
{
  NSString *path;

  path = [self writeCurrentHTML];
  if (path == nil) {
    return nil;
  }

  return [NSURL fileURLWithPath:path];
}

- (NSString *)writeCurrentHTML
{
  NSString *path;
  NSString *html;
  NSArray *messages;
  NSError *error;

  path = [htmlDirectoryPath_ stringByAppendingPathComponent:@"messages.html"];
  if (!StrappyEnsureDirectory(htmlDirectoryPath_)) {
    return nil;
  }

  error = nil;
  messages = nil;

  if (sessionId_ != nil) {
    messages = [StrappySession messagesForSessionIdentifier:sessionId_
                                                      error:&error];
  }

  html = [self htmlForMessages:messages error:error];
  if (![html writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil]) {
    return nil;
  }

  return path;
}

- (NSString *)htmlForMessages:(NSArray *)messages error:(NSError *)error
{
  NSMutableString *messagesHTML;
  NSUInteger count;
  NSUInteger start;
  NSString *emptyText;
  BOOL hasPending;
  BOOL hasMessages;

  if (![messages isKindOfClass:[NSArray class]]) {
    messages = nil;
  }

  count = [messages count];
  start = 0U;
  if (count > kStrappyInitialMessageLimit) {
    start = count - kStrappyInitialMessageLimit;
  }
  oldestRenderedMessageIndex_ = start;
  renderedMessageCount_ = count - start;
  lastKnownMessageIdentifier_ = 0LL;
  if (count > 0U) {
    lastKnownMessageIdentifier_ =
      StrappyMessageNumericIdentifier([messages objectAtIndex:(count - 1U)]);
  }

  hasPending = [self pendingAppliesToCurrentSession];
  hasMessages = ((count > 0U) || hasPending) ? YES : NO;
  if ([statusText_ length] > 0U) {
    emptyText = statusText_;
  } else if (error != nil) {
    emptyText = [error localizedDescription];
  } else {
    emptyText = NSLocalizedString(@"New Session", nil);
  }

  messagesHTML = [NSMutableString string];
  if (count > 0U) {
    [messagesHTML appendString:StrappyMessagesHTMLForRange(messages, start, count)];
  }
  if (hasPending) {
    [messagesHTML appendString:
      StrappyPendingMessageHTML(pendingPrompt_,
                                pendingMessageIdentifier_,
                                (sending_ ? nil : @"error"),
                                (sending_
                                 ? nil
                                 : StrappyStatusHTML(NSLocalizedString(@"Failed to send.", nil), YES)))];
    if (pendingToolActivityIdentifier_ != nil) {
      [messagesHTML appendString:
        StrappyToolActivityMessageHTML(
          pendingToolActivityIdentifier_,
          streamingToolActivityText_,
          (sending_ ? @"pending" : @"error"),
          (sending_
           ? StrappyStatusHTML(NSLocalizedString(@"Running tools...", nil), NO)
           : StrappyStatusHTML(NSLocalizedString(@"Failed to run tools.", nil), NO)))];
    }
    if (pendingAssistantMessageIdentifier_ != nil) {
      [messagesHTML appendString:
        StrappyStreamingAssistantMessageHTML(pendingAssistantMessageIdentifier_,
                                             streamingAssistantText_,
                                             streamingReasoningText_,
                                             (sending_ ? @"pending" : @"error"),
                                             (sending_
                                             ? StrappyStatusHTML(NSLocalizedString(@"Thinking", nil), NO)
                                             : StrappyStatusHTML(NSLocalizedString(@"Failed to send.", nil), NO)))];
    }
    if (pendingHarnessMessageIdentifier_ != nil) {
      [messagesHTML appendString:
        StrappyHarnessMessageHTML(pendingHarnessPrompt_,
                                  pendingHarnessMessageIdentifier_,
                                  (sending_ ? nil : @"error"),
                                  nil)];
      if (pendingHarnessToolActivityIdentifier_ != nil) {
        [messagesHTML appendString:
          StrappyToolActivityMessageHTML(
            pendingHarnessToolActivityIdentifier_,
            streamingHarnessToolActivityText_,
            (sending_ ? @"pending" : @"error"),
            (sending_
             ? StrappyStatusHTML(NSLocalizedString(@"Running tools...", nil), NO)
             : StrappyStatusHTML(NSLocalizedString(@"Failed to run tools.", nil), NO)))];
      }
      if (pendingHarnessAssistantMessageIdentifier_ != nil) {
        [messagesHTML appendString:
          StrappyStreamingAssistantMessageHTML(
            pendingHarnessAssistantMessageIdentifier_,
            streamingHarnessAssistantText_,
            streamingHarnessReasoningText_,
            (sending_ ? @"pending" : @"error"),
            (sending_
             ? StrappyStatusHTML(NSLocalizedString(@"Thinking", nil), NO)
             : StrappyStatusHTML(NSLocalizedString(@"Failed to send.", nil), NO)))];
      }
    }
  }

  return StrappyMessagesPageHTML(messagesHTML,
                                 emptyText,
                                 hasMessages,
                                 (start > 0U) ? YES : NO);
}

- (void)promptSendViewController:(PromptSendViewController *)controller
                 didSubmitPrompt:(NSString *)prompt
{
  (void)controller;
  [self beginSendingPrompt:prompt reusingPendingMessage:NO];
}

- (void)promptSendViewControllerDidChangeHeight:
    (PromptSendViewController *)controller
{
  (void)controller;
  [self layoutWebViewAndPromptBar];
  [[self view] setNeedsDisplay:YES];
}

- (void)beginSendingPrompt:(NSString *)prompt reusingPendingMessage:(BOOL)reuse
{
  NSMutableDictionary *request;
  NSString *promptToSend;
  static unsigned long pendingCounter = 0UL;

  if (sending_) {
    return;
  }

  if (![prompt isKindOfClass:[NSString class]] || ([prompt length] == 0U)) {
    return;
  }

  promptToSend = [prompt copy];

  sending_ = YES;
  [statusText_ release];
  statusText_ = nil;
  [sendController_ setSending:YES];

  if (!reuse || (pendingMessageIdentifier_ == nil)) {
    NSMutableString *js;

    [self clearPendingMessageState];
    pendingCounter++;
    pendingMessageIdentifier_ =
      [[NSString stringWithFormat:@"pending-%lu", pendingCounter] retain];
    pendingAssistantMessageIdentifier_ =
      [[NSString stringWithFormat:@"pending-%lu-assistant", pendingCounter] retain];
    pendingToolActivityIdentifier_ =
      [[NSString stringWithFormat:@"pending-%lu-tools", pendingCounter] retain];
    pendingPrompt_ = [promptToSend copy];
    sendingSessionId_ = [sessionId_ retain];
    streamingAssistantText_ = [[NSMutableString alloc] init];
    streamingReasoningText_ = [[NSMutableString alloc] init];
    streamingToolActivityText_ = [[NSMutableString alloc] init];
    pendingAssistantTextDelta_ = [[NSMutableString alloc] init];
    pendingReasoningTextDelta_ = [[NSMutableString alloc] init];
    pendingToolActivityTextDelta_ = [[NSMutableString alloc] init];
    pendingHarnessAssistantTextDelta_ = [[NSMutableString alloc] init];
    pendingHarnessReasoningTextDelta_ = [[NSMutableString alloc] init];
    pendingHarnessToolActivityTextDelta_ = [[NSMutableString alloc] init];

    js = [NSMutableString string];
    [js appendString:
      StrappyAppendMessageJavaScript(
        StrappyPendingMessageHTML(promptToSend,
                                  pendingMessageIdentifier_,
                                  nil,
                                  nil))];
    [js appendString:
      StrappyAppendMessageJavaScript(
        StrappyToolActivityMessageHTML(
          pendingToolActivityIdentifier_,
          streamingToolActivityText_,
          @"pending",
          StrappyStatusHTML(NSLocalizedString(@"Running tools...", nil), NO)))];
    [js appendString:
      StrappyAppendMessageJavaScript(
        StrappyStreamingAssistantMessageHTML(
          pendingAssistantMessageIdentifier_,
          streamingAssistantText_,
          streamingReasoningText_,
          @"pending",
          StrappyStatusHTML(NSLocalizedString(@"Thinking", nil), NO)))];
    [self pushJavaScript:js];
  } else {
    NSMutableString *js;

    if (pendingAssistantMessageIdentifier_ == nil) {
      pendingCounter++;
      pendingAssistantMessageIdentifier_ =
        [[NSString stringWithFormat:@"pending-%lu-assistant", pendingCounter] retain];
    }
    if (pendingToolActivityIdentifier_ == nil) {
      pendingToolActivityIdentifier_ =
        [[NSString stringWithFormat:@"pending-%lu-tools", pendingCounter] retain];
    }
    [streamingAssistantText_ release];
    streamingAssistantText_ = [[NSMutableString alloc] init];
    [streamingReasoningText_ release];
    streamingReasoningText_ = [[NSMutableString alloc] init];
    [streamingToolActivityText_ release];
    streamingToolActivityText_ = [[NSMutableString alloc] init];
    [pendingAssistantTextDelta_ release];
    pendingAssistantTextDelta_ = [[NSMutableString alloc] init];
    [pendingReasoningTextDelta_ release];
    pendingReasoningTextDelta_ = [[NSMutableString alloc] init];
    [pendingToolActivityTextDelta_ release];
    pendingToolActivityTextDelta_ = [[NSMutableString alloc] init];
    [pendingHarnessAssistantTextDelta_ release];
    pendingHarnessAssistantTextDelta_ = [[NSMutableString alloc] init];
    [pendingHarnessReasoningTextDelta_ release];
    pendingHarnessReasoningTextDelta_ = [[NSMutableString alloc] init];
    [pendingHarnessToolActivityTextDelta_ release];
    pendingHarnessToolActivityTextDelta_ = [[NSMutableString alloc] init];

    js = [NSMutableString string];
    [js appendString:
      StrappySetMessageStateJavaScript(pendingMessageIdentifier_,
                                       nil,
                                       nil)];
    [js appendString:StrappyRemoveMessageJavaScript(pendingToolActivityIdentifier_)];
    [js appendString:
      StrappyInsertMessageBeforeJavaScript(
        pendingAssistantMessageIdentifier_,
        StrappyToolActivityMessageHTML(
          pendingToolActivityIdentifier_,
          streamingToolActivityText_,
          @"pending",
          StrappyStatusHTML(NSLocalizedString(@"Running tools...", nil), NO)))];
    [js appendString:
      StrappyAppendMessageJavaScript(
        StrappyStreamingAssistantMessageHTML(
          pendingAssistantMessageIdentifier_,
          streamingAssistantText_,
          streamingReasoningText_,
          @"pending",
          StrappyStatusHTML(NSLocalizedString(@"Thinking", nil), NO)))];
    [self pushJavaScript:js];
  }

  request = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
    promptToSend, @"prompt",
    pendingMessageIdentifier_, @"pending_id",
    pendingAssistantMessageIdentifier_, @"assistant_pending_id",
    [NSNumber numberWithLongLong:lastKnownMessageIdentifier_], @"previous_last_id",
    [NSNumber numberWithBool:(sessionId_ == nil)], @"created",
    nil];
  if (sessionId_ != nil) {
    [request setObject:sessionId_ forKey:@"session_id"];
  }
  [self retain];
  [NSThread detachNewThreadSelector:@selector(sendPromptInBackground:)
                           toTarget:self
                         withObject:request];
  [request release];
  [promptToSend release];
}

- (void)retryFailedPrompt
{
  if (sending_ || (pendingPrompt_ == nil)) {
    return;
  }
  [self beginSendingPrompt:pendingPrompt_ reusingPendingMessage:YES];
}

- (void)sendPromptInBackground:(NSDictionary *)request
{
  NSAutoreleasePool *pool;
  NSError *error;
  NSDictionary *session;
  NSMutableDictionary *result;
  NSString *errorMessage;
  NSString *prompt;
  NSString *pendingIdentifier;
  NSString *assistantPendingIdentifier;
  NSNumber *sessionId;
  NSNumber *previousLastIdentifier;
  NSNumber *created;
  NSDictionary *streamContext;

  pool = [[NSAutoreleasePool alloc] init];
  prompt = [request objectForKey:@"prompt"];
  if (![prompt isKindOfClass:[NSString class]]) {
    prompt = @"";
  }
  pendingIdentifier = [request objectForKey:@"pending_id"];
  if (![pendingIdentifier isKindOfClass:[NSString class]]) {
    pendingIdentifier = @"";
  }
  assistantPendingIdentifier = [request objectForKey:@"assistant_pending_id"];
  if (![assistantPendingIdentifier isKindOfClass:[NSString class]]) {
    assistantPendingIdentifier = @"";
  }
  sessionId = [request objectForKey:@"session_id"];
  if (![sessionId isKindOfClass:[NSNumber class]]) {
    sessionId = nil;
  }
  previousLastIdentifier = [request objectForKey:@"previous_last_id"];
  if (![previousLastIdentifier isKindOfClass:[NSNumber class]]) {
    previousLastIdentifier = [NSNumber numberWithLongLong:0LL];
  }
  created = [request objectForKey:@"created"];
  if (![created isKindOfClass:[NSNumber class]]) {
    created = [NSNumber numberWithBool:NO];
  }
  result = [[NSMutableDictionary alloc] init];
  [result setObject:pendingIdentifier forKey:@"pending_id"];
  [result setObject:assistantPendingIdentifier forKey:@"assistant_pending_id"];
  [result setObject:previousLastIdentifier forKey:@"previous_last_id"];
  [result setObject:created forKey:@"created"];

  streamContext = [NSDictionary dictionaryWithObjectsAndKeys:
    pendingIdentifier, @"pending_id",
    assistantPendingIdentifier, @"assistant_pending_id",
    nil];

  error = nil;
  session = [StrappySession submitPromptStreaming:prompt
                              inSessionIdentifier:sessionId
                                          context:streamContext
                                         delegate:self
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

  [self performSelectorOnMainThread:@selector(sendPromptDidFinish:)
                         withObject:result
                      waitUntilDone:NO];
  [result release];
  [pool release];
  [self release];
}

- (void)strappySessionStreamDidReceiveContentDelta:(NSDictionary *)delta
{
  [self streamContentDeltaDidArrive:delta];
}

- (void)strappySessionStreamDidReceiveReasoningDelta:(NSDictionary *)delta
{
  [self streamReasoningDeltaDidArrive:delta];
}

- (void)strappySessionStreamDidReceiveToolCall:(NSDictionary *)event
{
  [self streamToolCallDidArrive:event];
}

- (void)strappySessionStreamDidReceiveToolResult:(NSDictionary *)event
{
  [self streamToolResultDidArrive:event];
}

- (void)strappySessionStreamDidReceiveToolError:(NSDictionary *)event
{
  [self streamToolErrorDidArrive:event];
}

- (void)strappySessionStreamDidStartTurn:(NSDictionary *)event
{
  [self streamTurnDidStart:event];
}

- (void)strappySessionStreamDidFinishTurn:(NSDictionary *)event
{
  [self streamTurnDidFinish:event];
}

- (void)streamContentDeltaDidArrive:(NSDictionary *)delta
{
  NSDictionary *retainedDelta;

  retainedDelta = [delta retain];
  [self performSelectorOnMainThread:@selector(applyStreamContentDeltaAndRelease:)
                         withObject:retainedDelta
                      waitUntilDone:NO];
}

- (void)streamReasoningDeltaDidArrive:(NSDictionary *)delta
{
  NSDictionary *retainedDelta;

  retainedDelta = [delta retain];
  [self performSelectorOnMainThread:@selector(applyStreamReasoningDeltaAndRelease:)
                         withObject:retainedDelta
                      waitUntilDone:NO];
}

- (void)streamToolCallDidArrive:(NSDictionary *)event
{
  NSDictionary *retainedEvent;

  retainedEvent = [event retain];
  [self performSelectorOnMainThread:@selector(applyStreamToolEventAndRelease:)
                         withObject:retainedEvent
                      waitUntilDone:NO];
}

- (void)streamToolResultDidArrive:(NSDictionary *)event
{
  NSDictionary *retainedEvent;

  retainedEvent = [event retain];
  [self performSelectorOnMainThread:@selector(applyStreamToolEventAndRelease:)
                         withObject:retainedEvent
                      waitUntilDone:NO];
}

- (void)streamToolErrorDidArrive:(NSDictionary *)event
{
  NSDictionary *retainedEvent;

  retainedEvent = [event retain];
  [self performSelectorOnMainThread:@selector(applyStreamToolEventAndRelease:)
                         withObject:retainedEvent
                      waitUntilDone:NO];
}

- (void)streamTurnDidStart:(NSDictionary *)event
{
  NSDictionary *retainedEvent;

  retainedEvent = [event retain];
  [self performSelectorOnMainThread:@selector(applyStreamTurnStartAndRelease:)
                         withObject:retainedEvent
                      waitUntilDone:NO];
}

- (void)streamTurnDidFinish:(NSDictionary *)event
{
  NSDictionary *retainedEvent;

  retainedEvent = [event retain];
  [self performSelectorOnMainThread:@selector(applyStreamTurnFinishAndRelease:)
                         withObject:retainedEvent
                      waitUntilDone:NO];
}

- (BOOL)streamDeltaAppliesToCurrentPendingMessage:(NSDictionary *)delta
{
  NSDictionary *context;
  NSString *pendingIdentifier;
  NSString *assistantPendingIdentifier;

  if (![delta isKindOfClass:[NSDictionary class]]) {
    return NO;
  }

  context = [delta objectForKey:@"context"];
  if (![context isKindOfClass:[NSDictionary class]]) {
    return NO;
  }

  pendingIdentifier = [context objectForKey:@"pending_id"];
  assistantPendingIdentifier = [context objectForKey:@"assistant_pending_id"];
  if (![pendingIdentifier isKindOfClass:[NSString class]] ||
      ![assistantPendingIdentifier isKindOfClass:[NSString class]]) {
    return NO;
  }

  if ((pendingMessageIdentifier_ == nil) ||
      ![pendingMessageIdentifier_ isEqualToString:pendingIdentifier]) {
    return NO;
  }

  if ((pendingAssistantMessageIdentifier_ == nil) ||
      ![pendingAssistantMessageIdentifier_ isEqualToString:assistantPendingIdentifier]) {
    return NO;
  }

  return [self pendingAppliesToCurrentSession];
}

- (BOOL)streamEventUsesHarnessTarget:(NSDictionary *)event
{
  NSString *actor;
  NSString *renderRole;

  actor = [event objectForKey:@"actor"];
  if ([actor isKindOfClass:[NSString class]] &&
      [actor isEqualToString:@"harness"]) {
    return YES;
  }

  renderRole = [event objectForKey:@"render_role"];
  return ([renderRole isKindOfClass:[NSString class]] &&
          [renderRole isEqualToString:@"harness"]) ? YES : NO;
}

- (void)ensurePendingHarnessTurnWithPrompt:(NSString *)prompt
{
  NSMutableString *js;
  NSString *promptText;

  if (pendingHarnessMessageIdentifier_ != nil) {
    return;
  }

  if ((pendingMessageIdentifier_ == nil) ||
      (pendingAssistantMessageIdentifier_ == nil)) {
    return;
  }

  promptText = ([prompt isKindOfClass:[NSString class]] &&
                ([prompt length] > 0U)) ?
    prompt : NSLocalizedString(@"Learning Summary", nil);

  pendingHarnessMessageIdentifier_ =
    [[pendingMessageIdentifier_ stringByAppendingString:@"-harness"] retain];
  pendingHarnessAssistantMessageIdentifier_ =
    [[pendingMessageIdentifier_ stringByAppendingString:@"-harness-assistant"] retain];
  pendingHarnessToolActivityIdentifier_ =
    [[pendingMessageIdentifier_ stringByAppendingString:@"-harness-tools"] retain];
  pendingHarnessPrompt_ = [promptText copy];
  streamingHarnessAssistantText_ = [[NSMutableString alloc] init];
  streamingHarnessReasoningText_ = [[NSMutableString alloc] init];
  streamingHarnessToolActivityText_ = [[NSMutableString alloc] init];
  if (pendingHarnessAssistantTextDelta_ == nil) {
    pendingHarnessAssistantTextDelta_ = [[NSMutableString alloc] init];
  }
  if (pendingHarnessReasoningTextDelta_ == nil) {
    pendingHarnessReasoningTextDelta_ = [[NSMutableString alloc] init];
  }
  if (pendingHarnessToolActivityTextDelta_ == nil) {
    pendingHarnessToolActivityTextDelta_ = [[NSMutableString alloc] init];
  }

  js = [NSMutableString string];
  [js appendString:
    StrappyAppendMessageJavaScript(
      StrappyHarnessMessageHTML(promptText,
                                pendingHarnessMessageIdentifier_,
                                nil,
                                nil))];
  [js appendString:
    StrappyAppendMessageJavaScript(
      StrappyToolActivityMessageHTML(
        pendingHarnessToolActivityIdentifier_,
        streamingHarnessToolActivityText_,
        @"pending",
        StrappyStatusHTML(NSLocalizedString(@"Running tools...", nil), NO)))];
  [js appendString:
    StrappyAppendMessageJavaScript(
      StrappyStreamingAssistantMessageHTML(
        pendingHarnessAssistantMessageIdentifier_,
        streamingHarnessAssistantText_,
        streamingHarnessReasoningText_,
        @"pending",
        StrappyStatusHTML(NSLocalizedString(@"Thinking", nil), NO)))];
  [self pushJavaScript:js];
}

- (void)applyStreamTurnStartAndRelease:(NSDictionary *)event
{
  NSString *prompt;

  if (![self streamDeltaAppliesToCurrentPendingMessage:event]) {
    [event release];
    return;
  }

  if ([self streamEventUsesHarnessTarget:event]) {
    prompt = [event objectForKey:@"delta"];
    if (![prompt isKindOfClass:[NSString class]]) {
      prompt = nil;
    }
    [self ensurePendingHarnessTurnWithPrompt:prompt];
  }
  [event release];
}

- (void)applyStreamTurnFinishAndRelease:(NSDictionary *)event
{
  if ([self streamDeltaAppliesToCurrentPendingMessage:event]) {
    [self flushStreamDeltas];
  }
  [event release];
}

- (void)applyStreamContentDeltaAndRelease:(NSDictionary *)delta
{
  NSMutableString *assistantText;
  NSString *assistantIdentifier;
  NSString *text;
  BOOL harnessTarget;

  if (![self streamDeltaAppliesToCurrentPendingMessage:delta]) {
    [delta release];
    return;
  }

  text = [delta objectForKey:@"delta"];
  if (![text isKindOfClass:[NSString class]] || ([text length] == 0U)) {
    [delta release];
    return;
  }

  harnessTarget = [self streamEventUsesHarnessTarget:delta];
  if (harnessTarget) {
    [self ensurePendingHarnessTurnWithPrompt:nil];
    if (streamingHarnessAssistantText_ == nil) {
      streamingHarnessAssistantText_ = [[NSMutableString alloc] init];
    }
    if (pendingHarnessAssistantTextDelta_ == nil) {
      pendingHarnessAssistantTextDelta_ = [[NSMutableString alloc] init];
    }
    assistantText = streamingHarnessAssistantText_;
    assistantIdentifier = pendingHarnessAssistantMessageIdentifier_;
  } else {
    if (streamingAssistantText_ == nil) {
      streamingAssistantText_ = [[NSMutableString alloc] init];
    }
    if (pendingAssistantTextDelta_ == nil) {
      pendingAssistantTextDelta_ = [[NSMutableString alloc] init];
    }
    assistantText = streamingAssistantText_;
    assistantIdentifier = pendingAssistantMessageIdentifier_;
  }

  if (assistantIdentifier == nil) {
    [delta release];
    return;
  }

  [assistantText appendString:text];
  if (harnessTarget) {
    [pendingHarnessAssistantTextDelta_ appendString:text];
  } else {
    [pendingAssistantTextDelta_ appendString:text];
  }
  [self scheduleStreamFlush];

  [delta release];
}

- (void)applyStreamReasoningDeltaAndRelease:(NSDictionary *)delta
{
  NSMutableString *reasoningText;
  NSString *assistantIdentifier;
  NSString *text;
  BOOL harnessTarget;

  if (![self streamDeltaAppliesToCurrentPendingMessage:delta]) {
    [delta release];
    return;
  }

  text = [delta objectForKey:@"delta"];
  if (![text isKindOfClass:[NSString class]] || ([text length] == 0U)) {
    [delta release];
    return;
  }

  harnessTarget = [self streamEventUsesHarnessTarget:delta];
  if (harnessTarget) {
    [self ensurePendingHarnessTurnWithPrompt:nil];
    if (streamingHarnessReasoningText_ == nil) {
      streamingHarnessReasoningText_ = [[NSMutableString alloc] init];
    }
    if (pendingHarnessReasoningTextDelta_ == nil) {
      pendingHarnessReasoningTextDelta_ = [[NSMutableString alloc] init];
    }
    reasoningText = streamingHarnessReasoningText_;
    assistantIdentifier = pendingHarnessAssistantMessageIdentifier_;
  } else {
    if (streamingReasoningText_ == nil) {
      streamingReasoningText_ = [[NSMutableString alloc] init];
    }
    if (pendingReasoningTextDelta_ == nil) {
      pendingReasoningTextDelta_ = [[NSMutableString alloc] init];
    }
    reasoningText = streamingReasoningText_;
    assistantIdentifier = pendingAssistantMessageIdentifier_;
  }

  if (assistantIdentifier == nil) {
    [delta release];
    return;
  }

  [reasoningText appendString:text];
  if (harnessTarget) {
    [pendingHarnessReasoningTextDelta_ appendString:text];
  } else {
    [pendingReasoningTextDelta_ appendString:text];
  }
  [self scheduleStreamFlush];

  [delta release];
}

- (void)applyStreamToolEventAndRelease:(NSDictionary *)event
{
  NSMutableString *js;
  NSString *eventType;
  NSString *toolCallIdentifier;
  NSString *toolName;
  NSString *argumentsJSON;
  NSString *resultJSON;
  NSString *eventText;
  NSString *assistantIdentifier;
  NSString *toolActivityIdentifier;
  NSMutableString *assistantText;
  NSMutableString *reasoningText;
  NSMutableString *toolActivityText;
  BOOL harnessTarget;
  BOOL createdToolActivity;

  if (![self streamDeltaAppliesToCurrentPendingMessage:event]) {
    [event release];
    return;
  }

  eventType = [event objectForKey:@"event_type"];
  if (![eventType isKindOfClass:[NSString class]] || ([eventType length] == 0U)) {
    eventType = @"call";
  }
  toolCallIdentifier = [event objectForKey:@"tool_call_id"];
  if (![toolCallIdentifier isKindOfClass:[NSString class]]) {
    toolCallIdentifier = @"";
  }
  toolName = [event objectForKey:@"tool_name"];
  if (![toolName isKindOfClass:[NSString class]]) {
    toolName = @"";
  }
  argumentsJSON = [event objectForKey:@"arguments_json"];
  if (![argumentsJSON isKindOfClass:[NSString class]]) {
    argumentsJSON = @"";
  }
  resultJSON = [event objectForKey:@"result_json"];
  if (![resultJSON isKindOfClass:[NSString class]]) {
    resultJSON = @"";
  }

  eventText = StrappyToolEventText(eventType,
                                   toolCallIdentifier,
                                   toolName,
                                   argumentsJSON,
                                   resultJSON);
  if ([eventText length] == 0U) {
    [event release];
    return;
  }

  harnessTarget = [self streamEventUsesHarnessTarget:event];
  if (harnessTarget) {
    [self ensurePendingHarnessTurnWithPrompt:nil];
    assistantIdentifier = pendingHarnessAssistantMessageIdentifier_;
    toolActivityIdentifier = pendingHarnessToolActivityIdentifier_;
    if (streamingHarnessAssistantText_ == nil) {
      streamingHarnessAssistantText_ = [[NSMutableString alloc] init];
    }
    if (streamingHarnessReasoningText_ == nil) {
      streamingHarnessReasoningText_ = [[NSMutableString alloc] init];
    }
    if (streamingHarnessToolActivityText_ == nil) {
      streamingHarnessToolActivityText_ = [[NSMutableString alloc] init];
    }
    if (pendingHarnessToolActivityTextDelta_ == nil) {
      pendingHarnessToolActivityTextDelta_ = [[NSMutableString alloc] init];
    }
    assistantText = streamingHarnessAssistantText_;
    reasoningText = streamingHarnessReasoningText_;
    toolActivityText = streamingHarnessToolActivityText_;
  } else {
    assistantIdentifier = pendingAssistantMessageIdentifier_;
    toolActivityIdentifier = pendingToolActivityIdentifier_;
    if (streamingAssistantText_ == nil) {
      streamingAssistantText_ = [[NSMutableString alloc] init];
    }
    if (streamingReasoningText_ == nil) {
      streamingReasoningText_ = [[NSMutableString alloc] init];
    }
    if (streamingToolActivityText_ == nil) {
      streamingToolActivityText_ = [[NSMutableString alloc] init];
    }
    if (pendingToolActivityTextDelta_ == nil) {
      pendingToolActivityTextDelta_ = [[NSMutableString alloc] init];
    }
    assistantText = streamingAssistantText_;
    reasoningText = streamingReasoningText_;
    toolActivityText = streamingToolActivityText_;
  }

  if (assistantIdentifier == nil) {
    [event release];
    return;
  }

  createdToolActivity = NO;
  if (toolActivityIdentifier == nil) {
    toolActivityIdentifier = [assistantIdentifier stringByAppendingString:@"-tools"];
    if (harnessTarget) {
      pendingHarnessToolActivityIdentifier_ = [toolActivityIdentifier retain];
    } else {
      pendingToolActivityIdentifier_ = [toolActivityIdentifier retain];
    }
    createdToolActivity = YES;
  }
  [toolActivityText appendString:eventText];

  js = [NSMutableString string];
  if ([eventType isEqualToString:@"call"] &&
      (assistantText != nil) &&
      ([assistantText length] > 0U)) {
    [self flushStreamDeltas];
    if ([reasoningText length] > 0U) {
      [reasoningText appendString:@"\n"];
    }
    [reasoningText appendString:assistantText];
    [assistantText setString:@""];
    [js appendString:
      StrappyMoveMessageTextToReasoningJavaScript(assistantIdentifier)];
  }
  if (createdToolActivity) {
    NSString *html;

    html = StrappyToolActivityMessageHTML(
      toolActivityIdentifier,
      @"",
      @"pending",
      StrappyStatusHTML(NSLocalizedString(@"Running tools...", nil), NO));
    if (assistantIdentifier != nil) {
      [js appendString:
        StrappyInsertMessageBeforeJavaScript(assistantIdentifier,
                                             html)];
    } else {
      [js appendString:StrappyAppendMessageJavaScript(html)];
    }
  }
  if ([js length] > 0U) {
    [self pushJavaScript:js];
  }
  if (harnessTarget) {
    [pendingHarnessToolActivityTextDelta_ appendString:eventText];
  } else {
    [pendingToolActivityTextDelta_ appendString:eventText];
  }
  [self scheduleStreamFlush];

  [event release];
}

- (void)sendPromptDidFinish:(NSDictionary *)result
{
  NSDictionary *session;
  NSString *errorMessage;
  NSString *pendingIdentifier;
  NSString *assistantPendingIdentifier;
  NSNumber *previousLastIdentifier;
  NSNumber *created;
  NSNumber *resultSessionId;
  BOOL pendingIsCurrent;

  sending_ = NO;
  [sendController_ setSending:NO];
  [self flushStreamDeltas];

  pendingIdentifier = [result objectForKey:@"pending_id"];
  if (![pendingIdentifier isKindOfClass:[NSString class]]) {
    pendingIdentifier = @"";
  }
  assistantPendingIdentifier = [result objectForKey:@"assistant_pending_id"];
  if (![assistantPendingIdentifier isKindOfClass:[NSString class]]) {
    assistantPendingIdentifier = @"";
  }
  previousLastIdentifier = [result objectForKey:@"previous_last_id"];
  if (![previousLastIdentifier isKindOfClass:[NSNumber class]]) {
    previousLastIdentifier = [NSNumber numberWithLongLong:0LL];
  }
  created = [result objectForKey:@"created"];
  if (![created isKindOfClass:[NSNumber class]]) {
    created = [NSNumber numberWithBool:NO];
  }

  pendingIsCurrent = (pendingMessageIdentifier_ != nil &&
                      [pendingMessageIdentifier_ isEqualToString:pendingIdentifier] &&
                      [self pendingAppliesToCurrentSession]) ? YES : NO;

  session = [result objectForKey:@"session"];
  if ([session isKindOfClass:[NSDictionary class]]) {
    NSArray *messages;
    NSError *messagesError;
    NSMutableString *js;
    NSMutableArray *newMessages;
    BOOL replacedPrompt;
    BOOL replacedAssistant;
    BOOL replacedHarnessPrompt;
    BOOL replacedHarnessAssistant;
    NSUInteger index;
    long long newestIdentifier;
    long long previousIdentifier;

    resultSessionId = [session objectForKey:@"id"];
    if (![resultSessionId isKindOfClass:[NSNumber class]]) {
      resultSessionId = nil;
    }

    if (delegate_ != nil) {
      if ([created boolValue]) {
        [delegate_ messageListViewController:self didCreateSession:session];
      } else {
        [delegate_ messageListViewController:self didUpdateSession:session];
      }
    }

    if (!pendingIsCurrent) {
      if ((resultSessionId != nil) && [sessionId_ isEqualToNumber:resultSessionId]) {
        [self reloadContent];
      }
      return;
    }

    if (resultSessionId != nil) {
      [sessionId_ release];
      sessionId_ = [resultSessionId retain];
    }

    messagesError = nil;
    messages = nil;
    if (sessionId_ != nil) {
      messages = [StrappySession messagesForSessionIdentifier:sessionId_
                                                        error:&messagesError];
    }

    if (messages == nil) {
      [self clearPendingMessageState];
      if (messagesError != nil) {
        [statusText_ release];
        statusText_ = [[messagesError localizedDescription] retain];
      }
      [self reloadContent];
      return;
    }

    previousIdentifier = [previousLastIdentifier longLongValue];
    newestIdentifier = previousIdentifier;
    newMessages = [NSMutableArray array];

    for (index = 0U; index < [messages count]; index++) {
      NSDictionary *message;
      long long messageIdentifier;

      message = [messages objectAtIndex:index];
      if (![message isKindOfClass:[NSDictionary class]]) {
        continue;
      }

      messageIdentifier = StrappyMessageNumericIdentifier(message);
      if (messageIdentifier <= previousIdentifier) {
        continue;
      }

      [newMessages addObject:message];
      if (messageIdentifier > newestIdentifier) {
        newestIdentifier = messageIdentifier;
      }
    }

    if ([newMessages count] == 0U) {
      [self clearPendingMessageState];
      [self reloadContent];
      return;
    }

    js = [NSMutableString string];
    [js appendString:@"beginMessageBatch();"];
    if (pendingToolActivityIdentifier_ != nil) {
      [js appendString:StrappyRemoveMessageJavaScript(pendingToolActivityIdentifier_)];
    }
    if (pendingHarnessToolActivityIdentifier_ != nil) {
      [js appendString:
        StrappyRemoveMessageJavaScript(pendingHarnessToolActivityIdentifier_)];
    }

    replacedPrompt = NO;
    replacedAssistant = NO;
    replacedHarnessPrompt = NO;
    replacedHarnessAssistant = NO;
    for (index = 0U; index < [newMessages count]; index++) {
      NSDictionary *message;
      NSString *messageHTML;

      message = [newMessages objectAtIndex:index];
      if (!replacedPrompt) {
        [js appendString:
          StrappyReplaceMessageJavaScript(pendingIdentifier,
                                          StrappyMessageHTML(message,
                                                             nil,
                                                             nil,
                                                             nil))];
        replacedPrompt = YES;
        continue;
      }

      if (!replacedAssistant && StrappyMessageHasRole(message, @"assistant")) {
        messageHTML = StrappyMessageHTMLWithReasoning(message,
                                                      streamingReasoningText_);
        if ([assistantPendingIdentifier length] > 0U) {
          [js appendString:
            StrappyReplaceMessageJavaScript(assistantPendingIdentifier,
                                            messageHTML)];
        } else {
          [js appendString:StrappyAppendMessageJavaScript(messageHTML)];
        }
        replacedAssistant = YES;
        continue;
      }

      if (replacedAssistant &&
          !replacedHarnessPrompt &&
          (pendingHarnessMessageIdentifier_ != nil) &&
          StrappyMessageHasRole(message, @"harness")) {
        [js appendString:
          StrappyReplaceMessageJavaScript(pendingHarnessMessageIdentifier_,
                                          StrappyMessageHTML(message,
                                                             nil,
                                                             nil,
                                                             nil))];
        replacedHarnessPrompt = YES;
        continue;
      }

      if (replacedAssistant &&
          !replacedHarnessAssistant &&
          (pendingHarnessAssistantMessageIdentifier_ != nil) &&
          StrappyMessageHasRole(message, @"assistant")) {
        [js appendString:
          StrappyReplaceMessageJavaScript(pendingHarnessAssistantMessageIdentifier_,
                                          StrappyMessageHTML(message,
                                                             nil,
                                                             nil,
                                                             nil))];
        replacedHarnessAssistant = YES;
        continue;
      }

      messageHTML = StrappyMessageHTML(message, nil, nil, nil);
      if (!replacedAssistant && ([assistantPendingIdentifier length] > 0U)) {
        [js appendString:
          StrappyInsertMessageBeforeJavaScript(assistantPendingIdentifier,
                                               messageHTML)];
      } else if (replacedAssistant &&
                 !replacedHarnessAssistant &&
                 (pendingHarnessAssistantMessageIdentifier_ != nil)) {
        [js appendString:
          StrappyInsertMessageBeforeJavaScript(
            pendingHarnessAssistantMessageIdentifier_,
            messageHTML)];
      } else {
        [js appendString:StrappyAppendMessageJavaScript(messageHTML)];
      }
    }

    if (!replacedAssistant && ([assistantPendingIdentifier length] > 0U)) {
      [js appendString:StrappyRemoveMessageJavaScript(assistantPendingIdentifier)];
    }
    if (!replacedHarnessPrompt && (pendingHarnessMessageIdentifier_ != nil)) {
      [js appendString:StrappyRemoveMessageJavaScript(pendingHarnessMessageIdentifier_)];
    }
    if (!replacedHarnessAssistant &&
        (pendingHarnessAssistantMessageIdentifier_ != nil)) {
      [js appendString:
        StrappyRemoveMessageJavaScript(pendingHarnessAssistantMessageIdentifier_)];
    }

    [js appendString:@"endMessageBatch();"];
    [self pushJavaScript:js];
    lastKnownMessageIdentifier_ = newestIdentifier;
    [self clearPendingMessageState];
    return;
  }

  errorMessage = [result objectForKey:@"error"];
  if (![errorMessage isKindOfClass:[NSString class]] || ([errorMessage length] == 0U)) {
    errorMessage = NSLocalizedString(@"Prompt failed.", nil);
  }

  if (pendingIsCurrent) {
    NSMutableString *js;

    js = [NSMutableString string];
    if ([assistantPendingIdentifier length] > 0U) {
      [js appendString:StrappyRemoveMessageJavaScript(assistantPendingIdentifier)];
    }
    if (pendingToolActivityIdentifier_ != nil) {
      [js appendString:StrappyRemoveMessageJavaScript(pendingToolActivityIdentifier_)];
    }
    if (pendingHarnessMessageIdentifier_ != nil) {
      [js appendString:StrappyRemoveMessageJavaScript(pendingHarnessMessageIdentifier_)];
    }
    if (pendingHarnessToolActivityIdentifier_ != nil) {
      [js appendString:
        StrappyRemoveMessageJavaScript(pendingHarnessToolActivityIdentifier_)];
    }
    if (pendingHarnessAssistantMessageIdentifier_ != nil) {
      [js appendString:
        StrappyRemoveMessageJavaScript(pendingHarnessAssistantMessageIdentifier_)];
    }
    [js appendString:
      StrappySetMessageStateJavaScript(pendingIdentifier,
                                       StrappyStatusHTML(errorMessage, YES),
                                       @"error")];
    [self pushJavaScript:js];
    [pendingAssistantMessageIdentifier_ release];
    pendingAssistantMessageIdentifier_ = nil;
    [pendingToolActivityIdentifier_ release];
    pendingToolActivityIdentifier_ = nil;
    [pendingHarnessMessageIdentifier_ release];
    pendingHarnessMessageIdentifier_ = nil;
    [pendingHarnessAssistantMessageIdentifier_ release];
    pendingHarnessAssistantMessageIdentifier_ = nil;
    [pendingHarnessToolActivityIdentifier_ release];
    pendingHarnessToolActivityIdentifier_ = nil;
    [pendingHarnessPrompt_ release];
    pendingHarnessPrompt_ = nil;
    [streamingAssistantText_ release];
    streamingAssistantText_ = nil;
    [streamingReasoningText_ release];
    streamingReasoningText_ = nil;
    [streamingToolActivityText_ release];
    streamingToolActivityText_ = nil;
    [streamingHarnessAssistantText_ release];
    streamingHarnessAssistantText_ = nil;
    [streamingHarnessReasoningText_ release];
    streamingHarnessReasoningText_ = nil;
    [streamingHarnessToolActivityText_ release];
    streamingHarnessToolActivityText_ = nil;
    [pendingAssistantTextDelta_ release];
    pendingAssistantTextDelta_ = nil;
    [pendingReasoningTextDelta_ release];
    pendingReasoningTextDelta_ = nil;
    [pendingToolActivityTextDelta_ release];
    pendingToolActivityTextDelta_ = nil;
    [pendingHarnessAssistantTextDelta_ release];
    pendingHarnessAssistantTextDelta_ = nil;
    [pendingHarnessReasoningTextDelta_ release];
    pendingHarnessReasoningTextDelta_ = nil;
    [pendingHarnessToolActivityTextDelta_ release];
    pendingHarnessToolActivityTextDelta_ = nil;
  } else {
    return;
  }
}

- (void)loadEarlierMessages
{
  NSError *error;
  NSArray *messages;
  NSUInteger end;
  NSUInteger start;
  NSString *html;
  NSString *js;

  if ((sessionId_ == nil) || (oldestRenderedMessageIndex_ == 0U)) {
    return;
  }

  error = nil;
  messages = [StrappySession messagesForSessionIdentifier:sessionId_
                                                    error:&error];
  if (messages == nil) {
    return;
  }

  end = oldestRenderedMessageIndex_;
  if (end > [messages count]) {
    end = [messages count];
  }
  if (end == 0U) {
    return;
  }

  if (end > kStrappyMessagePageSize) {
    start = end - kStrappyMessagePageSize;
  } else {
    start = 0U;
  }

  html = StrappyMessagesHTMLForRange(messages, start, end);
  oldestRenderedMessageIndex_ = start;
  renderedMessageCount_ += (end - start);

  js = StrappyPrependMessagesJavaScript(html, (start > 0U) ? YES : NO);
  [self pushJavaScript:js];
}

- (void)dealloc
{
  [htmlDirectoryPath_ release];
  [sessionId_ release];
  [sendController_ release];
  [statusText_ release];
  [pendingMessageIdentifier_ release];
  [pendingAssistantMessageIdentifier_ release];
  [pendingToolActivityIdentifier_ release];
  [pendingHarnessMessageIdentifier_ release];
  [pendingHarnessAssistantMessageIdentifier_ release];
  [pendingHarnessToolActivityIdentifier_ release];
  [pendingHarnessPrompt_ release];
  [pendingPrompt_ release];
  [sendingSessionId_ release];
  [streamingAssistantText_ release];
  [streamingReasoningText_ release];
  [streamingToolActivityText_ release];
  [streamingHarnessAssistantText_ release];
  [streamingHarnessReasoningText_ release];
  [streamingHarnessToolActivityText_ release];
  [pendingAssistantTextDelta_ release];
  [pendingReasoningTextDelta_ release];
  [pendingToolActivityTextDelta_ release];
  [pendingHarnessAssistantTextDelta_ release];
  [pendingHarnessReasoningTextDelta_ release];
  [pendingHarnessToolActivityTextDelta_ release];
  [streamFlushTimer_ invalidate];
  [streamFlushTimer_ release];
  [super dealloc];
}

@end
