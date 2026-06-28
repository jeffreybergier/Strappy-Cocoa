#import "PromptSendViewController.h"
#import "StrappyBottomToolbarView.h"

static const CGFloat kPromptSendHeightCollapsed = 32.0;
static const CGFloat kPromptSendHeightExpanded = 108.0;
static const CGFloat kPromptSendPad = 4.0;
static const CGFloat kPromptOptionsButtonWidth = 94.0;
static const CGFloat kPromptSendButtonWidth = 96.0;
static const CGFloat kPromptActionButtonHeight = 24.0;

static NSColor *StrappyInputBezelBackgroundColor(void) { return [NSColor controlBackgroundColor]; }
static NSColor *StrappyInputBezelBorderColor(void) { return [NSColor gridColor]; }
static NSColor *StrappyInputBezelHighlightColor(void) { return XPColorControlHighlight; }

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
- (void)barViewFrameDidChange:(NSNotification *)notification;
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

  streamingPopUpButton_ =
    [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:YES];
  [streamingPopUpButton_ setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [streamingPopUpButton_ setBezelStyle:XPBezelStyleRounded];
  [streamingPopUpButton_ setToolTip:NSLocalizedString(@"Prompt Options", nil)];
  [streamingPopUpButton_ removeAllItems];
  [streamingPopUpButton_ addItemWithTitle:NSLocalizedString(@"Options", nil)];
  [streamingPopUpButton_ addItemWithTitle:
    NSLocalizedString(@"Stream Responses", nil)];
  [[streamingPopUpButton_ menu] setAutoenablesItems:NO];
  streamingMenuItem_ = [[streamingPopUpButton_ itemAtIndex:1] retain];
  [streamingMenuItem_ setTarget:self];
  [streamingMenuItem_ setAction:@selector(streamingMenuItemClicked:)];
  [barView_ addSubview:streamingPopUpButton_];

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
  [streamingPopUpButton_ setFrame:NSMakeRect(optionsX,
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

- (void)updateActionControls
{
  [self updateSendButtonAppearance];

  [streamingPopUpButton_ setEnabled:(enabled_ && !sending_)];
  if ([streamingPopUpButton_ numberOfItems] > 0) {
    [streamingPopUpButton_ selectItemAtIndex:0];
  }
  [streamingMenuItem_ setEnabled:(enabled_ && !sending_)];
  [streamingMenuItem_ setState:(streamingEnabled_ ?
    XPControlStateValueOn : XPControlStateValueOff)];
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
  [streamingPopUpButton_ release];
  [streamingMenuItem_ release];
  [sendButton_ release];
  [super dealloc];
}

@end
