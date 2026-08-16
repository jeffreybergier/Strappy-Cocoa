#import "PreferencesTableViewController.h"

#import "FileScanner.h"
#import "StrappyAuthentication.h"
#import "StrappyAppearance.h"
#import "StrappyKeychain.h"
#import "StrappyActivityAccessoryView.h"
#import "StrappyPreferencesDatabaseWhitelistTableViewController.h"
#import "StrappyPreferencesDatabaseStudyViewController.h"
#import "StrappyPreferencesModelWhitelistTableViewController.h"
#import "StrappyPreferencesSystemPromptsTableViewController.h"
#import "StrappySession.h"
#import "StrappySessionOptionsTableViewController.h"
#import "XPUIKit.h"

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
  kStrappyPreferencesSectionOpenRouter = 0,
  kStrappyPreferencesSectionChatGPT,
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
- (void)chatGPTAuthenticationDidChange:(NSNotification *)notification;
- (NSInteger)chatGPTRowCount;
- (UITableViewCell *)chatGPTCellForRow:(NSInteger)row;
- (void)selectChatGPTRow:(NSInteger)row;
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
  [[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(chatGPTAuthenticationDidChange:)
           name:StrappyAuthenticationDidChangeNotification
         object:[StrappyAuthentication sharedAuthentication]];
  [[StrappyAuthentication sharedAuthentication]
    refreshChatGPTCredentialsIfNeeded];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [self setDefaultSessionOptions:nil];
  [self setDefaultSessionOptionsLoaded:NO];
  [[self navigationController] setToolbarHidden:YES animated:animated];
  [[StrappyAuthentication sharedAuthentication]
    refreshChatGPTCredentialsIfNeeded];
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

- (void)chatGPTAuthenticationDidChange:(NSNotification *)notification
{
  NSIndexSet *sections;

  (void)notification;
  sections = [NSIndexSet indexSetWithIndex:
    (NSUInteger)kStrappyPreferencesSectionChatGPT];
  [[self tableView] reloadSections:sections
                 withRowAnimation:UITableViewRowAnimationFade];
}

- (NSInteger)chatGPTRowCount
{
  StrappyAuthentication *authentication;
  StrappyAuthenticationState state;

  authentication = [StrappyAuthentication sharedAuthentication];
  state = [authentication state];
  if (state == StrappyAuthenticationStateAwaitingUser) {
    return 5;
  }
  if ((state == StrappyAuthenticationStateError) &&
      [authentication hasStoredCredentials]) {
    return 3;
  }
  return 2;
}

- (UITableViewCell *)chatGPTCellForRow:(NSInteger)row
{
  StrappyAuthentication *authentication;
  StrappyAuthenticationState state;
  UITableViewCell *cell;

  authentication = [StrappyAuthentication sharedAuthentication];
  state = [authentication state];
  if (row == 0) {
    NSString *status;
    NSString *accountIdentifier;

    cell = [[UITableViewCell alloc]
      initWithStyle:UITableViewCellStyleSubtitle
      reuseIdentifier:nil];
    [[cell textLabel] setText:NSLocalizedString(@"Status", nil)];
    [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
    accountIdentifier = [authentication accountIdentifier];
    if (state == StrappyAuthenticationStateRequestingCode) {
      status = NSLocalizedString(@"Requesting a device code…", nil);
    } else if (state == StrappyAuthenticationStateAwaitingUser) {
      status = NSLocalizedString(@"Waiting for browser approval", nil);
    } else if (state == StrappyAuthenticationStateSignedIn) {
      status = ([accountIdentifier length] > 0U)
        ? [NSString stringWithFormat:NSLocalizedString(@"Signed in as %@", nil),
            accountIdentifier]
        : NSLocalizedString(@"Signed in", nil);
    } else if (state == StrappyAuthenticationStateRefreshing) {
      status = NSLocalizedString(@"Refreshing credentials…", nil);
    } else if (state == StrappyAuthenticationStateError) {
      status = [authentication errorMessage];
      if ([status length] == 0U) {
        status = NSLocalizedString(@"Authentication failed.", nil);
      }
    } else if (state == StrappyAuthenticationStateCancelled) {
      status = NSLocalizedString(@"Sign-in cancelled", nil);
    } else {
      status = NSLocalizedString(@"Not signed in", nil);
    }
    [[cell detailTextLabel] setText:status];
    [[cell detailTextLabel] setNumberOfLines:2];
    return cell;
  }

  cell = [[UITableViewCell alloc]
    initWithStyle:UITableViewCellStyleValue1
    reuseIdentifier:nil];
  [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
  if (state == StrappyAuthenticationStateAwaitingUser) {
    if (row == 1) {
      [[cell textLabel] setText:NSLocalizedString(@"Code", nil)];
      [[cell detailTextLabel] setText:[authentication userCode]];
      [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
    } else if (row == 2) {
      [[cell textLabel] setText:NSLocalizedString(@"Copy Code", nil)];
    } else if (row == 3) {
      [[cell textLabel] setText:NSLocalizedString(@"Open Browser", nil)];
    } else {
      [[cell textLabel] setText:NSLocalizedString(@"Cancel", nil)];
      [[cell textLabel] setTextColor:[UIColor redColor]];
    }
  } else if (state == StrappyAuthenticationStateRequestingCode) {
    [[cell textLabel] setText:NSLocalizedString(@"Cancel", nil)];
    [[cell textLabel] setTextColor:[UIColor redColor]];
  } else if ((state == StrappyAuthenticationStateSignedIn) ||
             (state == StrappyAuthenticationStateRefreshing)) {
    [[cell textLabel] setText:NSLocalizedString(@"Sign Out", nil)];
    [[cell textLabel] setTextColor:[UIColor redColor]];
  } else if ((state == StrappyAuthenticationStateError) &&
             [authentication hasStoredCredentials]) {
    if (row == 1) {
      [[cell textLabel] setText:NSLocalizedString(@"Retry Refresh", nil)];
    } else {
      [[cell textLabel] setText:NSLocalizedString(@"Sign Out", nil)];
      [[cell textLabel] setTextColor:[UIColor redColor]];
    }
  } else {
    [[cell textLabel] setText:NSLocalizedString(@"Sign In with ChatGPT", nil)];
  }
  return cell;
}

- (void)selectChatGPTRow:(NSInteger)row
{
  StrappyAuthentication *authentication;
  StrappyAuthenticationState state;

  if (row == 0) {
    return;
  }
  authentication = [StrappyAuthentication sharedAuthentication];
  state = [authentication state];
  if (state == StrappyAuthenticationStateAwaitingUser) {
    if (row == 1) {
      return;
    }
    if (row == 2) {
      NSString *code;

      code = [authentication userCode];
      if ([code length] > 0U) {
        [[UIPasteboard generalPasteboard] setString:code];
        [self showMessage:NSLocalizedString(
          @"The device code was copied to the clipboard.", nil)
                    title:NSLocalizedString(@"Code Copied", nil)];
      }
      return;
    }
    if (row == 3) {
      NSURL *URL;
      UIApplication *application;

      URL = [NSURL URLWithString:[authentication verificationURL]];
      application = [UIApplication sharedApplication];
      if ((URL == nil) || ![application canOpenURL:URL] ||
          ![application openURL:URL]) {
        [self showMessage:NSLocalizedString(
          @"The ChatGPT sign-in page could not be opened.", nil)
                    title:NSLocalizedString(@"Could Not Open Browser", nil)];
      }
      return;
    }
    [authentication cancelChatGPTDeviceLogin];
    return;
  }
  if (state == StrappyAuthenticationStateRequestingCode) {
    [authentication cancelChatGPTDeviceLogin];
    return;
  }
  if ((state == StrappyAuthenticationStateSignedIn) ||
      (state == StrappyAuthenticationStateRefreshing)) {
    (void)[authentication signOutChatGPT];
    return;
  }
  if ((state == StrappyAuthenticationStateError) &&
      [authentication hasStoredCredentials]) {
    if (row == 1) {
      (void)[authentication refreshChatGPTCredentialsIfNeeded];
    } else {
      (void)[authentication signOutChatGPT];
    }
    return;
  }
  (void)[authentication startChatGPTDeviceLogin];
}

- (void)doneAction:(id)sender
{
  (void)sender;
  [[self view] endEditing:YES];
  if (![self saveAuthenticationIfNeeded]) {
    return;
  }
  [self XP_dismissViewControllerAnimated:YES];
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
  if (section == kStrappyPreferencesSectionOpenRouter) {
    return kStrappyAuthRowCount;
  }
  if (section == kStrappyPreferencesSectionChatGPT) {
    return [self chatGPTRowCount];
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
  if (section == kStrappyPreferencesSectionOpenRouter) {
    return NSLocalizedString(@"OpenRouter", nil);
  }
  if (section == kStrappyPreferencesSectionChatGPT) {
    return NSLocalizedString(@"ChatGPT (Experimental)", nil);
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
  if (section == kStrappyPreferencesSectionChatGPT) {
    return NSLocalizedString(
      @"Uses the Pi-compatible device flow. Access and refresh tokens are "
       "stored together in the Keychain and refreshed automatically. "
       "Device-code login must be enabled for your ChatGPT account or "
       "workspace.", nil);
  }
  return nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell;
  UITextField *field;

  if ([indexPath section] == kStrappyPreferencesSectionOpenRouter) {
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
  if ([indexPath section] == kStrappyPreferencesSectionChatGPT) {
    return [self chatGPTCellForRow:[indexPath row]];
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

- (CGFloat)tableView:(UITableView *)tableView
heightForRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  if (([indexPath section] == kStrappyPreferencesSectionChatGPT) &&
      ([indexPath row] == 0)) {
    return 60.0f;
  }
  return 44.0f;
}

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  UIViewController *controller;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];

  if ([indexPath section] == kStrappyPreferencesSectionOpenRouter) {
    return;
  }
  if ([indexPath section] == kStrappyPreferencesSectionChatGPT) {
    [self selectChatGPTRow:[indexPath row]];
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
