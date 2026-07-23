#import "MessageListViewController.h"

#import "PromptSendViewController.h"
#import "StrappyIdleTimerAssertion.h"
#import "StrappySession.h"
#import "XPUIKit.h"

static const NSTimeInterval kStrappyStreamEventFlushInterval = 0.3;
static const CGFloat kStrappyInitialSendHeight = 44.0f;

static NSString *StrappyHTMLDirectory(void)
{
  return [[StrappySession sessionsDatabasePath] stringByDeletingLastPathComponent];
}

static BOOL StrappyEnsureDirectory(NSString *path)
{
  NSFileManager *fileManager;
  BOOL isDirectory;

  if ([path length] == 0U) {
    return NO;
  }

  fileManager = [NSFileManager defaultManager];
  isDirectory = NO;
  if ([fileManager fileExistsAtPath:path isDirectory:&isDirectory]) {
    return isDirectory ? YES : NO;
  }

  return [fileManager createDirectoryAtPath:path
                withIntermediateDirectories:YES
                                 attributes:nil
                                      error:nil];
}

static NSString *StrappyStringForSessionSummary(NSDictionary *summary,
                                                NSString *key)
{
  id value;

  if (![summary isKindOfClass:[NSDictionary class]]) {
    return @"";
  }

  value = [summary objectForKey:key];
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

static NSString *StrappyTitleForSessionSummary(NSDictionary *summary)
{
  NSString *name;
  NSString *prompt;

  name = StrappyStringForSessionSummary(summary, @"name");
  if ([name length] > 0U) {
    return name;
  }

  prompt = StrappyStringForSessionSummary(summary, @"prompt");
  if ([prompt length] > 0U) {
    return prompt;
  }

  return NSLocalizedString(@"New Session", nil);
}

static void StrappySetScrollsToTopOwnerInView(UIView *view,
                                              UIScrollView *owner)
{
  NSArray *subviews;
  NSUInteger index;

  if (view == nil) {
    return;
  }

  if ([view isKindOfClass:[UIScrollView class]]) {
    [(UIScrollView *)view setScrollsToTop:(view == owner) ? YES : NO];
  }

  subviews = [view subviews];
  for (index = 0U; index < [subviews count]; index++) {
    StrappySetScrollsToTopOwnerInView([subviews objectAtIndex:index], owner);
  }
}

static NSString *StrappyMessageListApplicationStateName(void)
{
  UIApplication *application;

  application = [UIApplication sharedApplication];
  switch ([application applicationState]) {
    case UIApplicationStateActive:
      return @"active";
    case UIApplicationStateInactive:
      return @"inactive";
    case UIApplicationStateBackground:
      return @"background";
  }
  return @"unknown";
}

static NSString *StrappyMessageListBoolString(BOOL value)
{
  return value ? @"YES" : @"NO";
}

static NSString *StrappyMessageListLifecycleEventName(NSString *notificationName)
{
  if ([notificationName isEqualToString:UIApplicationWillResignActiveNotification]) {
    return @"UIApplicationWillResignActiveNotification";
  }
  if ([notificationName isEqualToString:UIApplicationDidEnterBackgroundNotification]) {
    return @"UIApplicationDidEnterBackgroundNotification";
  }
  if ([notificationName isEqualToString:UIApplicationWillEnterForegroundNotification]) {
    return @"UIApplicationWillEnterForegroundNotification";
  }
  if ([notificationName isEqualToString:UIApplicationDidBecomeActiveNotification]) {
    return @"UIApplicationDidBecomeActiveNotification";
  }
  if ([notificationName isEqualToString:UIApplicationWillTerminateNotification]) {
    return @"UIApplicationWillTerminateNotification";
  }
  if ([notificationName isEqualToString:UIApplicationDidReceiveMemoryWarningNotification]) {
    return @"UIApplicationDidReceiveMemoryWarningNotification";
  }
  return [notificationName isKindOfClass:[NSString class]] ?
    notificationName : @"UIApplicationNotification";
}

@interface MessageListViewController () <UIWebViewDelegate,
                                          PromptSendViewControllerDelegate>
@property (nonatomic, strong) StrappySession *session;
@property (nonatomic, copy) NSString *htmlDirectoryPath;
@property (nonatomic, strong) UIWebView *webView;
@property (nonatomic, strong) PromptSendViewController *sendBar;
@property (nonatomic, copy) NSString *statusText;
@property (nonatomic, strong) NSMutableString *pendingStreamJavaScript;
@property (nonatomic, strong) NSTimer *streamEventFlushTimer;
@property (nonatomic, assign) NSUInteger newestRenderedMessageCount;
@property (nonatomic, assign) BOOL webViewContentLoaded;
@property (nonatomic, assign) BOOL sending;
@property (nonatomic, assign) BOOL cancelPromptRequested;
@property (nonatomic, assign) CGFloat composeBarBottomY;
@property (nonatomic, assign) BOOL composing;
@property (nonatomic, assign) BOOL tearingDown;
@property (nonatomic, assign, getter=isPromptIdleTimerAssertionEnabled)
  BOOL promptIdleTimerAssertionEnabled;
- (void)logLifecycleEvent:(NSString *)event;
- (void)observeKeyboard;
- (void)observeApplicationLifecycle;
- (void)applicationLifecycleNotification:(NSNotification *)notification;
- (void)updatePromptIdleTimerAssertion;
- (void)setPromptIdleTimerAssertionEnabled:(BOOL)enabled;
- (void)keyboardWillShow:(NSNotification *)notification;
- (void)keyboardWillHide:(NSNotification *)notification;
- (NSTimeInterval)keyboardDuration:(NSDictionary *)userInfo;
- (UIViewAnimationCurve)keyboardCurve:(NSDictionary *)userInfo;
- (void)relayoutComposeBarAnimated:(BOOL)animated
                              curve:(UIViewAnimationCurve)curve
                           duration:(NSTimeInterval)duration;
- (void)scrollWebViewToBottom;
- (void)setWebViewScrollsToTop:(BOOL)scrollsToTop;
- (void)sessionStreamEvent:(NSNotification *)notification;
- (void)sessionPromptDidStart:(NSNotification *)notification;
- (void)sessionPromptDidFinish:(NSNotification *)notification;
- (void)modelCatalogDidChange:(NSNotification *)notification;
- (void)sendPromptDidFinish:(NSDictionary *)result;
- (NSString *)javaScriptForStreamEvent:(NSDictionary *)event;
- (BOOL)queueJavaScriptForStreamEvent:(NSDictionary *)event;
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
- (void)reloadContent;
- (NSString *)writeCurrentHTML;
- (void)clearRequestState;
- (BOOL)isLeavingNavigationStack;
- (void)prepareForRemoval;
- (void)updateTitleFromSession;
- (void)showError:(NSError *)error title:(NSString *)title;
- (void)showMessage:(NSString *)message title:(NSString *)title;
@end

@implementation MessageListViewController

- (instancetype)initWithSession:(StrappySession *)session
{
  NSString *directoryPath;

  directoryPath = StrappyHTMLDirectory();
  StrappyEnsureDirectory(directoryPath);

  if ((self = [super initWithNibName:nil bundle:nil])) {
    [self setHtmlDirectoryPath:directoryPath];
    [self setSession:session];
    [self setTitle:NSLocalizedString(@"Strappy", nil)];
    [self logLifecycleEvent:@"initWithSession"];
  }
  return self;
}

- (void)logLifecycleEvent:(NSString *)event
{
  UIApplication *application;
  NSNumber *sessionIdentifier;
  NSUInteger pendingJavaScriptLength;
  BOOL viewLoaded;
  BOOL viewInWindow;
  BOOL inFlight;

  application = [UIApplication sharedApplication];
  sessionIdentifier = nil;
  if ([self session] != nil) {
    sessionIdentifier = [[self session] sessionIdentifier];
  }
  pendingJavaScriptLength = [[self pendingStreamJavaScript] length];
  viewLoaded = [self isViewLoaded] ? YES : NO;
  viewInWindow = NO;
  if (viewLoaded) {
    viewInWindow = ([[self view] window] != nil) ? YES : NO;
  }
  inFlight = [self sessionPromptIsInFlight];

  NSLog(@"StrappyLifecycle MessageList %@ self=%p appState=%@ backgroundTimeRemaining=%.3f session=%@ webView=%p viewLoaded=%@ viewInWindow=%@ contentLoaded=%@ sending=%@ inFlight=%@ cancelRequested=%@ pendingJS=%lu timer=%@ promptIdleAssertion=%@ tearingDown=%@",
        [event isKindOfClass:[NSString class]] ? event : @"event",
        (__bridge void *)self,
        StrappyMessageListApplicationStateName(),
        [application backgroundTimeRemaining],
        sessionIdentifier,
        (__bridge void *)[self webView],
        StrappyMessageListBoolString(viewLoaded),
        StrappyMessageListBoolString(viewInWindow),
        StrappyMessageListBoolString([self webViewContentLoaded]),
        StrappyMessageListBoolString([self sending]),
        StrappyMessageListBoolString(inFlight),
        StrappyMessageListBoolString([self promptCancellationRequested]),
        (unsigned long)pendingJavaScriptLength,
        StrappyMessageListBoolString(([self streamEventFlushTimer] != nil) ? YES : NO),
        StrappyMessageListBoolString([self isPromptIdleTimerAssertionEnabled]),
        StrappyMessageListBoolString([self tearingDown]));
}

- (void)viewDidLoad
{
  CGRect bounds;
  CGFloat barTop;
  UIWebView *webView;
  PromptSendViewController *sendBar;

  [super viewDidLoad];
  [self logLifecycleEvent:@"viewDidLoad begin"];

  [[self view] setBackgroundColor:[UIColor messagesBackgroundColor]];
  [self updateTitleFromSession];

  bounds = [[self view] bounds];
  barTop = bounds.size.height - kStrappyInitialSendHeight;
  if (barTop < 0.0f) {
    barTop = 0.0f;
  }

  webView =
    [[UIWebView alloc] initWithFrame:CGRectMake(0.0f,
                                                0.0f,
                                                bounds.size.width,
                                                barTop)];
  [webView setAutoresizingMask:UIViewAutoresizingFlexibleWidth |
                               UIViewAutoresizingFlexibleHeight];
  [webView setDelegate:self];
  [webView XP_setBackgroundTransparent];
  [[webView XP_scrollView] setDecelerationRate:UIScrollViewDecelerationRateNormal];
  [[self view] addSubview:webView];
  [self setWebView:webView];
  [self setWebViewScrollsToTop:YES];

  sendBar = [[PromptSendViewController alloc]
    initWithFrame:CGRectMake(0.0f,
                             barTop,
                             bounds.size.width,
                             kStrappyInitialSendHeight)];
  [sendBar setDelegate:self];
  [[self view] addSubview:sendBar];
  [self setSendBar:sendBar];

  [self setComposeBarBottomY:bounds.size.height];
  [self observeKeyboard];
  [self observeApplicationLifecycle];

  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(modelCatalogDidChange:)
           name:StrappySessionModelCatalogDidChangeNotification
         object:nil];

  {
    StrappySession *initialSession;

    initialSession = [self session];
    [self setSession:nil];
    [self reloadWithSession:initialSession];
  }
  [self logLifecycleEvent:@"viewDidLoad end"];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [self logLifecycleEvent:@"viewWillAppear begin"];
  if (![self composing]) {
    [self setComposeBarBottomY:[[self view] bounds].size.height];
  }
  [self relayoutComposeBarAnimated:NO
                              curve:UIViewAnimationCurveEaseInOut
                           duration:0.0];
  [self setWebViewScrollsToTop:YES];
  [self logLifecycleEvent:@"viewWillAppear end"];
}

- (void)viewDidAppear:(BOOL)animated
{
  [super viewDidAppear:animated];
  [self updatePromptIdleTimerAssertion];
  [self logLifecycleEvent:@"viewDidAppear"];
}

- (void)viewWillDisappear:(BOOL)animated
{
  [super viewWillDisappear:animated];
  [self logLifecycleEvent:@"viewWillDisappear begin"];
  [self setPromptIdleTimerAssertionEnabled:NO];
  [self setWebViewScrollsToTop:NO];
  if ([self isLeavingNavigationStack]) {
    [self prepareForRemoval];
  }
  [[self view] endEditing:YES];
  [self logLifecycleEvent:@"viewWillDisappear end"];
}

- (void)viewDidDisappear:(BOOL)animated
{
  [super viewDidDisappear:animated];
  [self updatePromptIdleTimerAssertion];
  [self logLifecycleEvent:@"viewDidDisappear"];
}

- (void)didReceiveMemoryWarning
{
  [super didReceiveMemoryWarning];
  [self logLifecycleEvent:@"didReceiveMemoryWarning"];
}

- (BOOL)isLeavingNavigationStack
{
  UINavigationController *navigationController;

  if ([self XP_isMovingFromParentViewController]) {
    return YES;
  }

  navigationController = [self navigationController];
  if (navigationController == nil) {
    return YES;
  }
  return ![[navigationController viewControllers] containsObject:self];
}

- (void)prepareForRemoval
{
  UIWebView *webView;
  UIScrollView *scrollView;

  if ([self tearingDown]) {
    [self logLifecycleEvent:@"prepareForRemoval already tearingDown"];
    return;
  }

  [self logLifecycleEvent:@"prepareForRemoval begin"];
  [self setPromptIdleTimerAssertionEnabled:NO];
  [self setTearingDown:YES];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [self cancelPendingStreamEventFlush];
  [[self sendBar] setDelegate:nil];

  webView = [self webView];
  scrollView = [webView XP_scrollView];
  [scrollView setScrollsToTop:NO];
  [scrollView setDelegate:nil];
  [webView setDelegate:nil];
  [webView stopLoading];
  [webView removeFromSuperview];
  [self setWebView:nil];
  [self logLifecycleEvent:@"prepareForRemoval end"];
}

- (void)observeKeyboard
{
  NSNotificationCenter *notificationCenter;

  notificationCenter = [NSNotificationCenter defaultCenter];
  [notificationCenter addObserver:self
                         selector:@selector(keyboardWillShow:)
                             name:UIKeyboardWillShowNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(keyboardWillHide:)
                             name:UIKeyboardWillHideNotification
                           object:nil];
}

- (void)observeApplicationLifecycle
{
  NSNotificationCenter *notificationCenter;

  notificationCenter = [NSNotificationCenter defaultCenter];
  [notificationCenter addObserver:self
                         selector:@selector(applicationLifecycleNotification:)
                             name:UIApplicationWillResignActiveNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(applicationLifecycleNotification:)
                             name:UIApplicationDidEnterBackgroundNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(applicationLifecycleNotification:)
                             name:UIApplicationWillEnterForegroundNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(applicationLifecycleNotification:)
                             name:UIApplicationDidBecomeActiveNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(applicationLifecycleNotification:)
                             name:UIApplicationWillTerminateNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(applicationLifecycleNotification:)
                             name:UIApplicationDidReceiveMemoryWarningNotification
                           object:nil];
}

- (void)applicationLifecycleNotification:(NSNotification *)notification
{
  [self logLifecycleEvent:
    StrappyMessageListLifecycleEventName([notification name])];
  [self updatePromptIdleTimerAssertion];
}

- (void)setPromptIdleTimerAssertionEnabled:(BOOL)enabled
{
  if ([self isPromptIdleTimerAssertionEnabled] == enabled) {
    return;
  }

  _promptIdleTimerAssertionEnabled = enabled ? YES : NO;
  StrappyIdleTimerAssertionSetEnabled(enabled);
  NSLog(@"StrappyLifecycle MessageList promptIdleTimerAssertion %@ self=%p session=%@",
        StrappyMessageListBoolString(enabled),
        (__bridge void *)self,
        (([self session] != nil) ? [[self session] sessionIdentifier] : nil));
}

- (void)updatePromptIdleTimerAssertion
{
  BOOL viewVisible;
  BOOL shouldAssert;

  viewVisible = NO;
  if ([self isViewLoaded]) {
    viewVisible = ([[self view] window] != nil) ? YES : NO;
  }

  shouldAssert = (![self tearingDown] &&
                  viewVisible &&
                  [self sessionPromptIsInFlight]) ? YES : NO;
  [self setPromptIdleTimerAssertionEnabled:shouldAssert];
}

- (NSTimeInterval)keyboardDuration:(NSDictionary *)userInfo
{
  NSNumber *duration;

  duration = [userInfo objectForKey:UIKeyboardAnimationDurationUserInfoKey];
  return [duration isKindOfClass:[NSNumber class]] ? [duration doubleValue] : 0.25;
}

- (UIViewAnimationCurve)keyboardCurve:(NSDictionary *)userInfo
{
  NSNumber *curve;

  curve = [userInfo objectForKey:UIKeyboardAnimationCurveUserInfoKey];
  return [curve isKindOfClass:[NSNumber class]]
    ? (UIViewAnimationCurve)[curve integerValue]
    : UIViewAnimationCurveEaseInOut;
}

- (void)keyboardWillShow:(NSNotification *)notification
{
  NSDictionary *userInfo;
  NSValue *keyboardValue;
  CGRect keyboardFrame;
  CGRect convertedFrame;
  CGFloat bottom;
  CGFloat maxBottom;

  userInfo = [notification userInfo];
  keyboardValue = [userInfo objectForKey:UIKeyboardFrameEndUserInfoKey];
  if (![keyboardValue isKindOfClass:[NSValue class]]) {
    return;
  }

  keyboardFrame = [keyboardValue CGRectValue];
  convertedFrame = [[self view] convertRect:keyboardFrame fromView:nil];
  bottom = convertedFrame.origin.y;
  maxBottom = [[self view] bounds].size.height;
  if (bottom > maxBottom) {
    bottom = maxBottom;
  }

  [self setComposeBarBottomY:bottom];
  [self setComposing:YES];
  [self relayoutComposeBarAnimated:YES
                              curve:[self keyboardCurve:userInfo]
                           duration:[self keyboardDuration:userInfo]];
  [self scrollWebViewToBottom];
}

- (void)keyboardWillHide:(NSNotification *)notification
{
  NSDictionary *userInfo;

  userInfo = [notification userInfo];
  [self setComposeBarBottomY:[[self view] bounds].size.height];
  [self setComposing:NO];
  [self relayoutComposeBarAnimated:YES
                              curve:[self keyboardCurve:userInfo]
                           duration:[self keyboardDuration:userInfo]];
}

- (void)relayoutComposeBarAnimated:(BOOL)animated
                              curve:(UIViewAnimationCurve)curve
                           duration:(NSTimeInterval)duration
{
  CGFloat width;
  CGFloat height;
  CGFloat barTop;

  if (([self sendBar] == nil) || ([self webView] == nil)) {
    return;
  }

  [[self sendBar] setComposing:[self composing]];
  width = [[self view] bounds].size.width;
  height = [[self sendBar] preferredHeight];
  barTop = [self composeBarBottomY] - height;
  if (barTop < 0.0f) {
    barTop = 0.0f;
  }

  if (animated && (duration > 0.0)) {
    [UIView beginAnimations:nil context:NULL];
    [UIView setAnimationDuration:duration];
    [UIView setAnimationCurve:curve];
  }

  [[self sendBar] setFrame:CGRectMake(0.0f, barTop, width, height)];
  [[self webView] setFrame:CGRectMake(0.0f, 0.0f, width, barTop)];
  [[self sendBar] layoutIfNeeded];

  if (animated && (duration > 0.0)) {
    [UIView commitAnimations];
  }
}

- (void)scrollWebViewToBottom
{
  if ([self tearingDown] || ([self webView] == nil)) {
    return;
  }
  [[self webView] stringByEvaluatingJavaScriptFromString:
    @"window.scrollTo(0, document.body.scrollHeight);"];
}

- (void)setWebViewScrollsToTop:(BOOL)scrollsToTop
{
  UIScrollView *scrollView;

  scrollView = [[self webView] XP_scrollView];
  if (scrollsToTop && (scrollView != nil)) {
    StrappySetScrollsToTopOwnerInView([self view], scrollView);
  } else {
    StrappySetScrollsToTopOwnerInView([self view], nil);
  }
}

- (void)updateTitleFromSession
{
  NSDictionary *summary;

  summary = [[self session] cachedSummary];
  if (![summary isKindOfClass:[NSDictionary class]]) {
    summary = [[self session] summaryWithError:nil];
  }
  [self setTitle:StrappyTitleForSessionSummary(summary)];
}

- (void)clearRequestState
{
  [self cancelPendingStreamEventFlush];
}

- (void)reloadWithSession:(StrappySession *)session
{
  StrappySession *oldSession;
  BOOL sessionChanged;
  BOOL studyLocked;

  if ([self tearingDown]) {
    return;
  }

  if (![session isKindOfClass:[StrappySession class]]) {
    session = nil;
  }

  oldSession = [self session];
  sessionChanged = (oldSession != session) ? YES : NO;
  if (sessionChanged) {
    [self clearRequestState];
    if (oldSession != nil) {
      [[NSNotificationCenter defaultCenter] removeObserver:self
                                                      name:StrappySessionPromptDidStartNotification
                                                    object:oldSession];
      [[NSNotificationCenter defaultCenter] removeObserver:self
                                                      name:StrappySessionPromptDidFinishNotification
                                                    object:oldSession];
      [[NSNotificationCenter defaultCenter] removeObserver:self
                                                      name:StrappySessionStreamEventNotification
                                                    object:oldSession];
    }
    [self setSession:session];
    if (session != nil) {
      [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(sessionPromptDidStart:)
               name:StrappySessionPromptDidStartNotification
             object:session];
      [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(sessionPromptDidFinish:)
               name:StrappySessionPromptDidFinishNotification
             object:session];
      [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(sessionStreamEvent:)
               name:StrappySessionStreamEventNotification
             object:session];
    }
  }

  [self updateTitleFromSession];
  studyLocked = (session != nil) && [session isDatabaseStudySession];
  [[self sendBar] setEnabled:((session != nil) && !studyLocked)];
  [[self sendBar] setStudyLocked:studyLocked];
  [self updateSendingStateFromSession];
  [self updatePromptIdleTimerAssertion];
  [[self sendBar] setWebProvider:(session != nil) ?
    [session webProvider] : StrappyWebProviderNone];
  [[self sendBar] setBashEnabled:(session != nil) ?
    [session bashEnabled] : NO];
  [[self sendBar] reloadOptionsMenu];
  if (([self webView] != nil) &&
      (sessionChanged || ![self webViewContentLoaded])) {
    [self reloadContent];
  }
}

- (BOOL)sessionPromptIsInFlight
{
  NSNumber *identifier;

  if ([self session] == nil) {
    return NO;
  }
  if ([[self session] isPromptInFlight]) {
    return YES;
  }

  identifier = [[self session] sessionIdentifier];
  return [StrappySession isPromptInFlightForSessionIdentifier:identifier];
}

- (void)updateSendingStateFromSession
{
  BOOL inFlight;

  inFlight = [self sessionPromptIsInFlight];
  [self setSending:inFlight ? YES : NO];
  [[self sendBar] setSending:[self sending]];
  [[self sendBar] setCancellationRequested:
    ([self sending] && [self promptCancellationRequested]) ? YES : NO];
  [self updatePromptIdleTimerAssertion];
}

- (BOOL)canSendCurrentPrompt
{
  if (([self session] == nil) || [self sending] ||
      [[self session] isDatabaseStudySession] ||
      [self sessionPromptIsInFlight]) {
    return NO;
  }
  return [[self sendBar] canSendCurrentPrompt];
}

- (void)sendCurrentPrompt:(id)sender
{
  [[self sendBar] performSend:sender];
}

- (BOOL)canCancelCurrentPrompt
{
  if (([self session] == nil) || ![self sessionPromptIsInFlight] ||
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
  [self promptSendViewControllerDidCancelPrompt:[self sendBar]];
}

- (NSArray *)availableModels
{
  NSArray *models;

  models = [StrappySession allowedOpenRouterModelCatalogWithError:nil];
  return [models isKindOfClass:[NSArray class]] ? models : [NSArray array];
}

- (NSArray *)availableAssistantSets
{
  NSArray *assistantSets;

  assistantSets = [StrappySession assistantSetCatalog];
  return [assistantSets isKindOfClass:[NSArray class]] ?
    assistantSets : [NSArray array];
}

- (NSArray *)assistantSetsForPromptSendViewController:
    (PromptSendViewController *)controller
{
  (void)controller;
  return [self availableAssistantSets];
}

- (NSString *)selectedAssistantSetIdentifier
{
  NSString *identifier;

  if ([self session] == nil) {
    return @"";
  }
  identifier = [[self session] assistantSetIdentifier];
  return [identifier isKindOfClass:[NSString class]] ? identifier : @"";
}

- (NSString *)selectedAssistantSetIdentifierForPromptSendViewController:
    (PromptSendViewController *)controller
{
  (void)controller;
  return [self selectedAssistantSetIdentifier];
}

- (BOOL)setSelectedAssistantSetIdentifier:(NSString *)assistantSetIdentifier
{
  BOOL changed;

  changed = [self promptSendViewController:[self sendBar]
            setSelectedAssistantSetIdentifier:assistantSetIdentifier];
  return changed;
}

- (BOOL)promptSendViewController:(PromptSendViewController *)controller
  setSelectedAssistantSetIdentifier:(NSString *)assistantSetIdentifier
{
  NSError *error;

  (void)controller;
  error = nil;
  if (![[self session] setAssistantSetIdentifier:assistantSetIdentifier
                                           error:&error]) {
    NSString *message;

    message = [error localizedDescription];
    if ([message length] == 0U) {
      message = NSLocalizedString(@"Your changes could not be saved.", nil);
    }
    [self setStatusText:message];
    [self showMessage:message
                title:NSLocalizedString(@"Failed to Save Changes", nil)];
    return NO;
  }
  [self setStatusText:nil];
  [[self sendBar] reloadOptionsMenu];
  return YES;
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

  if ([self session] == nil) {
    return @"";
  }

  modelIdentifier =
    [[self session] selectedOpenRouterModelIdentifierWithError:nil];
  return [modelIdentifier isKindOfClass:[NSString class]] ? modelIdentifier : @"";
}

- (NSString *)selectedModelIdentifierForPromptSendViewController:
    (PromptSendViewController *)controller
{
  (void)controller;
  return [self selectedModelIdentifier];
}

- (BOOL)canSelectModel
{
  if (([self session] == nil) || [self sending] ||
      [[self session] isDatabaseStudySession] ||
      [self sessionPromptIsInFlight]) {
    return NO;
  }
  return YES;
}

- (BOOL)setSelectedModelIdentifier:(NSString *)modelIdentifier
{
  BOOL changed;

  changed = [self promptSendViewController:[self sendBar]
                 setSelectedModelIdentifier:modelIdentifier];
  if (changed) {
    [[self sendBar] reloadOptionsMenu];
  }
  return changed;
}

- (BOOL)promptSendViewController:(PromptSendViewController *)controller
        setSelectedModelIdentifier:(NSString *)modelIdentifier
{
  NSError *error;

  (void)controller;
  if (![self canSelectModel]) {
    return NO;
  }

  error = nil;
  if (![[self session] setSelectedOpenRouterModelIdentifier:modelIdentifier
                                                      error:&error]) {
    NSString *message;

    message = [error localizedDescription];
    if ([message length] == 0U) {
      message = NSLocalizedString(@"Your changes could not be saved.", nil);
    }
    [self setStatusText:message];
    [self showMessage:message
                title:NSLocalizedString(@"Failed to Save Changes", nil)];
    return NO;
  }

  [self setStatusText:nil];
  [[self sendBar] reloadOptionsMenu];
  return YES;
}

- (BOOL)promptSendViewController:(PromptSendViewController *)controller
                  setWebProvider:(NSString *)webProvider
{
  NSError *error;

  (void)controller;
  if (![self canSelectModel]) {
    return NO;
  }

  error = nil;
  if (![[self session] setWebProvider:webProvider error:&error]) {
    NSString *message;

    message = [error localizedDescription];
    if ([message length] == 0U) {
      message = NSLocalizedString(@"Your changes could not be saved.", nil);
    }
    [self setStatusText:message];
    [[self sendBar] setWebProvider:[[self session] webProvider]];
    [self showMessage:message
                title:NSLocalizedString(@"Failed to Save Changes", nil)];
    return NO;
  }

  [[self sendBar] setWebProvider:[[self session] webProvider]];
  [self setStatusText:nil];
  return YES;
}

- (BOOL)promptSendViewController:(PromptSendViewController *)controller
                  setBashEnabled:(BOOL)enabled
{
  NSError *error;

  (void)controller;
  if (![self canSelectModel]) {
    return NO;
  }

  error = nil;
  if (![[self session] setBashEnabled:enabled error:&error]) {
    NSString *message;

    message = [error localizedDescription];
    if ([message length] == 0U) {
      message = NSLocalizedString(@"Your changes could not be saved.", nil);
    }
    [self setStatusText:message];
    [[self sendBar] setBashEnabled:[[self session] bashEnabled]];
    [self showMessage:message
                title:NSLocalizedString(@"Failed to Save Changes", nil)];
    return NO;
  }

  [[self sendBar] setBashEnabled:[[self session] bashEnabled]];
  [self setStatusText:nil];
  return YES;
}

- (NSString *)workingDirectoryForPromptSendViewController:
    (PromptSendViewController *)controller
{
  NSString *workingDirectory;

  (void)controller;
  workingDirectory = ([self session] != nil) ?
    [[self session] workingDirectoryWithError:nil] : nil;
  return [workingDirectory isKindOfClass:[NSString class]]
    ? workingDirectory
    : @"";
}

- (BOOL)promptSendViewController:(PromptSendViewController *)controller
             setWorkingDirectory:(NSString *)workingDirectory
{
  NSError *error;

  (void)controller;
  if (![self canSelectModel]) {
    return NO;
  }

  error = nil;
  if (![[self session] setWorkingDirectory:workingDirectory error:&error]) {
    NSString *message;

    message = [error localizedDescription];
    if ([message length] == 0U) {
      message = NSLocalizedString(@"Your changes could not be saved.", nil);
    }
    [self setStatusText:message];
    [self showMessage:message
                title:NSLocalizedString(@"Failed to Save Changes", nil)];
    return NO;
  }

  [self setStatusText:nil];
  return YES;
}

- (void)reloadContent
{
  NSString *path;

  if ([self tearingDown]) {
    return;
  }

  path = [self writeCurrentHTML];
  if ([path length] == 0U) {
    return;
  }

  [self setWebViewContentLoaded:YES];
  [[self webView] loadRequest:
    [NSURLRequest requestWithURL:[NSURL fileURLWithPath:path]]];
}

- (NSString *)writeCurrentHTML
{
  NSString *filename;
  NSString *path;
  NSString *html;
  NSString *errorText;
  NSNumber *identifier;
  NSUInteger count;

  if (!StrappyEnsureDirectory([self htmlDirectoryPath])) {
    return nil;
  }
  if ([self session] == nil) {
    return nil;
  }

  identifier = [[self session] sessionIdentifier];
  filename = [NSString stringWithFormat:@"session-%@.html",
    ([identifier isKindOfClass:[NSNumber class]] ? [identifier stringValue] : @"none")];
  path = [[self htmlDirectoryPath] stringByAppendingPathComponent:filename];

  errorText = ([[self statusText] length] > 0U) ? [self statusText] : nil;
  count = 0U;
  html = [[self session]
    webViewMessagesPageHTMLWithErrorText:errorText
                            messageCount:&count
                                   error:nil];
  [self setNewestRenderedMessageCount:count];
  if (![html isKindOfClass:[NSString class]] ||
      ![html writeToFile:path
              atomically:YES
                encoding:NSUTF8StringEncoding
                   error:nil]) {
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
  if (![self sending]) {
    return;
  }
  [self setPromptCancellationRequested:YES];
  [[self sendBar] setCancellationRequested:YES];
}

- (void)promptSendViewControllerDidChangeHeight:
    (PromptSendViewController *)controller
{
  (void)controller;
  [self relayoutComposeBarAnimated:YES
                              curve:UIViewAnimationCurveEaseInOut
                           duration:0.2];
  [self scrollWebViewToBottom];
}

- (void)setPromptCancellationRequested:(BOOL)requested
{
  @synchronized(self) {
    [self setCancelPromptRequested:requested ? YES : NO];
  }
  if (requested && ([self session] != nil)) {
    [[self session] cancelPrompt];
  }
}

- (BOOL)promptCancellationRequested
{
  BOOL requested;

  @synchronized(self) {
    requested = [self cancelPromptRequested];
  }
  if (!requested && ([self session] != nil)) {
    requested = [[self session] promptCancellationRequested];
  }
  return requested;
}

- (void)beginSendingPrompt:(NSString *)prompt
{
  NSError *startError;
  BOOL didStartPrompt;

  if ([self tearingDown]) {
    return;
  }
  if ([self sending]) {
    return;
  }
  if (([self session] == nil) || [[self session] isDatabaseStudySession] ||
      [self sessionPromptIsInFlight]) {
    return;
  }
  if (![prompt isKindOfClass:[NSString class]] || ([prompt length] == 0U)) {
    return;
  }

  [self logLifecycleEvent:@"beginSendingPrompt"];
  [self setSending:YES];
  [self setPromptCancellationRequested:NO];
  [self setStatusText:nil];
  [[self sendBar] setSending:YES];
  [[self sendBar] setCancellationRequested:NO];
  [self clearRequestState];

  startError = nil;
  didStartPrompt = [[self session] beginResponsesPrompt:prompt
                                                   context:nil
                                                     error:&startError];
  if (!didStartPrompt) {
    NSString *errorMessage;

    errorMessage = [startError localizedDescription];
    if ([errorMessage length] == 0U) {
      errorMessage = NSLocalizedString(@"Prompt failed.", nil);
    }
    [self sendPromptDidFinish:
      [NSDictionary dictionaryWithObject:errorMessage forKey:@"error"]];
  }
}

- (void)sessionPromptDidStart:(NSNotification *)notification
{
  if ([self tearingDown]) {
    return;
  }
  if ([notification object] != [self session]) {
    return;
  }
  [self logLifecycleEvent:@"sessionPromptDidStart"];
  [self updateSendingStateFromSession];
  [self updatePromptIdleTimerAssertion];
}

- (void)modelCatalogDidChange:(NSNotification *)notification
{
  (void)notification;
  if ([self tearingDown]) {
    return;
  }
  [[self sendBar] reloadOptionsMenu];
}

- (void)sessionStreamEvent:(NSNotification *)notification
{
  NSDictionary *event;
  NSString *streamEvent;

  if ([self tearingDown]) {
    return;
  }

  if ([notification object] != [self session]) {
    return;
  }

  event = [notification userInfo];
  if (![event isKindOfClass:[NSDictionary class]]) {
    return;
  }

  [self updateSendingStateFromSession];
  streamEvent = [event objectForKey:@"stream_event"];
  if ([streamEvent isEqualToString:@"ledger_changed"]) {
    [self logLifecycleEvent:@"sessionLedgerDidChange"];
    if (![self appendNewMessagesToWebView]) {
      NSLog(@"StrappyResponses could not append committed ledger rows for session %@",
            [[[self session] sessionIdentifier] description]);
    }
    return;
  }

  (void)[self queueJavaScriptForStreamEvent:event];
}

- (void)sessionPromptDidFinish:(NSNotification *)notification
{
  if ([self tearingDown]) {
    return;
  }
  if ([notification object] != [self session]) {
    return;
  }
  [self logLifecycleEvent:@"sessionPromptDidFinish begin"];
  [self flushPendingStreamEvents];
  [self sendPromptDidFinish:[notification userInfo]];
  [self updatePromptIdleTimerAssertion];
  [self logLifecycleEvent:@"sessionPromptDidFinish end"];
}

- (void)sendPromptDidFinish:(NSDictionary *)result
{
  NSDictionary *sessionSummary;
  NSString *errorMessage;

  [self setPromptCancellationRequested:NO];
  [self updateSendingStateFromSession];

  sessionSummary = [result objectForKey:@"session"];
  if ([sessionSummary isKindOfClass:[NSDictionary class]]) {
    [self setStatusText:nil];
    [self updateTitleFromSession];
  } else {
    errorMessage = [result objectForKey:@"error"];
    if (![errorMessage isKindOfClass:[NSString class]] ||
        ([errorMessage length] == 0U)) {
      errorMessage = NSLocalizedString(@"Prompt failed.", nil);
    }
    [self setStatusText:errorMessage];
  }

  [self clearRequestState];
  [self updateSendingStateFromSession];
  [self appendNewMessagesToWebView];
}

- (NSString *)javaScriptForStreamEvent:(NSDictionary *)event
{
  NSString *javaScript;

  if (([self session] == nil) || ![event isKindOfClass:[NSDictionary class]]) {
    return @"";
  }

  javaScript = [[self session] webViewJavaScriptForStreamEvent:event error:nil];
  return [javaScript isKindOfClass:[NSString class]] ? javaScript : @"";
}

- (BOOL)queueJavaScriptForStreamEvent:(NSDictionary *)event
{
  NSString *javaScript;

  if (![event isKindOfClass:[NSDictionary class]]) {
    return NO;
  }

  javaScript = [self javaScriptForStreamEvent:event];
  if ([javaScript length] == 0U) {
    return NO;
  }

  if ([self pendingStreamJavaScript] == nil) {
    [self setPendingStreamJavaScript:[NSMutableString string]];
  }
  [[self pendingStreamJavaScript] appendString:javaScript];
  [self schedulePendingStreamEventFlush];
  return YES;
}

- (void)schedulePendingStreamEventFlush
{
  if ([self streamEventFlushTimer] != nil) {
    return;
  }

  [self setStreamEventFlushTimer:
    [NSTimer scheduledTimerWithTimeInterval:kStrappyStreamEventFlushInterval
                                     target:self
                                   selector:@selector(streamEventFlushTimerDidFire:)
                                   userInfo:nil
                                    repeats:NO]];
}

- (void)streamEventFlushTimerDidFire:(NSTimer *)timer
{
  if ([self tearingDown]) {
    return;
  }
  if ([self streamEventFlushTimer] == timer) {
    [[self streamEventFlushTimer] invalidate];
    [self setStreamEventFlushTimer:nil];
  }
  [self flushPendingStreamEvents];
}

- (void)flushPendingStreamEvents
{
  NSString *batchJavaScript;

  if ([self tearingDown]) {
    [self setPendingStreamJavaScript:nil];
    return;
  }

  if ([[self pendingStreamJavaScript] length] == 0U) {
    return;
  }

  if ([self streamEventFlushTimer] != nil) {
    [[self streamEventFlushTimer] invalidate];
    [self setStreamEventFlushTimer:nil];
  }

  batchJavaScript =
    [StrappySession webViewBatchedJavaScriptForJavaScript:
      [self pendingStreamJavaScript]];
  [self setPendingStreamJavaScript:nil];

  if ([batchJavaScript length] > 0U) {
    if ([[UIApplication sharedApplication] applicationState] !=
        UIApplicationStateActive) {
      [self logLifecycleEvent:@"flushPendingStreamEvents non-active"];
    }
    [[self webView] stringByEvaluatingJavaScriptFromString:batchJavaScript];
  }
}

- (void)cancelPendingStreamEventFlush
{
  if ([self streamEventFlushTimer] != nil) {
    [[self streamEventFlushTimer] invalidate];
    [self setStreamEventFlushTimer:nil];
  }
  [self setPendingStreamJavaScript:nil];
}

- (BOOL)appendNewMessagesToWebView
{
  NSUInteger count;
  NSUInteger start;
  NSString *javaScript;

  if ([self session] == nil) {
    return NO;
  }

  start = [self newestRenderedMessageCount];
  count = 0U;
  javaScript = [[self session]
    webViewAppendMessagesJavaScriptFromIndex:start
                                messageCount:&count
                                       error:nil];
  if (![javaScript isKindOfClass:[NSString class]]) {
    return NO;
  }
  if (start > count) {
    return NO;
  }
  if (start == count) {
    return YES;
  }

  if ([javaScript length] == 0U) {
    return NO;
  }

  [self flushPendingStreamEvents];
  [[self webView] stringByEvaluatingJavaScriptFromString:javaScript];
  [self setNewestRenderedMessageCount:count];
  return YES;
}

- (BOOL)webView:(UIWebView *)webView
shouldStartLoadWithRequest:(NSURLRequest *)request
 navigationType:(UIWebViewNavigationType)navigationType
{
  NSURL *url;
  NSString *scheme;

  (void)webView;
  (void)navigationType;
  if ([self tearingDown]) {
    return NO;
  }
  url = [request URL];
  scheme = [url scheme];
  if ([scheme isEqualToString:@"strappy-action"]) {
    return NO;
  }
  return YES;
}

- (void)webViewDidFinishLoad:(UIWebView *)webView
{
  (void)webView;
  if ([self tearingDown]) {
    return;
  }
  [self logLifecycleEvent:@"webViewDidFinishLoad"];
  [[self webView] XP_setBackgroundTransparent];
  [self setWebViewScrollsToTop:YES];
  [self scrollWebViewToBottom];
}

- (void)showError:(NSError *)error title:(NSString *)title
{
  NSString *message;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = NSLocalizedString(@"An unknown error occurred.", nil);
  }
  [self showMessage:message title:title];
}

- (void)showMessage:(NSString *)message title:(NSString *)title
{
  UIAlertView *alert;

  alert = [[UIAlertView alloc] initWithTitle:title
                                     message:message
                                    delegate:nil
                           cancelButtonTitle:NSLocalizedString(@"OK", nil)
                           otherButtonTitles:nil];
  [alert show];
}

- (void)dealloc
{
  [self logLifecycleEvent:@"dealloc"];
  [self setPromptIdleTimerAssertionEnabled:NO];
  [self prepareForRemoval];
}

@end
