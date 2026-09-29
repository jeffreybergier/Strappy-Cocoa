#import "StrappyModelRows.h"
#import "XPFoundation.h"

NSString * const StrappyModelListReadFailedNotification = @"StrappyModelListReadFailedNotification";

@implementation StrappyModelRows
- (id)initWithModelSource:(id<StrappyModelListSource>)source
{
  if ((self = [super init])) {
    source_ = [source retain];
    pages_ = [[NSMutableDictionary alloc] init];
    pageOrder_ = [[NSMutableArray alloc] init];
  }
  return self;
}
- (void)dealloc
{
  [source_ release]; [pages_ release]; [pageOrder_ release]; [readError_ release];
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
  readError_ = [error retain];
  [[NSNotificationQueue defaultQueue] enqueueNotification:
    [NSNotification notificationWithName:StrappyModelListReadFailedNotification object:self
      userInfo:[NSDictionary dictionaryWithObject:error forKey:@"error"]]
    postingStyle:NSPostASAP];
}
- (BOOL)filterWithSearch:(NSString *)search sortDescriptors:(NSArray *)descriptors error:(NSError **)error
{
  if (![source_ filterWithSearch:search sortDescriptors:descriptors error:error]) return NO;
  [pages_ removeAllObjects]; [pageOrder_ removeAllObjects];
  [readError_ release]; readError_ = nil;
  return YES;
}
- (NSArray *)providerSectionsWithError:(NSError **)error
{
  return [source_ providerSectionsWithError:error];
}
- (NSUInteger)indexForModelIdentifier:(NSString *)identifier
{
  NSError *error = nil;
  NSUInteger index;
  if (![identifier isKindOfClass:[NSString class]] || [identifier length] == 0U) return NSNotFound;
  index = [source_ indexForModelIdentifier:identifier error:&error];
  [self recordReadError:error];
  return index;
}
- (id)objectAtIndex:(NSUInteger)index
{
  NSUInteger offset = (index / 32U) * 32U;
  NSNumber *key = [NSNumber XP_numberWithUnsignedInteger:offset];
  NSArray *page = [pages_ objectForKey:key];
  if (index >= [self count]) [NSException raise:NSRangeException format:@"Model index outside snapshot"];
  if (page == nil && readError_ == nil) {
    NSError *error = nil;
    page = [source_ pageAtOffset:offset error:&error];
    if (page == nil) [self recordReadError:error];
    else {
      if ([pageOrder_ count] >= 4U) {
        [pages_ removeObjectForKey:[pageOrder_ objectAtIndex:0U]];
        [pageOrder_ removeObjectAtIndex:0U];
      }
      [pages_ setObject:page forKey:key];
    }
  }
  if (page != nil) {
    [pageOrder_ removeObject:key]; [pageOrder_ addObject:key];
    if (index - offset < [page count]) return [page objectAtIndex:index - offset];
  }
  /* No model ID on failure, so a checkbox cannot modify a different model. */
  return [NSDictionary dictionaryWithObject:NSLocalizedString(@"Model list could not be loaded.",nil) forKey:@"name"];
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
