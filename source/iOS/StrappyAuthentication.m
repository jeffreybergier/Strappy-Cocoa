#import "XPFoundation.h"
#import "StrappyAsync.h"
#import "StrappyAuthentication.h"

#import "StrappySession.h"

NSString * const StrappyAuthenticationDidChangeNotification =
  @"StrappyAuthenticationDidChangeNotification";

static NSMutableDictionary *StrappyAuthenticationContexts = nil;

@interface StrappyAuthentication ()
- (id)initWithProviderAccountIdentifier:(NSString *)providerAccountIdentifier;
- (NSString *)designatedProviderAccountIdentifier;
- (BOOL)shouldCancelOperationWithGeneration:(NSUInteger)generation;
- (void)runDeviceLogin:(NSNumber *)generationNumber;
- (void)runCredentialRefresh:(NSDictionary *)operation;
- (void)postDidChangeNotification;
- (void)notifyDidChange;
@end

@implementation StrappyAuthentication

- (NSString *)designatedProviderAccountIdentifier
{
  return ([providerAccountIdentifier_ length] > 0U) ?
    providerAccountIdentifier_ : [StrappySession
    designatedProviderAccountIdentifierForProviderIdentifier:
      @"openai_chatgpt"
    error:NULL];
}

+ (StrappyAuthentication *)sharedAuthentication
{
  NSString *providerAccountIdentifier;

  providerAccountIdentifier = [StrappySession
    designatedProviderAccountIdentifierForProviderIdentifier:
      @"openai_chatgpt"
    error:NULL];
  return [self authenticationForProviderAccountIdentifier:
    providerAccountIdentifier];
}

+ (StrappyAuthentication *)authenticationForProviderAccountIdentifier:
  (NSString *)providerAccountIdentifier
{
  StrappyAuthentication *context;

  if ([providerAccountIdentifier length] == 0U) {
    return nil;
  }
  @synchronized(self) {
    if (StrappyAuthenticationContexts == nil) {
      StrappyAuthenticationContexts = [[NSMutableDictionary alloc] init];
    }
    context = [StrappyAuthenticationContexts
      objectForKey:providerAccountIdentifier];
    if (context == nil) {
      context = [[StrappyAuthentication alloc]
        initWithProviderAccountIdentifier:providerAccountIdentifier];
      [StrappyAuthenticationContexts setObject:context
                                        forKey:providerAccountIdentifier];
    }
  }
  return context;
}

+ (void)forgetAuthenticationForProviderAccountIdentifier:
  (NSString *)providerAccountIdentifier
{
  if ([providerAccountIdentifier length] == 0U) {
    return;
  }
  @synchronized(self) {
    [[StrappyAuthenticationContexts objectForKey:providerAccountIdentifier]
      cancelChatGPTDeviceLogin];
    [StrappyAuthenticationContexts removeObjectForKey:
      providerAccountIdentifier];
  }
}

+ (BOOL)isChatGPTProviderEnabled
{
  return [StrappySession isChatGPTProviderEnabled];
}

- (id)init
{
  return [self initWithProviderAccountIdentifier:[StrappySession
    designatedProviderAccountIdentifierForProviderIdentifier:
      @"openai_chatgpt"
    error:NULL]];
}

- (id)initWithProviderAccountIdentifier:(NSString *)providerAccountIdentifier
{
  NSString *accountIdentifier;
  BOOL loaded;

  if ((self = [super init])) {
    providerAccountIdentifier_ = [providerAccountIdentifier copy];
    accountIdentifier = nil;
    NSDictionary *credential = [StrappySession credentialStatusForProviderAccountIdentifier:
      providerAccountIdentifier error:NULL];
    loaded = [[credential objectForKey:@"present"] boolValue];
    accountIdentifier = [credential objectForKey:@"identity"];
    if (loaded) {
      state_ = StrappyAuthenticationStateSignedIn;
      accountIdentifier_ = [accountIdentifier copy];
    } else {
      state_ = StrappyAuthenticationStateSignedOut;
    }
  }
  return self;
}

- (StrappyAuthenticationState)state
{
  StrappyAuthenticationState state;

  @synchronized(self) {
    state = state_;
  }
  return state;
}

- (NSString *)verificationURL
{
  NSString *value;

  @synchronized(self) {
    value = [verificationURL_ copy];
  }
  return value;
}

- (NSString *)userCode
{
  NSString *value;

  @synchronized(self) {
    value = [userCode_ copy];
  }
  return value;
}

- (NSString *)accountIdentifier
{
  NSString *value;

  @synchronized(self) {
    value = [accountIdentifier_ copy];
  }
  return value;
}

- (NSString *)errorMessage
{
  NSString *value;

  @synchronized(self) {
    value = [errorMessage_ copy];
  }
  return value;
}

- (BOOL)isOperationInFlight
{
  StrappyAuthenticationState state;

  state = [self state];
  return (state == StrappyAuthenticationStateRequestingCode) ||
    (state == StrappyAuthenticationStateAwaitingUser) ||
    (state == StrappyAuthenticationStateRefreshing);
}

- (BOOL)hasStoredCredentials
{
  return [[[StrappySession credentialStatusForProviderAccountIdentifier:
    [self designatedProviderAccountIdentifier] error:NULL] objectForKey:@"present"] boolValue];
}

- (void)postDidChangeNotification
{
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappyAuthenticationDidChangeNotification
                  object:self];
}

- (void)notifyDidChange
{
  if ([NSThread isMainThread]) {
    [self postDidChangeNotification];
  } else {
    [self performSelectorOnMainThread:@selector(postDidChangeNotification)
                           withObject:nil
                        waitUntilDone:NO];
  }
}

- (BOOL)shouldCancelOperationWithGeneration:(NSUInteger)generation
{
  BOOL shouldCancel;

  @synchronized(self) {
    shouldCancel = cancellationRequested_ ||
      (generation != operationGeneration_);
  }
  return shouldCancel;
}

- (BOOL)startChatGPTDeviceLogin
{
  NSUInteger generation;

  if (![[self class] isChatGPTProviderEnabled]) {
    return NO;
  }
  @synchronized(self) {
    if ((state_ == StrappyAuthenticationStateRequestingCode) ||
        (state_ == StrappyAuthenticationStateAwaitingUser) ||
        (state_ == StrappyAuthenticationStateRefreshing)) {
      return NO;
    }
    operationGeneration_++;
    generation = operationGeneration_;
    cancellationRequested_ = NO;
    state_ = StrappyAuthenticationStateRequestingCode;
    verificationURL_ = nil;
    userCode_ = nil;
    errorMessage_ = nil;
  }
  [self notifyDidChange];
  [NSThread detachNewThreadSelector:@selector(runDeviceLogin:)
                           toTarget:self
                         withObject:[NSNumber numberWithUnsignedInteger:
                           generation]];
  return YES;
}

- (void)cancelChatGPTDeviceLogin
{
  BOOL changed;

  changed = NO;
  @synchronized(self) {
    if ((state_ == StrappyAuthenticationStateRequestingCode) ||
        (state_ == StrappyAuthenticationStateAwaitingUser)) {
      cancellationRequested_ = YES;
      operationGeneration_++;
      state_ = StrappyAuthenticationStateCancelled;
      verificationURL_ = nil;
      userCode_ = nil;
      errorMessage_ = nil;
      changed = YES;
    }
  }
  if (changed) {
    [self notifyDidChange];
  }
}

- (void)runDeviceLogin:(NSNumber *)generationNumber
{
  @autoreleasepool {
    NSUInteger generation;
    NSError *error;
    NSString *accountIdentifier;
    BOOL stateChanged;
    NSString *providerAccountIdentifier;

    generation = [generationNumber unsignedIntegerValue];
    providerAccountIdentifier = [self designatedProviderAccountIdentifier];
    error = nil;
    accountIdentifier = [StrappySession
      performChatGPTDeviceAuthorizationForProviderAccountIdentifier:
        providerAccountIdentifier
      observer:self
      context:generationNumber
      error:&error];

    stateChanged = NO;
    @synchronized(self) {
      if ((generation == operationGeneration_) &&
        !cancellationRequested_) {
        if ([accountIdentifier length] > 0U) {
          state_ = StrappyAuthenticationStateSignedIn;
          accountIdentifier_ = [accountIdentifier copy];
          errorMessage_ = nil;
        } else {
          state_ = StrappyAuthenticationStateError;
          accountIdentifier_ = nil;
          errorMessage_ = ([[error localizedDescription] length] > 0U) ?
            [error localizedDescription] :
            NSLocalizedString(@"ChatGPT sign-in failed.", nil);
        }
        verificationURL_ = nil;
        userCode_ = nil;
        stateChanged = YES;
      }
    }
    if (stateChanged) {
      [self notifyDidChange];
    }
  }
}

- (BOOL)strappyChatGPTAuthorizationShouldCancelWithContext:(id)context
{
  return [self shouldCancelOperationWithGeneration:
    [(NSNumber *)context unsignedIntegerValue]];
}

- (void)strappyChatGPTAuthorizationDidReceiveVerificationURL:
          (NSString *)verificationURL
                                                       userCode:
          (NSString *)userCode
                                                        context:
          (id)context
{
  NSUInteger generation;
  BOOL stateChanged;

  generation = [(NSNumber *)context unsignedIntegerValue];
  stateChanged = NO;
  @synchronized(self) {
    if ((generation == operationGeneration_) && !cancellationRequested_) {
      state_ = StrappyAuthenticationStateAwaitingUser;
      verificationURL_ = [verificationURL copy];
      userCode_ = [userCode copy];
      accountIdentifier_ = nil;
      errorMessage_ = nil;
      stateChanged = YES;
    }
  }
  if (stateChanged) {
    [self notifyDidChange];
  }
}

- (BOOL)refreshChatGPTCredentialsIfNeeded
{
  NSString *accountIdentifier;
  NSUInteger generation;
  BOOL stateChanged;
  BOOL loaded;
  NSString *providerAccountIdentifier;

  if (![[self class] isChatGPTProviderEnabled]) {
    return NO;
  }
  accountIdentifier = nil;
  providerAccountIdentifier = [self designatedProviderAccountIdentifier];
  NSDictionary *credential = [StrappySession credentialStatusForProviderAccountIdentifier:
    providerAccountIdentifier error:NULL];
  loaded = [[credential objectForKey:@"present"] boolValue];
  accountIdentifier = [credential objectForKey:@"identity"];
  if (!loaded) {
    stateChanged = NO;
    @synchronized(self) {
      if ((state_ != StrappyAuthenticationStateRequestingCode) &&
          (state_ != StrappyAuthenticationStateAwaitingUser) &&
          (state_ != StrappyAuthenticationStateRefreshing) &&
          (state_ != StrappyAuthenticationStateSignedOut)) {
        state_ = StrappyAuthenticationStateSignedOut;
        accountIdentifier_ = nil;
        errorMessage_ = nil;
        stateChanged = YES;
      }
    }
    if (stateChanged) {
      [self notifyDidChange];
    }
    return NO;
  }

  if (![[credential objectForKey:@"needs_refresh"] boolValue]) {
    stateChanged = NO;
    @synchronized(self) {
      if ((state_ != StrappyAuthenticationStateRequestingCode) &&
          (state_ != StrappyAuthenticationStateAwaitingUser) &&
          (state_ != StrappyAuthenticationStateRefreshing)) {
        state_ = StrappyAuthenticationStateSignedIn;
        accountIdentifier_ = [accountIdentifier copy];
        errorMessage_ = nil;
        stateChanged = YES;
      }
    }
    if (stateChanged) {
      [self notifyDidChange];
    }
    return YES;
  }

  @synchronized(self) {
    if ((state_ == StrappyAuthenticationStateRequestingCode) ||
        (state_ == StrappyAuthenticationStateAwaitingUser) ||
        (state_ == StrappyAuthenticationStateRefreshing)) {
      return NO;
    }
    operationGeneration_++;
    generation = operationGeneration_;
    cancellationRequested_ = NO;
    state_ = StrappyAuthenticationStateRefreshing;
    accountIdentifier_ = [accountIdentifier copy];
    errorMessage_ = nil;
  }
  [self notifyDidChange];
  [NSThread detachNewThreadSelector:@selector(runCredentialRefresh:)
                           toTarget:self
                         withObject:[NSDictionary dictionaryWithObjectsAndKeys:
                           [NSNumber numberWithUnsignedInteger:generation],
                             @"generation",
                           [accountIdentifier copy], @"account_identifier",
                           [providerAccountIdentifier copy],
                             @"provider_account_identifier",
                           nil]];
  return YES;
}

- (void)runCredentialRefresh:(NSDictionary *)operation
{
  @autoreleasepool {
    NSUInteger generation;
    NSString *previousAccountIdentifier;
    NSString *providerAccountIdentifier;
    NSString *accountIdentifier;
    NSError *error;
    BOOL stateChanged;

    generation = [[operation objectForKey:@"generation"]
      unsignedIntegerValue];
    previousAccountIdentifier = [operation
      objectForKey:@"account_identifier"];
    providerAccountIdentifier = [operation
      objectForKey:@"provider_account_identifier"];
    error = nil;
    accountIdentifier = [StrappySession
      refreshChatGPTCredentialsForProviderAccountIdentifier:
        providerAccountIdentifier
      expectedAccountIdentifier:previousAccountIdentifier
      observer:self
      context:[operation objectForKey:@"generation"]
      error:&error];

    stateChanged = NO;
    @synchronized(self) {
      if ((generation == operationGeneration_) &&
          !cancellationRequested_) {
        if ([accountIdentifier length] > 0U) {
          state_ = StrappyAuthenticationStateSignedIn;
          accountIdentifier_ = [accountIdentifier copy];
          errorMessage_ = nil;
        } else {
          state_ = StrappyAuthenticationStateError;
          errorMessage_ = ([[error localizedDescription] length] > 0U) ?
            [error localizedDescription] :
            NSLocalizedString(@"ChatGPT credential refresh failed.", nil);
        }
        verificationURL_ = nil;
        userCode_ = nil;
        stateChanged = YES;
      }
    }
    if (stateChanged) {
      [self notifyDidChange];
    }
  }
}

- (BOOL)signOutChatGPT
{
  BOOL deleted;
  NSString *providerAccountIdentifier;

  @synchronized(self) {
    cancellationRequested_ = YES;
    operationGeneration_++;
    verificationURL_ = nil;
    userCode_ = nil;
  }
  providerAccountIdentifier = [self designatedProviderAccountIdentifier];
  deleted = [StrappySession deleteChatGPTCredentialsForProviderAccountIdentifier:providerAccountIdentifier error:NULL];
  @synchronized(self) {
    accountIdentifier_ = nil;
    if (deleted) {
      state_ = StrappyAuthenticationStateSignedOut;
      errorMessage_ = nil;
    } else {
      state_ = StrappyAuthenticationStateError;
      errorMessage_ = NSLocalizedString(
        @"The Keychain refused to remove the ChatGPT credential.", nil);
    }
  }
  [self notifyDidChange];
  return deleted;
}

@end
