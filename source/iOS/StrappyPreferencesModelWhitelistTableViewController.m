#import "StrappyPreferencesModelWhitelistTableViewController.h"

#import "StrappyAppearance.h"
#import "StrappyModelCellFormatter.h"
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
  return ([name length] > 0U) ? name : StrappyStringForModelRow(row, @"id");
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
  result = StrappyCompareBooleans(StrappyModelRowIsAllowed(leftRow),
                                  StrappyModelRowIsAllowed(rightRow));
  if (result != NSOrderedSame) {
    return result;
  }
  result = StrappyCompareStrings(StrappyStringForModelRow(leftRow, @"id"),
                                 StrappyStringForModelRow(rightRow, @"id"));
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

@interface StrappyPreferencesModelWhitelistTableViewController ()
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
    initWithTitle:NSLocalizedString(@"Update", nil)
            style:UIBarButtonItemStyleBordered
           target:self
           action:@selector(actionButtonPressed:)];
  [updateButton
    setAccessibilityLabel:NSLocalizedString(@"Update Models", nil)];
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
  [self setRefreshingModels:[StrappySession isModelCatalogRefreshInFlight]];
}

- (NSArray *)loadAllRowsWithError:(NSError **)error
{
  return [StrappySession openRouterModelCatalogWithError:error];
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
  NSError *error;

  (void)sender;
  if ([self refreshingModels]) {
    return;
  }

  error = nil;
  if (![StrappySession beginOpenRouterModelCatalogRefreshWithError:&error]) {
    [self showError:error
              title:NSLocalizedString(@"Could not fetch models", nil)];
    return;
  }
  [self setRefreshingModels:YES];
}

- (void)setRefreshingModels:(BOOL)refreshingModels
{
  _refreshingModels = refreshingModels;
  [[self updateButton] setEnabled:refreshingModels ? NO : YES];
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
  if (![StrappySession setOpenRouterModelAllowed:allow
                              forModelIdentifier:modelIdentifier
                                           error:&error]) {
    [self showError:error
              title:NSLocalizedString(@"Failed to Save Changes", nil)];
    return;
  }
  [self reloadRows];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [[self updateButton] setTarget:nil];
}

@end
