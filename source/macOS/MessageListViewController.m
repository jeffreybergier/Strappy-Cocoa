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
  identifierValue = [identifierText XP_longLongValue];
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
- (void)sendPromptDidFinish:(NSDictionary *)result;
- (NSString *)javaScriptForStreamEvent:(NSDictionary *)event;
- (void)queueJavaScriptForStreamEvent:(NSDictionary *)event;
- (void)schedulePendingStreamEventFlush;
- (void)streamEventFlushTimerDidFire:(NSTimer *)timer;
- (void)flushPendingStreamEvents;
- (void)cancelPendingStreamEventFlush;
- (BOOL)reconcileRenderedMessages;
- (BOOL)sessionPromptIsInFlight;
- (void)updateSendingStateFromSession;
- (void)beginSendingPrompt:(NSString *)prompt;
- (void)setPromptCancellationRequested:(BOOL)requested;
- (BOOL)promptCancellationRequested;
- (NSString *)writeCurrentHTML;
- (void)layoutWebViewAndPromptBar;
- (void)clearRequestState;
- (BOOL)updateSessionOptions:(StrappySessionOptions *)options
               changedFields:(StrappySessionOptionMask)changedFields;
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
  [sendController_ setEnabled:(session_ != nil)];
  [sendController_ setStudyLocked:studyLocked];
  [self updateSendingStateFromSession];
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
  }
  return YES;
}

- (NSArray *)availableModels
{
  NSArray *models;

  models = [StrappySession allowedOpenRouterModelCatalogWithError:nil];
  return (models != nil) ? models : [NSArray array];
}

- (NSString *)selectedModelIdentifier
{
  StrappySessionOptions *options;

  if (session_ == nil) {
    return @"";
  }

  options = [session_ optionsWithError:nil];
  return ([options modelIdentifier] != nil) ? [options modelIdentifier] : @"";
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
  NSError *error;
  NSString *errorMessage;
  StrappySessionOptions *options;
  BOOL changed;

  error = nil;
  options = [[session_ optionsWithError:&error] copy];
  if (options == nil) {
    errorMessage = [error localizedDescription];
    if ([errorMessage length] == 0U) {
      errorMessage = NSLocalizedString(@"Your changes could not be saved.", nil);
    }
    [statusText_ release];
    statusText_ = [errorMessage retain];
    [self reloadContent];
    return NO;
  }
  [options setModelIdentifier:modelIdentifier];
  changed = [self updateSessionOptions:options
                         changedFields:StrappySessionOptionModel];
  [options release];
  if (!changed) {
    [self reloadContent];
  }
  return changed;
}

- (BOOL)updateSessionOptions:(StrappySessionOptions *)options
               changedFields:(StrappySessionOptionMask)changedFields
{
  NSError *error;

  if (![self canSelectModel] || options == nil || changedFields == 0U) {
    return NO;
  }

  error = nil;
  if (![session_ updateOptions:options
                  changedFields:changedFields
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

  [statusText_ release];
  statusText_ = nil;
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
  NSError *renderError;
  NSError *writeError;

  path = [htmlDirectoryPath_ stringByAppendingPathComponent:@"session.html"];
  if (!StrappyEnsureDirectory(htmlDirectoryPath_)) {
    return nil;
  }
  if (session_ == nil) {
    return nil;
  }

  errorText = ([statusText_ length] > 0U) ? statusText_ : nil;
  timelineCursor = nil;
  renderError = nil;
  html = [session_ webViewMessagesPageHTMLWithErrorText:errorText
                                                palette:StrappyWebViewPaletteNeutral
                                           messageCount:NULL
                                         timelineCursor:&timelineCursor
                                                  error:&renderError];
  if (![html isKindOfClass:[NSString class]] || ([html length] == 0U)) {
    NSLog(@"StrappyResponses could not render WebView HTML for session %@: %@",
          [[session_ sessionIdentifier] description],
          ([renderError localizedDescription] != nil) ?
            [renderError localizedDescription] : @"empty HTML");
    return nil;
  }

  writeError = nil;
  if (![html writeToFile:path
              atomically:YES
                encoding:NSUTF8StringEncoding
                   error:&writeError]) {
    NSLog(@"StrappyResponses could not write WebView HTML for session %@: %@",
          [[session_ sessionIdentifier] description],
          ([writeError localizedDescription] != nil) ?
            [writeError localizedDescription] : @"unknown write error");
    return nil;
  }

  [newestRenderedTimelineCursor_ release];
  newestRenderedTimelineCursor_ = [timelineCursor copy];
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
  webViewReloadRequired_ = NO;
  [sendController_ setSending:YES];
  [sendController_ setCancellationRequested:NO];

  [self clearRequestState];

  startError = nil;
  didStartPrompt = [session_ beginResponsesPrompt:promptToSend
                                          context:nil
                                            error:&startError];
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

- (void)sessionStreamEvent:(NSNotification *)notification
{
  NSDictionary *event;
  NSString *js;
  NSString *streamEvent;

  if ([notification object] != session_) {
    return;
  }

  event = [notification userInfo];
  if (![event isKindOfClass:[NSDictionary class]]) {
    return;
  }

  [self updateSendingStateFromSession];
  if ([[event objectForKey:@"webview_reload_required"] boolValue]) {
    if (!webViewReloadRequired_) {
      [self cancelPendingStreamEventFlush];
      webViewReloadRequired_ = YES;
      [self reloadContent];
    }
    return;
  }
  if (webViewReloadRequired_) {
    return;
  }
  streamEvent = [event objectForKey:@"stream_event"];
  if ([streamEvent isEqualToString:@"ledger_changed"] ||
      [streamEvent isEqualToString:@"terminal_delta"]) {
    [self flushPendingStreamEvents];
    js = [self javaScriptForStreamEvent:event];
    if ([js length] > 0U) {
      [self pushJavaScript:js];
    }
    return;
  }
  [self queueJavaScriptForStreamEvent:event];
}

- (void)sessionPromptDidFinish:(NSNotification *)notification
{
  NSDictionary *result;
  BOOL databaseStudy;

  if ([notification object] != session_) {
    return;
  }
  result = [notification userInfo];
  databaseStudy = [[result objectForKey:@"database_study"] boolValue];
  if (webViewReloadRequired_) {
    [self cancelPendingStreamEventFlush];
  } else {
    [self flushPendingStreamEvents];
    if (!databaseStudy && ![self reconcileRenderedMessages]) {
      webViewReloadRequired_ = YES;
    }
  }
  [self sendPromptDidFinish:result];
  if (webViewReloadRequired_ && !databaseStudy) {
    [self reloadContent];
  }
  webViewReloadRequired_ = NO;
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
  if ([[result objectForKey:@"database_study"] boolValue]) {
    [self reloadContent];
  }
}

- (NSString *)javaScriptForStreamEvent:(NSDictionary *)event
{
  NSString *js;
  if (![event isKindOfClass:[NSDictionary class]]) {
    return @"";
  }

  js = [event objectForKey:@"webview_javascript"];
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

- (BOOL)reconcileRenderedMessages
{
  NSError *error;
  NSString *batchJS;
  NSString *clearJS;
  NSString *js;
  NSString *nextCursor;
  NSMutableString *reconciliation;
  NSUInteger reconciledCount;

  if (session_ == nil) {
    return NO;
  }
  error = nil;
  nextCursor = nil;
  reconciledCount = 0U;
  js = [session_
    webViewReconcileMessagesJavaScriptAfterTimelineCursor:
      newestRenderedTimelineCursor_
                                    nextTimelineCursor:&nextCursor
                                reconciledMessageCount:&reconciledCount
                                                 error:&error];
  if (![js isKindOfClass:[NSString class]] ||
      ((reconciledCount > 0U) && ([js length] == 0U))) {
    NSLog(@"StrappyResponses could not reconcile the WebView: %@",
          ([error localizedDescription] != nil) ?
            [error localizedDescription] : @"empty reconciliation");
    return NO;
  }
  clearJS = [session_ webViewClearProcessingStatusJavaScript];
  if ([clearJS length] == 0U) {
    return NO;
  }
  reconciliation = [NSMutableString stringWithString:js];
  [reconciliation appendString:clearJS];
  batchJS = [StrappySession
    webViewBatchedJavaScriptForJavaScript:reconciliation];
  if ([batchJS length] == 0U) {
    return NO;
  }
  [self pushJavaScript:batchJS];
  if ([nextCursor isKindOfClass:[NSString class]]) {
    [newestRenderedTimelineCursor_ release];
    newestRenderedTimelineCursor_ = [nextCursor copy];
  }
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
