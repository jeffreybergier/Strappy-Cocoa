#import "StrappyPreferencesAuthenticationView.h"

#import "StrappyKeychain.h"
#import "XPAppKit.h"

static const CGFloat kStrappyPreferencesInset = 12.0;

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
  [label setFont:[NSFont systemFontOfSize:13.0]];
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
  NSTextField *endpointLabel;
  NSTextField *tokenLabel;
  NSTextField *hintLabel;
  NSButton *saveButton;
  NSString *apiEndpoint;
  NSString *apiToken;
  NSRect bounds;
  CGFloat labelWidth;
  CGFloat topY;
  CGFloat tokenY;
  CGFloat fieldX;
  CGFloat fieldWidth;

  bounds = [self bounds];
  labelWidth = 104.0;
  topY = NSMaxY(bounds) - kStrappyPreferencesInset - 28.0;
  tokenY = topY - 34.0;
  fieldX = kStrappyPreferencesInset + labelWidth;
  fieldWidth = NSWidth(bounds) - fieldX - kStrappyPreferencesInset;

  endpointLabel = StrappyPreferencesLabelWithFrame(
    NSMakeRect(kStrappyPreferencesInset,
               topY + 3.0,
               labelWidth - 8.0,
               20.0),
    NSLocalizedString(@"API Endpoint:", nil));
  [self addSubview:endpointLabel];

  apiEndpoint = [[StrappyKeychain sharedKeychain] apiEndpoint];
  if ([apiEndpoint length] == 0U) {
    apiEndpoint = [StrappyKeychain defaultAPIEndpoint];
  }
  apiEndpointField_ =
    [[NSTextField alloc] initWithFrame:NSMakeRect(fieldX,
                                                  topY,
                                                  fieldWidth,
                                                  24.0)];
  [apiEndpointField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [apiEndpointField_ setStringValue:(apiEndpoint != nil) ? apiEndpoint : @""];
  [[apiEndpointField_ cell] setPlaceholderString:
    NSLocalizedString(@"https://openrouter.ai/api/v1/responses", nil)];
  [self addSubview:apiEndpointField_];

  tokenLabel = StrappyPreferencesLabelWithFrame(
    NSMakeRect(kStrappyPreferencesInset,
               tokenY + 3.0,
               labelWidth - 8.0,
               20.0),
    NSLocalizedString(@"API Token:", nil));
  [self addSubview:tokenLabel];

  apiToken = [[StrappyKeychain sharedKeychain] apiToken];
  apiTokenField_ =
    [[NSSecureTextField alloc] initWithFrame:NSMakeRect(fieldX,
                                                        tokenY,
                                                        fieldWidth,
                                                        24.0)];
  [apiTokenField_ setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [apiTokenField_ setStringValue:(apiToken != nil) ? apiToken : @""];
  [[apiTokenField_ cell] setPlaceholderString:
    NSLocalizedString(@"Paste API token", nil)];
  [self addSubview:apiTokenField_];

  hintLabel = StrappyPreferencesLabelWithFrame(
    NSMakeRect(fieldX,
               tokenY - 46.0,
               fieldWidth,
               38.0),
    NSLocalizedString(
      @"APIENDPOINT or APITOKEN in .env or the process environment overrides keychain values while set.",
      nil));
  [hintLabel setFont:[NSFont systemFontOfSize:11.0]];
  [hintLabel setTextColor:[NSColor disabledControlTextColor]];
  [hintLabel setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
  [[hintLabel cell] setWraps:YES];
  [self addSubview:hintLabel];

  saveButton = [[[NSButton alloc]
    initWithFrame:NSMakeRect(NSMaxX(bounds) - kStrappyPreferencesInset - 96.0,
                             kStrappyPreferencesInset,
                             96.0,
                             24.0)] autorelease];
  [saveButton setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [saveButton setTitle:NSLocalizedString(@"Save", nil)];
  [saveButton setBezelStyle:XPBezelStyleRounded];
  [saveButton setButtonType:XPButtonTypeMomentaryLight];
  [saveButton setKeyEquivalent:@"\r"];
  [saveButton setTarget:target];
  [saveButton setAction:@selector(saveAPICredentials:)];
  [self addSubview:saveButton];

  statusLabel_ =
    [[NSTextField alloc] initWithFrame:NSMakeRect(kStrappyPreferencesInset,
                                                  kStrappyPreferencesInset + 2.0,
                                                  NSWidth(bounds) - 132.0,
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
