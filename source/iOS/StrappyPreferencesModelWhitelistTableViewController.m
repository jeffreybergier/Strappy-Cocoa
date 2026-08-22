#import "StrappyPreferencesModelWhitelistTableViewController.h"

#import "StrappyAppearance.h"
#import "StrappyModelCellFormatter.h"
#import "StrappyModelProvidersTableViewController.h"
#import "StrappySession.h"

static NSString * const kStrappyModelSearchTextKey =
  @"_strappy_model_search_text";
static NSString *StrappyStringForModelRow(NSDictionary *row, NSString *key)
{
  NSString *value;

  value = [row objectForKey:key];
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

static NSString *StrappyModelDisplayNameForRow(NSDictionary *row)
{
  NSString *name;

  name = StrappyStringForModelRow(row, @"name");
  return ([name length] > 0U) ? name :
    StrappyStringForModelRow(row, @"wire_model_id");
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
    NSString *providerIdentifier;
    NSString *wireModelIdentifier;
    NSString *key;

    row = [rows objectAtIndex:index];
    if (![row isKindOfClass:[NSDictionary class]]) {
      continue;
    }

    providerIdentifier = StrappyStringForModelRow(row, @"provider_id");
    wireModelIdentifier = StrappyStringForModelRow(row, @"wire_model_id");
    if (([providerIdentifier length] == 0U) ||
        ([wireModelIdentifier length] == 0U)) {
      continue;
    }
    key = [NSString stringWithFormat:@"%@\n%@", providerIdentifier,
      wireModelIdentifier];
    providerModel = [rowsByProviderModel objectForKey:key];
    if (providerModel == nil) {
      NSString *providerName;

      providerModel = [NSMutableDictionary dictionaryWithDictionary:row];
      [providerModel setObject:key forKey:@"provider_model_key"];
      if ([providerIdentifier isEqualToString:@"openrouter"]) providerName = @"OpenRouter";
      else if ([providerIdentifier isEqualToString:@"openai_chatgpt"]) providerName = @"ChatGPT";
      else providerName = NSLocalizedString(@"Other", nil);
      [providerModel setObject:providerName forKey:@"provider_name"];
      [rowsByProviderModel setObject:providerModel forKey:key];
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

        savedKey = [providerModel objectForKey:@"provider_model_key"];
        savedProviderName = [providerModel objectForKey:@"provider_name"];
        [providerModel setDictionary:row];
        [providerModel setObject:savedKey forKey:@"provider_model_key"];
        [providerModel setObject:savedProviderName forKey:@"provider_name"];
      }
      [providerModel setObject:[NSNumber numberWithBool:allowed] forKey:@"allowed"];
      [providerModel setObject:[NSNumber numberWithBool:selected] forKey:@"selected"];
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

static NSComparisonResult StrappyCompareStrings(NSString *left, NSString *right)
{
  if (![left isKindOfClass:[NSString class]]) {
    left = @"";
  }
  if (![right isKindOfClass:[NSString class]]) {
    right = @"";
  }
  return [left caseInsensitiveCompare:right];
}

static NSComparisonResult StrappyCompareBooleans(BOOL left, BOOL right)
{
  if (left == right) {
    return NSOrderedSame;
  }
  return left ? NSOrderedAscending : NSOrderedDescending;
}

static NSComparisonResult StrappyCompareDouble(double left, double right)
{
  if (left < right) {
    return NSOrderedAscending;
  }
  if (left > right) {
    return NSOrderedDescending;
  }
  return NSOrderedSame;
}

static BOOL StrappyModelRowIsDefault(NSDictionary *row)
{
  NSNumber *selected;

  selected = [row objectForKey:@"selected"];
  return ([selected isKindOfClass:[NSNumber class]] && [selected boolValue]) ?
    YES : NO;
}

static BOOL StrappyModelRowIsAllowed(NSDictionary *row)
{
  NSNumber *allowed;

  if (StrappyModelRowIsDefault(row)) {
    return YES;
  }
  allowed = [row objectForKey:@"allowed"];
  return ([allowed isKindOfClass:[NSNumber class]] && [allowed boolValue]) ?
    YES : NO;
}

static NSComparisonResult StrappyCompareModelWhitelistRows(id left,
                                                           id right,
                                                           void *context)
{
  NSDictionary *leftRow;
  NSDictionary *rightRow;
  NSComparisonResult result;

  (void)context;
  leftRow = [left isKindOfClass:[NSDictionary class]] ? left : nil;
  rightRow = [right isKindOfClass:[NSDictionary class]] ? right : nil;
  result = StrappyCompareStrings(
    StrappyStringForModelRow(leftRow, @"provider_name"),
    StrappyStringForModelRow(rightRow, @"provider_name"));
  if (result != NSOrderedSame) {
    return result;
  }
  result = StrappyCompareBooleans(StrappyModelRowIsAllowed(leftRow),
                                  StrappyModelRowIsAllowed(rightRow));
  if (result != NSOrderedSame) {
    return result;
  }
  result = StrappyCompareStrings(
    StrappyStringForModelRow(leftRow, @"wire_model_id"),
    StrappyStringForModelRow(rightRow, @"wire_model_id"));
  if (result != NSOrderedSame) {
    return result;
  }
  result = StrappyCompareDouble(
    [StrappyStringForModelRow(leftRow, @"pricing_completion") doubleValue],
    [StrappyStringForModelRow(rightRow, @"pricing_completion") doubleValue]);
  if (result != NSOrderedSame) {
    return result;
  }
  return StrappyCompareDouble(
    [StrappyStringForModelRow(leftRow, @"pricing_prompt") doubleValue],
    [StrappyStringForModelRow(rightRow, @"pricing_prompt") doubleValue]);
}

static NSArray *StrappyModelAccountIdentifiersForRows(NSArray *rows)
{
  NSMutableArray *identifiers;
  NSUInteger index;

  identifiers = [NSMutableArray array];
  for (index = 0U; index < [rows count]; index++) {
    NSDictionary *row;
    NSString *identifier;

    row = [rows objectAtIndex:index];
    identifier = StrappyStringForModelRow(row, @"provider_id");
    if (([identifier length] > 0U) &&
        ![identifiers containsObject:identifier]) {
      [identifiers addObject:identifier];
    }
  }
  return identifiers;
}

static NSArray *StrappyModelRowsForAccount(NSArray *rows,
                                           NSString *accountIdentifier)
{
  NSMutableArray *accountRows;
  NSUInteger index;

  accountRows = [NSMutableArray array];
  for (index = 0U; index < [rows count]; index++) {
    NSDictionary *row;

    row = [rows objectAtIndex:index];
    if ([StrappyStringForModelRow(row, @"provider_id")
          isEqualToString:accountIdentifier]) {
      [accountRows addObject:row];
    }
  }
  return accountRows;
}

@interface StrappyPreferencesModelWhitelistTableViewController ()
@property (nonatomic, assign) BOOL hasConfiguredAccounts;
@property (nonatomic, assign) BOOL refreshingModels;
@property (nonatomic, strong) UIBarButtonItem *updateButton;
@end
@implementation StrappyPreferencesModelWhitelistTableViewController

- (instancetype)init
{
  if ((self = [super initWithTitle:NSLocalizedString(@"Models", nil)])) {
  }
  return self;
}

- (void)viewDidLoad
{
  UIBarButtonItem *updateButton;

  [super viewDidLoad];

  updateButton = [[UIBarButtonItem alloc]
    initWithTitle:NSLocalizedString(@"Edit", nil)
            style:UIBarButtonItemStyleBordered
           target:self
           action:@selector(actionButtonPressed:)];
  [updateButton
    setAccessibilityLabel:NSLocalizedString(@"Edit Model Providers", nil)];
  [self setUpdateButton:updateButton];
  [[self navigationItem] setRightBarButtonItem:updateButton];
  [StrappyAppearance applyLegacyTintToBarButtonItem:updateButton];

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
       selector:@selector(providerAccountsDidChange:)
           name:StrappyProviderAccountsDidChangeNotification
         object:nil];
  [self setRefreshingModels:[StrappySession isModelCatalogRefreshInFlight]];
}

- (NSArray *)loadAllRowsWithError:(NSError **)error
{
  NSArray *accounts;

  accounts = [StrappySession providerAccountCatalogWithError:error];
  if (accounts == nil) {
    [self setHasConfiguredAccounts:NO];
    return nil;
  }
  [self setHasConfiguredAccounts:([accounts count] > 0U) ? YES : NO];
  return [StrappySession configuredProviderModelCatalogWithError:error];
}

- (NSArray *)preparedRowsForRows:(NSArray *)rows
{
  return StrappyPreparedModelRowsForRows(rows);
}

- (NSArray *)sortedRows:(NSArray *)rows
{
  return [rows sortedArrayUsingFunction:StrappyCompareModelWhitelistRows
                                context:NULL];
}

- (BOOL)row:(NSDictionary *)row matchesSearchText:(NSString *)searchText
{
  NSString *rowSearchText;

  if ([searchText length] == 0U) {
    return YES;
  }

  rowSearchText = [row objectForKey:kStrappyModelSearchTextKey];
  if (![rowSearchText isKindOfClass:[NSString class]]) {
    rowSearchText = StrappyModelSearchTextForRow(row);
  }
  return ([rowSearchText rangeOfString:[searchText lowercaseString]].location !=
          NSNotFound);
}

- (NSArray *)modelAccountIdentifiers
{
  return StrappyModelAccountIdentifiersForRows([self rows]);
}

- (NSArray *)modelRowsInSection:(NSInteger)section
{
  NSArray *accounts;

  accounts = [self modelAccountIdentifiers];
  if ((section < 0) || ((NSUInteger)section >= [accounts count])) {
    return [NSArray array];
  }
  return StrappyModelRowsForAccount(
    [self rows],
    [accounts objectAtIndex:(NSUInteger)section]);
}

- (NSDictionary *)modelRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSArray *sectionRows;

  sectionRows = [self modelRowsInSection:[indexPath section]];
  if (([indexPath row] < 0) ||
      ((NSUInteger)[indexPath row] >= [sectionRows count])) {
    return nil;
  }
  return [sectionRows objectAtIndex:(NSUInteger)[indexPath row]];
}

- (BOOL)modelRowIsDefault:(NSDictionary *)row
{
  return StrappyModelRowIsDefault(row);
}

- (BOOL)allowedValueForModelRow:(NSDictionary *)row
{
  return StrappyModelRowIsAllowed(row);
}

- (BOOL)rowIsSelected:(NSDictionary *)row
{
  return [self allowedValueForModelRow:row];
}

- (NSString *)workingStatusText
{
  if ([self refreshingModels]) {
    return NSLocalizedString(@"Fetching...", nil);
  }
  return nil;
}

- (NSString *)statusText
{
  if (![self working] && ([[self statusMessage] length] == 0U) &&
      ([[self currentSearchText] length] == 0U) &&
      ([[self allRows] count] == 0U)) {
    return [self hasConfiguredAccounts] ?
      NSLocalizedString(@"No Models Available", nil) :
      NSLocalizedString(@"No Accounts Configured", nil);
  }
  return [super statusText];
}

- (BOOL)showsStatusToolbarActionButton
{
  return NO;
}

- (NSString *)actionButtonAccessibilityLabel
{
  return NSLocalizedString(@"Update Models", nil);
}

- (void)configureCell:(UITableViewCell *)cell withRow:(NSDictionary *)row
{
  [[cell textLabel] setText:StrappyModelDisplayNameForRow(row)];
  [[cell detailTextLabel] setText:StrappyModelCellDetailText(row)];
  [[cell imageView] setImage:nil];
  [cell setAccessoryType:[self allowedValueForModelRow:row]
    ? UITableViewCellAccessoryCheckmark
    : UITableViewCellAccessoryNone];
  [[cell textLabel] setTextColor:[UIColor blackColor]];
}

- (void)actionButtonPressed:(id)sender
{
  (void)sender;
  [[self navigationController] pushViewController:
    [[StrappyModelProvidersTableViewController alloc] init] animated:YES];
}

- (void)setRefreshingModels:(BOOL)refreshingModels
{
  _refreshingModels = refreshingModels;
  [self setWorking:refreshingModels];
  [[self tableView] reloadData];
  [self refreshStatusToolbar];
}

- (void)modelCatalogRefreshDidStart:(NSNotification *)notification
{
  (void)notification;
  [self setRefreshingModels:YES];
}

- (void)modelCatalogRefreshDidFinish:(NSNotification *)notification
{
  NSDictionary *userInfo;
  NSString *errorMessage;

  userInfo = [notification userInfo];
  errorMessage = [userInfo objectForKey:@"error"];
  [self setRefreshingModels:NO];
  if ([errorMessage isKindOfClass:[NSString class]] &&
      ([errorMessage length] > 0U)) {
    [self setStatusMessage:errorMessage];
    [[self tableView] reloadData];
    [self refreshStatusToolbar];
    return;
  }
  [self reloadRows];
}

- (void)modelCatalogDidChange:(NSNotification *)notification
{
  (void)notification;
  [self reloadRows];
}

- (void)providerAccountsDidChange:(NSNotification *)notification
{
  (void)notification;
  [self reloadRows];
}

- (void)useRow:(NSDictionary *)row atIndexPath:(NSIndexPath *)indexPath
{
  NSString *modelIdentifier;
  NSError *error;
  BOOL allow;

  (void)indexPath;
  modelIdentifier = StrappyStringForModelRow(row, @"id");
  if ([modelIdentifier length] == 0U) {
    return;
  }

  error = nil;
  allow = [self allowedValueForModelRow:row] ? NO : YES;
  if (![StrappySession setModelAllowed:allow
                    forModelIdentifier:modelIdentifier
                                 error:&error]) {
    [self showError:error
              title:NSLocalizedString(@"Failed to Save Changes", nil)];
    return;
  }
  [self reloadRows];
}

#pragma mark - Account-grouped table

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  NSUInteger count;

  (void)tableView;
  count = [[self modelAccountIdentifiers] count];
  return (NSInteger)((count > 0U) ? count : 1U);
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
  (void)tableView;
  return (NSInteger)[[self modelRowsInSection:section] count];
}

- (NSString *)tableView:(UITableView *)tableView
 titleForHeaderInSection:(NSInteger)section
{
  NSArray *sectionRows;
  NSString *name;

  (void)tableView;
  sectionRows = [self modelRowsInSection:section];
  if ([sectionRows count] == 0U) {
    return nil;
  }
  name = StrappyStringForModelRow([sectionRows objectAtIndex:0U],
                                  @"provider_name");
  return ([name length] > 0U) ? name : NSLocalizedString(@"Provider", nil);
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell;
  NSDictionary *row;

  cell = [tableView dequeueReusableCellWithIdentifier:@"CatalogCell"];
  if (cell == nil) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:@"CatalogCell"];
    [[cell textLabel] setNumberOfLines:1];
    [[cell detailTextLabel] setNumberOfLines:1];
  }
  row = [self modelRowAtIndexPath:indexPath];
  [[cell textLabel] setTextColor:[UIColor blackColor]];
  [[cell detailTextLabel] setTextColor:[UIColor grayColor]];
  [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
  [self configureCell:cell withRow:row];
  return cell;
}

- (NSIndexPath *)tableView:(UITableView *)tableView
  willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  return ([self modelRowAtIndexPath:indexPath] != nil) ? indexPath : nil;
}

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *row;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  row = [self modelRowAtIndexPath:indexPath];
  if (row != nil) {
    [self useRow:row atIndexPath:indexPath];
  }
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [[self updateButton] setTarget:nil];
}

@end
