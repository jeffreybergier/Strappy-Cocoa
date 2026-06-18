#import "MessageListViewController.h"
#import "StrappySession.h"

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

static NSString *StrappyHTMLEscape(NSString *input)
{
  NSMutableString *output;
  NSUInteger index;

  if (![input isKindOfClass:[NSString class]]) {
    return @"";
  }

  output = [NSMutableString stringWithCapacity:[input length]];
  for (index = 0U; index < [input length]; index++) {
    unichar character = [input characterAtIndex:index];
    if (character == '&') {
      [output appendString:@"&amp;"];
    } else if (character == '<') {
      [output appendString:@"&lt;"];
    } else if (character == '>') {
      [output appendString:@"&gt;"];
    } else if (character == '"') {
      [output appendString:@"&quot;"];
    } else {
      [output appendFormat:@"%C", character];
    }
  }

  return output;
}

static NSString *StrappyJavaScriptStringLiteral(NSString *input)
{
  NSMutableString *output;
  NSUInteger index;

  if (![input isKindOfClass:[NSString class]]) {
    input = @"";
  }

  output = [NSMutableString stringWithString:@"'"];
  for (index = 0U; index < [input length]; index++) {
    unichar character = [input characterAtIndex:index];
    if (character == '\\') {
      [output appendString:@"\\\\"];
    } else if (character == '\'') {
      [output appendString:@"\\'"];
    } else if (character == '\n') {
      [output appendString:@"\\n"];
    } else if (character == '\r') {
      [output appendString:@"\\r"];
    } else if (character == '\t') {
      [output appendString:@"\\t"];
    } else if (character < 32) {
      [output appendFormat:@"\\u%04x", character];
    } else {
      [output appendFormat:@"%C", character];
    }
  }
  [output appendString:@"'"];
  return output;
}

static NSString *StrappyRoleLabel(NSString *role)
{
  if ([role isEqualToString:@"assistant"]) {
    return NSLocalizedString(@"Agent", nil);
  }
  return NSLocalizedString(@"You", nil);
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

static NSString *StrappySavedMessageElementIdentifier(NSDictionary *message)
{
  long long messageId;

  messageId = StrappyMessageNumericIdentifier(message);
  if (messageId <= 0LL) {
    return @"saved-0";
  }
  return [NSString stringWithFormat:@"saved-%lld", messageId];
}

static NSString *StrappyStatusHTML(NSString *text, BOOL retry)
{
  NSMutableString *html;

  html = [NSMutableString string];
  [html appendString:StrappyHTMLEscape(text)];
  if (retry) {
    [html appendString:@" &middot; <a href=\"strappy-action://retry\">"];
    [html appendString:StrappyHTMLEscape(NSLocalizedString(@"Retry", nil))];
    [html appendString:@"</a>"];
  }
  return html;
}

static NSString *StrappyMessageHTML(NSDictionary *message,
                                    NSString *elementIdentifier,
                                    NSString *state,
                                    NSString *statusHTML)
{
  NSMutableString *html;
  NSString *role;
  NSString *text;
  NSString *createdAt;
  NSNumber *httpStatus;
  NSString *stateClass;

  role = [message objectForKey:@"role"];
  text = [message objectForKey:@"text"];
  createdAt = [message objectForKey:@"created_at"];
  httpStatus = [message objectForKey:@"http_status"];

  if (![role isKindOfClass:[NSString class]]) {
    role = @"assistant";
  }
  if (![text isKindOfClass:[NSString class]]) {
    text = @"";
  }
  if (![createdAt isKindOfClass:[NSString class]]) {
    createdAt = @"";
  }
  if (![elementIdentifier isKindOfClass:[NSString class]] ||
      ([elementIdentifier length] == 0U)) {
    elementIdentifier = StrappySavedMessageElementIdentifier(message);
  }

  stateClass = @"";
  if ([state isKindOfClass:[NSString class]] && ([state length] > 0U)) {
    stateClass = [NSString stringWithFormat:@" state-%@", state];
  } else if ([httpStatus isKindOfClass:[NSNumber class]] &&
             ([httpStatus longValue] >= 400L)) {
    stateClass = @" state-error";
    statusHTML = StrappyStatusHTML(
      [NSString stringWithFormat:NSLocalizedString(@"HTTP %@", nil), httpStatus],
      NO);
  }

  html = [NSMutableString string];
  [html appendFormat:@"<div id=\"%@\" class=\"row %@%@\">",
    StrappyHTMLEscape(elementIdentifier),
    StrappyHTMLEscape(role),
    stateClass];
  [html appendFormat:@"<div class=\"role\">%@</div>",
    StrappyHTMLEscape(StrappyRoleLabel(role))];
  [html appendFormat:@"<div class=\"bubble\">%@</div>",
    StrappyHTMLEscape(text)];
  if ([statusHTML length] > 0U) {
    [html appendFormat:@"<div class=\"meta status\">%@</div>", statusHTML];
  } else if ([createdAt length] > 0U) {
    [html appendFormat:@"<div class=\"meta\">%@</div>",
      StrappyHTMLEscape(createdAt)];
  }
  [html appendString:@"</div>"];
  return html;
}

static NSString *StrappyPendingMessageHTML(NSString *prompt,
                                           NSString *elementIdentifier,
                                           NSString *state,
                                           NSString *statusHTML)
{
  NSDictionary *message;

  message = [NSDictionary dictionaryWithObjectsAndKeys:
    @"user", @"role",
    (prompt ? prompt : @""), @"text",
    @"", @"created_at",
    nil];
  return StrappyMessageHTML(message, elementIdentifier, state, statusHTML);
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
  return [NSString stringWithFormat:@"appendMessage(%@);",
    StrappyJavaScriptStringLiteral(messageHTML)];
}

static NSString *StrappyReplaceMessageJavaScript(NSString *elementIdentifier,
                                                 NSString *messageHTML)
{
  return [NSString stringWithFormat:@"replaceMessage(%@,%@);",
    StrappyJavaScriptStringLiteral(elementIdentifier),
    StrappyJavaScriptStringLiteral(messageHTML)];
}

static NSString *StrappySetMessageStateJavaScript(NSString *elementIdentifier,
                                                  NSString *statusHTML,
                                                  NSString *state)
{
  return [NSString stringWithFormat:@"setMessageState(%@,%@,%@);",
    StrappyJavaScriptStringLiteral(elementIdentifier),
    StrappyJavaScriptStringLiteral(statusHTML),
    StrappyJavaScriptStringLiteral(state)];
}

@interface MessageListViewController ()
- (void)sendPromptInBackground:(NSDictionary *)request;
- (void)sendPromptDidFinish:(NSDictionary *)result;
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
  [pendingMessageIdentifier_ release];
  pendingMessageIdentifier_ = nil;
  [pendingPrompt_ release];
  pendingPrompt_ = nil;
  [sendingSessionId_ release];
  sendingSessionId_ = nil;
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
  NSMutableString *html;
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

  html = [NSMutableString string];
  [html appendString:@"<!doctype html><html><head><meta charset=\"utf-8\">"];
  [html appendString:@"<style>"];
  [html appendString:@"html,body{margin:0;padding:0;background:#f4f4f4;color:#222;font:13px Helvetica,Arial,sans-serif;}"];
  [html appendString:@".page{padding:18px 22px 96px;}"];
  [html appendString:@".empty{margin:90px auto 0;max-width:520px;color:#777;text-align:center;line-height:1.45;}"];
  [html appendString:@"#messages{max-width:860px;margin:0 auto;}"];
  [html appendString:@".load-more{display:block;margin:0 auto 14px;padding:7px 10px;text-align:center;color:#2468a8;text-decoration:none;}"];
  [html appendString:@".row{margin:0 0 16px;clear:both;}"];
  [html appendString:@".role{font-size:11px;font-weight:bold;color:#666;text-transform:uppercase;margin:0 0 5px 2px;}"];
  [html appendString:@".bubble{display:block;max-width:72%;border:1px solid #d8d8d8;background:#fff;padding:12px 14px;line-height:1.45;white-space:pre-wrap;word-wrap:break-word;}"];
  [html appendString:@".assistant .bubble{background:#fcfcfc;}"];
  [html appendString:@".user .role,.user .meta{text-align:right;}"];
  [html appendString:@".user .bubble{margin-left:auto;background:#eef5ff;border-color:#c8d8ef;}"];
  [html appendString:@".meta{font-size:11px;color:#777;margin-top:6px;}"];
  [html appendString:@".state-pending .bubble{opacity:.72;}"];
  [html appendString:@".state-pending .status{color:#777;}"];
  [html appendString:@".state-error .bubble{border-color:#d99;background:#fff7f7;}"];
  [html appendString:@".state-error .status{color:#a22;}"];
  [html appendString:@".status a{color:#2468a8;text-decoration:none;}"];
  [html appendString:@"</style>"];
  [html appendString:@"<script>"];
  [html appendString:@"function byId(i){return document.getElementById(i);}"];
  [html appendString:@"function firstByClass(root,name){var n=root.getElementsByTagName('*');for(var i=0;i<n.length;i++){if((' '+n[i].className+' ').indexOf(' '+name+' ')>=0)return n[i];}return null;}"];
  [html appendString:@"function clearEmpty(){var e=byId('empty');if(e)e.style.display='none';}"];
  [html appendString:@"function nodesFromHTML(html){var d=document.createElement('div');d.innerHTML=html;return d;}"];
  [html appendString:@"function scrollBottom(){setTimeout(function(){window.scrollTo(0,document.body.scrollHeight);},0);}"];
  [html appendString:@"function appendMessage(html){clearEmpty();var m=byId('messages');if(!m)return;if(m.insertAdjacentHTML){m.insertAdjacentHTML('beforeend',html);}else{var d=nodesFromHTML(html);while(d.firstChild)m.appendChild(d.firstChild);}scrollBottom();}"];
  [html appendString:@"function replaceMessage(id,html){clearEmpty();var old=byId(id);if(!old){appendMessage(html);return;}var d=nodesFromHTML(html);if(d.firstChild)old.parentNode.replaceChild(d.firstChild,old);scrollBottom();}"];
  [html appendString:@"function prependMessages(html,hasMore){var m=byId('messages');if(!m)return;var d=nodesFromHTML(html);while(d.lastChild)m.insertBefore(d.lastChild,m.firstChild);var l=byId('load-more');if(l&&!hasMore)l.parentNode.removeChild(l);}"];
  [html appendString:@"function setMessageState(id,status,state){var r=byId(id);if(!r)return;r.className=r.className.replace(/\\sstate-[^\\s]+/g,'')+' state-'+state;var s=firstByClass(r,'status');if(!s){s=document.createElement('div');s.className='meta status';r.appendChild(s);}s.innerHTML=status;scrollBottom();}"];
  [html appendString:@"</script></head><body><div class=\"page\">"];

  if (start > 0U) {
    [html appendString:@"<a id=\"load-more\" class=\"load-more\" href=\"strappy-action://load-more\">"];
    [html appendString:StrappyHTMLEscape(NSLocalizedString(@"Show Earlier Messages", nil))];
    [html appendString:@"</a>"];
  }

  if (!hasMessages) {
    [html appendFormat:@"<div id=\"empty\" class=\"empty\">%@</div>",
      StrappyHTMLEscape(emptyText)];
  } else {
    [html appendString:@"<div id=\"empty\" class=\"empty\" style=\"display:none\"></div>"];
  }

  [html appendString:@"<div id=\"messages\">"];
  if (count > 0U) {
    [html appendString:StrappyMessagesHTMLForRange(messages, start, count)];
  }
  if (hasPending) {
    [html appendString:StrappyPendingMessageHTML(pendingPrompt_,
                                                pendingMessageIdentifier_,
                                                (sending_ ? @"pending" : @"error"),
                                                (sending_
                                                 ? StrappyStatusHTML(NSLocalizedString(@"Sending...", nil), NO)
                                                 : StrappyStatusHTML(NSLocalizedString(@"Failed to send.", nil), YES)))];
  }
  [html appendString:@"</div></div></body></html>"];
  return html;
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
  static unsigned long pendingCounter = 0UL;

  if (sending_) {
    return;
  }

  if (![prompt isKindOfClass:[NSString class]] || ([prompt length] == 0U)) {
    return;
  }

  sending_ = YES;
  [statusText_ release];
  statusText_ = nil;
  [sendController_ setSending:YES];

  if (!reuse || (pendingMessageIdentifier_ == nil)) {
    [self clearPendingMessageState];
    pendingCounter++;
    pendingMessageIdentifier_ =
      [[NSString stringWithFormat:@"pending-%lu", pendingCounter] retain];
    pendingPrompt_ = [prompt copy];
    sendingSessionId_ = [sessionId_ retain];
    [self pushJavaScript:
      StrappyAppendMessageJavaScript(
        StrappyPendingMessageHTML(prompt,
                                  pendingMessageIdentifier_,
                                  @"pending",
                                  StrappyStatusHTML(NSLocalizedString(@"Sending...", nil), NO)))];
  } else {
    [self pushJavaScript:
      StrappySetMessageStateJavaScript(pendingMessageIdentifier_,
                                       StrappyStatusHTML(NSLocalizedString(@"Sending...", nil), NO),
                                       @"pending")];
  }

  request = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
    prompt, @"prompt",
    pendingMessageIdentifier_, @"pending_id",
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
  NSNumber *sessionId;
  NSNumber *previousLastIdentifier;
  NSNumber *created;

  pool = [[NSAutoreleasePool alloc] init];
  prompt = [request objectForKey:@"prompt"];
  if (![prompt isKindOfClass:[NSString class]]) {
    prompt = @"";
  }
  pendingIdentifier = [request objectForKey:@"pending_id"];
  if (![pendingIdentifier isKindOfClass:[NSString class]]) {
    pendingIdentifier = @"";
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
  [result setObject:previousLastIdentifier forKey:@"previous_last_id"];
  [result setObject:created forKey:@"created"];

  error = nil;
  session = [StrappySession submitPrompt:prompt
                     inSessionIdentifier:sessionId
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

- (void)sendPromptDidFinish:(NSDictionary *)result
{
  NSDictionary *session;
  NSString *errorMessage;
  NSString *pendingIdentifier;
  NSNumber *previousLastIdentifier;
  NSNumber *created;
  NSNumber *resultSessionId;
  BOOL pendingIsCurrent;

  sending_ = NO;
  [sendController_ setSending:NO];

  pendingIdentifier = [result objectForKey:@"pending_id"];
  if (![pendingIdentifier isKindOfClass:[NSString class]]) {
    pendingIdentifier = @"";
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
    NSUInteger index;
    BOOL replacedPending;
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
    replacedPending = NO;
    js = [NSMutableString string];
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

      if (!replacedPending) {
        [js appendString:
          StrappyReplaceMessageJavaScript(pendingIdentifier,
                                          StrappyMessageHTML(message, nil, nil, nil))];
        replacedPending = YES;
      } else {
        [js appendString:
          StrappyAppendMessageJavaScript(StrappyMessageHTML(message, nil, nil, nil))];
      }

      if (messageIdentifier > newestIdentifier) {
        newestIdentifier = messageIdentifier;
      }
    }

    if (replacedPending) {
      [self pushJavaScript:js];
      lastKnownMessageIdentifier_ = newestIdentifier;
      [self clearPendingMessageState];
    } else {
      [self clearPendingMessageState];
      [self reloadContent];
    }
    return;
  }

  errorMessage = [result objectForKey:@"error"];
  if (![errorMessage isKindOfClass:[NSString class]] || ([errorMessage length] == 0U)) {
    errorMessage = NSLocalizedString(@"Prompt failed.", nil);
  }

  if (pendingIsCurrent) {
    [self pushJavaScript:
      StrappySetMessageStateJavaScript(pendingIdentifier,
                                       StrappyStatusHTML(errorMessage, YES),
                                       @"error")];
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

  js = [NSString stringWithFormat:@"prependMessages(%@,%@);",
    StrappyJavaScriptStringLiteral(html),
    (start > 0U ? @"true" : @"false")];
  [self pushJavaScript:js];
}

- (void)dealloc
{
  [htmlDirectoryPath_ release];
  [sessionId_ release];
  [sendController_ release];
  [statusText_ release];
  [pendingMessageIdentifier_ release];
  [pendingPrompt_ release];
  [sendingSessionId_ release];
  [super dealloc];
}

@end
