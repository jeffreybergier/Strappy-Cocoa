#import "StrappyAccountTableViewController.h"

#import "StrappyAppearance.h"
#import "StrappyAuthentication.h"
#import "StrappyKeychain.h"
#import "StrappySession.h"
#import "XPUIKit.h"

#include <errno.h>
#include <stdlib.h>

static NSString *StrappyAccountString(NSDictionary *row, NSString *key)
{
  id value;

  value = [row objectForKey:key];
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

static NSString *StrappyAccountProviderName(NSString *providerIdentifier)
{
  if ([providerIdentifier isEqualToString:@"openrouter"]) {
    return @"OpenRouter";
  }
  if ([providerIdentifier isEqualToString:@"openai_chatgpt"]) {
    return @"ChatGPT";
  }
  return NSLocalizedString(@"Other", nil);
}

@interface StrappyAccountTableViewController ()
  <UITextFieldDelegate, UIAlertViewDelegate>
@property (nonatomic, copy) NSString *providerAccountIdentifier;
@property (nonatomic, strong) NSDictionary *account;
@property (nonatomic, strong) UITextField *nameField;
@property (nonatomic, strong) UITextField *endpointField;
@property (nonatomic, strong) UITextField *tokenField;
@property (nonatomic, strong) UITextField *maxOutputField;
@property (nonatomic, strong) UISwitch *maxOutputSwitch;
@property (nonatomic, assign) BOOL dirty;
@property (nonatomic, assign) BOOL presentedModally;
- (void)reloadAccount;
- (void)authenticationDidChange:(NSNotification *)notification;
- (void)cancelAction:(id)sender;
- (void)saveAction:(id)sender;
- (void)deleteAction;
- (void)performChatGPTActionAtRow:(NSInteger)row;
@end

@implementation StrappyAccountTableViewController

- (id)initWithProviderAccountIdentifier:(NSString *)identifier
{
  return [self initWithProviderAccountIdentifier:identifier
                                presentedModally:NO];
}

- (id)initWithProviderAccountIdentifier:(NSString *)identifier
                       presentedModally:(BOOL)presentedModally
{
  if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
    [self setProviderAccountIdentifier:identifier];
    [self setPresentedModally:presentedModally];
  }
  return self;
}

- (UITextField *)fieldWithPlaceholder:(NSString *)placeholder secure:(BOOL)secure
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
  [field addTarget:self action:@selector(fieldChanged:)
    forControlEvents:UIControlEventEditingChanged];
  return field;
}

- (void)viewDidLoad
{
  [super viewDidLoad];
  [self setNameField:[self fieldWithPlaceholder:
    NSLocalizedString(@"Account Name", nil) secure:NO]];
  [self setEndpointField:[self fieldWithPlaceholder:
    NSLocalizedString(@"https://example.com/v1/responses", nil) secure:NO]];
  [[self endpointField] setKeyboardType:UIKeyboardTypeURL];
  [self setTokenField:[self fieldWithPlaceholder:
    NSLocalizedString(@"Paste API key", nil) secure:YES]];
  [self setMaxOutputField:[self fieldWithPlaceholder:@"14286" secure:NO]];
  [[self maxOutputField] setKeyboardType:UIKeyboardTypeNumberPad];
  [self setMaxOutputSwitch:[[UISwitch alloc] initWithFrame:CGRectZero]];
  [[self maxOutputSwitch] addTarget:self action:@selector(limitChanged:)
    forControlEvents:UIControlEventValueChanged];
  [[self navigationItem] setRightBarButtonItem:[[UIBarButtonItem alloc]
    initWithBarButtonSystemItem:UIBarButtonSystemItemSave target:self
    action:@selector(saveAction:)]];
  if ([self presentedModally]) {
    [[self navigationItem] setLeftBarButtonItem:[[UIBarButtonItem alloc]
      initWithBarButtonSystemItem:UIBarButtonSystemItemCancel target:self
      action:@selector(cancelAction:)]];
    [StrappyAppearance applyLegacyTintToBarButtonItem:
      [[self navigationItem] leftBarButtonItem]];
  }
  [StrappyAppearance applyLegacyTintToBarButtonItem:
    [[self navigationItem] rightBarButtonItem]];
  [[NSNotificationCenter defaultCenter] addObserver:self
    selector:@selector(authenticationDidChange:)
    name:StrappyAuthenticationDidChangeNotification object:nil];
  [self reloadAccount];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [self reloadAccount];
}

- (void)reloadAccount
{
  NSArray *accounts;
  NSDictionary *account;
  NSUInteger index;
  NSError *error;
  NSString *provider;
  NSString *token;
  long long maximum;

  error = nil;
  accounts = [StrappySession verifiedProviderAccountCatalogWithError:&error];
  account = nil;
  for (index = 0U; index < [accounts count]; index++) {
    NSDictionary *candidate;

    candidate = [accounts objectAtIndex:index];
    if ([StrappyAccountString(candidate, @"id")
          isEqualToString:[self providerAccountIdentifier]]) {
      account = candidate;
      break;
    }
  }
  if (account == nil) {
    if (error != nil) {
      [self showError:error title:NSLocalizedString(@"Could Not Load Account", nil)];
    }
    return;
  }
  [self setAccount:account];
  provider = StrappyAccountString(account, @"provider_id");
  [[self navigationItem] setTitle:StrappyAccountString(account, @"name")];
  [[self nameField] setText:StrappyAccountString(account, @"name")];
  [[self endpointField] setText:StrappyAccountString(account,
    @"responses_endpoint")];
  token = nil;
  if (![provider isEqualToString:@"openai_chatgpt"]) {
    (void)[[StrappyKeychain sharedKeychain] loadBearerToken:&token
      forProviderIdentifier:provider
      providerAccountIdentifier:[self providerAccountIdentifier]];
  }
  [[self tokenField] setText:(token != nil) ? token : @""];
  maximum = [[account objectForKey:@"max_output_tokens"] longLongValue];
  [[self maxOutputSwitch] setOn:(maximum > 0LL)];
  [[self maxOutputField] setText:(maximum > 0LL) ?
    [NSString stringWithFormat:@"%lld", maximum] : @""];
  [[self maxOutputField] setEnabled:(maximum > 0LL)];
  [self setDirty:NO];
  [[[self navigationItem] rightBarButtonItem] setEnabled:NO];
  if ([provider isEqualToString:@"openai_chatgpt"]) {
    StrappyAuthentication *authentication;

    authentication = [StrappyAuthentication
      authenticationForProviderAccountIdentifier:
        [self providerAccountIdentifier]];
    [authentication refreshChatGPTCredentialsIfNeeded];
  }
  [[self tableView] reloadData];
}

- (void)showError:(NSError *)error title:(NSString *)title
{
  NSString *message;
  UIAlertView *alert;

  message = [error localizedDescription];
  if ([message length] == 0U) {
    message = NSLocalizedString(@"The request failed.", nil);
  }
  alert = [[UIAlertView alloc] initWithTitle:title message:message delegate:nil
    cancelButtonTitle:NSLocalizedString(@"OK", nil) otherButtonTitles:nil];
  [alert show];
}

- (void)fieldChanged:(id)sender
{
  (void)sender;
  [self setDirty:YES];
  [[[self navigationItem] rightBarButtonItem] setEnabled:YES];
}

- (void)limitChanged:(id)sender
{
  NSIndexPath *maximumIndexPath;
  NSArray *indexPaths;
  BOOL enabled;

  (void)sender;
  enabled = [[self maxOutputSwitch] isOn];
  [[self maxOutputField] setEnabled:enabled];
  [self fieldChanged:sender];
  maximumIndexPath = [NSIndexPath indexPathForRow:1 inSection:2];
  indexPaths = [NSArray arrayWithObject:maximumIndexPath];
  if (enabled) {
    [[self tableView] insertRowsAtIndexPaths:indexPaths
      withRowAnimation:UITableViewRowAnimationFade];
  } else {
    [[self tableView] deleteRowsAtIndexPaths:indexPaths
      withRowAnimation:UITableViewRowAnimationFade];
  }
}

- (void)cancelAction:(id)sender
{
  (void)sender;
  [[self view] endEditing:YES];
  [self XP_dismissViewControllerAnimated:YES];
}

- (void)saveAction:(id)sender
{
  NSString *identifier;
  NSString *provider;
  NSString *name;
  NSString *endpoint;
  NSString *token;
  NSString *maximumText;
  const char *maximumBytes;
  char *maximumEnd;
  long long maximum;
  NSError *error;
  BOOL credentialSaved;

  (void)sender;
  identifier = [self providerAccountIdentifier];
  provider = StrappyAccountString([self account], @"provider_id");
  name = [[[self nameField] text] stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  endpoint = [[[self endpointField] text] stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  token = [[[self tokenField] text] stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  maximum = 0LL;
  if ([[self maxOutputSwitch] isOn]) {
    maximumText = [[[self maxOutputField] text]
      stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    maximumBytes = [maximumText UTF8String];
    maximumEnd = NULL;
    errno = 0;
    maximum = (maximumBytes != NULL) ?
      strtoll(maximumBytes, &maximumEnd, 10) : 0LL;
    if (([maximumText length] == 0U) || (errno == ERANGE) ||
        (maximumEnd == maximumBytes) || (maximumEnd == NULL) ||
        (*maximumEnd != '\0') || (maximum <= 0LL)) {
      [self showError:nil title:NSLocalizedString(
        @"Maximum Output Tokens Must Be a Positive Integer", nil)];
      return;
    }
  }
  if ([name length] == 0U) {
    [self showError:nil title:NSLocalizedString(@"Account Name Is Required", nil)];
    return;
  }
  if ([provider isEqualToString:@"openrouter"] && ([token length] == 0U)) {
    [self showError:nil title:NSLocalizedString(@"API Key Is Required", nil)];
    return;
  }
  if ([provider isEqualToString:@"other"] && ([endpoint length] == 0U)) {
    [self showError:nil title:NSLocalizedString(
      @"Responses Endpoint Is Required", nil)];
    return;
  }
  error = nil;
  if (![StrappySession updateProviderAccountIdentifier:identifier
    displayName:name responsesEndpoint:endpoint maxOutputTokens:maximum
    error:&error]) {
    [self showError:error title:NSLocalizedString(@"Could Not Save Account", nil)];
    return;
  }
  credentialSaved = YES;
  if (![provider isEqualToString:@"openai_chatgpt"]) {
    StrappyKeychain *keychain;
    NSObject *credentialLock;

    keychain = [StrappyKeychain sharedKeychain];
    credentialLock = [keychain credentialLockForProviderIdentifier:provider
      providerAccountIdentifier:identifier];
    @synchronized(credentialLock) {
      credentialSaved = ([token length] > 0U) ?
        [keychain saveBearerToken:token forProviderIdentifier:provider
          providerAccountIdentifier:identifier] :
        [keychain deleteBearerTokenForProviderIdentifier:provider
          providerAccountIdentifier:identifier];
    }
  }
  if (!credentialSaved) {
    [self showError:nil title:NSLocalizedString(@"Could Not Save Credential", nil)];
    return;
  }
  if ([self presentedModally]) {
    [self XP_dismissViewControllerAnimated:YES];
  } else {
    [self reloadAccount];
  }
}

- (void)authenticationDidChange:(NSNotification *)notification
{
  if ([[notification object] isEqual:[StrappyAuthentication
      authenticationForProviderAccountIdentifier:
        [self providerAccountIdentifier]]]) {
    [[self tableView] reloadData];
  }
}

- (NSInteger)chatGPTRowCount
{
  StrappyAuthentication *authentication;
  StrappyAuthenticationState state;

  authentication = [StrappyAuthentication
    authenticationForProviderAccountIdentifier:[self providerAccountIdentifier]];
  state = [authentication state];
  if (state == StrappyAuthenticationStateAwaitingUser) {
    return 6;
  }
  if ((state == StrappyAuthenticationStateError) &&
      [authentication hasStoredCredentials]) {
    return 3;
  }
  return 2;
}

- (NSString *)chatGPTStatus
{
  StrappyAuthentication *authentication;
  StrappyAuthenticationState state;

  authentication = [StrappyAuthentication
    authenticationForProviderAccountIdentifier:[self providerAccountIdentifier]];
  state = [authentication state];
  if (![StrappyAuthentication isChatGPTProviderEnabled]) return NSLocalizedString(@"Disabled", nil);
  if (state == StrappyAuthenticationStateRequestingCode) return NSLocalizedString(@"Requesting a device code…", nil);
  if (state == StrappyAuthenticationStateAwaitingUser) return NSLocalizedString(@"Waiting for browser approval", nil);
  if (state == StrappyAuthenticationStateSignedIn) return NSLocalizedString(@"Signed in", nil);
  if (state == StrappyAuthenticationStateRefreshing) return NSLocalizedString(@"Refreshing credentials…", nil);
  if (state == StrappyAuthenticationStateCancelled) return NSLocalizedString(@"Sign-in cancelled", nil);
  if (state == StrappyAuthenticationStateError) return ([authentication errorMessage] != nil) ? [authentication errorMessage] : NSLocalizedString(@"Authentication failed.", nil);
  return NSLocalizedString(@"Not signed in", nil);
}

- (void)performChatGPTActionAtRow:(NSInteger)row
{
  StrappyAuthentication *authentication;
  StrappyAuthenticationState state;

  if (row == 0 || ![StrappyAuthentication isChatGPTProviderEnabled]) return;
  authentication = [StrappyAuthentication authenticationForProviderAccountIdentifier:
    [self providerAccountIdentifier]];
  state = [authentication state];
  if (state == StrappyAuthenticationStateAwaitingUser) {
    if ((row == 1) || (row == 2)) return;
    if (row == 3) {
      [[UIPasteboard generalPasteboard] setString:[authentication userCode]];
    } else if (row == 4) {
      NSURL *URL;
      UIApplication *application;

      URL = [NSURL URLWithString:[authentication verificationURL]];
      application = [UIApplication sharedApplication];
      if ((URL == nil) || ![application canOpenURL:URL] ||
          ![application openURL:URL]) {
        [self showError:nil title:NSLocalizedString(@"Could Not Open Browser", nil)];
      }
    } else {
      [authentication cancelChatGPTDeviceLogin];
    }
  } else if (state == StrappyAuthenticationStateRequestingCode) {
    [authentication cancelChatGPTDeviceLogin];
  } else if ((state == StrappyAuthenticationStateSignedIn) ||
             (state == StrappyAuthenticationStateRefreshing)) {
    (void)[authentication signOutChatGPT];
  } else if ((state == StrappyAuthenticationStateError) &&
             [authentication hasStoredCredentials]) {
    if (row == 1) (void)[authentication refreshChatGPTCredentialsIfNeeded];
    else (void)[authentication signOutChatGPT];
  } else {
    (void)[authentication startChatGPTDeviceLogin];
  }
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return 4;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
  NSString *provider;

  (void)tableView;
  provider = StrappyAccountString([self account], @"provider_id");
  if (section == 0) return 1;
  if (section == 1) {
    if ([provider isEqualToString:@"openai_chatgpt"]) return [self chatGPTRowCount];
    return [provider isEqualToString:@"other"] ? 2 : 1;
  }
  if (section == 2) return [[self maxOutputSwitch] isOn] ? 2 : 1;
  return 1;
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
  NSString *provider;

  (void)tableView;
  provider = StrappyAccountString([self account], @"provider_id");
  if (section == 0) return StrappyAccountProviderName(provider);
  if (section == 1) return [provider isEqualToString:@"openai_chatgpt"] ?
    NSLocalizedString(@"Authentication", nil) : NSLocalizedString(@"Connection", nil);
  if (section == 2) return NSLocalizedString(@"Limits", nil);
  return nil;
}

- (UITableViewCell *)fieldCell:(UITextField *)field
{
  UITableViewCell *cell;

  cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
    reuseIdentifier:nil];
  [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
  [field setFrame:CGRectInset([[cell contentView] bounds], 15.0f, 0.0f)];
  [field setAutoresizingMask:UIViewAutoresizingFlexibleWidth |
    UIViewAutoresizingFlexibleHeight];
  [[cell contentView] addSubview:field];
  return cell;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell;
  NSString *provider;

  (void)tableView;
  provider = StrappyAccountString([self account], @"provider_id");
  if ([indexPath section] == 0) return [self fieldCell:[self nameField]];
  if ([indexPath section] == 1 && ![provider isEqualToString:@"openai_chatgpt"]) {
    if ([provider isEqualToString:@"other"] && [indexPath row] == 0)
      return [self fieldCell:[self endpointField]];
    return [self fieldCell:[self tokenField]];
  }
  cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1
    reuseIdentifier:nil];
  if ([indexPath section] == 1) {
    StrappyAuthentication *authentication;
    StrappyAuthenticationState state;
    NSInteger row;

    authentication = [StrappyAuthentication authenticationForProviderAccountIdentifier:
      [self providerAccountIdentifier]];
    state = [authentication state];
    row = [indexPath row];
    if (row == 0) {
      [[cell textLabel] setText:NSLocalizedString(@"Status", nil)];
      [[cell detailTextLabel] setText:[self chatGPTStatus]];
      [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
    } else if (state == StrappyAuthenticationStateAwaitingUser) {
      if (row == 1) { [[cell textLabel] setText:NSLocalizedString(@"Code", nil)]; [[cell detailTextLabel] setText:[authentication userCode]]; [cell setSelectionStyle:UITableViewCellSelectionStyleNone]; }
      else if (row == 2) {
        NSString *verificationURL;

        verificationURL = [authentication verificationURL];
        if ([verificationURL hasPrefix:@"https://"]) {
          verificationURL = [verificationURL substringFromIndex:8U];
        }
        [[cell textLabel] setText:NSLocalizedString(@"URL", nil)];
        [[cell detailTextLabel] setText:verificationURL];
        [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
      }
      else if (row == 3) [[cell textLabel] setText:NSLocalizedString(@"Copy Code", nil)];
      else if (row == 4) [[cell textLabel] setText:NSLocalizedString(@"Open Browser", nil)];
      else { [[cell textLabel] setText:NSLocalizedString(@"Cancel", nil)]; [[cell textLabel] setTextColor:[UIColor redColor]]; }
    } else if (state == StrappyAuthenticationStateRequestingCode) {
      [[cell textLabel] setText:NSLocalizedString(@"Cancel", nil)]; [[cell textLabel] setTextColor:[UIColor redColor]];
    } else if ((state == StrappyAuthenticationStateSignedIn) || (state == StrappyAuthenticationStateRefreshing)) {
      [[cell textLabel] setText:NSLocalizedString(@"Sign Out", nil)]; [[cell textLabel] setTextColor:[UIColor redColor]];
    } else if ((state == StrappyAuthenticationStateError) && [authentication hasStoredCredentials]) {
      [[cell textLabel] setText:(row == 1) ? NSLocalizedString(@"Retry Refresh", nil) : NSLocalizedString(@"Sign Out", nil)];
      if (row != 1) [[cell textLabel] setTextColor:[UIColor redColor]];
    } else {
      [[cell textLabel] setText:NSLocalizedString(@"Sign In with ChatGPT", nil)];
    }
    return cell;
  }
  if ([indexPath section] == 2) {
    if ([indexPath row] == 0) {
      [[cell textLabel] setText:NSLocalizedString(@"Limit Output Tokens", nil)];
      [cell setAccessoryView:[self maxOutputSwitch]];
      [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
      return cell;
    }
    return [self fieldCell:[self maxOutputField]];
  }
  cell = [[UITableViewCell alloc]
    initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
  [[cell textLabel] setText:NSLocalizedString(@"Delete Account", nil)];
  [[cell textLabel] setTextColor:[UIColor redColor]];
  [[cell textLabel] XP_setTextAlignmentCenter];
  [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
  return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSString *provider;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  provider = StrappyAccountString([self account], @"provider_id");
  if (([indexPath section] == 1) && [provider isEqualToString:@"openai_chatgpt"])
    [self performChatGPTActionAtRow:[indexPath row]];
  else if ([indexPath section] == 3) [self deleteAction];
}

- (void)deleteAction
{
  UIAlertView *alert;

  if ([StrappySession hasInFlightSessions]) {
    [self showError:nil title:NSLocalizedString(
      @"Wait for active requests before deleting this account.", nil)];
    return;
  }
  alert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"Delete Account?", nil)
    message:[NSString stringWithFormat:NSLocalizedString(
      @"The credential for \"%@\" will be removed. Existing conversations and history will be preserved, but they cannot send new requests with this account.", nil),
      StrappyAccountString([self account], @"name")]
    delegate:self cancelButtonTitle:NSLocalizedString(@"Cancel", nil)
    otherButtonTitles:NSLocalizedString(@"Delete Account", nil), nil];
  [alert show];
}

- (void)alertView:(UIAlertView *)alertView clickedButtonAtIndex:(NSInteger)buttonIndex
{
  NSError *error;

  (void)alertView;
  if (buttonIndex == 0) return;
  error = nil;
  if (![StrappySession archiveProviderAccountIdentifier:
      [self providerAccountIdentifier] error:&error]) {
    [self showError:error title:NSLocalizedString(@"Could Not Delete Account", nil)];
  } else if ([self presentedModally]) {
    [self XP_dismissViewControllerAnimated:YES];
  } else {
    [[self navigationController] popViewControllerAnimated:YES];
  }
}

- (BOOL)textFieldShouldReturn:(UITextField *)textField
{
  [textField resignFirstResponder];
  return NO;
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [[self nameField] setDelegate:nil];
  [[self endpointField] setDelegate:nil];
  [[self tokenField] setDelegate:nil];
  [[self maxOutputField] setDelegate:nil];
}

@end

@implementation StrappyProviderPickerTableViewController

- (id)init
{
  if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
    [[self navigationItem] setTitle:NSLocalizedString(@"Add Account", nil)];
  }
  return self;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
  (void)tableView; (void)section;
  return (NSInteger)[[StrappySession providerCatalog] count];
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  static NSString *identifier = @"ProviderCell";
  UITableViewCell *cell;
  NSDictionary *provider;

  cell = [tableView dequeueReusableCellWithIdentifier:identifier];
  if (cell == nil) cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:identifier];
  provider = [[StrappySession providerCatalog] objectAtIndex:(NSUInteger)[indexPath row]];
  [[cell textLabel] setText:StrappyAccountString(provider, @"name")];
  if ([StrappyAccountString(provider, @"id") isEqualToString:@"openrouter"])
    [[cell detailTextLabel] setText:NSLocalizedString(@"API token", nil)];
  else if ([StrappyAccountString(provider, @"id") isEqualToString:@"openai_chatgpt"])
    [[cell detailTextLabel] setText:NSLocalizedString(@"Sign in with ChatGPT", nil)];
  else [[cell detailTextLabel] setText:NSLocalizedString(@"Endpoint and optional bearer token", nil)];
  [cell setAccessoryType:UITableViewCellAccessoryDisclosureIndicator];
  [[cell textLabel] setEnabled:[[provider objectForKey:@"available"] boolValue]];
  [[cell detailTextLabel] setEnabled:[[provider objectForKey:@"available"] boolValue]];
  return cell;
}

- (NSIndexPath *)tableView:(UITableView *)tableView willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *provider;

  (void)tableView;
  provider = [[StrappySession providerCatalog] objectAtIndex:(NSUInteger)[indexPath row]];
  return [[provider objectForKey:@"available"] boolValue] ? indexPath : nil;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *provider;
  NSDictionary *account;
  NSError *error;
  StrappyAccountTableViewController *controller;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  provider = [[StrappySession providerCatalog] objectAtIndex:(NSUInteger)[indexPath row]];
  error = nil;
  account = [StrappySession createProviderAccountForProviderIdentifier:
    StrappyAccountString(provider, @"id") error:&error];
  if (account == nil) {
    UIAlertView *alert;
    alert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"Could Not Add Account", nil)
      message:[error localizedDescription] delegate:nil
      cancelButtonTitle:NSLocalizedString(@"OK", nil) otherButtonTitles:nil];
    [alert show];
    return;
  }
  controller = [[StrappyAccountTableViewController alloc]
    initWithProviderAccountIdentifier:StrappyAccountString(account, @"id")];
  [[self navigationController] pushViewController:controller animated:YES];
}

@end
