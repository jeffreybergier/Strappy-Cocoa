#import "MessageListViewController.h"
#import "StrappySession.h"
#import "strappy_webview.h"

#include <string.h>

static const NSUInteger kStrappyInitialMessageLimit = 80U;
static const NSUInteger kStrappyMessagePageSize = 40U;

static NSString *StrappyHTMLDirectory(void)
{
  return [[StrappySession sessionsDatabasePath] stringByDeletingLastPathComponent];
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
  NSString *promptGroupKey;
  NSString *messageKey;
  NSString *targetMessageKey;
  NSString *text;
  NSString *reasoning;
  NSString *metadataJSON;
  NSString *renderStateJSON;
  NSString *createdAt;
  NSNumber *httpStatus;
  NSNumber *isError;

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
  promptGroupKey = [dictionary objectForKey:@"prompt_group_key"];
  messageKey = [dictionary objectForKey:@"message_key"];
  targetMessageKey = [dictionary objectForKey:@"target_message_key"];
  text = [dictionary objectForKey:@"text"];
  reasoning = [dictionary objectForKey:@"reasoning"];
  metadataJSON = [dictionary objectForKey:@"metadata_json"];
  renderStateJSON = [dictionary objectForKey:@"render_state_json"];
  createdAt = [dictionary objectForKey:@"created_at"];
  httpStatus = [dictionary objectForKey:@"http_status"];
  isError = [dictionary objectForKey:@"is_error"];

  message->message_id = StrappyMessageNumericIdentifier(dictionary);
  message->http_status =
    [httpStatus isKindOfClass:[NSNumber class]] ? [httpStatus longValue] : 0L;
  message->role = StrappyCString(role);
  message->kind = StrappyCString(kind);
  message->actor = StrappyCString(actor);
  message->prompt_group_key = StrappyCString(promptGroupKey);
  message->message_key = StrappyCString(messageKey);
  message->target_message_key = StrappyCString(targetMessageKey);
  message->text = StrappyCString(text);
  message->reasoning = StrappyCString(reasoning);
  message->metadata_json = StrappyCString(metadataJSON);
  message->render_state_json = StrappyCString(renderStateJSON);
  message->created_at = StrappyCString(createdAt);
  message->is_error =
    [isError isKindOfClass:[NSNumber class]] ? ([isError boolValue] ? 1 : 0) : 0;
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

@interface MessageListViewController ()
- (void)sessionStreamEvent:(NSNotification *)notification;
- (void)sessionPromptDidStart:(NSNotification *)notification;
- (void)sessionPromptDidFinish:(NSNotification *)notification;
- (void)modelCatalogDidChange:(NSNotification *)notification;
- (void)sendPromptDidFinish:(NSDictionary *)result;
- (BOOL)pushDatabaseMessageForStreamEvent:(NSDictionary *)event;
- (BOOL)sessionPromptIsInFlight;
- (void)updateSendingStateFromSession;
- (void)beginSendingPrompt:(NSString *)prompt;
- (void)setPromptCancellationRequested:(BOOL)requested;
- (BOOL)promptCancellationRequested;
- (void)loadEarlierMessages;
- (NSString *)writeCurrentHTML;
- (NSString *)htmlForMessages:(NSArray *)messages error:(NSError *)error;
- (void)layoutWebViewAndPromptBar;
- (void)clearRequestState;
@end

@implementation MessageListViewController

- (id)init
{
  NSString *directoryPath;
  NSURL *baseURL;

  directoryPath = StrappyHTMLDirectory();
  StrappyEnsureDirectory(directoryPath);
  baseURL = [NSURL fileURLWithPath:[directoryPath stringByAppendingString:@"/"]];

  if ((self = [super initWithBaseURL:baseURL])) {
    htmlDirectoryPath_ = [directoryPath copy];
    sendController_ = [[PromptSendViewController alloc] init];
    [sendController_ setDelegate:self];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(modelCatalogDidChange:)
             name:StrappySessionModelCatalogDidChangeNotification
           object:nil];
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
  [self setDrawsBackground:NO];

  [self AI_addChildViewController:sendController_];
  [[sendController_ view] setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
  [[self view] addSubview:[sendController_ view]];
  [sendController_ setEnabled:(session_ != nil)];
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

- (void)clearRequestState
{
}

- (BOOL)sessionPromptIsInFlight
{
  NSNumber *identifier;

  if (session_ == nil) {
    return NO;
  }
  if ([session_ isPromptInFlight]) {
    return YES;
  }

  identifier = [session_ sessionIdentifier];
  return [StrappySession isPromptInFlightForSessionIdentifier:identifier];
}

- (void)updateSendingStateFromSession
{
  BOOL inFlight;

  inFlight = [self sessionPromptIsInFlight];
  sending_ = inFlight ? YES : NO;
  [sendController_ setSending:sending_];
  [sendController_ setCancellationRequested:
    (sending_ && [self promptCancellationRequested]) ? YES : NO];
}

- (void)reloadWithSession:(StrappySession *)session
{
  BOOL sessionChanged;

  if (![session isKindOfClass:[StrappySession class]]) {
    session = nil;
  }

  sessionChanged = (session_ != session) ? YES : NO;
  if (sessionChanged) {
    [self clearRequestState];
    if (session_ != nil) {
      [[NSNotificationCenter defaultCenter] removeObserver:self
                                                      name:StrappySessionPromptDidStartNotification
                                                    object:session_];
      [[NSNotificationCenter defaultCenter] removeObserver:self
                                                      name:StrappySessionPromptDidFinishNotification
                                                    object:session_];
      [[NSNotificationCenter defaultCenter] removeObserver:self
                                                      name:StrappySessionStreamEventNotification
                                                    object:session_];
    }
    [session_ release];
    session_ = [session retain];
    if (session_ != nil) {
      [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(sessionPromptDidStart:)
               name:StrappySessionPromptDidStartNotification
             object:session_];
      [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(sessionPromptDidFinish:)
               name:StrappySessionPromptDidFinishNotification
             object:session_];
      [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(sessionStreamEvent:)
               name:StrappySessionStreamEventNotification
             object:session_];
    }
  }

  [sendController_ setEnabled:(session_ != nil)];
  [self updateSendingStateFromSession];
  [sendController_ setStreamingEnabled:(session_ != nil) ?
    [session_ streamingEnabled] : NO];
  [sendController_ reloadOptionsMenu];
  if (sessionChanged) {
    [self reloadContent];
  }
}

- (void)reloadData
{
}

- (BOOL)canSendCurrentPrompt
{
  if ((session_ == nil) || sending_ || [self sessionPromptIsInFlight]) {
    return NO;
  }
  return [sendController_ canSendCurrentPrompt];
}

- (void)sendCurrentPrompt:(id)sender
{
  [sendController_ performSend:sender];
}

- (BOOL)canCancelCurrentPrompt
{
  if ((session_ == nil) || ![self sessionPromptIsInFlight] ||
      [self promptCancellationRequested]) {
    return NO;
  }
  return YES;
}

- (void)cancelCurrentPrompt:(id)sender
{
  (void)sender;
  if (![self canCancelCurrentPrompt]) {
    return;
  }
  [self promptSendViewControllerDidCancelPrompt:sendController_];
}

- (BOOL)canToggleStreaming
{
  if ((session_ == nil) || sending_ || [self sessionPromptIsInFlight]) {
    return NO;
  }
  return YES;
}

- (BOOL)streamingEnabled
{
  return (session_ != nil) ? [session_ streamingEnabled] : NO;
}

- (void)toggleStreaming:(id)sender
{
  (void)sender;
  if (![self canToggleStreaming]) {
    return;
  }
  (void)[self promptSendViewController:sendController_
                   setStreamingEnabled:![self streamingEnabled]];
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
  SEL action;

  action = [item action];
  if (action == @selector(sendCurrentPrompt:)) {
    return [self canSendCurrentPrompt];
  } else if (action == @selector(cancelCurrentPrompt:)) {
    return [self canCancelCurrentPrompt];
  } else if (action == @selector(toggleStreaming:)) {
    [item setState:([self streamingEnabled] ?
      XPControlStateValueOn : XPControlStateValueOff)];
    return [self canToggleStreaming];
  }
  return YES;
}

- (NSArray *)allowedModelsForPromptSendViewController:
    (PromptSendViewController *)controller
{
  NSArray *models;

  (void)controller;
  models = [StrappySession allowedOpenRouterModelCatalogWithError:nil];
  return (models != nil) ? models : [NSArray array];
}

- (NSString *)selectedModelIdentifierForPromptSendViewController:
    (PromptSendViewController *)controller
{
  NSString *modelIdentifier;

  (void)controller;
  if (session_ == nil) {
    return @"";
  }

  modelIdentifier = [session_ selectedOpenRouterModelIdentifierWithError:nil];
  return (modelIdentifier != nil) ? modelIdentifier : @"";
}

- (BOOL)promptSendViewController:(PromptSendViewController *)controller
        setSelectedModelIdentifier:(NSString *)modelIdentifier
{
  NSError *error;

  (void)controller;
  if (session_ == nil) {
    return NO;
  }

  error = nil;
  if (![session_ setSelectedOpenRouterModelIdentifier:modelIdentifier
                                                error:&error]) {
    NSString *errorMessage;

    errorMessage = [error localizedDescription];
    if ([errorMessage length] == 0U) {
      errorMessage = NSLocalizedString(@"Could not update model setting.", nil);
    }
    [statusText_ release];
    statusText_ = [errorMessage retain];
    return NO;
  }

  return YES;
}

- (BOOL)promptSendViewController:(PromptSendViewController *)controller
              setStreamingEnabled:(BOOL)enabled
{
  NSError *error;

  (void)controller;
  if (session_ == nil) {
    return NO;
  }

  error = nil;
  if (![session_ setStreamingEnabled:enabled error:&error]) {
    NSString *errorMessage;

    errorMessage = [error localizedDescription];
    if ([errorMessage length] == 0U) {
      errorMessage = NSLocalizedString(@"Could not update streaming setting.", nil);
    }
    [statusText_ release];
    statusText_ = [errorMessage retain];
    [sendController_ setStreamingEnabled:[session_ streamingEnabled]];
    return NO;
  }

  [sendController_ setStreamingEnabled:[session_ streamingEnabled]];
  return YES;
}

+ (NSArray *)handledURLSchemes
{
  return [NSArray arrayWithObject:@"strappy-action"];
}

- (void)handleActionURL:(NSURL *)url
{
  NSString *host;

  host = [url host];
  if ([host isEqualToString:@"load-more"]) {
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

  path = [htmlDirectoryPath_ stringByAppendingPathComponent:@"session.html"];
  if (!StrappyEnsureDirectory(htmlDirectoryPath_)) {
    return nil;
  }

  error = nil;
  messages = nil;

  if (session_ != nil) {
    messages = [session_ messagesWithError:&error];
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

  hasMessages = (count > 0U) ? YES : NO;
  if ([statusText_ length] > 0U) {
    emptyText = statusText_;
  } else if (error != nil) {
    emptyText = [error localizedDescription];
  } else if (session_ == nil) {
    emptyText = NSLocalizedString(@"No session selected.", nil);
  } else {
    emptyText = NSLocalizedString(@"New Session", nil);
  }

  messagesHTML = [NSMutableString string];
  if (count > 0U) {
    [messagesHTML appendString:StrappyMessagesHTMLForRange(messages, start, count)];
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
  [self beginSendingPrompt:prompt];
}

- (void)promptSendViewControllerDidCancelPrompt:
    (PromptSendViewController *)controller
{
  (void)controller;
  if (!sending_) {
    return;
  }
  [self setPromptCancellationRequested:YES];
  [sendController_ setCancellationRequested:YES];
}

- (void)promptSendViewControllerDidChangeHeight:
    (PromptSendViewController *)controller
{
  (void)controller;
  [self layoutWebViewAndPromptBar];
  [[self view] setNeedsDisplay:YES];
}

- (void)setPromptCancellationRequested:(BOOL)requested
{
  @synchronized(self) {
    cancelPromptRequested_ = requested ? YES : NO;
  }
  if (requested && (session_ != nil)) {
    [session_ cancelPrompt];
  }
}

- (BOOL)promptCancellationRequested
{
  BOOL requested;

  @synchronized(self) {
    requested = cancelPromptRequested_;
  }
  if (!requested && (session_ != nil)) {
    requested = [session_ promptCancellationRequested];
  }
  return requested;
}

- (void)beginSendingPrompt:(NSString *)prompt
{
  NSString *promptToSend;
  NSError *startError;
  BOOL shouldStream;
  BOOL didStartPrompt;

  if (sending_) {
    return;
  }

  if ((session_ == nil) || [self sessionPromptIsInFlight]) {
    return;
  }

  if (![prompt isKindOfClass:[NSString class]] || ([prompt length] == 0U)) {
    return;
  }

  promptToSend = [prompt copy];

  sending_ = YES;
  [self setPromptCancellationRequested:NO];
  [statusText_ release];
  statusText_ = nil;
  [sendController_ setSending:YES];
  [sendController_ setCancellationRequested:NO];

  [self clearRequestState];

  startError = nil;
  shouldStream = [session_ streamingEnabled];
  if (shouldStream) {
    didStartPrompt = [session_ beginStreamingPrompt:promptToSend
                                           context:nil
                                             error:&startError];
  } else {
    didStartPrompt = [session_ beginNonStreamingPrompt:promptToSend
                                              context:nil
                                                error:&startError];
  }
  if (!didStartPrompt) {
    NSMutableDictionary *result;
    NSString *errorMessage;

    errorMessage = [startError localizedDescription];
    if ([errorMessage length] == 0U) {
      errorMessage = NSLocalizedString(@"Prompt failed.", nil);
    }
    result = [[NSMutableDictionary alloc] init];
    [result setObject:errorMessage forKey:@"error"];
    [self sendPromptDidFinish:result];
    [result release];
  }
  [promptToSend release];
}

- (void)sessionPromptDidStart:(NSNotification *)notification
{
  if ([notification object] != session_) {
    return;
  }
  [self updateSendingStateFromSession];
}

- (void)modelCatalogDidChange:(NSNotification *)notification
{
  (void)notification;
  [sendController_ reloadOptionsMenu];
}

- (void)sessionStreamEvent:(NSNotification *)notification
{
  NSDictionary *event;

  if ([notification object] != session_) {
    return;
  }

  event = [notification userInfo];
  if (![event isKindOfClass:[NSDictionary class]]) {
    return;
  }

  [self updateSendingStateFromSession];
  if (![self pushDatabaseMessageForStreamEvent:event]) {
    return;
  }
}

- (void)sessionPromptDidFinish:(NSNotification *)notification
{
  if ([notification object] != session_) {
    return;
  }
  [self sendPromptDidFinish:[notification userInfo]];
}

- (void)sendPromptDidFinish:(NSDictionary *)result
{
  NSDictionary *session;
  NSString *errorMessage;

  [self setPromptCancellationRequested:NO];
  [self updateSendingStateFromSession];

  session = [result objectForKey:@"session"];
  if ([session isKindOfClass:[NSDictionary class]]) {
    if (delegate_ != nil) {
      [delegate_ messageListViewController:self didUpdateSession:session];
    }
  } else {
    errorMessage = [result objectForKey:@"error"];
    if (![errorMessage isKindOfClass:[NSString class]] ||
        ([errorMessage length] == 0U)) {
      errorMessage = NSLocalizedString(@"Prompt failed.", nil);
    }
    [statusText_ release];
    statusText_ = [errorMessage retain];
  }

  [self clearRequestState];
  [self updateSendingStateFromSession];
}

- (BOOL)pushDatabaseMessageForStreamEvent:(NSDictionary *)event
{
  NSString *js;
  NSError *error;

  if ((session_ == nil) || ![event isKindOfClass:[NSDictionary class]]) {
    return NO;
  }

  error = nil;
  js = [session_ webViewJavaScriptForStreamEvent:event error:&error];
  if ([js length] == 0U) {
    return NO;
  }

  [self pushJavaScript:js];
  return YES;
}

- (void)loadEarlierMessages
{
  NSError *error;
  NSArray *messages;
  NSUInteger end;
  NSUInteger start;
  NSString *html;
  NSString *js;

  if ((session_ == nil) || (oldestRenderedMessageIndex_ == 0U)) {
    return;
  }

  error = nil;
  messages = [session_ messagesWithError:&error];
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

  js = StrappyPrependMessagesJavaScript(html, (start > 0U) ? YES : NO);
  [self pushJavaScript:js];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [htmlDirectoryPath_ release];
  [session_ release];
  [sendController_ release];
  [statusText_ release];
  [super dealloc];
}

@end
