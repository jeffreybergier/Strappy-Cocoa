#import "StrappySessionOptionsTableViewController.h"

#import "StrappyAppearance.h"
#import "XPUIKit.h"

static NSArray *StrappyPromptSearchProviders(void)
{
  return [NSArray arrayWithObjects:
    StrappyWebProviderAuto,
    StrappyWebProviderNative,
    StrappyWebProviderExa,
    StrappyWebProviderParallel,
    nil];
}

static NSString *StrappyPromptWebProviderTitle(NSString *webProvider)
{
  if ([webProvider isEqualToString:StrappyWebProviderAuto]) {
    return NSLocalizedString(@"Auto", nil);
  }
  if ([webProvider isEqualToString:StrappyWebProviderNative]) {
    return NSLocalizedString(@"Native", nil);
  }
  if ([webProvider isEqualToString:StrappyWebProviderExa]) {
    return @"Exa";
  }
  if ([webProvider isEqualToString:StrappyWebProviderParallel]) {
    return @"Parallel";
  }
  return NSLocalizedString(@"None", nil);
}

static NSString *StrappyMessageModelStringForRow(NSDictionary *row,
                                                 NSString *key)
{
  id value;

  if (![row isKindOfClass:[NSDictionary class]]) {
    return @"";
  }

  value = [row objectForKey:key];
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

static NSString *StrappyMessageModelDisplayNameForRow(NSDictionary *row)
{
  NSString *name;
  NSString *modelIdentifier;

  name = StrappyMessageModelStringForRow(row, @"name");
  if ([name length] > 0U) {
    return name;
  }

  modelIdentifier = StrappyMessageModelStringForRow(row, @"id");
  return ([modelIdentifier length] > 0U)
    ? modelIdentifier
    : NSLocalizedString(@"Model", nil);
}

@class StrappySessionOptionsTableViewController;
@class StrappySessionDebugOptionsTableViewController;

enum {
  kStrappyPromptOptionsSectionModels = 0,
  kStrappyPromptOptionsSectionAssistantSet,
  kStrappyPromptOptionsSectionAvailableTools,
  kStrappyPromptOptionsSectionDebug,
  kStrappyPromptOptionsSectionCount
};

enum {
  kStrappyPromptDebugSectionLimits = 0,
  kStrappyPromptDebugSectionSearchProvider,
  kStrappyPromptDebugSectionCount
};

enum {
  kStrappyPromptDebugLimitRowLimitToOneTool = 0,
  kStrappyPromptDebugLimitRowAnswerQuality,
  kStrappyPromptDebugLimitRowRoundLimit,
  kStrappyPromptDebugLimitRowCount
};

static BOOL StrappyPromptParseLimit(NSString *text, NSUInteger *limitOut)
{
  NSString *trimmed;
  NSCharacterSet *invalidCharacters;
  long long value;

  trimmed = [text stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  invalidCharacters = [[NSCharacterSet characterSetWithCharactersInString:
    @"0123456789"] invertedSet];
  if (([trimmed length] == 0U) ||
      ([trimmed rangeOfCharacterFromSet:invalidCharacters].location !=
       NSNotFound)) {
    return NO;
  }
  value = [trimmed longLongValue];
  if ((value < 1LL) ||
      (value > (long long)StrappySessionMaximumLimit)) {
    return NO;
  }
  if (limitOut != NULL) {
    *limitOut = (NSUInteger)value;
  }
  return YES;
}

@interface StrappySessionOptionsTableViewController ()
@property (nonatomic, assign) id<StrappySessionOptionsTableViewControllerDelegate> optionsDelegate;
@property (nonatomic, copy) NSArray *assistantSets;
@property (nonatomic, copy) NSArray *models;
@property (nonatomic, copy) StrappySessionOptions *sessionOptions;
@property (nonatomic, strong) UISwitch *webSearchSwitch;
@property (nonatomic, strong) UISwitch *bashSwitch;
@property (nonatomic, assign) BOOL presentedModally;
- (instancetype)initWithOptionsDelegate:
    (id<StrappySessionOptionsTableViewControllerDelegate>)optionsDelegate
                       presentedModally:(BOOL)presentedModally;
- (void)reloadOptionsSnapshot;
- (void)reloadOptionsFromDelegate;
@end

@interface StrappySessionDebugOptionsTableViewController :
  UITableViewController <UITextFieldDelegate>
@property (nonatomic, assign) id<StrappySessionOptionsTableViewControllerDelegate> optionsDelegate;
@property (nonatomic, copy) StrappySessionOptions *sessionOptions;
@property (nonatomic, strong) UISwitch *limitToOneToolSwitch;
@property (nonatomic, strong) UISwitch *answerQualitySwitch;
@property (nonatomic, strong) UITextField *roundLimitField;
- (instancetype)initWithOptionsDelegate:
    (id<StrappySessionOptionsTableViewControllerDelegate>)optionsDelegate;
- (void)reloadOptionsSnapshot;
- (void)reloadOptionsFromDelegate;
@end

@implementation StrappySessionOptionsTableViewController

- (instancetype)initWithOptionsDelegate:
    (id<StrappySessionOptionsTableViewControllerDelegate>)optionsDelegate
                       presentedModally:(BOOL)presentedModally
{
  if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
    [self setOptionsDelegate:optionsDelegate];
    [self setPresentedModally:presentedModally];
    [[self navigationItem] setTitle:NSLocalizedString(@"Session Options", nil)];
    [self reloadOptionsSnapshot];
  }
  return self;
}

- (void)viewDidLoad
{
  UISwitch *webSearchSwitch;
  UISwitch *bashSwitch;

  [super viewDidLoad];

  webSearchSwitch = [[UISwitch alloc] initWithFrame:CGRectZero];
  [webSearchSwitch addTarget:self
                      action:@selector(webSearchSwitchChanged:)
            forControlEvents:UIControlEventValueChanged];
  [self setWebSearchSwitch:webSearchSwitch];

  bashSwitch = [[UISwitch alloc] initWithFrame:CGRectZero];
  [bashSwitch addTarget:self
                 action:@selector(bashSwitchChanged:)
       forControlEvents:UIControlEventValueChanged];
  [self setBashSwitch:bashSwitch];

  [self reloadOptionsFromDelegate];

  if ([self presentedModally]) {
    [[self navigationItem] setRightBarButtonItem:
      [[UIBarButtonItem alloc]
        initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                             target:self
                             action:@selector(doneAction:)]];
    [StrappyAppearance applyLegacyTintToBarButtonItem:
      [[self navigationItem] rightBarButtonItem]];
  }
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [self reloadOptionsFromDelegate];
}

- (void)reloadOptionsSnapshot
{
  id<StrappySessionOptionsTableViewControllerDelegate> optionsDelegate;

  optionsDelegate = [self optionsDelegate];
  [self setAssistantSets:
    (optionsDelegate != nil)
      ? [optionsDelegate currentAssistantSets]
      : [NSArray array]];
  [self setModels:
    (optionsDelegate != nil)
      ? [optionsDelegate currentAllowedModels]
      : [NSArray array]];
  [self setSessionOptions:(optionsDelegate != nil)
    ? [optionsDelegate sessionOptions] : nil];
}

- (void)reloadOptionsFromDelegate
{
  [self reloadOptionsSnapshot];
  [[self webSearchSwitch]
    setOn:[[self sessionOptions] webSearchEnabled]
  animated:NO];
  [[self bashSwitch] setOn:[[self sessionOptions] bashEnabled] animated:NO];
  [[self bashSwitch] setEnabled:YES];
  [[self tableView] reloadData];
}

- (void)doneAction:(id)sender
{
  (void)sender;
  [[self optionsDelegate] dismissOptionsControllerAnimated:YES];
}

- (void)webSearchSwitchChanged:(UISwitch *)sender
{
  id<StrappySessionOptionsTableViewControllerDelegate> optionsDelegate;
  StrappySessionOptions *options;

  optionsDelegate = [self optionsDelegate];
  if (optionsDelegate != nil) {
    options = [[optionsDelegate sessionOptions] copy];
    [options setWebSearchEnabled:[sender isOn]];
    (void)[optionsDelegate updateSessionOptions:options
                                           changedFields:
                                             StrappySessionOptionWebSearch];
    [self setSessionOptions:[optionsDelegate sessionOptions]];
  }
  [sender setOn:[[self sessionOptions] webSearchEnabled] animated:YES];
}

- (void)bashSwitchChanged:(UISwitch *)sender
{
  id<StrappySessionOptionsTableViewControllerDelegate> optionsDelegate;
  StrappySessionOptions *options;

  optionsDelegate = [self optionsDelegate];
  if (optionsDelegate != nil) {
    options = [[optionsDelegate sessionOptions] copy];
    [options setBashEnabled:[sender isOn]];
    (void)[optionsDelegate updateSessionOptions:options
                                           changedFields:
                                             StrappySessionOptionBash];
    [self setSessionOptions:[optionsDelegate sessionOptions]];
  }
  [sender setOn:[[self sessionOptions] bashEnabled] animated:YES];
  [sender setEnabled:YES];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return kStrappyPromptOptionsSectionCount;
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
  (void)tableView;
  if (section == kStrappyPromptOptionsSectionAssistantSet) {
    return (NSInteger)[[self assistantSets] count];
  }
  if (section == kStrappyPromptOptionsSectionModels) {
    return (NSInteger)[[self models] count];
  }
  if (section == kStrappyPromptOptionsSectionAvailableTools) {
    return 2;
  }
  if (section == kStrappyPromptOptionsSectionDebug) {
    return 1;
  }
  return 0;
}

- (NSString *)tableView:(UITableView *)tableView
titleForHeaderInSection:(NSInteger)section
{
  (void)tableView;
  if (section == kStrappyPromptOptionsSectionAssistantSet) {
    return ([[self assistantSets] count] > 0U)
      ? NSLocalizedString(@"Assistant", nil)
      : nil;
  }
  if (section == kStrappyPromptOptionsSectionModels) {
    return ([[self models] count] > 0U) ? NSLocalizedString(@"Model", nil) : nil;
  }
  if (section == kStrappyPromptOptionsSectionAvailableTools) {
    return NSLocalizedString(@"Tools", nil);
  }
  return nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell;

  if ([indexPath section] == kStrappyPromptOptionsSectionAssistantSet) {
    NSDictionary *assistantSet;
    NSString *identifier;
    NSString *name;
    NSNumber *available;
    BOOL enabled;

    cell = [tableView dequeueReusableCellWithIdentifier:@"AssistantSetCell"];
    if (cell == nil) {
      cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                    reuseIdentifier:@"AssistantSetCell"];
      [[cell textLabel] setNumberOfLines:1];
    }
    assistantSet = [[self assistantSets]
      objectAtIndex:(NSUInteger)[indexPath row]];
    identifier = StrappyMessageModelStringForRow(assistantSet, @"id");
    name = StrappyMessageModelStringForRow(assistantSet, @"name");
    available = [assistantSet objectForKey:@"available"];
    enabled = [available isKindOfClass:[NSNumber class]] &&
      [available boolValue];
    [[cell textLabel] setText:([name length] > 0U) ? name : identifier];
    [[cell textLabel] setTextColor:enabled ?
      [UIColor blackColor] : [UIColor grayColor]];
    [cell setSelectionStyle:enabled ?
      UITableViewCellSelectionStyleBlue : UITableViewCellSelectionStyleNone];
    [cell setAccessoryType:
      [identifier isEqualToString:
        [[self sessionOptions] assistantSetIdentifier]]
        ? UITableViewCellAccessoryCheckmark
        : UITableViewCellAccessoryNone];
    return cell;
  }

  if ([indexPath section] == kStrappyPromptOptionsSectionAvailableTools) {
    if ([indexPath row] == 0) {
      cell = [tableView dequeueReusableCellWithIdentifier:@"WebSearchCell"];
      if (cell == nil) {
        cell = [[UITableViewCell alloc]
          initWithStyle:UITableViewCellStyleDefault
         reuseIdentifier:@"WebSearchCell"];
        [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
        [[cell textLabel] setNumberOfLines:1];
      }
      [[cell textLabel] setText:NSLocalizedString(@"Enable Web Search", nil)];
      [[cell textLabel] setTextColor:[UIColor blackColor]];
      [[self webSearchSwitch]
        setOn:[[self sessionOptions] webSearchEnabled]
      animated:NO];
      [cell setAccessoryView:[self webSearchSwitch]];
      return cell;
    }

    cell = [tableView dequeueReusableCellWithIdentifier:@"BashCell"];
    if (cell == nil) {
      cell = [[UITableViewCell alloc]
        initWithStyle:UITableViewCellStyleDefault
       reuseIdentifier:@"BashCell"];
      [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
      [[cell textLabel] setNumberOfLines:1];
    }
    [[cell textLabel] setText:NSLocalizedString(@"Enable Bash", nil)];
    [[cell textLabel] setTextColor:[UIColor blackColor]];
    [[self bashSwitch] setOn:[[self sessionOptions] bashEnabled] animated:NO];
    [[self bashSwitch] setEnabled:YES];
    [cell setAccessoryView:[self bashSwitch]];
    return cell;
  }

  if ([indexPath section] == kStrappyPromptOptionsSectionDebug) {
    cell = [tableView dequeueReusableCellWithIdentifier:@"DebugCell"];
    if (cell == nil) {
      cell = [[UITableViewCell alloc]
        initWithStyle:UITableViewCellStyleDefault
       reuseIdentifier:@"DebugCell"];
      [[cell textLabel] setNumberOfLines:1];
    }
    [[cell textLabel] setText:NSLocalizedString(@"Advanced", nil)];
    [[cell textLabel] setTextColor:[UIColor blackColor]];
    [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
    [cell setAccessoryView:nil];
    [cell setAccessoryType:UITableViewCellAccessoryDisclosureIndicator];
    return cell;
  }

  cell = [tableView dequeueReusableCellWithIdentifier:@"ModelCell"];
  if (cell == nil) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                  reuseIdentifier:@"ModelCell"];
    [[cell textLabel] setNumberOfLines:1];
  }

  {
    NSDictionary *model;
    NSString *identifier;

    model = [[self models] objectAtIndex:(NSUInteger)[indexPath row]];
    identifier = StrappyMessageModelStringForRow(model, @"id");
    [[cell textLabel] setText:StrappyMessageModelDisplayNameForRow(model)];
    [[cell textLabel] setTextColor:[UIColor blackColor]];
    [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
    [cell setAccessoryView:nil];
    [cell setAccessoryType:
      [identifier isEqualToString:[[self sessionOptions] modelIdentifier]]
        ? UITableViewCellAccessoryCheckmark
        : UITableViewCellAccessoryNone];
  }
  return cell;
}

- (NSIndexPath *)tableView:(UITableView *)tableView
  willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  if ([indexPath section] == kStrappyPromptOptionsSectionAssistantSet) {
    NSDictionary *assistantSet;
    NSNumber *available;

    if ((NSUInteger)[indexPath row] >= [[self assistantSets] count]) {
      return nil;
    }
    assistantSet = [[self assistantSets]
      objectAtIndex:(NSUInteger)[indexPath row]];
    available = [assistantSet objectForKey:@"available"];
    return ([available isKindOfClass:[NSNumber class]] &&
            [available boolValue]) ? indexPath : nil;
  }
  if ([indexPath section] == kStrappyPromptOptionsSectionDebug) {
    return ([indexPath row] == 0) ? indexPath : nil;
  }
  if ([indexPath section] == kStrappyPromptOptionsSectionAvailableTools) {
    return nil;
  }
  if ([indexPath section] != kStrappyPromptOptionsSectionModels) {
    return nil;
  }
  return ([[self models] count] > 0U) ? indexPath : nil;
}

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  NSDictionary *model;
  NSString *modelIdentifier;

  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  if ([indexPath section] == kStrappyPromptOptionsSectionDebug) {
    StrappySessionDebugOptionsTableViewController *debugController;

    debugController =
      [[StrappySessionDebugOptionsTableViewController alloc]
        initWithOptionsDelegate:[self optionsDelegate]];
    [[self navigationController] pushViewController:debugController
                                           animated:YES];
    return;
  }
  if ([indexPath section] == kStrappyPromptOptionsSectionAssistantSet) {
    NSDictionary *assistantSet;
    NSString *assistantSetIdentifier;
    StrappySessionOptions *options;

    if ((NSUInteger)[indexPath row] >= [[self assistantSets] count]) {
      return;
    }
    assistantSet = [[self assistantSets]
      objectAtIndex:(NSUInteger)[indexPath row]];
    assistantSetIdentifier =
      StrappyMessageModelStringForRow(assistantSet, @"id");
    if ([assistantSetIdentifier length] == 0U) {
      return;
    }
    options = [[[self optionsDelegate] sessionOptions] copy];
    [options setAssistantSetIdentifier:assistantSetIdentifier];
    (void)[[self optionsDelegate]
      updateSessionOptions:options
             changedFields:StrappySessionOptionAssistantSet];
    [self reloadOptionsFromDelegate];
    return;
  }
  if (([indexPath section] != kStrappyPromptOptionsSectionModels) ||
      ([[self models] count] == 0U)) {
    return;
  }

  model = [[self models] objectAtIndex:(NSUInteger)[indexPath row]];
  modelIdentifier = StrappyMessageModelStringForRow(model, @"id");
  if ([modelIdentifier length] == 0U) {
    return;
  }

  {
    StrappySessionOptions *options;

    options = [[[self optionsDelegate] sessionOptions] copy];
    [options setModelIdentifier:modelIdentifier];
    if ([[self optionsDelegate]
          updateSessionOptions:options
                 changedFields:StrappySessionOptionModel]) {
      [self setSessionOptions:[[self optionsDelegate] sessionOptions]];
      [[self tableView] reloadSections:
        [NSIndexSet indexSetWithIndex:kStrappyPromptOptionsSectionModels]
                    withRowAnimation:UITableViewRowAnimationNone];
    } else {
      [self reloadOptionsFromDelegate];
    }
  }
}

@end

@implementation StrappySessionDebugOptionsTableViewController

- (instancetype)initWithOptionsDelegate:
    (id<StrappySessionOptionsTableViewControllerDelegate>)optionsDelegate
{
  if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
    [self setOptionsDelegate:optionsDelegate];
    [[self navigationItem] setTitle:NSLocalizedString(@"Advanced", nil)];
    [self reloadOptionsSnapshot];
  }
  return self;
}

- (void)viewDidLoad
{
  UISwitch *limitToOneToolSwitch;
  UISwitch *answerQualitySwitch;
  UITextField *roundLimitField;
  UIToolbar *keyboardToolbar;
  UIBarButtonItem *flexibleItem;
  UIBarButtonItem *doneItem;

  [super viewDidLoad];

  limitToOneToolSwitch = [[UISwitch alloc] initWithFrame:CGRectZero];
  [limitToOneToolSwitch addTarget:self
                           action:@selector(limitToOneToolSwitchChanged:)
                 forControlEvents:UIControlEventValueChanged];
  [self setLimitToOneToolSwitch:limitToOneToolSwitch];

  answerQualitySwitch = [[UISwitch alloc] initWithFrame:CGRectZero];
  [answerQualitySwitch addTarget:self
                          action:@selector(answerQualitySwitchChanged:)
                forControlEvents:UIControlEventValueChanged];
  [self setAnswerQualitySwitch:answerQualitySwitch];

  keyboardToolbar = [[UIToolbar alloc]
    initWithFrame:CGRectMake(0.0f, 0.0f, 320.0f, 44.0f)];
  flexibleItem = [[UIBarButtonItem alloc]
    initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                         target:nil
                         action:nil];
  doneItem = [[UIBarButtonItem alloc]
    initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                         target:self
                         action:@selector(limitFieldDoneAction:)];
  [StrappyAppearance applyLegacyTintToBarButtonItem:doneItem];
  [keyboardToolbar setItems:[NSArray arrayWithObjects:
    flexibleItem, doneItem, nil]];

  roundLimitField = [[UITextField alloc]
    initWithFrame:CGRectMake(0.0f, 0.0f, 72.0f, 30.0f)];
  [roundLimitField setKeyboardType:UIKeyboardTypeNumberPad];
  [roundLimitField XP_setTextAlignmentRight];
  [roundLimitField setContentVerticalAlignment:
    UIControlContentVerticalAlignmentCenter];
  [roundLimitField setDelegate:self];
  [roundLimitField setInputAccessoryView:keyboardToolbar];
  [roundLimitField addTarget:self
                       action:@selector(roundLimitFieldEditingDidEnd:)
             forControlEvents:UIControlEventEditingDidEnd];
  [self setRoundLimitField:roundLimitField];

  [self reloadOptionsFromDelegate];
}

- (void)viewWillAppear:(BOOL)animated
{
  [super viewWillAppear:animated];
  [self reloadOptionsFromDelegate];
}

- (void)reloadOptionsSnapshot
{
  id<StrappySessionOptionsTableViewControllerDelegate> optionsDelegate;

  optionsDelegate = [self optionsDelegate];
  [self setSessionOptions:(optionsDelegate != nil)
    ? [optionsDelegate sessionOptions] : nil];
}

- (void)reloadOptionsFromDelegate
{
  [self reloadOptionsSnapshot];
  [[self limitToOneToolSwitch]
    setOn:[[self sessionOptions] limitToOneTool]
  animated:NO];
  [[self answerQualitySwitch]
    setOn:[[self sessionOptions] answerQualityEnabled]
  animated:NO];
  [[self roundLimitField] setText:[NSString stringWithFormat:
    @"%lu", (unsigned long)[[self sessionOptions] roundLimit]]];
  [[self tableView] reloadData];
}

- (void)limitFieldDoneAction:(id)sender
{
  (void)sender;
  [[self view] endEditing:YES];
}

- (void)limitToOneToolSwitchChanged:(UISwitch *)sender
{
  id<StrappySessionOptionsTableViewControllerDelegate> optionsDelegate;
  StrappySessionOptions *options;

  optionsDelegate = [self optionsDelegate];
  if (optionsDelegate != nil) {
    options = [[optionsDelegate sessionOptions] copy];
    [options setLimitToOneTool:[sender isOn]];
    (void)[optionsDelegate updateSessionOptions:options
                                           changedFields:
                                             StrappySessionOptionLimitToOneTool];
    [self setSessionOptions:[optionsDelegate sessionOptions]];
  }
  [sender setOn:[[self sessionOptions] limitToOneTool] animated:YES];
}

- (void)roundLimitFieldEditingDidEnd:(UITextField *)sender
{
  id<StrappySessionOptionsTableViewControllerDelegate> optionsDelegate;
  StrappySessionOptions *options;
  NSUInteger limit;

  optionsDelegate = [self optionsDelegate];
  if ((optionsDelegate != nil) &&
      StrappyPromptParseLimit([sender text], &limit)) {
    options = [[optionsDelegate sessionOptions] copy];
    [options setRoundLimit:limit];
    (void)[optionsDelegate updateSessionOptions:options
                                           changedFields:
                                             StrappySessionOptionRoundLimit];
    [self setSessionOptions:[optionsDelegate sessionOptions]];
  }
  [sender setText:[NSString stringWithFormat:
    @"%lu", (unsigned long)[[self sessionOptions] roundLimit]]];
}

- (void)answerQualitySwitchChanged:(UISwitch *)sender
{
  id<StrappySessionOptionsTableViewControllerDelegate> optionsDelegate;
  StrappySessionOptions *options;

  optionsDelegate = [self optionsDelegate];
  if (optionsDelegate != nil) {
    options = [[optionsDelegate sessionOptions] copy];
    [options setAnswerQualityEnabled:[sender isOn]];
    (void)[optionsDelegate updateSessionOptions:options
                                           changedFields:
                                             StrappySessionOptionAnswerQuality];
    [self setSessionOptions:[optionsDelegate sessionOptions]];
  }
  [sender setOn:[[self sessionOptions] answerQualityEnabled] animated:YES];
}

- (BOOL)textFieldShouldReturn:(UITextField *)textField
{
  [textField resignFirstResponder];
  return YES;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
  (void)tableView;
  return kStrappyPromptDebugSectionCount;
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
  (void)tableView;
  if (section == kStrappyPromptDebugSectionLimits) {
    return kStrappyPromptDebugLimitRowCount;
  }
  if (section == kStrappyPromptDebugSectionSearchProvider) {
    return (NSInteger)[StrappyPromptSearchProviders() count];
  }
  return 0;
}

- (NSString *)tableView:(UITableView *)tableView
titleForHeaderInSection:(NSInteger)section
{
  (void)tableView;
  if (section == kStrappyPromptDebugSectionLimits) {
    return NSLocalizedString(@"Limits", nil);
  }
  if (section == kStrappyPromptDebugSectionSearchProvider) {
    return NSLocalizedString(@"Search Provider", nil);
  }
  return nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell;

  if (([indexPath section] == kStrappyPromptDebugSectionLimits) &&
      ([indexPath row] == kStrappyPromptDebugLimitRowLimitToOneTool)) {
    cell = [tableView dequeueReusableCellWithIdentifier:@"LimitOneToolCell"];
    if (cell == nil) {
      cell = [[UITableViewCell alloc]
        initWithStyle:UITableViewCellStyleDefault
       reuseIdentifier:@"LimitOneToolCell"];
      [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
      [[cell textLabel] setNumberOfLines:1];
    }
    [[cell textLabel] setText:NSLocalizedString(@"Limit to 1 Tool", nil)];
    [[cell textLabel] setTextColor:[UIColor blackColor]];
    [[self limitToOneToolSwitch]
      setOn:[[self sessionOptions] limitToOneTool]
    animated:NO];
    [cell setAccessoryType:UITableViewCellAccessoryNone];
    [cell setAccessoryView:[self limitToOneToolSwitch]];
    return cell;
  }

  if (([indexPath section] == kStrappyPromptDebugSectionLimits) &&
      ([indexPath row] == kStrappyPromptDebugLimitRowAnswerQuality)) {
    cell = [tableView dequeueReusableCellWithIdentifier:
      @"AnswerQualityCell"];
    if (cell == nil) {
      cell = [[UITableViewCell alloc]
        initWithStyle:UITableViewCellStyleDefault
       reuseIdentifier:@"AnswerQualityCell"];
      [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
      [[cell textLabel] setNumberOfLines:1];
    }
    [[cell textLabel] setText:NSLocalizedString(
      @"Answer Quality Check", nil)];
    [[cell textLabel] setTextColor:[UIColor blackColor]];
    [[self answerQualitySwitch]
      setOn:[[self sessionOptions] answerQualityEnabled]
    animated:NO];
    [cell setAccessoryType:UITableViewCellAccessoryNone];
    [cell setAccessoryView:[self answerQualitySwitch]];
    return cell;
  }

  if ([indexPath section] == kStrappyPromptDebugSectionLimits) {
    UITextField *limitField;
    NSString *title;

    limitField = [self roundLimitField];
    title = NSLocalizedString(@"Round Limit", nil);
    cell = [tableView dequeueReusableCellWithIdentifier:@"LimitValueCell"];
    if (cell == nil) {
      cell = [[UITableViewCell alloc]
        initWithStyle:UITableViewCellStyleDefault
       reuseIdentifier:@"LimitValueCell"];
      [cell setSelectionStyle:UITableViewCellSelectionStyleNone];
      [[cell textLabel] setNumberOfLines:1];
    }
    [[cell textLabel] setText:title];
    [[cell textLabel] setTextColor:[UIColor blackColor]];
    [limitField setAccessibilityLabel:title];
    [cell setAccessoryType:UITableViewCellAccessoryNone];
    [cell setAccessoryView:limitField];
    return cell;
  }

  {
    NSString *webProvider;

    cell = [tableView dequeueReusableCellWithIdentifier:@"WebProviderCell"];
    if (cell == nil) {
      cell = [[UITableViewCell alloc]
        initWithStyle:UITableViewCellStyleDefault
       reuseIdentifier:@"WebProviderCell"];
      [[cell textLabel] setNumberOfLines:1];
    }
    webProvider = [StrappyPromptSearchProviders()
      objectAtIndex:(NSUInteger)[indexPath row]];
    [[cell textLabel] setText:StrappyPromptWebProviderTitle(webProvider)];
    [[cell textLabel] setTextColor:[UIColor blackColor]];
    [cell setSelectionStyle:UITableViewCellSelectionStyleBlue];
    [cell setAccessoryView:nil];
    [cell setAccessoryType:
      [webProvider isEqualToString:[[self sessionOptions] webProvider]]
        ? UITableViewCellAccessoryCheckmark
        : UITableViewCellAccessoryNone];
    return cell;
  }
}

- (NSIndexPath *)tableView:(UITableView *)tableView
  willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  (void)tableView;
  if ([indexPath section] == kStrappyPromptDebugSectionSearchProvider) {
    return indexPath;
  }
  return nil;
}

- (void)tableView:(UITableView *)tableView
didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  [tableView deselectRowAtIndexPath:indexPath animated:YES];
  if ([indexPath section] == kStrappyPromptDebugSectionSearchProvider) {
    NSString *webProvider;
    StrappySessionOptions *options;

    if ((NSUInteger)[indexPath row] >=
        [StrappyPromptSearchProviders() count]) {
      return;
    }
    webProvider = [StrappyPromptSearchProviders()
      objectAtIndex:(NSUInteger)[indexPath row]];
    options = [[[self optionsDelegate] sessionOptions] copy];
    [options setWebProvider:webProvider];
    if ([[self optionsDelegate] updateSessionOptions:options
                                                changedFields:
                                                  StrappySessionOptionWebProvider]) {
      [self setSessionOptions:
        [[self optionsDelegate] sessionOptions]];
      [[self tableView] reloadSections:
        [NSIndexSet indexSetWithIndex:
          kStrappyPromptDebugSectionSearchProvider]
                    withRowAnimation:UITableViewRowAnimationNone];
    } else {
      [self reloadOptionsFromDelegate];
    }
  }
}

@end
