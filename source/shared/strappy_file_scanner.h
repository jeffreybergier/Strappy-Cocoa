#ifndef STRAPPY_FILE_SCANNER_H
#define STRAPPY_FILE_SCANNER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct strappy_file_scanner_record {
  char *path;
  long long size;
  long long modified_at;
  unsigned long long device;
  unsigned long long inode;
  int is_valid_sqlite;
  char *validation_error;
  char *app_group_key;
  char *app_name;
  char *app_bundle_id;
  char *app_container_path;
  char *app_bundle_path;
  char *app_source;
  char *origin_kind;
  char *location_tail;
  int hidden;
} strappy_file_scanner_record;

typedef struct strappy_file_scanner_record_list {
  strappy_file_scanner_record *records;
  size_t count;
  long long scan_run_id;
} strappy_file_scanner_record_list;

typedef struct strappy_file_scanner_progress {
  const char *current_path;
  unsigned long long directories_visited;
  unsigned long long files_examined;
  unsigned long long candidates_found;
  unsigned long long databases_found;
  unsigned long long errors_seen;
} strappy_file_scanner_progress;

typedef int (*strappy_file_scanner_progress_callback)(
  const strappy_file_scanner_progress *progress,
  void *user_data);

typedef int (*strappy_file_scanner_record_batch_callback)(
  strappy_file_scanner_record_list *list,
  void *user_data,
  char **error_out);

typedef struct strappy_file_scanner_options {
  const char *root_path;
  int validate_candidates;
  long max_files;
  long max_results;
  int max_depth;
  strappy_file_scanner_progress_callback progress_callback;
  void *progress_user_data;
  size_t record_batch_size;
  strappy_file_scanner_record_batch_callback record_batch_callback;
  void *record_batch_user_data;
} strappy_file_scanner_options;

void strappy_file_scanner_options_init(strappy_file_scanner_options *options);
void strappy_file_scanner_record_init(strappy_file_scanner_record *record);
void strappy_file_scanner_record_destroy(strappy_file_scanner_record *record);
int strappy_file_scanner_record_set_app_metadata(
  strappy_file_scanner_record *record,
  const char *app_group_key,
  const char *app_name,
  const char *app_bundle_id,
  const char *app_container_path,
  const char *app_bundle_path,
  const char *app_source,
  char **error_out);
void strappy_file_scanner_record_list_init(strappy_file_scanner_record_list *list);
void strappy_file_scanner_record_list_destroy(strappy_file_scanner_record_list *list);

int strappy_file_scanner_scan(const strappy_file_scanner_options *options,
                              strappy_file_scanner_record_list *list,
                              char **error_out);
int strappy_file_scanner_save_discovered_databases(
  const char *db_path,
  const strappy_file_scanner_record_list *list,
  const char *scan_root,
  char **error_out);
int strappy_file_scanner_save_discovered_database_batch(
  const char *db_path,
  const strappy_file_scanner_record_list *list,
  const char *scan_root,
  char **error_out);
/* Requires record_batch_callback; owns one catalog scan run across all batches. */
int strappy_file_scanner_scan_and_save_discovered_databases(
  const char *db_path,
  const strappy_file_scanner_options *options,
  strappy_file_scanner_record_list *list,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
