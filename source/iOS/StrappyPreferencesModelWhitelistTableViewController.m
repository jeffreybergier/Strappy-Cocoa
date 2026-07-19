#import "StrappyPreferencesModelWhitelistTableViewController.h"

#import "AIFontAwesome.h"
#import "StrappySession.h"

static NSString * const kStrappyModelSearchTextKey =
  @"_strappy_model_search_text";
static const CGFloat kStrappyModelDefaultIconCanvasSize = 24.0f;
static const CGFloat kStrappyModelDefaultIconSize = 20.0f;

static UIImage *StrappyModelDefaultIconImage(void)
{
  static UIImage *image = nil;

  if (image == nil) {
    image = [AIFontAwesome imageForIcon:AIFACircleCheck
                               style:AIFontAwesomeStyleSolid
                            iconSize:kStrappyModelDefaultIconSize
                          canvasSize:kStrappyModelDefaultIconCanvasSize
                                  color:[UIColor blackColor]
                                  scale:0.0f];
  }
  return image;
}

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

static NSComparisonResult StrappyCompareModelNameRows(id left,
                                                      id right,
                                                      void *context)
{
  NSDictionary *leftRow;
  NSDictionary *rightRow;
  NSComparisonResult result;

  (void)context;
  leftRow = [left isKindOfClass:[NSDictionary class]] ? left : nil;
  rightRow = [right isKindOfClass:[NSDictionary class]] ? right : nil;
  result = StrappyCompareStrings(StrappyModelDisplayNameForRow(leftRow),
                                 StrappyModelDisplayNameForRow(rightRow));
  if (result != NSOrderedSame) {
    return result;
  }
  return StrappyCompareStrings(StrappyStringForModelRow(leftRow, @"id"),
                               StrappyStringForModelRow(rightRow, @"id"));
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

@class StrappyPreferencesDefaultModelTableViewController;

@interface StrappyPreferencesModelWhitelistTableViewController ()
@property (nonatomic, strong) UINavigationController *defaultModelNavigationController;
@property (nonatomic, strong) StrappyPreferencesDefaultModelTableViewController *defaultModelController;
@property (nonatomic, assign) BOOL refreshingModels;
- (void)defaultModelButtonPressed:(id)sender;
- (BOOL)setDefaultModelIdentifierFromDefaultPicker:(NSString *)modelIdentifier;
- (void)dismissDefaultModelControllerAnimated:(BOOL)animated;
@end

@interface StrappyPreferencesDefaultModelTableViewController : UITableViewController
@property (nonatomic, assign)
  StrappyPreferencesModelWhitelistTableViewController *modelWhitelistViewController;
@property (nonatomic, copy) NSArray *models;
@property (nonatomic, copy) NSString *defaultModelIdentifier;
- (instancetype)initWithModelWhitelistViewController:
    (StrappyPreferencesModelWhitelistTableViewController *)viewController;
- (void)reloadModels;
@end

@implementation StrappyPreferencesDefaultModelTableViewController

- (instancetype)initWithModelWhitelistViewController:
    (StrappyPreferencesModelWhitelistTableViewController *)viewController
{
  if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
    [self setModelWhitelistViewController:viewController];
    [self setModels:[NSArray array]];
    [self setDefaultModelIdentifier:@""];
    [[self navigationItem] setTitle:NSLocalizedString(@"Default Model", nil)];
  }
  return self;
}

- (void)viewDidLoad
{
  [super viewDidLoad];

  [[self navigationItem] setRightBarButtonItem:
    [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                  target:self
                                                  action:@selector(doneAction:)]];
  [self reloadModels];
}

- (void)reloadModels
{
  NSError *error;
  NSArray *models;
  NSString *defaultModelIdentifier;

  error = nil;
  models = [StrappySession allowedOpenRouterModelCatalogWithError:&error];
  if (![models isKindOfClass:[NSArray class]]) {
    models = [NSArray array];
    if (error != nil) {
      [[self modelWhitelistViewController] showError:error
        title:NSLocalizedString(@"Could not load models", nil)];
    }
  }

  error = nil;
  defaultModelIdentifier =
    [StrappySession defaultOpenRouterModelIdentifierWithError:&error];
  if (![defaultModelIdentifier isKindOfClass:[NSString class]]) {
    defaultModelIdentifier = @"";
    if (error != nil) {
      [[self modelWhitelistViewController] showError:error
        title:NSLocalizedString(@"Could not load default model", nil)];
    }
  }

  [self setModels:
    [models sortedArrayUsingFunction:StrappyCompareModelNameRows context:NULL]];
  [self setDefaultModelIdentifier:defaultModelIdentifier];
  [[self tableView] reloadData];
}

- (void)doneAction:(id)sender
{
  (void)sender;
  [[self modelWhitelistViewController] dismissDefaultModelControllerAnimated:YES];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return 1;
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
  (void)tableView;
  if (section != 0) {
    return 0;
  }
  return (NSInteger)[[self models] count];
}

- (NSString *)tableView:(UITableView *)tableView
titleForHeaderInSection:(NSInteger)section
{
  (void)tableView;
  if ([[self models] count] == 0U) {
    return nil;
  }
  return (section == 0) ? NSLocalizedString(@"Models", nil) : nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell;

  cell = [tableView dequeueReusableCellWithIdentifier:@"DefaultModelCell"];
  if (cell == nil) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:@"DefaultModelCell"];
    [[cell textLabel] setNumberOfLines:1];
    [[cell detailTextLabel] setNumberOfLines:1];
  }

  {
    NSDictionary *model;
    NSString *identifier;

    model = [[self models] objectAtIndex:(NSUInteger)[indexPath row]];
    identifier = StrappyStringForModelRow(model, @"id");
    [[cell textLabel] setText:StrappyModelDisplayNameForRow(model)];
    [[cell detailTextLabel] setText:identifier];
    [[cell textLabel] setTextColor:[UIColor blackColor]];
    [[cell detailTextLabel] setTextColor:[UIColor grayColor]];
    [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
    [cell setAccessoryType:
      [identifier isEqualToString:[self defaultModelIdentifier]]
        ? UITableViewCellAccessoryCheckmark
        : UITableViewCellAccessoryNone];
  }
  return cell;
}

- (NSIndexPath *)tableView:(UITableView *)tableView
  willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  return ([[self models] count] > 0U) ? indexPath : nil;
}

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *model;
  NSString *modelIdentifier;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  if ([[self models] count] == 0U) {
    return;
  }

  model = [[self models] objectAtIndex:(NSUInteger)[indexPath row]];
  modelIdentifier = StrappyStringForModelRow(model, @"id");
  if ([modelIdentifier length] == 0U) {
    return;
  }

  if ([[self modelWhitelistViewController]
        setDefaultModelIdentifierFromDefaultPicker:modelIdentifier]) {
    [self setDefaultModelIdentifier:modelIdentifier];
    [[self tableView] reloadSections:[NSIndexSet indexSetWithIndex:0]
                     withRowAnimation:UITableViewRowAnimationNone];
  } else {
    [self reloadModels];
  }
}

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
  [super viewDidLoad];

  [[self navigationItem] setRightBarButtonItem:
    [[UIBarButtonItem alloc] initWithTitle:NSLocalizedString(@"Default", nil)
                                     style:UIBarButtonItemStyleBordered
                                    target:self
                                    action:@selector(defaultModelButtonPressed:)]];

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
  BOOL defaultModel;

  details = [NSMutableArray array];
  defaultModel = [self modelRowIsDefault:row];
  context = StrappyModelNumberString(row, @"context_length");
  if ([context length] > 0U) {
    [details addObject:[NSString stringWithFormat:
      NSLocalizedString(@"Context: %@", nil), context]];
  }
  promptPrice = StrappyModelPricingString(row, @"pricing_prompt");
  completionPrice = StrappyModelPricingString(row, @"pricing_completion");
  if ([promptPrice length] > 0U) {
    [details addObject:[NSString stringWithFormat:
      NSLocalizedString(@"In: %@", nil), promptPrice]];
  }
  if ([completionPrice length] > 0U) {
    [details addObject:[NSString stringWithFormat:
      NSLocalizedString(@"Out: %@", nil), completionPrice]];
  }

  [[cell textLabel] setText:StrappyModelDisplayNameForRow(row)];
  [[cell detailTextLabel] setText:[details componentsJoinedByString:@", "]];
  [[cell imageView] setContentMode:UIViewContentModeCenter];
  [[cell imageView] setImage:defaultModel ? StrappyModelDefaultIconImage() : nil];
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
  [[self defaultModelController] reloadModels];
}

- (void)defaultModelButtonPressed:(id)sender
{
  StrappyPreferencesDefaultModelTableViewController *controller;
  UINavigationController *navigationController;

  (void)sender;
  if ([self defaultModelNavigationController] != nil) {
    return;
  }

  [[self searchBar] resignFirstResponder];
  controller =
    [[StrappyPreferencesDefaultModelTableViewController alloc]
      initWithModelWhitelistViewController:self];
  navigationController =
    [[UINavigationController alloc] initWithRootViewController:controller];
  [self setDefaultModelController:controller];
  [self setDefaultModelNavigationController:navigationController];
  [self presentModalViewController:navigationController animated:YES];
}

- (BOOL)setDefaultModelIdentifierFromDefaultPicker:(NSString *)modelIdentifier
{
  NSError *error;

  error = nil;
  if (![StrappySession setDefaultOpenRouterModelIdentifier:modelIdentifier
                                                     error:&error]) {
    [self showError:error
              title:NSLocalizedString(@"Failed to Save Changes", nil)];
    return NO;
  }

  [self reloadRows];
  return YES;
}

- (void)dismissDefaultModelControllerAnimated:(BOOL)animated
{
  UINavigationController *navigationController;

  navigationController = [self defaultModelNavigationController];
  [self setDefaultModelController:nil];
  [self setDefaultModelNavigationController:nil];
  if (navigationController != nil) {
    [navigationController dismissModalViewControllerAnimated:animated];
  }
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
}

@end
