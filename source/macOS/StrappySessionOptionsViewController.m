#import "StrappySessionOptionsViewController.h"

#import "XPAppKit.h"
#include <math.h>

static const CGFloat kStrappyInspectorInset = 12.0;
static const CGFloat kStrappyInspectorGap = 8.0;
static const CGFloat kStrappyInspectorControlHeight = 24.0;
static const CGFloat kStrappyInspectorDocumentHeight = 526.0;

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

static NSString *StrappyInspectorAssistantSegmentTitle(NSDictionary *row)
{
  NSString *identifier;
  NSString *name;

  identifier = StrappyInspectorStringForRow(row, @"id");
  if ([identifier isEqualToString:@"world_knowledge"]) {
    return NSLocalizedString(@"World", nil);
  }
  if ([identifier isEqualToString:@"personal_assistant"]) {
    return NSLocalizedString(@"Personal", nil);
  }
  if ([identifier isEqualToString:@"coding_assistant"]) {
    return NSLocalizedString(@"Coding", nil);
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

static BOOL StrappyInspectorParseLimit(NSString *text, NSUInteger *limitOut)
{
  NSString *trimmed;
  NSCharacterSet *invalidCharacters;
  long long value;

  trimmed = [text stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  invalidCharacters = [[NSCharacterSet characterSetWithCharactersInString:
    @"0123456789"] invertedSet];
  if (([trimmed length] == 0U) ||
      ([trimmed rangeOfCharacterFromSet:invalidCharacters].location !=
       NSNotFound)) {
    return NO;
  }
  value = [trimmed XP_longLongValue];
  if ((value < 1LL) ||
      (value > (long long)StrappySessionMaximumLimit)) {
    return NO;
  }
  if (limitOut != NULL) {
    *limitOut = (NSUInteger)value;
  }
  return YES;
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
- (BOOL)canEditOptions;
- (void)populateModelPopUpWithOptions:(StrappySessionOptions *)options;
- (void)populateAssistantSegmentsWithOptions:(StrappySessionOptions *)options;
- (void)populateSearchProviderPopUpWithOptions:
    (StrappySessionOptions *)options;
- (void)updateControlEnabledStates;
- (BOOL)saveOptions:(StrappySessionOptions *)options
      changedFields:(StrappySessionOptionMask)changedFields;
- (void)modelChanged:(id)sender;
- (void)assistantChanged:(id)sender;
- (void)webSearchChanged:(id)sender;
- (void)bashChanged:(id)sender;
- (void)limitToOneToolChanged:(id)sender;
- (void)answerQualityChanged:(id)sender;
- (void)searchProviderChanged:(id)sender;
- (void)roundLimitChanged:(id)sender;
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
  }
  return self;
}

- (void)loadView
{
  NSView *view;

  view = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 300.0, 600.0)];
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
  [scrollView_ setHasVerticalScroller:YES];
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
    NSLocalizedString(@"Session Options", nil),
    [NSFont boldSystemFontOfSize:13.0]);
  [documentView_ addSubview:titleLabel_];

  statusLabel_ = StrappyInspectorLabel(@"",
    [NSFont systemFontOfSize:10.0]);
  [statusLabel_ setTextColor:[NSColor disabledControlTextColor]];
  [[statusLabel_ cell] setWraps:YES];
  [documentView_ addSubview:statusLabel_];

  modelAssistantBox_ = StrappyInspectorBox(
    NSLocalizedString(@"Model & Assistant", nil));
  toolsBox_ = StrappyInspectorBox(NSLocalizedString(@"Tools", nil));
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

  modelLabel_ = StrappyInspectorLabel(
    [NSString stringWithFormat:@"%@:", NSLocalizedString(@"Model", nil)],
    [NSFont systemFontOfSize:10.0]);
  assistantLabel_ = StrappyInspectorLabel(
    [NSString stringWithFormat:@"%@:", NSLocalizedString(@"Assistant", nil)],
    [NSFont systemFontOfSize:10.0]);
  [modelLabel_ setAutoresizingMask:NSViewWidthSizable];
  [assistantLabel_ setAutoresizingMask:NSViewWidthSizable];
  [documentView_ addSubview:modelLabel_];
  [documentView_ addSubview:assistantLabel_];

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

  roundLimitField_ = [[NSTextField alloc] initWithFrame:NSZeroRect];
  [roundLimitField_ setFont:[NSFont systemFontOfSize:11.0]];
  [roundLimitField_ setAlignment:XPTextAlignmentRight];
  [roundLimitField_ setDelegate:self];
  [roundLimitField_ setTarget:self];
  [roundLimitField_ setAction:@selector(roundLimitChanged:)];
  [documentView_ addSubview:roundLimitField_];

  searchProviderPopUpButton_ = [[NSPopUpButton alloc]
    initWithFrame:NSZeroRect pullsDown:NO];
  [searchProviderPopUpButton_ setFont:[NSFont systemFontOfSize:11.0]];
  [[searchProviderPopUpButton_ menu] setAutoenablesItems:NO];
  [searchProviderPopUpButton_ setTarget:self];
  [searchProviderPopUpButton_ setAction:@selector(searchProviderChanged:)];
  [documentView_ addSubview:searchProviderPopUpButton_];

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
  [limitsBox_ setFrame:NSMakeRect(contentX, y, contentWidth, 112.0)];
  [limitToOneToolButton_ setFrame:NSMakeRect(contentX + 12.0,
                                             y + 25.0,
                                             contentWidth - 24.0,
                                             20.0)];
  [answerQualityButton_ setFrame:NSMakeRect(contentX + 12.0,
                                            y + 50.0,
                                            contentWidth - 24.0,
                                            20.0)];
  [roundLimitLabel_ setFrame:NSMakeRect(contentX + 15.0,
                                        y + 80.0,
                                        contentWidth - 92.0,
                                        18.0)];
  [roundLimitField_ setFrame:NSMakeRect(contentX + contentWidth - 66.0,
                                        y + 75.0,
                                        54.0,
                                        kStrappyInspectorControlHeight)];

  y += 112.0 + kStrappyInspectorGap;
  [searchProviderBox_ setFrame:NSMakeRect(contentX, y, contentWidth, 72.0)];
  [searchProviderPopUpButton_ setFrame:NSMakeRect(
    contentX + 12.0,
    y + 29.0,
    contentWidth - 24.0,
    kStrappyInspectorControlHeight)];
}

- (void)reloadWithSession:(StrappySession *)session
{
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

- (BOOL)canEditOptions
{
  NSNumber *identifier;

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
  NSString *message;

  (void)[self view];
  if (reloading_) {
    return;
  }
  reloading_ = YES;

  options = (session_ != nil) ? [session_ optionsWithError:nil] : nil;
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
  [roundLimitField_ setStringValue:(options != nil) ?
    [NSString stringWithFormat:@"%lu", (unsigned long)[options roundLimit]] : @""];

  if ([statusText_ length] > 0U) {
    message = statusText_;
    [statusLabel_ setTextColor:[NSColor redColor]];
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
  [statusLabel_ setStringValue:message];
  [self updateControlEnabledStates];
  reloading_ = NO;
}

- (void)populateModelPopUpWithOptions:(StrappySessionOptions *)options
{
  NSArray *models;
  NSString *selectedIdentifier;
  NSMenuItem *selectedItem;
  NSUInteger index;
  NSUInteger validCount;

  models = [StrappySession allowedOpenRouterModelCatalogWithError:nil];
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
  for (index = 0U; index < [models count]; index++) {
    NSDictionary *row;
    NSString *identifier;
    NSMenuItem *item;

    row = [models objectAtIndex:index];
    identifier = StrappyInspectorStringForRow(row, @"id");
    if ([identifier length] == 0U) {
      continue;
    }
    [modelPopUpButton_ addItemWithTitle:StrappyInspectorModelTitle(row)];
    item = [modelPopUpButton_ lastItem];
    [item setRepresentedObject:identifier];
    validCount++;
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
      setLabel:StrappyInspectorAssistantSegmentTitle(row)
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

  enabled = [self canEditOptions];
  [modelPopUpButton_ setEnabled:
    (enabled && StrappyInspectorPopUpHasEnabledChoice(modelPopUpButton_))];
  [assistantSegmentedControl_ setEnabled:
    (enabled && ([assistantSegmentIdentifiers_ count] > 0U))];
  [webSearchButton_ setEnabled:enabled];
  [bashButton_ setEnabled:enabled];
  [limitToOneToolButton_ setEnabled:enabled];
  [answerQualityButton_ setEnabled:enabled];
  [roundLimitField_ setEnabled:enabled];
  [searchProviderPopUpButton_ setEnabled:enabled];
  [roundLimitLabel_ setTextColor:enabled ?
    [NSColor controlTextColor] : [NSColor disabledControlTextColor]];
}

- (BOOL)saveOptions:(StrappySessionOptions *)options
      changedFields:(StrappySessionOptionMask)changedFields
{
  NSError *error;
  NSString *message;

  if (reloading_ || ![self canEditOptions] || (options == nil) ||
      (changedFields == 0U)) {
    [self reloadOptions];
    return NO;
  }

  error = nil;
  if (![session_ updateOptions:options
                  changedFields:changedFields
                          error:&error]) {
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
  options = [[session_ optionsWithError:nil] copy];
  [options setModelIdentifier:identifier];
  (void)[self saveOptions:options changedFields:StrappySessionOptionModel];
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
  options = [[session_ optionsWithError:nil] copy];
  [options setAssistantSetIdentifier:identifier];
  (void)[self saveOptions:options
            changedFields:StrappySessionOptionAssistantSet];
  [options release];
}

- (void)webSearchChanged:(id)sender
{
  StrappySessionOptions *options;

  (void)sender;
  options = [[session_ optionsWithError:nil] copy];
  [options setWebSearchEnabled:([webSearchButton_ state] ==
    XPControlStateValueOn)];
  (void)[self saveOptions:options
            changedFields:StrappySessionOptionWebSearch];
  [options release];
}

- (void)bashChanged:(id)sender
{
  StrappySessionOptions *options;

  (void)sender;
  options = [[session_ optionsWithError:nil] copy];
  [options setBashEnabled:([bashButton_ state] == XPControlStateValueOn)];
  (void)[self saveOptions:options changedFields:StrappySessionOptionBash];
  [options release];
}

- (void)limitToOneToolChanged:(id)sender
{
  StrappySessionOptions *options;

  (void)sender;
  options = [[session_ optionsWithError:nil] copy];
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
  options = [[session_ optionsWithError:nil] copy];
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
  options = [[session_ optionsWithError:nil] copy];
  [options setWebProvider:provider];
  (void)[self saveOptions:options
            changedFields:StrappySessionOptionWebProvider];
  [options release];
}

- (void)roundLimitChanged:(id)sender
{
  StrappySessionOptions *options;
  NSString *message;
  NSUInteger limit;

  (void)sender;
  if (reloading_) {
    return;
  }
  if (!StrappyInspectorParseLimit([roundLimitField_ stringValue], &limit)) {
    message = [NSString stringWithFormat:
      NSLocalizedString(@"Round Limit must be between 1 and %lu.", nil),
      (unsigned long)StrappySessionMaximumLimit];
    [statusText_ release];
    statusText_ = [message copy];
    NSBeep();
    [self reloadOptions];
    return;
  }
  if (limit == [[session_ optionsWithError:nil] roundLimit]) {
    [self reloadOptions];
    return;
  }
  options = [[session_ optionsWithError:nil] copy];
  [options setRoundLimit:limit];
  (void)[self saveOptions:options
            changedFields:StrappySessionOptionRoundLimit];
  [options release];
}

- (void)controlTextDidEndEditing:(NSNotification *)notification
{
  if ([notification object] == roundLimitField_) {
    [self roundLimitChanged:roundLimitField_];
  }
}

- (void)documentViewFrameDidChange:(NSNotification *)notification
{
  (void)notification;
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
  [statusText_ release];
  [scrollView_ release];
  [documentView_ release];
  [titleLabel_ release];
  [statusLabel_ release];
  [modelAssistantBox_ release];
  [toolsBox_ release];
  [limitsBox_ release];
  [searchProviderBox_ release];
  [modelLabel_ release];
  [assistantLabel_ release];
  [modelPopUpButton_ release];
  [assistantSegmentedControl_ release];
  [assistantSegmentIdentifiers_ release];
  [webSearchButton_ release];
  [bashButton_ release];
  [limitToOneToolButton_ release];
  [answerQualityButton_ release];
  [roundLimitLabel_ release];
  [roundLimitField_ release];
  [searchProviderPopUpButton_ release];
  [super dealloc];
}

@end
