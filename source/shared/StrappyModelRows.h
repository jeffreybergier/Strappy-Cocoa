#import <Foundation/Foundation.h>

extern NSString * const StrappyModelListReadFailedNotification;

/* StrappySession supplies the SQLite bridge; views and the bounded array do
 * not depend on C storage headers. All methods run on the owning UI thread. */
@protocol StrappyModelListSource <NSObject>
- (NSUInteger)count;
- (BOOL)hasConfiguredAccounts;
- (NSUInteger)totalCount;
- (NSUInteger)allowedCount;
- (BOOL)filterWithSearch:(NSString *)search sortDescriptors:(NSArray *)descriptors error:(NSError **)error;
- (NSArray *)pageAtOffset:(NSUInteger)offset error:(NSError **)error;
- (NSArray *)providerSectionsWithError:(NSError **)error;
- (NSUInteger)indexForModelIdentifier:(NSString *)identifier error:(NSError **)error;
@end

@interface StrappyModelRows : NSArray {
 @private
  id<StrappyModelListSource> source_;
  NSMutableDictionary *pages_;
  NSMutableArray *pageOrder_;
  NSError *readError_;
}
- (id)initWithModelSource:(id<StrappyModelListSource>)source;
- (BOOL)hasConfiguredAccounts;
- (NSUInteger)totalCount;
- (NSUInteger)allowedCount;
- (BOOL)filterWithSearch:(NSString *)search sortDescriptors:(NSArray *)descriptors error:(NSError **)error;
- (NSArray *)providerSectionsWithError:(NSError **)error;
- (NSUInteger)indexForModelIdentifier:(NSString *)identifier;
- (NSError *)readError;
/* Same concatenation/lowercasing as the original model preference searches. */
+ (NSString *)searchTextForValues:(NSArray *)values;
@end
