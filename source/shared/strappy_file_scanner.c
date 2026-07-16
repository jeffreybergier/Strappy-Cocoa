#include "strappy_file_scanner.h"

#include "strappy_cocoa.h"
#include "strappy_core.h"
#include "strappy_db.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int strappy_file_scanner_ascii_tolower(int character)
{
  if ((character >= 'A') && (character <= 'Z')) {
    return character + ('a' - 'A');
  }
  return character;
}

static int strappy_file_scanner_case_insensitive_equal(const char *left,
                                                       const char *right)
{
  size_t index;

  if ((left == NULL) || (right == NULL)) {
    return 0;
  }

  for (index = 0U; (left[index] != '\0') && (right[index] != '\0'); index++) {
    if (strappy_file_scanner_ascii_tolower((unsigned char)left[index]) !=
        strappy_file_scanner_ascii_tolower((unsigned char)right[index])) {
      return 0;
    }
  }

  return (left[index] == right[index]) ? 1 : 0;
}

static const char *strappy_file_scanner_case_insensitive_find(
  const char *haystack,
  const char *needle)
{
  size_t haystack_index;
  size_t needle_index;

  if ((haystack == NULL) || (needle == NULL) || (needle[0] == '\0')) {
    return NULL;
  }

  for (haystack_index = 0U; haystack[haystack_index] != '\0'; haystack_index++) {
    for (needle_index = 0U; needle[needle_index] != '\0'; needle_index++) {
      if (haystack[haystack_index + needle_index] == '\0') {
        return NULL;
      }
      if (strappy_file_scanner_ascii_tolower(
            (unsigned char)haystack[haystack_index + needle_index]) !=
          strappy_file_scanner_ascii_tolower(
            (unsigned char)needle[needle_index])) {
        break;
      }
    }
    if (needle[needle_index] == '\0') {
      return haystack + haystack_index;
    }
  }

  return NULL;
}

static int strappy_file_scanner_case_insensitive_contains(const char *haystack,
                                                          const char *needle)
{
  return (strappy_file_scanner_case_insensitive_find(haystack, needle) != NULL) ?
    1 : 0;
}

static int strappy_file_scanner_case_insensitive_starts_with(
  const char *value,
  const char *prefix)
{
  size_t index;

  if ((value == NULL) || (prefix == NULL)) {
    return 0;
  }

  for (index = 0U; prefix[index] != '\0'; index++) {
    if (value[index] == '\0') {
      return 0;
    }
    if (strappy_file_scanner_ascii_tolower((unsigned char)value[index]) !=
        strappy_file_scanner_ascii_tolower((unsigned char)prefix[index])) {
      return 0;
    }
  }

  return 1;
}

static int strappy_file_scanner_case_insensitive_ends_with(const char *value,
                                                           const char *suffix)
{
  size_t value_length;
  size_t suffix_length;

  if ((value == NULL) || (suffix == NULL)) {
    return 0;
  }

  value_length = strlen(value);
  suffix_length = strlen(suffix);
  if (suffix_length > value_length) {
    return 0;
  }

  return strappy_file_scanner_case_insensitive_equal(
    value + (value_length - suffix_length),
    suffix);
}

static const char *strappy_file_scanner_basename(const char *path)
{
  const char *slash;

  if (path == NULL) {
    return NULL;
  }

  slash = strrchr(path, '/');
  return (slash != NULL) ? slash + 1 : path;
}

static int strappy_file_scanner_is_index_database_name(const char *name)
{
  size_t length;

  if (strappy_file_scanner_case_insensitive_equal(name, "index.db")) {
    return 1;
  }

  length = (name != NULL) ? strlen(name) : 0U;
  return (length > strlen("index-.db")) &&
         strappy_file_scanner_case_insensitive_starts_with(name, "index-") &&
         strappy_file_scanner_case_insensitive_ends_with(name, ".db") ?
    1 : 0;
}

static int strappy_file_scanner_is_apple_bundle_identifier(
  const char *bundle_identifier)
{
  return strappy_file_scanner_case_insensitive_starts_with(bundle_identifier,
                                                          "com.apple.") ?
    1 : 0;
}

static int strappy_file_scanner_database_should_be_hidden(
  const char *path,
  const char *app_bundle_id)
{
  const char *name;

  if ((path == NULL) || (path[0] == '\0')) {
    return 0;
  }

  name = strappy_file_scanner_basename(path);
  if (strappy_file_scanner_case_insensitive_contains(path, ".localstorage")) {
    return 1;
  }
  if (strappy_file_scanner_case_insensitive_equal(name, "ApplicationCache.db") ||
      strappy_file_scanner_case_insensitive_equal(name, "MapTiles.sqlitedb") ||
      strappy_file_scanner_case_insensitive_equal(name, "SafeBrowsing.db")) {
    return 1;
  }
  if ((strcmp(name, "Cache.db") == 0) ||
      strappy_file_scanner_case_insensitive_equal(name, "nsurlcache")) {
    return 1;
  }
  if ((strcmp(name, "cache.db") == 0) &&
      strappy_file_scanner_case_insensitive_contains(path, "/Caches/")) {
    return 1;
  }
  if (strappy_file_scanner_case_insensitive_contains(path, "/Library/Caches/")) {
    return 1;
  }
  if (strappy_file_scanner_is_apple_bundle_identifier(app_bundle_id) &&
      strappy_file_scanner_is_index_database_name(name)) {
    return 1;
  }

  return 0;
}

static char *strappy_file_scanner_duplicate_range(const char *start,
                                                  size_t length)
{
  char *copy;

  if (start == NULL) {
    return NULL;
  }

  copy = (char *)malloc(length + 1U);
  if (copy == NULL) {
    return NULL;
  }

  if (length > 0U) {
    memcpy(copy, start, length);
  }
  copy[length] = '\0';
  return copy;
}

typedef struct strappy_file_scanner_app_info {
  char *name;
  char *bundle_identifier;
  char *container_path;
  char *bundle_path;
} strappy_file_scanner_app_info;

typedef struct strappy_file_scanner_metadata_context {
  strappy_file_scanner_app_info *apps;
  size_t app_count;
} strappy_file_scanner_metadata_context;

static void strappy_file_scanner_metadata_context_init(
  strappy_file_scanner_metadata_context *context)
{
  if (context == NULL) {
    return;
  }
  context->apps = NULL;
  context->app_count = 0U;
}

static void strappy_file_scanner_metadata_context_destroy(
  strappy_file_scanner_metadata_context *context)
{
  size_t index;

  if (context == NULL) {
    return;
  }
  for (index = 0U; index < context->app_count; index++) {
    free(context->apps[index].name);
    free(context->apps[index].bundle_identifier);
    free(context->apps[index].container_path);
    free(context->apps[index].bundle_path);
  }
  free(context->apps);
  strappy_file_scanner_metadata_context_init(context);
}

static char *strappy_file_scanner_join_path(const char *directory,
                                            const char *name,
                                            char **error_out)
{
  size_t directory_length;
  size_t name_offset;
  size_t name_length;
  int needs_slash;
  char *path;

  if ((directory == NULL) || (directory[0] == '\0') ||
      (name == NULL) || (name[0] == '\0')) {
    strappy_set_error(error_out, "Scanner path join is incomplete.");
    return NULL;
  }
  directory_length = strlen(directory);
  name_offset = 0U;
  while (name[name_offset] == '/') {
    name_offset++;
  }
  name_length = strlen(name + name_offset);
  needs_slash = (directory[directory_length - 1U] != '/') ? 1 : 0;
  if (directory_length > (((size_t)-1) - name_length -
                          (size_t)needs_slash - 1U)) {
    strappy_set_error(error_out, "Scanner path is too large.");
    return NULL;
  }
  path = (char *)malloc(directory_length + name_length +
                        (size_t)needs_slash + 1U);
  if (path == NULL) {
    strappy_set_error(error_out, "Could not allocate scanner path.");
    return NULL;
  }
  memcpy(path, directory, directory_length);
  if (needs_slash) {
    path[directory_length] = '/';
    directory_length++;
  }
  memcpy(path + directory_length, name + name_offset, name_length);
  path[directory_length + name_length] = '\0';
  return path;
}

static int strappy_file_scanner_path_is_directory(const char *path)
{
  struct stat info;

  return ((path != NULL) && (stat(path, &info) == 0) &&
          S_ISDIR(info.st_mode)) ? 1 : 0;
}

static char *strappy_file_scanner_duplicate_optional(const char *value)
{
  return ((value != NULL) && (value[0] != '\0')) ?
    strappy_string_duplicate(value) : NULL;
}

static const strappy_file_scanner_app_info *
strappy_file_scanner_find_app_by_bundle_path(
  const strappy_file_scanner_metadata_context *context,
  const char *bundle_path)
{
  size_t index;

  if ((context == NULL) || (bundle_path == NULL)) {
    return NULL;
  }
  for (index = 0U; index < context->app_count; index++) {
    if ((context->apps[index].bundle_path != NULL) &&
        (strcmp(context->apps[index].bundle_path, bundle_path) == 0)) {
      return &context->apps[index];
    }
  }
  return NULL;
}

static const strappy_file_scanner_app_info *
strappy_file_scanner_find_app_by_container_path(
  const strappy_file_scanner_metadata_context *context,
  const char *container_path)
{
  size_t index;

  if ((context == NULL) || (container_path == NULL)) {
    return NULL;
  }
  for (index = 0U; index < context->app_count; index++) {
    if ((context->apps[index].container_path != NULL) &&
        (strcmp(context->apps[index].container_path, container_path) == 0)) {
      return &context->apps[index];
    }
  }
  return NULL;
}

static const strappy_file_scanner_app_info *
strappy_file_scanner_find_app_by_bundle_identifier(
  const strappy_file_scanner_metadata_context *context,
  const char *bundle_identifier)
{
  size_t index;

  if ((context == NULL) || (bundle_identifier == NULL)) {
    return NULL;
  }
  for (index = 0U; index < context->app_count; index++) {
    if ((context->apps[index].bundle_identifier != NULL) &&
        (strcmp(context->apps[index].bundle_identifier, bundle_identifier) ==
         0)) {
      return &context->apps[index];
    }
  }
  return NULL;
}

static int strappy_file_scanner_add_app_bundle(
  strappy_file_scanner_metadata_context *context,
  const char *bundle_path,
  const char *container_path,
  char **error_out)
{
  strappy_file_scanner_app_info *apps;
  strappy_file_scanner_app_info *app;
  char *bundle_identifier;
  char *bundle_path_copy;
  char *container_path_copy;
  char *name;

  if ((context == NULL) || (bundle_path == NULL) ||
      (bundle_path[0] == '\0')) {
    strappy_set_error(error_out, "Scanner app bundle is incomplete.");
    return 0;
  }
  if (strappy_file_scanner_find_app_by_bundle_path(context, bundle_path) !=
      NULL) {
    return 1;
  }
  name = NULL;
  bundle_identifier = NULL;
  if (!strappy_cocoa_copy_bundle_info(bundle_path,
                                      &name,
                                      &bundle_identifier,
                                      error_out)) {
    return 0;
  }
  bundle_path_copy = strappy_string_duplicate(bundle_path);
  container_path_copy =
    strappy_file_scanner_duplicate_optional(container_path);
  if ((bundle_path_copy == NULL) ||
      ((container_path != NULL) && (container_path[0] != '\0') &&
       (container_path_copy == NULL))) {
    free(name);
    free(bundle_identifier);
    free(bundle_path_copy);
    free(container_path_copy);
    strappy_set_error(error_out, "Could not allocate scanner app metadata.");
    return 0;
  }
  if (context->app_count >=
      (((size_t)-1) / sizeof(strappy_file_scanner_app_info))) {
    free(name);
    free(bundle_identifier);
    free(bundle_path_copy);
    free(container_path_copy);
    strappy_set_error(error_out, "Too many scanner applications.");
    return 0;
  }
  apps = (strappy_file_scanner_app_info *)realloc(
    context->apps,
    sizeof(strappy_file_scanner_app_info) * (context->app_count + 1U));
  if (apps == NULL) {
    free(name);
    free(bundle_identifier);
    free(bundle_path_copy);
    free(container_path_copy);
    strappy_set_error(error_out, "Could not allocate scanner applications.");
    return 0;
  }
  context->apps = apps;
  app = &context->apps[context->app_count];
  app->name = name;
  app->bundle_identifier = bundle_identifier;
  app->container_path = container_path_copy;
  app->bundle_path = bundle_path_copy;
  context->app_count++;
  return 1;
}

static int strappy_file_scanner_scan_nested_app_bundles(
  strappy_file_scanner_metadata_context *context,
  const char *container_path,
  char **error_out)
{
  struct dirent *entry;
  DIR *directory;

  directory = opendir(container_path);
  if (directory == NULL) {
    return 1;
  }
  while ((entry = readdir(directory)) != NULL) {
    char *bundle_path;

    if ((entry->d_name[0] == '.') ||
        !strappy_file_scanner_case_insensitive_ends_with(entry->d_name,
                                                         ".app")) {
      continue;
    }
    bundle_path = strappy_file_scanner_join_path(container_path,
                                                 entry->d_name,
                                                 error_out);
    if (bundle_path == NULL) {
      closedir(directory);
      return 0;
    }
    if (strappy_file_scanner_path_is_directory(bundle_path) &&
        !strappy_file_scanner_add_app_bundle(context,
                                             bundle_path,
                                             container_path,
                                             error_out)) {
      free(bundle_path);
      closedir(directory);
      return 0;
    }
    free(bundle_path);
  }
  closedir(directory);
  return 1;
}

static int strappy_file_scanner_scan_app_directory(
  strappy_file_scanner_metadata_context *context,
  const char *directory_path,
  char **error_out)
{
  struct dirent *entry;
  DIR *directory;

  directory = opendir(directory_path);
  if (directory == NULL) {
    return 1;
  }
  while ((entry = readdir(directory)) != NULL) {
    char *child_path;

    if (entry->d_name[0] == '.') {
      continue;
    }
    child_path = strappy_file_scanner_join_path(directory_path,
                                                entry->d_name,
                                                error_out);
    if (child_path == NULL) {
      closedir(directory);
      return 0;
    }
    if (!strappy_file_scanner_path_is_directory(child_path)) {
      free(child_path);
      continue;
    }
    if (strappy_file_scanner_case_insensitive_ends_with(entry->d_name,
                                                        ".app")) {
      if (!strappy_file_scanner_add_app_bundle(context,
                                               child_path,
                                               NULL,
                                               error_out)) {
        free(child_path);
        closedir(directory);
        return 0;
      }
    } else if (!strappy_file_scanner_scan_nested_app_bundles(context,
                                                              child_path,
                                                              error_out)) {
      free(child_path);
      closedir(directory);
      return 0;
    }
    free(child_path);
  }
  closedir(directory);
  return 1;
}

static int strappy_file_scanner_metadata_context_prepare(
  strappy_file_scanner_metadata_context *context,
  const char *root_path,
  char **error_out)
{
  char *directories[3];
  const char *home_path;
  size_t count;
  size_t index;
  size_t previous;

  directories[0] = NULL;
  directories[1] = NULL;
  directories[2] = NULL;
  count = 0U;
  home_path = getenv("HOME");
  if ((home_path != NULL) && (home_path[0] != '\0')) {
    directories[count] = strappy_file_scanner_join_path(home_path,
                                                        "Applications",
                                                        error_out);
    if (directories[count] == NULL) {
      return 0;
    }
    count++;
  }
  if ((root_path != NULL) && (root_path[0] != '\0')) {
    directories[count] = strappy_file_scanner_join_path(root_path,
                                                        "Applications",
                                                        error_out);
    if (directories[count] == NULL) {
      for (index = 0U; index < count; index++) {
        free(directories[index]);
      }
      return 0;
    }
    count++;
  }
  directories[count] = strappy_string_duplicate("/var/mobile/Applications");
  if (directories[count] == NULL) {
    for (index = 0U; index < count; index++) {
      free(directories[index]);
    }
    strappy_set_error(error_out, "Could not allocate app scan path.");
    return 0;
  }
  count++;

  for (index = 0U; index < count; index++) {
    int duplicate;

    duplicate = 0;
    for (previous = 0U; previous < index; previous++) {
      if (strcmp(directories[index], directories[previous]) == 0) {
        duplicate = 1;
        break;
      }
    }
    if (!duplicate &&
        !strappy_file_scanner_scan_app_directory(context,
                                                 directories[index],
                                                 error_out)) {
      for (previous = 0U; previous < count; previous++) {
        free(directories[previous]);
      }
      return 0;
    }
  }
  for (index = 0U; index < count; index++) {
    free(directories[index]);
  }
  return 1;
}

static int strappy_file_scanner_is_uuid_component(const char *component,
                                                  size_t length)
{
  size_t index;

  if ((component == NULL) || (length != 36U)) {
    return 0;
  }
  for (index = 0U; index < length; index++) {
    unsigned char character;

    character = (unsigned char)component[index];
    if ((index == 8U) || (index == 13U) ||
        (index == 18U) || (index == 23U)) {
      if (character != (unsigned char)'-') {
        return 0;
      }
    } else if (!(((character >= (unsigned char)'0') &&
                  (character <= (unsigned char)'9')) ||
                 ((character >= (unsigned char)'a') &&
                  (character <= (unsigned char)'f')) ||
                 ((character >= (unsigned char)'A') &&
                  (character <= (unsigned char)'F')))) {
      return 0;
    }
  }
  return 1;
}

static size_t strappy_file_scanner_component_length(const char *component)
{
  const char *end;

  if (component == NULL) {
    return 0U;
  }
  end = strchr(component, '/');
  return (end != NULL) ? (size_t)(end - component) : strlen(component);
}

static char *strappy_file_scanner_copy_component(const char *component)
{
  return strappy_file_scanner_duplicate_range(
    component,
    strappy_file_scanner_component_length(component));
}

static char *strappy_file_scanner_copy_parent_path(const char *path)
{
  const char *slash;

  if ((path == NULL) || (path[0] == '\0')) {
    return NULL;
  }
  slash = strrchr(path, '/');
  if (slash == NULL) {
    return NULL;
  }
  if (slash == path) {
    return strappy_file_scanner_duplicate_range(path, 1U);
  }
  return strappy_file_scanner_duplicate_range(path,
                                               (size_t)(slash - path));
}

static char *strappy_file_scanner_copy_component_name(const char *component)
{
  size_t length;

  if (component == NULL) {
    return NULL;
  }
  length = strappy_file_scanner_component_length(component);
  if ((length >= 4U) &&
      (strappy_file_scanner_ascii_tolower(
         (unsigned char)component[length - 4U]) == '.') &&
      (strappy_file_scanner_ascii_tolower(
         (unsigned char)component[length - 3U]) == 'a') &&
      (strappy_file_scanner_ascii_tolower(
         (unsigned char)component[length - 2U]) == 'p') &&
      (strappy_file_scanner_ascii_tolower(
         (unsigned char)component[length - 1U]) == 'p')) {
    length -= 4U;
  }
  return strappy_file_scanner_duplicate_range(component, length);
}

static char *strappy_file_scanner_prefixed_key(const char *prefix,
                                               const char *value,
                                               char **error_out)
{
  size_t prefix_length;
  size_t value_length;
  char *key;

  if ((prefix == NULL) || (value == NULL)) {
    strappy_set_error(error_out, "Scanner metadata key is incomplete.");
    return NULL;
  }
  prefix_length = strlen(prefix);
  value_length = strlen(value);
  if (prefix_length > (((size_t)-1) - value_length - 1U)) {
    strappy_set_error(error_out, "Scanner metadata key is too large.");
    return NULL;
  }
  key = (char *)malloc(prefix_length + value_length + 1U);
  if (key == NULL) {
    strappy_set_error(error_out, "Could not allocate scanner metadata key.");
    return NULL;
  }
  memcpy(key, prefix, prefix_length);
  memcpy(key + prefix_length, value, value_length + 1U);
  return key;
}

static int strappy_file_scanner_looks_like_bundle_identifier(
  const char *component)
{
  return ((component != NULL) && (strchr(component, '.') != NULL) &&
          (strchr(component, '/') == NULL)) ? 1 : 0;
}

static const char *strappy_file_scanner_known_bundle_name(const char *value)
{
  static const struct {
    const char *identifier;
    const char *name;
  } names[] = {
    { "com.apple.mobilesafari", "Safari" },
    { "com.apple.mobilemail", "Mail" },
    { "com.apple.mobilecal", "Calendar" },
    { "com.apple.MobileAddressBook", "Contacts" },
    { "com.apple.mobilenotes", "Notes" },
    { "com.apple.mobilephone", "Phone" },
    { "com.apple.mobileslideshow", "Photos" },
    { "com.apple.mobileipod", "Music" },
    { "com.apple.Maps", "Maps" },
    { "com.apple.AppStore", "App Store" },
    { "com.apple.MobileStore", "iTunes Store" },
    { "com.apple.itunesstored", "iTunes Store" },
    { "com.apple.iTunesStore", "iTunes Store" },
    { "com.apple.Preferences", "Settings" },
    { "com.apple.springboard", "SpringBoard" },
    { "com.saurik.Cydia", "Cydia" }
  };
  size_t index;

  for (index = 0U; index < (sizeof(names) / sizeof(names[0])); index++) {
    if ((value != NULL) && (strcmp(value, names[index].identifier) == 0)) {
      return names[index].name;
    }
  }
  return NULL;
}

static const char *strappy_file_scanner_known_library_name(const char *value)
{
  static const struct {
    const char *component;
    const char *name;
  } names[] = {
    { "Accounts", "Apple Accounts" }, { "AddressBook", "Contacts" },
    { "AggregateDictionary", "Diagnostics" }, { "Calendar", "Calendar" },
    { "GameKit", "Game Center" }, { "Keyboard", "Keyboard" },
    { "LASD", "Location Services" }, { "Mail", "Mail" },
    { "MediaStream", "Photo Stream" }, { "MusicLibrary", "Music" },
    { "Notes", "Notes" }, { "Passes", "Passbook" },
    { "SMS", "Messages" }, { "Safari", "Safari" },
    { "Social", "Social" }, { "Spotlight", "Spotlight" },
    { "TCC", "Privacy" }, { "Twitter", "Twitter" },
    { "Voicemail", "Phone" }, { "WebClips", "Web Clips" },
    { "WebKit", "WebKit / Safari" },
    { "com.apple.iTunesStore", "iTunes Store" },
    { "com.apple.itunesstored", "iTunes Store" }
  };
  size_t index;

  for (index = 0U; index < (sizeof(names) / sizeof(names[0])); index++) {
    if ((value != NULL) && (strcmp(value, names[index].component) == 0)) {
      return names[index].name;
    }
  }
  return value;
}

static const char *strappy_file_scanner_known_media_name(const char *value)
{
  if ((value != NULL) && (strcmp(value, "PhotoData") == 0)) {
    return "Photos";
  }
  if ((value != NULL) && (strcmp(value, "iTunes_Control") == 0)) {
    return "Music";
  }
  if ((value != NULL) && (strcmp(value, "Downloads") == 0)) {
    return "iTunes Downloads";
  }
  if ((value != NULL) && (strcmp(value, "Books") == 0)) {
    return "Books";
  }
  if ((value != NULL) && (strcmp(value, "Recordings") == 0)) {
    return "Voice Memos";
  }
  return value;
}

static int strappy_file_scanner_set_record_metadata(
  strappy_file_scanner_record *record,
  const char *key_prefix,
  const char *key_value,
  const char *name,
  const char *bundle_identifier,
  const char *container_path,
  const char *bundle_path,
  const char *source,
  char **error_out)
{
  char *group_key;
  int ok;

  group_key = strappy_file_scanner_prefixed_key(key_prefix,
                                                key_value,
                                                error_out);
  if (group_key == NULL) {
    return 0;
  }
  ok = strappy_file_scanner_record_set_app_metadata(record,
                                                    group_key,
                                                    name,
                                                    bundle_identifier,
                                                    container_path,
                                                    bundle_path,
                                                    source,
                                                    error_out);
  free(group_key);
  return ok;
}

static int strappy_file_scanner_annotate_bundle_record(
  strappy_file_scanner_metadata_context *context,
  strappy_file_scanner_record *record,
  const char *bundle_suffix,
  char **error_out)
{
  const strappy_file_scanner_app_info *app;
  const char *key_prefix;
  const char *key_value;
  char *bundle_path;
  char *container_path;
  char *parent_path;
  char *parent_parent_path;
  size_t bundle_length;

  bundle_length = (size_t)(bundle_suffix - record->path) + 4U;
  bundle_path = strappy_file_scanner_duplicate_range(record->path,
                                                      bundle_length);
  if (bundle_path == NULL) {
    strappy_set_error(error_out, "Could not allocate app bundle path.");
    return 0;
  }
  parent_path = strappy_file_scanner_copy_parent_path(bundle_path);
  parent_parent_path = strappy_file_scanner_copy_parent_path(parent_path);
  container_path = NULL;
  if ((parent_path != NULL) && (parent_parent_path != NULL)) {
    const char *parent_name;
    const char *parent_parent_name;

    parent_name = strrchr(parent_path, '/');
    parent_name = (parent_name != NULL) ? parent_name + 1 : parent_path;
    parent_parent_name = strrchr(parent_parent_path, '/');
    parent_parent_name = (parent_parent_name != NULL) ?
      parent_parent_name + 1 : parent_parent_path;
    if ((strcmp(parent_parent_name, "Applications") == 0) &&
        strappy_file_scanner_is_uuid_component(parent_name,
                                               strlen(parent_name))) {
      container_path = strappy_string_duplicate(parent_path);
      if (container_path == NULL) {
        free(bundle_path);
        free(parent_path);
        free(parent_parent_path);
        strappy_set_error(error_out, "Could not allocate app container path.");
        return 0;
      }
    }
  }
  free(parent_path);
  free(parent_parent_path);

  app = strappy_file_scanner_find_app_by_bundle_path(context, bundle_path);
  if (app == NULL) {
    if (!strappy_file_scanner_add_app_bundle(context,
                                             bundle_path,
                                             container_path,
                                             error_out)) {
      free(bundle_path);
      free(container_path);
      return 0;
    }
    app = strappy_file_scanner_find_app_by_bundle_path(context, bundle_path);
  }
  if ((app != NULL) && (app->bundle_identifier != NULL)) {
    key_prefix = "bundle:";
    key_value = app->bundle_identifier;
  } else if (container_path != NULL) {
    key_prefix = "container:";
    key_value = container_path;
  } else {
    key_prefix = "app-path:";
    key_value = bundle_path;
  }
  if (!strappy_file_scanner_set_record_metadata(
        record,
        key_prefix,
        key_value,
        (app != NULL) ? app->name : NULL,
        (app != NULL) ? app->bundle_identifier : NULL,
        container_path,
        bundle_path,
        "bundle_plist",
        error_out)) {
    free(bundle_path);
    free(container_path);
    return 0;
  }
  free(bundle_path);
  free(container_path);
  return 1;
}

static int strappy_file_scanner_annotate_record(
  strappy_file_scanner_metadata_context *context,
  strappy_file_scanner_record *record,
  char **error_out)
{
  const strappy_file_scanner_app_info *app;
  const char *marker;
  const char *name;
  const char *next;
  const char *suffix;
  char *component;
  char *container_path;
  char *fallback_name;
  size_t component_length;

  if ((context == NULL) || (record == NULL) || (record->path == NULL)) {
    strappy_set_error(error_out, "Scanner metadata request is incomplete.");
    return 0;
  }
  suffix = strappy_file_scanner_case_insensitive_find(record->path, ".app/");
  if (suffix != NULL) {
    return strappy_file_scanner_annotate_bundle_record(context,
                                                       record,
                                                       suffix,
                                                       error_out);
  }

  marker = strstr(record->path, "/Applications/");
  if (marker != NULL) {
    next = marker + strlen("/Applications/");
    component_length = strappy_file_scanner_component_length(next);
    container_path = strappy_file_scanner_duplicate_range(
      record->path,
      (size_t)(next - record->path) + component_length);
    if (container_path == NULL) {
      strappy_set_error(error_out, "Could not allocate app container path.");
      return 0;
    }
    if (strappy_file_scanner_is_uuid_component(next, component_length)) {
      app = strappy_file_scanner_find_app_by_container_path(context,
                                                            container_path);
      fallback_name = NULL;
      if ((app == NULL) && (next[component_length] == '/')) {
        fallback_name = strappy_file_scanner_copy_component_name(
          next + component_length + 1U);
      }
      if ((app == NULL) && (fallback_name == NULL)) {
        free(container_path);
        return 1;
      }
      if (!strappy_file_scanner_set_record_metadata(
            record,
            ((app != NULL) && (app->bundle_identifier != NULL)) ?
              "bundle:" : "container:",
            ((app != NULL) && (app->bundle_identifier != NULL)) ?
              app->bundle_identifier : container_path,
            (app != NULL) ? app->name : fallback_name,
            (app != NULL) ? app->bundle_identifier : NULL,
            container_path,
            (app != NULL) ? app->bundle_path : NULL,
            "bundle_plist",
            error_out)) {
        free(container_path);
        free(fallback_name);
        return 0;
      }
      free(container_path);
      free(fallback_name);
      return 1;
    }
    fallback_name = strappy_file_scanner_copy_component_name(next);
    if (fallback_name == NULL) {
      free(container_path);
      strappy_set_error(error_out, "Could not allocate app name.");
      return 0;
    }
    if (!strappy_file_scanner_set_record_metadata(record,
                                                   "app-path:",
                                                   container_path,
                                                   fallback_name,
                                                   NULL,
                                                   container_path,
                                                   NULL,
                                                   "applications_path_name",
                                                   error_out)) {
      free(container_path);
      free(fallback_name);
      return 0;
    }
    free(container_path);
    free(fallback_name);
    return 1;
  }

  marker = strstr(record->path, "/var/mobile/Library/");
  if (marker != NULL) {
    next = marker + strlen("/var/mobile/Library/");
    component = strappy_file_scanner_copy_component(next);
    if (component == NULL) {
      strappy_set_error(error_out, "Could not allocate Library metadata.");
      return 0;
    }
    if ((strcmp(component, "Caches") == 0) &&
        (next[strappy_file_scanner_component_length(next)] == '/')) {
      char *cache_component;

      cache_component = strappy_file_scanner_copy_component(
        next + strappy_file_scanner_component_length(next) + 1U);
      if (cache_component == NULL) {
        free(component);
        strappy_set_error(error_out, "Could not allocate cache metadata.");
        return 0;
      }
      app = strappy_file_scanner_find_app_by_bundle_identifier(
        context,
        cache_component);
      name = (app != NULL) ? app->name :
        strappy_file_scanner_known_bundle_name(cache_component);
      if (name == NULL) {
        name = strappy_file_scanner_looks_like_bundle_identifier(
          cache_component) ? cache_component : "Caches";
      }
      if (!strappy_file_scanner_set_record_metadata(
            record,
            strappy_file_scanner_looks_like_bundle_identifier(cache_component) ?
              "bundle:" : "system:",
            strappy_file_scanner_looks_like_bundle_identifier(cache_component) ?
              cache_component : component,
            name,
            strappy_file_scanner_looks_like_bundle_identifier(cache_component) ?
              cache_component : NULL,
            NULL,
            NULL,
            strappy_file_scanner_looks_like_bundle_identifier(cache_component) ?
              "cache_bundle_id" : "system_cache",
            error_out)) {
        free(cache_component);
        free(component);
        return 0;
      }
      free(cache_component);
      free(component);
      return 1;
    }
    if ((strcmp(component, "Application Support") == 0) &&
        (next[strappy_file_scanner_component_length(next)] == '/')) {
      char *support_component;
      int ok;

      support_component = strappy_file_scanner_copy_component(
        next + strappy_file_scanner_component_length(next) + 1U);
      if (support_component == NULL) {
        free(component);
        strappy_set_error(error_out,
                          "Could not allocate application support metadata.");
        return 0;
      }
      if (strcmp(support_component, "Ubiquity") == 0) {
        ok = strappy_file_scanner_set_record_metadata(
          record, "system:", "icloud-drive", "iCloud Drive", NULL,
          NULL, NULL, "application_support_rule", error_out);
      } else if (strcmp(support_component, "Strappy") == 0) {
        ok = strappy_file_scanner_set_record_metadata(
          record, "bundle:", "com.altivecintelligence.Strappy", "Strappy",
          "com.altivecintelligence.Strappy", NULL, NULL,
          "application_support_name", error_out);
      } else {
        ok = strappy_file_scanner_set_record_metadata(
          record, "app-support:", support_component, support_component, NULL,
          NULL, NULL, "application_support_name", error_out);
      }
      free(support_component);
      free(component);
      return ok;
    }
    if (strncmp(component,
                "processed-Mobile Documents",
                strlen("processed-Mobile Documents")) == 0) {
      int ok;

      ok = strappy_file_scanner_set_record_metadata(
        record, "system:", "icloud-drive", "iCloud Drive", NULL,
        NULL, NULL, "system_rule", error_out);
      free(component);
      return ok;
    }
    name = strappy_file_scanner_known_library_name(component);
    if (!strappy_file_scanner_set_record_metadata(record,
                                                   "system:",
                                                   component,
                                                   name,
                                                   NULL,
                                                   NULL,
                                                   NULL,
                                                   "system_library_top",
                                                   error_out)) {
      free(component);
      return 0;
    }
    free(component);
    return 1;
  }

  marker = strstr(record->path, "/var/mobile/Media/");
  if (marker != NULL) {
    next = marker + strlen("/var/mobile/Media/");
    component = strappy_file_scanner_copy_component(next);
    if (component == NULL) {
      strappy_set_error(error_out, "Could not allocate Media metadata.");
      return 0;
    }
    name = strappy_file_scanner_known_media_name(component);
    if (!strappy_file_scanner_set_record_metadata(record,
                                                   "media:",
                                                   component,
                                                   name,
                                                   NULL,
                                                   NULL,
                                                   NULL,
                                                   "media_top",
                                                   error_out)) {
      free(component);
      return 0;
    }
    free(component);
  }
  return 1;
}

static const char *strappy_file_scanner_database_origin_kind(const char *path)
{
  int app_data_path;

  if ((path == NULL) || (path[0] == '\0')) {
    return "other";
  }

  app_data_path =
    ((strappy_file_scanner_case_insensitive_find(
        path,
        "/Containers/Data/Application/") != NULL) ||
     (strappy_file_scanner_case_insensitive_find(
        path,
        "/Containers/Shared/AppGroup/") != NULL) ||
     (strappy_file_scanner_case_insensitive_find(
        path,
        "/mobile/Applications/") != NULL)) ? 1 : 0;

  if (strappy_file_scanner_case_insensitive_find(path, ".app/") != NULL) {
    return "app_bundle";
  }
  if (strappy_file_scanner_case_insensitive_find(path, "/Documents/") != NULL) {
    return "documents";
  }
  if (strappy_file_scanner_case_insensitive_find(
        path,
        "/Library/Application Support/") != NULL) {
    return "application_support";
  }
  if ((strappy_file_scanner_case_insensitive_find(path, "/Library/Caches/") !=
       NULL) ||
      (strappy_file_scanner_case_insensitive_find(path, "/Caches/") != NULL)) {
    return "cache";
  }
  if (strappy_file_scanner_case_insensitive_find(path, "/Media/") != NULL) {
    return "media";
  }
  if (strappy_file_scanner_case_insensitive_find(path, "/Library/") != NULL) {
    return (app_data_path ||
            (strappy_file_scanner_case_insensitive_find(path,
                                                        "/Applications/") !=
             NULL)) ? "app_library" : "system_library";
  }

  return "other";
}

static char *strappy_file_scanner_last_two_path_components(const char *path)
{
  const char *cursor;
  const char *end;
  const char *last_slash;
  const char *previous_slash;
  const char *start;
  size_t length;

  if ((path == NULL) || (path[0] == '\0')) {
    return NULL;
  }

  length = strlen(path);
  while ((length > 0U) && (path[length - 1U] == '/')) {
    length--;
  }
  if (length == 0U) {
    return NULL;
  }

  end = path + length;
  last_slash = NULL;
  for (cursor = path; cursor < end; cursor++) {
    if (*cursor == '/') {
      last_slash = cursor;
    }
  }

  if (last_slash == NULL) {
    return strappy_file_scanner_duplicate_range(path, length);
  }

  previous_slash = NULL;
  for (cursor = path; cursor < last_slash; cursor++) {
    if (*cursor == '/') {
      previous_slash = cursor;
    }
  }

  start = (previous_slash != NULL) ? previous_slash + 1 : last_slash + 1;
  if (start >= end) {
    return NULL;
  }

  return strappy_file_scanner_duplicate_range(start, (size_t)(end - start));
}

static char *strappy_file_scanner_tail_after_marker(const char *path,
                                                    const char *marker)
{
  const char *match;

  match = strappy_file_scanner_case_insensitive_find(path, marker);
  if (match == NULL) {
    return NULL;
  }

  match += strlen(marker);
  if (match[0] == '\0') {
    return NULL;
  }

  return strappy_string_duplicate(match);
}

static char *strappy_file_scanner_app_bundle_location_tail(const char *path)
{
  const char *app_suffix;
  const char *cursor;
  const char *start;

  app_suffix = strappy_file_scanner_case_insensitive_find(path, ".app/");
  if (app_suffix == NULL) {
    return NULL;
  }

  start = path;
  for (cursor = path; cursor < app_suffix; cursor++) {
    if (*cursor == '/') {
      start = cursor + 1;
    }
  }

  if ((start == NULL) || (start[0] == '\0')) {
    return NULL;
  }

  return strappy_string_duplicate(start);
}

static char *strappy_file_scanner_database_location_tail(const char *path,
                                                         const char *origin_kind)
{
  char *tail;

  if ((path == NULL) || (path[0] == '\0')) {
    return NULL;
  }

  tail = NULL;
  if ((origin_kind != NULL) && (strcmp(origin_kind, "app_bundle") == 0)) {
    tail = strappy_file_scanner_app_bundle_location_tail(path);
  } else if ((origin_kind != NULL) &&
             (strcmp(origin_kind, "application_support") == 0)) {
    tail = strappy_file_scanner_tail_after_marker(
      path,
      "/Library/Application Support/");
  } else if ((origin_kind != NULL) &&
             (strcmp(origin_kind, "documents") == 0)) {
    tail = strappy_file_scanner_tail_after_marker(path, "/Documents/");
  } else if ((origin_kind != NULL) && (strcmp(origin_kind, "cache") == 0)) {
    tail = strappy_file_scanner_tail_after_marker(path, "/Library/Caches/");
    if (tail == NULL) {
      tail = strappy_file_scanner_tail_after_marker(path, "/Caches/");
    }
  } else if ((origin_kind != NULL) &&
             ((strcmp(origin_kind, "app_library") == 0) ||
              (strcmp(origin_kind, "system_library") == 0))) {
    tail = strappy_file_scanner_tail_after_marker(path, "/Library/");
  } else if ((origin_kind != NULL) && (strcmp(origin_kind, "media") == 0)) {
    tail = strappy_file_scanner_tail_after_marker(path, "/Media/");
  }

  if (tail == NULL) {
    tail = strappy_file_scanner_last_two_path_components(path);
  }

  return tail;
}

static int strappy_file_scanner_report_progress(
  const strappy_file_scanner_options *options,
  const strappy_file_scanner_progress *progress)
{
  if ((options == NULL) || (options->progress_callback == NULL)) {
    return 1;
  }

  return options->progress_callback(progress, options->progress_user_data);
}

void strappy_file_scanner_options_init(strappy_file_scanner_options *options)
{
  if (options == NULL) {
    return;
  }

  options->root_path = NULL;
  options->validate_candidates = 1;
  options->max_files = 0L;
  options->max_results = 0L;
  options->max_depth = -1;
  options->progress_callback = NULL;
  options->progress_user_data = NULL;
  options->record_batch_size = 0U;
  options->record_batch_callback = NULL;
  options->record_batch_user_data = NULL;
}

void strappy_file_scanner_record_init(strappy_file_scanner_record *record)
{
  if (record == NULL) {
    return;
  }

  record->path = NULL;
  record->size = 0;
  record->modified_at = 0;
  record->device = 0ULL;
  record->inode = 0ULL;
  record->is_valid_sqlite = 0;
  record->validation_error = NULL;
  record->app_group_key = NULL;
  record->app_name = NULL;
  record->app_bundle_id = NULL;
  record->app_container_path = NULL;
  record->app_bundle_path = NULL;
  record->app_source = NULL;
  record->origin_kind = NULL;
  record->location_tail = NULL;
  record->hidden = 0;
}

void strappy_file_scanner_record_destroy(strappy_file_scanner_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->path);
  free(record->validation_error);
  free(record->app_group_key);
  free(record->app_name);
  free(record->app_bundle_id);
  free(record->app_container_path);
  free(record->app_bundle_path);
  free(record->app_source);
  free(record->origin_kind);
  free(record->location_tail);
  strappy_file_scanner_record_init(record);
}

static char *strappy_file_scanner_duplicate_optional_string(const char *value)
{
  if ((value == NULL) || (value[0] == '\0')) {
    return NULL;
  }

  return strappy_string_duplicate(value);
}

int strappy_file_scanner_record_set_app_metadata(
  strappy_file_scanner_record *record,
  const char *app_group_key,
  const char *app_name,
  const char *app_bundle_id,
  const char *app_container_path,
  const char *app_bundle_path,
  const char *app_source,
  char **error_out)
{
  char *new_group_key;
  char *new_name;
  char *new_bundle_id;
  char *new_container_path;
  char *new_bundle_path;
  char *new_source;

  if (record == NULL) {
    strappy_set_error(error_out, "Scanner record is missing.");
    return 0;
  }

  new_group_key = strappy_file_scanner_duplicate_optional_string(app_group_key);
  new_name = strappy_file_scanner_duplicate_optional_string(app_name);
  new_bundle_id = strappy_file_scanner_duplicate_optional_string(app_bundle_id);
  new_container_path =
    strappy_file_scanner_duplicate_optional_string(app_container_path);
  new_bundle_path = strappy_file_scanner_duplicate_optional_string(app_bundle_path);
  new_source = strappy_file_scanner_duplicate_optional_string(app_source);

  if (((app_group_key != NULL) && (app_group_key[0] != '\0') &&
       (new_group_key == NULL)) ||
      ((app_name != NULL) && (app_name[0] != '\0') && (new_name == NULL)) ||
      ((app_bundle_id != NULL) && (app_bundle_id[0] != '\0') &&
       (new_bundle_id == NULL)) ||
      ((app_container_path != NULL) && (app_container_path[0] != '\0') &&
       (new_container_path == NULL)) ||
      ((app_bundle_path != NULL) && (app_bundle_path[0] != '\0') &&
       (new_bundle_path == NULL)) ||
      ((app_source != NULL) && (app_source[0] != '\0') &&
       (new_source == NULL))) {
    free(new_group_key);
    free(new_name);
    free(new_bundle_id);
    free(new_container_path);
    free(new_bundle_path);
    free(new_source);
    strappy_set_error(error_out, "Could not allocate scanner app metadata.");
    return 0;
  }

  free(record->app_group_key);
  free(record->app_name);
  free(record->app_bundle_id);
  free(record->app_container_path);
  free(record->app_bundle_path);
  free(record->app_source);
  record->app_group_key = new_group_key;
  record->app_name = new_name;
  record->app_bundle_id = new_bundle_id;
  record->app_container_path = new_container_path;
  record->app_bundle_path = new_bundle_path;
  record->app_source = new_source;
  record->hidden =
    strappy_file_scanner_database_should_be_hidden(record->path,
                                                   record->app_bundle_id);
  return 1;
}

void strappy_file_scanner_record_list_init(strappy_file_scanner_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
  list->scan_run_id = 0LL;
}

void strappy_file_scanner_record_list_destroy(strappy_file_scanner_record_list *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0U; index < list->count; index++) {
    strappy_file_scanner_record_destroy(&list->records[index]);
  }
  free(list->records);
  strappy_file_scanner_record_list_init(list);
}

static int strappy_file_scanner_add_record(
  strappy_file_scanner_record_list *list,
  const char *path,
  const struct stat *stat_info,
  int is_valid_sqlite,
  char *validation_error,
  char **error_out)
{
  strappy_file_scanner_record *records;
  strappy_file_scanner_record *record;
  char *origin_kind;
  char *location_tail;
  const char *origin_kind_value;

  if ((list == NULL) || (path == NULL) || (stat_info == NULL)) {
    free(validation_error);
    strappy_set_error(error_out, "Scanner record request is incomplete.");
    return 0;
  }

  if (list->count == (size_t)-1) {
    free(validation_error);
    strappy_set_error(error_out, "Too many scanner records.");
    return 0;
  }

  origin_kind_value = strappy_file_scanner_database_origin_kind(path);
  origin_kind = strappy_string_duplicate(origin_kind_value);
  location_tail =
    strappy_file_scanner_database_location_tail(path, origin_kind_value);
  if ((origin_kind == NULL) || (location_tail == NULL)) {
    free(origin_kind);
    free(location_tail);
    free(validation_error);
    strappy_set_error(error_out, "Could not allocate scanner display metadata.");
    return 0;
  }

  records = (strappy_file_scanner_record *)realloc(
    list->records,
    sizeof(strappy_file_scanner_record) * (list->count + 1U));
  if (records == NULL) {
    free(origin_kind);
    free(location_tail);
    free(validation_error);
    strappy_set_error(error_out, "Could not allocate scanner record.");
    return 0;
  }

  list->records = records;
  record = &list->records[list->count];
  strappy_file_scanner_record_init(record);

  record->path = strappy_string_duplicate(path);
  if (record->path == NULL) {
    free(origin_kind);
    free(location_tail);
    free(validation_error);
    strappy_set_error(error_out, "Could not allocate scanner path.");
    return 0;
  }

  record->size = (long long)stat_info->st_size;
  record->modified_at = (long long)stat_info->st_mtime;
  record->device = (unsigned long long)stat_info->st_dev;
  record->inode = (unsigned long long)stat_info->st_ino;
  record->is_valid_sqlite = is_valid_sqlite;
  record->validation_error = validation_error;
  record->origin_kind = origin_kind;
  record->location_tail = location_tail;
  record->hidden = strappy_file_scanner_database_should_be_hidden(path, NULL);
  list->count++;
  return 1;
}

static int strappy_file_scanner_flush_record_batch(
  const strappy_file_scanner_options *options,
  strappy_file_scanner_record_list *list,
  char **error_out)
{
  long long scan_run_id;

  if ((options == NULL) || (options->record_batch_callback == NULL) ||
      (list == NULL) || (list->count == 0U)) {
    return 1;
  }

  if (!options->record_batch_callback(list,
                                      options->record_batch_user_data,
                                      error_out)) {
    return 0;
  }

  scan_run_id = list->scan_run_id;
  strappy_file_scanner_record_list_destroy(list);
  list->scan_run_id = scan_run_id;
  return 1;
}

static int strappy_file_scanner_has_sqlite_header(const char *path)
{
  static const unsigned char sqlite_header[16] = "SQLite format 3";
  unsigned char buffer[16];
  size_t offset;
  int fd;
  ssize_t bytes_read;

  if ((path == NULL) || (path[0] == '\0')) {
    return -1;
  }

  fd = open(path, O_RDONLY);
  if (fd < 0) {
    return -1;
  }

  offset = 0U;
  while (offset < sizeof(buffer)) {
    bytes_read = read(fd, buffer + offset, sizeof(buffer) - offset);
    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }
      close(fd);
      return -1;
    }
    if (bytes_read == 0) {
      close(fd);
      return 0;
    }
    offset += (size_t)bytes_read;
  }

  close(fd);
  if (memcmp(buffer, sqlite_header, sizeof(sqlite_header)) == 0) {
    return 1;
  }

  return 0;
}

static int strappy_file_scanner_validate_sqlite(const char *path,
                                                char **validation_error_out)
{
  sqlite3 *db;
  sqlite3_stmt *stmt;
  int flags;
  int rc;
  int ok;

  if (validation_error_out != NULL) {
    *validation_error_out = NULL;
  }

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(validation_error_out, "Database path is empty.");
    return 0;
  }

  db = NULL;
  flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX;
  rc = sqlite3_open_v2(path, &db, flags, NULL);
  if (rc != SQLITE_OK) {
    if (db != NULL) {
      strappy_set_formatted_error(validation_error_out,
                                  "Could not open database read-only: %s",
                                  sqlite3_errmsg(db));
      sqlite3_close(db);
    } else {
      strappy_set_error(validation_error_out,
                        "Could not open database read-only.");
    }
    return 0;
  }

  sqlite3_busy_timeout(db, 250);
  stmt = NULL;
  rc = sqlite3_prepare_v2(db,
                          "SELECT name FROM sqlite_master LIMIT 1;",
                          -1,
                          &stmt,
                          NULL);
  if (rc != SQLITE_OK) {
    strappy_set_formatted_error(validation_error_out,
                                "Could not inspect database schema: %s",
                                sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }

  rc = sqlite3_step(stmt);
  ok = ((rc == SQLITE_ROW) || (rc == SQLITE_DONE));
  if (!ok) {
    strappy_set_formatted_error(validation_error_out,
                                "Could not read database schema: %s",
                                sqlite3_errmsg(db));
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return ok;
}

int strappy_file_scanner_scan(const strappy_file_scanner_options *options,
                              strappy_file_scanner_record_list *list,
                              char **error_out)
{
  strappy_file_scanner_metadata_context metadata_context;
  char *root_path;
  char *paths[2];
  FTS *fts;
  FTSENT *entry;
  strappy_file_scanner_progress progress;
  int fts_options;
  int fts_errno;
  int stopped;
  int cancelled;
  int failed;
  size_t records_found;

  if ((options == NULL) || (list == NULL)) {
    strappy_set_error(error_out, "Scanner request is incomplete.");
    return 0;
  }

  if ((options->root_path == NULL) || (options->root_path[0] == '\0')) {
    strappy_set_error(error_out, "Scanner root path is empty.");
    return 0;
  }

  strappy_file_scanner_metadata_context_init(&metadata_context);
  root_path = strappy_string_duplicate(options->root_path);
  if (root_path == NULL) {
    strappy_set_error(error_out, "Could not allocate scanner root path.");
    return 0;
  }

  paths[0] = root_path;
  paths[1] = NULL;
  fts_options = FTS_PHYSICAL | FTS_NOCHDIR;
  fts = fts_open(paths, fts_options, NULL);
  if (fts == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not open scanner root: %s",
                                strerror(errno));
    free(root_path);
    return 0;
  }
  if (!strappy_file_scanner_metadata_context_prepare(&metadata_context,
                                                      options->root_path,
                                                      error_out)) {
    fts_close(fts);
    free(root_path);
    strappy_file_scanner_metadata_context_destroy(&metadata_context);
    return 0;
  }

  memset(&progress, 0, sizeof(progress));
  fts_errno = 0;
  stopped = 0;
  cancelled = 0;
  failed = 0;
  records_found = 0U;

  for (;;) {
    errno = 0;
    entry = fts_read(fts);
    if (entry == NULL) {
      fts_errno = errno;
      break;
    }

    progress.current_path = entry->fts_path;

    if ((entry->fts_info == FTS_D) && (entry->fts_level >= 0)) {
      progress.directories_visited++;
      if ((options->max_depth >= 0) &&
          (entry->fts_level >= options->max_depth)) {
        fts_set(fts, entry, FTS_SKIP);
      }
    } else if ((entry->fts_info == FTS_DNR) ||
               (entry->fts_info == FTS_ERR) ||
               (entry->fts_info == FTS_NS)) {
      progress.errors_seen++;
    } else if (entry->fts_info == FTS_F) {
      int header_status;

      if ((options->max_files > 0L) &&
          (progress.files_examined >= (unsigned long long)options->max_files)) {
        stopped = 1;
        break;
      }

      progress.files_examined++;
      if ((entry->fts_statp != NULL) && (entry->fts_statp->st_size >= 16)) {
        header_status = strappy_file_scanner_has_sqlite_header(entry->fts_path);
        if (header_status < 0) {
          progress.errors_seen++;
        } else if (header_status > 0) {
          char *validation_error;
          int is_valid_sqlite;

          progress.candidates_found++;
          validation_error = NULL;
          is_valid_sqlite = 1;
          if (options->validate_candidates) {
            is_valid_sqlite =
              strappy_file_scanner_validate_sqlite(entry->fts_path,
                                                   &validation_error);
          }
          if (is_valid_sqlite) {
            progress.databases_found++;
          }

          if (!strappy_file_scanner_add_record(list,
                                               entry->fts_path,
                                               entry->fts_statp,
                                               is_valid_sqlite,
                                               validation_error,
                                               error_out)) {
            failed = 1;
            break;
          }
          if (!strappy_file_scanner_annotate_record(
                &metadata_context,
                &list->records[list->count - 1U],
                error_out)) {
            failed = 1;
            break;
          }
          records_found++;

          if ((options->max_results > 0L) &&
              (records_found >= (size_t)options->max_results)) {
            stopped = 1;
            break;
          }

          if ((options->record_batch_callback != NULL) &&
              (options->record_batch_size > 0U) &&
              (list->count >= options->record_batch_size) &&
              !strappy_file_scanner_flush_record_batch(options,
                                                       list,
                                                       error_out)) {
            failed = 1;
            break;
          }
        }
      }
    }

    if (!strappy_file_scanner_report_progress(options, &progress)) {
      cancelled = 1;
      break;
    }
  }

  fts_close(fts);
  free(root_path);
  strappy_file_scanner_metadata_context_destroy(&metadata_context);

  if (failed) {
    return 0;
  }

  if (cancelled) {
    strappy_set_error(error_out, "Scan was cancelled.");
    return 0;
  }

  if ((!stopped) && (fts_errno != 0)) {
    strappy_set_formatted_error(error_out,
                                "Filesystem scan failed: %s",
                                strerror(fts_errno));
    return 0;
  }

  if (!strappy_file_scanner_flush_record_batch(options, list, error_out)) {
    return 0;
  }

  return 1;
}

static void strappy_file_scanner_discovered_database_input_from_record(
  strappy_discovered_database_input *input,
  const strappy_file_scanner_record *record,
  const char *scan_root)
{
  input->path = record->path;
  input->size = record->size;
  input->modified_at = record->modified_at;
  input->device = record->device;
  input->inode = record->inode;
  input->is_valid_sqlite = record->is_valid_sqlite;
  input->validation_error = record->validation_error;
  input->scan_root = scan_root;
  input->app_group_key = record->app_group_key;
  input->app_name = record->app_name;
  input->app_bundle_id = record->app_bundle_id;
  input->app_container_path = record->app_container_path;
  input->app_bundle_path = record->app_bundle_path;
  input->app_source = record->app_source;
  input->origin_kind = record->origin_kind;
  input->location_tail = record->location_tail;
  input->hidden = record->hidden;
}

static int strappy_file_scanner_save_discovered_databases_with_mode(
  const char *db_path,
  const strappy_file_scanner_record_list *list,
  const char *scan_root,
  int replace_scan_root,
  char **error_out)
{
  strappy_discovered_database_input *inputs;
  size_t index;
  int ok;

  if (list == NULL) {
    strappy_set_error(error_out, "Scanner records are missing.");
    return 0;
  }
  if ((list->records == NULL) && (list->count > 0U)) {
    strappy_set_error(error_out, "Scanner record storage is missing.");
    return 0;
  }

  inputs = NULL;
  if (list->count > 0U) {
    inputs = (strappy_discovered_database_input *)calloc(
      list->count,
      sizeof(strappy_discovered_database_input));
    if (inputs == NULL) {
      strappy_set_error(error_out,
                        "Could not allocate discovered database records.");
      return 0;
    }
  }

  for (index = 0U; index < list->count; index++) {
    strappy_file_scanner_discovered_database_input_from_record(
      &inputs[index],
      &list->records[index],
      scan_root);
  }

  if (replace_scan_root) {
    ok = strappy_db_replace_discovered_databases_for_scan_root(db_path,
                                                               inputs,
                                                               list->count,
                                                               scan_root,
                                                               error_out);
  } else if (list->scan_run_id > 0LL) {
    ok = strappy_db_save_discovered_databases_for_scan_run(
      db_path,
      inputs,
      list->count,
      list->scan_run_id,
      error_out);
  } else {
    ok = strappy_db_save_discovered_databases(db_path,
                                              inputs,
                                              list->count,
                                              error_out);
  }
  free(inputs);
  return ok;
}

int strappy_file_scanner_save_discovered_databases(
  const char *db_path,
  const strappy_file_scanner_record_list *list,
  const char *scan_root,
  char **error_out)
{
  return strappy_file_scanner_save_discovered_databases_with_mode(db_path,
                                                                  list,
                                                                  scan_root,
                                                                  1,
                                                                  error_out);
}

int strappy_file_scanner_save_discovered_database_batch(
  const char *db_path,
  const strappy_file_scanner_record_list *list,
  const char *scan_root,
  char **error_out)
{
  return strappy_file_scanner_save_discovered_databases_with_mode(db_path,
                                                                  list,
                                                                  scan_root,
                                                                  0,
                                                                  error_out);
}

int strappy_file_scanner_scan_and_save_discovered_databases(
  const char *db_path,
  const strappy_file_scanner_options *options,
  strappy_file_scanner_record_list *list,
  char **error_out)
{
  const char *scan_root;
  const char *finish_state;
  const char *scan_error;
  char *finish_error;
  long long scan_run_id;
  int scan_ok;
  int finish_ok;

  if ((options == NULL) || (list == NULL)) {
    strappy_set_error(error_out, "Scanner catalog request is incomplete.");
    return 0;
  }

  if ((options->record_batch_callback == NULL) ||
      (options->record_batch_size == 0U)) {
    strappy_set_error(error_out,
                      "Scanner catalog save requires batched records.");
    return 0;
  }

  scan_root = options->root_path;
  if ((scan_root != NULL) && (scan_root[0] == '\0')) {
    scan_root = NULL;
  }

  scan_run_id = 0LL;
  if (!strappy_db_begin_discovered_database_scan(db_path,
                                                 scan_root,
                                                 &scan_run_id,
                                                 error_out)) {
    return 0;
  }

  list->scan_run_id = scan_run_id;
  scan_ok = strappy_file_scanner_scan(options, list, error_out);
  scan_error = ((error_out != NULL) && (*error_out != NULL)) ?
    *error_out : NULL;
  finish_state = scan_ok ? "completed" :
    (((scan_error != NULL) && (strcmp(scan_error, "Scan was cancelled.") == 0)) ?
      "cancelled" : "error");
  finish_error = NULL;
  finish_ok = strappy_db_finish_discovered_database_scan(
    db_path,
    scan_run_id,
    finish_state,
    scan_error,
    &finish_error);
  list->scan_run_id = 0LL;
  if (!finish_ok && scan_ok) {
    strappy_set_error(error_out,
                      (finish_error != NULL) ? finish_error :
                        "Could not finish database scan.");
  }
  free(finish_error);
  return scan_ok && finish_ok;
}
