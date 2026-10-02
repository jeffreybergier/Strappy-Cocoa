/* All data belongs to a disposable fixture supplied by the runner. */
#include "strappy_db_internal.h"
#include "strappy_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void require(int ok, char *error)
{
    if (!ok) { fprintf(stderr, "store_harness: %s\n", error ? error : "check failed"); exit(1); }
}
int main(int argc, char **argv)
{
    char *error = NULL;
    sqlite3 *writer = NULL, *reader = NULL;
    strappy_session_reader *sidebar = NULL;
    strappy_sidebar_page page;
    struct stat info;
    int status;
    pid_t child;
    static const char *const tables[] = {"models", "app_preferences", NULL};
    assert(argc == 2);
    umask(077);
    assert(!strappy_db_check_reader(argv[1], &error)); free(error); error = NULL;
    assert(stat(argv[1], &info) != 0); /* A reader never creates a missing store. */
    require(strappy_db_initialize(argv[1], &error), error);
    require(sqlite3_open(argv[1], &writer) == SQLITE_OK, NULL);
    require(sqlite3_exec(writer,
      "INSERT INTO provider_accounts(id,provider_id,display_name,created_at_ms,updated_at_ms) VALUES('test','openrouter','Fixture',1,1);"
      "INSERT INTO sessions(id,name,provider_account_id,created_at_ms,updated_at_ms) VALUES(1,'Before','test',1,1);",
      NULL, NULL, NULL) == SQLITE_OK, NULL);
    sqlite3_close(writer); writer = NULL;
    require(strappy_db_sidebar_open(argv[1], &sidebar, &error), error);
    require(strappy_db_open_reader(argv[1], &reader, &error), error);
    require(strappy_db_reader_snapshot(reader, tables, &error), error);
    assert(sqlite3_get_autocommit(reader));
    child = fork(); assert(child >= 0);
    if (child == 0) {
        sqlite3_stmt *stmt = NULL;
        /* Fresh connection in another process; never use an inherited SQLite
         * handle after fork. Keep parent readers open throughout checkpoint. */
        if (sqlite3_open_v2(argv[1], &writer, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) _exit(2);
        if (sqlite3_exec(writer, "UPDATE sessions SET name='committed by child';", NULL, NULL, NULL) != SQLITE_OK) _exit(3);
        if (sqlite3_prepare_v2(writer, "PRAGMA wal_checkpoint(TRUNCATE)", -1, &stmt, NULL) != SQLITE_OK ||
            sqlite3_step(stmt) != SQLITE_ROW || sqlite3_column_int(stmt, 0) != 0) _exit(4);
        sqlite3_finalize(stmt); sqlite3_close(writer); _exit(0);
    }
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    require(strappy_db_sidebar_read(sidebar, 0, &page, &error), error);
    assert(page.count == 1 && strcmp(page.records[0].name, "committed by child") != 0);
    strappy_db_sidebar_page_destroy(&page); strappy_db_sidebar_close(sidebar);
    require(strappy_db_sidebar_open(argv[1], &sidebar, &error), error);
    require(strappy_db_sidebar_read(sidebar, 0, &page, &error), error);
    assert(page.count == 1 && strcmp(page.records[0].name, "committed by child") == 0);
    strappy_db_sidebar_page_destroy(&page); strappy_db_sidebar_close(sidebar);
    strappy_db_release(reader);
    assert(stat(argv[1], &info) == 0 && (info.st_mode & 0777) == 0600);
    {
        char *sidecar = malloc(strlen(argv[1]) + 5);
        assert(sidecar != NULL);
        sprintf(sidecar, "%s-wal", argv[1]);
        assert(stat(sidecar, &info) == 0 && (info.st_mode & 0777) == 0600);
        sprintf(sidecar, "%s-shm", argv[1]);
        assert(stat(sidecar, &info) == 0 && (info.st_mode & 0777) == 0600);
        free(sidecar);
    }
    /* A version mismatch is rejected by readers and writers without repair. */
    assert(sqlite3_open(argv[1], &writer) == SQLITE_OK);
    assert(sqlite3_exec(writer, "PRAGMA user_version=2", NULL, NULL, NULL) == SQLITE_OK);
    assert(!strappy_db_check_reader(argv[1], &error)); free(error); error = NULL;
    assert(!strappy_db_initialize(argv[1], &error)); free(error); error = NULL;
    /* Restore the test fixture, not a product migration. */
    assert(sqlite3_exec(writer, "PRAGMA user_version=1", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(writer);
    printf("store_harness: SQLite %s; missing store, cross-process WAL, checkpoint with retained snapshots, refresh, permissions, schema rejection passed\n", sqlite3_libversion());
    return 0;
}
