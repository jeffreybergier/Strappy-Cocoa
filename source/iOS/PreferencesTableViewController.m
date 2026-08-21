#import "PreferencesTableViewController.h"

#import "FileScanner.h"
#import "StrappyAccountTableViewController.h"
#import "StrappyAppearance.h"
#import "StrappyActivityAccessoryView.h"
#import "StrappyPreferencesDatabaseWhitelistTableViewController.h"
#import "StrappyPreferencesDatabaseStudyViewController.h"
#import "StrappyPreferencesModelWhitelistTableViewController.h"
#import "StrappyPreferencesSystemPromptsTableViewController.h"
#import "StrappySession.h"
#import "StrappySessionOptionsTableViewController.h"
#import "XPUIKit.h"

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
  kStrappyPreferencesSectionAccounts = 0,
  kStrappyPreferencesSectionPanes,
  kStrappyPreferencesSectionCount
};

enum {
  kStrappyPaneRowModels = 0,
  kStrappyPaneRowSessionDefaults,
  kStrappyPaneRowDatabases,
  kStrappyPaneRowStudy,
  kStrappyPaneRowPrompts,
  kStrappyPaneRowCount
};

@interface PreferencesTableViewController ()
  <StrappySessionOptionsTableViewControllerDelegate>
@property (nonatomic, strong) NSArray *accounts;
@property (nonatomic, copy) StrappySessionOptions *defaultSessionOptions;
@property (nonatomic, assign) BOOL defaultSessionOptionsLoaded;
- (void)showMessage:(NSString *)message title:(NSString *)title;
- (void)showError:(NSError *)error title:(NSString *)title;
- (void)reloadAccounts;
- (void)providerAccountsDidChange:(NSNotification *)notification;
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
  [self reloadAccounts];

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
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(providerAccountsDidChange:)
           name:StrappyProviderAccountsDidChangeNotification
         object:nil];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [self setDefaultSessionOptions:nil];
  [self setDefaultSessionOptionsLoaded:NO];
  [[self navigationController] setToolbarHidden:YES animated:animated];
  [self reloadAccounts];
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

- (void)reloadAccounts
{
  NSArray *accounts;
  NSError *error;

  error = nil;
  accounts = [StrappySession verifiedProviderAccountCatalogWithError:&error];
  if (![accounts isKindOfClass:[NSArray class]]) {
    [self setAccounts:[NSArray array]];
    [self showError:error title:NSLocalizedString(@"Could Not Load Accounts", nil)];
  } else {
    [self setAccounts:accounts];
  }
  [[self tableView] reloadData];
}

- (void)providerAccountsDidChange:(NSNotification *)notification
{
  (void)notification;
  [self reloadAccounts];
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
  [self XP_dismissViewControllerAnimated:YES];
}

#pragma mark - StrappySessionOptionsTableViewControllerDelegate

- (NSArray *)currentProviderAccounts
{
  NSError *error;
  NSArray *accounts;

  error = nil;
  accounts = [StrappySession providerAccountCatalogWithError:&error];
  if (![accounts isKindOfClass:[NSArray class]]) {
    [self showError:error
              title:NSLocalizedString(@"Could Not Load Accounts", nil)];
    return [NSArray array];
  }
  return accounts;
}

- (NSArray *)currentAllowedModels
{
  NSError *error;
  NSArray *models;

  error = nil;
  models = [StrappySession allowedModelCatalogWithError:&error];
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
  if (section == kStrappyPreferencesSectionAccounts) {
    return (NSInteger)[[self accounts] count] + 1;
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
  if (section == kStrappyPreferencesSectionAccounts) {
    return NSLocalizedString(@"Accounts", nil);
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
  if ([indexPath section] == kStrappyPreferencesSectionAccounts) {
    NSDictionary *account;
    NSString *provider;

    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:nil];
    [cell setAccessoryType:UITableViewCellAccessoryDisclosureIndicator];
    if ((NSUInteger)[indexPath row] == [[self accounts] count]) {
      [[cell textLabel] setText:NSLocalizedString(@"Add Account", nil)];
      [[cell detailTextLabel] setText:NSLocalizedString(
        @"OpenRouter, ChatGPT, or another Responses provider", nil)];
      return cell;
    }
    account = [[self accounts] objectAtIndex:(NSUInteger)[indexPath row]];
    provider = StrappyPreferencesModelStringForRow(account, @"provider_id");
    [[cell textLabel] setText:StrappyPreferencesModelStringForRow(account,
      @"name")];
    if ([provider isEqualToString:@"openrouter"]) {
      [[cell detailTextLabel] setText:@"OpenRouter"];
    } else if ([provider isEqualToString:@"openai_chatgpt"]) {
      [[cell detailTextLabel] setText:@"ChatGPT"];
    } else {
      [[cell detailTextLabel] setText:NSLocalizedString(@"Other", nil)];
    }
    if (![[account objectForKey:@"available"] boolValue]) {
      [[cell detailTextLabel] setText:[NSString stringWithFormat:
        NSLocalizedString(@"%@ — setup required", nil),
        [[cell detailTextLabel] text]]];
    }
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

  if ([indexPath section] == kStrappyPreferencesSectionAccounts) {
    if ((NSUInteger)[indexPath row] == [[self accounts] count]) {
      controller = [[StrappyProviderPickerTableViewController alloc] init];
    } else {
      NSDictionary *account;

      account = [[self accounts] objectAtIndex:(NSUInteger)[indexPath row]];
      controller = [[StrappyAccountTableViewController alloc]
        initWithProviderAccountIdentifier:
          StrappyPreferencesModelStringForRow(account, @"id")];
    }
    [[self navigationController] pushViewController:controller animated:YES];
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
}

@end
