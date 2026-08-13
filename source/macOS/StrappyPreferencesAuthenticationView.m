#import "StrappyPreferencesAuthenticationView.h"

#import "StrappyKeychain.h"
#import "XPAppKit.h"

static const CGFloat kStrappyAuthenticationControlHeight = 24.0;
static const CGFloat kStrappyAuthenticationLabelHeight = 16.0;
static const CGFloat kStrappyAuthenticationButtonWidth = 96.0;
static const CGFloat kStrappyAuthenticationControlGap = 8.0;
static const CGFloat kStrappyAuthenticationHintHeight = 38.0;
static const CGFloat kStrappyAuthenticationBoxContentInset = 12.0;
static const CGFloat kStrappyAuthenticationLabelTopInset = 21.0;
static const CGFloat kStrappyAuthenticationFieldTopInset = 36.0;

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
@end

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

  openRouterBox = [[[NSBox alloc] initWithFrame:[self bounds]] autorelease];
  [openRouterBox setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [openRouterBox setTitle:NSLocalizedString(@"OpenRouter", nil)];
  [openRouterBox setTitlePosition:NSAtTop];
  [self addSubview:openRouterBox];

  /* Match the explicit NSBox control geometry used by Session Defaults.
   * NSBox content-view margins vary across the supported AppKit versions. */
  bounds = [self bounds];
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
  [apiEndpointField_ release];
  [apiTokenField_ release];
  [statusLabel_ release];
  [super dealloc];
}

@end
