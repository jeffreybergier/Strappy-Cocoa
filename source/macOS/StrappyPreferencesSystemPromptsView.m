#import "StrappyPreferencesSystemPromptsView.h"

static const CGFloat kStrappyPreferencesInset = 12.0;

@interface StrappyPreferencesSystemPromptsView ()
- (void)buildView;
@end

@implementation StrappyPreferencesSystemPromptsView

- (id)initWithFrame:(NSRect)frame
{
  if ((self = [super initWithFrame:frame])) {
    [self setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [self buildView];
  }
  return self;
}

- (void)buildView
{
  NSScrollView *scrollView;

  scrollView = [[[NSScrollView alloc]
      initWithFrame:NSInsetRect([self bounds],
                                kStrappyPreferencesInset,
                                kStrappyPreferencesInset)] autorelease];
  [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [scrollView setBorderType:NSBezelBorder];
  [scrollView setHasVerticalScroller:YES];
  [scrollView setHasHorizontalScroller:NO];
  [scrollView setAutohidesScrollers:YES];

  textView_ =
    [[NSTextView alloc] initWithFrame:[[scrollView contentView] bounds]];
  [textView_ setMinSize:NSMakeSize(0.0, 0.0)];
  [textView_ setMaxSize:NSMakeSize(100000.0, 100000.0)];
  [textView_ setVerticallyResizable:YES];
  [textView_ setHorizontallyResizable:NO];
  [textView_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [[textView_ textContainer] setWidthTracksTextView:YES];
  [textView_ setEditable:NO];
  [textView_ setSelectable:YES];
  [textView_ setRichText:NO];
  [textView_ setFont:[NSFont userFixedPitchFontOfSize:12.0]];
  [textView_ setString:@""];

  [scrollView setDocumentView:textView_];
  [self addSubview:scrollView];
}

- (NSTextView *)textView
{
  return textView_;
}

- (void)dealloc
{
  [textView_ release];
  [super dealloc];
}

@end
