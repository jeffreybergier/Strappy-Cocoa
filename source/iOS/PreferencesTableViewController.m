#import "PreferencesTableViewController.h"

#import "FileScanner.h"
#import "StrappyAppearance.h"
#import "StrappyKeychain.h"
#import "StrappyActivityAccessoryView.h"
#import "StrappyPreferencesDatabaseWhitelistTableViewController.h"
#import "StrappyPreferencesDatabaseStudyViewController.h"
#import "StrappyPreferencesModelWhitelistTableViewController.h"
#import "StrappyPreferencesSystemPromptsTableViewController.h"
#import "StrappySession.h"
#import "StrappySessionOptionsTableViewController.h"

static NSString *StrappyPreferencesTrimmedString(NSString *string)
{
  if (![string isKindOfClass:[NSString class]]) {
    return @"";
  }
  return [string stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

static NSString *StrappyPreferencesModelStringForRow(NSDictionary *row,
                                                     NSString *key)
{
  id value;

  if (![row isKindOfClass:[NSDictionary class]]) {
    return @"";
  }
  value = [row objectForKey:key];
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

static NSString *StrappyPreferencesModelDisplayNameForRow(NSDictionary *row)
{
  NSString *name;
  NSString *modelIdentifier;

  name = StrappyPreferencesModelStringForRow(row, @"name");
  if ([name length] > 0U) {
    return name;
  }
  modelIdentifier = StrappyPreferencesModelStringForRow(row, @"id");
  return ([modelIdentifier length] > 0U)
    ? modelIdentifier : NSLocalizedString(@"Model", nil);
}

static NSComparisonResult StrappyPreferencesCompareModelNameRows(
  id left,
  id right,
  void *context)
{
  NSDictionary *leftRow;
  NSDictionary *rightRow;
  NSComparisonResult result;

  (void)context;
  leftRow = [left isKindOfClass:[NSDictionary class]] ? left : nil;
  rightRow = [right isKindOfClass:[NSDictionary class]] ? right : nil;
  result = [StrappyPreferencesModelDisplayNameForRow(leftRow)
    caseInsensitiveCompare:StrappyPreferencesModelDisplayNameForRow(rightRow)];
  if (result != NSOrderedSame) {
    return result;
  }
  return [StrappyPreferencesModelStringForRow(leftRow, @"id")
    caseInsensitiveCompare:StrappyPreferencesModelStringForRow(rightRow, @"id")];
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
  kStrappyPaneRowSessionDefaults = 0,
  kStrappyPaneRowModels,
  kStrappyPaneRowDatabases,
  kStrappyPaneRowStudy,
  kStrappyPaneRowPrompts,
  kStrappyPaneRowCount
};

@interface PreferencesTableViewController ()
  <UITextFieldDelegate, StrappySessionOptionsTableViewControllerDelegate>
@property (nonatomic, strong) UITextField *apiEndpointField;
@property (nonatomic, strong) UITextField *apiTokenField;
@property (nonatomic, copy) StrappySessionOptions *defaultSessionOptions;
@property (nonatomic, assign) BOOL defaultSessionOptionsLoaded;
@property (nonatomic, assign) BOOL authenticationDirty;
- (UITextField *)makeFieldSecure:(BOOL)secure placeholder:(NSString *)placeholder;
- (void)loadAuthenticationFields;
- (BOOL)saveAuthenticationIfNeeded;
- (BOOL)saveAuthentication;
- (void)showMessage:(NSString *)message title:(NSString *)title;
- (void)showError:(NSError *)error title:(NSString *)title;
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
                @"https://openrouter.ai/api/v1/responses", nil)]];
  [self setApiTokenField:
    [self makeFieldSecure:YES
              placeholder:NSLocalizedString(@"Paste API token", nil)]];
  [self loadAuthenticationFields];

  [[self navigationItem] setRightBarButtonItem:
    [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                  target:self
                                                  action:@selector(doneAction:)]];
  [StrappyAppearance applyLegacyTintToBarButtonItem:
    [[self navigationItem] rightBarButtonItem]];

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
  [self setDefaultSessionOptions:nil];
  [self setDefaultSessionOptionsLoaded:NO];
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

- (void)showError:(NSError *)error title:(NSString *)title
{
  NSString *message;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = NSLocalizedString(@"Your changes could not be saved.", nil);
  }
  [self showMessage:message title:title];
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

#pragma mark - StrappySessionOptionsTableViewControllerDelegate

- (NSArray *)currentAllowedModels
{
  NSError *error;
  NSArray *models;

  error = nil;
  models = [StrappySession allowedOpenRouterModelCatalogWithError:&error];
  if (![models isKindOfClass:[NSArray class]]) {
    [self showError:error
              title:NSLocalizedString(@"Could not load models", nil)];
    return [NSArray array];
  }
  return [models
    sortedArrayUsingFunction:StrappyPreferencesCompareModelNameRows
                     context:NULL];
}

- (NSArray *)currentAssistantSets
{
  NSArray *assistantSets;

  assistantSets = [StrappySession assistantSetCatalog];
  return [assistantSets isKindOfClass:[NSArray class]]
    ? assistantSets : [NSArray array];
}

- (StrappySessionOptions *)sessionOptions
{
  NSError *error;
  StrappySessionOptions *options;

  if ([self defaultSessionOptionsLoaded]) {
    return [self defaultSessionOptions];
  }

  error = nil;
  options = [StrappySession defaultSessionOptionsWithError:&error];
  [self setDefaultSessionOptionsLoaded:YES];
  [self setDefaultSessionOptions:options];
  if (options == nil) {
    [self showError:error
              title:NSLocalizedString(@"Could not load default options", nil)];
  }
  return [self defaultSessionOptions];
}

- (BOOL)updateSessionOptions:(StrappySessionOptions *)options
               changedFields:(StrappySessionOptionMask)changedFields
{
  NSError *error;

  error = nil;
  if (![StrappySession updateDefaultSessionOptions:options
                                      changedFields:changedFields
                                              error:&error]) {
    [self showError:error
              title:NSLocalizedString(@"Failed to Save Changes", nil)];
    return NO;
  }

  [self setDefaultSessionOptions:nil];
  [self setDefaultSessionOptionsLoaded:NO];
  (void)[self sessionOptions];
  return YES;
}

- (void)dismissOptionsControllerAnimated:(BOOL)animated
{
  [self setDefaultSessionOptions:nil];
  [self setDefaultSessionOptionsLoaded:NO];
  [[self navigationController] popToViewController:self animated:animated];
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

  cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                reuseIdentifier:nil];
  [cell setAccessoryType:UITableViewCellAccessoryDisclosureIndicator];
  [cell setAccessoryView:nil];
  [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
  if ([indexPath row] == kStrappyPaneRowSessionDefaults) {
    [[cell textLabel] setText:NSLocalizedString(@"Session Defaults", nil)];
  } else if ([indexPath row] == kStrappyPaneRowModels) {
    [[cell textLabel] setText:NSLocalizedString(@"Models", nil)];
    if ([StrappySession isModelCatalogRefreshInFlight]) {
      [cell setAccessoryType:UITableViewCellAccessoryNone];
      [cell setAccessoryView:StrappyActivityAccessoryView([UIColor grayColor])];
    }
  } else if ([indexPath row] == kStrappyPaneRowDatabases) {
    [[cell textLabel] setText:NSLocalizedString(@"Databases", nil)];
    if ([FileScanner isDatabaseCatalogScanInFlight]) {
      [cell setAccessoryType:UITableViewCellAccessoryNone];
      [cell setAccessoryView:StrappyActivityAccessoryView([UIColor grayColor])];
    }
  } else if ([indexPath row] == kStrappyPaneRowStudy) {
    [[cell textLabel] setText:NSLocalizedString(@"Study", nil)];
  } else {
    [[cell textLabel] setText:NSLocalizedString(@"Prompts", nil)];
  }
  return cell;
}

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  UIViewController *controller;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];

  if ([indexPath section] == kStrappyPreferencesSectionAuthentication) {
    return;
  }

  controller = nil;
  if ([indexPath row] == kStrappyPaneRowSessionDefaults) {
    [self setDefaultSessionOptions:nil];
    [self setDefaultSessionOptionsLoaded:NO];
    controller =
      [[StrappySessionOptionsTableViewController alloc]
        initWithOptionsDelegate:self
             presentedModally:NO];
    [controller setTitle:NSLocalizedString(@"Session Defaults", nil)];
  } else if ([indexPath row] == kStrappyPaneRowModels) {
    controller =
      [[StrappyPreferencesModelWhitelistTableViewController alloc] init];
  } else if ([indexPath row] == kStrappyPaneRowDatabases) {
    controller =
      [[StrappyPreferencesDatabaseWhitelistTableViewController alloc] init];
  } else if ([indexPath row] == kStrappyPaneRowStudy) {
    controller =
      [[StrappyPreferencesDatabaseStudyViewController alloc] init];
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
