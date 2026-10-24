/* Native AppKit fixture: actual startup controller/alert/timers, recorded
 * service and store. Never launches a daemon or touches user data/Keychain. */
#import "../../macOS/StrappyStartupController.h"
#import "../../macOS/StrappyServiceController.h"
#import "../../shared/StrappyAsync.h"
#import "../../shared/StrappySession.h"
#include <assert.h>

static BOOL serviceReady;
static BOOL storeValid = YES;
static BOOL disconnectOnValidation;
static unsigned int preparations;
static unsigned int validations;
static unsigned int completions;
static unsigned int handledErrors;
static unsigned int quits;

@interface StartupFixtureResult : NSObject {
  NSError *error_;
}
- (id)initWithError:(NSString *)message;
- (BOOL)succeeded;
- (NSError *)error;
- (void)handleError;
@end
@implementation StartupFixtureResult
- (id)initWithError:(NSString *)message;
{
  self = [super init];
  if (self && message) error_ = [[NSError errorWithDomain:@"StartupFixture"
    code:1 userInfo:[NSDictionary dictionaryWithObject:message
      forKey:NSLocalizedDescriptionKey]] retain];
  return self;
}
- (BOOL)succeeded; { return error_ == nil; }
- (NSError *)error; { return error_; }
- (void)handleError; { ++handledErrors; }
- (void)dealloc; { [error_ release]; [super dealloc]; }
@end

@interface StartupFixtureService : NSObject
+ (BOOL)prepareForApplicationWithError:(NSError **)error;
@end
@implementation StartupFixtureService
+ (BOOL)prepareForApplicationWithError:(NSError **)error;
{
  (void)error;
  ++preparations;
  return YES;
}
@end

@interface StartupFixtureAsync : NSObject
+ (id)backgroundTarget:(id)target title:(NSString *)title
  receiver:(id)receiver completion:(SEL)completion context:(id)context;
@end
@implementation StartupFixtureAsync
+ (id)backgroundTarget:(id)target title:(NSString *)title
  receiver:(id)receiver completion:(SEL)completion context:(id)context;
{
  (void)title; (void)receiver; (void)completion; (void)context;
  assert(target == [StartupFixtureService class]);
  /* Tests deliver the completion independently of readiness and timeout. */
  return target;
}
@end

@interface StartupFixtureSession : NSObject
+ (BOOL)isBackgroundServiceReady;
+ (BOOL)initializeSessionStoreWithError:(NSError **)error;
@end
@implementation StartupFixtureSession
+ (BOOL)isBackgroundServiceReady; { return serviceReady; }
+ (BOOL)initializeSessionStoreWithError:(NSError **)error;
{
  ++validations;
  if (disconnectOnValidation) serviceReady = NO;
  if ((!storeValid || disconnectOnValidation) && error)
    *error = [NSError errorWithDomain:@"StartupFixture" code:1 userInfo:
      [NSDictionary dictionaryWithObject:@"Fixture validation failed"
        forKey:NSLocalizedDescriptionKey]];
  return storeValid && !disconnectOnValidation;
}
@end

#define StrappyAsync StartupFixtureAsync
#define StrappyAsyncResult StartupFixtureResult
#define StrappyServiceController StartupFixtureService
#define StrappySession StartupFixtureSession
#import "../../macOS/StrappyStartupController.m"
#undef StrappySession
#undef StrappyServiceController
#undef StrappyAsyncResult
#undef StrappyAsync

@interface StrappyStartupController (FixtureInspection)
- (NSAlert *)fixtureAlert;
- (NSProgressIndicator *)fixtureSpinner;
- (NSTimer *)fixtureTimeout;
@end
@implementation StrappyStartupController (FixtureInspection)
- (NSAlert *)fixtureAlert; { return alert_; }
- (NSProgressIndicator *)fixtureSpinner; { return spinner_; }
- (NSTimer *)fixtureTimeout; { return timeoutTimer_; }
@end

@interface StartupFixtureApplication : NSApplication
@end
@implementation StartupFixtureApplication
- (void)terminate:(id)sender;
{
  (void)sender;
  ++quits;
}
@end

@interface StartupFixtureDelegate : NSObject <StrappyStartupDelegate>
@end
@implementation StartupFixtureDelegate
- (void)startupDidFinish:(StrappyStartupController *)controller;
{
  assert(![[[controller fixtureAlert] window] isVisible]);
  ++completions;
}
@end

static void Pump(NSTimeInterval interval)
{
  NSDate *end = [NSDate dateWithTimeIntervalSinceNow:interval];
  while ([end timeIntervalSinceNow] > 0) {
    NSEvent *event = [NSApp nextEventMatchingMask:~(NSUInteger)0
      untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
      inMode:NSDefaultRunLoopMode dequeue:YES];
    if (event) [NSApp sendEvent:event];
    [NSApp updateWindows];
  }
}

static unsigned int VisibleButtons(NSView *view)
{
  NSEnumerator *children;
  NSView *child;
  unsigned int count;
  if ([view isHidden]) return 0;
  count = [view isKindOfClass:[NSButton class]] ? 1U : 0U;
  children = [[view subviews] objectEnumerator];
  while ((child = [children nextObject]) != nil) count += VisibleButtons(child);
  return count;
}

static void CheckWaiting(StrappyStartupController *controller)
{
  NSAlert *alert = [controller fixtureAlert];
  NSButton *button = [[alert buttons] objectAtIndex:0];
  assert([[alert window] isVisible]);
  assert([[alert buttons] count] == 1);
  assert([button isHidden] && ![button isEnabled]);
  assert(VisibleButtons([[alert window] contentView]) == 0);
  assert([[alert window] defaultButtonCell] == nil);
  assert([[controller fixtureSpinner] superview] != nil);
  assert([[controller fixtureSpinner] isIndeterminate]);
  assert([[[controller fixtureTimeout] fireDate] timeIntervalSinceNow] > 19.0);
  assert([[[controller fixtureTimeout] fireDate] timeIntervalSinceNow] <= 20.0);
  [button performClick:nil];
  assert(quits == 0);
}

static void CheckFailed(StrappyStartupController *controller,
    NSString *message)
{
  NSAlert *alert = [controller fixtureAlert];
  NSButton *button = [[alert buttons] objectAtIndex:0];
  assert([[alert window] isVisible]);
  assert([[alert informativeText] rangeOfString:message].location != NSNotFound);
  assert(![button isHidden] && [button isEnabled]);
  assert(VisibleButtons([[alert window] contentView]) == 1);
  assert([[button title] isEqualToString:@"Quit"]);
  assert([[controller fixtureSpinner] superview] == nil);
  assert([controller fixtureTimeout] == nil);
}

static StrappyStartupController *NewController(id delegate)
{
  StrappyStartupController *controller;
  serviceReady = NO;
  storeValid = YES;
  disconnectOnValidation = NO;
  controller = [[StrappyStartupController alloc] initWithDelegate:delegate];
  [controller start];
  CheckWaiting(controller);
  return controller;
}

int main(void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  StartupFixtureDelegate *delegate = [[StartupFixtureDelegate alloc] init];
  StartupFixtureResult *success = [[StartupFixtureResult alloc] initWithError:nil];
  StartupFixtureResult *failure = [[StartupFixtureResult alloc]
    initWithError:@"Fixture install failed"];
  StrappyStartupController *controller;
  NSDate *started;
  unsigned int before;
  [StartupFixtureApplication sharedApplication];
  [NSApp finishLaunching];
  [NSApp activateIgnoringOtherApps:YES];

  controller = NewController(delegate);
  [controller start];
  assert(preparations == 1); /* Start is idempotent. */
  serviceReady = YES;
  Pump(0.2);
  assert(completions == 0 && validations == 0); /* Installer still pending. */
  serviceReady = NO;
  [controller servicePrepared:success];
  Pump(0.2);
  assert(completions == 0 && validations == 0); /* No handshake yet. */
  serviceReady = YES;
  Pump(0.2);
  assert(completions == 1 && validations == 1);
  [controller servicePrepared:success];
  [controller startupTimedOut:nil];
  [controller showWindow];
  assert(![[[controller fixtureAlert] window] isVisible]);
  assert(completions == 1);
  [controller release];

  controller = NewController(delegate);
  before = handledErrors;
  [controller servicePrepared:failure];
  CheckFailed(controller,@"Fixture install failed");
  assert(handledErrors == before + 1);
  serviceReady = YES;
  [controller servicePrepared:success];
  Pump(0.2);
  assert(completions == 1);
  [[[[controller fixtureAlert] buttons] objectAtIndex:0] performClick:nil];
  assert(quits == 1);
  quits = 0;
  [controller invalidate];
  [controller release];

  controller = NewController(delegate);
  storeValid = NO;
  serviceReady = YES;
  [controller servicePrepared:success];
  CheckFailed(controller,@"Fixture validation failed");
  [controller invalidate];
  [controller release];

  controller = NewController(delegate);
  disconnectOnValidation = YES;
  serviceReady = YES;
  [controller servicePrepared:success];
  CheckWaiting(controller);
  disconnectOnValidation = NO;
  serviceReady = YES;
  Pump(0.2);
  assert(completions == 2);
  [controller release];

  controller = NewController(delegate);
  [controller invalidate];
  serviceReady = YES;
  [controller servicePrepared:failure];
  [controller servicePrepared:success];
  [controller startupTimedOut:nil];
  Pump(0.2);
  assert(completions == 2);
  assert(![[[controller fixtureAlert] window] isVisible]);
  [controller release];

  /* Exercise the real 20-second timer, with no installer completion. A second
   * case below covers an installed daemon that never answers the handshake. */
  controller = NewController(delegate);
  started = [NSDate date];
  while ([controller fixtureTimeout] && [started timeIntervalSinceNow] > -23.0)
    Pump(0.05);
  assert([started timeIntervalSinceNow] <= -19.9);
  CheckFailed(controller,@"20 seconds");
  serviceReady = YES;
  [controller servicePrepared:success];
  [controller checkReadiness:nil];
  assert(completions == 2); /* A late reply cannot dismiss terminal failure. */
  [controller invalidate];
  [controller release];

  controller = NewController(delegate);
  [controller servicePrepared:success];
  [controller startupTimedOut:nil];
  CheckFailed(controller,@"20 seconds");
  [controller invalidate];
  [controller release];

  puts("PASS: startup alert, buttonless spinner, install/handshake ordering, "
    "validation, disconnect recovery, 20-second timeout, Quit, late replies "
    "and teardown (recorded service; native AppKit)");
  [failure release];
  [success release];
  [delegate release];
  [pool release];
  return 0;
}
