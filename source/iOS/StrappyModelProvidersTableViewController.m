#import "StrappyModelProvidersTableViewController.h"

#import "StrappyActivityAccessoryView.h"
#import "StrappySession.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>

static NSString *StrappyProviderModelString(NSDictionary *row, NSString *key)
{
  id value;

  value = [row objectForKey:key];
  if ([value isKindOfClass:[NSString class]]) return value;
  return [value isKindOfClass:[NSNumber class]] ? [value stringValue] : @"";
}

@interface StrappyManualModelsTableViewController : UITableViewController
@property (nonatomic, copy) NSString *providerIdentifier;
@property (nonatomic, strong) NSArray *models;
- (id)initWithProviderIdentifier:(NSString *)providerIdentifier title:(NSString *)title;
@end

@interface StrappyManualModelTableViewController : UITableViewController
  <UITextFieldDelegate, UIAlertViewDelegate>
@property (nonatomic, copy) NSString *providerIdentifier;
@property (nonatomic, strong) NSDictionary *model;
@property (nonatomic, strong) NSArray *fields;
@property (nonatomic, strong) UISwitch *reasoningSwitch;
@property (nonatomic, strong) UISwitch *imagesSwitch;
@property (nonatomic, assign) BOOL builtIn;
- (id)initWithProviderIdentifier:(NSString *)providerIdentifier model:(NSDictionary *)model;
@end

@interface StrappyModelProvidersTableViewController ()
@property (nonatomic, strong) NSArray *providers;
- (void)refreshChanged:(NSNotification *)notification;
@end

@implementation StrappyModelProvidersTableViewController

- (id)init
{
  if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
    [[self navigationItem] setTitle:NSLocalizedString(@"Model Providers", nil)];
  }
  return self;
}

- (void)viewDidLoad
{
  [super viewDidLoad];
  [self setProviders:[StrappySession providerCatalog]];
  [[NSNotificationCenter defaultCenter] addObserver:self
    selector:@selector(refreshChanged:)
    name:StrappySessionModelCatalogRefreshDidStartNotification object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self
    selector:@selector(refreshChanged:)
    name:StrappySessionModelCatalogRefreshDidFinishNotification object:nil];
}

- (void)refreshChanged:(NSNotification *)notification
{
  NSString *message;

  message = [[notification userInfo] objectForKey:@"error"];
  if ([message isKindOfClass:[NSString class]] && ([message length] > 0U)) {
    UIAlertView *alert;
    alert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"Could Not Fetch Models", nil)
      message:message delegate:nil cancelButtonTitle:NSLocalizedString(@"OK", nil)
      otherButtonTitles:nil];
    [alert show];
  }
  [[self tableView] reloadData];
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
  (void)tableView; (void)section;
  return (NSInteger)[[self providers] count];
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  static NSString *identifier = @"ModelProviderCell";
  UITableViewCell *cell;
  NSDictionary *provider;
  NSString *providerIdentifier;

  cell = [tableView dequeueReusableCellWithIdentifier:identifier];
  if (cell == nil) cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:identifier];
  provider = [[self providers] objectAtIndex:(NSUInteger)[indexPath row]];
  providerIdentifier = StrappyProviderModelString(provider, @"id");
  [[cell textLabel] setText:StrappyProviderModelString(provider, @"name")];
  if ([providerIdentifier isEqualToString:@"openrouter"]) {
    [[cell detailTextLabel] setText:NSLocalizedString(@"Fetch the latest model catalog", nil)];
    if ([StrappySession isModelCatalogRefreshInFlight]) {
      [cell setAccessoryView:StrappyActivityAccessoryView([UIColor grayColor])];
      [cell setAccessoryType:UITableViewCellAccessoryNone];
    } else {
      [cell setAccessoryView:nil];
      [cell setAccessoryType:UITableViewCellAccessoryNone];
    }
  } else {
    [[cell detailTextLabel] setText:NSLocalizedString(@"Add and edit provider models", nil)];
    [cell setAccessoryView:nil];
    [cell setAccessoryType:UITableViewCellAccessoryDisclosureIndicator];
  }
  [[cell textLabel] setEnabled:[[provider objectForKey:@"available"] boolValue]];
  [[cell detailTextLabel] setEnabled:[[provider objectForKey:@"available"] boolValue]];
  return cell;
}

- (NSIndexPath *)tableView:(UITableView *)tableView willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *provider;

  (void)tableView;
  provider = [[self providers] objectAtIndex:(NSUInteger)[indexPath row]];
  if (![[provider objectForKey:@"available"] boolValue]) return nil;
  if ([StrappyProviderModelString(provider, @"id") isEqualToString:@"openrouter"] &&
      [StrappySession isModelCatalogRefreshInFlight]) return nil;
  return indexPath;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *provider;
  NSString *identifier;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  provider = [[self providers] objectAtIndex:(NSUInteger)[indexPath row]];
  identifier = StrappyProviderModelString(provider, @"id");
  if ([identifier isEqualToString:@"openrouter"]) {
    NSError *error;
    error = nil;
    if (![StrappySession beginOpenRouterModelCatalogRefreshWithError:&error]) {
      UIAlertView *alert;
      alert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"Could Not Fetch Models", nil)
        message:[error localizedDescription] delegate:nil
        cancelButtonTitle:NSLocalizedString(@"OK", nil) otherButtonTitles:nil];
      [alert show];
    }
    return;
  }
  [[self navigationController] pushViewController:
    [[StrappyManualModelsTableViewController alloc]
      initWithProviderIdentifier:identifier
      title:StrappyProviderModelString(provider, @"name")]
    animated:YES];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end

@implementation StrappyManualModelsTableViewController

- (id)initWithProviderIdentifier:(NSString *)providerIdentifier title:(NSString *)title
{
  if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
    [self setProviderIdentifier:providerIdentifier];
    [[self navigationItem] setTitle:title];
  }
  return self;
}

- (void)viewDidLoad
{
  [super viewDidLoad];
  [[self navigationItem] setRightBarButtonItem:[[UIBarButtonItem alloc]
    initWithBarButtonSystemItem:UIBarButtonSystemItemAdd target:self
    action:@selector(addModel:)]];
  [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(reloadModels)
    name:StrappySessionModelCatalogDidChangeNotification object:nil];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [self reloadModels];
}

- (void)reloadModels
{
  NSArray *catalog;
  NSMutableArray *rows;
  NSUInteger index;

  catalog = [StrappySession modelCatalogWithError:nil];
  rows = [NSMutableArray array];
  for (index = 0U; index < [catalog count]; index++) {
    NSDictionary *model;
    model = [catalog objectAtIndex:index];
    if ([StrappyProviderModelString(model, @"provider_id")
          isEqualToString:[self providerIdentifier]]) [rows addObject:model];
  }
  [self setModels:rows];
  [[self tableView] reloadData];
}

- (void)addModel:(id)sender
{
  (void)sender;
  [[self navigationController] pushViewController:
    [[StrappyManualModelTableViewController alloc]
      initWithProviderIdentifier:[self providerIdentifier] model:nil]
    animated:YES];
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
  (void)tableView; (void)section;
  return (NSInteger)[[self models] count];
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  static NSString *identifier = @"ManualModelCell";
  UITableViewCell *cell;
  NSDictionary *model;
  NSString *name;

  cell = [tableView dequeueReusableCellWithIdentifier:identifier];
  if (cell == nil) cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:identifier];
  model = [[self models] objectAtIndex:(NSUInteger)[indexPath row]];
  name = StrappyProviderModelString(model, @"name");
  [[cell textLabel] setText:([name length] > 0U) ? name : StrappyProviderModelString(model, @"wire_model_id")];
  [[cell detailTextLabel] setText:StrappyProviderModelString(model, @"wire_model_id")];
  [cell setAccessoryType:UITableViewCellAccessoryDisclosureIndicator];
  return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *model;
  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  model = [[self models] objectAtIndex:(NSUInteger)[indexPath row]];
  [[self navigationController] pushViewController:
    [[StrappyManualModelTableViewController alloc]
      initWithProviderIdentifier:[self providerIdentifier] model:model]
    animated:YES];
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end

@implementation StrappyManualModelTableViewController

- (id)initWithProviderIdentifier:(NSString *)providerIdentifier model:(NSDictionary *)model
{
  if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
    [self setProviderIdentifier:providerIdentifier];
    [self setModel:model];
    [[self navigationItem] setTitle:(model == nil) ?
      NSLocalizedString(@"Add Model", nil) : NSLocalizedString(@"Edit Model", nil)];
  }
  return self;
}

- (UITextField *)modelField:(NSString *)placeholder text:(NSString *)text
{
  UITextField *field;
  field = [[UITextField alloc] initWithFrame:CGRectZero];
  [field setPlaceholder:placeholder];
  [field setText:(text != nil) ? text : @""];
  [field setDelegate:self];
  [field setAutocorrectionType:UITextAutocorrectionTypeNo];
  [field setAutocapitalizationType:UITextAutocapitalizationTypeNone];
  [field setClearButtonMode:UITextFieldViewModeWhileEditing];
  return field;
}

- (NSString *)priceTextForKey:(NSString *)key
{
  NSString *value;
  value = StrappyProviderModelString([self model], key);
  return ([value length] > 0U) ?
    [NSString stringWithFormat:@"%.12g", [value doubleValue] * 1000000.0] : @"";
}

- (void)viewDidLoad
{
  NSDictionary *model;
  NSArray *bundledModels;
  NSUInteger bundledIndex;
  BOOL creating;
  [super viewDidLoad];
  model = [self model];
  creating = (model == nil);
  bundledModels = [StrappySession bundledModelCatalogForProviderIdentifier:
    [self providerIdentifier] error:nil];
  for (bundledIndex = 0U; bundledIndex < [bundledModels count];
       bundledIndex++) {
    if ([StrappyProviderModelString([bundledModels objectAtIndex:bundledIndex],
          @"wire_model_id") isEqualToString:
          StrappyProviderModelString(model, @"wire_model_id")]) {
      [self setBuiltIn:YES];
      break;
    }
  }
  [self setFields:[NSArray arrayWithObjects:
    [self modelField:NSLocalizedString(@"Model ID (Required)", nil) text:StrappyProviderModelString(model, @"wire_model_id")],
    [self modelField:NSLocalizedString(@"Name (Optional)", nil) text:StrappyProviderModelString(model, @"name")],
    [self modelField:NSLocalizedString(@"Context Tokens (Optional)", nil) text:StrappyProviderModelString(model, @"context_length")],
    [self modelField:NSLocalizedString(@"Max Output Tokens (Optional)", nil) text:StrappyProviderModelString(model, @"top_provider_max_completion_tokens")],
    [self modelField:NSLocalizedString(@"Input $/1M (Optional)", nil) text:[self priceTextForKey:@"pricing_prompt"]],
    [self modelField:NSLocalizedString(@"Output $/1M (Optional)", nil) text:[self priceTextForKey:@"pricing_completion"]],
    [self modelField:NSLocalizedString(@"Cache Read $/1M (Optional)", nil) text:[self priceTextForKey:@"pricing_input_cache_read"]],
    [self modelField:NSLocalizedString(@"Cache Write $/1M (Optional)", nil) text:[self priceTextForKey:@"pricing_input_cache_write"]], nil]];
  [[[self fields] objectAtIndex:0U] setEnabled:creating];
  if ([self builtIn]) {
    NSUInteger fieldIndex;
    for (fieldIndex = 0U; fieldIndex < [[self fields] count]; fieldIndex++)
      [[[self fields] objectAtIndex:fieldIndex] setEnabled:NO];
  }
  [self setReasoningSwitch:[[UISwitch alloc] initWithFrame:CGRectZero]];
  [[self reasoningSwitch] setOn:[[model objectForKey:@"reasoning_enabled"] boolValue]];
  [[self reasoningSwitch] setEnabled:![self builtIn]];
  [self setImagesSwitch:[[UISwitch alloc] initWithFrame:CGRectZero]];
  [[self imagesSwitch] setOn:[[model objectForKey:@"image_input_enabled"] boolValue]];
  [[self imagesSwitch] setEnabled:![self builtIn]];
  if (![self builtIn]) {
    [[self navigationItem] setRightBarButtonItem:[[UIBarButtonItem alloc]
      initWithBarButtonSystemItem:UIBarButtonSystemItemSave target:self
      action:@selector(saveModel:)]];
  }
}

- (NSString *)priceForFieldAtIndex:(NSUInteger)index valid:(BOOL *)valid
{
  NSString *text;
  const char *bytes;
  char *end;
  double value;
  text = [[[[self fields] objectAtIndex:index] text]
    stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if ([text length] == 0U) return nil;
  bytes = [text UTF8String];
  end = NULL;
  errno = 0;
  value = strtod(bytes, &end);
  if ((errno == ERANGE) || (end == bytes) || (end == NULL) ||
      (*end != '\0') || !isfinite(value) || (value < 0.0)) {
    *valid = NO;
    return nil;
  }
  return [NSString stringWithFormat:@"%.17g", value / 1000000.0];
}

- (void)saveModel:(id)sender
{
  NSString *wireID;
  NSString *name;
  NSString *inputPrice;
  NSString *outputPrice;
  NSString *readPrice;
  NSString *writePrice;
  BOOL valid;
  NSError *error;
  BOOL success;
  (void)sender;
  wireID = [[[[self fields] objectAtIndex:0U] text]
    stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  name = [[[[self fields] objectAtIndex:1U] text]
    stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if ([wireID length] == 0U) { [self showMessage:NSLocalizedString(@"Enter a Model ID before saving.", nil) title:NSLocalizedString(@"Model ID Is Required", nil)]; return; }
  valid = YES;
  inputPrice = [self priceForFieldAtIndex:4U valid:&valid];
  outputPrice = valid ? [self priceForFieldAtIndex:5U valid:&valid] : nil;
  readPrice = valid ? [self priceForFieldAtIndex:6U valid:&valid] : nil;
  writePrice = valid ? [self priceForFieldAtIndex:7U valid:&valid] : nil;
  if (!valid) { [self showMessage:NSLocalizedString(@"Prices must be numbers greater than or equal to zero.", nil) title:NSLocalizedString(@"Invalid Price", nil)]; return; }
  error = nil;
  if ([self model] == nil) {
    success = [StrappySession createManualModelForProviderIdentifier:[self providerIdentifier]
      wireModelID:wireID displayName:name
      contextWindowTokens:[[[[self fields] objectAtIndex:2U] text] longLongValue]
      maxOutputTokens:[[[[self fields] objectAtIndex:3U] text] longLongValue]
      reasoningEnabled:[[self reasoningSwitch] isOn] imageInputEnabled:[[self imagesSwitch] isOn]
      localFunctionsEnabled:YES inputPricePerToken:inputPrice outputPricePerToken:outputPrice
      cacheReadPricePerToken:readPrice cacheWritePricePerToken:writePrice error:&error] != nil;
  } else {
    success = [StrappySession updateManualModelForProviderIdentifier:[self providerIdentifier]
      wireModelID:wireID displayName:name
      contextWindowTokens:[[[[self fields] objectAtIndex:2U] text] longLongValue]
      maxOutputTokens:[[[[self fields] objectAtIndex:3U] text] longLongValue]
      reasoningEnabled:[[self reasoningSwitch] isOn] imageInputEnabled:[[self imagesSwitch] isOn]
      localFunctionsEnabled:YES inputPricePerToken:inputPrice outputPricePerToken:outputPrice
      cacheReadPricePerToken:readPrice cacheWritePricePerToken:writePrice error:&error];
  }
  if (!success) { [self showMessage:[error localizedDescription] title:NSLocalizedString(@"Could Not Save Model", nil)]; return; }
  [[self navigationController] popViewControllerAnimated:YES];
}

- (void)showMessage:(NSString *)message title:(NSString *)title
{
  UIAlertView *alert;
  alert = [[UIAlertView alloc] initWithTitle:title message:message delegate:nil
    cancelButtonTitle:NSLocalizedString(@"OK", nil) otherButtonTitles:nil];
  [alert show];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return (([self model] == nil) || [self builtIn]) ? 2 : 3;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
  (void)tableView;
  if (section == 0) return 8;
  if (section == 1) return 2;
  return 1;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell;
  if ([indexPath section] == 0) {
    UITextField *field;
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    field = [[self fields] objectAtIndex:(NSUInteger)[indexPath row]];
    [field setFrame:CGRectInset([[cell contentView] bounds], 15.0f, 0.0f)];
    [field setAutoresizingMask:UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight];
    [[cell contentView] addSubview:field];
    [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
    return cell;
  }
  cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
  if ([indexPath section] == 1) {
    [[cell textLabel] setText:([indexPath row] == 0) ? NSLocalizedString(@"Reasoning", nil) : NSLocalizedString(@"Image Input", nil)];
    [cell setAccessoryView:([indexPath row] == 0) ? [self reasoningSwitch] : [self imagesSwitch]];
    [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
  } else {
    [[cell textLabel] setText:NSLocalizedString(@"Delete Model…", nil)];
    [[cell textLabel] setTextColor:[UIColor redColor]];
  }
  return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  UIAlertView *alert;
  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  if ([indexPath section] != 2) return;
  alert = [[UIAlertView alloc] initWithTitle:NSLocalizedString(@"Delete Model?", nil)
    message:StrappyProviderModelString([self model], @"wire_model_id") delegate:self
    cancelButtonTitle:NSLocalizedString(@"Cancel", nil)
    otherButtonTitles:NSLocalizedString(@"Delete", nil), nil];
  [alert show];
}

- (void)alertView:(UIAlertView *)alertView clickedButtonAtIndex:(NSInteger)buttonIndex
{
  NSError *error;
  (void)alertView;
  if (buttonIndex == 0) return;
  error = nil;
  if (![StrappySession archiveManualModelForProviderIdentifier:[self providerIdentifier]
      wireModelID:StrappyProviderModelString([self model], @"wire_model_id") error:&error]) {
    [self showMessage:[error localizedDescription] title:NSLocalizedString(@"Could Not Delete Model", nil)];
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
  NSUInteger index;
  for (index = 0U; index < [[self fields] count]; index++)
    [[[self fields] objectAtIndex:index] setDelegate:nil];
}

@end
