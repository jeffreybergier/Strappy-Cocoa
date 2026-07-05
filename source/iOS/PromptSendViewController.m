#import "PromptSendViewController.h"

#import "AIFontAwesome.h"

#import <QuartzCore/QuartzCore.h>

static const CGFloat kStrappySendCollapsedHeight = 44.0f;
static const CGFloat kStrappySendExpandedHeight = 104.0f;
static const CGFloat kStrappySendPad = 4.0f;
static const CGFloat kStrappySendButtonSize = 36.0f;
static const CGFloat kStrappySendOptionsWidth = 36.0f;
static const CGFloat kStrappySendFontSize = 16.0f;

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

@interface PromptSendViewController () <UITextViewDelegate,
                                          UIActionSheetDelegate>
@property (nonatomic, strong) UIButton *optionsButton;
@property (nonatomic, strong) UITextView *textView;
@property (nonatomic, strong) UILabel *placeholderLabel;
@property (nonatomic, strong) UIButton *sendButton;
@property (nonatomic, strong) UIActionSheet *optionsSheet;
@property (nonatomic, copy) NSArray *optionsModels;
@property (nonatomic, assign) NSInteger streamingButtonIndex;
@property (nonatomic, assign) NSInteger firstModelButtonIndex;
@property (nonatomic, assign) BOOL controlsEnabled;
@property (nonatomic, assign) BOOL composing;
@property (nonatomic, assign) BOOL expanded;
@property (nonatomic, assign) BOOL sending;
@property (nonatomic, assign) BOOL cancellationRequested;
@property (nonatomic, assign) BOOL streamingEnabled;
- (void)buildSubviews;
- (UIImage *)iconImageForIcon:(AIFontAwesomeIcon)icon
                        style:(AIFontAwesomeStyle)style
                    pointSize:(CGFloat)pointSize
                        color:(UIColor *)color;
- (NSString *)trimmedPromptText;
- (void)updateControls;
- (void)updateExpansion;
- (void)updatePlaceholderVisibility;
- (void)optionsTapped:(id)sender;
- (void)sendTapped:(id)sender;
@end

@implementation PromptSendViewController

- (instancetype)initWithFrame:(CGRect)frame
{
  if ((self = [super initWithFrame:frame])) {
    [self setBackgroundColor:[UIColor colorWithWhite:0.94f alpha:1.0f]];
    [self setAutoresizingMask:
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleTopMargin];
    [self setControlsEnabled:YES];
    [self setStreamingButtonIndex:-1];
    [self setFirstModelButtonIndex:-1];
    [self buildSubviews];
  }
  return self;
}

- (UIImage *)iconImageForIcon:(AIFontAwesomeIcon)icon
                        style:(AIFontAwesomeStyle)style
                    pointSize:(CGFloat)pointSize
                        color:(UIColor *)color
{
  CGFloat scale;

  scale = [[UIScreen mainScreen] scale];
  if (scale <= 0.0f) {
    scale = 1.0f;
  }

  return [AIFontAwesome imageForIcon:icon
                               style:style
                            iconSize:pointSize
                          canvasSize:kStrappySendButtonSize
                               color:color
                               scale:scale];
}

- (void)buildSubviews
{
  UIButton *options;
  UITextView *textView;
  UILabel *placeholder;
  UIButton *send;
  CGFloat hairline;

  hairline = 1.0f / [[UIScreen mainScreen] scale];
  if (hairline <= 0.0f) {
    hairline = 1.0f;
  }

  options = [UIButton buttonWithType:UIButtonTypeCustom];
  [options setImage:[self iconImageForIcon:AIFASliders
                                     style:AIFontAwesomeStyleSolid
                                 pointSize:18.0f
                                     color:[UIColor darkGrayColor]]
            forState:UIControlStateNormal];
  [options setAccessibilityLabel:NSLocalizedString(@"Prompt Options", nil)];
  [options addTarget:self
              action:@selector(optionsTapped:)
    forControlEvents:UIControlEventTouchUpInside];
  [self addSubview:options];
  [self setOptionsButton:options];

  textView = [[UITextView alloc] initWithFrame:CGRectZero];
  [textView setDelegate:self];
  [textView setFont:[UIFont systemFontOfSize:kStrappySendFontSize]];
  [textView setBackgroundColor:[UIColor whiteColor]];
  [textView setReturnKeyType:UIReturnKeyDefault];
  [textView setAutocorrectionType:UITextAutocorrectionTypeDefault];
  [textView setAutoresizingMask:UIViewAutoresizingFlexibleWidth |
                                UIViewAutoresizingFlexibleHeight];
  [[textView layer] setCornerRadius:7.0f];
  [[textView layer] setBorderWidth:hairline];
  [[textView layer] setBorderColor:[[UIColor colorWithWhite:0.76f alpha:1.0f] CGColor]];
  [self addSubview:textView];
  [self setTextView:textView];

  placeholder = [[UILabel alloc] initWithFrame:CGRectZero];
  [placeholder setText:NSLocalizedString(@"Ask Strappy", nil)];
  [placeholder setFont:[UIFont systemFontOfSize:kStrappySendFontSize]];
  [placeholder setTextColor:[UIColor colorWithWhite:0.58f alpha:1.0f]];
  [placeholder setBackgroundColor:[UIColor clearColor]];
  [placeholder setUserInteractionEnabled:NO];
  [self addSubview:placeholder];
  [self setPlaceholderLabel:placeholder];

  send = [UIButton buttonWithType:UIButtonTypeCustom];
  [send setImage:[self iconImageForIcon:AIFAPaperPlane
                                  style:AIFontAwesomeStyleRegular
                              pointSize:18.0f
                                  color:[UIColor whiteColor]]
         forState:UIControlStateNormal];
  [send setBackgroundColor:[UIColor colorWithRed:0.0f
                                           green:0.48f
                                            blue:0.92f
                                           alpha:1.0f]];
  [[send layer] setCornerRadius:kStrappySendButtonSize * 0.5f];
  [send setAccessibilityLabel:NSLocalizedString(@"Send Prompt", nil)];
  [send addTarget:self
           action:@selector(sendTapped:)
 forControlEvents:UIControlEventTouchUpInside];
  [self addSubview:send];
  [self setSendButton:send];

  [self updateControls];
  [self updatePlaceholderVisibility];
}

- (void)layoutSubviews
{
  CGRect bounds;
  CGFloat optionsX;
  CGFloat textX;
  CGFloat sendX;
  CGFloat textRight;
  CGFloat textHeight;
  CGFloat placeholderHeight;

  [super layoutSubviews];

  bounds = [self bounds];
  optionsX = kStrappySendPad;
  sendX = bounds.size.width - kStrappySendPad - kStrappySendButtonSize;
  textX = optionsX + kStrappySendOptionsWidth + kStrappySendPad;
  textRight = sendX - kStrappySendPad;
  if (textRight < textX) {
    textRight = textX;
  }
  textHeight = bounds.size.height - (kStrappySendPad * 2.0f);
  if (textHeight < kStrappySendButtonSize) {
    textHeight = kStrappySendButtonSize;
  }

  [[self optionsButton] setFrame:CGRectMake(optionsX,
                                            kStrappySendPad,
                                            kStrappySendOptionsWidth,
                                            kStrappySendButtonSize)];
  [[self sendButton] setFrame:CGRectMake(sendX,
                                         kStrappySendPad,
                                         kStrappySendButtonSize,
                                         kStrappySendButtonSize)];
  [[self textView] setFrame:CGRectMake(textX,
                                       kStrappySendPad,
                                       textRight - textX,
                                       textHeight)];

  placeholderHeight = (CGFloat)ceilf((float)[[[self textView] font] lineHeight]);
  [[self placeholderLabel] setFrame:CGRectMake(textX + 8.0f,
                                               kStrappySendPad + 8.0f,
                                               (textRight - textX) - 16.0f,
                                               placeholderHeight)];
}

- (CGFloat)preferredHeight
{
  return ([self composing] && [self expanded])
    ? kStrappySendExpandedHeight
    : kStrappySendCollapsedHeight;
}

- (void)setComposing:(BOOL)composing
{
  _composing = composing ? YES : NO;
  [self setNeedsLayout];
}

- (void)setEnabled:(BOOL)enabled
{
  [self setControlsEnabled:enabled ? YES : NO];
  [[self textView] setEditable:enabled ? YES : NO];
  [self updateControls];
}

- (void)setSending:(BOOL)sending
{
  _sending = sending ? YES : NO;
  if (!sending) {
    [self setCancellationRequested:NO];
  }
  [self updateControls];
}

- (void)setCancellationRequested:(BOOL)requested
{
  _cancellationRequested = requested ? YES : NO;
  [self updateControls];
}

- (void)setStreamingEnabled:(BOOL)enabled
{
  _streamingEnabled = enabled ? YES : NO;
}

- (void)reloadOptionsMenu
{
  [self updateControls];
}

- (NSString *)trimmedPromptText
{
  NSString *text;

  text = [[self textView] text];
  if (![text isKindOfClass:[NSString class]]) {
    text = @"";
  }
  return [text stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

- (BOOL)canSendCurrentPrompt
{
  if (![self controlsEnabled] || [self sending] || ([self textView] == nil)) {
    return NO;
  }
  return ([[self trimmedPromptText] length] > 0U) ? YES : NO;
}

- (void)updateControls
{
  BOOL canSend;
  BOOL sendEnabled;
  UIImage *sendImage;
  UIColor *sendColor;

  canSend = [self canSendCurrentPrompt];
  sendEnabled = [self sending]
    ? ([self controlsEnabled] && ![self cancellationRequested])
    : canSend;

  [[self optionsButton] setEnabled:
    ([self controlsEnabled] && ![self sending]) ? YES : NO];
  [[self textView] setEditable:
    ([self controlsEnabled] && ![self sending]) ? YES : NO];
  [[self sendButton] setEnabled:sendEnabled];

  if ([self sending]) {
    sendImage = [self iconImageForIcon:AIFAStop
                                 style:AIFontAwesomeStyleSolid
                             pointSize:16.0f
                                 color:[UIColor whiteColor]];
    [[self sendButton] setAccessibilityLabel:NSLocalizedString(@"Cancel Prompt", nil)];
  } else {
    sendImage = [self iconImageForIcon:AIFAPaperPlane
                                 style:AIFontAwesomeStyleRegular
                             pointSize:18.0f
                                 color:[UIColor whiteColor]];
    [[self sendButton] setAccessibilityLabel:NSLocalizedString(@"Send Prompt", nil)];
  }
  [[self sendButton] setImage:sendImage forState:UIControlStateNormal];

  if (!sendEnabled) {
    sendColor = [UIColor colorWithWhite:0.70f alpha:1.0f];
  } else if ([self sending]) {
    sendColor = [UIColor colorWithRed:0.72f green:0.18f blue:0.16f alpha:1.0f];
  } else {
    sendColor = [UIColor colorWithRed:0.0f green:0.48f blue:0.92f alpha:1.0f];
  }
  [[self sendButton] setBackgroundColor:sendColor];
}

- (void)updatePlaceholderVisibility
{
  [[self placeholderLabel] setHidden:[[[self textView] text] length] > 0U];
}

- (void)updateExpansion
{
  CGFloat lineHeight;
  CGFloat oneLineHeight;
  CGFloat availableWidth;
  CGFloat neededHeight;
  BOOL expanded;

  if ([self textView] == nil) {
    return;
  }

  lineHeight = (CGFloat)ceilf((float)[[[self textView] font] lineHeight]);
  if (lineHeight <= 0.0f) {
    lineHeight = 20.0f;
  }
  oneLineHeight = lineHeight + 16.0f;
  availableWidth = [[self textView] frame].size.width;
  if (availableWidth < 1.0f) {
    availableWidth = 1.0f;
  }

  neededHeight =
    [[self textView] sizeThatFits:CGSizeMake(availableWidth, 10000.0f)].height;
  expanded = (neededHeight > (oneLineHeight + (lineHeight * 0.5f))) ? YES : NO;
  if (expanded == [self expanded]) {
    return;
  }

  [self setExpanded:expanded];
  if ([[self delegate] respondsToSelector:
        @selector(promptSendViewControllerDidChangeHeight:)]) {
    [[self delegate] promptSendViewControllerDidChangeHeight:self];
  }
}

- (void)performSend:(id)sender
{
  NSString *prompt;

  (void)sender;
  if (![self canSendCurrentPrompt]) {
    return;
  }

  prompt = [self trimmedPromptText];
  if ([prompt length] == 0U) {
    return;
  }

  [[self textView] setText:@""];
  [self updatePlaceholderVisibility];
  [self updateExpansion];
  [self updateControls];

  if ([[self delegate] respondsToSelector:
        @selector(promptSendViewController:didSubmitPrompt:)]) {
    [[self delegate] promptSendViewController:self didSubmitPrompt:prompt];
  }
}

- (void)sendTapped:(id)sender
{
  (void)sender;
  if ([self sending]) {
    if (![self cancellationRequested] &&
        [[self delegate] respondsToSelector:
          @selector(promptSendViewControllerDidCancelPrompt:)]) {
      [self setCancellationRequested:YES];
      [[self delegate] promptSendViewControllerDidCancelPrompt:self];
    }
    return;
  }

  [self performSend:sender];
}

- (void)optionsTapped:(id)sender
{
  UIActionSheet *sheet;
  NSArray *models;
  NSMutableArray *sheetModels;
  NSString *selectedModelIdentifier;
  NSUInteger index;

  (void)sender;
  if ([self sending]) {
    return;
  }

  models = nil;
  if ([[self delegate] respondsToSelector:
        @selector(allowedModelsForPromptSendViewController:)]) {
    models = [[self delegate] allowedModelsForPromptSendViewController:self];
  }
  if (![models isKindOfClass:[NSArray class]]) {
    models = [NSArray array];
  }

  selectedModelIdentifier = @"";
  if ([[self delegate] respondsToSelector:
        @selector(selectedModelIdentifierForPromptSendViewController:)]) {
    selectedModelIdentifier =
      [[self delegate] selectedModelIdentifierForPromptSendViewController:self];
  }
  if (![selectedModelIdentifier isKindOfClass:[NSString class]]) {
    selectedModelIdentifier = @"";
  }

  sheet = [[UIActionSheet alloc] initWithTitle:NSLocalizedString(@"Prompt Options", nil)
                                      delegate:self
                             cancelButtonTitle:nil
                        destructiveButtonTitle:nil
                             otherButtonTitles:nil];
  [self setStreamingButtonIndex:
    [sheet addButtonWithTitle:([self streamingEnabled]
      ? NSLocalizedString(@"Stream Responses: On", nil)
      : NSLocalizedString(@"Stream Responses: Off", nil))]];
  [self setFirstModelButtonIndex:[sheet numberOfButtons]];
  sheetModels = [NSMutableArray arrayWithCapacity:[models count]];
  for (index = 0U; index < [models count]; index++) {
    NSDictionary *model;
    NSString *identifier;
    NSString *title;

    model = [models objectAtIndex:index];
    if (![model isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    identifier = StrappyMessageModelStringForRow(model, @"id");
    if ([identifier length] == 0U) {
      continue;
    }
    title = StrappyMessageModelDisplayNameForRow(model);
    if ([identifier isEqualToString:selectedModelIdentifier]) {
      title = [NSString stringWithFormat:NSLocalizedString(@"%@ (Selected)", nil),
        title];
    }
    [sheet addButtonWithTitle:title];
    [sheetModels addObject:model];
  }
  [sheet setCancelButtonIndex:
    [sheet addButtonWithTitle:NSLocalizedString(@"Cancel", nil)]];
  [self setOptionsModels:sheetModels];
  [self setOptionsSheet:sheet];
  [sheet showInView:self];
}

- (void)actionSheet:(UIActionSheet *)actionSheet
clickedButtonAtIndex:(NSInteger)buttonIndex
{
  NSInteger modelIndex;

  if (actionSheet != [self optionsSheet]) {
    return;
  }
  if (buttonIndex == [actionSheet cancelButtonIndex]) {
    [self setOptionsSheet:nil];
    [self setOptionsModels:nil];
    return;
  }

  if (buttonIndex == [self streamingButtonIndex]) {
    BOOL enabled;
    BOOL changed;

    enabled = [self streamingEnabled] ? NO : YES;
    changed = NO;
    if ([[self delegate] respondsToSelector:
          @selector(promptSendViewController:setStreamingEnabled:)]) {
      changed = [[self delegate] promptSendViewController:self
                                       setStreamingEnabled:enabled];
    }
    if (changed) {
      [self setStreamingEnabled:enabled];
    }
  } else if (buttonIndex >= [self firstModelButtonIndex]) {
    modelIndex = buttonIndex - [self firstModelButtonIndex];
    if ((modelIndex >= 0) &&
        (modelIndex < (NSInteger)[[self optionsModels] count])) {
      NSDictionary *model;
      NSString *modelIdentifier;

      model = [[self optionsModels] objectAtIndex:(NSUInteger)modelIndex];
      modelIdentifier = StrappyMessageModelStringForRow(model, @"id");
      if ([modelIdentifier length] > 0U &&
          [[self delegate] respondsToSelector:
            @selector(promptSendViewController:setSelectedModelIdentifier:)]) {
        (void)[[self delegate] promptSendViewController:self
                              setSelectedModelIdentifier:modelIdentifier];
      }
    }
  }

  [self setOptionsSheet:nil];
  [self setOptionsModels:nil];
  [self updateControls];
}

- (void)textViewDidChange:(UITextView *)textView
{
  (void)textView;
  [self updatePlaceholderVisibility];
  [self updateControls];
  [self updateExpansion];
}

- (void)dealloc
{
  [[self textView] setDelegate:nil];
  [[self optionsSheet] setDelegate:nil];
}

@end
