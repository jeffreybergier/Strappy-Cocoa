#import <Foundation/Foundation.h>

@interface StrappySession : NSObject

+ (NSString *)sessionsDatabasePath;
+ (NSString *)submitPromptSynchronously:(NSString *)prompt error:(NSError **)error;

@end
