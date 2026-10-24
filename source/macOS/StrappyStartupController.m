#import "StrappyStartupController.h"
#import "StrappyAsync.h"
#import "StrappyServiceController.h"
#import "StrappySession.h"

static const NSTimeInterval kStartupTimeout = 20.0;

@interface StrappyStartupController (Private)
- (void)servicePrepared:(StrappyAsyncResult *)result;
- (void)checkReadiness:(NSTimer *)timer;
- (void)startupTimedOut:(NSTimer *)timer;
- (void)failWithMessage:(NSString *)message;
- (void)stopTimers;
- (void)quit:(id)sender;
@end

@implementation StrappyStartupController
- (id)initWithDelegate:(id<StrappyStartupDelegate>)delegate;
{
  self = [super init];
  if (self) delegate_ = delegate;
  return self;
}

- (void)start;
{
  NSWindow *window;
  NSRect buttonFrame;
  if (started_ || invalidated_) return;
  started_ = YES;
  alert_ = [[NSAlert alloc] init];
  [alert_ setMessageText:NSLocalizedString(@"Starting background service", nil)];
  [alert_ setInformativeText:NSLocalizedString(
    @"Please wait while Strappy connects to its background service.", nil)];
  quitButton_ = [[alert_ addButtonWithTitle:NSLocalizedString(@"Quit", nil)] retain];
  [alert_ XP_layoutSingleButtonAlert];
  window = [alert_ window];
  [window setReleasedWhenClosed:NO];
  [quitButton_ setTarget:self];
  [quitButton_ setAction:@selector(quit:)];
  [quitButton_ setHidden:YES];
  [quitButton_ setEnabled:NO];
  [window setDefaultButtonCell:nil];
  /* Use the reserved button space rather than NSAlert's accessory-view API,
   * which is unavailable on Tiger. The alert owns the containing view. */
  buttonFrame = [quitButton_ frame];
  spinner_ = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(
    NSMidX(buttonFrame) - 16.0, NSMidY(buttonFrame) - 16.0, 32.0, 32.0)];
  [spinner_ setStyle:XPProgressIndicatorStyleSpinning];
  [spinner_ setIndeterminate:YES];
  [[quitButton_ superview] addSubview:spinner_];
  [spinner_ startAnimation:self];
  [window center];
  [self showWindow];
  /* Keep the ordinary main run loop alive so asynchronous installation and
   * service notifications can be delivered. Never run a nested modal loop. */
  pollTimer_ = [[NSTimer timerWithTimeInterval:0.1 target:self
    selector:@selector(checkReadiness:) userInfo:nil repeats:YES] retain];
  timeoutTimer_ = [[NSTimer timerWithTimeInterval:kStartupTimeout target:self
    selector:@selector(startupTimedOut:) userInfo:nil repeats:NO] retain];
  /* Use explicit modes: NSRunLoopCommonModes and +mainRunLoop are not
   * available on Tiger. Menu tracking must not suspend the startup deadline. */
  [[NSRunLoop currentRunLoop] addTimer:pollTimer_ forMode:NSDefaultRunLoopMode];
  [[NSRunLoop currentRunLoop] addTimer:pollTimer_ forMode:NSEventTrackingRunLoopMode];
  [[NSRunLoop currentRunLoop] addTimer:timeoutTimer_ forMode:NSDefaultRunLoopMode];
  [[NSRunLoop currentRunLoop] addTimer:timeoutTimer_ forMode:NSEventTrackingRunLoopMode];
  [[StrappyAsync backgroundTarget:[StrappyServiceController class]
    title:NSLocalizedString(@"Start background service", nil) receiver:self
    completion:@selector(servicePrepared:) context:nil]
      prepareForApplicationWithError:NULL];
}

- (void)showWindow;
{
  if (alert_ && !invalidated_) [[alert_ window] makeKeyAndOrderFront:self];
}

- (void)servicePrepared:(StrappyAsyncResult *)result;
{
  /* This alert owns installation errors, including completions after timeout. */
  [result handleError];
  if (invalidated_ || failed_) return;
  if (![result succeeded]) {
    [self failWithMessage:[[result error] localizedDescription]];
    return;
  }
  prepared_ = YES;
  [self checkReadiness:nil];
}

- (void)checkReadiness:(NSTimer *)timer;
{
  NSError *error = nil;
  id<StrappyStartupDelegate> delegate;
  (void)timer;
  if (invalidated_ || failed_ || !prepared_ ||
      ![StrappySession isBackgroundServiceReady]) return;
  if (![StrappySession initializeSessionStoreWithError:&error]) {
    /* A disconnect during validation is still a startup wait, not a failure. */
    if (![StrappySession isBackgroundServiceReady]) return;
    [self failWithMessage:[error localizedDescription]];
    return;
  }
  delegate = delegate_;
  /* The callback may release the delegate's ownership of this controller. */
  [self retain];
  [self invalidate];
  [delegate startupDidFinish:self];
  [self release];
}

- (void)startupTimedOut:(NSTimer *)timer;
{
  (void)timer;
  if (invalidated_ || failed_) return;
  [self failWithMessage:NSLocalizedString(
    @"The background service did not become ready within 20 seconds. "
     "Quit Strappy and try opening it again.", nil)];
}

- (void)failWithMessage:(NSString *)message;
{
  NSWindow *window;
  if (invalidated_ || failed_) return;
  failed_ = YES;
  [self stopTimers];
  [spinner_ stopAnimation:self];
  [spinner_ removeFromSuperview];
  [alert_ setMessageText:NSLocalizedString(
    @"Could not start background service", nil)];
  [alert_ setInformativeText:[message length] ? message : NSLocalizedString(
    @"The background service could not be started. Quit Strappy and try again.", nil)];
  [alert_ XP_layoutSingleButtonAlert];
  window = [alert_ window];
  [quitButton_ setTarget:self];
  [quitButton_ setAction:@selector(quit:)];
  [quitButton_ setHidden:NO];
  [quitButton_ setEnabled:YES];
  [window setDefaultButtonCell:[quitButton_ cell]];
  [self showWindow];
}

- (void)quit:(id)sender;
{
  (void)sender;
  if (failed_ && !invalidated_) [NSApp terminate:self];
}

- (void)stopTimers;
{
  [pollTimer_ invalidate];
  [pollTimer_ release];
  pollTimer_ = nil;
  [timeoutTimer_ invalidate];
  [timeoutTimer_ release];
  timeoutTimer_ = nil;
}

- (void)invalidate;
{
  invalidated_ = YES;
  delegate_ = nil;
  [self stopTimers];
  [spinner_ stopAnimation:self];
  if (alert_) [[alert_ window] orderOut:self];
  [quitButton_ setTarget:nil];
  [quitButton_ setAction:NULL];
}

- (void)dealloc;
{
  [self invalidate];
  [spinner_ release];
  [quitButton_ release];
  [alert_ release];
  [super dealloc];
}
@end
