#import "FileScanner.h"

#import "StrappySession.h"
#import "strappy_core.h"
#import "strappy_db.h"
#import "strappy_file_scanner.h"

#include <string.h>

static BOOL StrappyFileScannerStringHasValue(NSString *string)
{
  return ([string isKindOfClass:[NSString class]] && ([string length] > 0U)) ?
    YES : NO;
}

static BOOL StrappyFileScannerIsHexCharacter(unichar character)
{
  return (((character >= '0') && (character <= '9')) ||
          ((character >= 'a') && (character <= 'f')) ||
          ((character >= 'A') && (character <= 'F'))) ? YES : NO;
}

static BOOL StrappyFileScannerIsUUIDComponent(NSString *component)
{
  NSUInteger index;

  if (![component isKindOfClass:[NSString class]] ||
      ([component length] != 36U)) {
    return NO;
  }

  for (index = 0U; index < [component length]; index++) {
    unichar character;

    character = [component characterAtIndex:index];
    if ((index == 8U) || (index == 13U) || (index == 18U) || (index == 23U)) {
      if (character != '-') {
        return NO;
      }
    } else if (!StrappyFileScannerIsHexCharacter(character)) {
      return NO;
    }
  }

  return YES;
}

static NSString *StrappyFileScannerPathFromComponents(NSArray *components,
                                                      NSUInteger lastIndex)
{
  NSArray *pathComponents;

  if (![components isKindOfClass:[NSArray class]] ||
      (lastIndex >= [components count])) {
    return nil;
  }

  pathComponents =
    [components subarrayWithRange:NSMakeRange(0U, lastIndex + 1U)];
  return [NSString pathWithComponents:pathComponents];
}

static NSUInteger StrappyFileScannerApplicationsIndex(NSArray *components)
{
  NSUInteger index;

  if (![components isKindOfClass:[NSArray class]]) {
    return NSNotFound;
  }

  for (index = 0U; index < [components count]; index++) {
    NSString *component;

    component = [components objectAtIndex:index];
    if ([component isEqualToString:@"Applications"]) {
      return index;
    }
  }

  return NSNotFound;
}

static NSUInteger StrappyFileScannerAppBundleIndex(NSArray *components)
{
  NSUInteger index;

  if (![components isKindOfClass:[NSArray class]]) {
    return NSNotFound;
  }

  for (index = 0U; index < [components count]; index++) {
    NSString *component;

    component = [components objectAtIndex:index];
    if ([[component pathExtension] caseInsensitiveCompare:@"app"] ==
        NSOrderedSame) {
      return index;
    }
  }

  return NSNotFound;
}

static NSUInteger StrappyFileScannerMobileChildIndex(NSArray *components,
                                                     NSString *childName)
{
  NSUInteger index;

  if (![components isKindOfClass:[NSArray class]] ||
      !StrappyFileScannerStringHasValue(childName)) {
    return NSNotFound;
  }

  for (index = 0U; (index + 2U) < [components count]; index++) {
    if ([[components objectAtIndex:index] isEqualToString:@"var"] &&
        [[components objectAtIndex:(index + 1U)] isEqualToString:@"mobile"] &&
        [[components objectAtIndex:(index + 2U)] isEqualToString:childName]) {
      return index + 2U;
    }
  }

  return NSNotFound;
}

static NSString *StrappyFileScannerKnownNameForBundleIdentifier(
  NSString *bundleIdentifier)
{
  if (![bundleIdentifier isKindOfClass:[NSString class]]) {
    return nil;
  }

  if ([bundleIdentifier isEqualToString:@"com.apple.mobilesafari"]) {
    return @"Safari";
  }
  if ([bundleIdentifier isEqualToString:@"com.apple.mobilemail"]) {
    return @"Mail";
  }
  if ([bundleIdentifier isEqualToString:@"com.apple.mobilecal"]) {
    return @"Calendar";
  }
  if ([bundleIdentifier isEqualToString:@"com.apple.MobileAddressBook"]) {
    return @"Contacts";
  }
  if ([bundleIdentifier isEqualToString:@"com.apple.mobilenotes"]) {
    return @"Notes";
  }
  if ([bundleIdentifier isEqualToString:@"com.apple.mobilephone"]) {
    return @"Phone";
  }
  if ([bundleIdentifier isEqualToString:@"com.apple.mobileslideshow"]) {
    return @"Photos";
  }
  if ([bundleIdentifier isEqualToString:@"com.apple.mobileipod"]) {
    return @"Music";
  }
  if ([bundleIdentifier isEqualToString:@"com.apple.Maps"]) {
    return @"Maps";
  }
  if ([bundleIdentifier isEqualToString:@"com.apple.AppStore"]) {
    return @"App Store";
  }
  if ([bundleIdentifier isEqualToString:@"com.apple.MobileStore"] ||
      [bundleIdentifier isEqualToString:@"com.apple.itunesstored"] ||
      [bundleIdentifier isEqualToString:@"com.apple.iTunesStore"]) {
    return @"iTunes Store";
  }
  if ([bundleIdentifier isEqualToString:@"com.apple.Preferences"]) {
    return @"Settings";
  }
  if ([bundleIdentifier isEqualToString:@"com.apple.springboard"]) {
    return @"SpringBoard";
  }
  if ([bundleIdentifier isEqualToString:@"com.saurik.Cydia"]) {
    return @"Cydia";
  }

  return nil;
}

static NSString *StrappyFileScannerKnownNameForLibraryComponent(
  NSString *component)
{
  if ([component isEqualToString:@"Accounts"]) {
    return @"Apple Accounts";
  }
  if ([component isEqualToString:@"AddressBook"]) {
    return @"Contacts";
  }
  if ([component isEqualToString:@"AggregateDictionary"]) {
    return @"Diagnostics";
  }
  if ([component isEqualToString:@"Calendar"]) {
    return @"Calendar";
  }
  if ([component isEqualToString:@"GameKit"]) {
    return @"Game Center";
  }
  if ([component isEqualToString:@"Keyboard"]) {
    return @"Keyboard";
  }
  if ([component isEqualToString:@"LASD"]) {
    return @"Location Services";
  }
  if ([component isEqualToString:@"Mail"]) {
    return @"Mail";
  }
  if ([component isEqualToString:@"MediaStream"]) {
    return @"Photo Stream";
  }
  if ([component isEqualToString:@"MusicLibrary"]) {
    return @"Music";
  }
  if ([component isEqualToString:@"Notes"]) {
    return @"Notes";
  }
  if ([component isEqualToString:@"Passes"]) {
    return @"Passbook";
  }
  if ([component isEqualToString:@"SMS"]) {
    return @"Messages";
  }
  if ([component isEqualToString:@"Safari"]) {
    return @"Safari";
  }
  if ([component isEqualToString:@"Social"]) {
    return @"Social";
  }
  if ([component isEqualToString:@"Spotlight"]) {
    return @"Spotlight";
  }
  if ([component isEqualToString:@"TCC"]) {
    return @"Privacy";
  }
  if ([component isEqualToString:@"Twitter"]) {
    return @"Twitter";
  }
  if ([component isEqualToString:@"Voicemail"]) {
    return @"Phone";
  }
  if ([component isEqualToString:@"WebClips"]) {
    return @"Web Clips";
  }
  if ([component isEqualToString:@"WebKit"]) {
    return @"WebKit / Safari";
  }
  if ([component isEqualToString:@"com.apple.iTunesStore"] ||
      [component isEqualToString:@"com.apple.itunesstored"]) {
    return @"iTunes Store";
  }

  return component;
}

static NSString *StrappyFileScannerKnownNameForMediaComponent(
  NSString *component)
{
  if ([component isEqualToString:@"PhotoData"]) {
    return @"Photos";
  }
  if ([component isEqualToString:@"iTunes_Control"]) {
    return @"Music";
  }
  if ([component isEqualToString:@"Downloads"]) {
    return @"iTunes Downloads";
  }
  if ([component isEqualToString:@"Books"]) {
    return @"Books";
  }
  if ([component isEqualToString:@"Recordings"]) {
    return @"Voice Memos";
  }

  return component;
}

static BOOL StrappyFileScannerLooksLikeBundleIdentifier(NSString *component)
{
  if (![component isKindOfClass:[NSString class]]) {
    return NO;
  }

  return (([component rangeOfString:@"."].location != NSNotFound) &&
          ([component rangeOfString:@"/"].location == NSNotFound)) ? YES : NO;
}

static NSDictionary *StrappyFileScannerAppInfoForBundlePath(NSString *bundlePath)
{
  NSBundle *bundle;
  NSString *bundleName;
  NSString *bundleIdentifier;
  NSString *displayName;
  NSString *appName;

  if (!StrappyFileScannerStringHasValue(bundlePath)) {
    return nil;
  }

  bundle = [NSBundle bundleWithPath:bundlePath];
  displayName = [bundle objectForInfoDictionaryKey:@"CFBundleDisplayName"];
  bundleName = [bundle objectForInfoDictionaryKey:@"CFBundleName"];
  bundleIdentifier = [bundle bundleIdentifier];
  if (!StrappyFileScannerStringHasValue(bundleIdentifier)) {
    bundleIdentifier =
      [bundle objectForInfoDictionaryKey:@"CFBundleIdentifier"];
  }

  if (StrappyFileScannerStringHasValue(displayName)) {
    appName = displayName;
  } else if (StrappyFileScannerStringHasValue(bundleName)) {
    appName = bundleName;
  } else {
    appName = [[bundlePath lastPathComponent] stringByDeletingPathExtension];
  }

  if (!StrappyFileScannerStringHasValue(appName)) {
    return nil;
  }

  return [NSDictionary dictionaryWithObjectsAndKeys:
    appName, @"app_name",
    StrappyFileScannerStringHasValue(bundleIdentifier) ? bundleIdentifier : @"",
      @"app_bundle_id",
    bundlePath, @"app_bundle_path",
    nil];
}

static NSDictionary *StrappyFileScannerAppInfoForContainerPath(
  NSString *containerPath,
  NSMutableDictionary *cache)
{
  id cached;
  NSArray *children;
  NSArray *sortedChildren;
  NSFileManager *fileManager;
  NSUInteger index;

  if (!StrappyFileScannerStringHasValue(containerPath)) {
    return nil;
  }

  cached = [cache objectForKey:containerPath];
  if (cached != nil) {
    return [cached isKindOfClass:[NSDictionary class]] ? cached : nil;
  }

  fileManager = [NSFileManager defaultManager];
  children = [fileManager contentsOfDirectoryAtPath:containerPath error:nil];
  sortedChildren = [children sortedArrayUsingSelector:
    @selector(caseInsensitiveCompare:)];
  for (index = 0U; index < [sortedChildren count]; index++) {
    NSString *child;
    NSString *candidatePath;
    BOOL isDirectory;

    child = [sortedChildren objectAtIndex:index];
    if ([[child pathExtension] caseInsensitiveCompare:@"app"] !=
        NSOrderedSame) {
      continue;
    }

    candidatePath = [containerPath stringByAppendingPathComponent:child];
    isDirectory = NO;
    if ([fileManager fileExistsAtPath:candidatePath isDirectory:&isDirectory] &&
        isDirectory) {
      NSDictionary *info;

      info = StrappyFileScannerAppInfoForBundlePath(candidatePath);
      if (info != nil) {
        [cache setObject:info forKey:containerPath];
        return info;
      }
    }
  }

  [cache setObject:[NSNull null] forKey:containerPath];
  return nil;
}

static void StrappyFileScannerAddInstalledAppsFromDirectory(
  NSMutableDictionary *namesByBundleIdentifier,
  NSString *directoryPath)
{
  NSArray *children;
  NSFileManager *fileManager;
  NSUInteger index;

  if (!StrappyFileScannerStringHasValue(directoryPath)) {
    return;
  }

  fileManager = [NSFileManager defaultManager];
  children = [fileManager contentsOfDirectoryAtPath:directoryPath error:nil];
  for (index = 0U; index < [children count]; index++) {
    NSString *child;
    NSString *childPath;
    BOOL isDirectory;

    child = [children objectAtIndex:index];
    childPath = [directoryPath stringByAppendingPathComponent:child];
    isDirectory = NO;
    if (![fileManager fileExistsAtPath:childPath isDirectory:&isDirectory] ||
        !isDirectory) {
      continue;
    }

    if ([[child pathExtension] caseInsensitiveCompare:@"app"] == NSOrderedSame) {
      NSDictionary *info;
      NSString *bundleIdentifier;
      NSString *appName;

      info = StrappyFileScannerAppInfoForBundlePath(childPath);
      bundleIdentifier = [info objectForKey:@"app_bundle_id"];
      appName = [info objectForKey:@"app_name"];
      if (StrappyFileScannerStringHasValue(bundleIdentifier) &&
          StrappyFileScannerStringHasValue(appName)) {
        [namesByBundleIdentifier setObject:appName forKey:bundleIdentifier];
      }
    } else {
      NSArray *nestedChildren;
      NSUInteger nestedIndex;

      nestedChildren = [fileManager contentsOfDirectoryAtPath:childPath
                                                        error:nil];
      for (nestedIndex = 0U;
           nestedIndex < [nestedChildren count];
           nestedIndex++) {
        NSString *nestedChild;
        NSString *nestedPath;

        nestedChild = [nestedChildren objectAtIndex:nestedIndex];
        if ([[nestedChild pathExtension] caseInsensitiveCompare:@"app"] !=
            NSOrderedSame) {
          continue;
        }

        nestedPath = [childPath stringByAppendingPathComponent:nestedChild];
        if ([fileManager fileExistsAtPath:nestedPath
                              isDirectory:&isDirectory] && isDirectory) {
          NSDictionary *info;
          NSString *bundleIdentifier;
          NSString *appName;

          info = StrappyFileScannerAppInfoForBundlePath(nestedPath);
          bundleIdentifier = [info objectForKey:@"app_bundle_id"];
          appName = [info objectForKey:@"app_name"];
          if (StrappyFileScannerStringHasValue(bundleIdentifier) &&
              StrappyFileScannerStringHasValue(appName)) {
            [namesByBundleIdentifier setObject:appName
                                        forKey:bundleIdentifier];
          }
        }
      }
    }
  }
}

static NSDictionary *StrappyFileScannerInstalledAppNamesByBundleIdentifier(
  NSString *rootPath)
{
  NSMutableDictionary *namesByBundleIdentifier;
  NSMutableArray *candidateDirectories;
  NSString *homeApplications;
  NSUInteger index;

  namesByBundleIdentifier = [NSMutableDictionary dictionary];
  candidateDirectories = [NSMutableArray array];
  homeApplications =
    [NSHomeDirectory() stringByAppendingPathComponent:@"Applications"];
  if (StrappyFileScannerStringHasValue(homeApplications)) {
    [candidateDirectories addObject:homeApplications];
  }
  if (StrappyFileScannerStringHasValue(rootPath)) {
    NSString *rootApplications;

    rootApplications = [rootPath stringByAppendingPathComponent:@"Applications"];
    if (![candidateDirectories containsObject:rootApplications]) {
      [candidateDirectories addObject:rootApplications];
    }
  }
  if (![candidateDirectories containsObject:@"/var/mobile/Applications"]) {
    [candidateDirectories addObject:@"/var/mobile/Applications"];
  }

  for (index = 0U; index < [candidateDirectories count]; index++) {
    StrappyFileScannerAddInstalledAppsFromDirectory(
      namesByBundleIdentifier,
      [candidateDirectories objectAtIndex:index]);
  }

  return namesByBundleIdentifier;
}

static NSDictionary *StrappyFileScannerMetadata(NSString *groupKey,
                                                NSString *name,
                                                NSString *bundleIdentifier,
                                                NSString *containerPath,
                                                NSString *bundlePath,
                                                NSString *source)
{
  NSMutableDictionary *metadata;

  if (!StrappyFileScannerStringHasValue(groupKey) ||
      !StrappyFileScannerStringHasValue(name)) {
    return nil;
  }

  metadata = [NSMutableDictionary dictionaryWithObjectsAndKeys:
    groupKey, @"app_group_key",
    name, @"app_name",
    source, @"app_source",
    nil];
  if (StrappyFileScannerStringHasValue(bundleIdentifier)) {
    [metadata setObject:bundleIdentifier forKey:@"app_bundle_id"];
  }
  if (StrappyFileScannerStringHasValue(containerPath)) {
    [metadata setObject:containerPath forKey:@"app_container_path"];
  }
  if (StrappyFileScannerStringHasValue(bundlePath)) {
    [metadata setObject:bundlePath forKey:@"app_bundle_path"];
  }

  return metadata;
}

static NSDictionary *StrappyFileScannerAppMetadataForPath(
  NSString *path,
  NSMutableDictionary *containerAppInfoCache,
  NSDictionary *installedAppNamesByBundleIdentifier)
{
  NSArray *components;
  NSUInteger appBundleIndex;
  NSUInteger applicationsIndex;
  NSUInteger libraryIndex;
  NSUInteger mediaIndex;

  if (!StrappyFileScannerStringHasValue(path)) {
    return nil;
  }

  components = [path pathComponents];
  appBundleIndex = StrappyFileScannerAppBundleIndex(components);
  if (appBundleIndex != NSNotFound) {
    NSString *bundlePath;
    NSString *containerPath;
    NSDictionary *info;
    NSString *bundleIdentifier;
    NSString *appName;
    NSString *groupKey;

    bundlePath = StrappyFileScannerPathFromComponents(components,
                                                       appBundleIndex);
    containerPath = nil;
    if ((appBundleIndex >= 2U) &&
        [[components objectAtIndex:(appBundleIndex - 2U)]
          isEqualToString:@"Applications"] &&
        StrappyFileScannerIsUUIDComponent(
          [components objectAtIndex:(appBundleIndex - 1U)])) {
      containerPath = StrappyFileScannerPathFromComponents(components,
                                                           appBundleIndex - 1U);
    }
    info = StrappyFileScannerAppInfoForBundlePath(bundlePath);
    bundleIdentifier = [info objectForKey:@"app_bundle_id"];
    appName = [info objectForKey:@"app_name"];
    groupKey = StrappyFileScannerStringHasValue(bundleIdentifier) ?
      [@"bundle:" stringByAppendingString:bundleIdentifier] :
      (StrappyFileScannerStringHasValue(containerPath) ?
        [@"container:" stringByAppendingString:containerPath] :
        [@"app-path:" stringByAppendingString:bundlePath]);
    return StrappyFileScannerMetadata(groupKey,
                                      appName,
                                      bundleIdentifier,
                                      containerPath,
                                      bundlePath,
                                      @"bundle_plist");
  }

  applicationsIndex = StrappyFileScannerApplicationsIndex(components);
  if ((applicationsIndex != NSNotFound) &&
      ((applicationsIndex + 1U) < [components count])) {
    NSString *nextComponent;

    nextComponent = [components objectAtIndex:(applicationsIndex + 1U)];
    if (StrappyFileScannerIsUUIDComponent(nextComponent)) {
      NSString *containerPath;
      NSDictionary *info;
      NSString *bundleIdentifier;
      NSString *appName;
      NSString *bundlePath;
      NSString *groupKey;

      containerPath = StrappyFileScannerPathFromComponents(
        components,
        applicationsIndex + 1U);
      info = StrappyFileScannerAppInfoForContainerPath(
        containerPath,
        containerAppInfoCache);
      bundleIdentifier = [info objectForKey:@"app_bundle_id"];
      appName = [info objectForKey:@"app_name"];
      bundlePath = [info objectForKey:@"app_bundle_path"];
      if (!StrappyFileScannerStringHasValue(appName) &&
          ((applicationsIndex + 2U) < [components count])) {
        NSString *fallbackComponent;

        fallbackComponent = [components objectAtIndex:(applicationsIndex + 2U)];
        appName = [[fallbackComponent lastPathComponent]
          stringByDeletingPathExtension];
      }
      groupKey = StrappyFileScannerStringHasValue(bundleIdentifier) ?
        [@"bundle:" stringByAppendingString:bundleIdentifier] :
        [@"container:" stringByAppendingString:containerPath];
      return StrappyFileScannerMetadata(groupKey,
                                        appName,
                                        bundleIdentifier,
                                        containerPath,
                                        bundlePath,
                                        @"bundle_plist");
    }

    return StrappyFileScannerMetadata(
      [@"app-path:" stringByAppendingString:
        StrappyFileScannerPathFromComponents(components,
                                             applicationsIndex + 1U)],
      [[nextComponent lastPathComponent] stringByDeletingPathExtension],
      nil,
      StrappyFileScannerPathFromComponents(components,
                                           applicationsIndex + 1U),
      nil,
      @"applications_path_name");
  }

  libraryIndex = StrappyFileScannerMobileChildIndex(components, @"Library");
  if ((libraryIndex != NSNotFound) && ((libraryIndex + 1U) < [components count])) {
    NSString *libraryComponent;

    libraryComponent = [components objectAtIndex:(libraryIndex + 1U)];
    if ([libraryComponent isEqualToString:@"Caches"] &&
        ((libraryIndex + 2U) < [components count])) {
      NSString *cacheComponent;
      NSString *appName;

      cacheComponent = [components objectAtIndex:(libraryIndex + 2U)];
      appName = [installedAppNamesByBundleIdentifier objectForKey:cacheComponent];
      if (!StrappyFileScannerStringHasValue(appName)) {
        appName = StrappyFileScannerKnownNameForBundleIdentifier(cacheComponent);
      }
      if (!StrappyFileScannerStringHasValue(appName)) {
        appName = StrappyFileScannerLooksLikeBundleIdentifier(cacheComponent) ?
          cacheComponent : @"Caches";
      }
      return StrappyFileScannerMetadata(
        StrappyFileScannerLooksLikeBundleIdentifier(cacheComponent) ?
          [@"bundle:" stringByAppendingString:cacheComponent] :
          [@"system:" stringByAppendingString:libraryComponent],
        appName,
        StrappyFileScannerLooksLikeBundleIdentifier(cacheComponent) ?
          cacheComponent : nil,
        nil,
        nil,
        StrappyFileScannerLooksLikeBundleIdentifier(cacheComponent) ?
          @"cache_bundle_id" : @"system_cache");
    }

    if ([libraryComponent isEqualToString:@"Application Support"] &&
        ((libraryIndex + 2U) < [components count])) {
      NSString *supportComponent;

      supportComponent = [components objectAtIndex:(libraryIndex + 2U)];
      if ([supportComponent isEqualToString:@"Ubiquity"]) {
        return StrappyFileScannerMetadata(@"system:icloud-drive",
                                          @"iCloud Drive",
                                          nil,
                                          nil,
                                          nil,
                                          @"application_support_rule");
      }
      if ([supportComponent isEqualToString:@"Strappy"]) {
        return StrappyFileScannerMetadata(
          @"bundle:com.altivecintelligence.Strappy",
          @"Strappy",
          @"com.altivecintelligence.Strappy",
          nil,
          nil,
          @"application_support_name");
      }
      return StrappyFileScannerMetadata(
        [@"app-support:" stringByAppendingString:supportComponent],
        supportComponent,
        nil,
        nil,
        nil,
        @"application_support_name");
    }

    if ([libraryComponent hasPrefix:@"processed-Mobile Documents"]) {
      return StrappyFileScannerMetadata(@"system:icloud-drive",
                                        @"iCloud Drive",
                                        nil,
                                        nil,
                                        nil,
                                        @"system_rule");
    }

    return StrappyFileScannerMetadata(
      [@"system:" stringByAppendingString:libraryComponent],
      StrappyFileScannerKnownNameForLibraryComponent(libraryComponent),
      nil,
      nil,
      nil,
      @"system_library_top");
  }

  mediaIndex = StrappyFileScannerMobileChildIndex(components, @"Media");
  if ((mediaIndex != NSNotFound) && ((mediaIndex + 1U) < [components count])) {
    NSString *mediaComponent;

    mediaComponent = [components objectAtIndex:(mediaIndex + 1U)];
    return StrappyFileScannerMetadata(
      [@"media:" stringByAppendingString:mediaComponent],
      StrappyFileScannerKnownNameForMediaComponent(mediaComponent),
      nil,
      nil,
      nil,
      @"media_top");
  }

  return nil;
}

static void StrappyFileScannerAddCString(NSMutableDictionary *dictionary,
                                         NSString *key,
                                         const char *value)
{
  NSString *string;

  if ((dictionary == nil) || ![key isKindOfClass:[NSString class]] ||
      (value == NULL) || (value[0] == '\0')) {
    return;
  }

  string = [NSString stringWithUTF8String:value];
  if ([string length] > 0U) {
    [dictionary setObject:string forKey:key];
  }
}

@implementation FileScanner

+ (FileScanner *)sharedScanner
{
  static FileScanner *scanner = nil;

  @synchronized(self) {
    if (scanner == nil) {
      scanner = [[FileScanner alloc] init];
    }
  }

  return scanner;
}

+ (NSError *)errorFromCString:(char *)message
{
  NSString *description;
  NSDictionary *userInfo;

  if (message != NULL) {
    description = [NSString stringWithUTF8String:message];
  } else {
    description = nil;
  }

  if (description == nil) {
    description = NSLocalizedString(@"Filesystem scan failed.", nil);
  }

  userInfo = [NSDictionary dictionaryWithObject:description
                                         forKey:NSLocalizedDescriptionKey];
  return [NSError errorWithDomain:@"FileScannerErrorDomain"
                             code:1
                         userInfo:userInfo];
}

+ (NSString *)stringFromFileSystemPath:(const char *)path
{
  NSFileManager *fileManager;

  if (path == NULL) {
    return nil;
  }

  fileManager = [NSFileManager defaultManager];
  return [fileManager stringWithFileSystemRepresentation:path
                                                  length:strlen(path)];
}

+ (NSString *)stringFromCStringOrEmpty:(const char *)value
{
  NSString *string;

  if (value == NULL) {
    return @"";
  }

  string = [NSString stringWithUTF8String:value];
  if (string == nil) {
    return @"";
  }

  return string;
}

+ (NSDictionary *)dictionaryFromScannerRecord:
    (const strappy_file_scanner_record *)record
{
  NSString *path;
  NSString *validationError;
  NSNumber *size;
  NSNumber *modifiedAt;
  NSNumber *device;
  NSNumber *inode;
  NSNumber *isValidSQLite;
  NSNumber *hidden;
  NSMutableDictionary *dictionary;

  if (record == NULL) {
    return nil;
  }

  path = [FileScanner stringFromFileSystemPath:record->path];
  if (path == nil) {
    return nil;
  }

  validationError = nil;
  if (record->validation_error != NULL) {
    validationError = [NSString stringWithUTF8String:record->validation_error];
  }
  if (validationError == nil) {
    validationError = @"";
  }

  size = [NSNumber numberWithLongLong:record->size];
  modifiedAt = [NSNumber numberWithLongLong:record->modified_at];
  device = [NSNumber numberWithUnsignedLongLong:record->device];
  inode = [NSNumber numberWithUnsignedLongLong:record->inode];
  isValidSQLite = [NSNumber numberWithBool:(record->is_valid_sqlite ? YES : NO)];
  hidden = [NSNumber numberWithBool:(record->hidden ? YES : NO)];

  dictionary = [NSMutableDictionary dictionaryWithObjectsAndKeys:
    path, @"path",
    size, @"size",
    modifiedAt, @"modified_at",
    device, @"device",
    inode, @"inode",
    isValidSQLite, @"is_valid_sqlite",
    hidden, @"hidden",
    nil];
  if ([validationError length] > 0U) {
    [dictionary setObject:validationError forKey:@"validation_error"];
  }
  StrappyFileScannerAddCString(dictionary,
                               @"app_group_key",
                               record->app_group_key);
  StrappyFileScannerAddCString(dictionary, @"app_name", record->app_name);
  StrappyFileScannerAddCString(dictionary,
                               @"app_bundle_id",
                               record->app_bundle_id);
  StrappyFileScannerAddCString(dictionary,
                               @"app_container_path",
                               record->app_container_path);
  StrappyFileScannerAddCString(dictionary,
                               @"app_bundle_path",
                               record->app_bundle_path);
  StrappyFileScannerAddCString(dictionary, @"app_source", record->app_source);

  return dictionary;
}

+ (NSDictionary *)dictionaryFromDiscoveredDatabaseRecord:
    (const strappy_discovered_database_record *)record
{
  NSNumber *catalogId;
  NSNumber *size;
  NSNumber *modifiedAt;
  NSNumber *device;
  NSNumber *inode;
  NSNumber *isValidSQLite;
  NSNumber *hidden;
  NSString *assistantDatabaseId;
  NSString *path;
  NSString *validationError;
  NSString *scanStatus;
  NSString *userDecision;
  NSString *scanRoot;
  NSString *appGroupKey;
  NSString *appName;
  NSString *appBundleId;
  NSString *appContainerPath;
  NSString *appBundlePath;
  NSString *appSource;
  NSString *firstSeenAt;
  NSString *lastSeenAt;
  NSString *lastScannedAt;
  NSMutableDictionary *dictionary;

  if (record == NULL) {
    return nil;
  }

  catalogId = [NSNumber numberWithLongLong:record->catalog_id];
  size = [NSNumber numberWithLongLong:record->size];
  modifiedAt = [NSNumber numberWithLongLong:record->modified_at];
  device = [NSNumber numberWithUnsignedLongLong:record->device];
  inode = [NSNumber numberWithUnsignedLongLong:record->inode];
  isValidSQLite = [NSNumber numberWithBool:(record->is_valid_sqlite ? YES : NO)];
  hidden = [NSNumber numberWithBool:(record->hidden ? YES : NO)];
  assistantDatabaseId =
    [FileScanner stringFromCStringOrEmpty:record->assistant_database_id];
  path = [FileScanner stringFromCStringOrEmpty:record->path];
  validationError =
    [FileScanner stringFromCStringOrEmpty:record->validation_error];
  scanStatus = [FileScanner stringFromCStringOrEmpty:record->scan_status];
  userDecision = [FileScanner stringFromCStringOrEmpty:record->user_decision];
  scanRoot = [FileScanner stringFromCStringOrEmpty:record->scan_root];
  appGroupKey = [FileScanner stringFromCStringOrEmpty:record->app_group_key];
  appName = [FileScanner stringFromCStringOrEmpty:record->app_name];
  appBundleId = [FileScanner stringFromCStringOrEmpty:record->app_bundle_id];
  appContainerPath =
    [FileScanner stringFromCStringOrEmpty:record->app_container_path];
  appBundlePath =
    [FileScanner stringFromCStringOrEmpty:record->app_bundle_path];
  appSource = [FileScanner stringFromCStringOrEmpty:record->app_source];
  firstSeenAt = [FileScanner stringFromCStringOrEmpty:record->first_seen_at];
  lastSeenAt = [FileScanner stringFromCStringOrEmpty:record->last_seen_at];
  lastScannedAt =
    [FileScanner stringFromCStringOrEmpty:record->last_scanned_at];

  if ([path length] == 0U) {
    return nil;
  }

  dictionary = [NSMutableDictionary dictionaryWithObjectsAndKeys:
    catalogId, @"catalog_id",
    assistantDatabaseId, @"assistant_database_id",
    path, @"path",
    size, @"size",
    modifiedAt, @"modified_at",
    device, @"device",
    inode, @"inode",
    isValidSQLite, @"is_valid_sqlite",
    hidden, @"hidden",
    scanStatus, @"scan_status",
    userDecision, @"user_decision",
    firstSeenAt, @"first_seen_at",
    lastSeenAt, @"last_seen_at",
    lastScannedAt, @"last_scanned_at",
    nil];
  if ([validationError length] > 0U) {
    [dictionary setObject:validationError forKey:@"validation_error"];
  }
  if ([scanRoot length] > 0U) {
    [dictionary setObject:scanRoot forKey:@"scan_root"];
  }
  if ([appGroupKey length] > 0U) {
    [dictionary setObject:appGroupKey forKey:@"app_group_key"];
  }
  if ([appName length] > 0U) {
    [dictionary setObject:appName forKey:@"app_name"];
  }
  if ([appBundleId length] > 0U) {
    [dictionary setObject:appBundleId forKey:@"app_bundle_id"];
  }
  if ([appContainerPath length] > 0U) {
    [dictionary setObject:appContainerPath forKey:@"app_container_path"];
  }
  if ([appBundlePath length] > 0U) {
    [dictionary setObject:appBundlePath forKey:@"app_bundle_path"];
  }
  if ([appSource length] > 0U) {
    [dictionary setObject:appSource forKey:@"app_source"];
  }

  return dictionary;
}

+ (BOOL)setAppMetadata:(NSDictionary *)metadata
      forScannerRecord:(strappy_file_scanner_record *)record
                 error:(NSError **)error
{
  NSString *appGroupKey;
  NSString *appName;
  NSString *appBundleId;
  NSString *appContainerPath;
  NSString *appBundlePath;
  NSString *appSource;
  char *strappyError;
  int ok;

  if (record == NULL) {
    return YES;
  }
  if (![metadata isKindOfClass:[NSDictionary class]]) {
    return YES;
  }

  appGroupKey = [metadata objectForKey:@"app_group_key"];
  appName = [metadata objectForKey:@"app_name"];
  appBundleId = [metadata objectForKey:@"app_bundle_id"];
  appContainerPath = [metadata objectForKey:@"app_container_path"];
  appBundlePath = [metadata objectForKey:@"app_bundle_path"];
  appSource = [metadata objectForKey:@"app_source"];

  strappyError = NULL;
  ok = strappy_file_scanner_record_set_app_metadata(
    record,
    StrappyFileScannerStringHasValue(appGroupKey) ? [appGroupKey UTF8String] : NULL,
    StrappyFileScannerStringHasValue(appName) ? [appName UTF8String] : NULL,
    StrappyFileScannerStringHasValue(appBundleId) ? [appBundleId UTF8String] : NULL,
    StrappyFileScannerStringHasValue(appContainerPath) ?
      [appContainerPath UTF8String] : NULL,
    StrappyFileScannerStringHasValue(appBundlePath) ?
      [appBundlePath UTF8String] : NULL,
    StrappyFileScannerStringHasValue(appSource) ? [appSource UTF8String] : NULL,
    &strappyError);
  if (!ok) {
    if (error != nil) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return NO;
  }

  return YES;
}

+ (BOOL)annotateScannerRecordList:(strappy_file_scanner_record_list *)list
                      forRootPath:(NSString *)rootPath
                            error:(NSError **)error
{
  NSMutableDictionary *containerAppInfoCache;
  NSDictionary *installedAppNamesByBundleIdentifier;
  size_t index;

  if (list == NULL) {
    return YES;
  }

  containerAppInfoCache = [NSMutableDictionary dictionary];
  installedAppNamesByBundleIdentifier =
    StrappyFileScannerInstalledAppNamesByBundleIdentifier(rootPath);
  for (index = 0U; index < list->count; index++) {
    NSString *path;
    NSDictionary *metadata;

    path = [FileScanner stringFromFileSystemPath:list->records[index].path];
    metadata = StrappyFileScannerAppMetadataForPath(
      path,
      containerAppInfoCache,
      installedAppNamesByBundleIdentifier);
    if (![FileScanner setAppMetadata:metadata
                    forScannerRecord:&list->records[index]
                               error:error]) {
      return NO;
    }
  }

  return YES;
}

- (NSArray *)scanHomeDirectoryForSQLiteDatabasesWithError:(NSError **)error
{
  NSString *homeDirectory;

  homeDirectory = NSHomeDirectory();
  return [self scanDirectoryForSQLiteDatabasesAtPath:homeDirectory
                                               error:error];
}

- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                                             error:(NSError **)error
{
  strappy_file_scanner_options options;
  strappy_file_scanner_record_list list;
  NSMutableArray *records;
  char *strappyError;
  size_t index;

  if (![path isKindOfClass:[NSString class]] || ([path length] == 0U)) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Scan path is empty.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"FileScannerErrorDomain"
                                   code:2
                               userInfo:userInfo];
    }
    return nil;
  }

  strappy_file_scanner_options_init(&options);
  options.root_path = [path fileSystemRepresentation];
  options.validate_candidates = 1;

  strappy_file_scanner_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_file_scanner_scan(&options, &list, &strappyError)) {
    if (error != nil) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    strappy_file_scanner_record_list_destroy(&list);
    return nil;
  }
  if (![FileScanner annotateScannerRecordList:&list
                                  forRootPath:path
                                        error:error]) {
    strappy_file_scanner_record_list_destroy(&list);
    return nil;
  }

  records = [NSMutableArray arrayWithCapacity:list.count];
  for (index = 0U; index < list.count; index++) {
    NSDictionary *record =
      [FileScanner dictionaryFromScannerRecord:&list.records[index]];
    if (record != nil) {
      [records addObject:record];
    }
  }

  strappy_file_scanner_record_list_destroy(&list);
  return records;
}

- (NSArray *)scanDirectoryForSQLiteDatabasesAtPath:(NSString *)path
                   savingResultsToCatalogWithError:(NSError **)error
{
  NSString *databasePath;
  strappy_file_scanner_options options;
  strappy_file_scanner_record_list list;
  char *strappyError;

  if (![path isKindOfClass:[NSString class]] || ([path length] == 0U)) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Scan path is empty.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"FileScannerErrorDomain"
                                   code:2
                               userInfo:userInfo];
    }
    return nil;
  }

  if (![StrappySession initializeSessionStoreWithError:error]) {
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  strappy_file_scanner_options_init(&options);
  options.root_path = [path fileSystemRepresentation];
  options.validate_candidates = 1;

  strappy_file_scanner_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_file_scanner_scan(&options, &list, &strappyError)) {
    if (error != nil) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    strappy_file_scanner_record_list_destroy(&list);
    return nil;
  }
  if (![FileScanner annotateScannerRecordList:&list
                                  forRootPath:path
                                        error:error]) {
    strappy_file_scanner_record_list_destroy(&list);
    return nil;
  }
  if (!strappy_file_scanner_save_discovered_databases([databasePath UTF8String],
                                                      &list,
                                                      [path fileSystemRepresentation],
                                                      &strappyError)) {
    if (error != nil) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    strappy_file_scanner_record_list_destroy(&list);
    return nil;
  }

  strappy_file_scanner_record_list_destroy(&list);
  return [self catalogedSQLiteDatabasesWithError:error];
}

- (NSArray *)catalogedSQLiteDatabasesWithError:(NSError **)error
{
  NSString *databasePath;
  strappy_discovered_database_record_list list;
  NSMutableArray *rows;
  char *strappyError;
  size_t index;

  if (![StrappySession initializeSessionStoreWithError:error]) {
    return nil;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  strappy_discovered_database_record_list_init(&list);
  strappyError = NULL;
  if (!strappy_db_list_discovered_databases([databasePath UTF8String],
                                            &list,
                                            &strappyError)) {
    if (error != nil) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return nil;
  }

  rows = [NSMutableArray arrayWithCapacity:list.count];
  for (index = 0U; index < list.count; index++) {
    NSDictionary *row;

    row = [FileScanner dictionaryFromDiscoveredDatabaseRecord:&list.records[index]];
    if (row != nil) {
      [rows addObject:row];
    }
  }

  strappy_discovered_database_record_list_destroy(&list);
  return rows;
}

- (BOOL)setCatalogedDatabaseAllowed:(BOOL)allowed
               forCatalogIdentifier:(NSNumber *)catalogIdentifier
                              error:(NSError **)error
{
  NSString *databasePath;
  const char *decision;
  char *strappyError;
  int ok;

  if (![catalogIdentifier isKindOfClass:[NSNumber class]] ||
      ([catalogIdentifier longLongValue] <= 0LL)) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Database catalog id is missing.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"FileScannerErrorDomain"
                                   code:6
                               userInfo:userInfo];
    }
    return NO;
  }

  if (![StrappySession initializeSessionStoreWithError:error]) {
    return NO;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  decision = (allowed ? "allowed" : "unknown");
  strappyError = NULL;
  ok = strappy_db_update_discovered_database_decision(
    [databasePath UTF8String],
    [catalogIdentifier longLongValue],
    decision,
    &strappyError);
  if (!ok) {
    if (error != nil) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return NO;
  }

  return YES;
}

- (BOOL)setCatalogedDatabaseHidden:(BOOL)hidden
               forCatalogIdentifier:(NSNumber *)catalogIdentifier
                              error:(NSError **)error
{
  NSString *databasePath;
  char *strappyError;
  int ok;

  if (![catalogIdentifier isKindOfClass:[NSNumber class]] ||
      ([catalogIdentifier longLongValue] <= 0LL)) {
    if (error != nil) {
      NSDictionary *userInfo =
        [NSDictionary dictionaryWithObject:NSLocalizedString(@"Database catalog id is missing.", nil)
                                    forKey:NSLocalizedDescriptionKey];
      *error = [NSError errorWithDomain:@"FileScannerErrorDomain"
                                   code:6
                               userInfo:userInfo];
    }
    return NO;
  }

  if (![StrappySession initializeSessionStoreWithError:error]) {
    return NO;
  }

  databasePath = [StrappySession sessionsDatabasePath];
  strappyError = NULL;
  ok = strappy_db_update_discovered_database_hidden(
    [databasePath UTF8String],
    [catalogIdentifier longLongValue],
    hidden ? 1 : 0,
    &strappyError);
  if (!ok) {
    if (error != nil) {
      *error = [FileScanner errorFromCString:strappyError];
    }
    strappy_free_string(strappyError);
    return NO;
  }

  return YES;
}

@end
