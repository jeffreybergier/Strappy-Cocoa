#import "StrappyPreferencesModelWhitelistView.h"

#import "XPAppKit.h"

static const CGFloat kStrappyModelControlHeight = 24.0;
static const CGFloat kStrappyModelDefaultPopupWidth = 260.0;

static NSString *StrappyModelWhitelistStringForRow(NSDictionary *row,
                                                   NSString *key)
{
  NSString *value;

  value = [row objectForKey:key];
  if (![value isKindOfClass:[NSString class]]) {
    return @"";
  }
  return value;
}

static BOOL StrappyModelWhitelistRowIsDefault(NSDictionary *row)
{
  NSNumber *selected;

  selected = [row objectForKey:@"selected"];
  return ([selected isKindOfClass:[NSNumber class]] && [selected boolValue]) ?
    YES : NO;
}

static BOOL StrappyModelWhitelistRowIsAllowed(NSDictionary *row)
{
  NSNumber *allowed;

  if (StrappyModelWhitelistRowIsDefault(row)) {
    return YES;
  }

  allowed = [row objectForKey:@"allowed"];
  return ([allowed isKindOfClass:[NSNumber class]] && [allowed boolValue]) ?
    YES : NO;
}

static NSString *StrappyModelWhitelistDisplayNameForRow(NSDictionary *row)
{
  NSString *name;

  name = StrappyModelWhitelistStringForRow(row, @"name");
  if ([name length] > 0U) {
    return name;
  }
  return StrappyModelWhitelistStringForRow(row, @"id");
}

static NSComparisonResult StrappyWhitelistCompareStrings(NSString *left,
                                                         NSString *right)
{
  if (![left isKindOfClass:[NSString class]]) {
    left = @"";
  }
  if (![right isKindOfClass:[NSString class]]) {
    right = @"";
  }
  return [left caseInsensitiveCompare:right];
}

static NSComparisonResult StrappyWhitelistCompareLongLong(long long left,
                                                          long long right)
{
  if (left < right) {
    return NSOrderedAscending;
  }
  if (left > right) {
    return NSOrderedDescending;
  }
  return NSOrderedSame;
}

static NSComparisonResult StrappyWhitelistCompareDouble(double left, double right)
{
  if (left < right) {
    return NSOrderedAscending;
  }
  if (left > right) {
    return NSOrderedDescending;
  }
  return NSOrderedSame;
}

@implementation StrappyPreferencesModelWhitelistView

- (id)initWithFrame:(NSRect)frame
{
  return [self initWithFrame:frame
                      target:nil
                  dataSource:nil
                    delegate:nil];
}

- (id)initWithFrame:(NSRect)frame
             target:(id)target
         dataSource:(id)dataSource
           delegate:(id)delegate
{
  return [super initWithFrame:frame
                       target:target
                refreshAction:@selector(refreshModels:)
                 searchAction:NULL
               refreshToolTip:NSLocalizedString(
                 @"Refresh the OpenRouter model list.", nil)
                   dataSource:dataSource
                     delegate:delegate];
}

- (CGFloat)topAccessoryTrailingControlWidth
{
  return kStrappyModelDefaultPopupWidth;
}

- (void)configureTopAccessoryView:(NSView *)view target:(id)target
{
  NSRect bounds;
  CGFloat controlY;

  bounds = [view bounds];
  controlY = NSMaxY(bounds) - kStrappyModelControlHeight;

  defaultModelPopUpButton_ =
    [[NSPopUpButton alloc] initWithFrame:NSMakeRect(NSWidth(bounds) -
                                                      kStrappyModelDefaultPopupWidth,
                                                    controlY,
                                                    kStrappyModelDefaultPopupWidth,
                                                    kStrappyModelControlHeight)
                               pullsDown:NO];
  [defaultModelPopUpButton_ setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
  [defaultModelPopUpButton_ setBezelStyle:XPBezelStyleRounded];
  [defaultModelPopUpButton_ setToolTip:
    NSLocalizedString(@"Default model for new chats", nil)];
  [defaultModelPopUpButton_ setTarget:target];
  [defaultModelPopUpButton_ setAction:@selector(defaultModelPopUpButtonChanged:)];
  [[defaultModelPopUpButton_ menu] setAutoenablesItems:NO];
  [view addSubview:defaultModelPopUpButton_];
}

- (void)addTableColumnsToTableView:(NSTableView *)tableView
{
  NSTableColumn *allowedColumn;
  NSTableColumn *nameColumn;
  NSTableColumn *idColumn;
  NSTableColumn *contextColumn;
  NSTableColumn *promptColumn;
  NSTableColumn *completionColumn;
  NSButtonCell *allowedCell;
  NSTextFieldCell *textCell;
  NSTextFieldCell *rightCell;

  allowedColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"model_allowed"] autorelease];
  [[allowedColumn headerCell] setStringValue:NSLocalizedString(@"Use", nil)];
  [allowedColumn setWidth:44.0];
  [allowedColumn setMinWidth:40.0];
  [allowedColumn setMaxWidth:48.0];
  [allowedColumn setEditable:YES];
  [allowedColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"model_allowed"
                                 ascending:NO] autorelease]];
  allowedCell = [[[NSButtonCell alloc] init] autorelease];
  [allowedCell setButtonType:XPButtonTypeSwitch];
  [allowedCell setTitle:@""];
  [allowedCell setAlignment:XPTextAlignmentCenter];
  [allowedColumn setDataCell:allowedCell];
  [tableView addTableColumn:allowedColumn];

  nameColumn = [[[NSTableColumn alloc] initWithIdentifier:@"model_name"] autorelease];
  [[nameColumn headerCell] setStringValue:NSLocalizedString(@"Model", nil)];
  [nameColumn setWidth:148.0];
  [nameColumn setMinWidth:100.0];
  [nameColumn setEditable:NO];
  [nameColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"model_name"
                                 ascending:YES] autorelease]];
  textCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [textCell setLineBreakMode:NSLineBreakByTruncatingTail];
  [nameColumn setDataCell:textCell];
  [tableView addTableColumn:nameColumn];

  idColumn = [[[NSTableColumn alloc] initWithIdentifier:@"model_id"] autorelease];
  [[idColumn headerCell] setStringValue:NSLocalizedString(@"ID", nil)];
  [idColumn setWidth:174.0];
  [idColumn setMinWidth:120.0];
  [idColumn setEditable:NO];
  [idColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"model_id"
                                 ascending:YES] autorelease]];
  textCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [textCell setLineBreakMode:NSLineBreakByTruncatingMiddle];
  [textCell setTextColor:[NSColor disabledControlTextColor]];
  [idColumn setDataCell:textCell];
  [tableView addTableColumn:idColumn];

  contextColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"model_context"] autorelease];
  [[contextColumn headerCell] setStringValue:NSLocalizedString(@"Context", nil)];
  [[contextColumn headerCell] setAlignment:XPTextAlignmentRight];
  [contextColumn setWidth:58.0];
  [contextColumn setMinWidth:50.0];
  [contextColumn setEditable:NO];
  [contextColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"model_context"
                                 ascending:NO] autorelease]];
  rightCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [rightCell setAlignment:XPTextAlignmentRight];
  [contextColumn setDataCell:rightCell];
  [tableView addTableColumn:contextColumn];

  promptColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"model_prompt_price"] autorelease];
  [[promptColumn headerCell] setStringValue:NSLocalizedString(@"Cost In (1M)", nil)];
  [[promptColumn headerCell] setAlignment:XPTextAlignmentRight];
  [promptColumn setWidth:88.0];
  [promptColumn setMinWidth:76.0];
  [promptColumn setEditable:NO];
  [promptColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"model_prompt_price"
                                 ascending:YES] autorelease]];
  rightCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [rightCell setAlignment:XPTextAlignmentRight];
  [promptColumn setDataCell:rightCell];
  [tableView addTableColumn:promptColumn];

  completionColumn =
    [[[NSTableColumn alloc] initWithIdentifier:@"model_completion_price"] autorelease];
  [[completionColumn headerCell] setStringValue:NSLocalizedString(@"Cost Out (1M)", nil)];
  [[completionColumn headerCell] setAlignment:XPTextAlignmentRight];
  [completionColumn setWidth:94.0];
  [completionColumn setMinWidth:82.0];
  [completionColumn setEditable:NO];
  [completionColumn setSortDescriptorPrototype:
    [[[NSSortDescriptor alloc] initWithKey:@"model_completion_price"
                                 ascending:YES] autorelease]];
  rightCell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
  [rightCell setAlignment:XPTextAlignmentRight];
  [completionColumn setDataCell:rightCell];
  [tableView addTableColumn:completionColumn];

  [tableView setSortDescriptors:[NSArray arrayWithObjects:
    [[[NSSortDescriptor alloc] initWithKey:@"model_id"
                                 ascending:YES] autorelease],
    nil]];
}

- (NSSortDescriptor *)requiredSortDescriptor
{
  return [[[NSSortDescriptor alloc] initWithKey:@"model_allowed"
                                      ascending:NO] autorelease];
}

- (NSSortDescriptor *)defaultPrimarySortDescriptor
{
  return [[[NSSortDescriptor alloc] initWithKey:@"model_id"
                                      ascending:YES] autorelease];
}

- (NSArray *)fallbackSortDescriptors
{
  return [NSArray arrayWithObjects:
    [[[NSSortDescriptor alloc] initWithKey:@"model_completion_price"
                                 ascending:YES] autorelease],
    [[[NSSortDescriptor alloc] initWithKey:@"model_prompt_price"
                                 ascending:YES] autorelease],
    nil];
}

- (NSString *)stableSortKey
{
  return @"model_id";
}

- (BOOL)sortKeyIsKnown:(NSString *)key
{
  return ([key isEqualToString:@"model_allowed"] ||
          [key isEqualToString:@"model_name"] ||
          [key isEqualToString:@"model_id"] ||
          [key isEqualToString:@"model_context"] ||
          [key isEqualToString:@"model_prompt_price"] ||
          [key isEqualToString:@"model_completion_price"]) ? YES : NO;
}

- (NSComparisonResult)compareRow:(NSDictionary *)left
                             row:(NSDictionary *)right
                      forSortKey:(NSString *)key
{
  if ([key isEqualToString:@"model_allowed"]) {
    return StrappyWhitelistCompareLongLong(
      StrappyModelWhitelistRowIsAllowed(left) ? 1LL : 0LL,
      StrappyModelWhitelistRowIsAllowed(right) ? 1LL : 0LL);
  }
  if ([key isEqualToString:@"model_name"]) {
    return StrappyWhitelistCompareStrings(
      StrappyModelWhitelistDisplayNameForRow(left),
      StrappyModelWhitelistDisplayNameForRow(right));
  }
  if ([key isEqualToString:@"model_id"]) {
    return StrappyWhitelistCompareStrings(
      StrappyModelWhitelistStringForRow(left, @"id"),
      StrappyModelWhitelistStringForRow(right, @"id"));
  }
  if ([key isEqualToString:@"model_context"]) {
    NSNumber *leftValue;
    NSNumber *rightValue;

    leftValue = [left objectForKey:@"context_length"];
    rightValue = [right objectForKey:@"context_length"];
    return StrappyWhitelistCompareLongLong(
      [leftValue isKindOfClass:[NSNumber class]] ? [leftValue longLongValue] : 0LL,
      [rightValue isKindOfClass:[NSNumber class]] ? [rightValue longLongValue] : 0LL);
  }
  if ([key isEqualToString:@"model_prompt_price"]) {
    return StrappyWhitelistCompareDouble(
      [StrappyModelWhitelistStringForRow(left, @"pricing_prompt") doubleValue],
      [StrappyModelWhitelistStringForRow(right, @"pricing_prompt") doubleValue]);
  }
  if ([key isEqualToString:@"model_completion_price"]) {
    return StrappyWhitelistCompareDouble(
      [StrappyModelWhitelistStringForRow(left, @"pricing_completion") doubleValue],
      [StrappyModelWhitelistStringForRow(right, @"pricing_completion") doubleValue]);
  }
  return NSOrderedSame;
}

- (NSPopUpButton *)defaultModelPopUpButton
{
  return defaultModelPopUpButton_;
}

- (NSButton *)fetchButton
{
  return [self refreshButton];
}

- (void)dealloc
{
  [defaultModelPopUpButton_ release];
  [super dealloc];
}

@end
