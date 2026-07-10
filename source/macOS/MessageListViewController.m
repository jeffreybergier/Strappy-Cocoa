#import "MessageListViewController.h"
#import "StrappySession.h"
#import "XPAppKit.h"

static const NSUInteger kStrappyInitialMessageLimit = 80U;
static const NSUInteger kStrappyMessagePageSize = 40U;
static const NSTimeInterval kStrappyStreamEventFlushInterval = 0.3;

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

@interface MessageListViewController ()
- (void)sessionStreamEvent:(NSNotification *)notification;
- (void)sessionPromptDidStart:(NSNotification *)notification;
- (void)sessionPromptDidFinish:(NSNotification *)notification;
- (void)modelCatalogDidChange:(NSNotification *)notification;
- (void)sendPromptDidFinish:(NSDictionary *)result;
- (NSString *)javaScriptForStreamEvent:(NSDictionary *)event;
- (void)queueJavaScriptForStreamEvent:(NSDictionary *)event;
- (void)schedulePendingStreamEventFlush;
- (void)streamEventFlushTimerDidFire:(NSTimer *)timer;
- (void)flushPendingStreamEvents;
- (void)cancelPendingStreamEventFlush;
- (BOOL)sessionPromptIsInFlight;
- (void)updateSendingStateFromSession;
- (void)beginSendingPrompt:(NSString *)prompt;
- (void)setPromptCancellationRequested:(BOOL)requested;
- (BOOL)promptCancellationRequested;
- (BOOL)appendNewMessagesToWebView;
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
  [self cancelPendingStreamEventFlush];
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

- (BOOL)canPrintCurrentChat
{
  NSView *webView;

  webView = (NSView *)[self webView];
  return ((session_ != nil) &&
          (webView != nil) &&
          [webView XP_canPrintWebContent]) ? YES : NO;
}

- (void)printCurrentChat:(id)sender
{
  NSView *webView;

  webView = (NSView *)[self webView];
  if ((session_ == nil) ||
      (webView == nil) ||
      ![webView XP_printWebContent:sender]) {
    NSBeep();
    return;
  }
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

- (NSArray *)availableModels
{
  NSArray *models;

  models = [StrappySession allowedOpenRouterModelCatalogWithError:nil];
  return (models != nil) ? models : [NSArray array];
}

- (NSArray *)allowedModelsForPromptSendViewController:
    (PromptSendViewController *)controller
{
  (void)controller;
  return [self availableModels];
}

- (NSString *)selectedModelIdentifier
{
  NSString *modelIdentifier;

  if (session_ == nil) {
    return @"";
  }

  modelIdentifier = [session_ selectedOpenRouterModelIdentifierWithError:nil];
  return (modelIdentifier != nil) ? modelIdentifier : @"";
}

- (NSString *)selectedModelIdentifierForPromptSendViewController:
    (PromptSendViewController *)controller
{
  (void)controller;
  return [self selectedModelIdentifier];
}

- (BOOL)canSelectModel
{
  if ((session_ == nil) || sending_ || [self sessionPromptIsInFlight]) {
    return NO;
  }
  return YES;
}

- (BOOL)setSelectedModelIdentifier:(NSString *)modelIdentifier
{
  BOOL changed;

  changed = [self promptSendViewController:sendController_
                setSelectedModelIdentifier:modelIdentifier];
  if (changed) {
    [sendController_ reloadOptionsMenu];
  }
  return changed;
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
  newestRenderedMessageCount_ = count;

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
    [messagesHTML appendString:
      [StrappySession webViewMessagesHTMLForMessages:messages
                                          startIndex:start
                                            endIndex:count]];
  }

  return [StrappySession webViewMessagesPageHTMLForMessagesHTML:messagesHTML
                                                      emptyText:emptyText
                                                    hasMessages:hasMessages
                                                        hasMore:(start > 0U) ? YES : NO];
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
  NSString *streamEvent;

  if ([notification object] != session_) {
    return;
  }

  event = [notification userInfo];
  if (![event isKindOfClass:[NSDictionary class]]) {
    return;
  }

  [self updateSendingStateFromSession];
  streamEvent = [event objectForKey:@"stream_event"];
  if ([streamEvent isEqualToString:@"ledger_changed"]) {
    [self appendNewMessagesToWebView];
    return;
  }
  [self queueJavaScriptForStreamEvent:event];
}

- (void)sessionPromptDidFinish:(NSNotification *)notification
{
  if ([notification object] != session_) {
    return;
  }
  [self flushPendingStreamEvents];
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
  [self appendNewMessagesToWebView];
}

- (NSString *)javaScriptForStreamEvent:(NSDictionary *)event
{
  NSString *js;
  NSError *error;

  if ((session_ == nil) || ![event isKindOfClass:[NSDictionary class]]) {
    return @"";
  }

  error = nil;
  js = [session_ webViewJavaScriptForStreamEvent:event error:&error];
  if ([js length] == 0U) {
    return @"";
  }

  return js;
}

- (void)queueJavaScriptForStreamEvent:(NSDictionary *)event
{
  NSString *js;

  if (![event isKindOfClass:[NSDictionary class]]) {
    return;
  }

  js = [self javaScriptForStreamEvent:event];
  if ([js length] == 0U) {
    return;
  }

  if (pendingStreamJavaScript_ == nil) {
    pendingStreamJavaScript_ = [[NSMutableString alloc] init];
  }
  [pendingStreamJavaScript_ appendString:js];

  [self schedulePendingStreamEventFlush];
}

- (void)schedulePendingStreamEventFlush
{
  if (streamEventFlushTimer_ != nil) {
    return;
  }

  streamEventFlushTimer_ =
    [[NSTimer scheduledTimerWithTimeInterval:kStrappyStreamEventFlushInterval
                                      target:self
                                    selector:@selector(streamEventFlushTimerDidFire:)
                                    userInfo:nil
                                     repeats:NO] retain];
}

- (void)streamEventFlushTimerDidFire:(NSTimer *)timer
{
  if (streamEventFlushTimer_ == timer) {
    [streamEventFlushTimer_ invalidate];
    [streamEventFlushTimer_ release];
    streamEventFlushTimer_ = nil;
  }
  [self flushPendingStreamEvents];
}

- (void)flushPendingStreamEvents
{
  NSString *batchJS;

  if ([pendingStreamJavaScript_ length] == 0U) {
    return;
  }

  if (streamEventFlushTimer_ != nil) {
    [streamEventFlushTimer_ invalidate];
    [streamEventFlushTimer_ release];
    streamEventFlushTimer_ = nil;
  }

  batchJS =
    [StrappySession webViewBatchedJavaScriptForJavaScript:pendingStreamJavaScript_];
  [pendingStreamJavaScript_ release];
  pendingStreamJavaScript_ = nil;

  if ([batchJS length] > 0U) {
    [self pushJavaScript:batchJS];
  }
}

- (void)cancelPendingStreamEventFlush
{
  if (streamEventFlushTimer_ != nil) {
    [streamEventFlushTimer_ invalidate];
    [streamEventFlushTimer_ release];
    streamEventFlushTimer_ = nil;
  }
  if (pendingStreamJavaScript_ != nil) {
    [pendingStreamJavaScript_ release];
    pendingStreamJavaScript_ = nil;
  }
}

- (BOOL)appendNewMessagesToWebView
{
  NSArray *messages;
  NSError *error;
  NSUInteger count;
  NSUInteger start;
  NSString *html;
  NSString *js;

  if (session_ == nil) {
    return NO;
  }
  error = nil;
  messages = [session_ messagesWithError:&error];
  if (![messages isKindOfClass:[NSArray class]]) {
    return NO;
  }
  count = [messages count];
  start = newestRenderedMessageCount_;
  if (start > count) {
    return NO;
  }
  if (start == count) {
    return YES;
  }

  html = [StrappySession webViewMessagesHTMLForMessages:messages
                                             startIndex:start
                                               endIndex:count];
  if ([html length] == 0U) {
    return NO;
  }
  js = [StrappySession webViewAppendMessagesJavaScriptForHTML:html];
  if ([js length] == 0U) {
    return NO;
  }
  [self flushPendingStreamEvents];
  [self pushJavaScript:js];
  newestRenderedMessageCount_ = count;
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

  html = [StrappySession webViewMessagesHTMLForMessages:messages
                                             startIndex:start
                                               endIndex:end];
  oldestRenderedMessageIndex_ = start;

  js = [StrappySession webViewPrependMessagesJavaScriptForHTML:html
                                                       hasMore:(start > 0U) ? YES : NO];
  [self flushPendingStreamEvents];
  [self pushJavaScript:js];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [self cancelPendingStreamEventFlush];
  [htmlDirectoryPath_ release];
  [session_ release];
  [sendController_ release];
  [statusText_ release];
  [super dealloc];
}

@end
