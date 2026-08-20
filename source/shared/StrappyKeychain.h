#import <Foundation/Foundation.h>

extern NSString * const StrappyKeychainDidChangeNotification;

@interface StrappyKeychain : NSObject {
 @private
  NSString *cachedAPIToken_;
  NSString *cachedAPIEndpoint_;
  NSMutableDictionary *credentialLocks_;
  NSString *designatedOpenRouterAccountIdentifier_;
  BOOL      loaded_;
}

+ (StrappyKeychain *)sharedKeychain;
+ (NSString *)defaultAPIEndpoint;

- (NSString *)apiEndpoint;
- (NSString *)apiToken;
- (BOOL)hasAPICredentials;
- (BOOL)saveAPIEndpoint:(NSString *)apiEndpoint token:(NSString *)apiToken;

/* Account-keyed credential primitives. Provider IDs select a code-owned
 * Keychain service; opaque provider-account IDs are always the Keychain
 * account name. */
- (NSObject *)credentialLockForProviderIdentifier:(NSString *)providerIdentifier
                         providerAccountIdentifier:
                           (NSString *)providerAccountIdentifier;
- (BOOL)hasBearerTokenForProviderIdentifier:(NSString *)providerIdentifier
                   providerAccountIdentifier:
                     (NSString *)providerAccountIdentifier;
- (BOOL)loadBearerToken:(NSString **)bearerToken
  forProviderIdentifier:(NSString *)providerIdentifier
providerAccountIdentifier:(NSString *)providerAccountIdentifier;
- (BOOL)saveBearerToken:(NSString *)bearerToken
  forProviderIdentifier:(NSString *)providerIdentifier
providerAccountIdentifier:(NSString *)providerAccountIdentifier;
- (BOOL)deleteBearerTokenForProviderIdentifier:(NSString *)providerIdentifier
                      providerAccountIdentifier:
                        (NSString *)providerAccountIdentifier;
- (NSArray *)credentialProviderAccountIdentifiersForProviderIdentifier:
  (NSString *)providerIdentifier;
- (BOOL)loadDisplayName:(NSString **)displayName
  forProviderIdentifier:(NSString *)providerIdentifier
providerAccountIdentifier:(NSString *)providerAccountIdentifier;
- (BOOL)saveDisplayName:(NSString *)displayName
  forProviderIdentifier:(NSString *)providerIdentifier
providerAccountIdentifier:(NSString *)providerAccountIdentifier;

- (BOOL)hasChatGPTCredentials;
- (BOOL)loadChatGPTAccessToken:(NSString **)accessToken
                  refreshToken:(NSString **)refreshToken
             accountIdentifier:(NSString **)accountIdentifier
          expiresAtMilliseconds:(long long *)expiresAtMilliseconds;
- (BOOL)saveChatGPTAccessToken:(NSString *)accessToken
                  refreshToken:(NSString *)refreshToken
             accountIdentifier:(NSString *)accountIdentifier
          expiresAtMilliseconds:(long long)expiresAtMilliseconds;
- (BOOL)deleteChatGPTCredentials;
- (BOOL)hasChatGPTCredentialsForProviderAccountIdentifier:
  (NSString *)providerAccountIdentifier;
- (BOOL)loadChatGPTAccessToken:(NSString **)accessToken
                  refreshToken:(NSString **)refreshToken
             accountIdentifier:(NSString **)accountIdentifier
          expiresAtMilliseconds:(long long *)expiresAtMilliseconds
     providerAccountIdentifier:(NSString *)providerAccountIdentifier;
- (BOOL)saveChatGPTAccessToken:(NSString *)accessToken
                  refreshToken:(NSString *)refreshToken
             accountIdentifier:(NSString *)accountIdentifier
          expiresAtMilliseconds:(long long)expiresAtMilliseconds
     providerAccountIdentifier:(NSString *)providerAccountIdentifier;
- (BOOL)deleteChatGPTCredentialsForProviderAccountIdentifier:
  (NSString *)providerAccountIdentifier;

/* One-time compatibility conversion. The legacy item is removed only after
 * the account-keyed replacement has been written successfully. */
- (BOOL)migrateLegacyOpenRouterCredentialToProviderAccountIdentifier:
          (NSString *)providerAccountIdentifier
                                                        endpoint:
          (NSString **)endpoint;
- (BOOL)migrateLegacyChatGPTCredentialToProviderAccountIdentifier:
  (NSString *)providerAccountIdentifier;
- (void)reload;

@end
