#import "StrappyPreferencesModelWhitelistTableViewController.h"

#import "StrappyAppearance.h"
#import "StrappyModelCellFormatter.h"
#import "StrappyModelProvidersTableViewController.h"
#import "StrappySession.h"

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

@interface StrappyPreferencesModelWhitelistTableViewController ()
@property (nonatomic, assign) BOOL hasConfiguredAccounts;
@property (nonatomic, copy) NSArray *modelSections;
- (void)loadModelRowsUsingSnapshot:(BOOL)reuse;
- (void)scheduleModelReload;
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
  [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(modelListReadFailed:)
    name:StrappyModelListReadFailedNotification object:nil];
  [self setRefreshingModels:[StrappySession isModelCatalogRefreshInFlight]];
}

- (void)reloadRows
{
  [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(reloadRows) object:nil];
  [self loadModelRowsUsingSnapshot:NO];
}

- (void)applyRows
{
  [self loadModelRowsUsingSnapshot:YES];
}

- (void)scheduleModelReload
{
  [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(reloadRows) object:nil];
  [self performSelector:@selector(reloadRows) withObject:nil afterDelay:0.0];
}

- (void)searchBar:(UISearchBar *)searchBar textDidChange:(NSString *)searchText
{
  (void)searchBar; (void)searchText;
  [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(applyRows) object:nil];
  [self performSelector:@selector(applyRows) withObject:nil afterDelay:0.15];
}

- (void)searchBarSearchButtonClicked:(UISearchBar *)searchBar
{
  [self applyRows];
  [searchBar resignFirstResponder];
}

- (void)loadModelRowsUsingSnapshot:(BOOL)reuse
{
  NSError *error = nil;
  StrappyModelRows *rows = nil;
  NSArray *sections;
  NSArray *sort = [NSArray arrayWithObjects:
    [[NSSortDescriptor alloc] initWithKey:@"model_provider" ascending:YES],
    [[NSSortDescriptor alloc] initWithKey:@"model_allowed" ascending:NO],
    [[NSSortDescriptor alloc] initWithKey:@"model_id" ascending:YES],
    [[NSSortDescriptor alloc] initWithKey:@"model_completion_price" ascending:YES],
    [[NSSortDescriptor alloc] initWithKey:@"model_prompt_price" ascending:YES],nil];
  [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(applyRows) object:nil];
  if (reuse && [[self rows] isKindOfClass:[StrappyModelRows class]]) {
    rows = (StrappyModelRows *)[self rows];
    if (![rows filterWithSearch:[self currentSearchText] sortDescriptors:sort error:&error]) rows = nil;
  } else {
    rows = [StrappySession modelPreferenceRowsMatchingSearch:[self currentSearchText]
      sortDescriptors:sort error:&error];
  }
  sections = rows != nil ? [rows providerSectionsWithError:&error] : nil;
  if (sections == nil) {
    [self setRows:[NSArray array]];
    [self setModelSections:[NSArray array]];
    [self setStatusMessage:[error localizedDescription]];
  } else {
    [self setRows:rows];
    [self setModelSections:sections];
    [self setHasConfiguredAccounts:[rows hasConfiguredAccounts]];
    [self setStatusMessage:nil];
  }
  [[self tableView] reloadData];
  [self refreshStatusToolbar];
}

- (void)modelListReadFailed:(NSNotification *)notification
{
  if ([notification object] != [self rows]) return;
  [self setStatusMessage:[[[notification userInfo] objectForKey:@"error"] localizedDescription]];
  [self refreshStatusToolbar];
}

- (NSDictionary *)modelSectionAtIndex:(NSInteger)section
{
  if (section < 0 || (NSUInteger)section >= [[self modelSections] count]) return nil;
  return [[self modelSections] objectAtIndex:(NSUInteger)section];
}

- (NSDictionary *)modelRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *section = [self modelSectionAtIndex:[indexPath section]];
  NSUInteger count = [[section objectForKey:@"count"] unsignedIntegerValue];
  NSUInteger offset = [[section objectForKey:@"offset"] unsignedIntegerValue];
  if ([indexPath row] < 0 || (NSUInteger)[indexPath row] >= count) return nil;
  return [[self rows] objectAtIndex:offset + (NSUInteger)[indexPath row]];
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

- (NSUInteger)totalRowCount
{
  return [[self rows] isKindOfClass:[StrappyModelRows class]] ? [(StrappyModelRows *)[self rows] totalCount] : 0U;
}

- (NSUInteger)selectedRowCount
{
  return [[self rows] isKindOfClass:[StrappyModelRows class]] ? [(StrappyModelRows *)[self rows] allowedCount] : 0U;
}

- (NSString *)statusText
{
  if (![self working] && ([[self statusMessage] length] == 0U) &&
      ([[self currentSearchText] length] == 0U) &&
      ([[self rows] count] == 0U)) {
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
  [self reloadRows];
  [self setRefreshingModels:NO];
  if ([errorMessage isKindOfClass:[NSString class]] &&
      ([errorMessage length] > 0U)) {
    [self setStatusMessage:errorMessage];
    [[self tableView] reloadData];
    [self refreshStatusToolbar];
    return;
  }
}

- (void)modelCatalogDidChange:(NSNotification *)notification
{
  (void)notification;
  [self scheduleModelReload];
}

- (void)providerAccountsDidChange:(NSNotification *)notification
{
  (void)notification;
  [self scheduleModelReload];
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

#pragma mark - Provider sections

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return (NSInteger)MAX([[self modelSections] count],1U);
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
  (void)tableView;
  return (NSInteger)[[[self modelSectionAtIndex:section] objectForKey:@"count"] unsignedIntegerValue];
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
  (void)tableView;
  return [[self modelSectionAtIndex:section] objectForKey:@"title"];
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
