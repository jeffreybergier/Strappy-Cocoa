#ifndef STRAPPY_STUDY_EXECUTION_H
#define STRAPPY_STUDY_EXECUTION_H
#include "strappy_study.h"
void strappy_study_batch_init(strappy_study_batch *batch);

void strappy_study_batch_destroy(strappy_study_batch *batch);

int strappy_study_copy_fresh_value(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  const char *key,
  char **value_out,
  char **error_out);

char *strappy_study_save_values(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  const char *description,
  const char *context,
  long long source_item_id,
  char **error_out);

char *strappy_study_status_json(const char *db_path, char **error_out);

int strappy_study_delete_database_values(const char *db_path,
                                         const char *database_id,
                                         char **error_out);

int strappy_study_reset(const char *db_path, char **error_out);

int strappy_study_progress(const char *db_path,
                           size_t *studied_count_out,
                           size_t *approved_count_out,
                           char **error_out);

int strappy_study_list_unstudied_database_ids(
  const char *db_path,
  strappy_study_database_id_list *list,
  char **error_out);

int strappy_study_next_batch(const char *db_path,
                             strappy_study_batch *batch,
                             char **error_out);

#endif
