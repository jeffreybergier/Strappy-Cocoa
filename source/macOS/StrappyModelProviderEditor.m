#import "StrappyModelProviderEditor.h"

#import "StrappySession.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>

enum {
  kStrappyOtherModelPrimarySegment = 0,
  kStrappyOtherModelDeleteSegment = 1
};

static const CGFloat kStrappyEditorWidth = 700.0;
static const CGFloat kStrappyEditorHeight = 440.0;
static const CGFloat kStrappyEditorSidebarWidth = 190.0;
static const CGFloat kStrappyEditorInset = 16.0;

static NSString *StrappyEditorString(NSDictionary *row, NSString *key)
{
  id value;

  value = [row objectForKey:key];
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

static NSString *StrappyEditorOptionalTokenCount(NSDictionary *row,
                                                  NSString *key)
{
  id value;

  value = [row objectForKey:key];
  if (![value respondsToSelector:@selector(longLongValue)] ||
      ([value longLongValue] <= 0LL)) {
    return @"";
  }
  return [value isKindOfClass:[NSString class]] ?
    value : [value stringValue];
}

static NSString *StrappyEditorPricePerMillion(NSDictionary *row,
                                               NSString *key)
{
  NSString *value;
  double perToken;

  value = StrappyEditorString(row, key);
  if ([value length] == 0U) {
    return @"";
  }
  perToken = [value doubleValue];
  return [NSString stringWithFormat:@"%.12g", perToken * 1000000.0];
}

static NSString *StrappyEditorPricePerToken(id value, BOOL *valid)
{
  NSString *text;
  const char *bytes;
  char *end;
  double perMillion;

  if (valid != NULL) {
    *valid = YES;
  }
  text = [value isKindOfClass:[NSString class]] ?
    [(NSString *)value stringByTrimmingCharactersInSet:
      [NSCharacterSet whitespaceAndNewlineCharacterSet]] :
    ([value respondsToSelector:@selector(stringValue)] ?
      [value stringValue] : @"");
  if ([text length] == 0U) {
    return nil;
  }
  bytes = [text UTF8String];
  errno = 0;
  end = NULL;
  perMillion = strtod(bytes, &end);
  if ((errno == ERANGE) || (end == bytes) || (end == NULL) ||
      (*end != '\0') || !isfinite(perMillion) || (perMillion < 0.0)) {
    if (valid != NULL) {
      *valid = NO;
    }
    return nil;
  }
  return [NSString stringWithFormat:@"%.17g", perMillion / 1000000.0];
}

static NSTextField *StrappyEditorLabel(NSRect frame, NSString *text,
                                       NSFont *font)
{
  NSTextField *label;

  label = [[[NSTextField alloc] initWithFrame:frame] autorelease];
  [label setBezeled:NO];
  [label setDrawsBackground:NO];
  [label setEditable:NO];
  [label setSelectable:NO];
  [label setStringValue:(text != nil) ? text : @""];
  if (font != nil) {
    [label setFont:font];
  }
  return label;
}

static NSButton *StrappyEditorButton(NSRect frame, NSString *title,
                                     id target, SEL action)
{
  NSButton *button;

  button = [[[NSButton alloc] initWithFrame:frame] autorelease];
  [button setBezelStyle:XPBezelStyleRounded];
  [button setButtonType:XPButtonTypeMomentaryLight];
  [button setTitle:title];
  [button setTarget:target];
  [button setAction:action];
  return button;
}

static NSTableColumn *StrappyEditorTextColumn(NSString *identifier,
                                              NSString *title,
                                              CGFloat width,
                                              BOOL editable)
{
  NSTableColumn *column;

  column = [[[NSTableColumn alloc] initWithIdentifier:identifier] autorelease];
  [[column headerCell] setStringValue:title];
  [column setWidth:width];
  [column setMinWidth:50.0];
  [column setEditable:editable];
  return column;
}

static NSTableColumn *StrappyEditorCheckboxColumn(NSString *identifier,
                                                  NSString *title,
                                                  CGFloat width)
{
  NSTableColumn *column;
  NSButtonCell *cell;

  column = [[[NSTableColumn alloc] initWithIdentifier:identifier] autorelease];
  [[column headerCell] setStringValue:title];
  [column setWidth:width];
  [column setMinWidth:width];
  [column setMaxWidth:width];
  [column setEditable:YES];
  cell = [[[NSButtonCell alloc] init] autorelease];
  [cell setButtonType:XPButtonTypeSwitch];
  [cell setTitle:@""];
  [cell setAlignment:XPTextAlignmentCenter];
  [column setDataCell:cell];
  return column;
}

@interface StrappyEditorDividerView : NSView
@end

@implementation StrappyEditorDividerView

- (void)drawRect:(NSRect)dirtyRect
{
  (void)dirtyRect;
  [[NSColor gridColor] set];
  NSRectFill([self bounds]);
}

@end

@interface StrappyModelProviderEditor ()
- (void)reloadCatalog;
- (void)showSelectedProvider;
- (void)showOpenRouter;
- (void)showChatGPT;
- (void)showOther;
- (void)showEditableModelsForProviderWithTitle:(NSString *)title;
- (void)clearDetailView;
- (void)close:(id)sender;
- (void)fetchOpenRouterModels:(id)sender;
- (void)addOtherModel:(id)sender;
- (void)saveOtherModelDraft:(id)sender;
- (void)deleteOtherModel:(id)sender;
- (void)otherModelActionClicked:(id)sender;
- (void)modelCatalogDidChange:(NSNotification *)notification;
- (void)reloadCatalogAfterNotification:(id)ignored;
- (void)modelRefreshDidStart:(NSNotification *)notification;
- (void)modelRefreshDidFinish:(NSNotification *)notification;
- (void)setRefreshing:(BOOL)refreshing;
- (void)showError:(NSError *)error title:(NSString *)title;
- (void)showErrorMessage:(NSString *)message title:(NSString *)title;
- (void)reloadOtherModels;
- (void)updateOtherModelActions;
- (NSDictionary *)otherModelAtRow:(NSInteger)row;
- (BOOL)otherModelIsBuiltIn:(NSDictionary *)model;
- (BOOL)otherRowIsDraft:(NSInteger)row;
- (BOOL)saveOtherModel:(NSDictionary *)model
              creating:(BOOL)creating
                 error:(NSError **)error;
@end

@implementation StrappyModelProviderEditor

- (id)initWithTarget:(id)target
{
  NSView *contentView;
  NSScrollView *providerScrollView;
  NSTableColumn *providerColumn;
  StrappyEditorDividerView *divider;
  NSButton *closeButton;

  if ((self = [super init])) {
    target_ = target;
    providers_ = [[NSArray alloc] init];
    models_ = [[NSArray alloc] init];
    bundledChatGPTModels_ = [[NSArray alloc] init];
    otherModels_ = [[NSArray alloc] init];
    sheet_ = [[NSPanel alloc]
      initWithContentRect:NSMakeRect(0.0, 0.0,
                                     kStrappyEditorWidth,
                                     kStrappyEditorHeight)
                styleMask:XPWindowStyleMaskTitled
                  backing:NSBackingStoreBuffered
                    defer:NO];
    [sheet_ setTitle:NSLocalizedString(@"Edit Model Providers", nil)];
    [sheet_ setReleasedWhenClosed:NO];
    contentView = [sheet_ contentView];

    providerScrollView = [[[NSScrollView alloc]
      initWithFrame:NSMakeRect(0.0, 0.0,
                               kStrappyEditorSidebarWidth,
                               kStrappyEditorHeight)] autorelease];
    [providerScrollView setBorderType:NSNoBorder];
    [providerScrollView setHasVerticalScroller:YES];
    providerTableView_ = [[NSTableView alloc]
      initWithFrame:[[providerScrollView contentView] bounds]];
    providerColumn = StrappyEditorTextColumn(@"provider", @"Provider", 150.0,
                                             NO);
    [providerTableView_ addTableColumn:providerColumn];
    [providerTableView_ setHeaderView:nil];
    [providerTableView_ setDataSource:self];
    [providerTableView_ setDelegate:self];
    [providerTableView_ setAllowsMultipleSelection:NO];
    [providerTableView_ setAllowsEmptySelection:NO];
    [providerTableView_ XP_setSourceListStyle];
    [providerScrollView setDocumentView:providerTableView_];
    [contentView addSubview:providerScrollView];

    divider = [[[StrappyEditorDividerView alloc] initWithFrame:NSMakeRect(
      kStrappyEditorSidebarWidth, 0.0, 1.0, kStrappyEditorHeight)] autorelease];
    [contentView addSubview:divider];

    detailView_ = [[NSView alloc] initWithFrame:NSMakeRect(
      kStrappyEditorSidebarWidth + 1.0,
      0.0,
      kStrappyEditorWidth - kStrappyEditorSidebarWidth - 1.0,
      kStrappyEditorHeight)];
    [contentView addSubview:detailView_];
    closeButton = StrappyEditorButton(NSMakeRect(
      kStrappyEditorWidth - kStrappyEditorInset - 80.0,
      14.0, 80.0, 28.0),
      NSLocalizedString(@"Done", nil), self, @selector(close:));
    [contentView addSubview:closeButton];

    [[NSNotificationCenter defaultCenter]
      addObserver:self selector:@selector(modelCatalogDidChange:)
          name:StrappySessionModelCatalogDidChangeNotification object:nil];
    [[NSNotificationCenter defaultCenter]
      addObserver:self selector:@selector(modelRefreshDidStart:)
          name:StrappySessionModelCatalogRefreshDidStartNotification object:nil];
    [[NSNotificationCenter defaultCenter]
      addObserver:self selector:@selector(modelRefreshDidFinish:)
          name:StrappySessionModelCatalogRefreshDidFinishNotification object:nil];
    [self reloadCatalog];
  }
  return self;
}

- (void)beginSheetForWindow:(NSWindow *)window
{
  if ((window == nil) || (sheet_ == nil)) {
    return;
  }
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  [NSApp beginSheet:sheet_ modalForWindow:window modalDelegate:nil
      didEndSelector:NULL contextInfo:NULL];
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

- (void)reloadCatalog
{
  NSArray *providers;
  NSArray *models;
  NSArray *bundledChatGPTModels;
  NSError *error;

  error = nil;
  providers = [StrappySession providerCatalog];
  models = [StrappySession modelCatalogWithError:&error];
  bundledChatGPTModels = [StrappySession
    bundledModelCatalogForProviderIdentifier:@"openai_chatgpt" error:&error];
  if ((models == nil) || (bundledChatGPTModels == nil)) {
    [self showError:error title:NSLocalizedString(@"Could Not Load Models", nil)];
    return;
  }
  [providers_ release];
  providers_ = [providers copy];
  [models_ release];
  models_ = [models copy];
  [bundledChatGPTModels_ release];
  bundledChatGPTModels_ = [bundledChatGPTModels copy];

  [providerTableView_ reloadData];
  if (([selectedProviderIdentifier_ length] == 0U) &&
      ([providers_ count] > 0U)) {
    [selectedProviderIdentifier_ release];
    selectedProviderIdentifier_ =
      [[[providers_ objectAtIndex:0U] objectForKey:@"id"] copy];
    [providerTableView_ selectRowIndexes:[NSIndexSet indexSetWithIndex:0U]
                     byExtendingSelection:NO];
  }
  [self showSelectedProvider];
}

- (void)clearDetailView
{
  NSArray *subviews;
  NSUInteger index;

  subviews = [[detailView_ subviews] copy];
  for (index = 0U; index < [subviews count]; index++) {
    [[subviews objectAtIndex:index] removeFromSuperview];
  }
  [subviews release];
  otherTableView_ = nil;
  fetchButton_ = nil;
  otherModelActionsSegmented_ = nil;
  progressIndicator_ = nil;
}

- (void)showSelectedProvider
{
  NSRect bounds;
  CGFloat width;

  [self clearDetailView];
  bounds = [detailView_ bounds];
  width = NSWidth(bounds) - (2.0 * kStrappyEditorInset);
  if ([selectedProviderIdentifier_ isEqualToString:@"openrouter"]) {
    [self showOpenRouter];
  } else if ([selectedProviderIdentifier_ isEqualToString:@"openai_chatgpt"]) {
    [self showChatGPT];
  } else if ([selectedProviderIdentifier_ isEqualToString:@"other"]) {
    [self showOther];
  } else {
    [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(
      kStrappyEditorInset, NSHeight(bounds) - 38.0, width, 22.0),
      selectedProviderIdentifier_, XPFontTextStyleBoldBody)];
    [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(
      kStrappyEditorInset, NSHeight(bounds) - 76.0, width, 34.0),
      NSLocalizedString(
        @"This provider does not expose model catalog controls.", nil),
      XPFontTextStyleBody)];
  }
}

- (void)showOpenRouter
{
  NSRect bounds;
  CGFloat width;

  bounds = [detailView_ bounds];
  width = NSWidth(bounds) - (2.0 * kStrappyEditorInset);
  [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(
    kStrappyEditorInset, NSHeight(bounds) - 38.0, width, 22.0),
    @"OpenRouter", XPFontTextStyleBoldBody)];
  [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(
    kStrappyEditorInset, NSHeight(bounds) - 90.0, width, 44.0),
    NSLocalizedString(
      @"Fetch the latest model catalog for the configured OpenRouter account. Existing whitelist choices are preserved.",
      nil), XPFontTextStyleBody)];
  fetchButton_ = StrappyEditorButton(NSMakeRect(
    kStrappyEditorInset, NSHeight(bounds) - 132.0, 112.0, 28.0),
    NSLocalizedString(@"Fetch Models", nil), self,
    @selector(fetchOpenRouterModels:));
  [detailView_ addSubview:fetchButton_];
  progressIndicator_ = [[[NSProgressIndicator alloc]
    initWithFrame:NSMakeRect(kStrappyEditorInset + 124.0,
                             NSHeight(bounds) - 127.0,
                             18.0, 18.0)] autorelease];
  [progressIndicator_ setStyle:XPProgressIndicatorStyleSpinning];
  [progressIndicator_ setIndeterminate:YES];
  [progressIndicator_ setDisplayedWhenStopped:NO];
  [detailView_ addSubview:progressIndicator_];
  [self setRefreshing:[StrappySession isModelCatalogRefreshInFlight]];
}

- (void)showChatGPT
{
  [self showEditableModelsForProviderWithTitle:@"ChatGPT"];
}

- (void)reloadOtherModels
{
  NSMutableArray *rows;
  NSUInteger index;

  rows = [NSMutableArray array];
  for (index = 0U; index < [models_ count]; index++) {
    NSDictionary *model;

    model = [models_ objectAtIndex:index];
    if ([[model objectForKey:@"provider_id"]
          isEqualToString:selectedProviderIdentifier_]) {
      [rows addObject:model];
    }
  }
  [otherModels_ release];
  otherModels_ = [rows copy];
  [otherTableView_ reloadData];
  [self updateOtherModelActions];
}

- (void)showOther
{
  [self showEditableModelsForProviderWithTitle:NSLocalizedString(@"Custom", nil)];
}

- (void)showEditableModelsForProviderWithTitle:(NSString *)title
{
  NSRect bounds;
  CGFloat width;
  NSScrollView *scrollView;

  bounds = [detailView_ bounds];
  width = NSWidth(bounds) - (2.0 * kStrappyEditorInset);
  [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(
    kStrappyEditorInset, NSHeight(bounds) - 38.0, width, 22.0),
    title, XPFontTextStyleBoldBody)];
  [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(
    kStrappyEditorInset, NSHeight(bounds) - 66.0, width, 24.0),
    NSLocalizedString(
      @"Only Model ID is required. Existing model edits save automatically.",
      nil), XPFontTextStyleBody)];
  scrollView = [[[NSScrollView alloc]
    initWithFrame:NSMakeRect(kStrappyEditorInset,
                             56.0,
                             width,
                             NSHeight(bounds) - 134.0)] autorelease];
  [scrollView setBorderType:NSBezelBorder];
  [scrollView setHasVerticalScroller:YES];
  [scrollView setHasHorizontalScroller:YES];
  otherTableView_ = [[[NSTableView alloc]
    initWithFrame:[[scrollView contentView] bounds]] autorelease];
  [otherTableView_ addTableColumn:StrappyEditorTextColumn(@"wire_model_id",
    @"Model ID (Required)", 180.0, YES)];
  [otherTableView_ addTableColumn:StrappyEditorTextColumn(@"name",
    @"Name (Optional)", 150.0, YES)];
  [otherTableView_ addTableColumn:StrappyEditorTextColumn(@"context_length",
    @"Context (Optional)", 120.0, YES)];
  [otherTableView_ addTableColumn:StrappyEditorTextColumn(
    @"top_provider_max_completion_tokens", @"Max Output (Optional)",
    145.0, YES)];
  [otherTableView_ addTableColumn:StrappyEditorCheckboxColumn(
    @"reasoning_enabled", @"Reasoning (Optional)", 130.0)];
  /* Keep image_input_enabled in model drafts and persistence, but hide its
   * column until Strappy supports image input or output.
  [otherTableView_ addTableColumn:StrappyEditorCheckboxColumn(
    @"image_input_enabled", @"Images (Optional)", 115.0)];
   */
  [otherTableView_ addTableColumn:StrappyEditorTextColumn(@"pricing_prompt",
    @"Input $/1M (Optional)", 140.0, YES)];
  [otherTableView_ addTableColumn:StrappyEditorTextColumn(@"pricing_completion",
    @"Output $/1M (Optional)", 145.0, YES)];
  [otherTableView_ addTableColumn:StrappyEditorTextColumn(
    @"pricing_input_cache_read", @"Cache Read $/1M (Optional)", 175.0, YES)];
  [otherTableView_ addTableColumn:StrappyEditorTextColumn(
    @"pricing_input_cache_write", @"Cache Write $/1M (Optional)", 180.0, YES)];
  [otherTableView_ setDataSource:self];
  [otherTableView_ setDelegate:self];
  [scrollView setDocumentView:otherTableView_];
  [detailView_ addSubview:scrollView];

  otherModelActionsSegmented_ = [[[NSSegmentedControl alloc]
    initWithFrame:NSMakeRect(kStrappyEditorInset, 14.0, 168.0, 28.0)]
    autorelease];
  [otherModelActionsSegmented_ setSegmentCount:2];
  [[otherModelActionsSegmented_ cell]
    setTrackingMode:NSSegmentSwitchTrackingMomentary];
  [otherModelActionsSegmented_ setWidth:84.0
                             forSegment:kStrappyOtherModelPrimarySegment];
  [otherModelActionsSegmented_ setWidth:84.0
                             forSegment:kStrappyOtherModelDeleteSegment];
  [otherModelActionsSegmented_ setLabel:NSLocalizedString(@"Delete", nil)
                             forSegment:kStrappyOtherModelDeleteSegment];
  [otherModelActionsSegmented_ setTarget:self];
  [otherModelActionsSegmented_
    setAction:@selector(otherModelActionClicked:)];
  [detailView_ addSubview:otherModelActionsSegmented_];
  [self reloadOtherModels];
}

- (void)updateOtherModelActions
{
  NSInteger row;
  NSString *primaryTitle;
  BOOL hasDraft;
  BOOL canDelete;

  if (otherModelActionsSegmented_ == nil) {
    return;
  }
  hasDraft = (draftOtherModel_ != nil);
  primaryTitle = hasDraft ? NSLocalizedString(@"Save", nil) :
    NSLocalizedString(@"Add", nil);
  [otherModelActionsSegmented_
    setLabel:primaryTitle forSegment:kStrappyOtherModelPrimarySegment];
  [otherModelActionsSegmented_ setEnabled:YES
                              forSegment:kStrappyOtherModelPrimarySegment];
  row = [otherTableView_ selectedRow];
  canDelete = [self otherRowIsDraft:row] ||
    (([self otherModelAtRow:row] != nil) &&
     ![self otherModelIsBuiltIn:[self otherModelAtRow:row]]);
  [otherModelActionsSegmented_ setEnabled:canDelete
                              forSegment:kStrappyOtherModelDeleteSegment];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
  if (tableView == providerTableView_) {
    return (NSInteger)[providers_ count];
  }
  if (tableView == otherTableView_) {
    return (NSInteger)[otherModels_ count] +
      ((draftOtherModel_ != nil) ? 1 : 0);
  }
  return 0;
}

- (id)tableView:(NSTableView *)tableView
 objectValueForTableColumn:(NSTableColumn *)tableColumn
             row:(NSInteger)row
{
  NSDictionary *item;
  NSString *identifier;

  if (tableView == providerTableView_) {
    if ((row < 0) || (row >= (NSInteger)[providers_ count])) {
      return @"";
    }
    return StrappyEditorString([providers_ objectAtIndex:(NSUInteger)row],
                               @"name");
  }
  item = [self otherRowIsDraft:row] ? draftOtherModel_ :
    [self otherModelAtRow:row];
  identifier = [tableColumn identifier];
  if ([identifier isEqualToString:@"name"] ||
      [identifier isEqualToString:@"wire_model_id"]) {
    if ((tableView == otherTableView_) &&
        [identifier isEqualToString:@"name"] &&
        ![self otherRowIsDraft:row] &&
        [StrappyEditorString(item, @"name")
          isEqualToString:StrappyEditorString(item, @"wire_model_id")]) {
      return @"";
    }
    return StrappyEditorString(item, identifier);
  }
  if ((tableView == otherTableView_) &&
      ([identifier isEqualToString:@"context_length"] ||
       [identifier isEqualToString:
         @"top_provider_max_completion_tokens"])) {
    return [self otherRowIsDraft:row] ?
      StrappyEditorString(item, identifier) :
      StrappyEditorOptionalTokenCount(item, identifier);
  }
  if ((tableView == otherTableView_) &&
      ([identifier isEqualToString:@"pricing_prompt"] ||
       [identifier isEqualToString:@"pricing_completion"] ||
       [identifier isEqualToString:@"pricing_input_cache_read"] ||
       [identifier isEqualToString:@"pricing_input_cache_write"])) {
    return [self otherRowIsDraft:row] ?
      StrappyEditorString(item, identifier) :
      StrappyEditorPricePerMillion(item, identifier);
  }
  return [item objectForKey:identifier];
}

- (BOOL)tableView:(NSTableView *)tableView
 shouldEditTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
  return (tableView == otherTableView_) &&
    ([self otherRowIsDraft:row] ||
     (([self otherModelAtRow:row] != nil) &&
      ![self otherModelIsBuiltIn:[self otherModelAtRow:row]] &&
      ![[tableColumn identifier] isEqualToString:@"wire_model_id"]));
}

- (BOOL)tableView:(NSTableView *)tableView shouldSelectRow:(NSInteger)row
{
  NSDictionary *model;

  if (tableView != otherTableView_) {
    return YES;
  }
  if ([self otherRowIsDraft:row]) {
    return YES;
  }
  model = [self otherModelAtRow:row];
  return ((model != nil) && ![self otherModelIsBuiltIn:model]) ? YES : NO;
}

- (void)tableView:(NSTableView *)tableView
  willDisplayCell:(id)cell
   forTableColumn:(NSTableColumn *)tableColumn
              row:(NSInteger)row
{
  NSDictionary *model;

  (void)tableColumn;
  if (tableView != otherTableView_) {
    return;
  }
  model = [self otherRowIsDraft:row] ? nil : [self otherModelAtRow:row];
  [cell setEnabled:(model == nil) || ![self otherModelIsBuiltIn:model]];
}

- (void)tableView:(NSTableView *)tableView
   setObjectValue:(id)object
   forTableColumn:(NSTableColumn *)tableColumn
              row:(NSInteger)row
{
  NSDictionary *model;
  NSMutableDictionary *updated;
  NSMutableArray *rows;
  NSString *identifier;
  NSUInteger modelIndex;
  BOOL draft;
  BOOL priceValid;
  NSError *error;

  if (tableView != otherTableView_) {
    return;
  }
  draft = [self otherRowIsDraft:row];
  model = draft ? draftOtherModel_ : [self otherModelAtRow:row];
  if ((model == nil) || (!draft && [self otherModelIsBuiltIn:model])) {
    return;
  }
  identifier = [tableColumn identifier];
  updated = [NSMutableDictionary dictionaryWithDictionary:model];
  if (object != nil) {
    [updated setObject:object forKey:identifier];
  }
  if ([identifier isEqualToString:@"wire_model_id"] ||
      [identifier isEqualToString:@"name"]) {
    [updated setObject:[StrappyEditorString(updated, identifier)
      stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]]
                forKey:identifier];
  }
  if (!draft &&
      ([identifier isEqualToString:@"pricing_prompt"] ||
       [identifier isEqualToString:@"pricing_completion"] ||
       [identifier isEqualToString:@"pricing_input_cache_read"] ||
       [identifier isEqualToString:@"pricing_input_cache_write"])) {
    NSString *price;

    priceValid = YES;
    price = StrappyEditorPricePerToken(object, &priceValid);
    if (!priceValid) {
      [otherTableView_ reloadData];
      [self showError:nil
                title:NSLocalizedString(@"Price Must Be Zero or Greater", nil)];
      return;
    }
    if (price != nil) {
      [updated setObject:price forKey:identifier];
    } else {
      [updated removeObjectForKey:identifier];
    }
  }
  if (draft) {
    [draftOtherModel_ setDictionary:updated];
    [self updateOtherModelActions];
    return;
  }
  error = nil;
  if (![self saveOtherModel:updated creating:draft error:&error]) {
    [otherTableView_ reloadData];
    [self showError:error title:NSLocalizedString(@"Could Not Save Model", nil)];
  } else {
    rows = [NSMutableArray arrayWithArray:otherModels_];
    [rows replaceObjectAtIndex:(NSUInteger)row withObject:updated];
    [otherModels_ release];
    otherModels_ = [rows copy];
    modelIndex = [models_ indexOfObjectIdenticalTo:model];
    if (modelIndex != NSNotFound) {
      rows = [NSMutableArray arrayWithArray:models_];
      [rows replaceObjectAtIndex:modelIndex withObject:updated];
      [models_ release];
      models_ = [rows copy];
    }
  }
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
  NSTableView *tableView;
  NSInteger row;

  tableView = [notification object];
  row = [tableView selectedRow];
  if (tableView == providerTableView_) {
    if ((row >= 0) && (row < (NSInteger)[providers_ count])) {
      NSString *providerIdentifier;

      providerIdentifier = [[providers_ objectAtIndex:(NSUInteger)row]
        objectForKey:@"id"];
      if ((draftOtherModel_ != nil) &&
          ![providerIdentifier isEqualToString:selectedProviderIdentifier_]) {
        [draftOtherModel_ release];
        draftOtherModel_ = nil;
      }
      [selectedProviderIdentifier_ release];
      selectedProviderIdentifier_ = [providerIdentifier copy];
      [self showSelectedProvider];
    }
  } else if (tableView == otherTableView_) {
    [self updateOtherModelActions];
  }
}

- (BOOL)otherRowIsDraft:(NSInteger)row
{
  return (draftOtherModel_ != nil) &&
    (row == (NSInteger)[otherModels_ count]);
}

- (BOOL)saveOtherModel:(NSDictionary *)model
              creating:(BOOL)creating
                 error:(NSError **)error
{
  NSString *inputPrice;
  NSString *outputPrice;
  NSString *cacheReadPrice;
  NSString *cacheWritePrice;
  BOOL valid;

  valid = YES;
  inputPrice = nil;
  outputPrice = nil;
  cacheReadPrice = nil;
  cacheWritePrice = nil;
  if (creating) {
    inputPrice = StrappyEditorPricePerToken(
      [model objectForKey:@"pricing_prompt"], &valid);
    if (valid) outputPrice = StrappyEditorPricePerToken(
      [model objectForKey:@"pricing_completion"], &valid);
    if (valid) cacheReadPrice = StrappyEditorPricePerToken(
      [model objectForKey:@"pricing_input_cache_read"], &valid);
    if (valid) cacheWritePrice = StrappyEditorPricePerToken(
      [model objectForKey:@"pricing_input_cache_write"], &valid);
  } else {
    inputPrice = StrappyEditorString(model, @"pricing_prompt");
    outputPrice = StrappyEditorString(model, @"pricing_completion");
    cacheReadPrice = StrappyEditorString(model, @"pricing_input_cache_read");
    cacheWritePrice = StrappyEditorString(model, @"pricing_input_cache_write");
  }
  if (!valid) {
    if (error != nil) {
      *error = [NSError errorWithDomain:@"StrappyAssistantErrorDomain"
                                   code:16
                               userInfo:[NSDictionary dictionaryWithObject:
        NSLocalizedString(@"Prices must be numbers greater than or equal to zero.", nil)
                                                            forKey:NSLocalizedDescriptionKey]];
    }
    return NO;
  }
  if (creating) {
    return [StrappySession
      createManualModelForProviderIdentifier:selectedProviderIdentifier_
      wireModelID:StrappyEditorString(model, @"wire_model_id")
      displayName:StrappyEditorString(model, @"name")
      contextWindowTokens:[[model objectForKey:@"context_length"] longLongValue]
      maxOutputTokens:[[model objectForKey:
        @"top_provider_max_completion_tokens"] longLongValue]
      reasoningEnabled:[[model objectForKey:@"reasoning_enabled"] boolValue]
      imageInputEnabled:[[model objectForKey:@"image_input_enabled"] boolValue]
      localFunctionsEnabled:YES
      inputPricePerToken:inputPrice
      outputPricePerToken:outputPrice
      cacheReadPricePerToken:cacheReadPrice
      cacheWritePricePerToken:cacheWritePrice
      error:error] != nil;
  }
  return [StrappySession
    updateManualModelForProviderIdentifier:selectedProviderIdentifier_
    wireModelID:StrappyEditorString(model, @"wire_model_id")
    displayName:StrappyEditorString(model, @"name")
    contextWindowTokens:[[model objectForKey:@"context_length"] longLongValue]
    maxOutputTokens:[[model objectForKey:
      @"top_provider_max_completion_tokens"] longLongValue]
    reasoningEnabled:[[model objectForKey:@"reasoning_enabled"] boolValue]
    imageInputEnabled:[[model objectForKey:@"image_input_enabled"] boolValue]
    localFunctionsEnabled:YES
    inputPricePerToken:inputPrice
    outputPricePerToken:outputPrice
    cacheReadPricePerToken:cacheReadPrice
    cacheWritePricePerToken:cacheWritePrice
    error:error];
}

- (NSDictionary *)otherModelAtRow:(NSInteger)row
{
  if ((row < 0) || (row >= (NSInteger)[otherModels_ count])) {
    return nil;
  }
  return [otherModels_ objectAtIndex:(NSUInteger)row];
}

- (BOOL)otherModelIsBuiltIn:(NSDictionary *)model
{
  NSString *wireModelID;
  NSUInteger index;

  if (![selectedProviderIdentifier_ isEqualToString:@"openai_chatgpt"] ||
      (model == nil)) {
    return NO;
  }
  wireModelID = StrappyEditorString(model, @"wire_model_id");
  for (index = 0U; index < [bundledChatGPTModels_ count]; index++) {
    if ([wireModelID isEqualToString:StrappyEditorString(
          [bundledChatGPTModels_ objectAtIndex:index], @"wire_model_id")]) {
      return YES;
    }
  }
  return NO;
}

- (void)addOtherModel:(id)sender
{
  NSInteger row;

  (void)sender;
  if (draftOtherModel_ == nil) {
    draftOtherModel_ = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
      @"", @"wire_model_id",
      @"", @"name",
      @"", @"context_length",
      @"", @"top_provider_max_completion_tokens",
      [NSNumber numberWithBool:YES], @"reasoning_enabled",
      [NSNumber numberWithBool:NO], @"image_input_enabled",
      @"", @"pricing_prompt",
      @"", @"pricing_completion",
      @"", @"pricing_input_cache_read",
      @"", @"pricing_input_cache_write",
      nil];
    [otherTableView_ reloadData];
    [self updateOtherModelActions];
  }
  row = (NSInteger)[otherModels_ count];
  [otherTableView_ selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
               byExtendingSelection:NO];
  [otherTableView_ scrollRowToVisible:row];
  [otherTableView_ editColumn:0 row:row withEvent:nil select:YES];
}

- (void)otherModelActionClicked:(id)sender
{
  NSInteger segment;

  if (![sender isKindOfClass:[NSSegmentedControl class]]) {
    return;
  }
  segment = [(NSSegmentedControl *)sender selectedSegment];
  if (segment == kStrappyOtherModelPrimarySegment) {
    if (draftOtherModel_ != nil) {
      [self saveOtherModelDraft:sender];
    } else {
      if (![sheet_ makeFirstResponder:otherTableView_]) {
        NSBeep();
        return;
      }
      [self addOtherModel:sender];
    }
  } else if (segment == kStrappyOtherModelDeleteSegment) {
    [self deleteOtherModel:sender];
  }
}

- (void)saveOtherModelDraft:(id)sender
{
  NSError *error;

  (void)sender;
  if (![sheet_ makeFirstResponder:otherTableView_]) {
    NSBeep();
    return;
  }
  if ((draftOtherModel_ == nil) ||
      ([StrappyEditorString(draftOtherModel_, @"wire_model_id") length] == 0U)) {
    [self showErrorMessage:NSLocalizedString(
      @"Enter a Model ID before saving.", nil)
                      title:NSLocalizedString(@"Model ID Is Required", nil)];
    return;
  }
  error = nil;
  if (![self saveOtherModel:draftOtherModel_ creating:YES error:&error]) {
    [self showError:error title:NSLocalizedString(@"Could Not Save Model", nil)];
    return;
  }
  [draftOtherModel_ release];
  draftOtherModel_ = nil;
  [self reloadCatalog];
}

- (void)deleteOtherModel:(id)sender
{
  NSDictionary *model;
  NSError *error;

  (void)sender;
  [otherTableView_ abortEditing];
  if ([self otherRowIsDraft:[otherTableView_ selectedRow]]) {
    [draftOtherModel_ release];
    draftOtherModel_ = nil;
    [otherTableView_ reloadData];
    [self updateOtherModelActions];
    return;
  }
  model = [self otherModelAtRow:[otherTableView_ selectedRow]];
  if ((model == nil) || [self otherModelIsBuiltIn:model]) {
    NSBeep();
    return;
  }
  error = nil;
  if (![StrappySession
      archiveManualModelForProviderIdentifier:selectedProviderIdentifier_
      wireModelID:StrappyEditorString(model, @"wire_model_id") error:&error]) {
    [self showError:error title:NSLocalizedString(@"Could Not Delete Model", nil)];
  } else {
    [self reloadCatalog];
  }
}

- (void)fetchOpenRouterModels:(id)sender
{
  NSError *error;

  (void)sender;
  error = nil;
  if (![StrappySession beginOpenRouterModelCatalogRefreshWithError:&error]) {
    [self showError:error title:NSLocalizedString(@"Could Not Fetch Models", nil)];
  }
}

- (void)setRefreshing:(BOOL)refreshing
{
  [fetchButton_ setEnabled:refreshing ? NO : YES];
  if (refreshing) {
    [progressIndicator_ startAnimation:self];
  } else {
    [progressIndicator_ stopAnimation:self];
  }
}

- (void)modelCatalogDidChange:(NSNotification *)notification
{
  (void)notification;
  if ([selectedProviderIdentifier_ isEqualToString:@"other"] ||
      [selectedProviderIdentifier_ isEqualToString:@"openai_chatgpt"]) {
    return;
  }
  [self performSelector:@selector(reloadCatalogAfterNotification:)
             withObject:nil afterDelay:0.0];
}

- (void)reloadCatalogAfterNotification:(id)ignored
{
  (void)ignored;
  if ([selectedProviderIdentifier_ isEqualToString:@"other"] ||
      [selectedProviderIdentifier_ isEqualToString:@"openai_chatgpt"]) {
    return;
  }
  [self reloadCatalog];
}

- (void)modelRefreshDidStart:(NSNotification *)notification
{
  (void)notification;
  [self setRefreshing:YES];
}

- (void)modelRefreshDidFinish:(NSNotification *)notification
{
  NSString *message;

  [self setRefreshing:NO];
  message = [[notification userInfo] objectForKey:@"error"];
  if ([message isKindOfClass:[NSString class]] && ([message length] > 0U)) {
    [self showErrorMessage:message
                     title:NSLocalizedString(@"Could Not Fetch Models", nil)];
  }
}

- (void)showError:(NSError *)error title:(NSString *)title
{
  NSString *message;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = NSLocalizedString(@"The model catalog could not be changed.", nil);
  }
  [self showErrorMessage:message title:title];
}

- (void)showErrorMessage:(NSString *)message title:(NSString *)title
{
  NSAlert *alert;

  alert = [[[NSAlert alloc] init] autorelease];
  [alert setMessageText:title];
  [alert setInformativeText:message];
  [alert addButtonWithTitle:NSLocalizedString(@"OK", nil)];
  [alert XP_beginSheetModalForWindow:sheet_
                       modalDelegate:nil
                      didEndSelector:NULL
                         contextInfo:NULL];
}

- (void)close:(id)sender
{
  (void)sender;
  [NSApp endSheet:sheet_];
  [sheet_ orderOut:self];
  if ([target_ respondsToSelector:@selector(modelProviderEditorDidClose:)]) {
    [target_ performSelector:@selector(modelProviderEditorDidClose:)
                  withObject:self];
  }
}

- (void)dealloc
{
  [NSObject cancelPreviousPerformRequestsWithTarget:self];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [providerTableView_ setDataSource:nil];
  [providerTableView_ setDelegate:nil];
  [otherTableView_ setDataSource:nil];
  [otherTableView_ setDelegate:nil];
  [sheet_ release];
  [providerTableView_ release];
  [detailView_ release];
  [providers_ release];
  [models_ release];
  [bundledChatGPTModels_ release];
  [otherModels_ release];
  [draftOtherModel_ release];
  [selectedProviderIdentifier_ release];
  [super dealloc];
}

@end
