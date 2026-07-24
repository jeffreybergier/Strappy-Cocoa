#import "MessageListViewController.h"
#import "StrappySession.h"
#import "XPAppKit.h"

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

static BOOL StrappyContextRoundActionValues(
  NSURL *url,
  NSNumber **identifier,
  BOOL *includedInContext)
{
  NSString *host;
  NSString *identifierText;
  NSString *includedText;
  NSArray *components;
  NSUInteger index;
  long long identifierValue;

  if ((identifier == NULL) || (includedInContext == NULL)) {
    return NO;
  }
  *identifier = nil;
  *includedInContext = NO;
  host = [url host];
  if (![host isEqualToString:@"context-round"]) {
    return NO;
  }

  components = [[url path] componentsSeparatedByString:@"/"];
  if ([components count] != 3U) {
    return NO;
  }
  identifierText = [components objectAtIndex:1U];
  includedText = [components objectAtIndex:2U];
  if (([identifierText length] == 0U) ||
      ([identifierText characterAtIndex:0U] == '0')) {
    return NO;
  }
  for (index = 0U; index < [identifierText length]; index++) {
    unichar character;

    character = [identifierText characterAtIndex:index];
    if ((character < '0') || (character > '9')) {
      return NO;
    }
  }
  identifierValue = [identifierText longLongValue];
  if (identifierValue <= 0LL) {
    return NO;
  }
  if ([includedText isEqualToString:@"1"]) {
    *includedInContext = YES;
  } else if (![includedText isEqualToString:@"0"]) {
    return NO;
  }
  *identifier = [NSNumber numberWithLongLong:identifierValue];
  return YES;
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
- (NSString *)writeCurrentHTML;
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
  BOOL studyLocked;

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

  studyLocked = (session_ != nil) && [session_ isDatabaseStudySession];
  [sendController_ setEnabled:((session_ != nil) && !studyLocked)];
  [sendController_ setStudyLocked:studyLocked];
  [self updateSendingStateFromSession];
  [sendController_ setStreamingEnabled:(session_ != nil) ?
    [session_ streamingEnabled] : NO];
  [sendController_ setWebProvider:(session_ != nil) ?
    [session_ webProvider] : StrappyWebProviderNone];
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
  if ((session_ == nil) || [session_ isDatabaseStudySession] || sending_ ||
      [self sessionPromptIsInFlight]) {
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
  if ((session_ == nil) || [session_ isDatabaseStudySession] || sending_ ||
      [self sessionPromptIsInFlight]) {
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

- (NSString *)webProviderForPromptSendViewController:
    (PromptSendViewController *)controller
{
  (void)controller;
  return (session_ != nil) ? [session_ webProvider] : StrappyWebProviderNone;
}

- (BOOL)canSelectModel
{
  if ((session_ == nil) || [session_ isDatabaseStudySession] || sending_ ||
      [self sessionPromptIsInFlight]) {
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
      errorMessage = NSLocalizedString(@"Your changes could not be saved.", nil);
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
      errorMessage = NSLocalizedString(@"Your changes could not be saved.", nil);
    }
    [statusText_ release];
    statusText_ = [errorMessage retain];
    [sendController_ setStreamingEnabled:[session_ streamingEnabled]];
    return NO;
  }

  [sendController_ setStreamingEnabled:[session_ streamingEnabled]];
  return YES;
}

- (BOOL)promptSendViewController:(PromptSendViewController *)controller
                  setWebProvider:(NSString *)webProvider
{
  NSError *error;

  (void)controller;
  if (session_ == nil) {
    return NO;
  }
  error = nil;
  if (![session_ setWebProvider:webProvider error:&error]) {
    NSString *errorMessage;

    errorMessage = [error localizedDescription];
    if ([errorMessage length] == 0U) {
      errorMessage = NSLocalizedString(@"Your changes could not be saved.", nil);
    }
    [statusText_ release];
    statusText_ = [errorMessage retain];
    [sendController_ setWebProvider:[session_ webProvider]];
    return NO;
  }
  [sendController_ setWebProvider:[session_ webProvider]];
  return YES;
}

+ (NSArray *)handledURLSchemes
{
  return [NSArray arrayWithObject:@"strappy-action"];
}

- (void)handleActionURL:(NSURL *)url
{
  NSNumber *identifier;
  NSError *error;
  NSString *errorMessage;
  NSString *javaScript;
  BOOL includedInContext;
  BOOL saved;

  if (!StrappyContextRoundActionValues(url,
                                       &identifier,
                                       &includedInContext)) {
    return;
  }
  if ((session_ == nil) || [self sessionPromptIsInFlight]) {
    [self reloadContent];
    return;
  }

  error = nil;
  saved = [session_
    setModelRequestIdentifier:identifier
            includedInContext:includedInContext
                        error:&error];
  if (saved) {
    [statusText_ release];
    statusText_ = nil;
    javaScript = [session_
      webViewJavaScriptForModelRequestIdentifier:identifier
                               includedInContext:includedInContext
                                         animated:YES];
    if ([javaScript length] > 0U) {
      [self flushPendingStreamEvents];
      [self pushJavaScript:javaScript];
      return;
    }
  } else {
    errorMessage = [error localizedDescription];
    if ([errorMessage length] == 0U) {
      errorMessage =
        NSLocalizedString(@"Your changes could not be saved.", nil);
    }
    [statusText_ release];
    statusText_ = [errorMessage retain];
    NSBeep();
  }
  [self reloadContent];
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
  NSString *errorText;
  NSString *timelineCursor;

  path = [htmlDirectoryPath_ stringByAppendingPathComponent:@"session.html"];
  if (!StrappyEnsureDirectory(htmlDirectoryPath_)) {
    return nil;
  }
  if (session_ == nil) {
    return nil;
  }

  errorText = ([statusText_ length] > 0U) ? statusText_ : nil;
  timelineCursor = nil;
  html = [session_ webViewMessagesPageHTMLWithErrorText:errorText
                                           messageCount:NULL
                                         timelineCursor:&timelineCursor
                                                  error:nil];
  [newestRenderedTimelineCursor_ release];
  newestRenderedTimelineCursor_ = [timelineCursor copy];
  if (![html writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil]) {
    return nil;
  }

  return path;
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

  if ((session_ == nil) || [session_ isDatabaseStudySession] ||
      [self sessionPromptIsInFlight]) {
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
  if (![session isKindOfClass:[NSDictionary class]]) {
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
  NSUInteger appendedCount;
  NSString *js;
  NSString *nextCursor;

  if (session_ == nil) {
    return NO;
  }
  appendedCount = 0U;
  nextCursor = nil;
  js = [session_
    webViewAppendMessagesJavaScriptAfterTimelineCursor:
      newestRenderedTimelineCursor_
                                    nextTimelineCursor:&nextCursor
                                  appendedMessageCount:&appendedCount
                                                 error:nil];
  if (![js isKindOfClass:[NSString class]]) {
    return NO;
  }
  if (appendedCount == 0U) {
    [newestRenderedTimelineCursor_ release];
    newestRenderedTimelineCursor_ = [nextCursor copy];
    return YES;
  }

  if ([js length] == 0U) {
    return NO;
  }
  [self flushPendingStreamEvents];
  [self pushJavaScript:js];
  [newestRenderedTimelineCursor_ release];
  newestRenderedTimelineCursor_ = [nextCursor copy];
  return YES;
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [self cancelPendingStreamEventFlush];
  [htmlDirectoryPath_ release];
  [session_ release];
  [sendController_ release];
  [statusText_ release];
  [newestRenderedTimelineCursor_ release];
  [super dealloc];
}

@end
