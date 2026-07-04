#import "PreferencesWindowController.h"

#import "AIFontAwesome.h"
#import "FileScanner.h"
#import "StrappySession.h"
#import "StrappyPreferencesAuthenticationView.h"
#import "StrappyPreferencesDatabaseWhitelistView.h"
#import "StrappyPreferencesModelWhitelistView.h"
#import "StrappyPreferencesSystemPromptsView.h"
#import "StrappyKeychain.h"

static const CGFloat kStrappyPreferencesWidth = 720.0;
static const CGFloat kStrappyPreferencesHeight = 480.0;
static const CGFloat kStrappyPreferencesInset = 12.0;
static const CGFloat kStrappyPreferencesToolbarIconPoint = 24.0;
static const CGFloat kStrappyPreferencesToolbarIconCanvas = 32.0;
static NSString * const kStrappyPreferencesFrameAutosaveName =
  @"StrappyPreferencesWindow";
static NSString * const kStrappyPreferencesToolbarIdentifier =
  @"StrappyPreferencesToolbar";
static NSString * const kStrappyPreferencesToolbarAuthentication =
  @"StrappyPreferencesToolbar.Authentication";
static NSString * const kStrappyPreferencesToolbarModels =
  @"StrappyPreferencesToolbar.Models";
static NSString * const kStrappyPreferencesToolbarDatabases =
  @"StrappyPreferencesToolbar.Databases";
static NSString * const kStrappyPreferencesToolbarPrompts =
  @"StrappyPreferencesToolbar.Prompts";
static NSString * const kStrappyModelSearchTextKey =
  @"_strappy_model_search_text";

static NSString *StrappyByteCountString(NSNumber *sizeNumber)
{
  unsigned long long size;
  double value;
  NSArray *units;
  NSUInteger unitIndex;

  if (![sizeNumber isKindOfClass:[NSNumber class]]) {
    return @"";
  }

  size = [sizeNumber unsignedLongLongValue];
  value = (double)size;
  units = [NSArray arrayWithObjects:@"B", @"KB", @"MB", @"GB", @"TB", nil];
  unitIndex = 0U;

  while ((value >= 1024.0) && ((unitIndex + 1U) < [units count])) {
    value = value / 1024.0;
    unitIndex++;
  }

  if (unitIndex == 0U) {
    return [NSString stringWithFormat:@"%llu %@",
      size,
      [units objectAtIndex:unitIndex]];
  }
  return [NSString stringWithFormat:@"%.1f %@",
    value,
    [units objectAtIndex:unitIndex]];
}

static NSString *StrappyDatabasePathForRow(NSDictionary *row)
{
  NSString *path;

  path = [row objectForKey:@"path"];
  if (![path isKindOfClass:[NSString class]]) {
    return @"";
  }
  return path;
}

static NSString *StrappyDatabaseNameForRow(NSDictionary *row)
{
  NSString *path;
  NSString *name;

  path = StrappyDatabasePathForRow(row);
  name = [path lastPathComponent];
  if ([name length] == 0U) {
    return path;
  }
  return name;
}

static NSString *StrappyDatabaseLocationForRow(NSDictionary *row)
{
  NSString *path;
  NSString *directory;
  NSString *homeDirectory;
  NSUInteger homeLength;

  path = StrappyDatabasePathForRow(row);
  directory = [path stringByDeletingLastPathComponent];
  if (([directory length] == 0U) || [directory isEqualToString:path]) {
    return @"";
  }

  homeDirectory = NSHomeDirectory();
  homeLength = [homeDirectory length];
  if ((homeLength > 0U) && [directory hasPrefix:homeDirectory]) {
    if ([directory length] == homeLength) {
      return @"~";
    }
    if ([directory characterAtIndex:homeLength] == '/') {
      return [@"~" stringByAppendingString:
        [directory substringFromIndex:homeLength]];
    }
  }

  return directory;
}

static NSString *StrappyStringForModelRow(NSDictionary *row, NSString *key)
{
  NSString *value;

  value = [row objectForKey:key];
  if (![value isKindOfClass:[NSString class]]) {
    return @"";
  }
  return value;
}

static NSString *StrappyModelDisplayNameForRow(NSDictionary *row)
{
  NSString *name;

  name = StrappyStringForModelRow(row, @"name");
  if ([name length] > 0U) {
    return name;
  }
  return StrappyStringForModelRow(row, @"id");
}

static NSString *StrappyModelNumberString(NSDictionary *row, NSString *key)
{
  NSNumber *value;
  unsigned long long count;

  value = [row objectForKey:key];
  if (![value isKindOfClass:[NSNumber class]] || ([value longLongValue] <= 0LL)) {
    return @"";
  }
  count = [value unsignedLongLongValue];
  return [NSString stringWithFormat:@"%lluk", (count + 500ULL) / 1000ULL];
}

static NSString *StrappyModelPricingString(NSDictionary *row, NSString *key)
{
  static NSNumberFormatter *formatter = nil;
  NSString *value;
  double dollarsPerMillion;
  NSString *formatted;

  value = StrappyStringForModelRow(row, key);
  if ([value length] == 0U) {
    return @"";
  }

  if (formatter == nil) {
    NSLocale *locale;

    formatter = [[NSNumberFormatter alloc] init];
    [formatter setFormatterBehavior:NSNumberFormatterBehavior10_4];
    [formatter setNumberStyle:NSNumberFormatterCurrencyStyle];
    locale = [[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"];
    [formatter setLocale:locale];
    [locale release];
    [formatter setCurrencyCode:@"USD"];
    [formatter setCurrencySymbol:@"$"];
    [formatter setMinimumFractionDigits:0U];
    [formatter setMaximumFractionDigits:6U];
  }

  dollarsPerMillion = [value doubleValue] * 1000000.0;
  formatted =
    [formatter stringFromNumber:[NSNumber numberWithDouble:dollarsPerMillion]];
  return (formatted != nil) ? formatted : @"";
}

static NSArray *StrappyModelSearchKeys(void)
{
  static NSArray *keys = nil;

  if (keys == nil) {
    keys = [[NSArray alloc] initWithObjects:
      @"id",
      @"canonical_slug",
      @"hugging_face_id",
      @"name",
      @"description",
      @"context_length",
      @"created",
      @"architecture_modality",
      @"architecture_tokenizer",
      @"architecture_instruct_type",
      @"pricing_prompt",
      @"pricing_completion",
      @"pricing_request",
      @"pricing_image",
      @"pricing_audio",
      @"pricing_web_search",
      @"pricing_internal_reasoning",
      @"pricing_input_cache_read",
      @"pricing_input_cache_write",
      @"top_provider_context_length",
      @"top_provider_max_completion_tokens",
      @"knowledge_cutoff",
      @"expiration_date",
      @"fetched_at",
      nil];
  }

  return keys;
}

static void StrappyAppendModelSearchValue(NSMutableString *searchText, id value)
{
  NSString *stringValue;

  if ([value isKindOfClass:[NSString class]]) {
    stringValue = value;
  } else if ([value isKindOfClass:[NSNumber class]]) {
    stringValue = [value stringValue];
  } else {
    return;
  }

  if ([stringValue length] == 0U) {
    return;
  }

  if ([searchText length] > 0U) {
    [searchText appendString:@" "];
  }
  [searchText appendString:stringValue];
}

static NSString *StrappyModelSearchTextForRow(NSDictionary *row)
{
  NSMutableString *searchText;
  NSArray *keys;
  NSUInteger index;

  searchText = [NSMutableString string];
  keys = StrappyModelSearchKeys();
  for (index = 0U; index < [keys count]; index++) {
    StrappyAppendModelSearchValue(searchText,
                                  [row objectForKey:[keys objectAtIndex:index]]);
  }

  return [searchText lowercaseString];
}

static NSArray *StrappyPreparedModelRowsForRows(NSArray *rows)
{
  NSMutableArray *preparedRows;
  NSUInteger index;

  if (![rows isKindOfClass:[NSArray class]]) {
    return [NSArray array];
  }

  preparedRows = [NSMutableArray arrayWithCapacity:[rows count]];
  for (index = 0U; index < [rows count]; index++) {
    NSDictionary *row;
    NSMutableDictionary *preparedRow;

    row = [rows objectAtIndex:index];
    if (![row isKindOfClass:[NSDictionary class]]) {
      continue;
    }

    preparedRow = [NSMutableDictionary dictionaryWithDictionary:row];
    [preparedRow setObject:StrappyModelSearchTextForRow(row)
                    forKey:kStrappyModelSearchTextKey];
    [preparedRows addObject:preparedRow];
  }

  return preparedRows;
}

@interface PreferencesWindowController ()
- (void)buildContentView;
- (void)setupToolbar;
- (NSArray *)toolbarPaneIdentifiers;
- (void)switchPreferencePane:(id)sender;
- (void)selectPreferencePaneWithIdentifier:(NSString *)identifier;
- (NSToolbarItem *)makeToolbarItemWithIdentifier:(NSString *)identifier
                                            icon:(AIFontAwesomeIcon)icon
                                           label:(NSString *)label;
- (void)refreshAPITokenStatusWithSaved:(BOOL)saved;
- (void)loadSystemPrompt;
- (NSString *)currentModelSearchText;
- (NSArray *)modelRows:(NSArray *)rows matchingSearchText:(NSString *)searchText;
- (void)applyModelRows;
- (void)refreshModelStatus;
- (void)loadOpenRouterModels;
- (void)reloadDefaultModelPopUpButton;
- (void)sortAllModelRows;
- (NSString *)selectedModelTableRowIdentifier;
- (void)selectModelTableRowWithIdentifier:(NSString *)modelIdentifier;
- (NSArray *)selectedDatabaseTableRowPaths;
- (void)selectDatabaseTableRowsWithPaths:(NSArray *)paths;
- (void)modelSearchChanged:(id)sender;
- (void)defaultModelPopUpButtonChanged:(id)sender;
- (void)modelSearchTextDidChange:(NSNotification *)notification;
- (void)setModelCatalogRefreshing:(BOOL)refreshing;
- (void)modelCatalogRefreshDidStart:(NSNotification *)notification;
- (void)modelCatalogRefreshDidFinish:(NSNotification *)notification;
- (void)modelCatalogDidChange:(NSNotification *)notification;
- (NSString *)currentDatabaseSearchText;
- (NSArray *)databaseRows:(NSArray *)rows
  matchingSearchText:(NSString *)searchText;
- (void)applyDatabaseRows;
- (void)loadCatalogedDatabases;
- (void)setScanning:(BOOL)scanning;
- (void)databaseSearchChanged:(id)sender;
- (void)databaseSearchTextDidChange:(NSNotification *)notification;
- (void)scanDatabasesInBackground:(NSString *)rootPath;
- (void)scanDatabasesDidFinish:(NSDictionary *)result;
- (void)refreshDatabaseStatus;
- (void)whitelistTableViewDidPressSpace:(NSTableView *)tableView;
- (void)toggleSelectedModelRows;
- (void)toggleSelectedDatabaseRows;
- (NSNumber *)allowedValueForModelRow:(NSDictionary *)row;
- (BOOL)modelRowIsDefault:(NSDictionary *)row;
- (BOOL)databaseRowCanBeAllowed:(NSDictionary *)row;
- (NSNumber *)allowedValueForDatabaseRow:(NSDictionary *)row;
@end

@implementation PreferencesWindowController

- (id)init
{
  NSWindow *window;
  NSUInteger styleMask;

  styleMask = (XPWindowStyleMaskTitled |
               XPWindowStyleMaskClosable |
               XPWindowStyleMaskMiniaturizable |
               XPWindowStyleMaskResizable);
  window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0.0,
                                     0.0,
                                     kStrappyPreferencesWidth,
                                     kStrappyPreferencesHeight)
                styleMask:styleMask
                  backing:NSBackingStoreBuffered
                    defer:NO];
  [window setTitle:NSLocalizedString(@"Preferences", nil)];
  [window setReleasedWhenClosed:NO];
  [window setShowsToolbarButton:NO];
  if (![window setFrameUsingName:kStrappyPreferencesFrameAutosaveName]) {
    [window setContentSize:NSMakeSize(kStrappyPreferencesWidth,
                                      kStrappyPreferencesHeight)];
    [window center];
  }
  [window setFrameAutosaveName:kStrappyPreferencesFrameAutosaveName];

  if ((self = [super initWithWindow:window])) {
    allModelRows_ = [[NSArray alloc] init];
    modelRows_ = [[NSArray alloc] init];
    allDatabaseRows_ = [[NSArray alloc] init];
    databaseRows_ = [[NSArray alloc] init];
    [self setupToolbar];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(modelCatalogRefreshDidStart:)
             name:StrappySessionModelCatalogRefreshDidStartNotification
           object:nil];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(modelCatalogRefreshDidFinish:)
             name:StrappySessionModelCatalogRefreshDidFinishNotification
           object:nil];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(modelCatalogDidChange:)
             name:StrappySessionModelCatalogDidChangeNotification
           object:nil];
    [self buildContentView];
    [self loadSystemPrompt];
    [self setModelCatalogRefreshing:[StrappySession isModelCatalogRefreshInFlight]];
    [self loadOpenRouterModels];
    [self setScanning:NO];
    [self loadCatalogedDatabases];
  }

  [window release];
  return self;
}

- (void)buildContentView
{
  NSView *contentView;
  NSRect bounds;
  NSRect paneFrame;

  contentView = [[self window] contentView];
  bounds = [contentView bounds];
  contentPaneView_ =
    [[NSView alloc] initWithFrame:NSInsetRect(bounds,
                                              kStrappyPreferencesInset,
                                              kStrappyPreferencesInset)];
  [contentPaneView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

  paneFrame = [contentPaneView_ bounds];

  authenticationPaneView_ =
    [[StrappyPreferencesAuthenticationView alloc] initWithFrame:paneFrame
                                                         target:self];
  apiEndpointField_ = [[authenticationPaneView_ apiEndpointField] retain];
  apiTokenField_ = [[authenticationPaneView_ apiTokenField] retain];
  apiTokenStatusLabel_ = [[authenticationPaneView_ statusLabel] retain];
  [self refreshAPITokenStatusWithSaved:NO];

  modelWhitelistView_ =
    [[StrappyPreferencesModelWhitelistView alloc] initWithFrame:paneFrame
                                                         target:self
                                                     dataSource:self
                                                       delegate:self];
  modelSearchField_ = [[modelWhitelistView_ searchField] retain];
  defaultModelPopUpButton_ =
    [[modelWhitelistView_ defaultModelPopUpButton] retain];
  modelTableView_ = [[modelWhitelistView_ tableView] retain];
  fetchModelsButton_ = [[modelWhitelistView_ fetchButton] retain];
  modelProgressIndicator_ = [[modelWhitelistView_ progressIndicator] retain];
  modelStatusLabel_ = [[modelWhitelistView_ statusLabel] retain];
  [self reloadDefaultModelPopUpButton];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(modelSearchTextDidChange:)
           name:NSControlTextDidChangeNotification
         object:modelSearchField_];

  databaseWhitelistView_ =
    [[StrappyPreferencesDatabaseWhitelistView alloc] initWithFrame:paneFrame
                                                            target:self
                                                        dataSource:self
                                                          delegate:self];
  databaseSearchField_ = [[databaseWhitelistView_ searchField] retain];
  databaseTableView_ = [[databaseWhitelistView_ tableView] retain];
  scanButton_ = [[databaseWhitelistView_ scanButton] retain];
  scanProgressIndicator_ = [[databaseWhitelistView_ progressIndicator] retain];
  databaseStatusLabel_ = [[databaseWhitelistView_ statusLabel] retain];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(databaseSearchTextDidChange:)
           name:NSControlTextDidChangeNotification
         object:databaseSearchField_];

  systemPromptsPaneView_ =
    [[StrappyPreferencesSystemPromptsView alloc] initWithFrame:paneFrame];
  systemPromptTextView_ = [[systemPromptsPaneView_ textView] retain];

  [contentView addSubview:contentPaneView_];
  [self selectPreferencePaneWithIdentifier:kStrappyPreferencesToolbarAuthentication];
}

- (void)setupToolbar
{
  NSToolbar *toolbar;

  toolbar =
    [[[NSToolbar alloc] initWithIdentifier:kStrappyPreferencesToolbarIdentifier]
      autorelease];
  [toolbar setDelegate:self];
  [toolbar setAllowsUserCustomization:NO];
  [toolbar setAutosavesConfiguration:NO];
  [toolbar setDisplayMode:NSToolbarDisplayModeIconAndLabel];
  [toolbar setSizeMode:NSToolbarSizeModeDefault];
  [toolbar setSelectedItemIdentifier:kStrappyPreferencesToolbarAuthentication];
  [[self window] setToolbar:toolbar];
  [[self window] XP_setToolbarPreferenceStyle];
}

- (NSArray *)toolbarPaneIdentifiers
{
  return [NSArray arrayWithObjects:
    kStrappyPreferencesToolbarAuthentication,
    kStrappyPreferencesToolbarModels,
    kStrappyPreferencesToolbarDatabases,
    kStrappyPreferencesToolbarPrompts,
    nil];
}

- (void)switchPreferencePane:(id)sender
{
  NSString *identifier;

  if (![sender isKindOfClass:[NSToolbarItem class]]) {
    return;
  }

  identifier = [(NSToolbarItem *)sender itemIdentifier];
  if ([identifier length] == 0U) {
    return;
  }

  [self selectPreferencePaneWithIdentifier:identifier];
}

- (void)selectPreferencePaneWithIdentifier:(NSString *)identifier
{
  NSView *paneView;
  NSArray *subviews;
  NSUInteger index;

  if (contentPaneView_ == nil) {
    return;
  }

  paneView = nil;
  if ([identifier isEqualToString:kStrappyPreferencesToolbarAuthentication]) {
    paneView = authenticationPaneView_;
  } else if ([identifier isEqualToString:kStrappyPreferencesToolbarModels]) {
    paneView = modelWhitelistView_;
  } else if ([identifier isEqualToString:kStrappyPreferencesToolbarDatabases]) {
    paneView = databaseWhitelistView_;
  } else if ([identifier isEqualToString:kStrappyPreferencesToolbarPrompts]) {
    paneView = systemPromptsPaneView_;
  }
  if (paneView == nil) {
    return;
  }

  subviews = [[contentPaneView_ subviews] copy];
  for (index = 0U; index < [subviews count]; index++) {
    [[subviews objectAtIndex:index] removeFromSuperview];
  }
  [subviews release];

  [paneView setFrame:[contentPaneView_ bounds]];
  [paneView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [contentPaneView_ addSubview:paneView];
  [[[self window] toolbar] setSelectedItemIdentifier:identifier];
}

#pragma mark - NSToolbar Delegate

- (NSArray *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar
{
  (void)toolbar;
  return [self toolbarPaneIdentifiers];
}

- (NSArray *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar
{
  (void)toolbar;
  return [self toolbarPaneIdentifiers];
}

- (NSArray *)toolbarSelectableItemIdentifiers:(NSToolbar *)toolbar
{
  (void)toolbar;
  return [self toolbarPaneIdentifiers];
}

- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar
     itemForItemIdentifier:(NSString *)identifier
 willBeInsertedIntoToolbar:(BOOL)flag
{
  (void)toolbar;
  (void)flag;

  if ([identifier isEqualToString:kStrappyPreferencesToolbarAuthentication]) {
    return [self makeToolbarItemWithIdentifier:identifier
                                          icon:AIFAKey
                                         label:NSLocalizedString(@"Authentication", nil)];
  }
  if ([identifier isEqualToString:kStrappyPreferencesToolbarModels]) {
    return [self makeToolbarItemWithIdentifier:identifier
                                          icon:AIFAMicrochip
                                         label:NSLocalizedString(@"Models", nil)];
  }
  if ([identifier isEqualToString:kStrappyPreferencesToolbarDatabases]) {
    return [self makeToolbarItemWithIdentifier:identifier
                                          icon:AIFADatabase
                                         label:NSLocalizedString(@"Databases", nil)];
  }
  if ([identifier isEqualToString:kStrappyPreferencesToolbarPrompts]) {
    return [self makeToolbarItemWithIdentifier:identifier
                                          icon:AIFAScroll
                                         label:NSLocalizedString(@"Prompts", nil)];
  }
  return nil;
}

- (NSToolbarItem *)makeToolbarItemWithIdentifier:(NSString *)identifier
                                            icon:(AIFontAwesomeIcon)icon
                                           label:(NSString *)label
{
  NSToolbarItem *item;

  if ((identifier == nil) || (label == nil)) {
    return nil;
  }

  item = [[[NSToolbarItem alloc] initWithItemIdentifier:identifier] autorelease];
  [item setLabel:label];
  [item setPaletteLabel:label];
  [item setImage:[AIFontAwesome imageForIcon:icon
                                       style:AIFontAwesomeStyleSolid
                                    iconSize:kStrappyPreferencesToolbarIconPoint
                                  canvasSize:kStrappyPreferencesToolbarIconCanvas
                                       scale:[[self window] XP_backingScaleFactor]]];
  [item setTarget:self];
  [item setAction:@selector(switchPreferencePane:)];
  return item;
}

#pragma mark - Authentication

- (void)refreshAPITokenStatusWithSaved:(BOOL)saved
{
  NSString *message;

  if (apiTokenStatusLabel_ == nil) {
    return;
  }

  if (saved) {
    message = NSLocalizedString(@"API credentials saved to keychain.", nil);
  } else if ([[StrappyKeychain sharedKeychain] hasAPICredentials]) {
    message = NSLocalizedString(@"API credentials are available.", nil);
  } else {
    message = NSLocalizedString(@"No API credentials are saved in the keychain.", nil);
  }
  [apiTokenStatusLabel_ setStringValue:message];
}

- (void)loadSystemPrompt
{
  NSString *path;
  NSString *prompt;

  path = [[NSBundle mainBundle] pathForResource:@"PromptSystem" ofType:@"txt"];
  if ([path length] == 0U) {
    [systemPromptTextView_ setString:
      NSLocalizedString(@"System prompt template is missing from the app bundle.", nil)];
    return;
  }

  prompt = [NSString stringWithContentsOfFile:path
                                     encoding:NSUTF8StringEncoding
                                        error:nil];
  if (prompt == nil) {
    prompt = NSLocalizedString(@"System prompt template could not be read.", nil);
  }
  [systemPromptTextView_ setString:prompt];
}

- (NSString *)currentModelSearchText
{
  NSString *searchText;

  if (modelSearchField_ == nil) {
    return nil;
  }

  searchText = [[modelSearchField_ stringValue]
    stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if ([searchText length] == 0U) {
    return nil;
  }
  return searchText;
}

- (NSArray *)modelRows:(NSArray *)rows matchingSearchText:(NSString *)searchText
{
  NSMutableArray *matchingRows;
  NSString *needle;
  NSUInteger index;

  if (![rows isKindOfClass:[NSArray class]]) {
    return [NSArray array];
  }
  if ([searchText length] == 0U) {
    return rows;
  }

  needle = [searchText lowercaseString];
  matchingRows = [NSMutableArray arrayWithCapacity:[rows count]];
  for (index = 0U; index < [rows count]; index++) {
    NSDictionary *row;
    NSString *rowSearchText;

    row = [rows objectAtIndex:index];
    if (![row isKindOfClass:[NSDictionary class]]) {
      continue;
    }

    rowSearchText = [row objectForKey:kStrappyModelSearchTextKey];
    if (![rowSearchText isKindOfClass:[NSString class]]) {
      rowSearchText = StrappyModelSearchTextForRow(row);
    }

    if ([rowSearchText rangeOfString:needle].location != NSNotFound) {
      [matchingRows addObject:row];
    }
  }

  return matchingRows;
}

- (void)applyModelRows
{
  NSArray *rows;
  NSString *selectedModelIdentifier;

  selectedModelIdentifier = [[self selectedModelTableRowIdentifier] retain];
  rows = [self modelRows:allModelRows_
      matchingSearchText:[self currentModelSearchText]];
  [modelRows_ release];
  modelRows_ = [rows copy];
  [modelTableView_ reloadData];
  [self selectModelTableRowWithIdentifier:selectedModelIdentifier];
  [selectedModelIdentifier release];
  [self refreshModelStatus];
}

- (void)refreshModelStatus
{
  NSUInteger count;
  NSString *searchText;

  if ((modelStatusLabel_ == nil) || refreshingModels_) {
    return;
  }

  count = [modelRows_ count];
  searchText = [self currentModelSearchText];
  if ([searchText length] > 0U) {
    if (count == 0U) {
      [modelStatusLabel_ setStringValue:
        NSLocalizedString(@"No matching models.", nil)];
    } else if (count == 1U) {
      [modelStatusLabel_ setStringValue:
        NSLocalizedString(@"1 model shown.", nil)];
    } else {
      [modelStatusLabel_ setStringValue:
        [NSString stringWithFormat:NSLocalizedString(@"%lu models shown.", nil),
          (unsigned long)count]];
    }
  } else if (count == 0U) {
    [modelStatusLabel_ setStringValue:
      NSLocalizedString(@"No models have been fetched yet.", nil)];
  } else if (count == 1U) {
    [modelStatusLabel_ setStringValue:
      NSLocalizedString(@"1 model available.", nil)];
  } else {
    [modelStatusLabel_ setStringValue:
      [NSString stringWithFormat:NSLocalizedString(@"%lu models available.", nil),
        (unsigned long)count]];
  }
}

- (void)loadOpenRouterModels
{
  NSArray *rows;

  rows = [StrappySession openRouterModelCatalogWithError:nil];
  if (rows != nil) {
    [allModelRows_ release];
    allModelRows_ = [StrappyPreparedModelRowsForRows(rows) copy];
    [self sortAllModelRows];
    [self applyModelRows];
    [self reloadDefaultModelPopUpButton];
    return;
  }

  [modelStatusLabel_ setStringValue:
    NSLocalizedString(@"Model list could not be loaded.", nil)];
}

- (void)reloadDefaultModelPopUpButton
{
  NSString *defaultModelIdentifier;
  NSInteger selectedIndex;
  NSUInteger index;

  if (defaultModelPopUpButton_ == nil) {
    return;
  }

  [defaultModelPopUpButton_ removeAllItems];
  defaultModelIdentifier = @"";
  selectedIndex = -1;
  for (index = 0U; index < [allModelRows_ count]; index++) {
    NSDictionary *model;
    NSString *modelIdentifier;
    NSString *title;
    NSMenuItem *item;

    model = [allModelRows_ objectAtIndex:index];
    if (![model isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    if (![[self allowedValueForModelRow:model] boolValue]) {
      continue;
    }

    modelIdentifier = StrappyStringForModelRow(model, @"id");
    if ([modelIdentifier length] == 0U) {
      continue;
    }

    title = StrappyModelDisplayNameForRow(model);
    [defaultModelPopUpButton_ addItemWithTitle:
      ([title length] > 0U) ? title : modelIdentifier];
    item = [defaultModelPopUpButton_ itemAtIndex:
      ([defaultModelPopUpButton_ numberOfItems] - 1)];
    [item setRepresentedObject:modelIdentifier];
    if ([self modelRowIsDefault:model]) {
      defaultModelIdentifier = modelIdentifier;
      selectedIndex = [defaultModelPopUpButton_ numberOfItems] - 1;
    }
  }

  if ([defaultModelPopUpButton_ numberOfItems] == 0) {
    [defaultModelPopUpButton_ addItemWithTitle:NSLocalizedString(@"Default Model", nil)];
    [[defaultModelPopUpButton_ itemAtIndex:0] setEnabled:NO];
    [defaultModelPopUpButton_ setEnabled:NO];
    return;
  }

  if (selectedIndex < 0) {
    defaultModelIdentifier =
      [StrappySession defaultOpenRouterModelIdentifierWithError:nil];
    if (![defaultModelIdentifier isKindOfClass:[NSString class]]) {
      defaultModelIdentifier = @"";
    }
  }

  if ((selectedIndex < 0) && ([defaultModelIdentifier length] > 0U)) {
    [defaultModelPopUpButton_ addItemWithTitle:defaultModelIdentifier];
    [[defaultModelPopUpButton_ itemAtIndex:
      ([defaultModelPopUpButton_ numberOfItems] - 1)]
        setRepresentedObject:defaultModelIdentifier];
    selectedIndex = [defaultModelPopUpButton_ numberOfItems] - 1;
  }

  if (selectedIndex < 0) {
    selectedIndex = 0;
  }
  [defaultModelPopUpButton_ selectItemAtIndex:selectedIndex];
  [defaultModelPopUpButton_ setEnabled:YES];
}

- (void)sortAllModelRows
{
  NSArray *sortedRows;

  if ((modelWhitelistView_ == nil) || (allModelRows_ == nil)) {
    return;
  }

  sortedRows = [modelWhitelistView_ sortedRows:allModelRows_];
  [allModelRows_ release];
  allModelRows_ = [sortedRows copy];
}

- (NSString *)selectedModelTableRowIdentifier
{
  NSInteger row;

  if (modelTableView_ == nil) {
    return nil;
  }

  row = [modelTableView_ selectedRow];
  if ((row < 0) || (row >= (NSInteger)[modelRows_ count])) {
    return nil;
  }

  return StrappyStringForModelRow([modelRows_ objectAtIndex:(NSUInteger)row],
                                  @"id");
}

- (void)selectModelTableRowWithIdentifier:(NSString *)modelIdentifier
{
  NSUInteger index;

  if (modelTableView_ == nil) {
    return;
  }

  if (![modelIdentifier isKindOfClass:[NSString class]] ||
      ([modelIdentifier length] == 0U)) {
    [modelTableView_ deselectAll:self];
    return;
  }

  for (index = 0U; index < [modelRows_ count]; index++) {
    NSDictionary *row;

    row = [modelRows_ objectAtIndex:index];
    if ([StrappyStringForModelRow(row, @"id")
          isEqualToString:modelIdentifier]) {
      [modelTableView_ selectRowIndexes:[NSIndexSet indexSetWithIndex:index]
                     byExtendingSelection:NO];
      [modelTableView_ scrollRowToVisible:(NSInteger)index];
      return;
    }
  }

  [modelTableView_ deselectAll:self];
}

- (NSArray *)selectedDatabaseTableRowPaths
{
  NSMutableArray *paths;
  NSIndexSet *selectedRows;
  NSUInteger rowIndex;

  paths = [NSMutableArray array];
  if (databaseTableView_ == nil) {
    return paths;
  }

  selectedRows = [databaseTableView_ selectedRowIndexes];
  for (rowIndex = [selectedRows firstIndex];
       rowIndex != NSNotFound;
       rowIndex = [selectedRows indexGreaterThanIndex:rowIndex]) {
    NSString *path;

    if (rowIndex >= [databaseRows_ count]) {
      continue;
    }

    path = StrappyDatabasePathForRow([databaseRows_ objectAtIndex:rowIndex]);
    if ([path length] > 0U) {
      [paths addObject:path];
    }
  }

  return paths;
}

- (void)selectDatabaseTableRowsWithPaths:(NSArray *)paths
{
  NSMutableIndexSet *indexes;
  NSUInteger index;

  if (databaseTableView_ == nil) {
    return;
  }

  if (![paths isKindOfClass:[NSArray class]] || ([paths count] == 0U)) {
    [databaseTableView_ deselectAll:self];
    return;
  }

  indexes = [NSMutableIndexSet indexSet];
  for (index = 0U; index < [databaseRows_ count]; index++) {
    NSString *path;

    path = StrappyDatabasePathForRow([databaseRows_ objectAtIndex:index]);
    if ([paths containsObject:path]) {
      [indexes addIndex:index];
    }
  }

  if ([indexes count] == 0U) {
    [databaseTableView_ deselectAll:self];
    return;
  }
  [databaseTableView_ selectRowIndexes:indexes byExtendingSelection:NO];
}

- (void)modelSearchChanged:(id)sender
{
  (void)sender;
  [self applyModelRows];
}

- (void)databaseSearchChanged:(id)sender
{
  (void)sender;
  [self applyDatabaseRows];
}

- (void)defaultModelPopUpButtonChanged:(id)sender
{
  NSMenuItem *item;
  NSString *modelIdentifier;

  if (sender != defaultModelPopUpButton_) {
    return;
  }

  item = [defaultModelPopUpButton_ selectedItem];
  modelIdentifier = [item representedObject];
  if (![modelIdentifier isKindOfClass:[NSString class]] ||
      ([modelIdentifier length] == 0U)) {
    [self reloadDefaultModelPopUpButton];
    return;
  }

  if (![StrappySession setDefaultOpenRouterModelIdentifier:modelIdentifier
                                                     error:nil]) {
    NSBeep();
    [self reloadDefaultModelPopUpButton];
    return;
  }

  [self loadOpenRouterModels];
}

- (void)modelSearchTextDidChange:(NSNotification *)notification
{
  if ([notification object] == modelSearchField_) {
    [self applyModelRows];
  }
}

- (void)databaseSearchTextDidChange:(NSNotification *)notification
{
  if ([notification object] == databaseSearchField_) {
    [self applyDatabaseRows];
  }
}

- (void)setModelCatalogRefreshing:(BOOL)refreshing
{
  refreshingModels_ = refreshing;
  [fetchModelsButton_ setEnabled:(refreshingModels_ ? NO : YES)];
  if (refreshingModels_) {
    [modelProgressIndicator_ startAnimation:self];
    [modelStatusLabel_ setStringValue:NSLocalizedString(@"Fetching models...", nil)];
  } else {
    [modelProgressIndicator_ stopAnimation:self];
  }
}

- (void)refreshModels:(id)sender
{
  NSError *error;
  NSString *message;
  NSAlert *alert;

  (void)sender;
  if (refreshingModels_) {
    return;
  }

  error = nil;
  if (![StrappySession beginOpenRouterModelCatalogRefreshWithError:&error]) {
    message = [error localizedDescription];
    if ([message length] == 0U) {
      message = NSLocalizedString(@"Model refresh could not start.", nil);
    }
    alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:NSLocalizedString(@"Could not fetch models", nil)];
    [alert setInformativeText:message];
    [alert runModal];
    return;
  }

  [self setModelCatalogRefreshing:YES];
}

- (void)modelCatalogRefreshDidStart:(NSNotification *)notification
{
  (void)notification;
  [self setModelCatalogRefreshing:YES];
}

- (void)modelCatalogRefreshDidFinish:(NSNotification *)notification
{
  NSDictionary *userInfo;
  NSString *errorMessage;
  NSNumber *count;

  userInfo = [notification userInfo];
  errorMessage = [userInfo objectForKey:@"error"];
  [self setModelCatalogRefreshing:NO];
  if ([errorMessage isKindOfClass:[NSString class]] &&
      ([errorMessage length] > 0U)) {
    [modelStatusLabel_ setStringValue:errorMessage];
    NSBeep();
    return;
  }

  [self loadOpenRouterModels];
  count = [userInfo objectForKey:@"model_count"];
  if (([self currentModelSearchText] == nil) &&
      [count isKindOfClass:[NSNumber class]]) {
    [modelStatusLabel_ setStringValue:
      [NSString stringWithFormat:NSLocalizedString(@"%lu models available.", nil),
        (unsigned long)[count XP_unsignedIntegerValue]]];
  }
}

- (void)modelCatalogDidChange:(NSNotification *)notification
{
  (void)notification;
  [self loadOpenRouterModels];
}

- (NSString *)currentDatabaseSearchText
{
  NSString *searchText;

  if (databaseSearchField_ == nil) {
    return nil;
  }

  searchText = [[databaseSearchField_ stringValue]
    stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if ([searchText length] == 0U) {
    return nil;
  }
  return searchText;
}

- (NSArray *)databaseRows:(NSArray *)rows matchingSearchText:(NSString *)searchText
{
  NSMutableArray *matchingRows;
  NSUInteger index;

  if (![rows isKindOfClass:[NSArray class]]) {
    return [NSArray array];
  }
  if ([searchText length] == 0U) {
    return rows;
  }

  matchingRows = [NSMutableArray arrayWithCapacity:[rows count]];
  for (index = 0U; index < [rows count]; index++) {
    NSDictionary *row;
    NSString *name;
    NSString *location;
    NSString *path;

    row = [rows objectAtIndex:index];
    if (![row isKindOfClass:[NSDictionary class]]) {
      continue;
    }

    name = StrappyDatabaseNameForRow(row);
    location = StrappyDatabaseLocationForRow(row);
    path = StrappyDatabasePathForRow(row);
    if (([name rangeOfString:searchText
                     options:NSCaseInsensitiveSearch].location != NSNotFound) ||
        ([location rangeOfString:searchText
                         options:NSCaseInsensitiveSearch].location != NSNotFound) ||
        ([path rangeOfString:searchText
                     options:NSCaseInsensitiveSearch].location != NSNotFound)) {
      [matchingRows addObject:row];
    }
  }

  return matchingRows;
}

- (void)applyDatabaseRows
{
  NSArray *rows;

  rows = [self databaseRows:allDatabaseRows_
        matchingSearchText:[self currentDatabaseSearchText]];
  rows = [databaseWhitelistView_ sortedRows:rows];
  [databaseRows_ release];
  databaseRows_ = [rows copy];
  [databaseTableView_ reloadData];
  [self refreshDatabaseStatus];
}

- (void)loadCatalogedDatabases
{
  NSArray *rows;

  rows = [[FileScanner sharedScanner] catalogedSQLiteDatabasesWithError:nil];
  if (rows != nil) {
    [allDatabaseRows_ release];
    allDatabaseRows_ = [rows copy];
    [self applyDatabaseRows];
    return;
  }

  NSBeep();
}

- (void)setScanning:(BOOL)scanning
{
  scanning_ = scanning;
  [scanButton_ setEnabled:(scanning_ ? NO : YES)];
  if (scanning_) {
    [scanProgressIndicator_ startAnimation:self];
  } else {
    [scanProgressIndicator_ stopAnimation:self];
  }
  [self refreshDatabaseStatus];
}

- (void)refreshDatabaseStatus
{
  NSUInteger count;
  NSString *searchText;

  if (databaseStatusLabel_ == nil) {
    return;
  }

  if (scanning_) {
    [databaseStatusLabel_ setStringValue:
      NSLocalizedString(@"Scanning databases...", nil)];
    return;
  }

  count = [databaseRows_ count];
  searchText = [self currentDatabaseSearchText];
  if ([searchText length] > 0U) {
    if (count == 1U) {
      [databaseStatusLabel_ setStringValue:
        NSLocalizedString(@"1 database shown.", nil)];
    } else if (count == 0U) {
      [databaseStatusLabel_ setStringValue:
        NSLocalizedString(@"No matching databases.", nil)];
    } else {
      [databaseStatusLabel_ setStringValue:
        [NSString stringWithFormat:NSLocalizedString(@"%lu databases shown.", nil),
          (unsigned long)count]];
    }
  } else if (count == 1U) {
    [databaseStatusLabel_ setStringValue:
      NSLocalizedString(@"1 database available.", nil)];
  } else if (count == 0U) {
    [databaseStatusLabel_ setStringValue:
      NSLocalizedString(@"No databases available.", nil)];
  } else {
    [databaseStatusLabel_ setStringValue:
      [NSString stringWithFormat:NSLocalizedString(@"%lu databases available.", nil),
        (unsigned long)count]];
  }
}

- (void)saveAPICredentials:(id)sender
{
  NSString *apiEndpoint;
  NSString *apiToken;
  NSAlert *alert;

  (void)sender;
  apiEndpoint = [[apiEndpointField_ stringValue]
    stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  apiToken = [[apiTokenField_ stringValue]
    stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if (([apiEndpoint length] == 0U) || ([apiToken length] == 0U)) {
    alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:NSLocalizedString(@"API credentials are required", nil)];
    [alert setInformativeText:NSLocalizedString(
      @"Enter an API endpoint and token before saving them to the keychain.", nil)];
    [alert runModal];
    return;
  }

  if (![[StrappyKeychain sharedKeychain] saveAPIEndpoint:apiEndpoint
                                                   token:apiToken]) {
    alert = [[[NSAlert alloc] init] autorelease];
    [alert setMessageText:NSLocalizedString(@"Could not save API credentials", nil)];
    [alert setInformativeText:NSLocalizedString(
      @"The keychain refused the write.", nil)];
    [alert runModal];
    return;
  }

  [apiEndpointField_ setStringValue:apiEndpoint];
  [apiTokenField_ setStringValue:apiToken];
  [self refreshAPITokenStatusWithSaved:YES];
}

- (void)scanDatabases:(id)sender
{
  NSString *rootPath;

  (void)sender;
  if (scanning_) {
    return;
  }

  rootPath = [NSHomeDirectory() copy];
  [self setScanning:YES];
  [self retain];
  [NSThread detachNewThreadSelector:@selector(scanDatabasesInBackground:)
                           toTarget:self
                         withObject:rootPath];
  [rootPath release];
}

- (void)scanDatabasesInBackground:(NSString *)rootPath
{
  NSAutoreleasePool *pool;
  NSArray *rows;
  NSMutableDictionary *result;

  pool = [[NSAutoreleasePool alloc] init];
  rows = [[FileScanner sharedScanner] scanDirectoryForSQLiteDatabasesAtPath:rootPath
                                            savingResultsToCatalogWithError:nil];
  result = [[NSMutableDictionary alloc] init];
  if (rows != nil) {
    [result setObject:rows forKey:@"rows"];
  }

  [self performSelectorOnMainThread:@selector(scanDatabasesDidFinish:)
                         withObject:result
                      waitUntilDone:NO];
  [result release];
  [pool release];
  [self release];
}

- (void)scanDatabasesDidFinish:(NSDictionary *)result
{
  NSArray *rows;

  rows = [result objectForKey:@"rows"];
  if ([rows isKindOfClass:[NSArray class]]) {
    [allDatabaseRows_ release];
    allDatabaseRows_ = [rows copy];
    [self applyDatabaseRows];
  } else {
    NSBeep();
  }

  [self setScanning:NO];
}

- (void)whitelistTableViewDidPressSpace:(NSTableView *)tableView
{
  if (tableView == modelTableView_) {
    [self toggleSelectedModelRows];
  } else if (tableView == databaseTableView_) {
    [self toggleSelectedDatabaseRows];
  }
}

- (void)toggleSelectedModelRows
{
  NSIndexSet *selectedRows;
  NSUInteger rowIndex;
  NSDictionary *model;
  BOOL shouldAllow;
  NSUInteger eligibleCount;

  selectedRows = [modelTableView_ selectedRowIndexes];
  if ([selectedRows count] == 0U) {
    return;
  }

  shouldAllow = NO;
  eligibleCount = 0U;
  for (rowIndex = [selectedRows firstIndex];
       rowIndex != NSNotFound;
       rowIndex = [selectedRows indexGreaterThanIndex:rowIndex]) {
    if (rowIndex >= [modelRows_ count]) {
      continue;
    }

    model = [modelRows_ objectAtIndex:rowIndex];
    if ([self modelRowIsDefault:model]) {
      continue;
    }

    eligibleCount++;
    if (![[self allowedValueForModelRow:model] boolValue]) {
      shouldAllow = YES;
    }
  }

  if (eligibleCount == 0U) {
    NSBeep();
    return;
  }

  for (rowIndex = [selectedRows firstIndex];
       rowIndex != NSNotFound;
       rowIndex = [selectedRows indexGreaterThanIndex:rowIndex]) {
    NSString *modelId;

    if (rowIndex >= [modelRows_ count]) {
      continue;
    }

    model = [modelRows_ objectAtIndex:rowIndex];
    if ([self modelRowIsDefault:model] ||
        ([[self allowedValueForModelRow:model] boolValue] == shouldAllow)) {
      continue;
    }

    modelId = StrappyStringForModelRow(model, @"id");
    if (([modelId length] == 0U) ||
        ![StrappySession setOpenRouterModelAllowed:shouldAllow
                                forModelIdentifier:modelId
                                             error:nil]) {
      NSBeep();
      [modelTableView_ reloadData];
      return;
    }
  }

  [self loadOpenRouterModels];
}

- (void)toggleSelectedDatabaseRows
{
  NSIndexSet *selectedRows;
  NSUInteger rowIndex;
  NSDictionary *database;
  NSNumber *catalogId;
  BOOL shouldAllow;
  NSUInteger eligibleCount;

  selectedRows = [databaseTableView_ selectedRowIndexes];
  if ([selectedRows count] == 0U) {
    return;
  }

  shouldAllow = NO;
  eligibleCount = 0U;
  for (rowIndex = [selectedRows firstIndex];
       rowIndex != NSNotFound;
       rowIndex = [selectedRows indexGreaterThanIndex:rowIndex]) {
    if (rowIndex >= [databaseRows_ count]) {
      continue;
    }

    database = [databaseRows_ objectAtIndex:rowIndex];
    if (![self databaseRowCanBeAllowed:database]) {
      continue;
    }

    eligibleCount++;
    if (![[self allowedValueForDatabaseRow:database] boolValue]) {
      shouldAllow = YES;
    }
  }

  if (eligibleCount == 0U) {
    NSBeep();
    return;
  }

  for (rowIndex = [selectedRows firstIndex];
       rowIndex != NSNotFound;
       rowIndex = [selectedRows indexGreaterThanIndex:rowIndex]) {
    if (rowIndex >= [databaseRows_ count]) {
      continue;
    }

    database = [databaseRows_ objectAtIndex:rowIndex];
    if (![self databaseRowCanBeAllowed:database]) {
      continue;
    }
    if ([[self allowedValueForDatabaseRow:database] boolValue] == shouldAllow) {
      continue;
    }

    catalogId = [database objectForKey:@"catalog_id"];
    if (![[FileScanner sharedScanner] setCatalogedDatabaseAllowed:shouldAllow
                                             forCatalogIdentifier:catalogId
                                                            error:nil]) {
      NSBeep();
      [databaseTableView_ reloadData];
      return;
    }
  }

  [self loadCatalogedDatabases];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
  if (tableView == modelTableView_) {
    return (NSInteger)[modelRows_ count];
  }
  return (NSInteger)[databaseRows_ count];
}

- (void)tableView:(NSTableView *)tableView
  sortDescriptorsDidChange:(NSArray *)oldDescriptors
{
  NSArray *selectedDatabasePaths;

  (void)oldDescriptors;
  if (tableView == modelTableView_) {
    [self sortAllModelRows];
    [self applyModelRows];
    return;
  }

  if (tableView == databaseTableView_) {
    selectedDatabasePaths = [[self selectedDatabaseTableRowPaths] retain];
    [self applyDatabaseRows];
    [self selectDatabaseTableRowsWithPaths:selectedDatabasePaths];
    [selectedDatabasePaths release];
  }
}

- (id)tableView:(NSTableView *)tableView
    objectValueForTableColumn:(NSTableColumn *)tableColumn
                          row:(NSInteger)row
{
  NSDictionary *database;
  NSDictionary *model;
  NSString *identifier;

  if (tableView == modelTableView_) {
    if ((row < 0) || (row >= (NSInteger)[modelRows_ count])) {
      return @"";
    }

    model = [modelRows_ objectAtIndex:(NSUInteger)row];
    identifier = [tableColumn identifier];
    if ([identifier isEqualToString:@"model_allowed"]) {
      return [self allowedValueForModelRow:model];
    }
    if ([identifier isEqualToString:@"model_name"]) {
      return StrappyModelDisplayNameForRow(model);
    }
    if ([identifier isEqualToString:@"model_id"]) {
      return StrappyStringForModelRow(model, @"id");
    }
    if ([identifier isEqualToString:@"model_context"]) {
      return StrappyModelNumberString(model, @"context_length");
    }
    if ([identifier isEqualToString:@"model_prompt_price"]) {
      return StrappyModelPricingString(model, @"pricing_prompt");
    }
    if ([identifier isEqualToString:@"model_completion_price"]) {
      return StrappyModelPricingString(model, @"pricing_completion");
    }
    return nil;
  }

  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    return nil;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  identifier = [tableColumn identifier];
  if ([identifier isEqualToString:@"allowed"]) {
    return [self allowedValueForDatabaseRow:database];
  }
  if ([identifier isEqualToString:@"name"]) {
    return StrappyDatabaseNameForRow(database);
  }
  if ([identifier isEqualToString:@"location"]) {
    return StrappyDatabaseLocationForRow(database);
  }
  if ([identifier isEqualToString:@"size"]) {
    return StrappyByteCountString([database objectForKey:@"size"]);
  }

  return nil;
}

- (NSString *)tableView:(NSTableView *)tableView
         toolTipForCell:(NSCell *)cell
                   rect:(NSRectPointer)rect
            tableColumn:(NSTableColumn *)tableColumn
                    row:(NSInteger)row
          mouseLocation:(NSPoint)mouseLocation
{
  NSDictionary *database;
  NSDictionary *model;
  NSString *identifier;
  NSString *validationError;

  (void)cell;
  (void)rect;
  (void)mouseLocation;
  if (tableView == modelTableView_) {
    if ((row < 0) || (row >= (NSInteger)[modelRows_ count])) {
      return @"";
    }

    model = [modelRows_ objectAtIndex:(NSUInteger)row];
    identifier = [tableColumn identifier];
    if ([identifier isEqualToString:@"model_allowed"]) {
      if ([self modelRowIsDefault:model]) {
        return NSLocalizedString(@"Default model is always allowed.", nil);
      }
      return @"";
    }
    if ([identifier isEqualToString:@"model_name"]) {
      NSString *description;

      description = StrappyStringForModelRow(model, @"description");
      if ([description length] > 0U) {
        return description;
      }
      return StrappyStringForModelRow(model, @"id");
    }
    if ([identifier isEqualToString:@"model_id"]) {
      return StrappyStringForModelRow(model, @"id");
    }
    return @"";
  }

  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    return @"";
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  identifier = [tableColumn identifier];
  if ([identifier isEqualToString:@"allowed"] &&
      ![self databaseRowCanBeAllowed:database]) {
    validationError = [database objectForKey:@"validation_error"];
    if ([validationError isKindOfClass:[NSString class]] &&
        ([validationError length] > 0U)) {
      return validationError;
    }
    return @"";
  }
  if ([identifier isEqualToString:@"name"] ||
      [identifier isEqualToString:@"location"]) {
    return StrappyDatabasePathForRow(database);
  }

  return @"";
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
  NSTableView *tableView;

  tableView = [notification object];
  if (tableView != modelTableView_) {
    return;
  }
}

- (void)tableView:(NSTableView *)tableView
   setObjectValue:(id)object
   forTableColumn:(NSTableColumn *)tableColumn
              row:(NSInteger)row
{
  NSDictionary *database;
  NSDictionary *model;
  NSString *identifier;
  NSNumber *catalogId;
  BOOL allowed;

  identifier = [tableColumn identifier];
  allowed = ([object respondsToSelector:@selector(boolValue)] &&
             [object boolValue]) ? YES : NO;

  if (tableView == modelTableView_) {
    NSString *modelId;

    if ((row < 0) || (row >= (NSInteger)[modelRows_ count]) ||
        ![identifier isEqualToString:@"model_allowed"]) {
      return;
    }

    model = [modelRows_ objectAtIndex:(NSUInteger)row];
    if ([self modelRowIsDefault:model] && !allowed) {
      NSBeep();
      [modelTableView_ reloadData];
      return;
    }

    modelId = StrappyStringForModelRow(model, @"id");
    if (([modelId length] == 0U) ||
        ![StrappySession setOpenRouterModelAllowed:allowed
                                forModelIdentifier:modelId
                                             error:nil]) {
      NSBeep();
      [modelTableView_ reloadData];
      return;
    }

    [self loadOpenRouterModels];
    return;
  }

  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    return;
  }
  if (![identifier isEqualToString:@"allowed"]) {
    return;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  if (allowed && ![self databaseRowCanBeAllowed:database]) {
    NSBeep();
    [databaseTableView_ reloadData];
    return;
  }

  catalogId = [database objectForKey:@"catalog_id"];
  if (![[FileScanner sharedScanner] setCatalogedDatabaseAllowed:allowed
                                           forCatalogIdentifier:catalogId
                                                          error:nil]) {
    NSBeep();
    [databaseTableView_ reloadData];
    return;
  }

  [self loadCatalogedDatabases];
}

- (BOOL)tableView:(NSTableView *)tableView
 shouldEditTableColumn:(NSTableColumn *)tableColumn
              row:(NSInteger)row
{
  NSDictionary *database;

  if (tableView == modelTableView_) {
    NSDictionary *model;

    if (![[tableColumn identifier] isEqualToString:@"model_allowed"] ||
        (row < 0) || (row >= (NSInteger)[modelRows_ count])) {
      return NO;
    }

    model = [modelRows_ objectAtIndex:(NSUInteger)row];
    return [self modelRowIsDefault:model] ? NO : YES;
  }

  if (![[tableColumn identifier] isEqualToString:@"allowed"]) {
    return NO;
  }
  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    return NO;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  return [self databaseRowCanBeAllowed:database];
}

- (void)tableView:(NSTableView *)tableView
  willDisplayCell:(id)cell
   forTableColumn:(NSTableColumn *)tableColumn
              row:(NSInteger)row
{
  NSDictionary *database;

  if (tableView == modelTableView_) {
    NSDictionary *model;

    if (![[tableColumn identifier] isEqualToString:@"model_allowed"] ||
        ![cell respondsToSelector:@selector(setEnabled:)]) {
      return;
    }
    if ((row < 0) || (row >= (NSInteger)[modelRows_ count])) {
      [cell setEnabled:NO];
      return;
    }

    model = [modelRows_ objectAtIndex:(NSUInteger)row];
    [cell setEnabled:([self modelRowIsDefault:model] ? NO : YES)];
    return;
  }

  if (![[tableColumn identifier] isEqualToString:@"allowed"] ||
      ![cell respondsToSelector:@selector(setEnabled:)]) {
    return;
  }
  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    [cell setEnabled:NO];
    return;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  [cell setEnabled:[self databaseRowCanBeAllowed:database]];
}

- (NSNumber *)allowedValueForModelRow:(NSDictionary *)row
{
  NSNumber *allowed;

  if ([self modelRowIsDefault:row]) {
    return [NSNumber numberWithBool:YES];
  }

  allowed = [row objectForKey:@"allowed"];
  return ([allowed isKindOfClass:[NSNumber class]]) ?
    allowed : [NSNumber numberWithBool:NO];
}

- (BOOL)modelRowIsDefault:(NSDictionary *)row
{
  NSNumber *selected;

  selected = [row objectForKey:@"selected"];
  return ([selected isKindOfClass:[NSNumber class]] && [selected boolValue]) ?
    YES : NO;
}

- (BOOL)databaseRowCanBeAllowed:(NSDictionary *)row
{
  NSNumber *valid;

  valid = [row objectForKey:@"is_valid_sqlite"];
  return ([valid isKindOfClass:[NSNumber class]] && [valid boolValue]) ? YES : NO;
}

- (NSNumber *)allowedValueForDatabaseRow:(NSDictionary *)row
{
  NSString *decision;

  decision = [row objectForKey:@"user_decision"];
  return [NSNumber numberWithBool:[decision isEqualToString:@"allowed"]];
}

- (void)dealloc
{
  NSToolbar *toolbar;

  [[NSNotificationCenter defaultCenter] removeObserver:self];
  toolbar = [[self window] toolbar];
  [toolbar setDelegate:nil];
  [contentPaneView_ release];
  [authenticationPaneView_ release];
  [apiEndpointField_ release];
  [apiTokenField_ release];
  [apiTokenStatusLabel_ release];
  [modelSearchField_ release];
  [defaultModelPopUpButton_ release];
  [modelTableView_ release];
  [modelWhitelistView_ release];
  [fetchModelsButton_ release];
  [modelProgressIndicator_ release];
  [modelStatusLabel_ release];
  [systemPromptsPaneView_ release];
  [systemPromptTextView_ release];
  [databaseSearchField_ release];
  [databaseTableView_ release];
  [databaseWhitelistView_ release];
  [scanButton_ release];
  [scanProgressIndicator_ release];
  [databaseStatusLabel_ release];
  [allModelRows_ release];
  [modelRows_ release];
  [allDatabaseRows_ release];
  [databaseRows_ release];
  [super dealloc];
}

@end
