#import "StrappyAsync.h"
#import "XPFoundation.h"
#include <string.h>
#include <stdlib.h>

NSString * const StrappyAsyncCommandFailedNotification = @"StrappyAsyncCommandFailedNotification";
NSString * const StrappyAsyncActivityChangedNotification = @"StrappyAsyncActivityChangedNotification";

@interface StrappyAsyncResult (Private)
- (id)initWithValue:(id)value error:(NSError *)error context:(id)context title:(NSString *)title;
@end
@implementation StrappyAsyncResult
- (id)initWithValue:(id)value error:(NSError *)error context:(id)context title:(NSString *)title
{
  if ((self = [super init])) { value_ = [value retain]; error_ = [error retain]; context_ = [context retain]; title_ = [title copy]; }
  return self;
}
- (id)value { return value_; }
- (NSError *)error { return error_; }
- (id)context { return context_; }
- (BOOL)succeeded { return error_ == nil; }
- (void)handleError { errorHandled_ = YES; }
- (void)dealloc { [value_ release]; [error_ release]; [context_ release]; [title_ release]; [super dealloc]; }
- (void)publishUnhandledError
{
  if (error_ && !errorHandled_) [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappyAsyncCommandFailedNotification object:nil
    userInfo:[NSDictionary dictionaryWithObjectsAndKeys:error_,@"error",title_ ? title_ : @"Action failed",@"title",nil]];
}
@end

@interface StrappyAsyncRequest : NSObject {
 @public
  NSInvocation *invocation;
  id receiver;
  SEL completion;
  id context;
  NSString *title;
  BOOL query;
  BOOL superseded;
  StrappyAsyncResult *result;
}
- (void)execute;
- (void)deliver;
@end

static NSMutableArray *StrappyCommandQueue;
static NSMutableArray *StrappyQueryQueue;
static NSMutableArray *StrappyQueries;
static BOOL StrappyCommandWorker, StrappyQueryWorker;
static NSUInteger StrappyCommandCount;

@interface StrappyAsync (Private)
+ (BOOL)enqueue:(StrappyAsyncRequest *)request;
+ (void)runCommands:(id)unused;
+ (void)runQueries:(id)unused;
+ (void)deliverNotification:(NSNotification *)notification;
@end

@implementation StrappyAsyncRequest
- (void)dealloc
{
  [invocation release]; [receiver release]; [context release]; [title release]; [result release];
  [super dealloc];
}
- (void)execute
{
  NSMethodSignature *signature = [invocation methodSignature];
  NSError *error = nil;
  NSError **errorPointer = &error;
  id value = nil;
  NSUInteger index;
  for (index = 2; index < [signature numberOfArguments]; ++index) {
    const char *type = [signature getArgumentTypeAtIndex:index];
    if (!strcmp(type,"^@")) [invocation setArgument:&errorPointer atIndex:(NSInteger)index];
  }
  @try {
    [invocation invoke];
    if ([signature methodReturnLength]) {
      const char *type = [signature methodReturnType];
      if (!strcmp(type,"@")) [invocation getReturnValue:&value];
      else if (!strcmp(type,@encode(BOOL))) {
        BOOL ok = NO; [invocation getReturnValue:&ok];
        value = [NSNumber numberWithBool:ok];
        if (!ok && !error) error = [NSError errorWithDomain:@"StrappyAsync" code:1 userInfo:
          [NSDictionary dictionaryWithObject:@"The action could not be completed. Reload stored state before trying again." forKey:NSLocalizedDescriptionKey]];
      }
    }
    if (!value && [signature methodReturnLength] && !error)
      error = [NSError errorWithDomain:@"StrappyAsync" code:2 userInfo:
        [NSDictionary dictionaryWithObject:@"The requested result is unavailable." forKey:NSLocalizedDescriptionKey]];
  } @catch (NSException *exception) {
    /* Exception reasons and invocation arguments may include credentials. */
    (void)exception;
    error = [NSError errorWithDomain:@"StrappyAsync" code:3 userInfo:
      [NSDictionary dictionaryWithObject:@"The action could not be completed." forKey:NSLocalizedDescriptionKey]];
  }
  result = [[StrappyAsyncResult alloc] initWithValue:value error:error context:context title:title];
}
- (void)deliver
{
  BOOL discard;
  NSAssert([NSThread XP_isMainThread], @"Async delivery requires the main thread");
  @synchronized([StrappyAsync class]) {
    discard = superseded;
    if (query) [StrappyQueries removeObjectIdenticalTo:self];
    else --StrappyCommandCount;
  }
  if (!discard && receiver && completion) [receiver performSelector:completion withObject:result];
  if (!query) {
    [result publishUnhandledError];
    [StrappyAsync postNotificationName:StrappyAsyncActivityChangedNotification object:nil];
  }
}
@end

@interface StrappyAsyncProxy : NSProxy {
  id target_;
  StrappyAsyncRequest *request_;
}
- (id)initWithTarget:(id)target request:(StrappyAsyncRequest *)request;
@end
@implementation StrappyAsyncProxy
- (id)initWithTarget:(id)target request:(StrappyAsyncRequest *)request
{ target_ = [target retain]; request_ = [request retain]; return self; }
- (NSMethodSignature *)methodSignatureForSelector:(SEL)selector { return [target_ methodSignatureForSelector:selector]; }
- (void)forwardInvocation:(NSInvocation *)call
{
  NSMethodSignature *signature = [call methodSignature];
  NSUInteger index;
  BOOL queued;
  NSAssert(request_->invocation == nil, @"Use a fresh async proxy for each request");
  NSInvocation *captured = [NSInvocation invocationWithMethodSignature:signature];
  [captured setTarget:target_]; [captured setSelector:[call selector]];
  for (index = 2; index < [signature numberOfArguments]; ++index) {
    const char *type = [signature getArgumentTypeAtIndex:index];
    if (!strcmp(type,"^@")) { NSError **empty = NULL; [captured setArgument:&empty atIndex:(NSInteger)index]; }
    else if (type[0] == '@') {
      id value = nil;
      [call getArgument:&value atIndex:(NSInteger)index];
      if ([value conformsToProtocol:@protocol(NSCopying)]) value = [[value copy] autorelease];
      [captured setArgument:&value atIndex:(NSInteger)index];
    } else {
      NSUInteger size = 0;
      void *bytes;
      NSAssert(strchr("cCsSiIlLqQBfd",type[0]) != NULL, @"Unsupported async argument");
      NSGetSizeAndAlignment(type,&size,NULL);
      bytes = malloc(size); NSAssert(bytes != NULL,@"Could not capture async argument");
      [call getArgument:bytes atIndex:(NSInteger)index]; [captured setArgument:bytes atIndex:(NSInteger)index]; free(bytes);
    }
  }
  [captured retainArguments];
  request_->invocation = [captured retain];
  queued = [StrappyAsync enqueue:request_];
  if ([signature methodReturnLength]) {
    if (!strcmp([signature methodReturnType],@encode(BOOL))) [call setReturnValue:&queued];
    else { id empty = nil; NSAssert(!strcmp([signature methodReturnType],"@"), @"Unsupported async return type"); [call setReturnValue:&empty]; }
  }
}
- (void)dealloc { [target_ release]; [request_ release]; [super dealloc]; }
@end

@implementation StrappyAsync
+ (id)target:(id)target title:(NSString *)title receiver:(id)receiver completion:(SEL)completion context:(id)context query:(BOOL)query
{
  StrappyAsyncRequest *request = [[[StrappyAsyncRequest alloc] init] autorelease];
  request->receiver = [receiver retain]; request->completion = completion;
  request->context = [context retain]; request->title = [title copy]; request->query = query;
  return [[[StrappyAsyncProxy alloc] initWithTarget:target request:request] autorelease];
}
+ (id)commandTarget:(id)target title:(NSString *)title receiver:(id)receiver completion:(SEL)completion context:(id)context
{ return [self target:target title:title receiver:receiver completion:completion context:context query:NO]; }
+ (id)queryTarget:(id)target receiver:(id)receiver completion:(SEL)completion context:(id)context
{ return [self target:target title:nil receiver:receiver completion:completion context:context query:YES]; }
+ (void)reportError:(NSError *)error title:(NSString *)title
{
  if (error) [self postNotificationName:StrappyAsyncCommandFailedNotification object:nil
    userInfo:[NSDictionary dictionaryWithObjectsAndKeys:error,@"error",title,@"title",nil]];
}
+ (NSUInteger)pendingCommandCount
{ @synchronized(self) { return StrappyCommandCount; } return 0; }
+ (BOOL)enqueue:(StrappyAsyncRequest *)request
{
  BOOL start = NO, full = NO;
  @synchronized(self) {
    NSMutableArray *queue;
    if (!StrappyCommandQueue) {
      StrappyCommandQueue = [[NSMutableArray alloc] init]; StrappyQueryQueue = [[NSMutableArray alloc] init];
      StrappyQueries = [[NSMutableArray alloc] init];
    }
    queue = request->query ? StrappyQueryQueue : StrappyCommandQueue;
    if (request->query) {
      NSUInteger i;
      for (i = 0; i < [StrappyQueries count]; ++i) {
        StrappyAsyncRequest *old = [StrappyQueries objectAtIndex:i];
        if (old->receiver == request->receiver && old->completion == request->completion) old->superseded = YES;
      }
      [StrappyQueries addObject:request];
    } else ++StrappyCommandCount;
    full = [queue count] >= 128;
    if (!full) {
      [queue addObject:request];
      start = request->query ? !StrappyQueryWorker : !StrappyCommandWorker;
      if (request->query) StrappyQueryWorker = YES; else StrappyCommandWorker = YES;
    }
  }
  if (full) {
    request->result = [[StrappyAsyncResult alloc] initWithValue:nil error:
      [NSError errorWithDomain:@"StrappyAsync" code:4 userInfo:[NSDictionary dictionaryWithObject:
        @"Too many actions are pending. Wait for them to finish and try again." forKey:NSLocalizedDescriptionKey]] context:request->context title:request->title];
    [request performSelectorOnMainThread:@selector(deliver) withObject:nil waitUntilDone:NO];
  } else if (start) [NSThread detachNewThreadSelector:request->query ? @selector(runQueries:) : @selector(runCommands:)
    toTarget:self withObject:nil];
  if (!request->query) [self postNotificationName:StrappyAsyncActivityChangedNotification object:nil];
  /* Even rejection has an asynchronous completion; never report it twice. */
  return YES;
}
+ (void)runQueue:(BOOL)query
{
  for (;;) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    StrappyAsyncRequest *request = nil;
    BOOL discard = NO;
    @synchronized(self) {
      NSMutableArray *queue = query ? StrappyQueryQueue : StrappyCommandQueue;
      if ([queue count]) {
        request = [[queue objectAtIndex:0] retain]; [queue removeObjectAtIndex:0]; discard = request->superseded;
      } else { if (query) StrappyQueryWorker = NO; else StrappyCommandWorker = NO; }
    }
    if (!request) { [pool release]; return; }
    if (!discard) [request execute];
    [request performSelectorOnMainThread:@selector(deliver) withObject:nil waitUntilDone:NO];
    [request release]; [pool release];
  }
}
+ (void)runCommands:(id)unused { (void)unused; [self runQueue:NO]; }
+ (void)runQueries:(id)unused { (void)unused; [self runQueue:YES]; }
+ (void)postNotificationName:(NSString *)name object:(id)object
{ [self postNotificationName:name object:object userInfo:nil]; }
+ (void)postNotificationName:(NSString *)name object:(id)object userInfo:(NSDictionary *)info
{
  NSNotification *notification = [NSNotification notificationWithName:name object:object userInfo:info];
  if ([NSThread XP_isMainThread]) [self deliverNotification:notification];
  else [self performSelectorOnMainThread:@selector(deliverNotification:) withObject:notification waitUntilDone:NO];
}
+ (void)deliverNotification:(NSNotification *)notification
{ [[NSNotificationCenter defaultCenter] postNotification:notification]; }
@end
