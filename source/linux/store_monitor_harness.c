#include "strappy_store_client.h"
#include "strappy_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char *database_path;
static void changed(void)
{
    char *error = NULL;
    puts(strappy_db_check_reader(database_path, &error) ? "ready" : "unavailable");
    fflush(stdout);
    free(error);
}
int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    database_path = argv[2];
    changed();
    if (!strappy_store_client_start(argv[1], argv[2], changed)) return 1;
    while (getchar() != EOF) {}
    return 0;
}
