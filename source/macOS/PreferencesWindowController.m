#import "PreferencesWindowController.h"

#import "AIFontAwesome.h"
#import "FileScanner.h"
#import "StrappySession.h"
#import "StrappyPreferencesAuthenticationView.h"
#import "StrappyPreferencesDatabaseWhitelistView.h"
#import "StrappyPreferencesDatabaseStudyView.h"
#import "StrappyPreferencesModelWhitelistView.h"
#import "StrappyPreferencesSystemPromptsView.h"
#import "StrappySessionOptionsViewController.h"

static const CGFloat kStrappyPreferencesWidth = 640.0;
static const CGFloat kStrappyPreferencesHeight = 480.0;
static const CGFloat kStrappyPreferencesMinimumWidth = 480.0;
static const CGFloat kStrappyPreferencesMinimumHeight = 320.0;
static const CGFloat kStrappyPreferencesWindowEdgeInset = 8.0;
static const CGFloat kStrappyPreferencesToolbarIconPoint = 24.0;
static const CGFloat kStrappyPreferencesToolbarIconCanvas = 32.0;
static NSString * const kStrappyPreferencesFrameAutosaveName =
  @"StrappyPreferencesWindow";
static NSString * const kStrappyPreferencesToolbarIdentifier =
  @"StrappyPreferencesToolbar";
static NSString * const kStrappyPreferencesToolbarAuthentication =
  @"StrappyPreferencesToolbar.Authentication";
static NSString * const kStrappyPreferencesToolbarSessionDefaults =
  @"StrappyPreferencesToolbar.SessionDefaults";
static NSString * const kStrappyPreferencesToolbarModels =
  @"StrappyPreferencesToolbar.Models";
static NSString * const kStrappyPreferencesToolbarDatabases =
  @"StrappyPreferencesToolbar.Databases";
static NSString * const kStrappyPreferencesToolbarStudy =
  @"StrappyPreferencesToolbar.Study";
static NSString * const kStrappyPreferencesToolbarPrompts =
  @"StrappyPreferencesToolbar.Prompts";
static NSString * const kStrappyModelSearchTextKey =
  @"_strappy_model_search_text";

static NSString *StrappyPreferencesErrorMessage(NSError *error,
                                                 NSString *fallbackMessage)
{
  NSString *message;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = fallbackMessage;
  }
  return ([message length] > 0U) ? message :
    NSLocalizedString(@"The request failed.", nil);
}

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

static NSString *StrappyDatabaseAppNameForRow(NSDictionary *row)
{
  NSString *appName;
  NSString *groupKey;

  appName = [row objectForKey:@"app_name"];
  if ([appName isKindOfClass:[NSString class]] && ([appName length] > 0U)) {
    return appName;
  }

  groupKey = [row objectForKey:@"app_group_key"];
  if ([groupKey isKindOfClass:[NSString class]] && ([groupKey length] > 0U)) {
    return groupKey;
  }

  return NSLocalizedString(@"Other", nil);
}

static NSString *StrappyModelProviderDisplayName(NSString *providerId)
{
  if ([providerId isEqualToString:@"openrouter"]) {
    return @"OpenRouter";
  }
  if ([providerId isEqualToString:@"openai_chatgpt"]) {
    return @"ChatGPT";
  }
  if ([providerId isEqualToString:@"other"]) {
    return NSLocalizedString(@"Other", nil);
  }
  return ([providerId length] > 0U) ? providerId :
    NSLocalizedString(@"Other", nil);
}

static NSString *StrappyDatabaseBundleIdentifierForRow(NSDictionary *row)
{
  NSString *bundleIdentifier;

  bundleIdentifier = [row objectForKey:@"app_bundle_id"];
  return ([bundleIdentifier isKindOfClass:[NSString class]] &&
          ([bundleIdentifier length] > 0U)) ? bundleIdentifier : @"";
}

static BOOL StrappyDatabaseRowAllowedValue(NSDictionary *row)
{
  NSString *decision;

  decision = [row objectForKey:@"user_decision"];
  return [decision isEqualToString:@"allowed"];
}

static BOOL StrappyDatabaseRowHiddenValue(NSDictionary *row)
{
  NSNumber *hidden;

  hidden = [row objectForKey:@"hidden"];
  return ([hidden isKindOfClass:[NSNumber class]] && [hidden boolValue]) ?
    YES : NO;
}

static NSString *StrappyDatabaseStudyStringForRow(NSDictionary *row,
                                                  NSString *key)
{
  NSString *value;

  value = [row objectForKey:key];
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

static NSString *StrappyDatabaseStudyNameForRow(NSDictionary *row)
{
  NSString *name;
  NSString *path;

  path = StrappyDatabaseStudyStringForRow(row, @"path");
  name = [path lastPathComponent];
  return ([name length] > 0U) ?
    name : StrappyDatabaseStudyStringForRow(row, @"database_id");
}

static NSString *StrappyDatabaseStudyAppNameForRow(NSDictionary *row)
{
  NSString *appName;

  appName = StrappyDatabaseStudyStringForRow(row, @"app_name");
  return ([appName length] > 0U) ?
    appName : NSLocalizedString(@"Other", nil);
}

static BOOL StrappyDatabaseStudyRowIsStudied(NSDictionary *row)
{
  NSNumber *studied;

  studied = [row objectForKey:@"studied"];
  return ([studied isKindOfClass:[NSNumber class]] && [studied boolValue]) ?
    YES : NO;
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
  return StrappyStringForModelRow(row, @"wire_model_id");
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
      @"wire_model_id",
      @"provider_account_id",
      @"provider_id",
      @"provider_name",
      @"provider_account_name",
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
  NSMutableDictionary *rowsByProviderModel;
  NSMutableArray *providerModels;
  NSMutableArray *preparedRows;
  NSUInteger index;

  if (![rows isKindOfClass:[NSArray class]]) {
    return [NSArray array];
  }

  rowsByProviderModel = [NSMutableDictionary dictionary];
  providerModels = [NSMutableArray array];
  for (index = 0U; index < [rows count]; index++) {
    NSDictionary *row;
    NSMutableDictionary *providerModel;
    NSString *providerId;
    NSString *wireModelId;
    NSString *providerModelKey;

    row = [rows objectAtIndex:index];
    if (![row isKindOfClass:[NSDictionary class]]) {
      continue;
    }

    providerId = StrappyStringForModelRow(row, @"provider_id");
    wireModelId = StrappyStringForModelRow(row, @"wire_model_id");
    if (([providerId length] == 0U) || ([wireModelId length] == 0U)) {
      continue;
    }
    providerModelKey = [NSString stringWithFormat:@"%@\n%@",
      providerId, wireModelId];
    providerModel = [rowsByProviderModel objectForKey:providerModelKey];
    if (providerModel == nil) {
      providerModel = [NSMutableDictionary dictionaryWithDictionary:row];
      [providerModel setObject:providerModelKey forKey:@"provider_model_key"];
      [providerModel setObject:StrappyModelProviderDisplayName(providerId)
                       forKey:@"provider_name"];
      [rowsByProviderModel setObject:providerModel forKey:providerModelKey];
      [providerModels addObject:providerModel];
    } else {
      BOOL allowed;
      BOOL selected;

      allowed = [[providerModel objectForKey:@"allowed"] boolValue] ||
        [[row objectForKey:@"allowed"] boolValue];
      selected = [[providerModel objectForKey:@"selected"] boolValue] ||
        [[row objectForKey:@"selected"] boolValue];
      if ([[row objectForKey:@"selected"] boolValue]) {
        NSString *savedKey;
        NSString *savedProviderName;

        savedKey = [[providerModel objectForKey:@"provider_model_key"] retain];
        savedProviderName = [[providerModel objectForKey:@"provider_name"] retain];
        [providerModel setDictionary:row];
        [providerModel setObject:savedKey forKey:@"provider_model_key"];
        [providerModel setObject:savedProviderName forKey:@"provider_name"];
        [savedKey release];
        [savedProviderName release];
      }
      [providerModel setObject:[NSNumber numberWithBool:allowed]
                       forKey:@"allowed"];
      [providerModel setObject:[NSNumber numberWithBool:selected]
                       forKey:@"selected"];
    }
  }

  preparedRows = [NSMutableArray arrayWithCapacity:[providerModels count]];
  for (index = 0U; index < [providerModels count]; index++) {
    NSDictionary *row;
    NSMutableDictionary *preparedRow;

    row = [providerModels objectAtIndex:index];
    preparedRow = [NSMutableDictionary dictionaryWithDictionary:row];
    [preparedRow setObject:StrappyModelSearchTextForRow(row)
                    forKey:kStrappyModelSearchTextKey];
    [preparedRows addObject:preparedRow];
  }

  return preparedRows;
}

static NSArray *StrappyModelRowsForAvailableAccounts(NSArray *rows,
                                                      NSArray *accounts)
{
  NSMutableSet *availableProviders;
  NSMutableArray *availableRows;
  NSUInteger index;

  availableProviders = [NSMutableSet set];
  for (index = 0U; index < [accounts count]; index++) {
    NSDictionary *account;
    NSString *providerIdentifier;

    account = [accounts objectAtIndex:index];
    if (![[account objectForKey:@"available"] boolValue]) {
      continue;
    }
    providerIdentifier = [account objectForKey:@"provider_id"];
    if ([providerIdentifier isKindOfClass:[NSString class]]) {
      [availableProviders addObject:providerIdentifier];
    }
  }

  availableRows = [NSMutableArray array];
  for (index = 0U; index < [rows count]; index++) {
    NSDictionary *row;

    row = [rows objectAtIndex:index];
    if ([availableProviders containsObject:
          StrappyStringForModelRow(row, @"provider_id")]) {
      [availableRows addObject:row];
    }
  }
  return availableRows;
}

static BOOL StrappyAccountsContainAvailableModelAccount(NSArray *accounts)
{
  NSUInteger index;

  for (index = 0U; index < [accounts count]; index++) {
    NSDictionary *account;

    account = [accounts objectAtIndex:index];
    if ([[account objectForKey:@"available"] boolValue]) {
      return YES;
    }
  }
  return NO;
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
- (void)preferencesWindowDidBecomeKey:(NSNotification *)notification;
- (void)loadSystemPrompt;
- (void)loadDatabaseStudy;
- (void)databaseStudyPromptDidFinish:(NSNotification *)notification;
- (NSString *)currentDatabaseStudySearchText;
- (NSArray *)databaseStudyRows:(NSArray *)rows
  matchingSearchText:(NSString *)searchText;
- (void)applyDatabaseStudyRows;
- (NSString *)selectedDatabaseStudyIdentifier;
- (void)selectDatabaseStudyRowWithIdentifier:(NSString *)databaseIdentifier;
- (void)updateDatabaseStudyProgress;
- (void)updateDatabaseStudyActionButtonForAllStudied:(BOOL)allStudied;
- (NSString *)databaseStudyDateForRow:(NSDictionary *)row;
- (BOOL)databaseStudyRowIsExpanded:(NSDictionary *)row;
- (void)databaseStudyRowClicked:(id)sender;
- (void)databaseStudySearchTextDidChange:(NSNotification *)notification;
- (NSMenu *)whitelistTableView:(NSTableView *)tableView
             contextMenuForRow:(NSInteger)row;
- (void)whitelistTableViewDidPressDelete:(NSTableView *)tableView;
- (void)deleteDatabaseStudyMenuItem:(id)sender;
- (void)confirmDeleteDatabaseStudyForRow:(NSDictionary *)row;
- (void)databaseStudyDeleteAlertDidEnd:(NSAlert *)alert
                            returnCode:(NSInteger)returnCode
                           contextInfo:(void *)contextInfo;
- (void)showDatabaseStudyError:(NSError *)error title:(NSString *)title;
- (void)resetDatabaseStudy:(id)sender;
- (void)databaseStudyResetAlertDidEnd:(NSAlert *)alert
                           returnCode:(NSInteger)returnCode
                          contextInfo:(void *)contextInfo;
- (void)beginDatabaseStudy:(id)sender;
- (void)databaseStudyRunAlertDidEnd:(NSAlert *)alert
                         returnCode:(NSInteger)returnCode
                        contextInfo:(void *)contextInfo;
- (NSString *)currentModelSearchText;
- (NSArray *)modelRows:(NSArray *)rows matchingSearchText:(NSString *)searchText;
- (void)applyModelRows;
- (void)refreshModelStatus;
- (void)setModelStatusErrorMessage:(NSString *)message;
- (void)loadOpenRouterModels;
- (void)sortAllModelRows;
- (NSString *)selectedModelTableRowIdentifier;
- (void)selectModelTableRowWithIdentifier:(NSString *)modelIdentifier;
- (NSArray *)selectedDatabaseTableRowPaths;
- (void)selectDatabaseTableRowsWithPaths:(NSArray *)paths;
- (void)modelSearchChanged:(id)sender;
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
- (void)databaseCatalogDidChange:(NSNotification *)notification;
- (void)setScanning:(BOOL)scanning;
- (void)databaseSearchChanged:(id)sender;
- (void)showHiddenDatabasesChanged:(id)sender;
- (void)databaseSearchTextDidChange:(NSNotification *)notification;
- (void)databaseScanAlertDidEnd:(NSAlert *)alert
                      returnCode:(NSInteger)returnCode
                     contextInfo:(void *)contextInfo;
- (void)beginDatabaseScanWithMode:(FileScannerDatabaseScanMode)scanMode;
- (void)scanDatabasesInBackground:(NSDictionary *)request;
- (void)scanDatabasesDidFinish:(NSDictionary *)result;
- (void)refreshDatabaseStatus;
- (void)setDatabaseStatusErrorMessage:(NSString *)message;
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
  [window setContentMinSize:NSMakeSize(kStrappyPreferencesMinimumWidth,
                                       kStrappyPreferencesMinimumHeight)];
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
    allDatabaseStudyRows_ = [[NSArray alloc] init];
    databaseStudyRows_ = [[NSArray alloc] init];
    databaseStudyDateFormatter_ = [[NSDateFormatter alloc] init];
    [databaseStudyDateFormatter_
      setFormatterBehavior:NSDateFormatterBehavior10_4];
    [databaseStudyDateFormatter_ setDateStyle:NSDateFormatterShortStyle];
    [databaseStudyDateFormatter_ setTimeStyle:NSDateFormatterShortStyle];
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
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(databaseCatalogDidChange:)
             name:FileScannerDatabaseCatalogDidChangeNotification
           object:nil];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(preferencesWindowDidBecomeKey:)
             name:NSWindowDidBecomeKeyNotification
           object:window];
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(databaseStudyPromptDidFinish:)
             name:StrappySessionPromptDidFinishNotification
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
      kStrappyPreferencesWindowEdgeInset,
      kStrappyPreferencesWindowEdgeInset)];
  [contentPaneView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

  paneFrame = [contentPaneView_ bounds];

  authenticationPaneView_ =
    [[StrappyPreferencesAuthenticationView alloc] initWithFrame:paneFrame
                                                         target:self];

  sessionDefaultsController_ =
    [[StrappySessionOptionsViewController alloc] initForSessionDefaults];

  modelWhitelistView_ =
    [[StrappyPreferencesModelWhitelistView alloc] initWithFrame:paneFrame
                                                         target:self
                                                     dataSource:self
                                                       delegate:self];
  modelSearchField_ = [[modelWhitelistView_ searchField] retain];
  modelTableView_ = [[modelWhitelistView_ tableView] retain];
  fetchModelsButton_ = [[modelWhitelistView_ fetchButton] retain];
  modelProgressIndicator_ = [[modelWhitelistView_ progressIndicator] retain];
  modelStatusLabel_ = [[modelWhitelistView_ statusLabel] retain];
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
  showHiddenDatabasesButton_ =
    [[databaseWhitelistView_ showHiddenDatabasesButton] retain];
  scanProgressIndicator_ = [[databaseWhitelistView_ progressIndicator] retain];
  databaseStatusLabel_ = [[databaseWhitelistView_ statusLabel] retain];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(databaseSearchTextDidChange:)
           name:NSControlTextDidChangeNotification
         object:databaseSearchField_];

  databaseStudyPaneView_ =
    [[StrappyPreferencesDatabaseStudyView alloc] initWithFrame:paneFrame
                                                        target:self
                                                    dataSource:self
                                                      delegate:self];
  databaseStudySearchField_ =
    [[databaseStudyPaneView_ searchField] retain];
  databaseStudyTableView_ = [[databaseStudyPaneView_ tableView] retain];
  databaseStudyActionButton_ =
    [[databaseStudyPaneView_ studyButton] retain];
  databaseStudyStatusLabel_ =
    [[databaseStudyPaneView_ statusLabel] retain];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(databaseStudySearchTextDidChange:)
           name:NSControlTextDidChangeNotification
         object:databaseStudySearchField_];
  [self loadDatabaseStudy];

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
    kStrappyPreferencesToolbarSessionDefaults,
    kStrappyPreferencesToolbarDatabases,
    kStrappyPreferencesToolbarStudy,
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
  } else if ([identifier
               isEqualToString:kStrappyPreferencesToolbarSessionDefaults]) {
    paneView = [sessionDefaultsController_ view];
  } else if ([identifier isEqualToString:kStrappyPreferencesToolbarModels]) {
    [self loadOpenRouterModels];
    paneView = modelWhitelistView_;
  } else if ([identifier isEqualToString:kStrappyPreferencesToolbarDatabases]) {
    paneView = databaseWhitelistView_;
  } else if ([identifier isEqualToString:kStrappyPreferencesToolbarStudy]) {
    [self loadDatabaseStudy];
    paneView = databaseStudyPaneView_;
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
  if ([identifier
        isEqualToString:kStrappyPreferencesToolbarSessionDefaults]) {
    [sessionDefaultsController_ reloadOptions];
  }
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
                                          icon:AIFAUsers
                                         label:NSLocalizedString(@"Accounts", nil)];
  }
  if ([identifier
        isEqualToString:kStrappyPreferencesToolbarSessionDefaults]) {
    return [self makeToolbarItemWithIdentifier:identifier
                                          icon:AIFASliders
                                         label:NSLocalizedString(@"Defaults", nil)];
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
  if ([identifier isEqualToString:kStrappyPreferencesToolbarStudy]) {
    return [self makeToolbarItemWithIdentifier:identifier
                                          icon:AIFABookOpen
                                         label:NSLocalizedString(@"Study", nil)];
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

#pragma mark - Database Study

- (void)preferencesWindowDidBecomeKey:(NSNotification *)notification
{
  NSString *identifier;

  if ([notification object] != [self window]) {
    return;
  }
  identifier = [[[self window] toolbar] selectedItemIdentifier];
  if ([identifier isEqualToString:kStrappyPreferencesToolbarStudy]) {
    [self loadDatabaseStudy];
  }
}

- (void)databaseStudyPromptDidFinish:(NSNotification *)notification
{
  NSString *errorMessage;
  NSDictionary *userInfo;

  userInfo = [notification userInfo];
  if (![[userInfo objectForKey:@"database_study"] boolValue]) {
    return;
  }

  [self loadDatabaseStudy];
  errorMessage = [userInfo objectForKey:@"error"];
  if ([errorMessage isKindOfClass:[NSString class]] &&
      ([errorMessage length] > 0U)) {
    [databaseStudyStatusLabel_ setStringValue:errorMessage];
    [databaseStudyStatusLabel_ setToolTip:errorMessage];
  }
}

- (void)loadDatabaseStudy
{
  NSError *error;
  NSString *message;
  NSArray *rows;

  if (databaseStudyTableView_ == nil) {
    return;
  }
  error = nil;
  rows = [StrappySession databaseStudyRowsWithError:&error];
  if (![rows isKindOfClass:[NSArray class]]) {
    [allDatabaseStudyRows_ release];
    allDatabaseStudyRows_ = [[NSArray alloc] init];
    [databaseStudyRows_ release];
    databaseStudyRows_ = [[NSArray alloc] init];
    [expandedDatabaseStudyIdentifier_ release];
    expandedDatabaseStudyIdentifier_ = nil;
    [databaseStudyTableView_ deselectAll:self];
    [databaseStudyTableView_ reloadData];
    [self updateDatabaseStudyActionButtonForAllStudied:NO];
    message = StrappyPreferencesErrorMessage(
      error,
      NSLocalizedString(@"The request failed.", nil));
    [databaseStudyStatusLabel_ setStringValue:message];
    [databaseStudyStatusLabel_ setToolTip:message];
    return;
  }

  [allDatabaseStudyRows_ release];
  allDatabaseStudyRows_ = [rows copy];
  [self updateDatabaseStudyProgress];
  [self applyDatabaseStudyRows];
}

- (NSString *)currentDatabaseStudySearchText
{
  NSString *searchText;

  if (databaseStudySearchField_ == nil) {
    return nil;
  }
  searchText = [[databaseStudySearchField_ stringValue]
    stringByTrimmingCharactersInSet:
      [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  return ([searchText length] > 0U) ? searchText : nil;
}

- (NSArray *)databaseStudyRows:(NSArray *)rows
  matchingSearchText:(NSString *)searchText
{
  NSArray *keys;
  NSMutableArray *matchingRows;
  NSUInteger rowIndex;

  if (![rows isKindOfClass:[NSArray class]]) {
    return [NSArray array];
  }
  if ([searchText length] == 0U) {
    return rows;
  }

  keys = [NSArray arrayWithObjects:
    @"database_id", @"path", @"app_group_key", @"app_name",
    @"app_bundle_id", @"description", @"context", nil];
  matchingRows = [NSMutableArray arrayWithCapacity:[rows count]];
  for (rowIndex = 0U; rowIndex < [rows count]; rowIndex++) {
    NSDictionary *row;
    BOOL matches;
    NSUInteger keyIndex;

    row = [rows objectAtIndex:rowIndex];
    if (![row isKindOfClass:[NSDictionary class]]) {
      continue;
    }

    matches = ([StrappyDatabaseStudyNameForRow(row)
      rangeOfString:searchText
            options:NSCaseInsensitiveSearch].location != NSNotFound) ?
      YES : NO;
    for (keyIndex = 0U; !matches && (keyIndex < [keys count]); keyIndex++) {
      NSString *value;

      value = StrappyDatabaseStudyStringForRow(
        row,
        [keys objectAtIndex:keyIndex]);
      if ([value rangeOfString:searchText
                      options:NSCaseInsensitiveSearch].location != NSNotFound) {
        matches = YES;
      }
    }
    if (matches) {
      [matchingRows addObject:row];
    }
  }
  return matchingRows;
}

- (void)applyDatabaseStudyRows
{
  NSArray *rows;
  NSString *selectedIdentifier;
  BOOL expandedRowIsVisible;
  NSUInteger index;

  selectedIdentifier = [[self selectedDatabaseStudyIdentifier] retain];
  rows = [self databaseStudyRows:allDatabaseStudyRows_
              matchingSearchText:[self currentDatabaseStudySearchText]];
  rows = [databaseStudyPaneView_ sortedRows:rows];
  [databaseStudyRows_ release];
  databaseStudyRows_ = [rows copy];

  expandedRowIsVisible = NO;
  for (index = 0U;
       ([expandedDatabaseStudyIdentifier_ length] > 0U) &&
         (index < [databaseStudyRows_ count]);
       index++) {
    NSDictionary *row;

    row = [databaseStudyRows_ objectAtIndex:index];
    if (StrappyDatabaseStudyRowIsStudied(row) &&
        [StrappyDatabaseStudyStringForRow(row, @"database_id")
          isEqualToString:expandedDatabaseStudyIdentifier_]) {
      expandedRowIsVisible = YES;
    }
  }
  if (!expandedRowIsVisible) {
    [expandedDatabaseStudyIdentifier_ release];
    expandedDatabaseStudyIdentifier_ = nil;
  }
  [databaseStudyTableView_ reloadData];
  [self selectDatabaseStudyRowWithIdentifier:selectedIdentifier];
  [selectedIdentifier release];
}

- (NSString *)selectedDatabaseStudyIdentifier
{
  NSDictionary *row;
  NSInteger rowIndex;

  rowIndex = [databaseStudyTableView_ selectedRow];
  if ((rowIndex < 0) ||
      (rowIndex >= (NSInteger)[databaseStudyRows_ count])) {
    return nil;
  }
  row = [databaseStudyRows_ objectAtIndex:(NSUInteger)rowIndex];
  if (!StrappyDatabaseStudyRowIsStudied(row)) {
    return nil;
  }
  return StrappyDatabaseStudyStringForRow(row, @"database_id");
}

- (void)selectDatabaseStudyRowWithIdentifier:(NSString *)databaseIdentifier
{
  NSUInteger index;

  [databaseStudyTableView_ deselectAll:self];
  if ([databaseIdentifier length] == 0U) {
    return;
  }

  for (index = 0U; index < [databaseStudyRows_ count]; index++) {
    NSDictionary *row;

    row = [databaseStudyRows_ objectAtIndex:index];
    if (StrappyDatabaseStudyRowIsStudied(row) &&
        [StrappyDatabaseStudyStringForRow(row, @"database_id")
          isEqualToString:databaseIdentifier]) {
      [databaseStudyTableView_
        selectRowIndexes:[NSIndexSet indexSetWithIndex:index]
        byExtendingSelection:NO];
      return;
    }
  }
}

- (void)updateDatabaseStudyProgress
{
  BOOL allStudied;
  NSUInteger index;
  NSUInteger studiedCount;

  studiedCount = 0U;
  for (index = 0U; index < [allDatabaseStudyRows_ count]; index++) {
    id row;

    row = [allDatabaseStudyRows_ objectAtIndex:index];
    if ([row isKindOfClass:[NSDictionary class]] &&
        StrappyDatabaseStudyRowIsStudied(row)) {
      studiedCount++;
    }
  }
  allStudied = ([allDatabaseStudyRows_ count] > 0U) &&
    (studiedCount == [allDatabaseStudyRows_ count]);
  [self updateDatabaseStudyActionButtonForAllStudied:allStudied];
  [databaseStudyStatusLabel_ setToolTip:nil];
  [databaseStudyStatusLabel_ setStringValue:[NSString stringWithFormat:
    NSLocalizedString(@"%lu of %lu", nil),
    (unsigned long)studiedCount,
    (unsigned long)[allDatabaseStudyRows_ count]]];
}

- (void)updateDatabaseStudyActionButtonForAllStudied:(BOOL)allStudied
{
  [databaseStudyActionButton_ setTitle:allStudied ?
    NSLocalizedString(@"Reset", nil) : NSLocalizedString(@"Study", nil)];
  [databaseStudyActionButton_ setAction:allStudied ?
    @selector(resetDatabaseStudy:) : @selector(beginDatabaseStudy:)];
  [databaseStudyActionButton_ setToolTip:allStudied ?
    NSLocalizedString(
      @"This clears every stored database description and context.", nil) :
    NSLocalizedString(
      @"Study databases to save time and tokens when Strappy tries to query them in future prompts. Study sessions use the model selected in Session Defaults.",
      nil)];
}

- (NSString *)databaseStudyDateForRow:(NSDictionary *)row
{
  NSDate *date;
  NSNumber *studiedAt;
  NSTimeInterval seconds;

  if (!StrappyDatabaseStudyRowIsStudied(row)) {
    return @"";
  }
  studiedAt = [row objectForKey:@"studied_at_ms"];
  if (![studiedAt isKindOfClass:[NSNumber class]] ||
      ([studiedAt longLongValue] <= 0LL)) {
    return @"";
  }
  seconds = (NSTimeInterval)[studiedAt longLongValue] / 1000.0;
  date = [NSDate dateWithTimeIntervalSince1970:seconds];
  return [databaseStudyDateFormatter_ stringFromDate:date];
}

- (BOOL)databaseStudyRowIsExpanded:(NSDictionary *)row
{
  NSString *databaseIdentifier;

  if (!StrappyDatabaseStudyRowIsStudied(row) ||
      ([expandedDatabaseStudyIdentifier_ length] == 0U)) {
    return NO;
  }
  databaseIdentifier =
    StrappyDatabaseStudyStringForRow(row, @"database_id");
  return [databaseIdentifier
    isEqualToString:expandedDatabaseStudyIdentifier_] ? YES : NO;
}

- (void)databaseStudyRowClicked:(id)sender
{
  NSString *databaseIdentifier;
  NSDictionary *row;
  NSInteger rowIndex;

  if (sender != databaseStudyTableView_) {
    return;
  }
  rowIndex = [databaseStudyTableView_ clickedRow];
  if (rowIndex < 0) {
    rowIndex = [databaseStudyTableView_ selectedRow];
  }
  if ((rowIndex < 0) ||
      (rowIndex >= (NSInteger)[databaseStudyRows_ count])) {
    return;
  }

  row = [databaseStudyRows_ objectAtIndex:(NSUInteger)rowIndex];
  if (!StrappyDatabaseStudyRowIsStudied(row)) {
    [databaseStudyTableView_ deselectAll:self];
    return;
  }

  databaseIdentifier =
    StrappyDatabaseStudyStringForRow(row, @"database_id");
  if ([databaseIdentifier
        isEqualToString:expandedDatabaseStudyIdentifier_]) {
    [expandedDatabaseStudyIdentifier_ release];
    expandedDatabaseStudyIdentifier_ = nil;
  } else {
    [expandedDatabaseStudyIdentifier_ release];
    expandedDatabaseStudyIdentifier_ = [databaseIdentifier copy];
  }
  [databaseStudyTableView_ reloadData];
}

- (void)databaseStudySearchTextDidChange:(NSNotification *)notification
{
  if ([notification object] == databaseStudySearchField_) {
    [self applyDatabaseStudyRows];
  }
}

- (NSMenu *)whitelistTableView:(NSTableView *)tableView
             contextMenuForRow:(NSInteger)rowIndex
{
  NSDictionary *row;
  NSMenu *menu;
  NSMenuItem *item;

  if ((tableView != databaseStudyTableView_) || (rowIndex < 0) ||
      (rowIndex >= (NSInteger)[databaseStudyRows_ count])) {
    return nil;
  }
  row = [databaseStudyRows_ objectAtIndex:(NSUInteger)rowIndex];
  if (!StrappyDatabaseStudyRowIsStudied(row)) {
    return nil;
  }

  menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
  item = [[[NSMenuItem alloc]
    initWithTitle:NSLocalizedString(@"Delete Study...", nil)
            action:@selector(deleteDatabaseStudyMenuItem:)
     keyEquivalent:@""] autorelease];
  [item setTarget:self];
  [item setRepresentedObject:row];
  [menu addItem:item];
  return menu;
}

- (void)whitelistTableViewDidPressDelete:(NSTableView *)tableView
{
  NSInteger rowIndex;

  if (tableView != databaseStudyTableView_) {
    return;
  }
  rowIndex = [databaseStudyTableView_ selectedRow];
  if ((rowIndex < 0) ||
      (rowIndex >= (NSInteger)[databaseStudyRows_ count])) {
    return;
  }
  [self confirmDeleteDatabaseStudyForRow:
    [databaseStudyRows_ objectAtIndex:(NSUInteger)rowIndex]];
}

- (void)deleteDatabaseStudyMenuItem:(id)sender
{
  id row;

  row = [sender respondsToSelector:@selector(representedObject)] ?
    [sender representedObject] : nil;
  if ([row isKindOfClass:[NSDictionary class]]) {
    [self confirmDeleteDatabaseStudyForRow:row];
  }
}

- (void)confirmDeleteDatabaseStudyForRow:(NSDictionary *)row
{
  NSString *databaseIdentifier;
  NSString *databaseName;
  NSAlert *alert;
  NSWindow *window;

  if (!StrappyDatabaseStudyRowIsStudied(row)) {
    return;
  }
  databaseIdentifier =
    StrappyDatabaseStudyStringForRow(row, @"database_id");
  if ([databaseIdentifier length] == 0U) {
    NSBeep();
    return;
  }
  window = [self window];
  if (window == nil) {
    NSBeep();
    return;
  }

  databaseName = StrappyDatabaseStudyNameForRow(row);
  alert = [[[NSAlert alloc] init] autorelease];
  [alert setMessageText:[NSString stringWithFormat:
    NSLocalizedString(@"Delete Study for \"%@\"?", nil), databaseName]];
  [alert setInformativeText:NSLocalizedString(
    @"This clears the stored description and context for this database. You can study it again later.",
    nil)];
  [alert addButtonWithTitle:NSLocalizedString(@"Delete", nil)];
  [alert addButtonWithTitle:NSLocalizedString(@"Cancel", nil)];
  [alert XP_beginSheetModalForWindow:window
                       modalDelegate:self
                      didEndSelector:@selector(databaseStudyDeleteAlertDidEnd:returnCode:contextInfo:)
                         contextInfo:[databaseIdentifier retain]];
}

- (void)databaseStudyDeleteAlertDidEnd:(NSAlert *)alert
                            returnCode:(NSInteger)returnCode
                           contextInfo:(void *)contextInfo
{
  NSString *databaseIdentifier;
  NSError *error;

  (void)alert;
  databaseIdentifier = (NSString *)contextInfo;
  if (returnCode != NSAlertFirstButtonReturn) {
    [databaseIdentifier release];
    return;
  }

  error = nil;
  if (![StrappySession deleteDatabaseStudyValuesForDatabaseIdentifier:
        databaseIdentifier
                                                               error:&error]) {
    [databaseIdentifier release];
    [self showDatabaseStudyError:error
                           title:NSLocalizedString(@"Could Not Delete Study", nil)];
    return;
  }

  if ([expandedDatabaseStudyIdentifier_
        isEqualToString:databaseIdentifier]) {
    [expandedDatabaseStudyIdentifier_ release];
    expandedDatabaseStudyIdentifier_ = nil;
  }
  [databaseIdentifier release];
  [databaseStudyTableView_ deselectAll:self];
  [self loadDatabaseStudy];
}

- (void)showDatabaseStudyError:(NSError *)error title:(NSString *)title
{
  NSAlert *alert;
  NSString *message;
  NSWindow *window;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = NSLocalizedString(@"The request failed.", nil);
  }
  alert = [[[NSAlert alloc] init] autorelease];
  [alert setMessageText:title];
  [alert setInformativeText:message];
  [alert addButtonWithTitle:NSLocalizedString(@"OK", nil)];
  window = [self window];
  if (window == nil) {
    NSBeep();
    return;
  }
  [alert XP_beginSheetModalForWindow:window
                       modalDelegate:nil
                      didEndSelector:NULL
                         contextInfo:NULL];
}

- (void)resetDatabaseStudy:(id)sender
{
  NSAlert *alert;
  NSWindow *window;

  (void)sender;
  window = [self window];
  if (window == nil) {
    NSBeep();
    return;
  }
  alert = [[[NSAlert alloc] init] autorelease];
  [alert setMessageText:NSLocalizedString(@"Reset Database Study?", nil)];
  [alert setInformativeText:NSLocalizedString(
    @"This clears every stored database description and context.", nil)];
  [alert addButtonWithTitle:NSLocalizedString(@"Reset", nil)];
  [alert addButtonWithTitle:NSLocalizedString(@"Cancel", nil)];
  [alert XP_beginSheetModalForWindow:window
                       modalDelegate:self
                      didEndSelector:@selector(databaseStudyResetAlertDidEnd:returnCode:contextInfo:)
                         contextInfo:NULL];
}

- (void)databaseStudyResetAlertDidEnd:(NSAlert *)alert
                           returnCode:(NSInteger)returnCode
                          contextInfo:(void *)contextInfo
{
  NSError *error;

  (void)alert;
  (void)contextInfo;
  if (returnCode != NSAlertFirstButtonReturn) {
    return;
  }
  error = nil;
  if (![StrappySession resetDatabaseStudyWithError:&error]) {
    [self showDatabaseStudyError:error
                           title:NSLocalizedString(@"Could Not Reset Study", nil)];
    return;
  }
  [self loadDatabaseStudy];
}

- (void)beginDatabaseStudy:(id)sender
{
  NSAlert *alert;
  NSWindow *window;

  (void)sender;
  window = [self window];
  if (window == nil) {
    NSBeep();
    return;
  }
  alert = [[[NSAlert alloc] init] autorelease];
  [alert setMessageText:NSLocalizedString(@"Study Databases?", nil)];
  [alert setInformativeText:NSLocalizedString(
    @"Study databases to save time and tokens when Strappy tries to query them in future prompts. Study sessions use the model selected in Session Defaults.",
    nil)];
  [alert addButtonWithTitle:NSLocalizedString(@"Study", nil)];
  [alert addButtonWithTitle:NSLocalizedString(@"Cancel", nil)];
  [alert XP_beginSheetModalForWindow:window
                       modalDelegate:self
                      didEndSelector:@selector(databaseStudyRunAlertDidEnd:returnCode:contextInfo:)
                         contextInfo:NULL];
}

- (void)databaseStudyRunAlertDidEnd:(NSAlert *)alert
                         returnCode:(NSInteger)returnCode
                        contextInfo:(void *)contextInfo
{
  NSError *error;

  (void)alert;
  (void)contextInfo;
  if (returnCode != NSAlertFirstButtonReturn) {
    return;
  }
  error = nil;
  if ([StrappySession beginDatabaseStudyWithError:&error] == nil) {
    [self showDatabaseStudyError:error
                           title:NSLocalizedString(@"Could Not Start Study", nil)];
    return;
  }
  [[self window] close];
}

- (void)loadSystemPrompt
{
  NSString *prompt;
  NSError *error;

  error = nil;
  prompt = [StrappySession
    systemPromptForAssistantSetIdentifier:@"personal_assistant"
                         webSearchEnabled:YES
                                    error:&error];
  if (prompt == nil) {
    prompt = (error != nil) ? [error localizedDescription] :
      NSLocalizedString(@"System prompt could not be generated.", nil);
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

  [modelStatusLabel_ setToolTip:nil];
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
    if (noAvailableModelAccounts_) {
      [modelStatusLabel_ setStringValue:
        NSLocalizedString(@"No accounts configured.", nil)];
    } else {
      [modelStatusLabel_ setStringValue:
        NSLocalizedString(@"No models have been fetched yet.", nil)];
    }
  } else if (count == 1U) {
    [modelStatusLabel_ setStringValue:
      NSLocalizedString(@"1 model available.", nil)];
  } else {
    [modelStatusLabel_ setStringValue:
      [NSString stringWithFormat:NSLocalizedString(@"%lu models available.", nil),
        (unsigned long)count]];
  }
}

- (void)setModelStatusErrorMessage:(NSString *)message
{
  if (modelStatusLabel_ == nil) {
    return;
  }
  if ([message length] == 0U) {
    message = NSLocalizedString(@"Your changes could not be saved.", nil);
  }
  [modelStatusLabel_ setStringValue:message];
  [modelStatusLabel_ setToolTip:message];
}

- (void)loadOpenRouterModels
{
  NSError *error;
  NSArray *accounts;
  NSArray *rows;

  error = nil;
  rows = [StrappySession modelCatalogWithError:&error];
  if (rows != nil) {
    accounts = [StrappySession providerAccountCatalogWithError:&error];
    if (accounts == nil) {
      [self setModelStatusErrorMessage:StrappyPreferencesErrorMessage(
        error,
        NSLocalizedString(@"Accounts could not be loaded.", nil))];
      return;
    }
    noAvailableModelAccounts_ =
      !StrappyAccountsContainAvailableModelAccount(accounts);
    [allModelRows_ release];
    allModelRows_ = [StrappyPreparedModelRowsForRows(
      StrappyModelRowsForAvailableAccounts(rows, accounts)) copy];
    [self sortAllModelRows];
    [self applyModelRows];
    return;
  }

  [self setModelStatusErrorMessage:StrappyPreferencesErrorMessage(
    error,
    NSLocalizedString(@"Model list could not be loaded.", nil))];
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
                                  @"provider_model_key");
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
    if ([StrappyStringForModelRow(row, @"provider_model_key")
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

- (void)showHiddenDatabasesChanged:(id)sender
{
  NSArray *selectedPaths;

  if (sender != showHiddenDatabasesButton_) {
    return;
  }

  selectedPaths = [[self selectedDatabaseTableRowPaths] retain];
  [self applyDatabaseRows];
  [self selectDatabaseTableRowsWithPaths:selectedPaths];
  [selectedPaths release];
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
    [modelStatusLabel_ setToolTip:nil];
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
  NSWindow *window;

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
    [alert addButtonWithTitle:NSLocalizedString(@"OK", nil)];
    window = [self window];
    if (window == nil) {
      NSBeep();
      return;
    }
    [alert XP_beginSheetModalForWindow:window
                         modalDelegate:nil
                        didEndSelector:NULL
                           contextInfo:NULL];
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
    [self setModelStatusErrorMessage:errorMessage];
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
    NSString *appName;
    NSString *appBundleId;
    NSString *location;
    NSString *path;

    row = [rows objectAtIndex:index];
    if (![row isKindOfClass:[NSDictionary class]]) {
      continue;
    }

    name = StrappyDatabaseNameForRow(row);
    appName = StrappyDatabaseAppNameForRow(row);
    appBundleId = StrappyDatabaseBundleIdentifierForRow(row);
    location = StrappyDatabaseLocationForRow(row);
    path = StrappyDatabasePathForRow(row);
    if (([name rangeOfString:searchText
                     options:NSCaseInsensitiveSearch].location != NSNotFound) ||
        ([appName rangeOfString:searchText
                        options:NSCaseInsensitiveSearch].location != NSNotFound) ||
        ([appBundleId rangeOfString:searchText
                            options:NSCaseInsensitiveSearch].location !=
         NSNotFound) ||
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
  NSMutableArray *visibleRows;
  NSArray *rows;
  NSUInteger index;

  rows = allDatabaseRows_;
  if ([showHiddenDatabasesButton_ state] != XPControlStateValueOn) {
    visibleRows = [NSMutableArray arrayWithCapacity:[allDatabaseRows_ count]];
    for (index = 0U; index < [allDatabaseRows_ count]; index++) {
      NSDictionary *row;

      row = [allDatabaseRows_ objectAtIndex:index];
      if (![row isKindOfClass:[NSDictionary class]]) {
        continue;
      }
      if (StrappyDatabaseRowHiddenValue(row) &&
          !StrappyDatabaseRowAllowedValue(row)) {
        continue;
      }
      [visibleRows addObject:row];
    }
    rows = visibleRows;
  }

  rows = [self databaseRows:rows
        matchingSearchText:[self currentDatabaseSearchText]];
  rows = [databaseWhitelistView_ sortedRows:rows];
  [databaseRows_ release];
  databaseRows_ = [rows copy];
  [databaseTableView_ reloadData];
  [self refreshDatabaseStatus];
}

- (void)loadCatalogedDatabases
{
  NSError *error;
  NSArray *rows;

  error = nil;
  rows = [[FileScanner sharedScanner] catalogedSQLiteDatabasesWithError:&error];
  if (rows != nil) {
    [allDatabaseRows_ release];
    allDatabaseRows_ = [rows copy];
    [self applyDatabaseRows];
    return;
  }

  [self setDatabaseStatusErrorMessage:StrappyPreferencesErrorMessage(
    error,
    NSLocalizedString(@"Rows could not be loaded.", nil))];
}

- (void)databaseCatalogDidChange:(NSNotification *)notification
{
  NSArray *rows;
  NSArray *selectedPaths;

  rows = [[notification userInfo] objectForKey:@"rows"];
  if (![rows isKindOfClass:[NSArray class]]) {
    return;
  }
  selectedPaths = [self selectedDatabaseTableRowPaths];
  [allDatabaseRows_ release];
  allDatabaseRows_ = [rows copy];
  [self applyDatabaseRows];
  [self selectDatabaseTableRowsWithPaths:selectedPaths];
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

  [databaseStatusLabel_ setToolTip:nil];
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

- (void)setDatabaseStatusErrorMessage:(NSString *)message
{
  if (databaseStatusLabel_ == nil) {
    return;
  }
  if ([message length] == 0U) {
    message = NSLocalizedString(@"Your changes could not be saved.", nil);
  }
  [databaseStatusLabel_ setStringValue:message];
  [databaseStatusLabel_ setToolTip:message];
}

- (void)scanDatabases:(id)sender
{
  NSAlert *alert;
  NSWindow *window;

  (void)sender;
  if (scanning_) {
    return;
  }

  window = [self window];
  if (window == nil) {
    NSBeep();
    return;
  }

  alert = [[[NSAlert alloc] init] autorelease];
  [alert setMessageText:NSLocalizedString(@"Scan Databases?", nil)];
  [alert setInformativeText:NSLocalizedString(
    @"Scans your home folder for SQLite databases. After scanning, whitelist desired databases so Strappy can read their data. Quick Scan saves time by only scanning files with common file extensions used for SQLite databases.",
    nil)];
  [alert addButtonWithTitle:NSLocalizedString(@"Quick Scan", nil)];
  [alert addButtonWithTitle:NSLocalizedString(@"Full Scan", nil)];
  [alert addButtonWithTitle:NSLocalizedString(@"Cancel", nil)];
  [alert XP_beginSheetModalForWindow:window
                       modalDelegate:self
                      didEndSelector:@selector(databaseScanAlertDidEnd:returnCode:contextInfo:)
                         contextInfo:NULL];
}

- (void)databaseScanAlertDidEnd:(NSAlert *)alert
                      returnCode:(NSInteger)returnCode
                     contextInfo:(void *)contextInfo
{
  (void)alert;
  (void)contextInfo;

  if (returnCode == NSAlertFirstButtonReturn) {
    [self beginDatabaseScanWithMode:FileScannerDatabaseScanModeQuick];
  } else if (returnCode == NSAlertSecondButtonReturn) {
    [self beginDatabaseScanWithMode:FileScannerDatabaseScanModeFull];
  }
}

- (void)beginDatabaseScanWithMode:(FileScannerDatabaseScanMode)scanMode
{
  NSDictionary *request;

  if (scanning_) {
    return;
  }

  scanMode = (scanMode == FileScannerDatabaseScanModeQuick) ?
    FileScannerDatabaseScanModeQuick : FileScannerDatabaseScanModeFull;
  request = [[NSDictionary alloc] initWithObjectsAndKeys:
    NSHomeDirectory(), @"path",
    [NSNumber XP_numberWithInteger:(XPInteger)scanMode], @"scan_mode",
    nil];
  [self setScanning:YES];
  [self retain];
  [NSThread detachNewThreadSelector:@selector(scanDatabasesInBackground:)
                           toTarget:self
                         withObject:request];
  [request release];
}

- (void)scanDatabasesInBackground:(NSDictionary *)request
{
  NSAutoreleasePool *pool;
  NSError *error;
  NSArray *rows;
  NSMutableDictionary *result;
  NSString *errorMessage;
  NSString *rootPath;
  NSNumber *scanModeNumber;
  FileScannerDatabaseScanMode scanMode;

  pool = [[NSAutoreleasePool alloc] init];
  rootPath = [request objectForKey:@"path"];
  scanModeNumber = [request objectForKey:@"scan_mode"];
  scanMode = ([scanModeNumber isKindOfClass:[NSNumber class]] &&
              ([scanModeNumber XP_integerValue] ==
               FileScannerDatabaseScanModeQuick)) ?
    FileScannerDatabaseScanModeQuick : FileScannerDatabaseScanModeFull;
  error = nil;
  rows = [[FileScanner sharedScanner] scanDirectoryForSQLiteDatabasesAtPath:rootPath
                                                                   scanMode:scanMode
                                            savingResultsToCatalogWithError:&error];
  result = [[NSMutableDictionary alloc] init];
  if (rows != nil) {
    [result setObject:rows forKey:@"rows"];
  } else {
    errorMessage = StrappyPreferencesErrorMessage(
      error,
      NSLocalizedString(@"Database scan failed.", nil));
    [result setObject:errorMessage forKey:@"error"];
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
  NSString *errorMessage;
  NSArray *rows;

  rows = [result objectForKey:@"rows"];
  if ([rows isKindOfClass:[NSArray class]]) {
    [allDatabaseRows_ release];
    allDatabaseRows_ = [rows copy];
    [self applyDatabaseRows];
  }

  [self setScanning:NO];
  if (![rows isKindOfClass:[NSArray class]]) {
    errorMessage = [result objectForKey:@"error"];
    [self setDatabaseStatusErrorMessage:
      [errorMessage isKindOfClass:[NSString class]] ? errorMessage :
        NSLocalizedString(@"Database scan failed.", nil)];
  }
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
  NSError *error;
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
    [self setModelStatusErrorMessage:
      NSLocalizedString(@"Default model is always allowed.", nil)];
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
    error = nil;
    if (([modelId length] == 0U) ||
        ![StrappySession setModelAllowed:shouldAllow
                      forModelIdentifier:modelId
                                   error:&error]) {
      [self loadOpenRouterModels];
      [self setModelStatusErrorMessage:StrappyPreferencesErrorMessage(
        error,
        NSLocalizedString(@"Your changes could not be saved.", nil))];
      return;
    }
  }

  [self loadOpenRouterModels];
}

- (void)toggleSelectedDatabaseRows
{
  NSError *error;
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
    [self setDatabaseStatusErrorMessage:
      NSLocalizedString(@"This file is not a valid SQLite database.", nil)];
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
    error = nil;
    if (![[FileScanner sharedScanner] setCatalogedDatabaseAllowed:shouldAllow
                                             forCatalogIdentifier:catalogId
                                                            error:&error]) {
      [self loadCatalogedDatabases];
      [self setDatabaseStatusErrorMessage:StrappyPreferencesErrorMessage(
        error,
        NSLocalizedString(@"Your changes could not be saved.", nil))];
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
  if (tableView == databaseStudyTableView_) {
    return (NSInteger)[databaseStudyRows_ count];
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
    return;
  }

  if (tableView == databaseStudyTableView_) {
    [self applyDatabaseStudyRows];
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
    if ([identifier isEqualToString:@"model_provider"]) {
      return StrappyStringForModelRow(model, @"provider_name");
    }
    if ([identifier isEqualToString:@"model_id"]) {
      return StrappyStringForModelRow(model, @"wire_model_id");
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

  if (tableView == databaseStudyTableView_) {
    if ((row < 0) || (row >= (NSInteger)[databaseStudyRows_ count])) {
      return nil;
    }

    database = [databaseStudyRows_ objectAtIndex:(NSUInteger)row];
    identifier = [tableColumn identifier];
    if ([identifier isEqualToString:@"study_studied"]) {
      return [NSNumber numberWithBool:
        StrappyDatabaseStudyRowIsStudied(database)];
    }
    if ([identifier isEqualToString:@"study_application"]) {
      return StrappyDatabaseStudyAppNameForRow(database);
    }
    if ([identifier isEqualToString:@"study_name"]) {
      return StrappyDatabaseStudyNameForRow(database);
    }
    if ([identifier isEqualToString:@"study_last_studied"]) {
      return [self databaseStudyDateForRow:database];
    }
    if ([identifier isEqualToString:@"study_description"]) {
      return StrappyDatabaseStudyStringForRow(database, @"description");
    }
    if ([identifier isEqualToString:@"study_context"]) {
      return StrappyDatabaseStudyStringForRow(database, @"context");
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
  if ([identifier isEqualToString:@"hidden"]) {
    return [NSNumber numberWithBool:StrappyDatabaseRowHiddenValue(database)];
  }
  if ([identifier isEqualToString:@"application"]) {
    return StrappyDatabaseAppNameForRow(database);
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
      return StrappyStringForModelRow(model, @"wire_model_id");
    }
    if ([identifier isEqualToString:@"model_provider"]) {
      return StrappyStringForModelRow(model, @"provider_id");
    }
    if ([identifier isEqualToString:@"model_id"]) {
      return StrappyStringForModelRow(model, @"wire_model_id");
    }
    return @"";
  }

  if (tableView == databaseStudyTableView_) {
    if ((row < 0) || (row >= (NSInteger)[databaseStudyRows_ count])) {
      return @"";
    }

    database = [databaseStudyRows_ objectAtIndex:(NSUInteger)row];
    identifier = [tableColumn identifier];
    if ([identifier isEqualToString:@"study_studied"]) {
      if (!StrappyDatabaseStudyRowIsStudied(database)) {
        return @"";
      }
      return [self databaseStudyRowIsExpanded:database] ?
        NSLocalizedString(@"Hides the recorded description and context.", nil) :
        NSLocalizedString(@"Shows the recorded description and context.", nil);
    }
    if ([identifier isEqualToString:@"study_application"]) {
      NSString *bundleIdentifier;

      bundleIdentifier = StrappyDatabaseStudyStringForRow(
        database,
        @"app_bundle_id");
      return ([bundleIdentifier length] > 0U) ?
        bundleIdentifier : StrappyDatabaseStudyAppNameForRow(database);
    }
    if ([identifier isEqualToString:@"study_name"]) {
      return StrappyDatabaseStudyStringForRow(database, @"path");
    }
    if ([identifier isEqualToString:@"study_last_studied"]) {
      return [self databaseStudyDateForRow:database];
    }
    if ([identifier isEqualToString:@"study_description"]) {
      return StrappyDatabaseStudyStringForRow(database, @"description");
    }
    if ([identifier isEqualToString:@"study_context"]) {
      return StrappyDatabaseStudyStringForRow(database, @"context");
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

- (BOOL)tableView:(NSTableView *)tableView shouldSelectRow:(NSInteger)row
{
  if (tableView != databaseStudyTableView_) {
    return YES;
  }
  if ((row < 0) || (row >= (NSInteger)[databaseStudyRows_ count])) {
    return NO;
  }
  return StrappyDatabaseStudyRowIsStudied(
    [databaseStudyRows_ objectAtIndex:(NSUInteger)row]);
}

- (CGFloat)tableView:(NSTableView *)tableView heightOfRow:(NSInteger)row
{
  NSDictionary *studyRow;

  if ((tableView != databaseStudyTableView_) || (row < 0) ||
      (row >= (NSInteger)[databaseStudyRows_ count])) {
    return [tableView rowHeight];
  }
  studyRow = [databaseStudyRows_ objectAtIndex:(NSUInteger)row];
  if (![self databaseStudyRowIsExpanded:studyRow]) {
    return [tableView rowHeight];
  }
  return [databaseStudyPaneView_
    expandedRowHeightForDescription:
      StrappyDatabaseStudyStringForRow(studyRow, @"description")
                           context:
      StrappyDatabaseStudyStringForRow(studyRow, @"context")];
}

- (void)tableView:(NSTableView *)tableView
   setObjectValue:(id)object
   forTableColumn:(NSTableColumn *)tableColumn
              row:(NSInteger)row
{
  NSDictionary *database;
  NSDictionary *model;
  NSError *error;
  NSString *identifier;
  NSString *validationError;
  NSNumber *catalogId;
  NSArray *selectedPaths;
  BOOL checked;

  identifier = [tableColumn identifier];
  checked = ([object respondsToSelector:@selector(boolValue)] &&
             [object boolValue]) ? YES : NO;

  if (tableView == databaseStudyTableView_) {
    return;
  }

  if (tableView == modelTableView_) {
    NSString *modelId;

    if ((row < 0) || (row >= (NSInteger)[modelRows_ count]) ||
        ![identifier isEqualToString:@"model_allowed"]) {
      return;
    }

    model = [modelRows_ objectAtIndex:(NSUInteger)row];
    if ([self modelRowIsDefault:model] && !checked) {
      [modelTableView_ reloadData];
      [self setModelStatusErrorMessage:
        NSLocalizedString(@"Default model is always allowed.", nil)];
      return;
    }

    modelId = StrappyStringForModelRow(model, @"id");
    error = nil;
    if (([modelId length] == 0U) ||
        ![StrappySession setModelAllowed:checked
                      forModelIdentifier:modelId
                                   error:&error]) {
      [modelTableView_ reloadData];
      [self setModelStatusErrorMessage:StrappyPreferencesErrorMessage(
        error,
        NSLocalizedString(@"Your changes could not be saved.", nil))];
      return;
    }

    [self loadOpenRouterModels];
    return;
  }

  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    return;
  }
  if (![identifier isEqualToString:@"allowed"] &&
      ![identifier isEqualToString:@"hidden"]) {
    return;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  if ([identifier isEqualToString:@"allowed"] && checked &&
      ![self databaseRowCanBeAllowed:database]) {
    validationError = [database objectForKey:@"validation_error"];
    if (![validationError isKindOfClass:[NSString class]] ||
        ([validationError length] == 0U)) {
      validationError =
        NSLocalizedString(@"This file is not a valid SQLite database.", nil);
    }
    [databaseTableView_ reloadData];
    [self setDatabaseStatusErrorMessage:validationError];
    return;
  }

  catalogId = [database objectForKey:@"catalog_id"];
  error = nil;
  if ([identifier isEqualToString:@"allowed"] &&
      ![[FileScanner sharedScanner] setCatalogedDatabaseAllowed:checked
                                            forCatalogIdentifier:catalogId
                                                           error:&error]) {
    [databaseTableView_ reloadData];
    [self setDatabaseStatusErrorMessage:StrappyPreferencesErrorMessage(
      error,
      NSLocalizedString(@"Your changes could not be saved.", nil))];
    return;
  }
  if ([identifier isEqualToString:@"hidden"] &&
      ![[FileScanner sharedScanner] setCatalogedDatabaseHidden:checked
                                           forCatalogIdentifier:catalogId
                                                          error:&error]) {
    [databaseTableView_ reloadData];
    [self setDatabaseStatusErrorMessage:StrappyPreferencesErrorMessage(
      error,
      NSLocalizedString(@"Your changes could not be saved.", nil))];
    return;
  }

  selectedPaths = [[self selectedDatabaseTableRowPaths] retain];
  [self loadCatalogedDatabases];
  [self selectDatabaseTableRowsWithPaths:selectedPaths];
  [selectedPaths release];
}

- (BOOL)tableView:(NSTableView *)tableView
 shouldEditTableColumn:(NSTableColumn *)tableColumn
              row:(NSInteger)row
{
  NSDictionary *database;

  if (tableView == databaseStudyTableView_) {
    return NO;
  }

  if (tableView == modelTableView_) {
    NSDictionary *model;

    if (![[tableColumn identifier] isEqualToString:@"model_allowed"] ||
        (row < 0) || (row >= (NSInteger)[modelRows_ count])) {
      return NO;
    }

    model = [modelRows_ objectAtIndex:(NSUInteger)row];
    return [self modelRowIsDefault:model] ? NO : YES;
  }

  if (![[tableColumn identifier] isEqualToString:@"allowed"] &&
      ![[tableColumn identifier] isEqualToString:@"hidden"]) {
    return NO;
  }
  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    return NO;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  if ([[tableColumn identifier] isEqualToString:@"hidden"]) {
    return YES;
  }
  return [self databaseRowCanBeAllowed:database];
}

- (void)tableView:(NSTableView *)tableView
  willDisplayCell:(id)cell
   forTableColumn:(NSTableColumn *)tableColumn
              row:(NSInteger)row
{
  NSDictionary *database;

  if (tableView == databaseStudyTableView_) {
    BOOL expanded;
    BOOL studied;
    NSString *identifier;

    if ((row < 0) || (row >= (NSInteger)[databaseStudyRows_ count])) {
      return;
    }
    database = [databaseStudyRows_ objectAtIndex:(NSUInteger)row];
    identifier = [tableColumn identifier];
    studied = StrappyDatabaseStudyRowIsStudied(database);
    expanded = [self databaseStudyRowIsExpanded:database];

    if ([identifier isEqualToString:@"study_name"] &&
        [cell respondsToSelector:@selector(setFont:)]) {
      [cell setFont:studied ? [NSFont boldSystemFontOfSize:12.0] :
                              [NSFont systemFontOfSize:12.0]];
    }
    if (([identifier isEqualToString:@"study_name"] ||
         [identifier isEqualToString:@"study_application"] ||
         [identifier isEqualToString:@"study_last_studied"] ||
         [identifier isEqualToString:@"study_description"] ||
         [identifier isEqualToString:@"study_context"]) &&
        [cell respondsToSelector:@selector(setTextColor:)]) {
      [cell setTextColor:studied ? [NSColor controlTextColor] :
                                  [NSColor disabledControlTextColor]];
    }
    if (([identifier isEqualToString:@"study_description"] ||
         [identifier isEqualToString:@"study_context"]) &&
        [cell respondsToSelector:@selector(setWraps:)] &&
        [cell respondsToSelector:@selector(setLineBreakMode:)]) {
      [cell setWraps:expanded];
      [cell setLineBreakMode:expanded ? NSLineBreakByWordWrapping :
                                        NSLineBreakByTruncatingTail];
    }
    return;
  }

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

  if ((![[tableColumn identifier] isEqualToString:@"allowed"] &&
       ![[tableColumn identifier] isEqualToString:@"hidden"]) ||
      ![cell respondsToSelector:@selector(setEnabled:)]) {
    return;
  }
  if ((row < 0) || (row >= (NSInteger)[databaseRows_ count])) {
    [cell setEnabled:NO];
    return;
  }

  database = [databaseRows_ objectAtIndex:(NSUInteger)row];
  if ([[tableColumn identifier] isEqualToString:@"hidden"]) {
    [cell setEnabled:YES];
  } else {
    [cell setEnabled:[self databaseRowCanBeAllowed:database]];
  }
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
  [sessionDefaultsController_ release];
  [modelSearchField_ release];
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
  [databaseStudyPaneView_ release];
  [databaseStudySearchField_ release];
  [databaseStudyTableView_ release];
  [databaseStudyActionButton_ release];
  [databaseStudyStatusLabel_ release];
  [scanButton_ release];
  [showHiddenDatabasesButton_ release];
  [scanProgressIndicator_ release];
  [databaseStatusLabel_ release];
  [allModelRows_ release];
  [modelRows_ release];
  [allDatabaseRows_ release];
  [databaseRows_ release];
  [allDatabaseStudyRows_ release];
  [databaseStudyRows_ release];
  [databaseStudyDateFormatter_ release];
  [expandedDatabaseStudyIdentifier_ release];
  [super dealloc];
}

@end
