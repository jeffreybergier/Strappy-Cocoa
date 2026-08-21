#import "StrappyModelProviderEditor.h"

#import "StrappySession.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>

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

@interface StrappyModelProviderEditor ()
- (void)reloadCatalog;
- (void)showSelectedProvider;
- (void)showOpenRouter;
- (void)showChatGPT;
- (void)showOther;
- (void)clearDetailView;
- (void)close:(id)sender;
- (void)fetchOpenRouterModels:(id)sender;
- (void)addOtherModel:(id)sender;
- (void)saveOtherModelDraft:(id)sender;
- (void)deleteOtherModel:(id)sender;
- (void)modelCatalogDidChange:(NSNotification *)notification;
- (void)reloadCatalogAfterNotification:(id)ignored;
- (void)modelRefreshDidStart:(NSNotification *)notification;
- (void)modelRefreshDidFinish:(NSNotification *)notification;
- (void)setRefreshing:(BOOL)refreshing;
- (void)showError:(NSError *)error title:(NSString *)title;
- (void)reloadOtherModels;
- (NSDictionary *)otherModelAtRow:(NSInteger)row;
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
  NSButton *closeButton;

  if ((self = [super init])) {
    target_ = target;
    providers_ = [[NSArray alloc] init];
    models_ = [[NSArray alloc] init];
    chatGPTModels_ = [[NSArray alloc] init];
    otherModels_ = [[NSArray alloc] init];
    sheet_ = [[NSPanel alloc]
      initWithContentRect:NSMakeRect(0.0, 0.0, 700.0, 440.0)
                styleMask:XPWindowStyleMaskTitled
                  backing:NSBackingStoreBuffered
                    defer:NO];
    [sheet_ setTitle:NSLocalizedString(@"Edit Model Providers", nil)];
    [sheet_ setReleasedWhenClosed:NO];
    contentView = [sheet_ contentView];

    providerScrollView = [[[NSScrollView alloc]
      initWithFrame:NSMakeRect(16.0, 56.0, 170.0, 364.0)] autorelease];
    [providerScrollView setBorderType:NSBezelBorder];
    [providerScrollView setHasVerticalScroller:YES];
    providerTableView_ = [[NSTableView alloc]
      initWithFrame:[[providerScrollView contentView] bounds]];
    providerColumn = StrappyEditorTextColumn(@"provider", @"Provider", 150.0,
                                             NO);
    [providerTableView_ addTableColumn:providerColumn];
    [providerTableView_ setHeaderView:nil];
    [providerTableView_ setDataSource:self];
    [providerTableView_ setDelegate:self];
    [providerScrollView setDocumentView:providerTableView_];
    [contentView addSubview:providerScrollView];

    detailView_ = [[NSView alloc] initWithFrame:NSMakeRect(202.0, 56.0,
                                                           482.0, 364.0)];
    [contentView addSubview:detailView_];
    closeButton = StrappyEditorButton(NSMakeRect(604.0, 14.0, 80.0, 28.0),
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
  [chatGPTModels_ release];
  chatGPTModels_ = [bundledChatGPTModels copy];

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
  chatGPTTableView_ = nil;
  otherTableView_ = nil;
  fetchButton_ = nil;
  saveButton_ = nil;
  deleteButton_ = nil;
  progressIndicator_ = nil;
  statusLabel_ = nil;
}

- (void)showSelectedProvider
{
  [self clearDetailView];
  if ([selectedProviderIdentifier_ isEqualToString:@"openrouter"]) {
    [self showOpenRouter];
  } else if ([selectedProviderIdentifier_ isEqualToString:@"openai_chatgpt"]) {
    [self showChatGPT];
  } else if ([selectedProviderIdentifier_ isEqualToString:@"other"]) {
    [self showOther];
  } else {
    [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(0.0, 330.0,
      470.0, 22.0), selectedProviderIdentifier_, XPFontTextStyleBoldBody)];
    [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(0.0, 296.0,
      470.0, 34.0), NSLocalizedString(@"This provider does not expose model catalog controls.", nil), XPFontTextStyleBody)];
  }
}

- (void)showOpenRouter
{
  [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(0.0, 330.0, 470.0,
    22.0), @"OpenRouter", XPFontTextStyleBoldBody)];
  [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(0.0, 282.0, 470.0,
    44.0), NSLocalizedString(@"Fetch the latest model catalog for the configured OpenRouter account. Existing whitelist choices are preserved.", nil), XPFontTextStyleBody)];
  fetchButton_ = StrappyEditorButton(NSMakeRect(0.0, 240.0, 112.0, 28.0),
    NSLocalizedString(@"Fetch Models", nil), self,
    @selector(fetchOpenRouterModels:));
  [detailView_ addSubview:fetchButton_];
  progressIndicator_ = [[[NSProgressIndicator alloc]
    initWithFrame:NSMakeRect(124.0, 245.0, 18.0, 18.0)] autorelease];
  [progressIndicator_ setStyle:XPProgressIndicatorStyleSpinning];
  [progressIndicator_ setIndeterminate:YES];
  [detailView_ addSubview:progressIndicator_];
  statusLabel_ = StrappyEditorLabel(NSMakeRect(150.0, 241.0, 320.0, 24.0),
                                    @"", XPFontTextStyleSmallLabel);
  [detailView_ addSubview:statusLabel_];
  [self setRefreshing:[StrappySession isModelCatalogRefreshInFlight]];
}

- (void)showChatGPT
{
  NSScrollView *scrollView;

  [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(0.0, 330.0, 470.0,
    22.0), @"ChatGPT", XPFontTextStyleBoldBody)];
  [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(0.0, 302.0, 470.0,
    24.0), NSLocalizedString(@"Built-in model list", nil), XPFontTextStyleBody)];
  scrollView = [[[NSScrollView alloc]
    initWithFrame:NSMakeRect(0.0, 0.0, 480.0, 294.0)] autorelease];
  [scrollView setBorderType:NSBezelBorder];
  [scrollView setHasVerticalScroller:YES];
  [scrollView setHasHorizontalScroller:YES];
  chatGPTTableView_ = [[[NSTableView alloc]
    initWithFrame:[[scrollView contentView] bounds]] autorelease];
  [chatGPTTableView_ addTableColumn:StrappyEditorTextColumn(@"name", @"Model",
                                                            190.0, NO)];
  [chatGPTTableView_ addTableColumn:StrappyEditorTextColumn(@"wire_model_id",
    @"Model ID", 255.0, NO)];
  [chatGPTTableView_ setDataSource:self];
  [chatGPTTableView_ setDelegate:self];
  [scrollView setDocumentView:chatGPTTableView_];
  [detailView_ addSubview:scrollView];
}

- (void)reloadOtherModels
{
  NSMutableArray *rows;
  NSUInteger index;

  rows = [NSMutableArray array];
  for (index = 0U; index < [models_ count]; index++) {
    NSDictionary *model;

    model = [models_ objectAtIndex:index];
    if ([[model objectForKey:@"provider_id"] isEqualToString:@"other"]) {
      [rows addObject:model];
    }
  }
  [otherModels_ release];
  otherModels_ = [rows copy];
  [otherTableView_ reloadData];
  [saveButton_ setEnabled:(draftOtherModel_ != nil) &&
    ([StrappyEditorString(draftOtherModel_, @"wire_model_id") length] > 0U)];
  [deleteButton_ setEnabled:NO];
}

- (void)showOther
{
  NSScrollView *scrollView;
  NSButton *addButton;

  [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(0.0, 330.0, 470.0,
    22.0), NSLocalizedString(@"Other", nil), XPFontTextStyleBoldBody)];
  [detailView_ addSubview:StrappyEditorLabel(NSMakeRect(0.0, 302.0, 470.0,
    24.0), NSLocalizedString(@"Only Model ID is required. Double-click any cell to edit it, then click Save.", nil), XPFontTextStyleBody)];
  scrollView = [[[NSScrollView alloc]
    initWithFrame:NSMakeRect(0.0, 42.0, 480.0, 248.0)] autorelease];
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
  [otherTableView_ addTableColumn:StrappyEditorCheckboxColumn(
    @"image_input_enabled", @"Images (Optional)", 115.0)];
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

  addButton = StrappyEditorButton(NSMakeRect(0.0, 4.0, 80.0, 28.0),
    NSLocalizedString(@"Add", nil), self, @selector(addOtherModel:));
  [detailView_ addSubview:addButton];
  saveButton_ = StrappyEditorButton(NSMakeRect(88.0, 4.0, 80.0, 28.0),
    NSLocalizedString(@"Save", nil), self, @selector(saveOtherModelDraft:));
  [saveButton_ setEnabled:(draftOtherModel_ != nil) &&
    ([StrappyEditorString(draftOtherModel_, @"wire_model_id") length] > 0U)];
  [detailView_ addSubview:saveButton_];
  deleteButton_ = StrappyEditorButton(NSMakeRect(176.0, 4.0, 80.0, 28.0),
    NSLocalizedString(@"Delete", nil), self, @selector(deleteOtherModel:));
  [deleteButton_ setEnabled:NO];
  [detailView_ addSubview:deleteButton_];
  [self reloadOtherModels];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
  if (tableView == providerTableView_) {
    return (NSInteger)[providers_ count];
  }
  if (tableView == chatGPTTableView_) {
    return (NSInteger)[chatGPTModels_ count];
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
  item = (tableView == chatGPTTableView_) ?
    ((row >= 0) && (row < (NSInteger)[chatGPTModels_ count]) ?
      [chatGPTModels_ objectAtIndex:(NSUInteger)row] : nil) :
    ([self otherRowIsDraft:row] ? draftOtherModel_ :
      [self otherModelAtRow:row]);
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
      ![[tableColumn identifier] isEqualToString:@"wire_model_id"]));
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
  if (model == nil) {
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
    [saveButton_ setEnabled:
      ([StrappyEditorString(updated, @"wire_model_id") length] > 0U)];
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
      [selectedProviderIdentifier_ release];
      selectedProviderIdentifier_ =
        [[[providers_ objectAtIndex:(NSUInteger)row] objectForKey:@"id"] copy];
      [self showSelectedProvider];
    }
  } else if (tableView == otherTableView_) {
    [deleteButton_ setEnabled:(([self otherModelAtRow:row] != nil) ||
                               [self otherRowIsDraft:row])];
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
    return [StrappySession createManualModelForProviderIdentifier:@"other"
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
  return [StrappySession updateManualModelForProviderIdentifier:@"other"
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
      [NSNumber numberWithBool:NO], @"reasoning_enabled",
      [NSNumber numberWithBool:NO], @"image_input_enabled",
      @"", @"pricing_prompt",
      @"", @"pricing_completion",
      @"", @"pricing_input_cache_read",
      @"", @"pricing_input_cache_write",
      nil];
    [otherTableView_ reloadData];
    [saveButton_ setEnabled:NO];
  }
  row = (NSInteger)[otherModels_ count];
  [otherTableView_ selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
               byExtendingSelection:NO];
  [otherTableView_ scrollRowToVisible:row];
  [otherTableView_ editColumn:0 row:row withEvent:nil select:YES];
}

- (void)saveOtherModelDraft:(id)sender
{
  NSError *error;

  (void)sender;
  if ((draftOtherModel_ == nil) ||
      ([StrappyEditorString(draftOtherModel_, @"wire_model_id") length] == 0U)) {
    NSBeep();
    return;
  }
  error = nil;
  if (![self saveOtherModel:draftOtherModel_ creating:YES error:&error]) {
    [self showError:error title:NSLocalizedString(@"Could Not Save Model", nil)];
    return;
  }
  [draftOtherModel_ release];
  draftOtherModel_ = nil;
  [saveButton_ setEnabled:NO];
  [self reloadCatalog];
}

- (void)deleteOtherModel:(id)sender
{
  NSDictionary *model;
  NSError *error;

  (void)sender;
  if ([self otherRowIsDraft:[otherTableView_ selectedRow]]) {
    [draftOtherModel_ release];
    draftOtherModel_ = nil;
    [otherTableView_ reloadData];
    [saveButton_ setEnabled:NO];
    [deleteButton_ setEnabled:NO];
    return;
  }
  model = [self otherModelAtRow:[otherTableView_ selectedRow]];
  if (model == nil) {
    NSBeep();
    return;
  }
  error = nil;
  if (![StrappySession archiveManualModelForProviderIdentifier:@"other"
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
    [statusLabel_ setStringValue:NSLocalizedString(@"Fetching models...", nil)];
  } else {
    [progressIndicator_ stopAnimation:self];
    [statusLabel_ setStringValue:@""];
  }
}

- (void)modelCatalogDidChange:(NSNotification *)notification
{
  (void)notification;
  if ([selectedProviderIdentifier_ isEqualToString:@"other"]) {
    return;
  }
  [self performSelector:@selector(reloadCatalogAfterNotification:)
             withObject:nil afterDelay:0.0];
}

- (void)reloadCatalogAfterNotification:(id)ignored
{
  (void)ignored;
  if ([selectedProviderIdentifier_ isEqualToString:@"other"]) {
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
    [statusLabel_ setStringValue:message];
    [statusLabel_ setToolTip:message];
  }
}

- (void)showError:(NSError *)error title:(NSString *)title
{
  NSAlert *alert;
  NSString *message;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = NSLocalizedString(@"The model catalog could not be changed.", nil);
  }
  alert = [[[NSAlert alloc] init] autorelease];
  [alert setMessageText:title];
  [alert setInformativeText:message];
  [alert addButtonWithTitle:NSLocalizedString(@"OK", nil)];
  [alert runModal];
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
  [chatGPTTableView_ setDataSource:nil];
  [chatGPTTableView_ setDelegate:nil];
  [otherTableView_ setDataSource:nil];
  [otherTableView_ setDelegate:nil];
  [sheet_ release];
  [providerTableView_ release];
  [detailView_ release];
  [providers_ release];
  [models_ release];
  [chatGPTModels_ release];
  [otherModels_ release];
  [draftOtherModel_ release];
  [selectedProviderIdentifier_ release];
  [super dealloc];
}

@end
