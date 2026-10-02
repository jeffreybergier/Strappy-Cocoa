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

typedef struct strappy_study_batch {
  strappy_study_database_id_list database_ids;
  strappy_study_database_id_list pending_database_ids;
  char *prompt;
} strappy_study_batch;

typedef struct strappy_study_database_status_record {
  char *database_id;
  char *path;
  char *app_group_key;
  char *app_name;
  char *app_bundle_id;
  char *description;
  char *context;
  int studied;
  long long studied_at_ms;
} strappy_study_database_status_record;

typedef struct strappy_study_database_status_record_list {
  strappy_study_database_status_record *records;
  size_t count;
} strappy_study_database_status_record_list;

void strappy_study_database_id_list_init(
  strappy_study_database_id_list *list);
void strappy_study_database_id_list_destroy(
  strappy_study_database_id_list *list);
void strappy_study_database_status_record_list_init(
  strappy_study_database_status_record_list *list);
void strappy_study_database_status_record_list_destroy(
  strappy_study_database_status_record_list *list);

int strappy_study_key_is_valid(const char *key);

int strappy_study_list_database_status_records(
  const char *db_path,
  strappy_study_database_status_record_list *list,
  char **error_out);

#ifdef __cplusplus
}
#endif

#endif
