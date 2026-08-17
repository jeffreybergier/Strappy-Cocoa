#import "StrappyPreferencesAuthenticationView.h"

#import "StrappyAuthentication.h"
#import "StrappyKeychain.h"
#import "XPAppKit.h"

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
  #define StrappyAuthenticationPasteboardStringType NSPasteboardTypeString
#else
  #define StrappyAuthenticationPasteboardStringType NSStringPboardType
#endif

static const CGFloat kStrappyAuthenticationControlHeight = 24.0;
static const CGFloat kStrappyAuthenticationLabelHeight = 16.0;
static const CGFloat kStrappyAuthenticationButtonWidth = 96.0;
static const CGFloat kStrappyAuthenticationControlGap = 8.0;
static const CGFloat kStrappyAuthenticationHintHeight = 38.0;
static const CGFloat kStrappyAuthenticationBoxContentInset = 12.0;
static const CGFloat kStrappyAuthenticationLabelTopInset = 21.0;
static const CGFloat kStrappyAuthenticationFieldTopInset = 36.0;
static const CGFloat kStrappyAuthenticationSectionGap = 8.0;
static const CGFloat kStrappyChatGPTBoxHeight = 184.0;

static NSTextField *StrappyPreferencesLabelWithFrame(NSRect frame,
                                                     NSString *text)
{
  NSTextField *label;

  label = [[[NSTextField alloc] initWithFrame:frame] autorelease];
  [label setStringValue:(text != nil) ? text : @""];
  [label setBezeled:NO];
  [label setDrawsBackground:NO];
  [label setEditable:NO];
  [label setSelectable:NO];
  [label setFont:[NSFont systemFontOfSize:10.0]];
  return label;
}

@interface StrappyPreferencesAuthenticationView ()
- (void)buildViewWithTarget:(id)target;
- (void)reloadChatGPTState;
- (void)authenticationDidChange:(NSNotification *)notification;
- (void)startChatGPTLogin:(id)sender;
- (void)copyChatGPTCode:(id)sender;
- (void)openChatGPTVerificationURL:(id)sender;
- (void)cancelChatGPTLogin:(id)sender;
- (void)retryChatGPTRefresh:(id)sender;
- (void)signOutChatGPT:(id)sender;
@end

static NSButton *StrappyPreferencesButton(NSRect frame,
                                          NSString *title,
                                          id target,
                                          SEL action)
{
  NSButton *button;

  button = [[[NSButton alloc] initWithFrame:frame] autorelease];
  [button setTitle:title];
  [button setBezelStyle:XPBezelStyleRounded];
  [button setButtonType:XPButtonTypeMomentaryLight];
  [button setTarget:target];
  [button setAction:action];
  return button;
}

@implementation StrappyPreferencesAuthenticationView

- (id)initWithFrame:(NSRect)frame
{
  return [self initWithFrame:frame target:nil];
}

- (id)initWithFrame:(NSRect)frame target:(id)target
{
  if ((self = [super initWithFrame:frame])) {
    [self setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [self buildViewWithTarget:target];
  }
  return self;
}

- (void)buildViewWithTarget:(id)target
{
  NSBox *openRouterBox;
  NSBox *chatGPTBox;
  NSTextField *endpointLabel;
  NSTextField *tokenLabel;
  NSTextField *hintLabel;
  NSButton *saveButton;
  NSString *apiEndpoint;
  NSString *apiToken;
  NSRect bounds;
  CGFloat endpointLabelY;
  CGFloat endpointY;
  CGFloat tokenLabelTop;
  CGFloat tokenLabelY;
  CGFloat tokenFieldTop;
  CGFloat tokenY;
  CGFloat fieldWidth;
  CGFloat hintTop;
  CGFloat hintY;
  CGFloat statusWidth;
  NSRect openRouterFrame;
  NSRect chatGPTFrame;
  CGFloat chatY;
  CGFloat buttonWidth;

  bounds = [self bounds];
  chatGPTFrame = NSMakeRect(0.0,
                            0.0,
                            NSWidth(bounds),
                            kStrappyChatGPTBoxHeight);
  openRouterFrame = NSMakeRect(
    0.0,
    NSMaxY(chatGPTFrame) + kStrappyAuthenticationSectionGap,
    NSWidth(bounds),
    NSHeight(bounds) - kStrappyChatGPTBoxHeight -
      kStrappyAuthenticationSectionGap);
  openRouterBox = [[[NSBox alloc] initWithFrame:openRouterFrame] autorelease];
  [openRouterBox setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [openRouterBox setTitle:NSLocalizedString(@"OpenRouter", nil)];
  [openRouterBox setTitlePosition:NSAtTop];
  [self addSubview:openRouterBox];

  /* Match the explicit NSBox control geometry used by Session Defaults.
   * NSBox content-view margins vary across the supported AppKit versions. */
  bounds = openRouterFrame;
  endpointLabelY = NSMaxY(bounds) - kStrappyAuthenticationLabelTopInset -
    kStrappyAuthenticationLabelHeight;
  endpointY = NSMaxY(bounds) - kStrappyAuthenticationFieldTopInset -
    kStrappyAuthenticationControlHeight;
  tokenLabelTop = kStrappyAuthenticationFieldTopInset +
    kStrappyAuthenticationControlHeight + kStrappyAuthenticationControlGap;
  tokenLabelY = NSMaxY(bounds) - tokenLabelTop -
    kStrappyAuthenticationLabelHeight;
  tokenFieldTop = tokenLabelTop + kStrappyAuthenticationFieldTopInset -
    kStrappyAuthenticationLabelTopInset;
  tokenY = NSMaxY(bounds) - tokenFieldTop -
    kStrappyAuthenticationControlHeight;
  fieldWidth = NSWidth(bounds) -
    (2.0 * kStrappyAuthenticationBoxContentInset);
  hintTop = tokenFieldTop + kStrappyAuthenticationControlHeight +
    kStrappyAuthenticationControlGap;
  hintY = NSMaxY(bounds) - hintTop - kStrappyAuthenticationHintHeight;

  endpointLabel = StrappyPreferencesLabelWithFrame(
    NSMakeRect(kStrappyAuthenticationBoxContentInset,
               endpointLabelY,
               fieldWidth,
               kStrappyAuthenticationLabelHeight),
    NSLocalizedString(@"Endpoint", nil));
  [endpointLabel setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [self addSubview:endpointLabel];

  apiEndpoint = [[StrappyKeychain sharedKeychain] apiEndpoint];
  if ([apiEndpoint length] == 0U) {
    apiEndpoint = [StrappyKeychain defaultAPIEndpoint];
  }
  apiEndpointField_ = [[NSTextField alloc] initWithFrame:NSMakeRect(
    kStrappyAuthenticationBoxContentInset,
    endpointY,
    fieldWidth,
    kStrappyAuthenticationControlHeight)];
  [apiEndpointField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [apiEndpointField_ setStringValue:(apiEndpoint != nil) ? apiEndpoint : @""];
  [[apiEndpointField_ cell] setPlaceholderString:
    NSLocalizedString(@"https://openrouter.ai/api/v1/responses", nil)];
  [self addSubview:apiEndpointField_];

  tokenLabel = StrappyPreferencesLabelWithFrame(
    NSMakeRect(kStrappyAuthenticationBoxContentInset,
               tokenLabelY,
               fieldWidth,
               kStrappyAuthenticationLabelHeight),
    NSLocalizedString(@"Token", nil));
  [tokenLabel setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [self addSubview:tokenLabel];

  apiToken = [[StrappyKeychain sharedKeychain] apiToken];
  apiTokenField_ = [[NSSecureTextField alloc] initWithFrame:NSMakeRect(
    kStrappyAuthenticationBoxContentInset,
    tokenY,
    fieldWidth,
    kStrappyAuthenticationControlHeight)];
  [apiTokenField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [apiTokenField_ setStringValue:(apiToken != nil) ? apiToken : @""];
  [[apiTokenField_ cell] setPlaceholderString:
    NSLocalizedString(@"Paste API token", nil)];
  [self addSubview:apiTokenField_];

  hintLabel = StrappyPreferencesLabelWithFrame(
    NSMakeRect(kStrappyAuthenticationBoxContentInset,
               hintY,
               fieldWidth,
               kStrappyAuthenticationHintHeight),
    NSLocalizedString(
      @"APIENDPOINT or APITOKEN in .env or the process environment overrides keychain values while set.",
      nil));
  [hintLabel setFont:[NSFont systemFontOfSize:11.0]];
  [hintLabel setTextColor:[NSColor disabledControlTextColor]];
  [hintLabel setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [[hintLabel cell] setWraps:YES];
  [self addSubview:hintLabel];

  saveButton = [[[NSButton alloc]
    initWithFrame:NSMakeRect(NSMaxX(bounds) -
                               kStrappyAuthenticationBoxContentInset -
                               kStrappyAuthenticationButtonWidth,
                             kStrappyAuthenticationBoxContentInset,
                             kStrappyAuthenticationButtonWidth,
                             kStrappyAuthenticationControlHeight)] autorelease];
  [saveButton setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [saveButton setTitle:NSLocalizedString(@"Save", nil)];
  [saveButton setBezelStyle:XPBezelStyleRounded];
  [saveButton setButtonType:XPButtonTypeMomentaryLight];
  [saveButton setKeyEquivalent:@"\r"];
  [saveButton setTarget:target];
  [saveButton setAction:@selector(saveAPICredentials:)];
  [self addSubview:saveButton];

  statusWidth = NSWidth(bounds) -
    (2.0 * kStrappyAuthenticationBoxContentInset) -
    kStrappyAuthenticationButtonWidth - kStrappyAuthenticationControlGap;
  statusLabel_ = [[NSTextField alloc]
    initWithFrame:NSMakeRect(kStrappyAuthenticationBoxContentInset,
                             kStrappyAuthenticationBoxContentInset + 2.0,
                             statusWidth,
                             20.0)];
  [statusLabel_ setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
  [statusLabel_ setBezeled:NO];
  [statusLabel_ setDrawsBackground:NO];
  [statusLabel_ setEditable:NO];
  [statusLabel_ setSelectable:NO];
  [statusLabel_ setFont:[NSFont systemFontOfSize:11.0]];
  [statusLabel_ setTextColor:[NSColor disabledControlTextColor]];
  [self addSubview:statusLabel_];

  chatGPTBox = [[[NSBox alloc] initWithFrame:chatGPTFrame] autorelease];
  [chatGPTBox setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
  [chatGPTBox setTitle:NSLocalizedString(@"ChatGPT (Experimental)", nil)];
  [chatGPTBox setTitlePosition:NSAtTop];
  [self addSubview:chatGPTBox];

  chatGPTStatusLabel_ = [[NSTextField alloc] initWithFrame:NSMakeRect(
    kStrappyAuthenticationBoxContentInset,
    NSMaxY(chatGPTFrame) - 58.0,
    NSWidth(chatGPTFrame) - (2.0 * kStrappyAuthenticationBoxContentInset),
    34.0)];
  [chatGPTStatusLabel_ setAutoresizingMask:NSViewWidthSizable |
    NSViewMaxYMargin];
  [chatGPTStatusLabel_ setBezeled:NO];
  [chatGPTStatusLabel_ setDrawsBackground:NO];
  [chatGPTStatusLabel_ setEditable:NO];
  [chatGPTStatusLabel_ setSelectable:YES];
  [chatGPTStatusLabel_ setFont:[NSFont systemFontOfSize:11.0]];
  [[chatGPTStatusLabel_ cell] setWraps:YES];
  [self addSubview:chatGPTStatusLabel_];

  chatGPTCodeLabel_ = [[NSTextField alloc] initWithFrame:NSMakeRect(
    kStrappyAuthenticationBoxContentInset,
    NSMaxY(chatGPTFrame) - 88.0,
    NSWidth(chatGPTFrame) - (2.0 * kStrappyAuthenticationBoxContentInset),
    24.0)];
  [chatGPTCodeLabel_ setAutoresizingMask:NSViewWidthSizable |
    NSViewMaxYMargin];
  [chatGPTCodeLabel_ setBezeled:YES];
  [chatGPTCodeLabel_ setDrawsBackground:YES];
  [chatGPTCodeLabel_ setEditable:NO];
  [chatGPTCodeLabel_ setSelectable:YES];
  [chatGPTCodeLabel_ setAlignment:XPTextAlignmentCenter];
  [chatGPTCodeLabel_ setFont:[NSFont boldSystemFontOfSize:15.0]];
  [self addSubview:chatGPTCodeLabel_];

  buttonWidth = (NSWidth(chatGPTFrame) -
    (2.0 * kStrappyAuthenticationBoxContentInset) -
    (2.0 * kStrappyAuthenticationControlGap)) / 3.0;
  chatY = NSMaxY(chatGPTFrame) - 124.0;
  chatGPTSignInButton_ = [StrappyPreferencesButton(
    NSMakeRect(kStrappyAuthenticationBoxContentInset,
               chatY,
               buttonWidth,
               kStrappyAuthenticationControlHeight),
    NSLocalizedString(@"Sign In", nil),
    self,
    @selector(startChatGPTLogin:)) retain];
  chatGPTCopyButton_ = [StrappyPreferencesButton(
    NSMakeRect(kStrappyAuthenticationBoxContentInset + buttonWidth +
               kStrappyAuthenticationControlGap,
               chatY,
               buttonWidth,
               kStrappyAuthenticationControlHeight),
    NSLocalizedString(@"Copy Code", nil),
    self,
    @selector(copyChatGPTCode:)) retain];
  chatGPTOpenButton_ = [StrappyPreferencesButton(
    NSMakeRect(kStrappyAuthenticationBoxContentInset +
               (2.0 * (buttonWidth + kStrappyAuthenticationControlGap)),
               chatY,
               buttonWidth,
               kStrappyAuthenticationControlHeight),
    NSLocalizedString(@"Open Browser", nil),
    self,
    @selector(openChatGPTVerificationURL:)) retain];
  chatY -= kStrappyAuthenticationControlHeight +
    kStrappyAuthenticationControlGap;
  chatGPTCancelButton_ = [StrappyPreferencesButton(
    NSMakeRect(kStrappyAuthenticationBoxContentInset,
               chatY,
               buttonWidth,
               kStrappyAuthenticationControlHeight),
    NSLocalizedString(@"Cancel", nil),
    self,
    @selector(cancelChatGPTLogin:)) retain];
  chatGPTRetryButton_ = [StrappyPreferencesButton(
    NSMakeRect(kStrappyAuthenticationBoxContentInset + buttonWidth +
               kStrappyAuthenticationControlGap,
               chatY,
               buttonWidth,
               kStrappyAuthenticationControlHeight),
    NSLocalizedString(@"Retry Refresh", nil),
    self,
    @selector(retryChatGPTRefresh:)) retain];
  chatGPTSignOutButton_ = [StrappyPreferencesButton(
    NSMakeRect(kStrappyAuthenticationBoxContentInset +
               (2.0 * (buttonWidth + kStrappyAuthenticationControlGap)),
               chatY,
               buttonWidth,
               kStrappyAuthenticationControlHeight),
    NSLocalizedString(@"Sign Out", nil),
    self,
    @selector(signOutChatGPT:)) retain];
  [self addSubview:chatGPTSignInButton_];
  [self addSubview:chatGPTCopyButton_];
  [self addSubview:chatGPTOpenButton_];
  [self addSubview:chatGPTCancelButton_];
  [self addSubview:chatGPTRetryButton_];
  [self addSubview:chatGPTSignOutButton_];

  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(authenticationDidChange:)
           name:StrappyAuthenticationDidChangeNotification
         object:[StrappyAuthentication sharedAuthentication]];
  [[StrappyAuthentication sharedAuthentication]
    refreshChatGPTCredentialsIfNeeded];
  [self reloadChatGPTState];
}

- (void)authenticationDidChange:(NSNotification *)notification
{
  (void)notification;
  [self reloadChatGPTState];
}

- (void)reloadChatGPTState
{
  StrappyAuthentication *authentication;
  StrappyAuthenticationState state;
  NSString *message;
  NSString *code;
  BOOL awaiting;
  BOOL inFlight;
  BOOL hasCredentials;
  BOOL providerEnabled;

  authentication = [StrappyAuthentication sharedAuthentication];
  state = [authentication state];
  code = [authentication userCode];
  awaiting = state == StrappyAuthenticationStateAwaitingUser;
  inFlight = [authentication isOperationInFlight];
  hasCredentials = [authentication hasStoredCredentials];
  providerEnabled = [StrappyAuthentication isChatGPTProviderEnabled];
  if (!providerEnabled) {
    message = NSLocalizedString(
      @"Disabled by the experimental provider kill switch", nil);
  } else switch (state) {
    case StrappyAuthenticationStateRequestingCode:
      message = NSLocalizedString(@"Requesting a device code…", nil);
      break;
    case StrappyAuthenticationStateAwaitingUser:
      message = NSLocalizedString(
        @"Open the verification page and enter this code.", nil);
      break;
    case StrappyAuthenticationStateSignedIn:
      message = NSLocalizedString(@"Signed in to ChatGPT.", nil);
      break;
    case StrappyAuthenticationStateRefreshing:
      message = NSLocalizedString(@"Refreshing ChatGPT credentials…", nil);
      break;
    case StrappyAuthenticationStateError:
      message = [authentication errorMessage];
      if ([message length] == 0U) {
        message = NSLocalizedString(@"ChatGPT authentication failed.", nil);
      }
      break;
    case StrappyAuthenticationStateCancelled:
      message = NSLocalizedString(@"ChatGPT sign-in was cancelled.", nil);
      break;
    case StrappyAuthenticationStateSignedOut:
    default:
      message = NSLocalizedString(@"Not signed in to ChatGPT.", nil);
      break;
  }
  [chatGPTStatusLabel_ setStringValue:(message != nil) ? message : @""];
  [chatGPTCodeLabel_ setStringValue:(code != nil) ? code : @""];
  [chatGPTSignInButton_ setEnabled:providerEnabled && !inFlight &&
    !hasCredentials];
  [chatGPTCopyButton_ setEnabled:providerEnabled && awaiting &&
    ([code length] > 0U)];
  [chatGPTOpenButton_ setEnabled:providerEnabled && awaiting &&
    ([[authentication verificationURL] length] > 0U)];
  [chatGPTCancelButton_ setEnabled:providerEnabled &&
    ((state == StrappyAuthenticationStateRequestingCode) || awaiting)];
  [chatGPTRetryButton_ setEnabled:providerEnabled && !inFlight &&
    hasCredentials &&
    (state == StrappyAuthenticationStateError)];
  [chatGPTSignOutButton_ setEnabled:hasCredentials];
}

- (void)startChatGPTLogin:(id)sender
{
  (void)sender;
  if (![[StrappyAuthentication sharedAuthentication]
        startChatGPTDeviceLogin]) {
    NSBeep();
  }
}

- (void)copyChatGPTCode:(id)sender
{
  NSString *code;
  NSPasteboard *pasteboard;

  (void)sender;
  code = [[StrappyAuthentication sharedAuthentication] userCode];
  if ([code length] == 0U) {
    NSBeep();
    return;
  }
  pasteboard = [NSPasteboard generalPasteboard];
  [pasteboard declareTypes:[NSArray arrayWithObject:
    StrappyAuthenticationPasteboardStringType]
                      owner:nil];
  [pasteboard setString:code
                forType:StrappyAuthenticationPasteboardStringType];
}

- (void)openChatGPTVerificationURL:(id)sender
{
  NSString *verificationURL;
  NSURL *url;

  (void)sender;
  verificationURL =
    [[StrappyAuthentication sharedAuthentication] verificationURL];
  url = ([verificationURL length] > 0U) ?
    [NSURL URLWithString:verificationURL] : nil;
  if ((url == nil) || ![[NSWorkspace sharedWorkspace] openURL:url]) {
    NSBeep();
  }
}

- (void)cancelChatGPTLogin:(id)sender
{
  (void)sender;
  [[StrappyAuthentication sharedAuthentication] cancelChatGPTDeviceLogin];
}

- (void)retryChatGPTRefresh:(id)sender
{
  (void)sender;
  if (![[StrappyAuthentication sharedAuthentication]
        refreshChatGPTCredentialsIfNeeded]) {
    NSBeep();
  }
}

- (void)signOutChatGPT:(id)sender
{
  (void)sender;
  if (![[StrappyAuthentication sharedAuthentication] signOutChatGPT]) {
    NSBeep();
  }
}

- (NSTextField *)apiEndpointField
{
  return apiEndpointField_;
}

- (NSSecureTextField *)apiTokenField
{
  return apiTokenField_;
}

- (NSTextField *)statusLabel
{
  return statusLabel_;
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [apiEndpointField_ release];
  [apiTokenField_ release];
  [statusLabel_ release];
  [chatGPTStatusLabel_ release];
  [chatGPTCodeLabel_ release];
  [chatGPTSignInButton_ release];
  [chatGPTCopyButton_ release];
  [chatGPTOpenButton_ release];
  [chatGPTCancelButton_ release];
  [chatGPTRetryButton_ release];
  [chatGPTSignOutButton_ release];
  [super dealloc];
}

@end
