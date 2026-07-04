#import "StrappyPreferencesModelWhitelistTableViewController.h"

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

static NSComparisonResult StrappyCompareModelRows(id left, id right, void *context)
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
  return StrappyCompareStrings(StrappyStringForModelRow(leftRow, @"id"),
                               StrappyStringForModelRow(rightRow, @"id"));
}

@interface StrappyPreferencesModelWhitelistTableViewController ()
@property (nonatomic, copy) NSDictionary *pendingModelRow;
@property (nonatomic, assign) NSInteger pendingDefaultButtonIndex;
@property (nonatomic, assign) NSInteger pendingToggleButtonIndex;
@property (nonatomic, assign) BOOL refreshingModels;
@end

@implementation StrappyPreferencesModelWhitelistTableViewController

- (instancetype)init
{
  if ((self = [super initWithTitle:NSLocalizedString(@"Models", nil)])) {
    [self setPendingDefaultButtonIndex:-1];
    [self setPendingToggleButtonIndex:-1];
  }
  return self;
}

- (void)viewDidLoad
{
  [super viewDidLoad];
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
  return [rows sortedArrayUsingFunction:StrappyCompareModelRows context:NULL];
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

- (NSString *)statusText
{
  NSUInteger count;
  NSString *searchText;

  if ([self refreshingModels]) {
    return NSLocalizedString(@"Fetching models...", nil);
  }
  if ([[self statusMessage] length] > 0U) {
    return [self statusMessage];
  }

  count = [[self rows] count];
  searchText = [self currentSearchText];
  if ([searchText length] > 0U) {
    if (count == 0U) {
      return NSLocalizedString(@"No matching models.", nil);
    }
    if (count == 1U) {
      return NSLocalizedString(@"1 model shown.", nil);
    }
    return [NSString stringWithFormat:NSLocalizedString(@"%lu models shown.", nil),
      (unsigned long)count];
  }

  if (count == 0U) {
    return NSLocalizedString(@"No models have been fetched yet.", nil);
  }
  if (count == 1U) {
    return NSLocalizedString(@"1 model available.", nil);
  }
  return [NSString stringWithFormat:NSLocalizedString(@"%lu models available.", nil),
    (unsigned long)count];
}

- (NSString *)emptyText
{
  return NSLocalizedString(@"No models available.", nil);
}

- (NSString *)actionButtonAccessibilityLabel
{
  return NSLocalizedString(@"Fetch Models", nil);
}

- (void)configureCell:(UITableViewCell *)cell withRow:(NSDictionary *)row
{
  NSMutableArray *details;
  NSString *context;
  NSString *promptPrice;
  NSString *completionPrice;
  NSString *identifier;

  details = [NSMutableArray array];
  identifier = StrappyStringForModelRow(row, @"id");
  if ([self modelRowIsDefault:row]) {
    [details addObject:NSLocalizedString(@"Default", nil)];
  }
  if ([identifier length] > 0U) {
    [details addObject:identifier];
  }
  context = StrappyModelNumberString(row, @"context_length");
  if ([context length] > 0U) {
    [details addObject:[NSString stringWithFormat:
      NSLocalizedString(@"Context %@", nil), context]];
  }
  promptPrice = StrappyModelPricingString(row, @"pricing_prompt");
  completionPrice = StrappyModelPricingString(row, @"pricing_completion");
  if (([promptPrice length] > 0U) || ([completionPrice length] > 0U)) {
    [details addObject:[NSString stringWithFormat:
      NSLocalizedString(@"In %@ / Out %@", nil), promptPrice, completionPrice]];
  }

  [[cell textLabel] setText:StrappyModelDisplayNameForRow(row)];
  [[cell detailTextLabel] setText:[details componentsJoinedByString:@" | "]];
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
  [self setWorking:refreshingModels];
  [[[self navigationItem] rightBarButtonItem]
    setEnabled:refreshingModels ? NO : YES];
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
  UIActionSheet *sheet;
  BOOL allowed;

  (void)indexPath;
  [self setPendingModelRow:row];
  [self setPendingDefaultButtonIndex:-1];
  [self setPendingToggleButtonIndex:-1];

  if ([self modelRowIsDefault:row]) {
    [self showMessage:NSLocalizedString(
      @"This model is the default model and is always used.", nil)
                title:StrappyModelDisplayNameForRow(row)];
    return;
  }

  allowed = [self allowedValueForModelRow:row];
  sheet = [[UIActionSheet alloc] initWithTitle:StrappyModelDisplayNameForRow(row)
                                      delegate:self
                             cancelButtonTitle:nil
                        destructiveButtonTitle:nil
                             otherButtonTitles:nil];
  [self setPendingDefaultButtonIndex:
    [sheet addButtonWithTitle:NSLocalizedString(@"Use as Default", nil)]];
  [self setPendingToggleButtonIndex:
    [sheet addButtonWithTitle:allowed
      ? NSLocalizedString(@"Stop Using Model", nil)
      : NSLocalizedString(@"Use Model", nil)]];
  [sheet setCancelButtonIndex:
    [sheet addButtonWithTitle:NSLocalizedString(@"Cancel", nil)]];
  [sheet showInView:[self view]];
}

- (void)actionSheet:(UIActionSheet *)actionSheet
clickedButtonAtIndex:(NSInteger)buttonIndex
{
  NSString *modelIdentifier;
  NSError *error;

  (void)actionSheet;
  if ((buttonIndex == [actionSheet cancelButtonIndex]) ||
      ([self pendingModelRow] == nil)) {
    [self setPendingModelRow:nil];
    return;
  }

  modelIdentifier = StrappyStringForModelRow([self pendingModelRow], @"id");
  if ([modelIdentifier length] == 0U) {
    [self setPendingModelRow:nil];
    return;
  }

  error = nil;
  if (buttonIndex == [self pendingDefaultButtonIndex]) {
    if (![StrappySession setDefaultOpenRouterModelIdentifier:modelIdentifier
                                                       error:&error]) {
      [self showError:error
                title:NSLocalizedString(@"Could not set default model", nil)];
    } else {
      [self reloadRows];
    }
  } else if (buttonIndex == [self pendingToggleButtonIndex]) {
    BOOL allow;

    allow = [self allowedValueForModelRow:[self pendingModelRow]] ? NO : YES;
    if (![StrappySession setOpenRouterModelAllowed:allow
                                forModelIdentifier:modelIdentifier
                                             error:&error]) {
      [self showError:error
                title:NSLocalizedString(@"Could not update model", nil)];
    } else {
      [self reloadRows];
    }
  }

  [self setPendingModelRow:nil];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end
