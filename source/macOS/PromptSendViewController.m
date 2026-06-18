#import "PromptSendViewController.h"
#import "AIFontAwesome.h"

static const CGFloat kPromptSendHeightCollapsed = 32.0;
static const CGFloat kPromptSendHeightExpanded = 108.0;
static const CGFloat kPromptSendPad = 4.0;

enum {
  kPromptActionClear = 0,
  kPromptActionSend = 1
};

static NSColor *StrappyInputBezelBackgroundColor(void) { return [NSColor controlBackgroundColor]; }
static NSColor *StrappyInputBezelBorderColor(void) { return [NSColor gridColor]; }
static NSColor *StrappyInputBezelHighlightColor(void) { return XPColorControlHighlight; }

@interface StrappyPromptToolbarView : NSView
@end

@implementation StrappyPromptToolbarView

- (BOOL)isOpaque
{
  return YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
  NSRect bounds;

  (void)dirtyRect;
  bounds = [self bounds];

  [XPColorWindowFrame set];
  NSRectFill(bounds);

  [XPColorControlHighlight set];
  NSRectFill(NSMakeRect(bounds.origin.x,
                        bounds.origin.y + bounds.size.height - 1.0,
                        bounds.size.width,
                        1.0));
}

@end

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
- (void)updateActionSegments;
- (void)rebuildActionSegmentIcons;
- (void)actionSegmentClicked:(id)sender;
- (void)performClear:(id)sender;
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
  StrappyPromptToolbarView *bar;

  bar = [[StrappyPromptToolbarView alloc]
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

  scrollView_ = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  [scrollView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView_ setBorderType:NSNoBorder];
  [scrollView_ setHasVerticalScroller:YES];
  [scrollView_ setHasHorizontalScroller:NO];
  [scrollView_ setAutohidesScrollers:YES];
  [bezelView_ addSubview:scrollView_];

  textView_ = [[NSTextView alloc] initWithFrame:NSZeroRect];
  [textView_ setMinSize:NSMakeSize(0.0, 0.0)];
  [textView_ setMaxSize:NSMakeSize(100000.0, 100000.0)];
  [textView_ setVerticallyResizable:YES];
  [textView_ setHorizontallyResizable:NO];
  [textView_ setAutoresizingMask:NSViewWidthSizable];
  [[textView_ textContainer] setWidthTracksTextView:YES];
  [textView_ setFont:[NSFont systemFontOfSize:13.0]];
  [textView_ setDrawsBackground:YES];
  [textView_ setBackgroundColor:[NSColor controlBackgroundColor]];
  [textView_ setTextContainerInset:NSMakeSize(2.0, 2.0)];
  [textView_ setDelegate:self];
  [scrollView_ setDocumentView:textView_];

  actionsSegmented_ = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
  [actionsSegmented_ setSegmentCount:2];
  [[actionsSegmented_ cell] setTrackingMode:NSSegmentSwitchTrackingMomentary];
  if ([[actionsSegmented_ cell] respondsToSelector:@selector(setSegmentStyle:)]) {
    [[actionsSegmented_ cell] setSegmentStyle:NSSegmentStyleTexturedRounded];
  }
  [actionsSegmented_ setLabel:NSLocalizedString(@"Send", nil)
                   forSegment:kPromptActionSend];
  [actionsSegmented_ setTarget:self];
  [actionsSegmented_ setAction:@selector(actionSegmentClicked:)];
  [actionsSegmented_ setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];
  [actionsSegmented_ setToolTip:NSLocalizedString(@"Clear Draft / Send Prompt (Command-Return)", nil)];
  [self rebuildActionSegmentIcons];
  [actionsSegmented_ sizeToFit];
  [barView_ addSubview:actionsSegmented_];

  [self setEnabled:enabled_];
}

- (void)viewDidLayout
{
  NSRect bounds;
  CGFloat segmentWidth;
  CGFloat segmentHeight;
  CGFloat inputWidth;

  [super viewDidLayout];
  bounds = [barView_ bounds];
  segmentWidth = [actionsSegmented_ frame].size.width;
  segmentHeight = [actionsSegmented_ frame].size.height;
  inputWidth = bounds.size.width - segmentWidth - (kPromptSendPad * 3.0);
  if (inputWidth < 0.0) {
    inputWidth = 0.0;
  }

  [actionsSegmented_ setFrame:NSMakeRect(NSMaxX(bounds) - kPromptSendPad - segmentWidth,
                                         kPromptSendPad,
                                         segmentWidth,
                                         segmentHeight)];
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

- (void)rebuildActionSegmentIcons
{
  CGFloat scale;
  NSImage *clearImage;
  NSImage *sendImage;

  if (actionsSegmented_ == nil) {
    return;
  }

  scale = 1.0;
  if ([[barView_ window] respondsToSelector:@selector(XP_backingScaleFactor)]) {
    scale = [[barView_ window] XP_backingScaleFactor];
  }
  if (scale < 1.0) {
    scale = 1.0;
  }

  clearImage = [AIFontAwesome imageForIcon:AIFAXmark
                                     style:AIFontAwesomeStyleSolid
                                  iconSize:12.0
                                canvasSize:18.0
                                     scale:scale];
  sendImage = [AIFontAwesome imageForIcon:AIFAPaperPlane
                                    style:AIFontAwesomeStyleRegular
                                 iconSize:12.0
                               canvasSize:18.0
                                    scale:scale];

  if (clearImage != nil) {
    [actionsSegmented_ setImage:clearImage forSegment:kPromptActionClear];
    [actionsSegmented_ setLabel:@"" forSegment:kPromptActionClear];
  } else {
    [actionsSegmented_ setLabel:NSLocalizedString(@"Clear", nil)
                     forSegment:kPromptActionClear];
  }

  if (sendImage != nil) {
    [actionsSegmented_ setImage:sendImage forSegment:kPromptActionSend];
  }
}

- (void)updateActionSegments
{
  NSString *text;
  NSString *trimmed;
  BOOL hasDraft;

  hasDraft = NO;
  if (textView_ != nil) {
    text = [textView_ string];
    trimmed = [text stringByTrimmingCharactersInSet:
      [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    hasDraft = ([trimmed length] > 0U) ? YES : NO;
  }

  [actionsSegmented_ setEnabled:(enabled_ && hasDraft)
                     forSegment:kPromptActionClear];
  [actionsSegmented_ setEnabled:[self canSendCurrentPrompt]
                     forSegment:kPromptActionSend];
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
  if (actionsSegmented_ != nil) {
    [actionsSegmented_ setEnabled:enabled_];
    [self updateActionSegments];
  }
}

- (void)setSending:(BOOL)sending
{
  sending_ = sending ? YES : NO;
  [self updateActionSegments];
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
  NSInteger selectedSegment;

  selectedSegment = [(NSSegmentedControl *)sender selectedSegment];
  if (selectedSegment == kPromptActionClear) {
    [self performClear:sender];
  } else if (selectedSegment == kPromptActionSend) {
    [self performSend:sender];
  }
}

- (void)performClear:(id)sender
{
  (void)sender;
  if (!enabled_ || (textView_ == nil)) {
    return;
  }

  [textView_ setString:@""];
  [self updateActionSegments];
  [self updateExpansion];
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
  [self updateActionSegments];
  [self updateExpansion];
}

- (void)textDidChange:(NSNotification *)notification
{
  (void)notification;
  [self updateActionSegments];
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

  [self performSend:textView_];
  return YES;
}

- (void)dealloc
{
  [scrollView_ release];
  [textView_ release];
  [actionsSegmented_ release];
  [super dealloc];
}

@end
