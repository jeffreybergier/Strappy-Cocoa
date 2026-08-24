#import "StrappyAuthentication.h"

#import "StrappyKeychain.h"
#import "StrappySession.h"
#import "XPFoundation.h"

NSString * const StrappyAuthenticationDidChangeNotification =
  @"StrappyAuthenticationDidChangeNotification";

static const long long kStrappyAuthenticationRefreshLeewayMilliseconds =
  5LL * 60LL * 1000LL;
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

static void StrappyAuthenticationReplaceString(NSString **slot,
                                               NSString *value)
{
  NSString *copy;

  copy = [value copy];
  [*slot release];
  *slot = copy;
}

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
      context = [[[StrappyAuthentication alloc]
        initWithProviderAccountIdentifier:providerAccountIdentifier]
        autorelease];
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
  StrappyKeychain *keychain;
  BOOL loaded;

  if ((self = [super init])) {
    providerAccountIdentifier_ = [providerAccountIdentifier copy];
    accountIdentifier = nil;
    keychain = [StrappyKeychain sharedKeychain];
    @synchronized(keychain) {
      loaded = [keychain loadChatGPTAccessToken:NULL
                                      refreshToken:NULL
                                 accountIdentifier:&accountIdentifier
                              expiresAtMilliseconds:NULL
                         providerAccountIdentifier:
                           providerAccountIdentifier];
    }
    if (loaded) {
      state_ = StrappyAuthenticationStateSignedIn;
      StrappyAuthenticationReplaceString(&accountIdentifier_,
                                          accountIdentifier);
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
    value = [[verificationURL_ copy] autorelease];
  }
  return value;
}

- (NSString *)userCode
{
  NSString *value;

  @synchronized(self) {
    value = [[userCode_ copy] autorelease];
  }
  return value;
}

- (NSString *)accountIdentifier
{
  NSString *value;

  @synchronized(self) {
    value = [[accountIdentifier_ copy] autorelease];
  }
  return value;
}

- (NSString *)errorMessage
{
  NSString *value;

  @synchronized(self) {
    value = [[errorMessage_ copy] autorelease];
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
  StrappyKeychain *keychain;
  BOOL stored;

  keychain = [StrappyKeychain sharedKeychain];
  {
    NSString *providerAccountIdentifier;
    NSObject *credentialLock;

    providerAccountIdentifier = [self designatedProviderAccountIdentifier];
    credentialLock = [keychain
      credentialLockForProviderIdentifier:@"openai_chatgpt"
      providerAccountIdentifier:providerAccountIdentifier];
  @synchronized(credentialLock) {
    stored = [keychain
      hasChatGPTCredentialsForProviderAccountIdentifier:
        providerAccountIdentifier];
  }
  }
  return stored;
}

- (void)postDidChangeNotification
{
  [[NSNotificationCenter defaultCenter]
    postNotificationName:StrappyAuthenticationDidChangeNotification
                  object:self];
}

- (void)notifyDidChange
{
  if ([NSThread XP_isMainThread]) {
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
    if ([self isOperationInFlight]) {
      return NO;
    }
    operationGeneration_++;
    generation = operationGeneration_;
    cancellationRequested_ = NO;
    state_ = StrappyAuthenticationStateRequestingCode;
    StrappyAuthenticationReplaceString(&verificationURL_, nil);
    StrappyAuthenticationReplaceString(&userCode_, nil);
    StrappyAuthenticationReplaceString(&errorMessage_, nil);
  }
  [self notifyDidChange];
  [NSThread detachNewThreadSelector:@selector(runDeviceLogin:)
                           toTarget:self
                         withObject:[NSNumber XP_numberWithUnsignedInteger:
                           (XPUInteger)generation]];
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
      StrappyAuthenticationReplaceString(&verificationURL_, nil);
      StrappyAuthenticationReplaceString(&userCode_, nil);
      StrappyAuthenticationReplaceString(&errorMessage_, nil);
      changed = YES;
    }
  }
  if (changed) {
    [self notifyDidChange];
  }
}

- (void)runDeviceLogin:(NSNumber *)generationNumber
{
  NSAutoreleasePool *pool;
  NSUInteger generation;
  NSError *error;
  NSString *accountIdentifier;
  BOOL stateChanged;
  NSString *providerAccountIdentifier;

  pool = [[NSAutoreleasePool alloc] init];
  generation = [generationNumber XP_unsignedIntegerValue];
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
    if ((generation == operationGeneration_) && !cancellationRequested_) {
      if ([accountIdentifier length] > 0U) {
        state_ = StrappyAuthenticationStateSignedIn;
        StrappyAuthenticationReplaceString(&accountIdentifier_,
                                            accountIdentifier);
        StrappyAuthenticationReplaceString(&errorMessage_, nil);
      } else {
        state_ = StrappyAuthenticationStateError;
        StrappyAuthenticationReplaceString(
          &errorMessage_,
          ([[error localizedDescription] length] > 0U) ?
            [error localizedDescription] :
            NSLocalizedString(@"ChatGPT sign-in failed.", nil));
      }
      StrappyAuthenticationReplaceString(&verificationURL_, nil);
      StrappyAuthenticationReplaceString(&userCode_, nil);
      stateChanged = YES;
    }
  }
  if (stateChanged) {
    [self notifyDidChange];
  }
  [pool drain];
}

- (BOOL)strappyChatGPTAuthorizationShouldCancelWithContext:(id)context
{
  return [self shouldCancelOperationWithGeneration:
    [(NSNumber *)context XP_unsignedIntegerValue]];
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

  generation = [(NSNumber *)context XP_unsignedIntegerValue];
  stateChanged = NO;
  @synchronized(self) {
    if ((generation == operationGeneration_) && !cancellationRequested_) {
      state_ = StrappyAuthenticationStateAwaitingUser;
      StrappyAuthenticationReplaceString(&verificationURL_, verificationURL);
      StrappyAuthenticationReplaceString(&userCode_, userCode);
      StrappyAuthenticationReplaceString(&accountIdentifier_, nil);
      StrappyAuthenticationReplaceString(&errorMessage_, nil);
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
  StrappyKeychain *keychain;
  long long expiresAtMilliseconds;
  long long nowMilliseconds;
  NSUInteger generation;
  BOOL loaded;
  NSString *providerAccountIdentifier;

  if (![[self class] isChatGPTProviderEnabled]) {
    return NO;
  }
  accountIdentifier = nil;
  expiresAtMilliseconds = 0LL;
  keychain = [StrappyKeychain sharedKeychain];
  providerAccountIdentifier = [self designatedProviderAccountIdentifier];
  @synchronized([keychain
    credentialLockForProviderIdentifier:@"openai_chatgpt"
    providerAccountIdentifier:providerAccountIdentifier]) {
    loaded = [keychain loadChatGPTAccessToken:NULL
                                    refreshToken:NULL
                               accountIdentifier:&accountIdentifier
                            expiresAtMilliseconds:&expiresAtMilliseconds
                       providerAccountIdentifier:providerAccountIdentifier];
  }
  if (!loaded) {
    @synchronized(self) {
      if (![self isOperationInFlight]) {
        state_ = StrappyAuthenticationStateSignedOut;
        StrappyAuthenticationReplaceString(&accountIdentifier_, nil);
        StrappyAuthenticationReplaceString(&errorMessage_, nil);
      }
    }
    [self notifyDidChange];
    return NO;
  }
  nowMilliseconds = [StrappySession currentTimestampMilliseconds];
  if ((nowMilliseconds > 0LL) &&
      ((expiresAtMilliseconds - nowMilliseconds) >
       kStrappyAuthenticationRefreshLeewayMilliseconds)) {
    @synchronized(self) {
      if (![self isOperationInFlight]) {
        state_ = StrappyAuthenticationStateSignedIn;
        StrappyAuthenticationReplaceString(&accountIdentifier_,
                                            accountIdentifier);
        StrappyAuthenticationReplaceString(&errorMessage_, nil);
      }
    }
    [self notifyDidChange];
    return YES;
  }
  @synchronized(self) {
    if ([self isOperationInFlight]) {
      return NO;
    }
    operationGeneration_++;
    generation = operationGeneration_;
    cancellationRequested_ = NO;
    state_ = StrappyAuthenticationStateRefreshing;
    StrappyAuthenticationReplaceString(&accountIdentifier_, accountIdentifier);
    StrappyAuthenticationReplaceString(&errorMessage_, nil);
  }
  [self notifyDidChange];
  [NSThread detachNewThreadSelector:@selector(runCredentialRefresh:)
                           toTarget:self
                         withObject:[NSDictionary dictionaryWithObjectsAndKeys:
                           [NSNumber XP_numberWithUnsignedInteger:
                             (XPUInteger)generation],
                             @"generation",
                           accountIdentifier, @"account_identifier",
                           providerAccountIdentifier,
                             @"provider_account_identifier",
                           nil]];
  return YES;
}

- (void)runCredentialRefresh:(NSDictionary *)operation
{
  NSAutoreleasePool *pool;
  NSUInteger generation;
  NSString *previousAccountIdentifier;
  NSString *providerAccountIdentifier;
  NSString *accountIdentifier;
  NSError *error;
  BOOL stateChanged;

  pool = [[NSAutoreleasePool alloc] init];
  generation = [[operation objectForKey:@"generation"]
    XP_unsignedIntegerValue];
  previousAccountIdentifier = [operation objectForKey:@"account_identifier"];
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
    if ((generation == operationGeneration_) && !cancellationRequested_) {
      if ([accountIdentifier length] > 0U) {
        state_ = StrappyAuthenticationStateSignedIn;
        StrappyAuthenticationReplaceString(&accountIdentifier_,
                                            accountIdentifier);
        StrappyAuthenticationReplaceString(&errorMessage_, nil);
      } else {
        state_ = StrappyAuthenticationStateError;
        StrappyAuthenticationReplaceString(
          &errorMessage_,
          ([[error localizedDescription] length] > 0U) ?
            [error localizedDescription] :
            NSLocalizedString(@"ChatGPT credential refresh failed.", nil));
      }
      stateChanged = YES;
    }
  }
  if (stateChanged) {
    [self notifyDidChange];
  }
  [pool drain];
}

- (BOOL)signOutChatGPT
{
  BOOL deleted;
  NSString *providerAccountIdentifier;

  @synchronized(self) {
    cancellationRequested_ = YES;
    operationGeneration_++;
    StrappyAuthenticationReplaceString(&verificationURL_, nil);
    StrappyAuthenticationReplaceString(&userCode_, nil);
  }
  providerAccountIdentifier = [self designatedProviderAccountIdentifier];
  @synchronized([[StrappyKeychain sharedKeychain]
    credentialLockForProviderIdentifier:@"openai_chatgpt"
    providerAccountIdentifier:providerAccountIdentifier]) {
    deleted = [[StrappyKeychain sharedKeychain]
      deleteChatGPTCredentialsForProviderAccountIdentifier:
        providerAccountIdentifier];
  }
  @synchronized(self) {
    StrappyAuthenticationReplaceString(&accountIdentifier_, nil);
    if (deleted) {
      state_ = StrappyAuthenticationStateSignedOut;
      StrappyAuthenticationReplaceString(&errorMessage_, nil);
    } else {
      state_ = StrappyAuthenticationStateError;
      StrappyAuthenticationReplaceString(
        &errorMessage_,
        NSLocalizedString(
          @"The Keychain refused to remove the ChatGPT credential.", nil));
    }
  }
  [self notifyDidChange];
  return deleted;
}

- (void)dealloc
{
  [verificationURL_ release];
  [userCode_ release];
  [accountIdentifier_ release];
  [providerAccountIdentifier_ release];
  [errorMessage_ release];
  [super dealloc];
}

@end
