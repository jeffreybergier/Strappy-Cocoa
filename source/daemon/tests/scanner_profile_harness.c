#define _POSIX_C_SOURCE 200809L
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include "strappy_core.h"
#include "strappy_db.h"
#include "strappy_file_scanner.h"

#include <sqlite3.h>
#include <fts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct scanner_fixture {
  char root[1600];
  char installed_bundle[1800];
} scanner_fixture;

static void scanner_remove_fixture(const char *root)
{
  char *paths[2];
  FTS *tree;
  FTSENT *entry;

  if ((root == NULL) || (root[0] == '\0')) {
    return;
  }
  paths[0] = (char *)root;
  paths[1] = NULL;
  tree = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
  if (tree == NULL) {
    return;
  }
  while ((entry = fts_read(tree)) != NULL) {
    if ((entry->fts_info == FTS_F) || (entry->fts_info == FTS_SL) ||
        (entry->fts_info == FTS_SLNONE)) {
      (void)unlink(entry->fts_path);
    } else if (entry->fts_info == FTS_DP) {
      (void)rmdir(entry->fts_path);
    }
  }
  fts_close(tree);
}

static int scanner_join(char *output,
                        size_t output_size,
                        const char *left,
                        const char *right)
{
  int written;

  written = snprintf(output, output_size, "%s/%s", left, right);
  return ((written > 0) && ((size_t)written < output_size)) ? 1 : 0;
}

static int scanner_make_directories(const char *path)
{
  char copy[2200];
  size_t index;
  size_t length;

  length = strlen(path);
  if ((length == 0U) || (length >= sizeof(copy))) {
    return 0;
  }
  memcpy(copy, path, length + 1U);
  for (index = 1U; index < length; index++) {
    if (copy[index] == '/') {
      copy[index] = '\0';
      if ((mkdir(copy, 0700) != 0) && (access(copy, F_OK) != 0)) {
        return 0;
      }
      copy[index] = '/';
    }
  }
  return ((mkdir(copy, 0700) == 0) || (access(copy, F_OK) == 0)) ? 1 : 0;
}

static int scanner_create_database(const char *path)
{
  char parent[2200];
  char *slash;
  sqlite3 *db;
  int rc;

  if (strlen(path) >= sizeof(parent)) {
    return 0;
  }
  strcpy(parent, path);
  slash = strrchr(parent, '/');
  if (slash == NULL) {
    return 0;
  }
  *slash = '\0';
  if (!scanner_make_directories(parent)) {
    return 0;
  }
  db = NULL;
  rc = sqlite3_open(path, &db);
  if (rc == SQLITE_OK) {
    rc = sqlite3_exec(db,
                      "CREATE TABLE fixture(value TEXT);",
                      NULL,
                      NULL,
                      NULL);
  }
  if (db != NULL) {
    sqlite3_close(db);
  }
  return (rc == SQLITE_OK) ? 1 : 0;
}

static int scanner_bundle_info(const char *bundle_path,
                               char **name_out,
                               char **bundle_identifier_out,
                               void *user_data,
                               char **error_out)
{
  scanner_fixture *fixture;

  (void)error_out;
  fixture = (scanner_fixture *)user_data;
  *name_out = NULL;
  *bundle_identifier_out = NULL;
  if ((fixture != NULL) &&
      (strcmp(bundle_path, fixture->installed_bundle) == 0)) {
    *name_out = strappy_string_duplicate("Example Test");
    *bundle_identifier_out = strappy_string_duplicate("com.example.Test");
    return ((*name_out != NULL) && (*bundle_identifier_out != NULL)) ? 1 : 0;
  }
  *name_out = strappy_string_duplicate("Not Installed");
  return (*name_out != NULL) ? 1 : 0;
}

static int scanner_container_info(const char *container_path,
                                  char **identifier_out,
                                  char **creator_out,
                                  char **bundle_path_out,
                                  void *user_data,
                                  char **error_out)
{
  scanner_fixture *fixture;

  (void)error_out;
  fixture = (scanner_fixture *)user_data;
  *identifier_out = NULL;
  *creator_out = NULL;
  *bundle_path_out = NULL;
  if (strstr(container_path, "/Library/Containers/com.example.Test") != NULL) {
    *identifier_out = strappy_string_duplicate("com.example.Test");
  } else if (strstr(container_path,
                    "/Library/Group Containers/TEAM.group.example") != NULL) {
    *identifier_out = strappy_string_duplicate("TEAM.group.example");
  } else {
    return 1;
  }
  *creator_out = strappy_string_duplicate("com.example.Test");
  *bundle_path_out = strappy_string_duplicate(fixture->installed_bundle);
  return ((*identifier_out != NULL) && (*creator_out != NULL) &&
          (*bundle_path_out != NULL)) ? 1 : 0;
}

static const strappy_file_scanner_record *scanner_find_record(
  const strappy_file_scanner_record_list *list,
  const char *path)
{
  size_t index;

  for (index = 0U; index < list->count; index++) {
    if ((list->records[index].path != NULL) &&
        (strcmp(list->records[index].path, path) == 0)) {
      return &list->records[index];
    }
  }
  return NULL;
}

static int scanner_expect_metadata(const strappy_file_scanner_record *record,
                                   const char *bundle_identifier,
                                   const char *source)
{
  return (record != NULL) && (record->app_bundle_id != NULL) &&
    (strcmp(record->app_bundle_id, bundle_identifier) == 0) &&
    (record->app_source != NULL) &&
    (strcmp(record->app_source, source) == 0);
}

static int scanner_run_profile_tests(void)
{
  static const char *const relative_paths[] = {
    "Library/Containers/com.example.Test/Data/Library/Application Support/main.sqlite",
    "Library/Group Containers/TEAM.group.example/group.sqlite",
    "Library/HTTPStorages/com.example.Test/httpstorages.sqlite",
    "Library/WebKit/com.example.Test/WebsiteData/ResourceLoadStatistics/observations.db",
    "Library/LocalStorage/com.example.Test/user.localstorage",
    "Library/Application Support/Example Test/.tipkit/tips.sqlite",
    "Library/Application Support/Firefox/Profile/https+++lucid.app/origin.sqlite",
    "Library/Developer/Xcode/Archives/Archived.app/archive.sqlite",
    "project/root/var/mobile/Library/Calendar/Calendar.sqlitedb",
    "Library/Application Support/Strappy/strappy.sqlite",
    "Library/Containers/com.fallback.Test/Data/fallback.sqlite",
    "Library/Group Containers/UNKNOWN.group/unowned.sqlite",
    "Pictures/Test.photoslibrary/database/Photos.sqlite",
    "Pictures/Test.photoslibrary/private/caches/photoCache.sqlite"
  };
  char paths[sizeof(relative_paths) / sizeof(relative_paths[0])][2200];
  char template_path[1800];
  const char *temp_base;
  scanner_fixture fixture;
  strappy_file_scanner_options options;
  strappy_file_scanner_record_list list;
  const strappy_file_scanner_record *record;
  char *error;
  size_t index;
  int ok;

  memset(&fixture, 0, sizeof(fixture));
  temp_base = getenv("TMPDIR");
  if ((temp_base == NULL) || (temp_base[0] == '\0')) {
    fprintf(stderr, "scanner_profile_harness requires TMPDIR.\n");
    return 0;
  }
  if (!scanner_join(template_path,
                    sizeof(template_path),
                    temp_base,
                    "strappy-scanner-profile-XXXXXX") ||
      (mkdtemp(template_path) == NULL) ||
      (strlen(template_path) >= sizeof(fixture.root))) {
    fprintf(stderr, "Could not create scanner profile fixture.\n");
    return 0;
  }
  strcpy(fixture.root, template_path);
  if (!scanner_join(fixture.installed_bundle,
                    sizeof(fixture.installed_bundle),
                    fixture.root,
                    "Applications/Example Test.app") ||
      !scanner_make_directories(fixture.installed_bundle)) {
    fprintf(stderr, "Could not create installed bundle fixture.\n");
    return 0;
  }
  for (index = 0U;
       index < (sizeof(relative_paths) / sizeof(relative_paths[0]));
       index++) {
    if (!scanner_join(paths[index],
                      sizeof(paths[index]),
                      fixture.root,
                      relative_paths[index]) ||
        !scanner_create_database(paths[index])) {
      fprintf(stderr, "Could not create scanner database fixture.\n");
      return 0;
    }
  }

  strappy_file_scanner_options_init(&options);
  options.root_path = fixture.root;
  options.platform_profile = STRAPPY_FILE_SCANNER_PLATFORM_MACOS;
  options.bundle_info_callback = scanner_bundle_info;
  options.container_info_callback = scanner_container_info;
  options.metadata_user_data = &fixture;
  strappy_file_scanner_record_list_init(&list);
  error = NULL;
  if (!strappy_file_scanner_scan(&options, &list, &error)) {
    fprintf(stderr, "Mac scanner profile failed: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }

  ok = scanner_expect_metadata(scanner_find_record(&list, paths[0]),
                               "com.example.Test",
                               "mac_container_metadata") &&
    scanner_expect_metadata(scanner_find_record(&list, paths[1]),
                            "com.example.Test",
                            "mac_group_container_metadata") &&
    scanner_expect_metadata(scanner_find_record(&list, paths[2]),
                            "com.example.Test",
                            "mac_http_storage_bundle_id") &&
    scanner_expect_metadata(scanner_find_record(&list, paths[3]),
                            "com.example.Test",
                            "mac_webkit_bundle_id");
  record = scanner_find_record(&list, paths[1]);
  ok = ok && (record != NULL) && (record->origin_kind != NULL) &&
    (strcmp(record->origin_kind, "app_library") == 0);
  record = scanner_find_record(&list, paths[2]);
  ok = ok && (record != NULL) && (record->hidden == 1) &&
    (record->hidden_reason != NULL) &&
    (strcmp(record->hidden_reason, "http_storage") == 0);
  record = scanner_find_record(&list, paths[3]);
  ok = ok && (record != NULL) && (record->hidden == 1) &&
    (record->hidden_reason != NULL) &&
    (strcmp(record->hidden_reason, "webkit_tracking") == 0);
  record = scanner_find_record(&list, paths[4]);
  ok = ok && (record != NULL) && (record->hidden == 0) &&
    (record->hidden_reason == NULL) && (record->origin_kind != NULL) &&
    (strcmp(record->origin_kind, "web_storage") == 0);
  record = scanner_find_record(&list, paths[5]);
  ok = ok && scanner_expect_metadata(record,
                                     "com.example.Test",
                                     "mac_application_support_match") &&
    (record->hidden == 1) && (record->hidden_reason != NULL) &&
    (strcmp(record->hidden_reason, "tipkit") == 0);
  record = scanner_find_record(&list, paths[6]);
  ok = ok && (record != NULL) && (record->app_group_key == NULL);
  record = scanner_find_record(&list, paths[7]);
  ok = ok && (record != NULL) && (record->app_group_key == NULL);
  record = scanner_find_record(&list, paths[8]);
  ok = ok && (record != NULL) && (record->app_group_key == NULL);
  record = scanner_find_record(&list, paths[9]);
  ok = ok && scanner_expect_metadata(record,
                                     "com.altivecintelligence.Strappy",
                                     "mac_application_support_match") &&
    (record->hidden == 1) && (record->hidden_reason != NULL) &&
    (strcmp(record->hidden_reason, "strappy_catalog") == 0);
  ok = ok && scanner_expect_metadata(scanner_find_record(&list, paths[10]),
                                     "com.fallback.Test",
                                     "mac_container_metadata");
  record = scanner_find_record(&list, paths[11]);
  ok = ok && (record != NULL) && (record->app_group_key == NULL);
  record = scanner_find_record(&list, paths[12]);
  ok = ok && scanner_expect_metadata(record,
                                     "com.apple.Photos",
                                     "mac_photos_library") &&
    (record->origin_kind != NULL) &&
    (strcmp(record->origin_kind, "media") == 0) &&
    (record->hidden == 0);
  record = scanner_find_record(&list, paths[13]);
  ok = ok && scanner_expect_metadata(record,
                                     "com.apple.Photos",
                                     "mac_photos_library") &&
    (record->origin_kind != NULL) &&
    (strcmp(record->origin_kind, "cache") == 0) &&
    (record->hidden == 1) && (record->hidden_reason != NULL) &&
    (strcmp(record->hidden_reason, "cache_filename") == 0);
  strappy_file_scanner_record_list_destroy(&list);

  strappy_file_scanner_options_init(&options);
  options.root_path = fixture.root;
  options.platform_profile = STRAPPY_FILE_SCANNER_PLATFORM_IOS;
  strappy_file_scanner_record_list_init(&list);
  if (!strappy_file_scanner_scan(&options, &list, &error)) {
    fprintf(stderr, "iOS scanner profile failed: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }
  record = scanner_find_record(&list, paths[8]);
  ok = ok && (record != NULL) && (record->app_name != NULL) &&
    (strcmp(record->app_name, "Calendar") == 0);
  record = scanner_find_record(&list, paths[4]);
  ok = ok && (record != NULL) && (record->hidden == 1) &&
    (record->hidden_reason != NULL) &&
    (strcmp(record->hidden_reason, "local_storage") == 0);
  record = scanner_find_record(&list, paths[12]);
  ok = ok && (record != NULL) && (record->app_group_key == NULL);
  strappy_file_scanner_record_list_destroy(&list);

  if (!ok) {
    fprintf(stderr, "Scanner platform profile results did not match.\n");
  }
  scanner_remove_fixture(fixture.root);
  return ok;
}

/* Scanner catalog entry points are not exercised by this focused harness. */
int strappy_db_begin_discovered_database_scan(const char *a, const char *b,
  long long *c, char **d) { (void)a; (void)b; (void)c; (void)d; return 0; }
int strappy_db_begin_incremental_discovered_database_scan(const char *a,
  const char *b, long long *c, char **d)
  { (void)a; (void)b; (void)c; (void)d; return 0; }
int strappy_db_finish_discovered_database_scan(const char *a, long long b,
  const char *c, const char *d, char **e)
  { (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
int strappy_db_finish_incremental_discovered_database_scan(const char *a,
  long long b, const char *c, const char *d, char **e)
  { (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
int strappy_db_list_discovered_databases(const char *a,
  strappy_discovered_database_record_list *b, char **c)
  { (void)a; (void)b; (void)c; return 0; }
int strappy_db_replace_discovered_databases_for_scan_root(const char *a,
  const strappy_discovered_database_input *b, size_t c, const char *d, char **e)
  { (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
int strappy_db_save_discovered_databases(const char *a,
  const strappy_discovered_database_input *b, size_t c, char **d)
  { (void)a; (void)b; (void)c; (void)d; return 0; }
int strappy_db_save_discovered_databases_for_scan_run(const char *a,
  const strappy_discovered_database_input *b, size_t c, long long d, char **e)
  { (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
void strappy_discovered_database_record_list_init(
  strappy_discovered_database_record_list *list)
  { if (list != NULL) { list->records = NULL; list->count = 0U; } }
void strappy_discovered_database_record_list_destroy(
  strappy_discovered_database_record_list *list)
  { if (list != NULL) { free(list->records); list->records = NULL;
                        list->count = 0U; } }

static int scanner_run_read_only_audit(const char *root_path)
{
  strappy_file_scanner_options options;
  strappy_file_scanner_record_list list;
  char *error;
  size_t assigned;
  size_t hidden;
  size_t mac_containers;
  size_t assigned_mac_containers;
  size_t mac_group_containers;
  size_t assigned_mac_group_containers;
  size_t embedded_ios_assignments;
  size_t archive_assignments;
  size_t web_origin_assignments;
  size_t web_origin_misattributions;
  size_t index;

  strappy_file_scanner_options_init(&options);
  options.root_path = root_path;
  options.platform_profile = STRAPPY_FILE_SCANNER_PLATFORM_MACOS;
  options.validate_candidates = 0;
  options.use_filename_filter = 1;
  strappy_file_scanner_record_list_init(&list);
  error = NULL;
  if (!strappy_file_scanner_scan(&options, &list, &error)) {
    fprintf(stderr, "Read-only scanner audit failed: %s\n",
            (error != NULL) ? error : "unknown");
    free(error);
    return 0;
  }
  assigned = 0U;
  hidden = 0U;
  mac_containers = 0U;
  assigned_mac_containers = 0U;
  mac_group_containers = 0U;
  assigned_mac_group_containers = 0U;
  embedded_ios_assignments = 0U;
  archive_assignments = 0U;
  web_origin_assignments = 0U;
  web_origin_misattributions = 0U;
  for (index = 0U; index < list.count; index++) {
    const strappy_file_scanner_record *record;
    int has_assignment;

    record = &list.records[index];
    has_assignment = ((record->app_group_key != NULL) &&
                      (record->app_group_key[0] != '\0')) ? 1 : 0;
    assigned += has_assignment ? 1U : 0U;
    hidden += record->hidden ? 1U : 0U;
    if (strstr(record->path, "/Library/Containers/") != NULL) {
      mac_containers++;
      assigned_mac_containers += has_assignment ? 1U : 0U;
    }
    if (strstr(record->path, "/Library/Group Containers/") != NULL) {
      mac_group_containers++;
      assigned_mac_group_containers += has_assignment ? 1U : 0U;
    }
    if (has_assignment && (strstr(record->path, "/var/mobile/") != NULL)) {
      embedded_ios_assignments++;
    }
    if (has_assignment && (strstr(record->path, "/Xcode/Archives/") != NULL)) {
      archive_assignments++;
    }
    if (has_assignment &&
        (strstr(record->path, "https+++lucid.app/") != NULL)) {
      web_origin_assignments++;
      if ((record->app_name == NULL) ||
          (strcmp(record->app_name, "Firefox") != 0)) {
        web_origin_misattributions++;
      }
    }
  }
  printf("records=%lu\n", (unsigned long)list.count);
  printf("assigned=%lu\n", (unsigned long)assigned);
  printf("hidden=%lu\n", (unsigned long)hidden);
  printf("mac_containers=%lu\n", (unsigned long)mac_containers);
  printf("assigned_mac_containers=%lu\n",
         (unsigned long)assigned_mac_containers);
  printf("mac_group_containers=%lu\n",
         (unsigned long)mac_group_containers);
  printf("assigned_mac_group_containers=%lu\n",
         (unsigned long)assigned_mac_group_containers);
  printf("embedded_ios_assignments=%lu\n",
         (unsigned long)embedded_ios_assignments);
  printf("archive_assignments=%lu\n", (unsigned long)archive_assignments);
  printf("web_origin_assignments=%lu\n",
         (unsigned long)web_origin_assignments);
  printf("web_origin_misattributions=%lu\n",
         (unsigned long)web_origin_misattributions);
  strappy_file_scanner_record_list_destroy(&list);
  return 1;
}

int main(int argc, char **argv)
{
  if (argc == 2) {
    return scanner_run_read_only_audit(argv[1]) ? 0 : 1;
  }
  if (!scanner_run_profile_tests()) {
    return 1;
  }
  printf("scanner_profile_harness passed.\n");
  return 0;
}
