/* Actual AppKit data-source callback, synthetic rows only. No app delegate,
 * database, credentials or daemon startup. */
#import <AppKit/AppKit.h>
#import <AIPagedRows.h>
#import "../../macOS/PreferencesWindowController.h"
#include <assert.h>
@interface PreferencesWindowController (ModelCellFixture)
- (id)tableView:(NSTableView *)table objectValueForTableColumn:(NSTableColumn *)column row:(NSInteger)row;
@end
@interface ModelCellRows : AIPagedRows { @public NSUInteger reads; }
@end
@implementation ModelCellRows
- (NSUInteger)count; { return 100000; }
- (id)copyWithZone:(NSZone *)zone; { (void)zone; return [self retain]; }
- (NSArray *)pageAtOffset:(NSUInteger)offset error:(NSError **)error;
{
  (void)error; ++reads;
  NSMutableArray *page=[NSMutableArray array]; NSUInteger i;
  for(i=offset;i<MIN(offset+32,[self count]);++i)
    [page addObject:[NSDictionary dictionaryWithObjectsAndKeys:
      [NSString stringWithFormat:@"Model %lu",(unsigned long)i],@"name",nil]];
  return page;
}
@end
int main(void) {
  NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  PreferencesWindowController *owner=[[PreferencesWindowController alloc] initWithWindow:nil];
  NSTableView *table=[[[NSTableView alloc] initWithFrame:NSMakeRect(0,0,400,400)] autorelease];
  NSTableColumn *column=[[[NSTableColumn alloc] initWithIdentifier:@"model_name"] autorelease];
  ModelCellRows *rows=[[[ModelCellRows alloc] init] autorelease];
  [owner setValue:rows forKey:@"modelRows_"]; [owner setValue:table forKey:@"modelTableView_"];
  assert([rows count]==100000 && rows->reads==0);
  NSUInteger jumps[]={0,99999,128,512,1024,4096,0},i;
  for(i=0;i<7;++i) {
    NSString *text=[owner tableView:table objectValueForTableColumn:column row:(NSInteger)jumps[i]];
    NSString *expected=[NSString stringWithFormat:@"Model %lu",(unsigned long)jumps[i]];
    assert([text isEqual:expected] && rows->reads==i+1);
    if(i==5) assert([rows cachedObjectAtIndex:0]==nil);
  }
  NSLog(@"PASS: AppKit model callbacks return complete text synchronously with bounded pages for 100000 rows");
  [owner release]; [pool drain]; return 0;
}
