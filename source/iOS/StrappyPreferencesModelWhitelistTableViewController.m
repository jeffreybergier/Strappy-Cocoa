#import <AICollectionController.h>
#import <AICollectionTableAdapter.h>
#import "StrappyAsync.h"
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
@property (nonatomic, strong) AICollectionController *collectionController;
@property (nonatomic, assign) BOOL hasConfiguredAccounts;
@property (nonatomic, copy) NSArray *modelSections;
- (void)loadModelRowsUsingSnapshot:(BOOL)reuse;
- (void)scheduleModelReload;
@property (nonatomic, assign) BOOL refreshingModels;
@property (nonatomic, strong) UIBarButtonItem *updateButton;
@end
@implementation StrappyPreferencesModelWhitelistTableViewController
- (void)refreshCollection
{ [[self collectionTableAdapter] refreshVisibleRows]; }

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
  _refreshingModels=[StrappySession isModelCatalogRefreshInFlight];
  [self setWorking:_refreshingModels];
  [self refreshStatusToolbar];
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
  [[self collectionController] cancel];
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
  (void)reuse;
  [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(applyRows) object:nil];
  NSArray *sort = [NSArray arrayWithObjects:
    [[NSSortDescriptor alloc] initWithKey:@"model_provider" ascending:YES],
    [[NSSortDescriptor alloc] initWithKey:@"model_allowed" ascending:NO],
    [[NSSortDescriptor alloc] initWithKey:@"model_id" ascending:YES],
    [[NSSortDescriptor alloc] initWithKey:@"model_completion_price" ascending:YES],
    [[NSSortDescriptor alloc] initWithKey:@"model_prompt_price" ascending:YES],nil];
  [self setStatusMessage:NSLocalizedString(@"Loading…",nil)];
  [self refreshStatusToolbar];
  if (![self collectionController]) [self setCollectionController:
    [[AICollectionController alloc] initWithTarget:self selector:@selector(modelRowsLoaded:)
      builder:[StrappySession class]]];
  NSDictionary *request=[NSDictionary dictionaryWithObjectsAndKeys:
    [self currentSearchText] ?: @"",@"search",sort,@"sort",nil];
  if (![[self collectionTableAdapter] snapshot])
    [self modelRowsLoaded:[[self collectionController] buildResultForRequest:request]];
  else [[self collectionController] request:request];
}

- (void)modelRowsLoaded:(NSDictionary *)result
{
  StrappyModelRows *rows = [result objectForKey:@"rows"];
  if (!rows) {
    [self setStatusMessage:[[result objectForKey:@"error"] localizedDescription]];
    [self refreshStatusToolbar];
    return;
  }
  if ([[self rows] isKindOfClass:[AIPagedRows class]]) [(AIPagedRows *)[self rows] invalidatePages];
  [self setRows:rows];
  [self setModelSections:[result objectForKey:@"sections"]];
  [self setHasConfiguredAccounts:[rows hasConfiguredAccounts]];
  [self setStatusMessage:nil];
  [[self collectionTableAdapter] applySnapshot:[result objectForKey:@"collection"]];
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
  [[self collectionTableAdapter] refreshVisibleRows];
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
    [[self collectionTableAdapter] refreshVisibleRows];
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
  BOOL allow;

  (void)indexPath;
  modelIdentifier = StrappyStringForModelRow(row, @"id");
  if ([modelIdentifier length] == 0U) {
    return;
  }

  allow = [self allowedValueForModelRow:row] ? NO : YES;
  [[StrappyAsync commandTarget:[StrappySession class] title:NSLocalizedString(@"Update model approval",nil) receiver:nil completion:NULL context:nil] setModelAllowed:allow
                    forModelIdentifier:modelIdentifier
                                 error:NULL];
  [self reloadRows];
}

#pragma mark - Provider sections

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return (NSInteger)[[self modelSections] count];
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
  return [[[self modelRowAtIndexPath:indexPath] objectForKey:@"id"] length] ? indexPath : nil;
}

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *row;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  row = [self modelRowAtIndexPath:indexPath];
  if ([[row objectForKey:@"id"] length]) {
    [self useRow:row atIndexPath:indexPath];
  }
}

- (void)dealloc
{
  [_collectionController invalidate];
  if ([[self rows] isKindOfClass:[AIPagedRows class]]) [(AIPagedRows *)[self rows] invalidatePages];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [[self updateButton] setTarget:nil];
}

@end
