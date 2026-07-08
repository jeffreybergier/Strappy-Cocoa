#import "StrappyPreferencesWhitelistTableViewController.h"

#import "AIFontAwesome.h"
#import "StrappyIdleTimerAssertion.h"
#import "XPUIKit.h"

static NSString *StrappyPreferencesTrimmedString(NSString *string)
{
  if (![string isKindOfClass:[NSString class]]) {
    return @"";
  }
  return [string stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

static const CGFloat kStrappyPreferencesToolbarSideItemWidth = 44.0f;
static const CGFloat kStrappyPreferencesToolbarLabelHeight = 30.0f;
static const CGFloat kStrappyPreferencesToolbarFallbackHeight = 44.0f;

@interface StrappyPreferencesWhitelistTableViewController ()
@property (nonatomic, strong) UIView *toolbarContentView;
@property (nonatomic, strong) UIBarButtonItem *toolbarContentItem;
@property (nonatomic, strong) UIButton *actionToolbarButton;
@property (nonatomic, strong) UIActivityIndicatorView *activityIndicatorView;
@property (nonatomic, strong) UIImage *actionToolbarImage;
- (void)layoutToolbarContentView;
- (void)refreshToolbarItems;
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
  [[self tableView] setTableHeaderView:[self searchBar]];

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
  UIView *contentView;
  UIBarButtonItem *contentItem;
  UIButton *actionButton;
  UIActivityIndicatorView *activityView;
  UILabel *label;
  UIImage *actionImage;
  CGFloat width;
  CGFloat height;

  width = CGRectGetWidth([[self view] bounds]);
  if (width <= 0.0f) {
    width = 320.0f;
  }
  height = kStrappyPreferencesToolbarFallbackHeight;

  contentView = [[UIView alloc] initWithFrame:
    CGRectMake(0.0f, 0.0f, width, height)];
  [contentView setBackgroundColor:[UIColor clearColor]];
  [contentView setOpaque:NO];
  [contentView setAutoresizingMask:UIViewAutoresizingFlexibleWidth];

  label = [[UILabel alloc] initWithFrame:CGRectZero];
  [label setBackgroundColor:[UIColor clearColor]];
  [label setTextColor:[UIColor whiteColor]];
  [label setShadowColor:[UIColor colorWithWhite:0.0f alpha:0.5f]];
  [label setShadowOffset:CGSizeMake(0.0f, -1.0f)];
  [label setFont:[UIFont boldSystemFontOfSize:15.0f]];
  [label setNumberOfLines:1];
  [label XP_setTextAlignmentCenter];
  [label setAutoresizingMask:UIViewAutoresizingFlexibleWidth];
  [contentView addSubview:label];
  [self setStatusLabel:label];

  actionImage = [AIFontAwesome imageForIcon:AIFAArrowsRotate
                                      style:AIFontAwesomeStyleSolid
                                   iconSize:18.0f
                                 canvasSize:kStrappyPreferencesToolbarSideItemWidth
                                      color:[UIColor whiteColor]
                                      scale:0.0f];
  actionButton = [UIButton buttonWithType:UIButtonTypeCustom];
  [actionButton setImage:actionImage forState:UIControlStateNormal];
  [actionButton setShowsTouchWhenHighlighted:YES];
  [actionButton setAccessibilityLabel:[self actionButtonAccessibilityLabel]];
  [actionButton addTarget:self
                   action:@selector(actionButtonPressed:)
         forControlEvents:UIControlEventTouchUpInside];
  [contentView addSubview:actionButton];

  activityView = [[UIActivityIndicatorView alloc]
    initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleWhite];
  [activityView setHidesWhenStopped:YES];
  [contentView addSubview:activityView];

  contentItem = [[UIBarButtonItem alloc] initWithCustomView:contentView];
  [contentItem setWidth:width];

  [self setToolbarContentView:contentView];
  [self setToolbarContentItem:contentItem];
  [self setActionToolbarButton:actionButton];
  [self setActionToolbarImage:actionImage];
  [self setActivityIndicatorView:activityView];
  [self setToolbarItems:[NSArray arrayWithObject:contentItem]];
  [self refreshStatusToolbar];
}

- (void)refreshStatusToolbar
{
  [[self statusLabel] setText:[self statusText]];
  [self layoutToolbarContentView];
  [self refreshToolbarItems];
}

- (void)layoutToolbarContentView
{
  UIToolbar *toolbar;
  UIView *contentView;
  CGRect frame;
  CGFloat width;
  CGFloat contentWidth;
  CGFloat contentX;
  CGFloat height;
  CGFloat sideWidth;
  CGFloat labelX;
  CGFloat labelHeight;
  CGFloat labelWidth;
  CGFloat buttonX;
  CGFloat labelY;

  contentView = [self toolbarContentView];
  if (contentView == nil) {
    return;
  }

  toolbar = [[self navigationController] toolbar];
  width = CGRectGetWidth([toolbar bounds]);
  if (width <= 0.0f) {
    width = CGRectGetWidth([[self view] bounds]);
  }
  if (width <= 0.0f) {
    width = CGRectGetWidth([contentView bounds]);
  }
  if (width <= 0.0f) {
    width = 320.0f;
  }

  height = CGRectGetHeight([toolbar bounds]);
  if (height <= 0.0f) {
    height = kStrappyPreferencesToolbarFallbackHeight;
  }

  frame = [contentView frame];
  contentX = CGRectGetMinX(frame);
  if ((contentX < 0.0f) || (contentX >= width)) {
    contentX = 0.0f;
  }
  contentWidth = width - contentX;
  if (contentWidth <= 0.0f) {
    contentWidth = width;
  }

  frame.size.width = contentWidth;
  frame.size.height = height;
  [contentView setFrame:frame];
  [[self toolbarContentItem] setWidth:contentWidth];

  sideWidth = kStrappyPreferencesToolbarSideItemWidth;
  if ((sideWidth * 2.0f) > width) {
    sideWidth = width * 0.5f;
  }
  labelX = sideWidth - contentX;
  if (labelX < 0.0f) {
    labelX = 0.0f;
  }
  buttonX = contentWidth - sideWidth;
  if (buttonX < 0.0f) {
    buttonX = 0.0f;
  }
  labelWidth = buttonX - labelX;
  if (labelWidth < 0.0f) {
    labelWidth = 0.0f;
  }
  labelHeight = kStrappyPreferencesToolbarLabelHeight;
  labelY = (CGFloat)floor((double)((height - labelHeight) * 0.5f));
  [[self statusLabel] setFrame:
    CGRectMake(labelX,
               labelY,
               labelWidth,
               labelHeight)];
  [[self actionToolbarButton] setFrame:
    CGRectMake(buttonX, 0.0f, sideWidth, height)];
  [[self activityIndicatorView] setFrame:
    CGRectMake(buttonX, 0.0f, sideWidth, height)];
  [toolbar setNeedsLayout];
}

- (void)refreshToolbarItems
{
  if ([self working]) {
    [[self activityIndicatorView] startAnimating];
    [[self actionToolbarButton] setHidden:YES];
    [[self activityIndicatorView] setHidden:NO];
  } else {
    [[self activityIndicatorView] stopAnimating];
    [[self activityIndicatorView] setHidden:YES];
    [[self actionToolbarButton] setHidden:NO];
  }
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
  [self setWorking:NO];
  [[self searchBar] setDelegate:nil];
}

@end
