#import "StrappySessionOptionsViewController.h"

#import "StrappyAuthentication.h"
#import "XPAppKit.h"
#include <math.h>

static const CGFloat kStrappyInspectorInset = 12.0;
static const CGFloat kStrappyInspectorGap = 8.0;
static const CGFloat kStrappyInspectorControlHeight = 24.0;
static const CGFloat kStrappyInspectorDocumentHeight = 526.0;
static const CGFloat kStrappyDefaultsModelBoxHeight = 164.0;
static const CGFloat kStrappyDefaultsBottomBoxMinimumHeight = 126.0;
static const CGFloat kStrappyDefaultsStatusHeight = 20.0;
static const NSUInteger kStrappyRoundLimitSliderMinimum = 20U;
static const NSUInteger kStrappyRoundLimitSliderMaximum = 200U;
static const NSUInteger kStrappyRoundLimitSliderStep = 10U;

static NSArray *StrappyInspectorSearchProviders(void)
{
  return [NSArray arrayWithObjects:
    StrappyWebProviderAuto,
    StrappyWebProviderNative,
    StrappyWebProviderExa,
    StrappyWebProviderParallel,
    nil];
}

static NSString *StrappyInspectorSearchProviderTitle(NSString *webProvider)
{
  if ([webProvider isEqualToString:StrappyWebProviderAuto]) {
    return NSLocalizedString(@"Auto", nil);
  }
  if ([webProvider isEqualToString:StrappyWebProviderNative]) {
    return NSLocalizedString(@"Native", nil);
  }
  if ([webProvider isEqualToString:StrappyWebProviderExa]) {
    return @"Exa";
  }
  if ([webProvider isEqualToString:StrappyWebProviderParallel]) {
    return @"Parallel";
  }
  return NSLocalizedString(@"None", nil);
}

static NSString *StrappyInspectorStringForRow(NSDictionary *row,
                                              NSString *key)
{
  id value;

  if (![row isKindOfClass:[NSDictionary class]]) {
    return @"";
  }
  value = [row objectForKey:key];
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

static NSString *StrappyInspectorModelTitle(NSDictionary *row)
{
  NSString *name;
  NSString *identifier;

  name = StrappyInspectorStringForRow(row, @"name");
  if ([name length] > 0U) {
    return name;
  }
  identifier = StrappyInspectorStringForRow(row, @"id");
  return ([identifier length] > 0U) ? identifier :
    NSLocalizedString(@"Model", nil);
}

static NSDictionary *StrappyInspectorModelForIdentifier(NSArray *models,
                                                         NSString *identifier)
{
  NSDictionary *model;
  NSUInteger index;

  if (![identifier isKindOfClass:[NSString class]]) {
    identifier = @"";
  }
  for (index = 0U; index < [models count]; index++) {
    model = [models objectAtIndex:index];
    if ([StrappyInspectorStringForRow(model, @"id")
          isEqualToString:identifier]) {
      return model;
    }
  }
  return nil;
}

static NSDictionary *StrappyInspectorAccountForIdentifier(
    NSArray *accounts, NSString *identifier)
{
  NSDictionary *account;
  NSUInteger index;

  if (![identifier isKindOfClass:[NSString class]]) {
    identifier = @"";
  }
  for (index = 0U; index < [accounts count]; index++) {
    account = [accounts objectAtIndex:index];
    if ([StrappyInspectorStringForRow(account, @"id")
          isEqualToString:identifier]) {
      return account;
    }
  }
  return nil;
}

static BOOL StrappyInspectorModelBoolean(NSDictionary *model,
                                         NSString *key,
                                         BOOL fallback)
{
  NSNumber *value;

  value = [model objectForKey:key];
  return [value isKindOfClass:[NSNumber class]] ? [value boolValue] : fallback;
}

static BOOL StrappyInspectorModelIsSignedIn(NSDictionary *model,
                                             NSString *accountIdentifier)
{
  if (![StrappyInspectorStringForRow(model, @"provider_id")
        isEqualToString:@"openai_chatgpt"]) {
    return YES;
  }
  return [StrappyAuthentication isChatGPTProviderEnabled] &&
    [[StrappyAuthentication
      authenticationForProviderAccountIdentifier:accountIdentifier]
      hasStoredCredentials];
}

static NSDictionary *StrappyInspectorFirstUsableModelForAccount(
    NSArray *models, NSString *accountIdentifier)
{
  NSArray *accounts;
  NSDictionary *account;
  NSString *providerIdentifier;
  NSUInteger index;

  accounts = [StrappySession providerAccountCatalogWithError:nil];
  account = StrappyInspectorAccountForIdentifier(accounts, accountIdentifier);
  providerIdentifier = StrappyInspectorStringForRow(account, @"provider_id");
  for (index = 0U; index < [models count]; index++) {
    NSDictionary *model;

    model = [models objectAtIndex:index];
    if ([StrappyInspectorStringForRow(model, @"provider_id")
          isEqualToString:providerIdentifier] &&
        StrappyInspectorModelIsSignedIn(model, accountIdentifier)) {
      return model;
    }
  }
  return nil;
}

static NSString *StrappyInspectorAssistantSegmentTitle(NSDictionary *row,
                                                        BOOL useFullName)
{
  NSString *identifier;
  NSString *name;

  identifier = StrappyInspectorStringForRow(row, @"id");
  if ([identifier isEqualToString:@"world_knowledge"]) {
    return NSLocalizedString(useFullName ? @"World Knowledge" : @"World", nil);
  }
  if ([identifier isEqualToString:@"personal_assistant"]) {
    return NSLocalizedString(useFullName ? @"Personal Assistant" : @"Personal", nil);
  }
  if ([identifier isEqualToString:@"coding_assistant"]) {
    return NSLocalizedString(useFullName ? @"Coding Assistant" : @"Coding", nil);
  }

  name = StrappyInspectorStringForRow(row, @"name");
  return ([name length] > 0U) ? name : identifier;
}

static void StrappyInspectorDistributeAssistantSegmentWidths(
    NSSegmentedControl *segmentedControl)
{
  NSRect frame;
  NSSize measuredCellSize;
  NSInteger count;
  NSInteger index;
  CGFloat availableWidth;
  CGFloat baseWidth;
  CGFloat chromeWidth;
  CGFloat usedWidth;

  if (segmentedControl == nil) {
    return;
  }
  count = [segmentedControl segmentCount];
  frame = [segmentedControl frame];
  if ((count <= 0) || (frame.size.width <= 0.0)) {
    return;
  }

  /* NSSegmentedCell adds outer-cap and separator chrome beyond the widths
   * assigned to its segments. Measure that extra width using a provisional
   * full-width distribution, then reserve it so the right cap stays inside
   * the control's frame. On Tiger a three-segment cell adds 8 points. */
  baseWidth = floor(frame.size.width / (CGFloat)count);
  usedWidth = 0.0;
  for (index = 0; index < count; index++) {
    CGFloat width;

    width = (index == (count - 1)) ?
      (frame.size.width - usedWidth) : baseWidth;
    [segmentedControl setWidth:width forSegment:index];
    usedWidth += width;
  }

  measuredCellSize = [[segmentedControl cell] cellSize];
  chromeWidth = ceil(measuredCellSize.width - frame.size.width);
  if (chromeWidth < 0.0) {
    chromeWidth = 0.0;
  }
  availableWidth = frame.size.width - chromeWidth;
  if (availableWidth <= 0.0) {
    return;
  }

  baseWidth = floor(availableWidth / (CGFloat)count);
  usedWidth = 0.0;
  for (index = 0; index < count; index++) {
    CGFloat width;

    width = (index == (count - 1)) ?
      (availableWidth - usedWidth) : baseWidth;
    [segmentedControl setWidth:width forSegment:index];
    usedWidth += width;
  }
}

static NSTextField *StrappyInspectorLabel(NSString *text, NSFont *font)
{
  NSTextField *label;

  label = [[NSTextField alloc] initWithFrame:NSZeroRect];
  [label setStringValue:(text != nil) ? text : @""];
  [label setBezeled:NO];
  [label setDrawsBackground:NO];
  [label setEditable:NO];
  [label setSelectable:NO];
  [label setFont:font];
  return label;
}

static NSBox *StrappyInspectorBox(NSString *title)
{
  NSBox *box;

  box = [[NSBox alloc] initWithFrame:NSZeroRect];
  [box setTitle:(title != nil) ? title : @""];
  [box setTitlePosition:NSAtTop];
  return box;
}

static NSButton *StrappyInspectorCheckbox(NSString *title,
                                         id target,
                                         SEL action)
{
  NSButton *button;

  button = [[NSButton alloc] initWithFrame:NSZeroRect];
  [button setButtonType:XPButtonTypeSwitch];
  [button setTitle:(title != nil) ? title : @""];
  [button setFont:[NSFont systemFontOfSize:11.0]];
  [button setTarget:target];
  [button setAction:action];
  return button;
}

static BOOL StrappyInspectorPopUpHasEnabledChoice(NSPopUpButton *popUpButton)
{
  NSArray *items;
  NSUInteger index;

  items = [popUpButton itemArray];
  for (index = 0U; index < [items count]; index++) {
    NSMenuItem *item;
    id value;

    item = [items objectAtIndex:index];
    value = [item representedObject];
    if ([item isEnabled] && [value isKindOfClass:[NSString class]] &&
        ([(NSString *)value length] > 0U)) {
      return YES;
    }
  }
  return NO;
}

static NSUInteger StrappyInspectorSnapSliderRoundLimit(double value)
{
  NSUInteger limit;

  if (value <= (double)kStrappyRoundLimitSliderMinimum) {
    return kStrappyRoundLimitSliderMinimum;
  }
  if (value >= (double)kStrappyRoundLimitSliderMaximum) {
    return kStrappyRoundLimitSliderMaximum;
  }
  limit = (NSUInteger)(
    (value / (double)kStrappyRoundLimitSliderStep) + 0.5) *
    kStrappyRoundLimitSliderStep;
  return limit;
}

static CGFloat StrappyDefaultsMinimumDocumentHeight(void)
{
  return kStrappyDefaultsModelBoxHeight + kStrappyInspectorGap +
    kStrappyDefaultsBottomBoxMinimumHeight + kStrappyInspectorGap +
    kStrappyDefaultsStatusHeight;
}

@interface StrappyInspectorDocumentView : NSView
@end

@implementation StrappyInspectorDocumentView

- (BOOL)isFlipped
{
  return YES;
}

@end

@interface StrappySessionOptionsViewController ()
- (void)layoutInspectorViews;
- (void)layoutDefaultsViews;
- (StrappySessionOptions *)currentOptions;
- (BOOL)canEditOptions;
- (void)populateAccountPopUpWithOptions:(StrappySessionOptions *)options;
- (void)populateModelPopUpWithOptions:(StrappySessionOptions *)options;
- (void)populateAssistantSegmentsWithOptions:(StrappySessionOptions *)options;
- (void)populateSearchProviderPopUpWithOptions:
    (StrappySessionOptions *)options;
- (void)updateControlEnabledStates;
- (NSString *)selectedProviderIdentifier;
- (BOOL)saveOptions:(StrappySessionOptions *)options
      changedFields:(StrappySessionOptionMask)changedFields;
- (void)modelChanged:(id)sender;
- (void)accountChanged:(id)sender;
- (void)assistantChanged:(id)sender;
- (void)webSearchChanged:(id)sender;
- (void)bashChanged:(id)sender;
- (void)limitToOneToolChanged:(id)sender;
- (void)answerQualityChanged:(id)sender;
- (void)searchProviderChanged:(id)sender;
- (void)roundLimitSliderChanged:(id)sender;
- (void)documentViewFrameDidChange:(NSNotification *)notification;
- (void)sessionDidUpdate:(NSNotification *)notification;
- (void)sessionActivityDidChange:(NSNotification *)notification;
- (void)modelCatalogDidChange:(NSNotification *)notification;
@end

@implementation StrappySessionOptionsViewController

- (id)init
{
  if ((self = [super init])) {
    NSNotificationCenter *notificationCenter;

    notificationCenter = [NSNotificationCenter defaultCenter];
    [notificationCenter addObserver:self
                           selector:@selector(sessionDidUpdate:)
                               name:StrappySessionDidUpdateNotification
                             object:nil];
    [notificationCenter addObserver:self
                           selector:@selector(sessionActivityDidChange:)
                               name:StrappySessionPromptDidStartNotification
                             object:nil];
    [notificationCenter addObserver:self
                           selector:@selector(sessionActivityDidChange:)
                               name:StrappySessionPromptDidFinishNotification
                             object:nil];
    [notificationCenter addObserver:self
                           selector:@selector(modelCatalogDidChange:)
                               name:StrappySessionModelCatalogDidChangeNotification
                             object:nil];
    [notificationCenter addObserver:self
                           selector:@selector(modelCatalogDidChange:)
                               name:StrappyProviderAccountsDidChangeNotification
                             object:nil];
    [notificationCenter addObserver:self
                           selector:@selector(modelCatalogDidChange:)
                               name:StrappyAuthenticationDidChangeNotification
                             object:nil];
  }
  return self;
}

- (id)initForSessionDefaults
{
  if ((self = [self init])) {
    editsSessionDefaults_ = YES;
  }
  return self;
}

- (void)loadView
{
  NSView *view;

  view = [[NSView alloc] initWithFrame:editsSessionDefaults_ ?
    NSMakeRect(0.0, 0.0, 696.0, 456.0) :
    NSMakeRect(0.0, 0.0, 300.0, 600.0)];
  [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [self setView:view];
  [view release];
}

- (void)viewDidLoad
{
  [super viewDidLoad];

  scrollView_ = [[NSScrollView alloc] initWithFrame:[[self view] bounds]];
  [scrollView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView_ setBorderType:NSNoBorder];
  [scrollView_ setHasVerticalScroller:!editsSessionDefaults_];
  [scrollView_ setHasHorizontalScroller:NO];
  [scrollView_ setDrawsBackground:YES];
  [scrollView_ setBackgroundColor:[NSColor windowBackgroundColor]];
  [[self view] addSubview:scrollView_];

  documentView_ = [[StrappyInspectorDocumentView alloc]
    initWithFrame:NSMakeRect(0.0, 0.0, 300.0,
                             kStrappyInspectorDocumentHeight)];
  [documentView_ setAutoresizingMask:NSViewWidthSizable];
  [documentView_ setPostsFrameChangedNotifications:YES];
  [scrollView_ setDocumentView:documentView_];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(documentViewFrameDidChange:)
             name:NSViewFrameDidChangeNotification
           object:documentView_];

  titleLabel_ = StrappyInspectorLabel(
    NSLocalizedString(editsSessionDefaults_ ?
      @"Session Defaults" : @"Session Options", nil),
    [NSFont boldSystemFontOfSize:editsSessionDefaults_ ? 14.0 : 13.0]);
  [documentView_ addSubview:titleLabel_];

  statusLabel_ = StrappyInspectorLabel(@"",
    [NSFont systemFontOfSize:10.0]);
  [statusLabel_ setTextColor:[NSColor disabledControlTextColor]];
  [[statusLabel_ cell] setWraps:YES];
  [documentView_ addSubview:statusLabel_];

  modelAssistantBox_ = StrappyInspectorBox(
    NSLocalizedString(@"Assistant", nil));
  toolsBox_ = StrappyInspectorBox(NSLocalizedString(editsSessionDefaults_ ?
    @"Tools & Search" : @"Tools", nil));
  limitsBox_ = StrappyInspectorBox(NSLocalizedString(@"Limits", nil));
  searchProviderBox_ = StrappyInspectorBox(
    NSLocalizedString(@"Search Provider", nil));
  [modelAssistantBox_ setAutoresizingMask:NSViewWidthSizable];
  [toolsBox_ setAutoresizingMask:NSViewWidthSizable];
  [limitsBox_ setAutoresizingMask:NSViewWidthSizable];
  [searchProviderBox_ setAutoresizingMask:NSViewWidthSizable];
  [documentView_ addSubview:modelAssistantBox_];
  [documentView_ addSubview:toolsBox_];
  [documentView_ addSubview:limitsBox_];
  [documentView_ addSubview:searchProviderBox_];

  accountLabel_ = StrappyInspectorLabel(
    [NSString stringWithFormat:@"%@:", NSLocalizedString(@"Account", nil)],
    [NSFont systemFontOfSize:10.0]);
  modelLabel_ = StrappyInspectorLabel(
    [NSString stringWithFormat:@"%@:", NSLocalizedString(@"Model", nil)],
    [NSFont systemFontOfSize:10.0]);
  assistantLabel_ = StrappyInspectorLabel(
    [NSString stringWithFormat:@"%@:", NSLocalizedString(@"Assistant", nil)],
    [NSFont systemFontOfSize:10.0]);
  [accountLabel_ setAutoresizingMask:NSViewWidthSizable];
  [modelLabel_ setAutoresizingMask:NSViewWidthSizable];
  [assistantLabel_ setAutoresizingMask:NSViewWidthSizable];
  [accountLabel_ setHidden:!editsSessionDefaults_];
  [documentView_ addSubview:accountLabel_];
  [documentView_ addSubview:modelLabel_];
  [documentView_ addSubview:assistantLabel_];

  searchProviderLabel_ = StrappyInspectorLabel(
    [NSString stringWithFormat:@"%@:", NSLocalizedString(@"Provider", nil)],
    [NSFont systemFontOfSize:10.0]);
  [searchProviderLabel_ setHidden:!editsSessionDefaults_];
  [documentView_ addSubview:searchProviderLabel_];

  accountPopUpButton_ = [[NSPopUpButton alloc] initWithFrame:NSZeroRect
                                                   pullsDown:NO];
  [accountPopUpButton_ setFont:[NSFont systemFontOfSize:11.0]];
  [[accountPopUpButton_ menu] setAutoenablesItems:NO];
  [accountPopUpButton_ setTarget:self];
  [accountPopUpButton_ setAction:@selector(accountChanged:)];
  [accountPopUpButton_ setAutoresizingMask:NSViewWidthSizable];
  [accountPopUpButton_ setHidden:!editsSessionDefaults_];
  [documentView_ addSubview:accountPopUpButton_];

  modelPopUpButton_ = [[NSPopUpButton alloc] initWithFrame:NSZeroRect
                                                pullsDown:NO];
  [modelPopUpButton_ setFont:[NSFont systemFontOfSize:11.0]];
  [[modelPopUpButton_ menu] setAutoenablesItems:NO];
  [modelPopUpButton_ setTarget:self];
  [modelPopUpButton_ setAction:@selector(modelChanged:)];
  [modelPopUpButton_ setAutoresizingMask:NSViewWidthSizable];
  [documentView_ addSubview:modelPopUpButton_];

  assistantSegmentedControl_ = [[NSSegmentedControl alloc]
    initWithFrame:NSZeroRect];
  [[assistantSegmentedControl_ cell]
    setTrackingMode:NSSegmentSwitchTrackingSelectOne];
  [assistantSegmentedControl_ setFont:[NSFont systemFontOfSize:10.0]];
  [assistantSegmentedControl_ setTarget:self];
  [assistantSegmentedControl_ setAction:@selector(assistantChanged:)];
  [assistantSegmentedControl_ setAutoresizingMask:NSViewWidthSizable];
  [documentView_ addSubview:assistantSegmentedControl_];

  webSearchButton_ = StrappyInspectorCheckbox(
    NSLocalizedString(@"Enable Web Search", nil),
    self,
    @selector(webSearchChanged:));
  bashButton_ = StrappyInspectorCheckbox(
    NSLocalizedString(@"Enable Bash", nil),
    self,
    @selector(bashChanged:));
  limitToOneToolButton_ = StrappyInspectorCheckbox(
    NSLocalizedString(@"Limit to 1 Tool", nil),
    self,
    @selector(limitToOneToolChanged:));
  answerQualityButton_ = StrappyInspectorCheckbox(
    NSLocalizedString(@"Check Answer Quality", nil),
    self,
    @selector(answerQualityChanged:));
  [documentView_ addSubview:webSearchButton_];
  [documentView_ addSubview:bashButton_];
  [documentView_ addSubview:limitToOneToolButton_];
  [documentView_ addSubview:answerQualityButton_];

  roundLimitLabel_ = StrappyInspectorLabel(
    [NSString stringWithFormat:@"%@:",
      NSLocalizedString(@"Round Limit", nil)],
    [NSFont systemFontOfSize:11.0]);
  [documentView_ addSubview:roundLimitLabel_];

  roundLimitValueLabel_ = StrappyInspectorLabel(
    @"",
    [NSFont systemFontOfSize:11.0]);
  [roundLimitValueLabel_ setAlignment:XPTextAlignmentRight];
  [documentView_ addSubview:roundLimitValueLabel_];

  roundLimitMinimumLabel_ = StrappyInspectorLabel(
    [NSString stringWithFormat:@"%lu",
      (unsigned long)kStrappyRoundLimitSliderMinimum],
    [NSFont systemFontOfSize:9.0]);
  [roundLimitMinimumLabel_ setTextColor:[NSColor disabledControlTextColor]];
  [documentView_ addSubview:roundLimitMinimumLabel_];

  roundLimitSlider_ = [[NSSlider alloc] initWithFrame:NSZeroRect];
  [roundLimitSlider_ setMinValue:(double)kStrappyRoundLimitSliderMinimum];
  [roundLimitSlider_ setMaxValue:(double)kStrappyRoundLimitSliderMaximum];
  [roundLimitSlider_ setContinuous:YES];
  [roundLimitSlider_ setTarget:self];
  [roundLimitSlider_ setAction:@selector(roundLimitSliderChanged:)];
  [documentView_ addSubview:roundLimitSlider_];

  roundLimitMaximumLabel_ = StrappyInspectorLabel(
    [NSString stringWithFormat:@"%lu",
      (unsigned long)kStrappyRoundLimitSliderMaximum],
    [NSFont systemFontOfSize:9.0]);
  [roundLimitMaximumLabel_ setAlignment:XPTextAlignmentRight];
  [roundLimitMaximumLabel_ setTextColor:[NSColor disabledControlTextColor]];
  [documentView_ addSubview:roundLimitMaximumLabel_];

  searchProviderPopUpButton_ = [[NSPopUpButton alloc]
    initWithFrame:NSZeroRect pullsDown:NO];
  [searchProviderPopUpButton_ setFont:[NSFont systemFontOfSize:11.0]];
  [[searchProviderPopUpButton_ menu] setAutoenablesItems:NO];
  [searchProviderPopUpButton_ setTarget:self];
  [searchProviderPopUpButton_ setAction:@selector(searchProviderChanged:)];
  [documentView_ addSubview:searchProviderPopUpButton_];

  [searchProviderBox_ setHidden:editsSessionDefaults_];
  [self layoutInspectorViews];
  [self reloadOptions];
}

- (void)viewDidLayout
{
  [super viewDidLayout];
  [self layoutInspectorViews];
}

- (void)layoutInspectorViews
{
  NSRect bounds;
  CGFloat documentWidth;
  CGFloat contentX;
  CGFloat contentWidth;
  CGFloat y;

  if (editsSessionDefaults_) {
    [self layoutDefaultsViews];
    return;
  }
  if ((scrollView_ == nil) || (documentView_ == nil)) {
    return;
  }

  bounds = [[self view] bounds];
  [scrollView_ setFrame:bounds];
  documentWidth = [scrollView_ contentSize].width;
  if (documentWidth < 220.0) {
    documentWidth = 220.0;
  }
  [documentView_ setFrame:NSMakeRect(0.0,
                                     0.0,
                                     documentWidth,
                                     kStrappyInspectorDocumentHeight)];

  contentX = kStrappyInspectorInset;
  contentWidth = documentWidth - (kStrappyInspectorInset * 2.0);
  if (contentWidth < 196.0) {
    contentWidth = 196.0;
  }

  [titleLabel_ setFrame:NSMakeRect(contentX, 10.0, contentWidth, 20.0)];
  [statusLabel_ setFrame:NSMakeRect(contentX, 31.0, contentWidth, 28.0)];

  y = 66.0;
  [modelAssistantBox_ setFrame:NSMakeRect(contentX, y, contentWidth, 126.0)];
  [modelLabel_ setFrame:NSMakeRect(contentX + 15.0,
                                  y + 21.0,
                                  contentWidth - 30.0,
                                  16.0)];
  [modelPopUpButton_ setFrame:NSMakeRect(contentX + 12.0,
                                         y + 36.0,
                                         contentWidth - 24.0,
                                         kStrappyInspectorControlHeight)];
  [assistantLabel_ setFrame:NSMakeRect(contentX + 15.0,
                                      y + 68.0,
                                      contentWidth - 30.0,
                                      16.0)];
  [assistantSegmentedControl_ setFrame:NSMakeRect(
    contentX + 12.0,
    y + 83.0,
    contentWidth - 24.0,
    kStrappyInspectorControlHeight)];
  StrappyInspectorDistributeAssistantSegmentWidths(
    assistantSegmentedControl_);

  y += 126.0 + kStrappyInspectorGap;
  [toolsBox_ setFrame:NSMakeRect(contentX, y, contentWidth, 88.0)];
  [webSearchButton_ setFrame:NSMakeRect(contentX + 12.0,
                                        y + 26.0,
                                        contentWidth - 24.0,
                                        20.0)];
  [bashButton_ setFrame:NSMakeRect(contentX + 12.0,
                                   y + 51.0,
                                   contentWidth - 24.0,
                                   20.0)];

  y += 88.0 + kStrappyInspectorGap;
  [limitsBox_ setFrame:NSMakeRect(contentX, y, contentWidth, 132.0)];
  [limitToOneToolButton_ setFrame:NSMakeRect(contentX + 12.0,
                                             y + 25.0,
                                             contentWidth - 24.0,
                                             20.0)];
  [answerQualityButton_ setFrame:NSMakeRect(contentX + 12.0,
                                            y + 50.0,
                                            contentWidth - 24.0,
                                            20.0)];
  [roundLimitLabel_ setFrame:NSMakeRect(contentX + 12.0,
                                        y + 78.0,
                                        contentWidth - 67.0,
                                        16.0)];
  [roundLimitValueLabel_ setFrame:NSMakeRect(
    contentX + contentWidth - 55.0,
    y + 78.0,
    43.0,
    16.0)];
  [roundLimitMinimumLabel_ setFrame:NSMakeRect(contentX + 12.0,
                                               y + 100.0,
                                               24.0,
                                               14.0)];
  [roundLimitSlider_ setFrame:NSMakeRect(contentX + 40.0,
                                         y + 96.0,
                                         contentWidth - 86.0,
                                         20.0)];
  [roundLimitMaximumLabel_ setFrame:NSMakeRect(
    contentX + contentWidth - 42.0,
    y + 100.0,
    30.0,
    14.0)];

  y += 132.0 + kStrappyInspectorGap;
  [searchProviderBox_ setFrame:NSMakeRect(contentX, y, contentWidth, 72.0)];
  [searchProviderPopUpButton_ setFrame:NSMakeRect(
    contentX + 12.0,
    y + 29.0,
    contentWidth - 24.0,
    kStrappyInspectorControlHeight)];
}

- (void)layoutDefaultsViews
{
  NSRect bounds;
  NSSize contentSize;
  CGFloat documentWidth;
  CGFloat documentHeight;
  CGFloat contentX;
  CGFloat contentWidth;
  CGFloat modelBoxY;
  CGFloat modelBoxHeight;
  CGFloat bottomY;
  CGFloat bottomBoxHeight;
  CGFloat columnGap;
  CGFloat leftWidth;
  CGFloat rightX;
  CGFloat rightWidth;
  CGFloat minimumDocumentHeight;

  if ((scrollView_ == nil) || (documentView_ == nil) || layingOut_) {
    return;
  }
  layingOut_ = YES;

  bounds = [[self view] bounds];
  [scrollView_ setFrame:bounds];
  minimumDocumentHeight = StrappyDefaultsMinimumDocumentHeight();
  [scrollView_ setHasVerticalScroller:
    (NSHeight(bounds) < minimumDocumentHeight)];
  contentSize = [scrollView_ contentSize];
  documentWidth = contentSize.width;
  if (documentWidth < 440.0) {
    documentWidth = 440.0;
  }
  documentHeight = contentSize.height;
  if (documentHeight < minimumDocumentHeight) {
    documentHeight = minimumDocumentHeight;
  }
  [documentView_ setFrame:NSMakeRect(0.0,
                                     0.0,
                                     documentWidth,
                                     documentHeight)];

  /* PreferencesWindowController supplies the single outer window margin.
   * Keep this pane flush with that host view so the margin is not doubled. */
  contentX = 0.0;
  contentWidth = documentWidth;

  [titleLabel_ setHidden:YES];
  modelBoxY = 0.0;
  modelBoxHeight = kStrappyDefaultsModelBoxHeight;
  [modelAssistantBox_ setFrame:NSMakeRect(contentX,
                                          modelBoxY,
                                          contentWidth,
                                          modelBoxHeight)];
  [accountLabel_ setFrame:NSMakeRect(contentX + 15.0,
                                   modelBoxY + 21.0,
                                   contentWidth - 30.0,
                                   16.0)];
  [accountPopUpButton_ setFrame:NSMakeRect(
    contentX + 12.0,
    modelBoxY + 36.0,
    contentWidth - 24.0,
    kStrappyInspectorControlHeight)];
  [modelLabel_ setFrame:NSMakeRect(contentX + 15.0,
                                       modelBoxY + 68.0,
                                       contentWidth - 30.0,
                                       16.0)];
  [modelPopUpButton_ setFrame:NSMakeRect(
    contentX + 12.0,
    modelBoxY + 83.0,
    contentWidth - 24.0,
    kStrappyInspectorControlHeight)];
  [assistantLabel_ setFrame:NSMakeRect(contentX + 15.0,
                                       modelBoxY + 115.0,
                                       contentWidth - 30.0,
                                       16.0)];
  [assistantSegmentedControl_ setFrame:NSMakeRect(
    contentX + 12.0,
    modelBoxY + 130.0,
    contentWidth - 24.0,
    kStrappyInspectorControlHeight)];
  StrappyInspectorDistributeAssistantSegmentWidths(
    assistantSegmentedControl_);

  bottomY = modelBoxY + modelBoxHeight + kStrappyInspectorGap;
  bottomBoxHeight = documentHeight - bottomY - kStrappyInspectorGap -
    kStrappyDefaultsStatusHeight;
  if (bottomBoxHeight < kStrappyDefaultsBottomBoxMinimumHeight) {
    bottomBoxHeight = kStrappyDefaultsBottomBoxMinimumHeight;
  }
  columnGap = 10.0;
  leftWidth = floor((contentWidth - columnGap) / 2.0);
  rightX = contentX + leftWidth + columnGap;
  rightWidth = contentWidth - leftWidth - columnGap;

  [toolsBox_ setFrame:NSMakeRect(
    contentX, bottomY, leftWidth, bottomBoxHeight)];
  [webSearchButton_ setFrame:NSMakeRect(contentX + 12.0,
                                        bottomY + 25.0,
                                        leftWidth - 24.0,
                                        20.0)];
  [bashButton_ setFrame:NSMakeRect(contentX + 12.0,
                                   bottomY + ([webSearchButton_ isHidden] ?
                                     25.0 : 53.0),
                                   leftWidth - 24.0,
                                   20.0)];
  [searchProviderLabel_ setFrame:NSMakeRect(contentX + 15.0,
                                            bottomY + 78.0,
                                            leftWidth - 30.0,
                                            16.0)];
  [searchProviderPopUpButton_ setFrame:NSMakeRect(
    contentX + 12.0,
    bottomY + 93.0,
    leftWidth - 24.0,
    kStrappyInspectorControlHeight)];

  [limitsBox_ setFrame:NSMakeRect(
    rightX, bottomY, rightWidth, bottomBoxHeight)];
  [limitToOneToolButton_ setFrame:NSMakeRect(rightX + 12.0,
                                             bottomY + 25.0,
                                             rightWidth - 24.0,
                                             20.0)];
  [answerQualityButton_ setFrame:NSMakeRect(rightX + 12.0,
                                            bottomY + 53.0,
                                            rightWidth - 24.0,
                                            20.0)];

  [roundLimitLabel_ setFrame:NSMakeRect(rightX + 12.0,
                                        bottomY + 78.0,
                                        rightWidth - 67.0,
                                        16.0)];
  [roundLimitValueLabel_ setFrame:NSMakeRect(
    rightX + rightWidth - 55.0,
    bottomY + 78.0,
    43.0,
    16.0)];
  [roundLimitMinimumLabel_ setFrame:NSMakeRect(rightX + 12.0,
                                               bottomY + 100.0,
                                               24.0,
                                               14.0)];
  [roundLimitSlider_ setFrame:NSMakeRect(rightX + 40.0,
                                         bottomY + 96.0,
                                         rightWidth - 86.0,
                                         20.0)];
  [roundLimitMaximumLabel_ setFrame:NSMakeRect(
    rightX + rightWidth - 42.0,
    bottomY + 100.0,
    30.0,
    14.0)];
  [statusLabel_ setFrame:NSMakeRect(contentX,
                                    documentHeight -
                                      kStrappyDefaultsStatusHeight,
                                    contentWidth,
                                    kStrappyDefaultsStatusHeight)];

  layingOut_ = NO;
}

- (void)reloadWithSession:(StrappySession *)session
{
  if (editsSessionDefaults_) {
    [self reloadOptions];
    return;
  }
  if (![session isKindOfClass:[StrappySession class]]) {
    session = nil;
  }
  if (session_ != session) {
    [session_ release];
    session_ = [session retain];
    [statusText_ release];
    statusText_ = nil;
  }
  [self reloadOptions];
}

- (StrappySessionOptions *)currentOptions
{
  if (editsSessionDefaults_) {
    return defaultOptions_;
  }
  return (session_ != nil) ? [session_ optionsWithError:nil] : nil;
}

- (BOOL)canEditOptions
{
  NSNumber *identifier;

  if (editsSessionDefaults_) {
    return (defaultOptions_ != nil) ? YES : NO;
  }
  if ((session_ == nil) || [session_ isDatabaseStudySession] ||
      [session_ isPromptInFlight]) {
    return NO;
  }
  identifier = [session_ sessionIdentifier];
  if ([identifier isKindOfClass:[NSNumber class]] &&
      [StrappySession isPromptInFlightForSessionIdentifier:identifier]) {
    return NO;
  }
  return YES;
}

- (void)reloadOptions
{
  StrappySessionOptions *options;
  NSError *loadError;
  NSString *loadErrorMessage;
  NSString *message;
  NSUInteger roundLimit;

  (void)[self view];
  if (reloading_) {
    return;
  }
  reloading_ = YES;

  loadError = nil;
  loadErrorMessage = nil;
  if (editsSessionDefaults_) {
    options = [StrappySession defaultSessionOptionsWithError:&loadError];
    [defaultOptions_ release];
    defaultOptions_ = [options retain];
    if (options == nil) {
      loadErrorMessage = [loadError localizedDescription];
      if ([loadErrorMessage length] == 0U) {
        loadErrorMessage = NSLocalizedString(
          @"Default session options could not be loaded.", nil);
      }
    }
  } else {
    options = (session_ != nil) ? [session_ optionsWithError:nil] : nil;
  }
  [self populateAccountPopUpWithOptions:options];
  [self populateModelPopUpWithOptions:options];
  [self populateAssistantSegmentsWithOptions:options];
  [self populateSearchProviderPopUpWithOptions:options];

  [webSearchButton_ setState:([options webSearchEnabled] ?
    XPControlStateValueOn : XPControlStateValueOff)];
  [bashButton_ setState:([options bashEnabled] ?
    XPControlStateValueOn : XPControlStateValueOff)];
  [limitToOneToolButton_ setState:([options limitToOneTool] ?
    XPControlStateValueOn : XPControlStateValueOff)];
  [answerQualityButton_ setState:([options answerQualityEnabled] ?
    XPControlStateValueOn : XPControlStateValueOff)];
  {
    NSArray *models;
    NSDictionary *selectedModel;

    models = [StrappySession allowedModelCatalogWithError:nil];
    selectedModel = StrappyInspectorModelForIdentifier(
      models,
      [options modelIdentifier]);
    if (!StrappyInspectorModelBoolean(selectedModel,
                                      @"hosted_tools_enabled",
                                      YES)) {
      [webSearchButton_ setState:XPControlStateValueOff];
    }
    if (!StrappyInspectorModelBoolean(selectedModel,
                                      @"local_functions_enabled",
                                      YES)) {
      [bashButton_ setState:XPControlStateValueOff];
    }
  }
  roundLimit = StrappyInspectorSnapSliderRoundLimit((double)((options != nil) ?
    [options roundLimit] : StrappySessionDefaultRoundLimit));
  [roundLimitSlider_ setDoubleValue:(double)roundLimit];
  [roundLimitValueLabel_ setStringValue:
    [NSString stringWithFormat:@"%lu", (unsigned long)roundLimit]];

  if ([statusText_ length] > 0U) {
    message = statusText_;
    [statusLabel_ setTextColor:[NSColor redColor]];
  } else if (editsSessionDefaults_ && (options == nil)) {
    message = loadErrorMessage;
    [statusLabel_ setTextColor:[NSColor redColor]];
  } else if (editsSessionDefaults_) {
    message = NSLocalizedString(
      @"Session defaults apply to new sessions",
      nil);
    [statusLabel_ setTextColor:[NSColor disabledControlTextColor]];
  } else if (session_ == nil) {
    message = NSLocalizedString(@"Select a chat to edit its options.", nil);
    [statusLabel_ setTextColor:[NSColor disabledControlTextColor]];
  } else if ([session_ isDatabaseStudySession]) {
    message = NSLocalizedString(@"Options are fixed for Database Study.", nil);
    [statusLabel_ setTextColor:[NSColor disabledControlTextColor]];
  } else if (![self canEditOptions]) {
    message = NSLocalizedString(
      @"Options are unavailable while a prompt is running.", nil);
    [statusLabel_ setTextColor:[NSColor disabledControlTextColor]];
  } else {
    message = NSLocalizedString(@"Changes apply to the selected chat.", nil);
    [statusLabel_ setTextColor:[NSColor disabledControlTextColor]];
  }
  [statusLabel_ setStringValue:(message != nil) ? message : @""];
  [self updateControlEnabledStates];
  [self layoutInspectorViews];
  reloading_ = NO;
}

- (void)populateAccountPopUpWithOptions:(StrappySessionOptions *)options
{
  NSArray *accounts;
  NSArray *models;
  NSString *selectedIdentifier;
  NSMenuItem *selectedItem;
  NSUInteger index;
  NSUInteger validCount;

  if (!editsSessionDefaults_) {
    return;
  }
  accounts = [StrappySession providerAccountCatalogWithError:nil];
  models = [StrappySession allowedModelCatalogWithError:nil];
  if (![accounts isKindOfClass:[NSArray class]]) {
    accounts = [NSArray array];
  }
  if (![models isKindOfClass:[NSArray class]]) {
    models = [NSArray array];
  }
  selectedIdentifier = [options providerAccountIdentifier];
  if (![selectedIdentifier isKindOfClass:[NSString class]]) {
    selectedIdentifier = @"";
  }

  [accountPopUpButton_ removeAllItems];
  selectedItem = nil;
  validCount = 0U;
  for (index = 0U; index < [accounts count]; index++) {
    NSDictionary *account;
    NSString *identifier;
    NSString *name;
    NSMenuItem *item;
    BOOL usable;

    account = [accounts objectAtIndex:index];
    identifier = StrappyInspectorStringForRow(account, @"id");
    if ([identifier length] == 0U) {
      continue;
    }
    name = StrappyInspectorStringForRow(account, @"name");
    [accountPopUpButton_ addItemWithTitle:([name length] > 0U) ?
      name : identifier];
    item = [accountPopUpButton_ lastItem];
    [item setRepresentedObject:identifier];
    usable = StrappyInspectorFirstUsableModelForAccount(
      models, identifier) != nil;
    [item setEnabled:usable];
    if (usable) {
      validCount++;
    }
    if ([identifier isEqualToString:selectedIdentifier]) {
      selectedItem = item;
    }
  }
  if ((selectedItem == nil) && ([selectedIdentifier length] > 0U)) {
    [accountPopUpButton_ addItemWithTitle:selectedIdentifier];
    selectedItem = [accountPopUpButton_ lastItem];
    [selectedItem setRepresentedObject:selectedIdentifier];
    [selectedItem setEnabled:NO];
  }
  if (selectedItem == nil) {
    [accountPopUpButton_ addItemWithTitle:(validCount > 0U) ?
      NSLocalizedString(@"Choose an Account", nil) :
      NSLocalizedString(@"No Accounts Available", nil)];
    selectedItem = [accountPopUpButton_ lastItem];
    [selectedItem setEnabled:NO];
  }
  [accountPopUpButton_ selectItem:selectedItem];
}

- (void)populateModelPopUpWithOptions:(StrappySessionOptions *)options
{
  NSArray *models;
  NSString *selectedIdentifier;
  NSMenuItem *selectedItem;
  NSUInteger index;
  NSUInteger validCount;
  NSString *lastAccountIdentifier;
  NSString *selectedAccountIdentifier;
  NSString *selectedProviderIdentifier;
  BOOL sessionAccountLocked;

  models = [StrappySession allowedModelCatalogWithError:nil];
  if (![models isKindOfClass:[NSArray class]]) {
    models = [NSArray array];
  }
  selectedIdentifier = [options modelIdentifier];
  if (![selectedIdentifier isKindOfClass:[NSString class]]) {
    selectedIdentifier = @"";
  }

  [modelPopUpButton_ removeAllItems];
  selectedItem = nil;
  validCount = 0U;
  lastAccountIdentifier = @"";
  selectedAccountIdentifier = [options providerAccountIdentifier];
  selectedProviderIdentifier = StrappyInspectorStringForRow(
    StrappyInspectorAccountForIdentifier(
      [StrappySession providerAccountCatalogWithError:nil],
      selectedAccountIdentifier), @"provider_id");
  sessionAccountLocked = !editsSessionDefaults_ &&
    ([[[session_ cachedSummary] objectForKey:@"prompt"] length] > 0U) &&
    ([selectedAccountIdentifier length] > 0U);
  for (index = 0U; index < [models count]; index++) {
    NSDictionary *row;
    NSString *identifier;
    NSString *accountIdentifier;
    NSString *accountName;
    NSMenuItem *item;

    row = [models objectAtIndex:index];
    identifier = StrappyInspectorStringForRow(row, @"id");
    if ([identifier length] == 0U) {
      continue;
    }
    accountIdentifier = StrappyInspectorStringForRow(row, @"provider_id");
    if (editsSessionDefaults_ &&
        ![accountIdentifier isEqualToString:selectedProviderIdentifier]) {
      continue;
    }
    accountName =
      StrappyInspectorStringForRow(row, @"provider_name");
    if (!editsSessionDefaults_ &&
        ![accountIdentifier isEqualToString:lastAccountIdentifier]) {
      if ([lastAccountIdentifier length] > 0U) {
        [[modelPopUpButton_ menu] addItem:[NSMenuItem separatorItem]];
      }
      [modelPopUpButton_ addItemWithTitle:([accountName length] > 0U) ?
        accountName : accountIdentifier];
      item = [modelPopUpButton_ lastItem];
      [item setEnabled:NO];
      lastAccountIdentifier = accountIdentifier;
    }
    [modelPopUpButton_ addItemWithTitle:StrappyInspectorModelTitle(row)];
    item = [modelPopUpButton_ lastItem];
    [item setRepresentedObject:identifier];
    if (!StrappyInspectorModelIsSignedIn(row, selectedAccountIdentifier) ||
        (sessionAccountLocked &&
         ![accountIdentifier isEqualToString:selectedProviderIdentifier])) {
      [item setEnabled:NO];
    } else {
      validCount++;
    }
    if ([identifier isEqualToString:selectedIdentifier]) {
      selectedItem = item;
    }
  }

  if ((selectedItem == nil) && ([selectedIdentifier length] > 0U)) {
    [modelPopUpButton_ addItemWithTitle:selectedIdentifier];
    selectedItem = [modelPopUpButton_ lastItem];
    [selectedItem setRepresentedObject:selectedIdentifier];
    [selectedItem setEnabled:NO];
  }
  if (selectedItem == nil) {
    [modelPopUpButton_ addItemWithTitle:(validCount > 0U) ?
      NSLocalizedString(@"Choose a Model", nil) :
      NSLocalizedString(@"No Models Available", nil)];
    selectedItem = [modelPopUpButton_ lastItem];
    [selectedItem setEnabled:NO];
  }
  [modelPopUpButton_ selectItem:selectedItem];
}

- (void)populateAssistantSegmentsWithOptions:(StrappySessionOptions *)options
{
  NSArray *assistantSets;
  NSString *selectedIdentifier;
  NSMutableArray *identifiers;
  NSUInteger index;

  assistantSets = [StrappySession assistantSetCatalog];
  if (![assistantSets isKindOfClass:[NSArray class]]) {
    assistantSets = [NSArray array];
  }
  selectedIdentifier = [options assistantSetIdentifier];
  if (![selectedIdentifier isKindOfClass:[NSString class]]) {
    selectedIdentifier = @"";
  }

  identifiers = [NSMutableArray arrayWithCapacity:[assistantSets count]];
  [assistantSegmentedControl_ setSegmentCount:
    (NSInteger)[assistantSets count]];
  for (index = 0U; index < [assistantSets count]; index++) {
    NSDictionary *row;
    NSString *identifier;
    NSString *name;
    NSNumber *available;

    row = [assistantSets objectAtIndex:index];
    identifier = StrappyInspectorStringForRow(row, @"id");
    [identifiers addObject:identifier];
    name = StrappyInspectorStringForRow(row, @"name");
    [assistantSegmentedControl_
      setLabel:StrappyInspectorAssistantSegmentTitle(
        row,
        editsSessionDefaults_)
      forSegment:(NSInteger)index];
    [assistantSegmentedControl_
      XP_setToolTip:(([name length] > 0U) ? name : identifier)
      forSegment:(NSInteger)index];
    available = [row objectForKey:@"available"];
    [assistantSegmentedControl_
      setEnabled:([available isKindOfClass:[NSNumber class]] &&
                  [available boolValue])
      forSegment:(NSInteger)index];
    if ([identifier isEqualToString:selectedIdentifier]) {
      [assistantSegmentedControl_ setSelected:YES
                                   forSegment:(NSInteger)index];
    } else {
      [assistantSegmentedControl_ setSelected:NO
                                   forSegment:(NSInteger)index];
    }
  }
  [assistantSegmentIdentifiers_ release];
  assistantSegmentIdentifiers_ = [identifiers copy];
  /* setSegmentCount:/setLabel: use natural label widths on Tiger. Reassert
   * equal widths after population so the drawn cell fits the same frame as
   * the model popup. */
  StrappyInspectorDistributeAssistantSegmentWidths(
    assistantSegmentedControl_);
}

- (void)populateSearchProviderPopUpWithOptions:
    (StrappySessionOptions *)options
{
  NSArray *providers;
  NSString *selectedProvider;
  NSMenuItem *selectedItem;
  NSUInteger index;

  providers = StrappyInspectorSearchProviders();
  selectedProvider = [options webProvider];
  if (![selectedProvider isKindOfClass:[NSString class]]) {
    selectedProvider = @"";
  }

  [searchProviderPopUpButton_ removeAllItems];
  selectedItem = nil;
  for (index = 0U; index < [providers count]; index++) {
    NSString *provider;
    NSMenuItem *item;

    provider = [providers objectAtIndex:index];
    [searchProviderPopUpButton_
      addItemWithTitle:StrappyInspectorSearchProviderTitle(provider)];
    item = [searchProviderPopUpButton_ lastItem];
    [item setRepresentedObject:provider];
    if ([provider isEqualToString:selectedProvider]) {
      selectedItem = item;
    }
  }
  if (selectedItem == nil) {
    [searchProviderPopUpButton_ addItemWithTitle:
      StrappyInspectorSearchProviderTitle(selectedProvider)];
    selectedItem = [searchProviderPopUpButton_ lastItem];
    [selectedItem setRepresentedObject:selectedProvider];
    [selectedItem setEnabled:NO];
  }
  [searchProviderPopUpButton_ selectItem:selectedItem];
}

- (void)updateControlEnabledStates
{
  BOOL enabled;
  BOOL providerEnabled;
  BOOL hostedToolsEnabled;
  BOOL localFunctionsEnabled;
  BOOL supportsWebSearch;
  BOOL supportsSearchProvider;
  NSString *providerIdentifier;
  StrappySessionOptions *options;
  NSArray *models;
  NSDictionary *selectedModel;

  enabled = [self canEditOptions];
  options = [self currentOptions];
  models = [StrappySession allowedModelCatalogWithError:nil];
  selectedModel = StrappyInspectorModelForIdentifier(models,
                                                      [options modelIdentifier]);
  hostedToolsEnabled = StrappyInspectorModelBoolean(
    selectedModel,
    @"hosted_tools_enabled",
    YES);
  localFunctionsEnabled = StrappyInspectorModelBoolean(
    selectedModel,
    @"local_functions_enabled",
    YES);
  providerIdentifier = [self selectedProviderIdentifier];
  supportsWebSearch = !editsSessionDefaults_ ||
    [providerIdentifier isEqualToString:@"openrouter"] ||
    [providerIdentifier isEqualToString:@"openai_chatgpt"];
  supportsSearchProvider = !editsSessionDefaults_ ||
    [providerIdentifier isEqualToString:@"openrouter"];
  [webSearchButton_ setHidden:!supportsWebSearch];
  if (editsSessionDefaults_) {
    [toolsBox_ setTitle:NSLocalizedString(supportsWebSearch ?
      @"Tools & Search" : @"Tools", nil)];
    [searchProviderLabel_ setHidden:!supportsSearchProvider];
    [searchProviderPopUpButton_ setHidden:!supportsSearchProvider];
  }
  providerEnabled = enabled &&
    hostedToolsEnabled &&
    supportsSearchProvider &&
    (!editsSessionDefaults_ || [options webSearchEnabled]);
  [accountPopUpButton_ setEnabled:editsSessionDefaults_ && enabled &&
    StrappyInspectorPopUpHasEnabledChoice(accountPopUpButton_)];
  [modelPopUpButton_ setEnabled:
    (enabled && StrappyInspectorPopUpHasEnabledChoice(modelPopUpButton_))];
  [assistantSegmentedControl_ setEnabled:
    (enabled && ([assistantSegmentIdentifiers_ count] > 0U))];
  [webSearchButton_ setEnabled:enabled && hostedToolsEnabled &&
    supportsWebSearch];
  [bashButton_ setEnabled:enabled && localFunctionsEnabled];
  [limitToOneToolButton_ setEnabled:enabled];
  [answerQualityButton_ setEnabled:enabled];
  [roundLimitSlider_ setEnabled:enabled];
  [searchProviderPopUpButton_ setEnabled:providerEnabled];
  [roundLimitLabel_ setTextColor:enabled ?
    [NSColor controlTextColor] : [NSColor disabledControlTextColor]];
  [roundLimitValueLabel_ setTextColor:enabled ?
    [NSColor controlTextColor] : [NSColor disabledControlTextColor]];
  [searchProviderLabel_ setTextColor:providerEnabled ?
    [NSColor controlTextColor] : [NSColor disabledControlTextColor]];
}

- (NSString *)selectedProviderIdentifier
{
  NSArray *accounts;
  NSDictionary *account;
  NSString *accountIdentifier;

  accountIdentifier = editsSessionDefaults_ ?
    [[accountPopUpButton_ selectedItem] representedObject] :
    [[self currentOptions] providerAccountIdentifier];
  accounts = [StrappySession providerAccountCatalogWithError:nil];
  account = StrappyInspectorAccountForIdentifier(accounts,
                                                  accountIdentifier);
  return StrappyInspectorStringForRow(account, @"provider_id");
}

- (BOOL)saveOptions:(StrappySessionOptions *)options
      changedFields:(StrappySessionOptionMask)changedFields
{
  NSError *error;
  NSString *message;
  BOOL saved;

  if (reloading_ || ![self canEditOptions] || (options == nil) ||
      (changedFields == 0U)) {
    [self reloadOptions];
    return NO;
  }

  error = nil;
  saved = editsSessionDefaults_ ?
    [StrappySession updateDefaultSessionOptions:options
                                  changedFields:changedFields
                                          error:&error] :
    [session_ updateOptions:options
              changedFields:changedFields
                      error:&error];
  if (!saved) {
    message = [error localizedDescription];
    if ([message length] == 0U) {
      message = NSLocalizedString(@"Your changes could not be saved.", nil);
    }
    [statusText_ release];
    statusText_ = [message copy];
    NSBeep();
    [self reloadOptions];
    return NO;
  }

  [statusText_ release];
  statusText_ = nil;
  [self reloadOptions];
  return YES;
}

- (void)modelChanged:(id)sender
{
  NSString *identifier;
  StrappySessionOptions *options;

  (void)sender;
  identifier = [[modelPopUpButton_ selectedItem] representedObject];
  if (![identifier isKindOfClass:[NSString class]] ||
      ([identifier length] == 0U)) {
    [self reloadOptions];
    return;
  }
  options = [[self currentOptions] copy];
  [options setModelIdentifier:identifier];
  (void)[self saveOptions:options changedFields:StrappySessionOptionModel];
  [options release];
}

- (void)accountChanged:(id)sender
{
  NSArray *accounts;
  NSArray *models;
  NSDictionary *account;
  NSDictionary *model;
  NSString *accountIdentifier;
  NSString *providerIdentifier;
  StrappySessionOptions *options;
  StrappySessionOptionMask changedFields;

  (void)sender;
  accountIdentifier = [[accountPopUpButton_ selectedItem]
    representedObject];
  accounts = [StrappySession providerAccountCatalogWithError:nil];
  models = [StrappySession allowedModelCatalogWithError:nil];
  account = StrappyInspectorAccountForIdentifier(accounts,
                                                  accountIdentifier);
  model = StrappyInspectorFirstUsableModelForAccount(models,
                                                      accountIdentifier);
  if ((account == nil) || (model == nil)) {
    NSBeep();
    [self reloadOptions];
    return;
  }

  options = [[self currentOptions] copy];
  [options setProviderAccountIdentifier:accountIdentifier];
  [options setModelIdentifier:StrappyInspectorStringForRow(model, @"id")];
  changedFields = StrappySessionOptionProviderAccount |
    StrappySessionOptionModel;
  providerIdentifier = StrappyInspectorStringForRow(account, @"provider_id");
  if ([providerIdentifier isEqualToString:@"openai_chatgpt"]) {
    [options setWebProvider:StrappyWebProviderNative];
    changedFields |= StrappySessionOptionWebProvider;
  } else if ([providerIdentifier isEqualToString:@"other"]) {
    [options setWebSearchEnabled:NO];
    [options setWebProvider:StrappyWebProviderNone];
    changedFields |= StrappySessionOptionWebSearch |
      StrappySessionOptionWebProvider;
  } else if ([[options webProvider]
               isEqualToString:StrappyWebProviderNone]) {
    [options setWebProvider:StrappyWebProviderAuto];
    changedFields |= StrappySessionOptionWebProvider;
  }
  (void)[self saveOptions:options changedFields:changedFields];
  [options release];
}

- (void)assistantChanged:(id)sender
{
  NSInteger segment;
  NSString *identifier;
  StrappySessionOptions *options;

  (void)sender;
  segment = [assistantSegmentedControl_ selectedSegment];
  if ((segment < 0) ||
      ((NSUInteger)segment >= [assistantSegmentIdentifiers_ count])) {
    [self reloadOptions];
    return;
  }
  identifier = [assistantSegmentIdentifiers_
    objectAtIndex:(NSUInteger)segment];
  if (![identifier isKindOfClass:[NSString class]] ||
      ([identifier length] == 0U)) {
    [self reloadOptions];
    return;
  }
  options = [[self currentOptions] copy];
  [options setAssistantSetIdentifier:identifier];
  (void)[self saveOptions:options
            changedFields:StrappySessionOptionAssistantSet];
  [options release];
}

- (void)webSearchChanged:(id)sender
{
  NSArray *models;
  NSDictionary *selectedModel;
  StrappySessionOptions *options;

  (void)sender;
  models = [StrappySession allowedModelCatalogWithError:nil];
  selectedModel = StrappyInspectorModelForIdentifier(
    models,
    [[self currentOptions] modelIdentifier]);
  if (!StrappyInspectorModelBoolean(selectedModel,
                                    @"hosted_tools_enabled",
                                    YES)) {
    [self reloadOptions];
    return;
  }
  options = [[self currentOptions] copy];
  [options setWebSearchEnabled:([webSearchButton_ state] ==
    XPControlStateValueOn)];
  (void)[self saveOptions:options
            changedFields:StrappySessionOptionWebSearch];
  [options release];
}

- (void)bashChanged:(id)sender
{
  NSArray *models;
  NSDictionary *selectedModel;
  StrappySessionOptions *options;

  (void)sender;
  models = [StrappySession allowedModelCatalogWithError:nil];
  selectedModel = StrappyInspectorModelForIdentifier(
    models,
    [[self currentOptions] modelIdentifier]);
  if (!StrappyInspectorModelBoolean(selectedModel,
                                    @"local_functions_enabled",
                                    YES)) {
    [self reloadOptions];
    return;
  }
  options = [[self currentOptions] copy];
  [options setBashEnabled:([bashButton_ state] == XPControlStateValueOn)];
  (void)[self saveOptions:options changedFields:StrappySessionOptionBash];
  [options release];
}

- (void)limitToOneToolChanged:(id)sender
{
  StrappySessionOptions *options;

  (void)sender;
  options = [[self currentOptions] copy];
  [options setLimitToOneTool:([limitToOneToolButton_ state] ==
    XPControlStateValueOn)];
  (void)[self saveOptions:options
            changedFields:StrappySessionOptionLimitToOneTool];
  [options release];
}

- (void)answerQualityChanged:(id)sender
{
  StrappySessionOptions *options;

  (void)sender;
  options = [[self currentOptions] copy];
  [options setAnswerQualityEnabled:([answerQualityButton_ state] ==
    XPControlStateValueOn)];
  (void)[self saveOptions:options
            changedFields:StrappySessionOptionAnswerQuality];
  [options release];
}

- (void)searchProviderChanged:(id)sender
{
  NSString *provider;
  StrappySessionOptions *options;

  (void)sender;
  provider = [[searchProviderPopUpButton_ selectedItem] representedObject];
  if (![StrappyInspectorSearchProviders() containsObject:provider]) {
    [self reloadOptions];
    return;
  }
  options = [[self currentOptions] copy];
  [options setWebProvider:provider];
  (void)[self saveOptions:options
            changedFields:StrappySessionOptionWebProvider];
  [options release];
}

- (void)roundLimitSliderChanged:(id)sender
{
  StrappySessionOptions *options;
  NSUInteger limit;

  (void)sender;
  if (reloading_) {
    return;
  }
  limit = StrappyInspectorSnapSliderRoundLimit(
    [roundLimitSlider_ doubleValue]);
  [roundLimitSlider_ setDoubleValue:(double)limit];
  [roundLimitValueLabel_ setStringValue:
    [NSString stringWithFormat:@"%lu", (unsigned long)limit]];
  if (limit == [[self currentOptions] roundLimit]) {
    [self reloadOptions];
    return;
  }
  options = [[self currentOptions] copy];
  [options setRoundLimit:limit];
  (void)[self saveOptions:options
            changedFields:StrappySessionOptionRoundLimit];
  [options release];
}

- (void)documentViewFrameDidChange:(NSNotification *)notification
{
  (void)notification;
  if (editsSessionDefaults_) {
    [self layoutDefaultsViews];
    return;
  }
  /* The controls themselves autoresize with the document view. Segmented
   * cells do not redistribute their segments automatically, so keep the
   * three equal whenever the inspector divider is dragged. */
  StrappyInspectorDistributeAssistantSegmentWidths(
    assistantSegmentedControl_);
}

- (void)sessionDidUpdate:(NSNotification *)notification
{
  if ([notification object] == session_) {
    [self reloadOptions];
  }
}

- (void)sessionActivityDidChange:(NSNotification *)notification
{
  if ([notification object] == session_) {
    [self reloadOptions];
  }
}

- (void)modelCatalogDidChange:(NSNotification *)notification
{
  (void)notification;
  [self reloadOptions];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [session_ release];
  [defaultOptions_ release];
  [statusText_ release];
  [scrollView_ release];
  [documentView_ release];
  [titleLabel_ release];
  [statusLabel_ release];
  [modelAssistantBox_ release];
  [toolsBox_ release];
  [limitsBox_ release];
  [searchProviderBox_ release];
  [accountLabel_ release];
  [modelLabel_ release];
  [assistantLabel_ release];
  [searchProviderLabel_ release];
  [accountPopUpButton_ release];
  [modelPopUpButton_ release];
  [assistantSegmentedControl_ release];
  [assistantSegmentIdentifiers_ release];
  [webSearchButton_ release];
  [bashButton_ release];
  [limitToOneToolButton_ release];
  [answerQualityButton_ release];
  [roundLimitLabel_ release];
  [roundLimitValueLabel_ release];
  [roundLimitMinimumLabel_ release];
  [roundLimitSlider_ release];
  [roundLimitMaximumLabel_ release];
  [searchProviderPopUpButton_ release];
  [super dealloc];
}

@end
