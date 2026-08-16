#import <Foundation/Foundation.h>

extern NSString * const StrappyKeychainDidChangeNotification;

@interface StrappyKeychain : NSObject {
 @private
  NSString *cachedAPIToken_;
  NSString *cachedAPIEndpoint_;
  BOOL      loaded_;
}

+ (StrappyKeychain *)sharedKeychain;
+ (NSString *)defaultAPIEndpoint;

- (NSString *)apiEndpoint;
- (NSString *)apiToken;
- (BOOL)hasAPICredentials;
- (BOOL)saveAPIEndpoint:(NSString *)apiEndpoint token:(NSString *)apiToken;

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
- (void)reload;

@end
