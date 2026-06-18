#import "MessageListViewController.h"
#import "StrappySession.h"

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

static NSString *StrappyRoleLabel(NSString *role)
{
  if ([role isEqualToString:@"assistant"]) {
    return NSLocalizedString(@"Agent", nil);
  }
  return NSLocalizedString(@"You", nil);
}

@interface MessageListViewController ()
- (void)sendPromptInBackground:(NSDictionary *)request;
- (void)sendPromptDidFinish:(NSDictionary *)result;
- (NSString *)writeCurrentHTML;
- (NSString *)htmlForMessages:(NSArray *)messages error:(NSError *)error;
- (void)layoutWebViewAndPromptBar;
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
  [sendController_ setEnabled:!sending_];

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

- (void)reloadWithSession:(NSDictionary *)session
{
  NSNumber *identifier;

  identifier = [session objectForKey:@"id"];
  if (![identifier isKindOfClass:[NSNumber class]]) {
    identifier = nil;
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
  NSUInteger index;

  html = [NSMutableString string];
  [html appendString:@"<!doctype html><html><head><meta charset=\"utf-8\">"];
  [html appendString:@"<style>"];
  [html appendString:@"html,body{margin:0;padding:0;background:#f4f4f4;color:#222;font:13px Helvetica,Arial,sans-serif;}"];
  [html appendString:@".page{padding:24px 28px 96px;}"];
  [html appendString:@".empty{margin:90px auto 0;max-width:520px;color:#777;text-align:center;line-height:1.45;}"];
  [html appendString:@".row{max-width:780px;margin:0 auto 18px;clear:both;}"];
  [html appendString:@".role{font-size:11px;font-weight:bold;color:#666;text-transform:uppercase;margin:0 0 5px 2px;}"];
  [html appendString:@".bubble{border:1px solid #d8d8d8;background:#fff;padding:12px 14px;line-height:1.45;white-space:pre-wrap;}"];
  [html appendString:@".assistant .bubble{background:#fcfcfc;}"];
  [html appendString:@".user .bubble{background:#eef5ff;border-color:#c8d8ef;}"];
  [html appendString:@".meta{font-size:11px;color:#777;margin-top:6px;}"];
  [html appendString:@"</style></head><body><div class=\"page\">"];

  if ([statusText_ length] > 0U) {
    [html appendFormat:@"<div class=\"empty\">%@</div>", StrappyHTMLEscape(statusText_)];
  } else if (error != nil) {
    [html appendFormat:@"<div class=\"empty\">%@</div>",
      StrappyHTMLEscape([error localizedDescription])];
  } else if ((messages == nil) || ([messages count] == 0U)) {
    [html appendFormat:@"<div class=\"empty\">%@</div>",
      StrappyHTMLEscape(NSLocalizedString(@"New Session", nil))];
  } else {
    for (index = 0U; index < [messages count]; index++) {
      NSDictionary *message = [messages objectAtIndex:index];
      NSString *role = [message objectForKey:@"role"];
      NSString *text = [message objectForKey:@"text"];
      NSString *createdAt = [message objectForKey:@"created_at"];

      if (![role isKindOfClass:[NSString class]]) {
        role = @"assistant";
      }
      if (![text isKindOfClass:[NSString class]]) {
        text = @"";
      }
      if (![createdAt isKindOfClass:[NSString class]]) {
        createdAt = @"";
      }

      [html appendFormat:@"<div class=\"row %@\">", StrappyHTMLEscape(role)];
      [html appendFormat:@"<div class=\"role\">%@</div>",
        StrappyHTMLEscape(StrappyRoleLabel(role))];
      [html appendFormat:@"<div class=\"bubble\">%@</div>",
        StrappyHTMLEscape(text)];
      if ([createdAt length] > 0U) {
        [html appendFormat:@"<div class=\"meta\">%@</div>",
          StrappyHTMLEscape(createdAt)];
      }
      [html appendString:@"</div>"];
    }
  }

  [html appendString:@"</div></body></html>"];
  return html;
}

- (void)promptSendViewController:(PromptSendViewController *)controller
                 didSubmitPrompt:(NSString *)prompt
{
  NSMutableDictionary *request;

  (void)controller;
  if (sending_) {
    return;
  }

  sending_ = YES;
  [statusText_ release];
  statusText_ = [NSLocalizedString(@"Sending prompt...", nil) retain];
  [sendController_ setEnabled:NO];
  [self reloadContent];

  request = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
    prompt, @"prompt",
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

- (void)promptSendViewControllerDidChangeHeight:
    (PromptSendViewController *)controller
{
  (void)controller;
  [self layoutWebViewAndPromptBar];
  [[self view] setNeedsDisplay:YES];
}

- (void)sendPromptInBackground:(NSDictionary *)request
{
  NSAutoreleasePool *pool;
  NSError *error;
  NSDictionary *session;
  NSDictionary *result;
  NSString *errorMessage;
  NSString *prompt;
  NSNumber *sessionId;

  pool = [[NSAutoreleasePool alloc] init];
  prompt = [request objectForKey:@"prompt"];
  if (![prompt isKindOfClass:[NSString class]]) {
    prompt = @"";
  }
  sessionId = [request objectForKey:@"session_id"];
  if (![sessionId isKindOfClass:[NSNumber class]]) {
    sessionId = nil;
  }

  error = nil;
  session = [StrappySession submitPrompt:prompt
                     inSessionIdentifier:sessionId
                                   error:&error];
  if (session != nil) {
    result = [[NSDictionary dictionaryWithObject:session forKey:@"session"] retain];
  } else {
    errorMessage = [error localizedDescription];
    if ([errorMessage length] == 0U) {
      errorMessage = NSLocalizedString(@"Prompt failed.", nil);
    }
    result = [[NSDictionary dictionaryWithObject:errorMessage forKey:@"error"] retain];
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

  sending_ = NO;
  [statusText_ release];
  statusText_ = nil;
  [sendController_ setEnabled:YES];

  session = [result objectForKey:@"session"];
  if ([session isKindOfClass:[NSDictionary class]]) {
    [sessionId_ release];
    sessionId_ = [[session objectForKey:@"id"] retain];
    if (delegate_ != nil) {
      [delegate_ messageListViewController:self didCreateSession:session];
    }
  } else {
    errorMessage = [result objectForKey:@"error"];
    if (![errorMessage isKindOfClass:[NSString class]] || ([errorMessage length] == 0U)) {
      errorMessage = NSLocalizedString(@"Prompt failed.", nil);
    }
    statusText_ = [errorMessage retain];
  }

  [self reloadContent];
}

- (void)dealloc
{
  [htmlDirectoryPath_ release];
  [sessionId_ release];
  [sendController_ release];
  [statusText_ release];
  [super dealloc];
}

@end
