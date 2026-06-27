#ifndef STRAPPY_KEYCHAIN_H
#define STRAPPY_KEYCHAIN_H

#ifdef __cplusplus
extern "C" {
#endif

char *strappy_keychain_copy_api_endpoint(void);
char *strappy_keychain_copy_api_token(void);
int strappy_keychain_save_api_credentials(const char *api_endpoint,
                                          const char *api_token);

#ifdef __cplusplus
}
#endif

#ifdef __OBJC__
#import <Foundation/Foundation.h>

extern NSString * const StrappyKeychainDidChangeNotification;

@interface StrappyKeychain : NSObject {
 @private
  NSString *cachedAPIToken_;
  NSString *cachedAPIEndpoint_;
  BOOL      loaded_;
}

+ (StrappyKeychain *)sharedKeychain;

- (NSString *)apiEndpoint;
- (NSString *)apiToken;
- (BOOL)hasAPICredentials;
- (BOOL)saveAPIEndpoint:(NSString *)apiEndpoint token:(NSString *)apiToken;
- (void)reload;

@end
#endif

#endif
