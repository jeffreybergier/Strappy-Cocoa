#ifndef STRAPPY_STUDY_H
#define STRAPPY_STUDY_H

#include "strappy_db.h"

#include <sqlite3.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRAPPY_STUDY_KEY_DESCRIPTION "description"
#define STRAPPY_STUDY_KEY_CONTEXT "context"

typedef struct strappy_study_database_id_list {
  char **database_ids;
  size_t count;
} strappy_study_database_id_list;

void strappy_study_database_id_list_init(
  strappy_study_database_id_list *list);
void strappy_study_database_id_list_destroy(
  strappy_study_database_id_list *list);

int strappy_study_key_is_valid(const char *key);
int strappy_study_copy_fresh_value(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  const char *key,
  char **value_out,
  char **error_out);
char *strappy_study_save_value(
  sqlite3 *db,
  const strappy_discovered_database_record *record,
  const char *key,
  const char *value,
  long long source_item_id,
  char **error_out);

char *strappy_study_status_json(const char *db_path, char **error_out);
int strappy_study_reset(const char *db_path, char **error_out);
int strappy_study_list_unstudied_database_ids(
  const char *db_path,
  strappy_study_database_id_list *list,
  char **error_out);
char *strappy_study_batch_prompt(const char * const *database_ids,
                                 size_t database_id_count,
                                 char **error_out);

#ifdef __cplusplus
}
#endif

#endif
