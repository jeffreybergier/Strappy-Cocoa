/* Built with the GUI's capability flags, also run against device fixtures. */
#include "strappy_db_internal.h"
#include "strappy_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    sqlite3 *db = NULL;
    char *error = NULL;
    char *model = NULL;
    strappy_session_reader *sidebar = NULL;
    strappy_session_options options;
    strappy_session_record_list sessions;
    strappy_model_record_list models;
    strappy_discovered_database_record_list catalog;
    long long id = 0;
    size_t i;
    assert(argc == 2);
    assert(!strappy_db_writer_available(&error)); free(error); error = NULL;
    assert(!strappy_db_check_reader(argv[1], &error)); free(error); error = NULL;
    strappy_db_set_service_ready(argv[1], 1);
    assert(strappy_db_open_reader(argv[1], &db, &error));
    assert(sqlite3_db_readonly(db, "main") == 1);
    assert(sqlite3_exec(db, "UPDATE app_preferences SET updated_at_ms=0", NULL, NULL, NULL) == SQLITE_READONLY);
    strappy_db_release(db);
    assert(!strappy_db_initialize(argv[1], &error)); free(error); error = NULL;
    assert(!strappy_db_create_session(argv[1], &id, &error)); free(error); error = NULL;
    assert(!strappy_db_set_default_model(argv[1], "invalid", &error)); free(error); error = NULL;
    assert(strappy_db_get_default_model(argv[1], &model, &error)); free(model);
    strappy_session_options_init(&options);
    assert(strappy_db_load_default_session_options(argv[1], "/", &options, &error));
    strappy_session_options_destroy(&options);
    assert(strappy_db_list_sessions(argv[1], &sessions, &error));
    for (i = 0; i < sessions.count; ++i) {
        char *directory = NULL;
        strappy_session_message_record_list timeline;
        strappy_session_record record;
        strappy_session_options_init(&options);
        assert(strappy_db_load_session_options(argv[1], sessions.records[i].session_id, &options, &error));
        strappy_session_options_destroy(&options);
        assert(strappy_db_get_session_working_directory(argv[1], sessions.records[i].session_id, &directory, &error));
        assert(directory != NULL); free(directory);
        assert(strappy_db_load_session(argv[1], sessions.records[i].session_id, &record, &error));
        strappy_session_record_destroy(&record);
        assert(strappy_db_list_response_timeline(argv[1], sessions.records[i].session_id, &timeline, &error));
        strappy_session_message_record_list_destroy(&timeline);
    }
    strappy_session_record_list_destroy(&sessions);
    assert(strappy_db_open_reader(argv[1], &db, &error));
    {
        sqlite3_stmt *stmt = NULL;
        assert(sqlite3_prepare_v2(db, "SELECT count(*) FROM session_working_directories", -1, &stmt, NULL) == SQLITE_OK);
        assert(sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0);
        sqlite3_finalize(stmt);
    }
    strappy_db_release(db);
    assert(strappy_db_list_models(argv[1], &models, &error));
    strappy_model_record_list_destroy(&models);
    assert(strappy_db_list_discovered_databases(argv[1], &catalog, &error));
    strappy_discovered_database_record_list_destroy(&catalog);
    assert(strappy_db_sidebar_open(argv[1], &sidebar, &error));
    strappy_db_sidebar_close(sidebar);
    strappy_db_set_service_ready(argv[1], 0);
    assert(!strappy_db_check_reader(argv[1], &error)); free(error);
    printf("store_reader_harness: SQLite %s; read-only APIs, write denial, readiness revocation passed\n", sqlite3_libversion());
    return 0;
}
