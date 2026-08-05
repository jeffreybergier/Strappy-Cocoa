#import "StrappyPreferencesWhitelistTableViewController.h"

#import "AIFontAwesome.h"
#import "StrappyIdleTimerAssertion.h"
#import "StrappyPreferencesStatusToolbarView.h"
#import "XPUIKit.h"

static NSString *StrappyPreferencesTrimmedString(NSString *string)
{
  if (![string isKindOfClass:[NSString class]]) {
    return @"";
  }
  return [string stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

@interface StrappyPreferencesWhitelistTableViewController ()
@property (nonatomic, strong)
  StrappyPreferencesStatusToolbarView *statusToolbarView;
@property (nonatomic, strong) UIBarButtonItem *statusToolbarItem;
- (void)layoutToolbarContentView;
@end

@implementation StrappyPreferencesWhitelistTableViewController

- (instancetype)initWithTitle:(NSString *)title
{
  if ((self = [super initWithStyle:UITableViewStylePlain])) {
    [[self navigationItem] setTitle:title];
    [self setAllRows:[NSArray array]];
    [self setRows:[NSArray array]];
  }
  return self;
}

- (void)viewDidLoad
{
  [super viewDidLoad];

  [self setSearchBar:[[UISearchBar alloc] initWithFrame:
    CGRectMake(0.0f, 0.0f, CGRectGetWidth([[self tableView] bounds]), 44.0f)]];
  [[self searchBar] setDelegate:self];
  [[self searchBar] setAutoresizingMask:UIViewAutoresizingFlexibleWidth];
  [[self searchBar] setPlaceholder:NSLocalizedString(@"Search", nil)];
  [[self searchBar] XP_enableSearchReturnKeyWhenEmpty];
  [[self tableView] setTableHeaderView:[self searchBar]];
  [[self tableView] XP_setKeyboardDismissModeOnDrag];

  [self buildStatusToolbar];
  [self reloadRows];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [[self navigationController] setToolbarHidden:NO animated:animated];
  [self refreshStatusToolbar];
}

- (void)viewDidAppear:(BOOL)animated
{
  [super viewDidAppear:animated];
  [self refreshStatusToolbar];
}

- (void)viewDidLayoutSubviews
{
  [super viewDidLayoutSubviews];
  [self layoutToolbarContentView];
}

- (void)willAnimateRotationToInterfaceOrientation:(UIInterfaceOrientation)toInterfaceOrientation
                                        duration:(NSTimeInterval)duration
{
  [super willAnimateRotationToInterfaceOrientation:toInterfaceOrientation
                                          duration:duration];
  [self layoutToolbarContentView];
}

- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)fromInterfaceOrientation
{
  [super didRotateFromInterfaceOrientation:fromInterfaceOrientation];
  [self layoutToolbarContentView];
}

- (void)viewWillDisappear:(BOOL)animated
{
  [super viewWillDisappear:animated];
  [[self navigationController] setToolbarHidden:YES animated:animated];
}

- (void)setWorking:(BOOL)working
{
  if (_working == working) {
    return;
  }

  _working = working;
  StrappyIdleTimerAssertionSetEnabled(working);
  [self refreshStatusToolbar];
}

- (void)buildStatusToolbar
{
  StrappyPreferencesStatusToolbarView *toolbarView;
  UIBarButtonItem *toolbarItem;

  toolbarView = [[StrappyPreferencesStatusToolbarView alloc]
    initWithActionIcon:AIFAArrowsRotate
                target:self
                action:@selector(actionButtonPressed:)];
  [toolbarView
    setActionAccessibilityLabel:[self actionButtonAccessibilityLabel]];
  toolbarItem = [[UIBarButtonItem alloc] initWithCustomView:toolbarView];

  [self setStatusToolbarView:toolbarView];
  [self setStatusToolbarItem:toolbarItem];
  [self setToolbarItems:[NSArray arrayWithObject:toolbarItem]];
  [self refreshStatusToolbar];
}

- (void)refreshStatusToolbar
{
  [[self statusToolbarView] setText:[self statusText]];
  [[self statusToolbarView] setWorking:[self working]];
  [self layoutToolbarContentView];
}

- (void)layoutToolbarContentView
{
  UIToolbar *toolbar;

  if ([self statusToolbarView] == nil) {
    return;
  }

  toolbar = [[self navigationController] toolbar];
  [[self statusToolbarView]
    layoutForToolbar:toolbar
      containingItem:[self statusToolbarItem]
       fallbackWidth:CGRectGetWidth([[self view] bounds])];
}

- (NSArray *)loadAllRowsWithError:(NSError **)error
{
  (void)error;
  return [NSArray array];
}

- (NSArray *)preparedRowsForRows:(NSArray *)rows
{
  return [rows isKindOfClass:[NSArray class]] ? rows : [NSArray array];
}

- (NSArray *)sortedRows:(NSArray *)rows
{
  return [rows isKindOfClass:[NSArray class]] ? rows : [NSArray array];
}

- (BOOL)row:(NSDictionary *)row matchesSearchText:(NSString *)searchText
{
  (void)row;
  return ([searchText length] == 0U) ? YES : NO;
}

- (BOOL)rowIsSelected:(NSDictionary *)row
{
  (void)row;
  return NO;
}

- (NSString *)currentSearchText
{
  return StrappyPreferencesTrimmedString([[self searchBar] text]);
}

- (NSString *)workingStatusText
{
  return nil;
}

- (NSString *)statusText
{
  NSUInteger index;
  NSUInteger selectedCount;
  NSUInteger totalCount;
  NSString *workingText;

  workingText = [self workingStatusText];
  if ([workingText length] > 0U) {
    return workingText;
  }

  if ([[self statusMessage] length] > 0U) {
    return [self statusMessage];
  }

  selectedCount = 0U;
  totalCount = [[self allRows] count];
  for (index = 0U; index < totalCount; index++) {
    NSDictionary *row;

    row = [[self allRows] objectAtIndex:index];
    if ([row isKindOfClass:[NSDictionary class]] && [self rowIsSelected:row]) {
      selectedCount++;
    }
  }
  return [NSString stringWithFormat:NSLocalizedString(@"%lu of %lu", nil),
    (unsigned long)selectedCount, (unsigned long)totalCount];
}

- (NSString *)actionButtonAccessibilityLabel
{
  return NSLocalizedString(@"Refresh", nil);
}

- (void)actionButtonPressed:(id)sender
{
  (void)sender;
  [self reloadRows];
}

- (void)reloadRows
{
  NSError *error;
  NSArray *loadedRows;

  error = nil;
  loadedRows = [self loadAllRowsWithError:&error];
  if (loadedRows == nil) {
    [self setAllRows:[NSArray array]];
    [self setRows:[NSArray array]];
    [self setStatusMessage:[error localizedDescription]];
    if ([[self statusMessage] length] == 0U) {
      [self setStatusMessage:NSLocalizedString(@"Rows could not be loaded.", nil)];
    }
    [[self tableView] reloadData];
    [self refreshStatusToolbar];
    return;
  }

  [self setStatusMessage:nil];
  [self setAllRows:[self sortedRows:[self preparedRowsForRows:loadedRows]]];
  [self applyRows];
}

- (void)applyRows
{
  NSMutableArray *matchingRows;
  NSString *searchText;
  NSUInteger index;

  searchText = [self currentSearchText];
  matchingRows = [NSMutableArray arrayWithCapacity:[[self allRows] count]];
  for (index = 0U; index < [[self allRows] count]; index++) {
    NSDictionary *row;

    row = [[self allRows] objectAtIndex:index];
    if (![row isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    if ([self row:row matchesSearchText:searchText]) {
      [matchingRows addObject:row];
    }
  }

  [self setRows:[self sortedRows:matchingRows]];
  [[self tableView] reloadData];
  [self refreshStatusToolbar];
}

- (void)configureCell:(UITableViewCell *)cell withRow:(NSDictionary *)row
{
  (void)row;
  [[cell textLabel] setText:@""];
  [[cell detailTextLabel] setText:@""];
}

- (void)useRow:(NSDictionary *)row atIndexPath:(NSIndexPath *)indexPath
{
  (void)row;
  (void)indexPath;
}

- (void)showError:(NSError *)error title:(NSString *)title
{
  NSString *message;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = NSLocalizedString(@"An unknown error occurred.", nil);
  }
  [self showMessage:message title:title];
}

- (void)showMessage:(NSString *)message title:(NSString *)title
{
  UIAlertView *alert;

  alert = [[UIAlertView alloc] initWithTitle:title
                                     message:message
                                    delegate:nil
                           cancelButtonTitle:NSLocalizedString(@"OK", nil)
                           otherButtonTitles:nil];
  [alert show];
}

#pragma mark - UISearchBarDelegate

- (void)searchBarTextDidBeginEditing:(UISearchBar *)searchBar
{
  [searchBar XP_enableSearchReturnKeyWhenEmpty];
}

- (void)searchBar:(UISearchBar *)searchBar textDidChange:(NSString *)searchText
{
  (void)searchBar;
  (void)searchText;
  [self applyRows];
}

- (void)searchBarSearchButtonClicked:(UISearchBar *)searchBar
{
  [searchBar resignFirstResponder];
}

#pragma mark - UITableViewDataSource

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
  return (NSInteger)[[self rows] count];
}

- (NSString *)tableView:(UITableView *)tableView
titleForFooterInSection:(NSInteger)section
{
  (void)tableView;
  (void)section;
  return nil;
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

  row = [[self rows] objectAtIndex:(NSUInteger)[indexPath row]];
  [[cell textLabel] setTextColor:[UIColor blackColor]];
  [[cell detailTextLabel] setTextColor:[UIColor grayColor]];
  [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
  [self configureCell:cell withRow:row];
  return cell;
}

#pragma mark - UITableViewDelegate

- (NSIndexPath *)tableView:(UITableView *)tableView
  willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  return ([[self rows] count] > 0U) ? indexPath : nil;
}

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *row;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  if ([[self rows] count] == 0U) {
    return;
  }

  row = [[self rows] objectAtIndex:(NSUInteger)[indexPath row]];
  [self useRow:row atIndexPath:indexPath];
}

- (void)dealloc
{
  [self setWorking:NO];
  [[self searchBar] setDelegate:nil];
}

@end
