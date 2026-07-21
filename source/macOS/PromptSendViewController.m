#import "PromptSendViewController.h"
#import "StrappyBottomToolbarView.h"
#import "StrappySession.h"

static const CGFloat kPromptSendHeightCollapsed = 32.0;
static const CGFloat kPromptSendHeightExpanded = 108.0;
static const CGFloat kPromptSendPad = 4.0;
static const CGFloat kPromptActionButtonHeight = 24.0;

enum {
  kPromptActionSegmentOptions = 0,
  kPromptActionSegmentSend = 1
};

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

static NSArray *StrappyPromptWebProviders(void)
{
  return [NSArray arrayWithObjects:
    StrappyWebProviderNone,
    StrappyWebProviderNative,
    StrappyWebProviderExa,
    StrappyWebProviderParallel,
    nil];
}

static NSString *StrappyPromptWebProviderTitle(NSString *webProvider)
{
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
- (void)layoutPromptViews;
- (void)sizeActionSegmentedControlToFit;
- (void)updateExpansion;
- (void)updateActionControls;
- (void)updateSendButtonAppearance;
- (void)updateOptionsSegmentTitle:(NSString *)title;
- (void)selectCurrentModelMenuItem;
- (void)barViewFrameDidChange:(NSNotification *)notification;
- (void)modelMenuItemClicked:(id)sender;
- (void)actionSegmentClicked:(id)sender;
- (void)sendButtonClicked:(id)sender;
- (void)webProviderMenuItemClicked:(id)sender;
- (void)streamingMenuItemClicked:(id)sender;
@end

@implementation PromptSendViewController

- (id)init
{
  if ((self = [super init])) {
    enabled_ = YES;
    webProvider_ = [StrappyWebProviderNone retain];
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
  [textView_ setRichText:NO];
  [textView_ setImportsGraphics:NO];
  [textView_ setFont:XPFontTextStyleBody];
  [textView_ setDrawsBackground:YES];
  [textView_ setBackgroundColor:[NSColor controlBackgroundColor]];
  [textView_ setTextContainerInset:NSMakeSize(2.0, 2.0)];
  [textView_ setDelegate:self];
  [scrollView_ setDocumentView:textView_];

  optionsMenu_ = [[NSMenu alloc]
      initWithTitle:NSLocalizedString(@"Prompt Options", nil)];
  [optionsMenu_ setAutoenablesItems:NO];

  actionSegmented_ = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
  [actionSegmented_ setSegmentCount:2];
  [[actionSegmented_ cell] setTrackingMode:NSSegmentSwitchTrackingMomentary];
  [actionSegmented_ XP_setToolbarSegmentStyle];
  [self sizeActionSegmentedControlToFit];
  [actionSegmented_ setMenu:optionsMenu_
                 forSegment:kPromptActionSegmentOptions];
  [actionSegmented_ setTarget:self];
  [actionSegmented_ setAction:@selector(actionSegmentClicked:)];
  [actionSegmented_ setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [actionSegmented_ XP_setToolTip:NSLocalizedString(@"Prompt Options", nil)
                       forSegment:kPromptActionSegmentOptions];
  [barView_ addSubview:actionSegmented_];
  [self reloadOptionsMenu];
  [self updateSendButtonAppearance];

  [barView_ setPostsFrameChangedNotifications:YES];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(barViewFrameDidChange:)
             name:NSViewFrameDidChangeNotification
           object:barView_];

  [self setEnabled:enabled_];
  [self setWebProvider:webProvider_];
  [self setStreamingEnabled:streamingEnabled_];
}

- (void)viewDidLayout
{
  [super viewDidLayout];
  [self layoutPromptViews];
}

- (void)layoutPromptViews
{
  NSRect bounds;
  NSSize actionSize;
  CGFloat actionWidth;
  CGFloat actionHeight;
  CGFloat inputWidth;
  CGFloat actionX;

  bounds = [barView_ bounds];
  actionSize = [actionSegmented_ frame].size;
  actionWidth = actionSize.width;
  actionHeight = actionSize.height;
  if (actionHeight <= 0.0) {
    actionHeight = kPromptActionButtonHeight;
  }
  inputWidth = bounds.size.width - actionWidth - (kPromptSendPad * 3.0);
  if (inputWidth < 0.0) {
    inputWidth = 0.0;
  }

  actionX = NSMaxX(bounds) - kPromptSendPad - actionWidth;
  [actionSegmented_ setFrame:NSMakeRect(actionX,
                                        kPromptSendPad,
                                        actionWidth,
                                        actionHeight)];
  [bezelView_ setFrame:NSMakeRect(kPromptSendPad,
                                  kPromptSendPad,
                                  inputWidth,
                                  bounds.size.height - (kPromptSendPad * 2.0))];
  [scrollView_ setFrame:[bezelView_ bounds]];
}

- (void)sizeActionSegmentedControlToFit
{
  if (actionSegmented_ == nil) {
    return;
  }

  [actionSegmented_ setWidth:0.0 forSegment:kPromptActionSegmentOptions];
  [actionSegmented_ setWidth:0.0 forSegment:kPromptActionSegmentSend];
  [actionSegmented_ sizeToFit];
  [self layoutPromptViews];
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
  if (actionSegmented_ == nil) {
    return;
  }

  if (sending_) {
    [actionSegmented_ setLabel:NSLocalizedString(@"Cancel", nil)
                    forSegment:kPromptActionSegmentSend];
    [actionSegmented_ XP_setToolTip:NSLocalizedString(@"Cancel Prompt", nil)
                         forSegment:kPromptActionSegmentSend];
  } else {
    [actionSegmented_ setLabel:NSLocalizedString(@"Send", nil)
                    forSegment:kPromptActionSegmentSend];
    [actionSegmented_ XP_setToolTip:NSLocalizedString(@"Send Prompt", nil)
                         forSegment:kPromptActionSegmentSend];
  }
  [self sizeActionSegmentedControlToFit];
}

- (void)updateOptionsSegmentTitle:(NSString *)title
{
  if (actionSegmented_ == nil) {
    return;
  }

  [actionSegmented_ setLabel:(([title length] > 0U) ?
                              title : NSLocalizedString(@"Model", nil))
                  forSegment:kPromptActionSegmentOptions];
  [self sizeActionSegmentedControlToFit];
}

- (void)reloadOptionsMenu
{
  NSArray *models;
  NSString *selectedModelIdentifier;
  NSString *selectedTitle;
  NSString *webProvider;
  NSString *webProviderCopy;
  NSUInteger index;
  BOOL foundSelectedModel;

  if (optionsMenu_ == nil) {
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
  selectedTitle = nil;
  webProvider = webProvider_;
  if (delegate_ != nil) {
    webProvider =
      [delegate_ webProviderForPromptSendViewController:self];
  }
  if (![StrappyPromptWebProviders() containsObject:webProvider]) {
    webProvider = StrappyWebProviderNone;
  }
  webProviderCopy = [webProvider copy];
  [webProvider_ release];
  webProvider_ = webProviderCopy;

  [webProviderMenuItem_ release];
  webProviderMenuItem_ = nil;
  [webProviderMenu_ release];
  webProviderMenu_ = nil;
  [streamingMenuItem_ release];
  streamingMenuItem_ = nil;
  while ([optionsMenu_ numberOfItems] > 0) {
    [optionsMenu_ removeItemAtIndex:0];
  }

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
    item = [optionsMenu_ addItemWithTitle:title
                                   action:@selector(modelMenuItemClicked:)
                            keyEquivalent:@""];
    [item setTarget:self];
    [item setRepresentedObject:modelIdentifier];
    if ([modelIdentifier isEqualToString:selectedModelIdentifier]) {
      [item setState:XPControlStateValueOn];
      selectedTitle = title;
      foundSelectedModel = YES;
    } else {
      [item setState:XPControlStateValueOff];
    }
  }

  if (!foundSelectedModel && ([selectedModelIdentifier length] > 0U)) {
    NSMenuItem *item;

    item = [optionsMenu_ addItemWithTitle:selectedModelIdentifier
                                   action:nil
                            keyEquivalent:@""];
    [item setEnabled:NO];
    [item setRepresentedObject:selectedModelIdentifier];
    [item setState:XPControlStateValueOn];
    selectedTitle = selectedModelIdentifier;
  }

  if ([optionsMenu_ numberOfItems] == 0) {
    NSMenuItem *item;

    selectedTitle = NSLocalizedString(@"Model", nil);
    item = [optionsMenu_ addItemWithTitle:selectedTitle
                                   action:nil
                            keyEquivalent:@""];
    [item setEnabled:NO];
  }

  if ([optionsMenu_ numberOfItems] > 0) {
    [optionsMenu_ addItem:[NSMenuItem separatorItem]];
  }

  webProviderMenuItem_ = [[optionsMenu_
      addItemWithTitle:NSLocalizedString(@"Web Search", nil)
                action:nil
         keyEquivalent:@""] retain];
  webProviderMenu_ = [[NSMenu alloc]
    initWithTitle:NSLocalizedString(@"Web Search", nil)];
  for (index = 0U; index < [StrappyPromptWebProviders() count]; index++) {
    NSString *provider;
    NSMenuItem *item;

    provider = [StrappyPromptWebProviders() objectAtIndex:index];
    item = [webProviderMenu_
      addItemWithTitle:StrappyPromptWebProviderTitle(provider)
                action:@selector(webProviderMenuItemClicked:)
         keyEquivalent:@""];
    [item setTarget:self];
    [item setRepresentedObject:provider];
  }
  [webProviderMenuItem_ setSubmenu:webProviderMenu_];

  streamingMenuItem_ = [[optionsMenu_
      addItemWithTitle:NSLocalizedString(@"Stream Responses", nil)
                action:@selector(streamingMenuItemClicked:)
         keyEquivalent:@""] retain];
  [streamingMenuItem_ setTarget:self];
  [self updateOptionsSegmentTitle:selectedTitle];
  [self updateActionControls];
}

- (void)selectCurrentModelMenuItem
{
  NSString *selectedModelIdentifier;
  NSInteger count;
  NSInteger index;
  NSString *selectedTitle;

  if (optionsMenu_ == nil) {
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

  selectedTitle = nil;
  count = [optionsMenu_ numberOfItems];
  for (index = 0; index < count; index++) {
    NSMenuItem *item;
    id representedObject;

    item = [optionsMenu_ itemAtIndex:index];
    representedObject = [item representedObject];
    if ([representedObject isKindOfClass:[NSString class]] &&
        ([(NSString *)representedObject length] > 0U) &&
        [representedObject isEqualToString:selectedModelIdentifier]) {
      [item setState:XPControlStateValueOn];
      selectedTitle = [item title];
    } else if ((item != webProviderMenuItem_) &&
               (item != streamingMenuItem_)) {
      [item setState:XPControlStateValueOff];
    }
  }

  if (([selectedTitle length] == 0U) && (count > 0)) {
    selectedTitle = [[optionsMenu_ itemAtIndex:0] title];
  }
  [self updateOptionsSegmentTitle:selectedTitle];
}

- (void)updateActionControls
{
  [self updateSendButtonAppearance];

  [actionSegmented_ setEnabled:(enabled_ && !sending_)
                    forSegment:kPromptActionSegmentOptions];
  [self selectCurrentModelMenuItem];
  if (webProviderMenuItem_ != nil) {
    NSInteger count;
    NSInteger index;

    [webProviderMenuItem_ setEnabled:(enabled_ && !sending_)];
    count = [webProviderMenu_ numberOfItems];
    for (index = 0; index < count; index++) {
      NSMenuItem *item;

      item = [webProviderMenu_ itemAtIndex:index];
      [item setState:[[item representedObject]
        isEqualToString:webProvider_] ?
          XPControlStateValueOn : XPControlStateValueOff];
    }
  }
  if (streamingMenuItem_ != nil) {
    [streamingMenuItem_ setEnabled:(enabled_ && !sending_)];
    [streamingMenuItem_ setState:(streamingEnabled_ ?
      XPControlStateValueOn : XPControlStateValueOff)];
  }
  [actionSegmented_ setEnabled:(sending_ ?
    (enabled_ && !cancellationRequested_) : [self canSendCurrentPrompt])
                    forSegment:kPromptActionSegmentSend];
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

- (void)setWebProvider:(NSString *)webProvider
{
  NSString *webProviderCopy;

  if (![StrappyPromptWebProviders() containsObject:webProvider]) {
    webProvider = StrappyWebProviderNone;
  }
  webProviderCopy = [webProvider copy];
  [webProvider_ release];
  webProvider_ = webProviderCopy;
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

- (void)actionSegmentClicked:(id)sender
{
  NSEvent *event;
  NSInteger segment;

  if (![sender isKindOfClass:[NSSegmentedControl class]]) {
    return;
  }

  segment = [(NSSegmentedControl *)sender selectedSegment];
  if (segment == kPromptActionSegmentSend) {
    [self sendButtonClicked:sender];
  } else if ((segment == kPromptActionSegmentOptions) &&
             (optionsMenu_ != nil) &&
             ([optionsMenu_ numberOfItems] > 0)) {
    event = [NSApp currentEvent];
    if (event == nil) {
      return;
    }
    [NSMenu popUpContextMenu:optionsMenu_
                   withEvent:event
                     forView:actionSegmented_];
  }
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

- (void)webProviderMenuItemClicked:(id)sender
{
  BOOL changed;
  NSString *webProvider;

  if (sending_) {
    return;
  }
  webProvider = [sender representedObject];
  if (![StrappyPromptWebProviders() containsObject:webProvider]) {
    return;
  }
  changed = ((delegate_ != nil) &&
             [delegate_ promptSendViewController:self
                                   setWebProvider:webProvider]) ? YES : NO;
  if (changed) {
    NSString *webProviderCopy;

    webProviderCopy = [webProvider copy];
    [webProvider_ release];
    webProvider_ = webProviderCopy;
  }
  [self updateActionControls];
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
  [actionSegmented_ release];
  [optionsMenu_ release];
  [webProviderMenuItem_ release];
  [webProviderMenu_ release];
  [webProvider_ release];
  [streamingMenuItem_ release];
  [super dealloc];
}

@end
