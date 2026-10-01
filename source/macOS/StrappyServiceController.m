#import "StrappyServiceController.h"
#import "XPAppKit.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static NSString * const StrappyServiceLabel = @"com.altivecintelligence.strappyd";

static int StrappyLaunchctl(NSArray *arguments, NSString **output)
{
  NSTask *task = [[[NSTask alloc] init] autorelease];
  NSPipe *pipe = [NSPipe pipe];
  NSData *data;
  [task setLaunchPath:@"/bin/launchctl"];
  [task setArguments:arguments];
  /* Tiger may start a per-user launchd while loading a job. Do not let that
     long-lived child inherit a capture pipe and prevent EOF in the installer. */
  [task setStandardOutput:output != NULL ? (id)pipe :
      (id)[NSFileHandle fileHandleWithNullDevice]];
  [task setStandardError:output != NULL ? (id)pipe :
      (id)[NSFileHandle fileHandleWithNullDevice]];
  @try {
    [task launch];
    data = output != NULL ? [[pipe fileHandleForReading] readDataToEndOfFile] : nil;
    [task waitUntilExit];
  } @catch (NSException *exception) {
    (void)exception;
    return -1;
  }
  if (output != NULL) {
    *output = [[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]
        autorelease];
  }
  return [task terminationStatus];
}

static BOOL StrappyServiceLoaded(void)
{
  NSString *output = nil;
  NSEnumerator *lines;
  NSString *line;
  if (StrappyLaunchctl([NSArray arrayWithObject:@"list"], &output) != 0)
    return NO;
  lines = [[output componentsSeparatedByString:@"\n"] objectEnumerator];
  while ((line = [lines nextObject]) != nil) {
    NSString *label = [[[line componentsSeparatedByString:@"\t"] lastObject]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if ([label isEqualToString:StrappyServiceLabel]) return YES;
  }
  return NO;
}

static BOOL StrappyServiceDirectory(NSString *path)
{
  NSFileManager *manager = [NSFileManager defaultManager];
  BOOL directory = NO;
  if ([manager fileExistsAtPath:path isDirectory:&directory]) return directory;
  if (!StrappyServiceDirectory([path stringByDeletingLastPathComponent])) return NO;
  return [manager XP_createDirectoryAtPath:path withIntermediateDirectories:NO
      attributes:[NSDictionary dictionaryWithObject:[NSNumber numberWithInt:0700]
          forKey:NSFilePosixPermissions] error:NULL];
}

static BOOL StrappyServiceChange(BOOL install, NSString **error)
{
  NSString *library = [NSHomeDirectory() stringByAppendingPathComponent:@"Library"];
  NSString *support = [library stringByAppendingPathComponent:@"Application Support/Strappy/Service"];
  NSString *installed = [support stringByAppendingPathComponent:@"strappyd"];
  NSString *agents = [library stringByAppendingPathComponent:@"LaunchAgents"];
  NSString *plistPath = [agents stringByAppendingPathComponent:
      [StrappyServiceLabel stringByAppendingPathExtension:@"plist"]];
  NSString *logDirectory = [library stringByAppendingPathComponent:@"Logs/Strappy"];
  NSString *logPath = [logDirectory stringByAppendingPathComponent:@"heartbeat.log"];
  NSString *embedded = [[[NSBundle mainBundle] bundlePath]
      stringByAppendingPathComponent:@"Contents/Library/LaunchServices/strappyd"];
  NSFileManager *manager = [NSFileManager defaultManager];
  NSMutableDictionary *plist = [NSMutableDictionary dictionaryWithObjectsAndKeys:
      StrappyServiceLabel, @"Label", [NSArray arrayWithObjects:installed, logPath, nil],
      @"ProgramArguments", @"/", @"WorkingDirectory",
      [NSNumber numberWithInt:10], @"ThrottleInterval",
      [NSNumber numberWithInt:10], @"ExitTimeOut", nil];
  int lockFD;
  BOOL success = NO;
  BOOL unchanged;
  NSData *binary = nil;
  if (error != NULL) *error = @"Could not prepare the background service directories.";
  if (!StrappyServiceDirectory(support)) return NO;
  lockFD = open([[support stringByAppendingPathComponent:@"install.lock"]
      fileSystemRepresentation], O_CREAT | O_RDWR, 0600);
  if (lockFD < 0) return NO;
  if (flock(lockFD, LOCK_EX | LOCK_NB) != 0) {
    if (error != NULL) *error = @"Another background service installation is in progress.";
    (void)close(lockFD);
    return NO;
  }
  XPConfigurePersistentLaunchAgent(plist);
  if (install) {
    if (!StrappyServiceDirectory(agents) || !StrappyServiceDirectory(logDirectory))
      goto done;
    binary = [NSData dataWithContentsOfFile:embedded];
    if ([binary length] == 0U) {
      if (error != NULL) *error = @"The embedded background service is missing.";
      goto done;
    }
  }
  unchanged = install && [manager contentsEqualAtPath:embedded andPath:installed] &&
      [[NSDictionary dictionaryWithContentsOfFile:plistPath] isEqual:plist];
  if (unchanged && StrappyServiceLoaded()) {
    success = YES;
    goto done;
  }
  if (error != NULL) *error = @"Could not unload the existing background service.";
  if ([manager fileExistsAtPath:plistPath]) {
    int status = StrappyLaunchctl(XPLaunchAgentArguments(@"unload", plistPath), NULL);
    if (status != 0 && StrappyServiceLoaded()) goto done;
  }
  if (!install) {
    if (error != NULL) *error = @"Could not remove the background service files.";
    if (unlink([plistPath fileSystemRepresentation]) != 0 && errno != ENOENT) goto done;
    if (unlink([installed fileSystemRepresentation]) != 0 && errno != ENOENT) goto done;
    success = YES;
    goto done;
  }
  if (error != NULL) *error = @"Could not install the background service files.";
  if (![binary writeToFile:installed atomically:YES] ||
      chmod([installed fileSystemRepresentation], 0700) != 0 ||
      ![plist writeToFile:plistPath atomically:YES] ||
      chmod([plistPath fileSystemRepresentation], 0600) != 0) goto done;
  if (error != NULL) *error = @"Could not load the background service with launchd.";
  success = StrappyLaunchctl(XPLaunchAgentArguments(@"load", plistPath), NULL) == 0;
done:
  (void)flock(lockFD, LOCK_UN);
  (void)close(lockFD);
  if (success && error != NULL) *error = nil;
  return success;
}

@implementation StrappyServiceController
+ (BOOL)installWithError:(NSString **)error
{
  return StrappyServiceChange(YES, error);
}
+ (BOOL)uninstallWithError:(NSString **)error
{
  return StrappyServiceChange(NO, error);
}
@end
