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
} strappy_file_scanner_record;

typedef struct strappy_file_scanner_record_list {
  strappy_file_scanner_record *records;
  size_t count;
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

typedef struct strappy_file_scanner_options {
  const char *root_path;
  int validate_candidates;
  long max_files;
  long max_results;
  int max_depth;
  strappy_file_scanner_progress_callback progress_callback;
  void *progress_user_data;
} strappy_file_scanner_options;

void strappy_file_scanner_options_init(strappy_file_scanner_options *options);
void strappy_file_scanner_record_init(strappy_file_scanner_record *record);
void strappy_file_scanner_record_destroy(strappy_file_scanner_record *record);
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
int strappy_file_scanner_scan_and_save_discovered_databases(
  const char *db_path,
  const strappy_file_scanner_options *options,
  strappy_file_scanner_record_list *list,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
