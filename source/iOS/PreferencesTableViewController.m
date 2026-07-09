#import "PreferencesTableViewController.h"

#import "FileScanner.h"
#import "StrappyKeychain.h"
#import "StrappyActivityAccessoryView.h"
#import "StrappyPreferencesDatabaseWhitelistTableViewController.h"
#import "StrappyPreferencesModelWhitelistTableViewController.h"
#import "StrappyPreferencesSystemPromptsTableViewController.h"
#import "StrappySession.h"

static NSString *StrappyPreferencesTrimmedString(NSString *string)
{
  if (![string isKindOfClass:[NSString class]]) {
    return @"";
  }
  return [string stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

enum {
  kStrappyPreferencesSectionAuthentication = 0,
  kStrappyPreferencesSectionPanes,
  kStrappyPreferencesSectionCount
};

enum {
  kStrappyAuthRowEndpoint = 0,
  kStrappyAuthRowToken,
  kStrappyAuthRowCount
};

enum {
  kStrappyPaneRowModels = 0,
  kStrappyPaneRowDatabases,
  kStrappyPaneRowPrompts,
  kStrappyPaneRowCount
};

@interface PreferencesTableViewController () <UITextFieldDelegate>
@property (nonatomic, strong) UITextField *apiEndpointField;
@property (nonatomic, strong) UITextField *apiTokenField;
@property (nonatomic, assign) BOOL authenticationDirty;
- (UITextField *)makeFieldSecure:(BOOL)secure placeholder:(NSString *)placeholder;
- (void)loadAuthenticationFields;
- (BOOL)saveAuthenticationIfNeeded;
- (BOOL)saveAuthentication;
- (void)showMessage:(NSString *)message title:(NSString *)title;
- (void)fieldChanged:(id)sender;
- (void)longRunningPreferenceWorkDidChange:(NSNotification *)notification;
- (void)doneAction:(id)sender;
@end

@implementation PreferencesTableViewController

- (instancetype)init
{
  if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
    [[self navigationItem] setTitle:NSLocalizedString(@"Preferences", nil)];
  }
  return self;
}

- (void)viewDidLoad
{
  [super viewDidLoad];

  [self setApiEndpointField:
    [self makeFieldSecure:NO
              placeholder:NSLocalizedString(
                @"https://openrouter.ai/api/v1/chat/completions", nil)]];
  [self setApiTokenField:
    [self makeFieldSecure:YES
              placeholder:NSLocalizedString(@"Paste API token", nil)]];
  [self loadAuthenticationFields];

  [[self navigationItem] setRightBarButtonItem:
    [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                  target:self
                                                  action:@selector(doneAction:)]];

  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(longRunningPreferenceWorkDidChange:)
           name:StrappySessionModelCatalogRefreshDidStartNotification
         object:nil];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(longRunningPreferenceWorkDidChange:)
           name:StrappySessionModelCatalogRefreshDidFinishNotification
         object:nil];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(longRunningPreferenceWorkDidChange:)
           name:FileScannerDatabaseCatalogScanDidStartNotification
         object:nil];
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(longRunningPreferenceWorkDidChange:)
           name:FileScannerDatabaseCatalogScanDidFinishNotification
         object:nil];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [[self navigationController] setToolbarHidden:YES animated:animated];
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

- (UITextField *)makeFieldSecure:(BOOL)secure placeholder:(NSString *)placeholder
{
  UITextField *field;

  field = [[UITextField alloc] initWithFrame:CGRectZero];
  [field setPlaceholder:placeholder];
  [field setSecureTextEntry:secure];
  [field setDelegate:self];
  [field setAutocorrectionType:UITextAutocorrectionTypeNo];
  [field setAutocapitalizationType:UITextAutocapitalizationTypeNone];
  [field setClearButtonMode:UITextFieldViewModeWhileEditing];
  [field setContentVerticalAlignment:UIControlContentVerticalAlignmentCenter];
  [field setReturnKeyType:secure ? UIReturnKeyDone : UIReturnKeyNext];
  [field setKeyboardType:secure ? UIKeyboardTypeDefault : UIKeyboardTypeURL];
  [field addTarget:self
            action:@selector(fieldChanged:)
  forControlEvents:UIControlEventEditingChanged];
  return field;
}

- (void)loadAuthenticationFields
{
  NSString *endpoint;
  NSString *token;

  endpoint = [[StrappyKeychain sharedKeychain] apiEndpoint];
  if ([endpoint length] == 0U) {
    endpoint = [StrappyKeychain defaultAPIEndpoint];
  }
  token = [[StrappyKeychain sharedKeychain] apiToken];

  [[self apiEndpointField] setText:(endpoint != nil) ? endpoint : @""];
  [[self apiTokenField] setText:(token != nil) ? token : @""];
  [self setAuthenticationDirty:NO];
}

- (void)fieldChanged:(id)sender
{
  (void)sender;
  [self setAuthenticationDirty:YES];
}

- (void)longRunningPreferenceWorkDidChange:(NSNotification *)notification
{
  (void)notification;
  [[self tableView] reloadData];
}

- (void)doneAction:(id)sender
{
  (void)sender;
  [[self view] endEditing:YES];
  if (![self saveAuthenticationIfNeeded]) {
    return;
  }
  [self dismissModalViewControllerAnimated:YES];
}

- (BOOL)saveAuthenticationIfNeeded
{
  if (![self authenticationDirty]) {
    return YES;
  }
  return [self saveAuthentication];
}

- (BOOL)saveAuthentication
{
  NSString *endpoint;
  NSString *token;

  endpoint = StrappyPreferencesTrimmedString([[self apiEndpointField] text]);
  token = StrappyPreferencesTrimmedString([[self apiTokenField] text]);
  if (([endpoint length] == 0U) || ([token length] == 0U)) {
    [self showMessage:NSLocalizedString(
      @"API endpoint and token are required.", nil)
                title:NSLocalizedString(@"Credentials Required", nil)];
    return NO;
  }

  if (![[StrappyKeychain sharedKeychain] saveAPIEndpoint:endpoint token:token]) {
    [self showMessage:NSLocalizedString(
      @"The keychain refused the write.", nil)
                title:NSLocalizedString(@"Could Not Save Credentials", nil)];
    return NO;
  }

  [[self apiEndpointField] setText:endpoint];
  [[self apiTokenField] setText:token];
  [self setAuthenticationDirty:NO];
  return YES;
}

#pragma mark - UITextFieldDelegate

- (BOOL)textFieldShouldReturn:(UITextField *)textField
{
  if (textField == [self apiEndpointField]) {
    [[self apiTokenField] becomeFirstResponder];
    return NO;
  }

  [textField resignFirstResponder];
  [self saveAuthenticationIfNeeded];
  return NO;
}

#pragma mark - UITableViewDataSource

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return kStrappyPreferencesSectionCount;
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
  (void)tableView;
  if (section == kStrappyPreferencesSectionAuthentication) {
    return kStrappyAuthRowCount;
  }
  if (section == kStrappyPreferencesSectionPanes) {
    return kStrappyPaneRowCount;
  }
  return 0;
}

- (NSString *)tableView:(UITableView *)tableView
titleForHeaderInSection:(NSInteger)section
{
  (void)tableView;
  if (section == kStrappyPreferencesSectionAuthentication) {
    return NSLocalizedString(@"Authentication", nil);
  }
  if (section == kStrappyPreferencesSectionPanes) {
    return NSLocalizedString(@"Preferences", nil);
  }
  return nil;
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
  UITextField *field;

  if ([indexPath section] == kStrappyPreferencesSectionAuthentication) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                  reuseIdentifier:nil];
    [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
    field = ([indexPath row] == kStrappyAuthRowEndpoint)
      ? [self apiEndpointField]
      : [self apiTokenField];
    [field setFrame:CGRectInset([[cell contentView] bounds], 15.0f, 0.0f)];
    [field setAutoresizingMask:
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight];
    [[cell contentView] addSubview:field];
    return cell;
  }

  cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1
                                reuseIdentifier:nil];
  [cell setAccessoryType:UITableViewCellAccessoryDisclosureIndicator];
  [cell setAccessoryView:nil];
  [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
  if ([indexPath row] == kStrappyPaneRowModels) {
    [[cell textLabel] setText:NSLocalizedString(@"Models", nil)];
    [[cell detailTextLabel] setText:NSLocalizedString(@"Fetch, search, use", nil)];
    if ([StrappySession isModelCatalogRefreshInFlight]) {
      [cell setAccessoryType:UITableViewCellAccessoryNone];
      [cell setAccessoryView:StrappyActivityAccessoryView([UIColor grayColor])];
    }
  } else if ([indexPath row] == kStrappyPaneRowDatabases) {
    [[cell textLabel] setText:NSLocalizedString(@"Databases", nil)];
    [[cell detailTextLabel] setText:NSLocalizedString(@"Scan, search, use", nil)];
    if ([FileScanner isDatabaseCatalogScanInFlight]) {
      [cell setAccessoryType:UITableViewCellAccessoryNone];
      [cell setAccessoryView:StrappyActivityAccessoryView([UIColor grayColor])];
    }
  } else {
    [[cell textLabel] setText:NSLocalizedString(@"Prompts", nil)];
    [[cell detailTextLabel] setText:NSLocalizedString(@"System prompt", nil)];
  }
  return cell;
}

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewController *controller;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];

  if ([indexPath section] == kStrappyPreferencesSectionAuthentication) {
    return;
  }

  controller = nil;
  if ([indexPath row] == kStrappyPaneRowModels) {
    controller =
      [[StrappyPreferencesModelWhitelistTableViewController alloc] init];
  } else if ([indexPath row] == kStrappyPaneRowDatabases) {
    controller =
      [[StrappyPreferencesDatabaseWhitelistTableViewController alloc] init];
  } else if ([indexPath row] == kStrappyPaneRowPrompts) {
    controller =
      [[StrappyPreferencesSystemPromptsTableViewController alloc] init];
  }

  if (controller != nil) {
    [[self navigationController] pushViewController:controller animated:YES];
  }
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [[self apiEndpointField] setDelegate:nil];
  [[self apiTokenField] setDelegate:nil];
}

@end
