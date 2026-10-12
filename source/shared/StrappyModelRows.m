#import "StrappyModelRows.h"
#import "XPFoundation.h"

NSString * const StrappyModelListReadFailedNotification = @"StrappyModelListReadFailedNotification";

@implementation StrappyModelRows
- (id)initWithModelSource:(id<StrappyModelListSource>)source
{
  if ((self = [super init])) {
    source_ = [source retain];
  }
  return self;
}
- (void)dealloc
{
  [source_ release]; [readError_ release];
  [super dealloc];
}
- (id)copyWithZone:(NSZone *)zone { (void)zone; return [self retain]; }
- (NSUInteger)count { return [source_ count]; }
- (BOOL)hasConfiguredAccounts { return [source_ hasConfiguredAccounts]; }
- (NSUInteger)totalCount { return [source_ totalCount]; }
- (NSUInteger)allowedCount { return [source_ allowedCount]; }
- (NSError *)readError { return readError_; }
- (void)recordReadError:(NSError *)error
{
  if (readError_ != nil || error == nil) return;
  [super recordReadError:error];
  readError_ = [error retain];
  [[NSNotificationQueue defaultQueue] enqueueNotification:
    [NSNotification notificationWithName:StrappyModelListReadFailedNotification object:self
      userInfo:[NSDictionary dictionaryWithObject:error forKey:@"error"]]
    postingStyle:NSPostASAP];
}
- (BOOL)filterWithSearch:(NSString *)search sortDescriptors:(NSArray *)descriptors error:(NSError **)error
{
  BOOL ok;
  [pageReadLock_ lock];
  ok = [source_ filterWithSearch:search sortDescriptors:descriptors error:error];
  [pageReadLock_ unlock];
  if (!ok) return NO;
  [self invalidatePages];
  [readError_ release]; readError_ = nil;
  return YES;
}
- (NSArray *)providerSectionsWithError:(NSError **)error
{
  NSArray *sections;
  [pageReadLock_ lock];
  sections = [[source_ providerSectionsWithError:error] retain];
  [pageReadLock_ unlock];
  return [sections autorelease];
}
- (NSUInteger)indexForModelIdentifier:(NSString *)identifier
{
  NSError *error = nil;
  NSUInteger index;
  if (![identifier isKindOfClass:[NSString class]] || [identifier length] == 0U) return NSNotFound;
  [pageReadLock_ lock];
  index = [source_ indexForModelIdentifier:identifier error:&error];
  [pageReadLock_ unlock];
  [self recordReadError:error];
  return index;
}
- (NSArray *)pageAtOffset:(NSUInteger)offset error:(NSError **)error
{
  return [source_ pageAtOffset:offset error:error];
}
+ (NSString *)searchTextForValues:(NSArray *)values
{
  NSMutableArray *parts = [NSMutableArray array];
  NSUInteger index;
  for (index = 0U; index < [values count]; index++) {
    id value = [values objectAtIndex:index];
    NSString *text = [value isKindOfClass:[NSNumber class]] ? [value stringValue] : value;
    if ([text isKindOfClass:[NSString class]] && [text length] > 0U) [parts addObject:text];
  }
  return [[parts componentsJoinedByString:@" "] lowercaseString];
}
@end
