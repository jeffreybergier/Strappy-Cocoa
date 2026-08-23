#import "StrappySession.h"

extern NSString * const StrappyAuthenticationDidChangeNotification;

typedef enum StrappyAuthenticationState {
  StrappyAuthenticationStateSignedOut = 0,
  StrappyAuthenticationStateRequestingCode,
  StrappyAuthenticationStateAwaitingUser,
  StrappyAuthenticationStateSignedIn,
  StrappyAuthenticationStateRefreshing,
  StrappyAuthenticationStateError,
  StrappyAuthenticationStateCancelled
} StrappyAuthenticationState;

@interface StrappyAuthentication : NSObject
    <StrappyChatGPTAuthorizationObserver> {
 @private
  StrappyAuthenticationState state_;
  NSString *verificationURL_;
  NSString *userCode_;
  NSString *accountIdentifier_;
  NSString *providerAccountIdentifier_;
  NSString *errorMessage_;
  NSUInteger operationGeneration_;
  BOOL cancellationRequested_;
}

+ (StrappyAuthentication *)sharedAuthentication;
+ (StrappyAuthentication *)authenticationForProviderAccountIdentifier:
  (NSString *)providerAccountIdentifier;
+ (void)forgetAuthenticationForProviderAccountIdentifier:
  (NSString *)providerAccountIdentifier;
+ (BOOL)isChatGPTProviderEnabled;
- (StrappyAuthenticationState)state;
- (NSString *)verificationURL;
- (NSString *)userCode;
- (NSString *)accountIdentifier;
- (NSString *)errorMessage;
- (BOOL)isOperationInFlight;
- (BOOL)hasStoredCredentials;
- (BOOL)startChatGPTDeviceLogin;
- (void)cancelChatGPTDeviceLogin;
- (BOOL)refreshChatGPTCredentialsIfNeeded;
- (BOOL)signOutChatGPT;

@end
