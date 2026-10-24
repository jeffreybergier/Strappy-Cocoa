/* Host-only fixture: real controller/filesystem, recorded launchctl. Never
 * touches the user's LaunchAgents, launches a daemon, or accesses Keychain. */
#import <Foundation/Foundation.h>
#import "XPAppKit.h"
#include <assert.h>

static NSString *testHome;
static BOOL jobLoaded;
static unsigned int loads;
static unsigned int unloads;
static BOOL failLoad;
static BOOL failUnload;

static NSString *TestHomeDirectory(void)
{
  return testHome;
}

@interface RecordedLaunchctl : NSObject {
  NSArray *arguments_;
  id output_;
  int status_;
}
- (void)setLaunchPath:(NSString *)path;
- (void)setArguments:(NSArray *)arguments;
- (void)setStandardOutput:(id)output;
- (void)setStandardError:(id)output;
- (void)launch;
- (void)waitUntilExit;
- (int)terminationStatus;
@end

@implementation RecordedLaunchctl
- (void)setLaunchPath:(NSString *)path;
{
  assert([path isEqual:@"/bin/launchctl"]);
}
- (void)setArguments:(NSArray *)arguments;
{
  arguments_ = [arguments copy];
}
- (void)setStandardOutput:(id)output;
{
  output_ = [output retain];
}
- (void)setStandardError:(id)output;
{
  (void)output;
}
- (void)launch;
{
  NSString *action = [arguments_ objectAtIndex:0];
  NSString *text = @"";
  status_ = 0;
  if ([action isEqual:@"list"]) {
    if (jobLoaded) text = @"123\t0\tcom.altivecintelligence.strappyd\n";
  } else if ([action isEqual:@"load"]) {
    assert([[arguments_ lastObject] rangeOfString:@"/LaunchAgents/"].location
        == NSNotFound);
    ++loads;
    status_ = failLoad ? 1 : 0;
    jobLoaded = !failLoad;
  } else if ([action isEqual:@"unload"]) {
    ++unloads;
    status_ = !failUnload && jobLoaded ? 0 : 1;
    if (!failUnload) jobLoaded = NO;
  } else {
    assert(0);
  }
  if ([output_ isKindOfClass:[NSPipe class]]) {
    [[output_ fileHandleForWriting]
        writeData:[text dataUsingEncoding:NSUTF8StringEncoding]];
    [[output_ fileHandleForWriting] closeFile];
  }
}
- (void)waitUntilExit;
{
}
- (int)terminationStatus;
{
  return status_;
}
- (void)dealloc;
{
  [arguments_ release];
  [output_ release];
  [super dealloc];
}
@end

/* The compatibility helper is not under test here. Record modern arguments
 * while exercising production lifecycle, locking, migration and file paths. */
NSArray *XPLaunchAgentArguments(NSString *action, NSString *path)
{
  return [NSArray arrayWithObjects:action, @"-S", @"Aqua", path, nil];
}
void XPConfigureApplicationLaunchAgent(NSMutableDictionary *plist)
{
  [plist setObject:[NSNumber numberWithBool:YES] forKey:@"RunAtLoad"];
  [plist setObject:[NSNumber numberWithBool:YES] forKey:@"KeepAlive"];
}
@implementation NSFileManager (ServiceFixture)
- (BOOL)XP_createDirectoryAtPath:(NSString *)path
   withIntermediateDirectories:(BOOL)intermediates
                    attributes:(NSDictionary *)attributes
                         error:(NSError **)error;
{
  return [self createDirectoryAtPath:path
       withIntermediateDirectories:intermediates attributes:attributes error:error];
}
@end

#define NSTask RecordedLaunchctl
#define NSHomeDirectory TestHomeDirectory
#import "../../macOS/StrappyServiceController.m"
#undef NSHomeDirectory
#undef NSTask

int main(int argc, const char **argv)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSFileManager *manager = [NSFileManager defaultManager];
  NSString *legacy;
  NSString *support;
  NSString *plist;
  NSString *binary;
  NSString *message = nil;
  NSError *error = nil;
  unsigned int previousLoads;
  assert(argc == 2);
  testHome = [NSString stringWithUTF8String:argv[1]];
  legacy = [testHome stringByAppendingPathComponent:
      @"Library/LaunchAgents/com.altivecintelligence.strappyd.plist"];
  support = [testHome stringByAppendingPathComponent:
      @"Library/Application Support/Strappy/Service"];
  plist = [support stringByAppendingPathComponent:
      @"com.altivecintelligence.strappyd.plist"];
  binary = [support stringByAppendingPathComponent:@"strappyd"];
  assert([manager createDirectoryAtPath:[legacy stringByDeletingLastPathComponent]
      withIntermediateDirectories:YES attributes:nil error:NULL]);
  assert([[NSDictionary dictionaryWithObject:@"fixture" forKey:@"Label"]
      writeToFile:legacy atomically:YES]);
  jobLoaded = YES;
  assert([StrappyServiceController prepareForApplicationWithError:&error]);
  assert(jobLoaded && loads == 1 && unloads == 1);
  assert(![manager fileExistsAtPath:legacy]);
  assert([manager fileExistsAtPath:plist]);
  assert([manager fileExistsAtPath:binary]);
  assert([StrappyServiceController prepareForApplicationWithError:&error]);
  assert(loads == 1); /* Unchanged startup does not duplicate the job. */
  failUnload = YES;
  assert(![StrappyServiceController stopForApplicationWithError:&message]);
  assert(jobLoaded && message && [manager fileExistsAtPath:binary]);
  failUnload = NO;
  assert([StrappyServiceController stopForApplicationWithError:&message]);
  assert(!jobLoaded && [manager fileExistsAtPath:binary]);
  assert([manager fileExistsAtPath:plist]);
  previousLoads = loads;
  assert(![StrappyServiceController prepareForApplicationWithError:&error]);
  assert(error && loads == previousLoads && !jobLoaded);

  /* Simulate the next app process, then explicit maintenance removal. */
  StrappyServiceApplicationStopping = NO;
  assert([StrappyServiceController prepareForApplicationWithError:&error]);
  assert(jobLoaded && loads == previousLoads + 1);
  assert([StrappyServiceController uninstallWithError:&message]);
  assert(!jobLoaded && ![manager fileExistsAtPath:binary]);
  assert(![manager fileExistsAtPath:plist]);

  /* A quit that wins the startup race prevents any queued load. */
  assert([StrappyServiceController stopForApplicationWithError:&message]);
  previousLoads = loads;
  assert(![StrappyServiceController prepareForApplicationWithError:&error]);
  assert(loads == previousLoads);

  StrappyServiceApplicationStopping = NO;
  failLoad = YES;
  assert(![StrappyServiceController prepareForApplicationWithError:&error]);
  assert(!jobLoaded && error);
  assert([StrappyServiceController stopForApplicationWithError:&message]);
  puts("PASS: Mac service migration, start/reuse, quit, late-start rejection, "
       "relaunch, uninstall and load/unload failures (recorded launchctl)");
  [pool release];
  return 0;
}
