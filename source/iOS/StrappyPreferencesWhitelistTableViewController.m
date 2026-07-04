#import "StrappyPreferencesWhitelistTableViewController.h"

#import "XPUIKit.h"

static NSString *StrappyPreferencesTrimmedString(NSString *string)
{
  if (![string isKindOfClass:[NSString class]]) {
    return @"";
  }
  return [string stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

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
  [[self tableView] setTableHeaderView:[self searchBar]];

  [[self navigationItem] setRightBarButtonItem:
    [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemRefresh
                                                  target:self
                                                  action:@selector(actionButtonPressed:)]];
  [[[self navigationItem] rightBarButtonItem]
    setAccessibilityLabel:[self actionButtonAccessibilityLabel]];

  [self buildStatusToolbar];
  [self reloadRows];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [self refreshStatusToolbar];
  [[self navigationController] setToolbarHidden:NO animated:animated];
}

- (void)viewWillDisappear:(BOOL)animated
{
  [super viewWillDisappear:animated];
  [[self navigationController] setToolbarHidden:YES animated:animated];
}

- (void)buildStatusToolbar
{
  UIBarButtonItem *flexLeft;
  UIBarButtonItem *flexRight;
  UIBarButtonItem *statusItem;
  UILabel *label;
  CGFloat width;

  width = CGRectGetWidth([[self view] bounds]) - 80.0f;
  if (width < 160.0f) {
    width = 160.0f;
  }

  label = [[UILabel alloc] initWithFrame:CGRectMake(0.0f, 0.0f, width, 30.0f)];
  [label setBackgroundColor:[UIColor clearColor]];
  [label setTextColor:[UIColor whiteColor]];
  [label setShadowColor:[UIColor darkGrayColor]];
  [label setShadowOffset:CGSizeMake(0.0f, -1.0f)];
  [label setFont:[UIFont systemFontOfSize:12.0f]];
  [label setNumberOfLines:1];
  [label XP_setTextAlignmentCenter];
  [label setAutoresizingMask:UIViewAutoresizingFlexibleWidth];
  [self setStatusLabel:label];

  statusItem = [[UIBarButtonItem alloc] initWithCustomView:label];
  flexLeft = [[UIBarButtonItem alloc]
    initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                         target:nil
                         action:NULL];
  flexRight = [[UIBarButtonItem alloc]
    initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                         target:nil
                         action:NULL];
  [self setToolbarItems:
    [NSArray arrayWithObjects:flexLeft, statusItem, flexRight, nil]];
  [self refreshStatusToolbar];
}

- (void)refreshStatusToolbar
{
  [[self statusLabel] setText:[self statusText]];
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

- (NSString *)currentSearchText
{
  return StrappyPreferencesTrimmedString([[self searchBar] text]);
}

- (NSString *)statusText
{
  NSUInteger count;
  NSString *searchText;

  if ([[self statusMessage] length] > 0U) {
    return [self statusMessage];
  }

  count = [[self rows] count];
  searchText = [self currentSearchText];
  if ([searchText length] > 0U) {
    if (count == 0U) {
      return NSLocalizedString(@"No matching rows.", nil);
    }
    if (count == 1U) {
      return NSLocalizedString(@"1 row shown.", nil);
    }
    return [NSString stringWithFormat:NSLocalizedString(@"%lu rows shown.", nil),
      (unsigned long)count];
  }

  if (count == 0U) {
    return [self emptyText];
  }
  if (count == 1U) {
    return NSLocalizedString(@"1 row available.", nil);
  }
  return [NSString stringWithFormat:NSLocalizedString(@"%lu rows available.", nil),
    (unsigned long)count];
}

- (NSString *)emptyText
{
  return NSLocalizedString(@"No rows available.", nil);
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
  return ([[self rows] count] > 0U) ? (NSInteger)[[self rows] count] : 1;
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

  if ([[self rows] count] == 0U) {
    [[cell textLabel] setText:[self emptyText]];
    [[cell detailTextLabel] setText:nil];
    [cell setAccessoryType:UITableViewCellAccessoryNone];
    [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
    [[cell textLabel] setTextColor:[UIColor grayColor]];
    [[cell detailTextLabel] setTextColor:[UIColor grayColor]];
    return cell;
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
  [[self searchBar] setDelegate:nil];
}

@end
