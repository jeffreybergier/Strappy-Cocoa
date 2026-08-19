#import "StrappyAuthentication.h"

#import "StrappyKeychain.h"
#import "StrappySession.h"

#include "strappy_core.h"
#include "strappy_openai_oauth.h"
#include "strappy_provider.h"

#include <string.h>
#include <sys/time.h>

NSString * const StrappyAuthenticationDidChangeNotification =
  @"StrappyAuthenticationDidChangeNotification";

static const long long kStrappyAuthenticationRefreshLeewayMilliseconds =
  5LL * 60LL * 1000LL;

typedef struct StrappyAuthenticationCancellationContext {
  __unsafe_unretained StrappyAuthentication *authentication;
  NSUInteger generation;
} StrappyAuthenticationCancellationContext;

@interface StrappyAuthentication ()
- (id)initWithProviderAccountIdentifier:(NSString *)providerAccountIdentifier;
- (NSString *)designatedProviderAccountIdentifier;
- (BOOL)shouldCancelOperationWithGeneration:(NSUInteger)generation;
- (void)runDeviceLogin:(NSNumber *)generationNumber;
- (void)runCredentialRefresh:(NSDictionary *)operation;
- (void)postDidChangeNotification;
- (void)notifyDidChange;
@end

static int StrappyAuthenticationShouldCancel(void *userData)
{
  StrappyAuthenticationCancellationContext *context;

  context = (StrappyAuthenticationCancellationContext *)userData;
  if ((context == NULL) || (context->authentication == nil)) {
    return 1;
  }
  return [context->authentication
    shouldCancelOperationWithGeneration:context->generation] ? 1 : 0;
}

static long long StrappyAuthenticationNowMilliseconds(void)
{
  struct timeval now;

  if ((gettimeofday(&now, NULL) != 0) || (now.tv_sec < 0)) {
    return 0LL;
  }
  return ((long long)now.tv_sec * 1000LL) +
    ((long long)now.tv_usec / 1000LL);
}

static NSString *StrappyAuthenticationErrorMessage(char *error,
                                                    NSString *fallback)
{
  NSString *message;

  message = nil;
  if (error != NULL) {
    message = [[NSString alloc] initWithBytes:error
                                       length:strlen(error)
                                     encoding:NSUTF8StringEncoding];
  }
  return ([message length] > 0U) ? message : fallback;
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
  static NSMutableDictionary *contexts = nil;
  StrappyAuthentication *context;

  if ([providerAccountIdentifier length] == 0U) {
    return nil;
  }
  @synchronized(self) {
    if (contexts == nil) {
      contexts = [[NSMutableDictionary alloc] init];
    }
    context = [contexts objectForKey:providerAccountIdentifier];
    if (context == nil) {
      context = [[StrappyAuthentication alloc]
        initWithProviderAccountIdentifier:providerAccountIdentifier];
      [contexts setObject:context forKey:providerAccountIdentifier];
    }
  }
  return context;
}

+ (void)bootstrapProcessWithCACertPath:(NSString *)caCertPath
{
  char *error;
  NSString *message;

  if (![caCertPath isKindOfClass:[NSString class]] ||
      ([caCertPath length] == 0U)) {
    [NSException raise:NSInvalidArgumentException
                format:@"ChatGPT OAuth requires a CA certificate path."];
  }

  error = NULL;
  if (!strappy_openai_oauth_set_cainfo([caCertPath fileSystemRepresentation],
                                       &error)) {
    message = StrappyAuthenticationErrorMessage(
      error,
      @"Could not configure ChatGPT OAuth networking.");
    strappy_free_string(error);
    [NSException raise:NSInvalidArgumentException format:@"%@", message];
  }
  strappy_free_string(error);
}

+ (BOOL)isChatGPTProviderEnabled
{
  return strappy_provider_chatgpt_is_enabled() ? YES : NO;
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
    StrappyAuthenticationCancellationContext cancellationContext;
    strappy_openai_oauth_configuration configuration;
    strappy_openai_oauth_device device;
    strappy_openai_oauth_credentials credentials;
    char *error;
    BOOL started;
    BOOL completed;
    BOOL credentialSaved;
    NSString *savedAccountIdentifier;
    BOOL stateChanged;
    NSString *providerAccountIdentifier;

    generation = [generationNumber unsignedIntegerValue];
    providerAccountIdentifier = [self designatedProviderAccountIdentifier];
    cancellationContext.authentication = self;
    cancellationContext.generation = generation;
    strappy_openai_oauth_default_configuration(&configuration);
    strappy_openai_oauth_device_init(&device);
    strappy_openai_oauth_credentials_init(&credentials);
    error = NULL;
    started = strappy_openai_oauth_start_device_authorization(
      &configuration,
      &device,
      StrappyAuthenticationShouldCancel,
      &cancellationContext,
      &error) ? YES : NO;
    stateChanged = NO;
    if (started) {
      NSString *verificationURL;
      NSString *userCode;

      verificationURL = [NSString
        stringWithUTF8String:configuration.verification_url];
      userCode = [NSString stringWithUTF8String:device.user_code];
      @synchronized(self) {
        if ((generation == operationGeneration_) &&
            !cancellationRequested_) {
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

    completed = NO;
    if (started &&
        ![self shouldCancelOperationWithGeneration:generation]) {
      completed = strappy_openai_oauth_complete_device_authorization(
        &configuration,
        &device,
        &credentials,
        StrappyAuthenticationShouldCancel,
        &cancellationContext,
        &error) ? YES : NO;
    }

    credentialSaved = NO;
    savedAccountIdentifier = nil;
    if (completed) {
      NSString *accessToken;
      NSString *refreshToken;
      NSString *accountIdentifier;
      StrappyKeychain *keychain;

      accessToken = [NSString stringWithUTF8String:credentials.access_token];
      refreshToken = [NSString stringWithUTF8String:
        credentials.refresh_token];
      accountIdentifier = [NSString stringWithUTF8String:
        credentials.account_id];
      keychain = [StrappyKeychain sharedKeychain];
      if ((accessToken != nil) && (refreshToken != nil) &&
          (accountIdentifier != nil)) {
        @synchronized([keychain
          credentialLockForProviderIdentifier:@"openai_chatgpt"
          providerAccountIdentifier:providerAccountIdentifier]) {
          if (![self shouldCancelOperationWithGeneration:generation]) {
            credentialSaved = [keychain
              saveChatGPTAccessToken:accessToken
                          refreshToken:refreshToken
                     accountIdentifier:accountIdentifier
                  expiresAtMilliseconds:
                    credentials.expires_at_milliseconds
             providerAccountIdentifier:providerAccountIdentifier];
          }
        }
        if (credentialSaved) {
          savedAccountIdentifier = accountIdentifier;
        }
      }
    }

    stateChanged = NO;
    @synchronized(self) {
      if ((generation == operationGeneration_) &&
        !cancellationRequested_) {
        if (completed) {
          if (credentialSaved) {
            state_ = StrappyAuthenticationStateSignedIn;
            accountIdentifier_ = [savedAccountIdentifier copy];
            errorMessage_ = nil;
          } else {
            state_ = StrappyAuthenticationStateError;
            accountIdentifier_ = nil;
            errorMessage_ = NSLocalizedString(
              @"The Keychain refused the ChatGPT credential write.", nil);
          }
        } else {
          state_ = StrappyAuthenticationStateError;
          accountIdentifier_ = nil;
          errorMessage_ = StrappyAuthenticationErrorMessage(
            error,
            NSLocalizedString(@"ChatGPT sign-in failed.", nil));
        }
        verificationURL_ = nil;
        userCode_ = nil;
        stateChanged = YES;
      }
    }
    if (stateChanged) {
      [self notifyDidChange];
    }
    strappy_free_string(error);
    strappy_openai_oauth_credentials_destroy(&credentials);
    strappy_openai_oauth_device_destroy(&device);
  }
}

- (BOOL)refreshChatGPTCredentialsIfNeeded
{
  NSString *accountIdentifier;
  StrappyKeychain *keychain;
  long long expiresAtMilliseconds;
  long long nowMilliseconds;
  NSUInteger generation;
  BOOL stateChanged;
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

  nowMilliseconds = StrappyAuthenticationNowMilliseconds();
  if ((nowMilliseconds > 0LL) &&
      (expiresAtMilliseconds - nowMilliseconds >
       kStrappyAuthenticationRefreshLeewayMilliseconds)) {
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
    StrappyAuthenticationCancellationContext cancellationContext;
    strappy_openai_oauth_configuration configuration;
    strappy_openai_oauth_credentials credentials;
    StrappyKeychain *keychain;
    NSString *accessToken;
    NSString *nextRefreshToken;
    NSString *accountIdentifier;
    char *error;
    BOOL refreshed;
    BOOL saved;
    BOOL accountChanged;
    BOOL stateChanged;

    generation = [[operation objectForKey:@"generation"]
      unsignedIntegerValue];
    previousAccountIdentifier = [operation
      objectForKey:@"account_identifier"];
    providerAccountIdentifier = [operation
      objectForKey:@"provider_account_identifier"];
    cancellationContext.authentication = self;
    cancellationContext.generation = generation;
    strappy_openai_oauth_default_configuration(&configuration);
    strappy_openai_oauth_credentials_init(&credentials);
    keychain = [StrappyKeychain sharedKeychain];
    accessToken = nil;
    nextRefreshToken = nil;
    accountIdentifier = nil;
    error = NULL;
    refreshed = NO;
    saved = NO;
    accountChanged = NO;
    @synchronized([keychain
      credentialLockForProviderIdentifier:@"openai_chatgpt"
      providerAccountIdentifier:providerAccountIdentifier]) {
      NSString *currentRefreshToken;
      NSString *currentAccountIdentifier;

      currentRefreshToken = nil;
      currentAccountIdentifier = nil;
      if (![keychain loadChatGPTAccessToken:NULL
                                  refreshToken:&currentRefreshToken
                             accountIdentifier:&currentAccountIdentifier
                          expiresAtMilliseconds:NULL
                     providerAccountIdentifier:providerAccountIdentifier]) {
        strappy_set_error(&error,
                          "Stored ChatGPT credentials are unavailable.");
      } else if (![currentAccountIdentifier
                   isEqualToString:previousAccountIdentifier]) {
        accountChanged = YES;
      } else {
        refreshed = strappy_openai_oauth_refresh_credentials(
          &configuration,
          [currentRefreshToken UTF8String],
          &credentials,
          StrappyAuthenticationShouldCancel,
          &cancellationContext,
          &error) ? YES : NO;
        if (refreshed) {
          accessToken = [NSString stringWithUTF8String:
            credentials.access_token];
          nextRefreshToken = [NSString stringWithUTF8String:
            credentials.refresh_token];
          accountIdentifier = [NSString stringWithUTF8String:
            credentials.account_id];
          accountChanged = (accountIdentifier == nil) ||
            ![accountIdentifier isEqualToString:previousAccountIdentifier];
          if (!accountChanged && (accessToken != nil) &&
              (nextRefreshToken != nil) &&
              ![self shouldCancelOperationWithGeneration:generation]) {
            saved = [keychain
              saveChatGPTAccessToken:accessToken
                          refreshToken:nextRefreshToken
                     accountIdentifier:accountIdentifier
                  expiresAtMilliseconds:
                    credentials.expires_at_milliseconds
             providerAccountIdentifier:providerAccountIdentifier];
          }
        }
      }
    }

    stateChanged = NO;
    @synchronized(self) {
      if ((generation == operationGeneration_) &&
          !cancellationRequested_) {
        if (refreshed && saved) {
          state_ = StrappyAuthenticationStateSignedIn;
          accountIdentifier_ = [accountIdentifier copy];
          errorMessage_ = nil;
        } else {
          state_ = StrappyAuthenticationStateError;
          errorMessage_ = accountChanged ?
            NSLocalizedString(
              @"The refreshed ChatGPT credential changed accounts.", nil) :
            (refreshed ?
              NSLocalizedString(
                @"The Keychain refused the refreshed ChatGPT credential.",
                nil) :
              StrappyAuthenticationErrorMessage(
                error,
                NSLocalizedString(
                  @"ChatGPT credential refresh failed.", nil)));
        }
        verificationURL_ = nil;
        userCode_ = nil;
        stateChanged = YES;
      }
    }
    if (stateChanged) {
      [self notifyDidChange];
    }
    strappy_free_string(error);
    strappy_openai_oauth_credentials_destroy(&credentials);
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
  @synchronized([[StrappyKeychain sharedKeychain]
    credentialLockForProviderIdentifier:@"openai_chatgpt"
    providerAccountIdentifier:providerAccountIdentifier]) {
    deleted = [[StrappyKeychain sharedKeychain]
      deleteChatGPTCredentialsForProviderAccountIdentifier:
        providerAccountIdentifier];
  }
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
