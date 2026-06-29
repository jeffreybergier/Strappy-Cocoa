#import "PromptSendViewController.h"
#import "StrappyBottomToolbarView.h"

static const CGFloat kPromptSendHeightCollapsed = 32.0;
static const CGFloat kPromptSendHeightExpanded = 108.0;
static const CGFloat kPromptSendPad = 4.0;
static const CGFloat kPromptOptionsButtonWidth = 180.0;
static const CGFloat kPromptSendButtonWidth = 96.0;
static const CGFloat kPromptActionButtonHeight = 24.0;

static NSColor *StrappyInputBezelBackgroundColor(void) { return [NSColor controlBackgroundColor]; }
static NSColor *StrappyInputBezelBorderColor(void) { return [NSColor gridColor]; }
static NSColor *StrappyInputBezelHighlightColor(void) { return XPColorControlHighlight; }

static NSString *StrappyPromptStringForModelRow(NSDictionary *row,
                                                NSString *key)
{
  id value;

  if (![row isKindOfClass:[NSDictionary class]]) {
    return @"";
  }

  value = [row objectForKey:key];
  if (![value isKindOfClass:[NSString class]]) {
    return @"";
  }
  return value;
}

static NSString *StrappyPromptDisplayNameForModelRow(NSDictionary *row)
{
  NSString *name;
  NSString *modelId;

  name = StrappyPromptStringForModelRow(row, @"name");
  if ([name length] > 0U) {
    return name;
  }

  modelId = StrappyPromptStringForModelRow(row, @"id");
  return ([modelId length] > 0U) ? modelId : NSLocalizedString(@"Model", nil);
}

@interface StrappyPromptInputBezelView : NSView
@end

@implementation StrappyPromptInputBezelView

- (void)drawRect:(NSRect)dirtyRect
{
  NSRect bounds;

  (void)dirtyRect;
  bounds = [self bounds];

  [StrappyInputBezelBackgroundColor() set];
  NSRectFill(bounds);

  if (AICCCurrentTier() >= AICCTierMiddle) {
    return;
  }

  [StrappyInputBezelBorderColor() set];
  NSFrameRect(bounds);

  [StrappyInputBezelHighlightColor() set];
  NSRectFill(NSMakeRect(bounds.origin.x + 1.0,
                        bounds.origin.y + bounds.size.height - 2.0,
                        bounds.size.width - 2.0,
                        1.0));
}

@end

@interface PromptSendViewController ()
- (void)updateExpansion;
- (void)updateActionControls;
- (void)updateSendButtonAppearance;
- (void)selectCurrentModelMenuItem;
- (void)barViewFrameDidChange:(NSNotification *)notification;
- (void)modelMenuItemClicked:(id)sender;
- (void)sendButtonClicked:(id)sender;
- (void)streamingMenuItemClicked:(id)sender;
@end

@implementation PromptSendViewController

- (id)init
{
  if ((self = [super init])) {
    enabled_ = YES;
  }
  return self;
}

- (void)setDelegate:(id<PromptSendViewControllerDelegate>)delegate
{
  delegate_ = delegate;
}

- (id<PromptSendViewControllerDelegate>)delegate
{
  return delegate_;
}

- (void)loadView
{
  StrappyBottomToolbarView *bar;

  bar = [[StrappyBottomToolbarView alloc]
      initWithFrame:NSMakeRect(0.0, 0.0, 400.0, kPromptSendHeightCollapsed)];
  barView_ = bar;
  [barView_ setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
  [self setView:barView_];
  [barView_ release];
}

- (void)viewDidLoad
{
  StrappyPromptInputBezelView *bezel;

  [super viewDidLoad];

  bezel = [[StrappyPromptInputBezelView alloc] initWithFrame:NSZeroRect];
  bezelView_ = bezel;
  [bezelView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [barView_ addSubview:bezelView_];
  [bezel release];
  [bezelView_ XP_setWantsLayer:YES];
  [bezelView_ XP_setLayerCornerRadius:8.0];
  [bezelView_ XP_setLayerMasksToBounds:YES];
  [bezelView_ XP_setLayerBorderWidth:1.0];
  [bezelView_ XP_setLayerBorderColor:StrappyInputBezelBorderColor()];

  scrollView_ = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  [scrollView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView_ setBorderType:NSNoBorder];
  [scrollView_ setHasVerticalScroller:YES];
  [scrollView_ setHasHorizontalScroller:NO];
  if ([scrollView_ respondsToSelector:@selector(setAutohidesScrollers:)]) {
    [scrollView_ setAutohidesScrollers:YES];
  }
  [bezelView_ addSubview:scrollView_];

  textView_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
  [textView_ setMinSize:NSMakeSize(0.0, 0.0)];
  [textView_ setMaxSize:NSMakeSize(100000.0, 100000.0)];
  [textView_ setVerticallyResizable:YES];
  [textView_ setHorizontallyResizable:NO];
  [textView_ setAutoresizingMask:NSViewWidthSizable];
  [[textView_ textContainer] setWidthTracksTextView:YES];
  [textView_ setFont:XPFontTextStyleBody];
  [textView_ setDrawsBackground:YES];
  [textView_ setBackgroundColor:[NSColor controlBackgroundColor]];
  [textView_ setTextContainerInset:NSMakeSize(2.0, 2.0)];
  [textView_ setDelegate:self];
  [scrollView_ setDocumentView:textView_];

  optionsPopUpButton_ =
    [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  [optionsPopUpButton_ setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [optionsPopUpButton_ setBezelStyle:XPBezelStyleRounded];
  [optionsPopUpButton_ setToolTip:NSLocalizedString(@"Prompt Options", nil)];
  [[optionsPopUpButton_ menu] setAutoenablesItems:NO];
  [barView_ addSubview:optionsPopUpButton_];
  [self reloadOptionsMenu];

  sendButton_ = [[NSButton alloc] initWithFrame:NSZeroRect];
  [sendButton_ setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [sendButton_ setBezelStyle:XPBezelStyleRounded];
  [sendButton_ setButtonType:XPButtonTypeMomentaryLight];
  [sendButton_ setTarget:self];
  [sendButton_ setAction:@selector(sendButtonClicked:)];
  [barView_ addSubview:sendButton_];
  [self updateSendButtonAppearance];

  [barView_ setPostsFrameChangedNotifications:YES];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(barViewFrameDidChange:)
             name:NSViewFrameDidChangeNotification
           object:barView_];

  [self setEnabled:enabled_];
  [self setStreamingEnabled:streamingEnabled_];
}

- (void)viewDidLayout
{
  NSRect bounds;
  CGFloat sendButtonWidth;
  CGFloat optionsButtonWidth;
  CGFloat inputWidth;
  CGFloat sendX;
  CGFloat optionsX;

  [super viewDidLayout];
  bounds = [barView_ bounds];
  sendButtonWidth = kPromptSendButtonWidth;
  optionsButtonWidth = kPromptOptionsButtonWidth;
  inputWidth = bounds.size.width - sendButtonWidth - optionsButtonWidth -
    (kPromptSendPad * 4.0);
  if (inputWidth < 0.0) {
    inputWidth = 0.0;
  }

  sendX = NSMaxX(bounds) - kPromptSendPad - sendButtonWidth;
  optionsX = sendX - kPromptSendPad - optionsButtonWidth;
  [optionsPopUpButton_ setFrame:NSMakeRect(optionsX,
                                           kPromptSendPad,
                                           optionsButtonWidth,
                                           kPromptActionButtonHeight)];
  [sendButton_ setFrame:NSMakeRect(sendX,
                                   kPromptSendPad,
                                   sendButtonWidth,
                                   kPromptActionButtonHeight)];
  [bezelView_ setFrame:NSMakeRect(kPromptSendPad,
                                  kPromptSendPad,
                                  inputWidth,
                                  bounds.size.height - (kPromptSendPad * 2.0))];
  [scrollView_ setFrame:[bezelView_ bounds]];
}

- (CGFloat)preferredHeight
{
  return expanded_ ? kPromptSendHeightExpanded : kPromptSendHeightCollapsed;
}

- (void)barViewFrameDidChange:(NSNotification *)notification
{
  (void)notification;
  [self updateExpansion];
}

- (void)updateSendButtonAppearance
{
  if (sendButton_ == nil) {
    return;
  }

  if (sending_) {
    [sendButton_ setTitle:NSLocalizedString(@"Cancel", nil)];
    [sendButton_ setToolTip:NSLocalizedString(@"Cancel Prompt", nil)];
  } else {
    [sendButton_ setTitle:NSLocalizedString(@"Send", nil)];
    [sendButton_ setToolTip:NSLocalizedString(@"Send Prompt", nil)];
  }
}

- (void)reloadOptionsMenu
{
  NSArray *models;
  NSString *selectedModelIdentifier;
  NSUInteger selectedIndex;
  NSUInteger index;
  BOOL foundSelectedModel;

  if (optionsPopUpButton_ == nil) {
    return;
  }

  models = nil;
  selectedModelIdentifier = nil;
  if (delegate_ != nil) {
    models = [delegate_ allowedModelsForPromptSendViewController:self];
  }
  if (![models isKindOfClass:[NSArray class]]) {
    models = [NSArray array];
  }

  if (delegate_ != nil) {
    selectedModelIdentifier =
      [delegate_ selectedModelIdentifierForPromptSendViewController:self];
  }
  if (![selectedModelIdentifier isKindOfClass:[NSString class]]) {
    selectedModelIdentifier = @"";
  }

  foundSelectedModel = NO;
  selectedIndex = NSNotFound;

  [streamingMenuItem_ release];
  streamingMenuItem_ = nil;
  [optionsPopUpButton_ removeAllItems];

  for (index = 0U; index < [models count]; index++) {
    NSDictionary *model;
    NSString *modelIdentifier;
    NSString *title;
    NSMenuItem *item;

    model = [models objectAtIndex:index];
    modelIdentifier = StrappyPromptStringForModelRow(model, @"id");
    if ([modelIdentifier length] == 0U) {
      continue;
    }

    title = StrappyPromptDisplayNameForModelRow(model);
    [optionsPopUpButton_ addItemWithTitle:title];
    item = [optionsPopUpButton_ itemAtIndex:
      ([optionsPopUpButton_ numberOfItems] - 1)];
    [item setTarget:self];
    [item setAction:@selector(modelMenuItemClicked:)];
    [item setRepresentedObject:modelIdentifier];
    if ([modelIdentifier isEqualToString:selectedModelIdentifier]) {
      [item setState:XPControlStateValueOn];
      selectedIndex = ([optionsPopUpButton_ numberOfItems] - 1);
      foundSelectedModel = YES;
    } else {
      [item setState:XPControlStateValueOff];
    }
  }

  if (!foundSelectedModel && ([selectedModelIdentifier length] > 0U)) {
    NSMenuItem *item;

    [optionsPopUpButton_ addItemWithTitle:selectedModelIdentifier];
    selectedIndex = ([optionsPopUpButton_ numberOfItems] - 1);
    item = [optionsPopUpButton_ itemAtIndex:selectedIndex];
    [item setEnabled:NO];
    [item setRepresentedObject:selectedModelIdentifier];
    [item setState:XPControlStateValueOn];
  }

  if ([optionsPopUpButton_ numberOfItems] == 0) {
    NSMenuItem *item;

    [optionsPopUpButton_ addItemWithTitle:NSLocalizedString(@"Model", nil)];
    selectedIndex = 0U;
    item = [optionsPopUpButton_ itemAtIndex:0];
    [item setEnabled:NO];
  }

  if ([optionsPopUpButton_ numberOfItems] > 0) {
    [[optionsPopUpButton_ menu] addItem:[NSMenuItem separatorItem]];
  }

  [optionsPopUpButton_ addItemWithTitle:
    NSLocalizedString(@"Stream Responses", nil)];
  streamingMenuItem_ = [[optionsPopUpButton_ itemAtIndex:
    ([optionsPopUpButton_ numberOfItems] - 1)] retain];
  [streamingMenuItem_ setTarget:self];
  [streamingMenuItem_ setAction:@selector(streamingMenuItemClicked:)];
  if (selectedIndex == NSNotFound) {
    selectedIndex = 0U;
  }
  [optionsPopUpButton_ selectItemAtIndex:selectedIndex];
  [optionsPopUpButton_ synchronizeTitleAndSelectedItem];
  [optionsPopUpButton_ setNeedsDisplay:YES];
  [self updateActionControls];
}

- (void)selectCurrentModelMenuItem
{
  NSString *selectedModelIdentifier;
  NSInteger count;
  NSInteger index;

  if (optionsPopUpButton_ == nil) {
    return;
  }

  selectedModelIdentifier = nil;
  if (delegate_ != nil) {
    selectedModelIdentifier =
      [delegate_ selectedModelIdentifierForPromptSendViewController:self];
  }
  if (![selectedModelIdentifier isKindOfClass:[NSString class]]) {
    selectedModelIdentifier = @"";
  }

  count = [optionsPopUpButton_ numberOfItems];
  for (index = 0; index < count; index++) {
    NSMenuItem *item;
    id representedObject;

    item = [optionsPopUpButton_ itemAtIndex:index];
    representedObject = [item representedObject];
    if ([representedObject isKindOfClass:[NSString class]] &&
        ([(NSString *)representedObject length] > 0U) &&
        [representedObject isEqualToString:selectedModelIdentifier]) {
      [item setState:XPControlStateValueOn];
      [optionsPopUpButton_ selectItemAtIndex:index];
      [optionsPopUpButton_ synchronizeTitleAndSelectedItem];
      return;
    }
    if (item != streamingMenuItem_) {
      [item setState:XPControlStateValueOff];
    }
  }

  if (count > 0) {
    [optionsPopUpButton_ selectItemAtIndex:0];
    [optionsPopUpButton_ synchronizeTitleAndSelectedItem];
  }
}

- (void)updateActionControls
{
  [self updateSendButtonAppearance];

  [optionsPopUpButton_ setEnabled:(enabled_ && !sending_)];
  [self selectCurrentModelMenuItem];
  if (streamingMenuItem_ != nil) {
    [streamingMenuItem_ setEnabled:(enabled_ && !sending_)];
    [streamingMenuItem_ setState:(streamingEnabled_ ?
      XPControlStateValueOn : XPControlStateValueOff)];
  }
  [sendButton_ setEnabled:(sending_ ?
    (enabled_ && !cancellationRequested_) : [self canSendCurrentPrompt])];
}

- (void)updateExpansion
{
  NSLayoutManager *layoutManager;
  NSTextContainer *textContainer;
  NSRange glyphRange;
  NSUInteger lineCount;
  NSUInteger index;
  BOOL nowExpanded;

  if (textView_ == nil) {
    return;
  }

  layoutManager = [textView_ layoutManager];
  textContainer = [textView_ textContainer];
  glyphRange = [layoutManager glyphRangeForTextContainer:textContainer];
  lineCount = 0U;
  index = glyphRange.location;

  while (index < NSMaxRange(glyphRange)) {
    NSRange lineRange;

    (void)[layoutManager lineFragmentUsedRectForGlyphAtIndex:index
                                               effectiveRange:&lineRange];
    if (lineRange.length == 0U) {
      break;
    }
    lineCount++;
    index = NSMaxRange(lineRange);
  }

  nowExpanded = (lineCount > 1U) ? YES : NO;
  if (nowExpanded == expanded_) {
    return;
  }

  expanded_ = nowExpanded;
  if (delegate_ != nil) {
    [delegate_ promptSendViewControllerDidChangeHeight:self];
  }
}

- (void)setEnabled:(BOOL)enabled
{
  enabled_ = enabled ? YES : NO;
  [textView_ setEditable:enabled_];
  [textView_ setSelectable:enabled_];
  [textView_ setDrawsBackground:YES];
  [textView_ setBackgroundColor:enabled_
    ? [NSColor controlBackgroundColor]
    : [NSColor disabledControlTextColor]];
  [self updateActionControls];
}

- (void)setSending:(BOOL)sending
{
  sending_ = sending ? YES : NO;
  if (!sending_) {
    cancellationRequested_ = NO;
  }
  [self updateActionControls];
}

- (void)setCancellationRequested:(BOOL)requested
{
  cancellationRequested_ = requested ? YES : NO;
  [self updateActionControls];
}

- (void)setStreamingEnabled:(BOOL)enabled
{
  streamingEnabled_ = enabled ? YES : NO;
  [self updateActionControls];
}

- (BOOL)canSendCurrentPrompt
{
  NSString *text;
  NSString *trimmed;

  if (!enabled_ || sending_ || (textView_ == nil)) {
    return NO;
  }

  text = [textView_ string];
  trimmed = [text stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  return ([trimmed length] > 0U) ? YES : NO;
}

- (void)sendButtonClicked:(id)sender
{
  if (sending_) {
    if (!cancellationRequested_) {
      [self setCancellationRequested:YES];
      if (delegate_ != nil) {
        [delegate_ promptSendViewControllerDidCancelPrompt:self];
      }
    }
    return;
  }

  [self performSend:sender];
}

- (void)modelMenuItemClicked:(id)sender
{
  NSString *modelIdentifier;
  BOOL changed;

  if (sending_) {
    return;
  }
  if (![sender respondsToSelector:@selector(representedObject)]) {
    return;
  }

  modelIdentifier = [sender representedObject];
  if (![modelIdentifier isKindOfClass:[NSString class]] ||
      ([modelIdentifier length] == 0U)) {
    return;
  }

  changed = ((delegate_ != nil) &&
             [delegate_ promptSendViewController:self
                      setSelectedModelIdentifier:modelIdentifier]) ? YES : NO;
  if (changed) {
    [self reloadOptionsMenu];
  } else {
    [self updateActionControls];
  }
}

- (void)streamingMenuItemClicked:(id)sender
{
  BOOL enabled;
  BOOL changed;

  (void)sender;
  if (sending_) {
    return;
  }

  enabled = streamingEnabled_ ? NO : YES;
  changed = ((delegate_ != nil) &&
             [delegate_ promptSendViewController:self
                              setStreamingEnabled:enabled]) ? YES : NO;
  if (changed) {
    streamingEnabled_ = enabled;
  }
  [self updateActionControls];
  [self performSelector:@selector(selectCurrentModelMenuItem)
             withObject:nil
             afterDelay:0.0];
}

- (void)performSend:(id)sender
{
  NSString *text;
  NSString *trimmed;

  (void)sender;
  if (![self canSendCurrentPrompt]) {
    return;
  }

  text = [textView_ string];
  trimmed = [text stringByTrimmingCharactersInSet:
    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if ([trimmed length] == 0U) {
    return;
  }

  if (delegate_ != nil) {
    [delegate_ promptSendViewController:self didSubmitPrompt:trimmed];
  }

  [textView_ setString:@""];
  [self updateActionControls];
  [self updateExpansion];
}

- (void)textDidChange:(NSNotification *)notification
{
  (void)notification;
  [self updateActionControls];
  [self updateExpansion];
}

- (BOOL)textView:(NSTextView *)textView doCommandBySelector:(SEL)commandSelector
{
  NSEvent *event;

  (void)textView;
  if (commandSelector != @selector(insertNewline:)) {
    return NO;
  }

  event = [NSApp currentEvent];
  if (([event modifierFlags] & XPEventModifierFlagCommand) == 0) {
    return NO;
  }

  if (sending_ || cancellationRequested_) {
    return YES;
  }

  [self performSend:textView_];
  return YES;
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [scrollView_ release];
  [textView_ release];
  [optionsPopUpButton_ release];
  [streamingMenuItem_ release];
  [sendButton_ release];
  [super dealloc];
}

@end
