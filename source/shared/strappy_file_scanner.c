#include "strappy_file_scanner.h"

#include "strappy_core.h"

#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
}

void strappy_file_scanner_record_destroy(strappy_file_scanner_record *record)
{
  if (record == NULL) {
    return;
  }

  free(record->path);
  free(record->validation_error);
  strappy_file_scanner_record_init(record);
}

void strappy_file_scanner_record_list_init(strappy_file_scanner_record_list *list)
{
  if (list == NULL) {
    return;
  }

  list->records = NULL;
  list->count = 0U;
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

  records = (strappy_file_scanner_record *)realloc(
    list->records,
    sizeof(strappy_file_scanner_record) * (list->count + 1U));
  if (records == NULL) {
    free(validation_error);
    strappy_set_error(error_out, "Could not allocate scanner record.");
    return 0;
  }

  list->records = records;
  record = &list->records[list->count];
  strappy_file_scanner_record_init(record);

  record->path = strappy_string_duplicate(path);
  if (record->path == NULL) {
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
  list->count++;
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

  if ((options == NULL) || (list == NULL)) {
    strappy_set_error(error_out, "Scanner request is incomplete.");
    return 0;
  }

  if ((options->root_path == NULL) || (options->root_path[0] == '\0')) {
    strappy_set_error(error_out, "Scanner root path is empty.");
    return 0;
  }

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

  memset(&progress, 0, sizeof(progress));
  fts_errno = 0;
  stopped = 0;
  cancelled = 0;
  failed = 0;

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

          if ((options->max_results > 0L) &&
              (list->count >= (size_t)options->max_results)) {
            stopped = 1;
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

  return 1;
}
