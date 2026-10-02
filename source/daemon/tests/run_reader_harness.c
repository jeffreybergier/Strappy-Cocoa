#include "strappy_run_execution.h"
#include "strappy_db.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    strappy_run_record run, denied;
    char *json;
    int state;
    assert(argc == 3);
    state = atoi(argv[2]);
    strappy_db_set_service_ready(argv[1], 1);
    assert(strappy_run_latest(argv[1], 1, &run));
    assert(run.id > 0 && run.state == state && run.request_key && strlen(run.request_key) == 64);
    assert(strappy_run_active_count(argv[1]) == (unsigned int)(state == 1 || state == 2));
    json = strappy_run_status_json(&run); assert(json != NULL); free(json);
    assert(strappy_run_accept(argv[1], 1, run.request_key, run.prompt, &denied) == STRAPPY_IPC_STORAGE_ERROR);
    assert(!strappy_run_update(argv[1], run.id, 4, 0, "", "", 0, 0));
    strappy_run_destroy(&denied); strappy_run_destroy(&run);
    strappy_db_set_service_ready(argv[1], 0);
    assert(!strappy_run_latest(argv[1], 1, &run));
    puts("run_reader_harness: reopened state, status, read-only enforcement and readiness passed");
    return 0;
}
