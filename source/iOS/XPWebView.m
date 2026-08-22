#import "XPWebView.h"

#import "XPUIKit.h"

static const NSInteger XPWKNavigationActionPolicyCancel = 0;
static const NSInteger XPWKNavigationActionPolicyAllow = 1;

static void XPWebViewSetObject(id target, SEL selector, id value)
{
  NSMethodSignature *signature;
  NSInvocation *invocation;
  __unsafe_unretained id valueArgument;

  signature = [target methodSignatureForSelector:selector];
  if (signature == nil) {
    return;
  }
  invocation = [NSInvocation invocationWithMethodSignature:signature];
  valueArgument = value;
  [invocation setTarget:target];
  [invocation setSelector:selector];
  [invocation setArgument:&valueArgument atIndex:2];
  [invocation invoke];
}

@interface XPWebView () <UIWebViewDelegate>
@property (nonatomic, strong) UIView *backingView;
@property (nonatomic, copy) NSURL *readAccessURL;
@property (nonatomic, strong) NSMutableArray *pendingJavaScripts;
@property (nonatomic, assign, getter=isDocumentLoading) BOOL documentLoading;
@property (nonatomic, assign, getter=isDocumentReady) BOOL documentReady;
@property (nonatomic, assign, getter=isJavaScriptEvaluationInFlight)
  BOOL javaScriptEvaluationInFlight;
@property (nonatomic, assign) NSUInteger javaScriptGeneration;
@property (nonatomic, assign, readwrite, getter=isUsingWKWebView)
  BOOL usingWKWebView;
- (BOOL)runtimeCanUseWKWebView;
- (void)drainPendingJavaScripts;
- (void)didFinishDocumentLoad;
- (void)didFailDocumentLoadWithError:(NSError *)error;
@end

@implementation XPWebView

- (id)initWithFrame:(CGRect)frame readAccessURL:(NSURL *)readAccessURL
{
  UIView *backingView;

  if (![readAccessURL isFileURL]) {
    return nil;
  }

  if ((self = [super initWithFrame:frame])) {
    [self setReadAccessURL:readAccessURL];
    [self setPendingJavaScripts:[NSMutableArray array]];
    [self setAutoresizingMask:UIViewAutoresizingFlexibleWidth |
                              UIViewAutoresizingFlexibleHeight];

    backingView = nil;
    if ([self runtimeCanUseWKWebView]) {
      Class configurationClass;
      Class webViewClass;
      id configuration;
      id allocatedWebView;
      __unsafe_unretained id initializedWebView;
      SEL selector;
      NSMethodSignature *signature;
      NSInvocation *invocation;
      CGRect frameArgument;
      __unsafe_unretained id configurationArgument;

      configurationClass = NSClassFromString(@"WKWebViewConfiguration");
      webViewClass = NSClassFromString(@"WKWebView");
      configuration = [[configurationClass alloc] init];
      allocatedWebView = [webViewClass alloc];
      selector = NSSelectorFromString(@"initWithFrame:configuration:");
      signature = [allocatedWebView methodSignatureForSelector:selector];
      invocation = (signature != nil) ?
        [NSInvocation invocationWithMethodSignature:signature] : nil;
      initializedWebView = nil;
      if (invocation != nil) {
        frameArgument = [self bounds];
        configurationArgument = configuration;
        [invocation setTarget:allocatedWebView];
        [invocation setSelector:selector];
        [invocation setArgument:&frameArgument atIndex:2];
        [invocation setArgument:&configurationArgument atIndex:3];
        [invocation invoke];
        [invocation getReturnValue:&initializedWebView];
      }
      backingView = initializedWebView;
      if (backingView != nil) {
        XPWebViewSetObject(backingView,
                           NSSelectorFromString(@"setNavigationDelegate:"),
                           self);
        [self setUsingWKWebView:YES];
      }
    }

    if (backingView == nil) {
      UIWebView *legacyWebView;

      legacyWebView = [[UIWebView alloc] initWithFrame:[self bounds]];
      [legacyWebView setDelegate:self];
      backingView = legacyWebView;
      [self setUsingWKWebView:NO];
    }

    [backingView setAutoresizingMask:UIViewAutoresizingFlexibleWidth |
                                     UIViewAutoresizingFlexibleHeight];
    [self addSubview:backingView];
    [self setBackingView:backingView];
  }
  return self;
}

- (BOOL)runtimeCanUseWKWebView
{
  Class configurationClass;
  Class webViewClass;

  configurationClass = NSClassFromString(@"WKWebViewConfiguration");
  webViewClass = NSClassFromString(@"WKWebView");
  if ((configurationClass == Nil) || (webViewClass == Nil)) {
    return NO;
  }
  return [webViewClass instancesRespondToSelector:
            NSSelectorFromString(@"initWithFrame:configuration:")] &&
         [webViewClass instancesRespondToSelector:
            NSSelectorFromString(@"loadFileURL:allowingReadAccessToURL:")] &&
         [webViewClass instancesRespondToSelector:
            NSSelectorFromString(@"evaluateJavaScript:completionHandler:")] &&
         [webViewClass instancesRespondToSelector:@selector(scrollView)];
}

- (void)loadFileURL:(NSURL *)fileURL
{
  if (![fileURL isFileURL]) {
    NSError *error;

    error = [NSError errorWithDomain:@"XPWebViewErrorDomain"
                                code:1
                            userInfo:[NSDictionary dictionaryWithObject:
      @"XPWebView requires a local file URL."
                                                               forKey:NSLocalizedDescriptionKey]];
    [self didFailDocumentLoadWithError:error];
    return;
  }

  [self setJavaScriptGeneration:[self javaScriptGeneration] + 1U];
  [self setJavaScriptEvaluationInFlight:NO];
  [[self pendingJavaScripts] removeAllObjects];
  [self setDocumentReady:NO];
  [self setDocumentLoading:YES];

  if ([self isUsingWKWebView]) {
    SEL selector;
    NSMethodSignature *signature;
    NSInvocation *invocation;
    __unsafe_unretained NSURL *fileURLArgument;
    __unsafe_unretained NSURL *readAccessURLArgument;

    selector = NSSelectorFromString(@"loadFileURL:allowingReadAccessToURL:");
    signature = [[self backingView] methodSignatureForSelector:selector];
    if (signature == nil) {
      NSError *error;

      error = [NSError errorWithDomain:@"XPWebViewErrorDomain"
                                  code:2
                              userInfo:[NSDictionary dictionaryWithObject:
        @"WKWebView cannot load scoped local files."
                                                                 forKey:NSLocalizedDescriptionKey]];
      [self didFailDocumentLoadWithError:error];
      return;
    }
    invocation = [NSInvocation invocationWithMethodSignature:signature];
    [invocation setTarget:[self backingView]];
    [invocation setSelector:selector];
    fileURLArgument = fileURL;
    readAccessURLArgument = [self readAccessURL];
    [invocation setArgument:&fileURLArgument atIndex:2];
    [invocation setArgument:&readAccessURLArgument atIndex:3];
    [invocation invoke];
    return;
  }

  [(UIWebView *)[self backingView] loadRequest:
    [NSURLRequest requestWithURL:fileURL]];
}

- (void)evaluateJavaScript:(NSString *)javaScript
{
  NSString *queuedJavaScript;

  if (![javaScript isKindOfClass:[NSString class]] ||
      ([javaScript length] == 0U)) {
    return;
  }
  queuedJavaScript = [javaScript copy];
  [[self pendingJavaScripts] addObject:queuedJavaScript];
  [self drainPendingJavaScripts];
}

- (void)drainPendingJavaScripts
{
  NSString *javaScript;

  if (![self isDocumentReady] || [self isDocumentLoading] ||
      [self isJavaScriptEvaluationInFlight] ||
      ([[self pendingJavaScripts] count] == 0U)) {
    return;
  }

  javaScript = [[self pendingJavaScripts] objectAtIndex:0];
  [[self pendingJavaScripts] removeObjectAtIndex:0];
  if (![self isUsingWKWebView]) {
    [(UIWebView *)[self backingView]
      stringByEvaluatingJavaScriptFromString:javaScript];
    [self drainPendingJavaScripts];
    return;
  }

  {
    NSUInteger generation;
    void (^completionHandler)(id, NSError *);
    SEL selector;
    NSMethodSignature *signature;
    NSInvocation *invocation;
    __unsafe_unretained NSString *javaScriptArgument;
    __unsafe_unretained id completionHandlerArgument;

    generation = [self javaScriptGeneration];
    [self setJavaScriptEvaluationInFlight:YES];
    completionHandler = ^(id result, NSError *error) {
      (void)result;
      if (generation != [self javaScriptGeneration]) {
        return;
      }
      [self setJavaScriptEvaluationInFlight:NO];
      if (error != nil) {
        NSLog(@"XPWebView JavaScript evaluation failed: %@",
              [error localizedDescription]);
      }
      [self drainPendingJavaScripts];
    };
    selector = NSSelectorFromString(@"evaluateJavaScript:completionHandler:");
    signature = [[self backingView] methodSignatureForSelector:selector];
    invocation = (signature != nil) ?
      [NSInvocation invocationWithMethodSignature:signature] : nil;
    if (invocation == nil) {
      [self setJavaScriptEvaluationInFlight:NO];
      return;
    }
    javaScriptArgument = javaScript;
    completionHandlerArgument = completionHandler;
    [invocation setTarget:[self backingView]];
    [invocation setSelector:selector];
    [invocation setArgument:&javaScriptArgument atIndex:2];
    [invocation setArgument:&completionHandlerArgument atIndex:3];
    [invocation invoke];
  }
}

- (void)stopLoading
{
  [self setJavaScriptGeneration:[self javaScriptGeneration] + 1U];
  [self setJavaScriptEvaluationInFlight:NO];
  [self setDocumentLoading:NO];
  [self setDocumentReady:NO];
  [[self pendingJavaScripts] removeAllObjects];
  [[self backingView] performSelector:@selector(stopLoading)];
}

- (UIScrollView *)scrollView
{
  if ([self isUsingWKWebView]) {
    return [[self backingView] performSelector:@selector(scrollView)];
  }
  return [(UIWebView *)[self backingView] XP_scrollView];
}

- (void)setBackgroundTransparent
{
  UIScrollView *scrollView;

  [self setOpaque:NO];
  [self setBackgroundColor:[UIColor clearColor]];
  [[self backingView] setOpaque:NO];
  [[self backingView] setBackgroundColor:[UIColor clearColor]];
  scrollView = [self scrollView];
  [scrollView setBackgroundColor:[UIColor clearColor]];
}

- (void)setVisibleFrame:(CGRect)frame
{
  UIEdgeInsets contentInset;
  UIEdgeInsets scrollIndicatorInsets;
  UIScrollView *scrollView;
  CGFloat bottomInset;

  scrollView = [self scrollView];
  if (scrollView == nil) {
    [self setFrame:frame];
    return;
  }

  bottomInset = CGRectGetMaxY([self frame]) - CGRectGetMaxY(frame);
  if (bottomInset < 0.0f) {
    bottomInset = 0.0f;
  }
  contentInset = [scrollView contentInset];
  contentInset.bottom = bottomInset;
  [scrollView setContentInset:contentInset];
  scrollIndicatorInsets = [scrollView scrollIndicatorInsets];
  scrollIndicatorInsets.bottom = bottomInset;
  [scrollView setScrollIndicatorInsets:scrollIndicatorInsets];
}

- (BOOL)shouldStartLoadWithRequest:(NSURLRequest *)request
{
  id<XPWebViewDelegate> delegate;

  delegate = [self delegate];
  if ([delegate respondsToSelector:
        @selector(xpWebView:shouldStartLoadWithRequest:)]) {
    return [delegate xpWebView:self shouldStartLoadWithRequest:request];
  }
  return YES;
}

- (void)didFinishDocumentLoad
{
  id<XPWebViewDelegate> delegate;

  [self setDocumentLoading:NO];
  [self setDocumentReady:YES];
  delegate = [self delegate];
  if ([delegate respondsToSelector:@selector(xpWebViewDidFinishLoad:)]) {
    [delegate xpWebViewDidFinishLoad:self];
  }
  [self drainPendingJavaScripts];
}

- (void)didFailDocumentLoadWithError:(NSError *)error
{
  id<XPWebViewDelegate> delegate;

  [self setJavaScriptGeneration:[self javaScriptGeneration] + 1U];
  [self setJavaScriptEvaluationInFlight:NO];
  [self setDocumentLoading:NO];
  [self setDocumentReady:NO];
  [[self pendingJavaScripts] removeAllObjects];
  delegate = [self delegate];
  if ([delegate respondsToSelector:
        @selector(xpWebView:didFailLoadWithError:)]) {
    [delegate xpWebView:self didFailLoadWithError:error];
  }
}

- (BOOL)webView:(UIWebView *)webView
shouldStartLoadWithRequest:(NSURLRequest *)request
 navigationType:(UIWebViewNavigationType)navigationType
{
  (void)webView;
  (void)navigationType;
  return [self shouldStartLoadWithRequest:request];
}

- (void)webViewDidFinishLoad:(UIWebView *)webView
{
  (void)webView;
  [self didFinishDocumentLoad];
}

- (void)webView:(UIWebView *)webView didFailLoadWithError:(NSError *)error
{
  (void)webView;
  [self didFailDocumentLoadWithError:error];
}

- (void)webView:(id)webView
decidePolicyForNavigationAction:(id)navigationAction
 decisionHandler:(void (^)(NSInteger))decisionHandler
{
  BOOL allow;

  (void)webView;
  allow = [self shouldStartLoadWithRequest:
    [navigationAction performSelector:@selector(request)]];
  decisionHandler(allow ? XPWKNavigationActionPolicyAllow :
                          XPWKNavigationActionPolicyCancel);
}

- (void)webView:(id)webView didFinishNavigation:(id)navigation
{
  (void)webView;
  (void)navigation;
  [self didFinishDocumentLoad];
}

- (void)webView:(id)webView
didFailProvisionalNavigation:(id)navigation
      withError:(NSError *)error
{
  (void)webView;
  (void)navigation;
  [self didFailDocumentLoadWithError:error];
}

- (void)webView:(id)webView
didFailNavigation:(id)navigation
      withError:(NSError *)error
{
  (void)webView;
  (void)navigation;
  [self didFailDocumentLoadWithError:error];
}

- (void)webViewWebContentProcessDidTerminate:(id)webView
{
  NSError *error;

  (void)webView;
  error = [NSError errorWithDomain:@"XPWebViewErrorDomain"
                              code:3
                          userInfo:[NSDictionary dictionaryWithObject:
    @"The WebView content process terminated."
                                                             forKey:NSLocalizedDescriptionKey]];
  [self didFailDocumentLoadWithError:error];
}

- (void)dealloc
{
  if ([self isUsingWKWebView]) {
    XPWebViewSetObject([self backingView],
                       NSSelectorFromString(@"setNavigationDelegate:"),
                       nil);
  } else {
    [(UIWebView *)[self backingView] setDelegate:nil];
  }
  [self setDelegate:nil];
}

@end
