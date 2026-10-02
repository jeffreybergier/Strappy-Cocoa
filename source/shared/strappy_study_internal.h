#ifndef STRAPPY_STUDY_INTERNAL_H
#define STRAPPY_STUDY_INTERNAL_H
#include "strappy_study.h"
#include <sqlite3.h>
#include <cJSON.h>

int strappy_study_collect_progress(
  const char *db_path,
  strappy_study_database_id_list *pending_list,
  strappy_study_database_status_record_list *status_list,
  size_t *studied_count_out,
  size_t *approved_count_out,
  char **error_out);
int strappy_study_copy_fresh_value_with_updated_at(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  const char *key,
  char **value_out,
  long long *updated_at_ms_out,
  char **error_out);
int strappy_study_list_append(strappy_study_database_id_list *list,
                                     const char *database_id,
                                     char **error_out);
int strappy_study_record_is_available(
  const strappy_discovered_database_record *record);
#endif
