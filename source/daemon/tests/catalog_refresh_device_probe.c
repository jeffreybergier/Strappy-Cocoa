/* Explicit live diagnostic: refreshes the sole active OpenRouter account.
 * Reads only account routing and catalog counts; never reads/logs credentials.
 * Run as the app user with STRAPPY_ALLOW_LIVE_PROVIDER_TESTS=YES.
 */
#include "strappy_ipc.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static const char *socket_path;
static char *exchange(unsigned int operation, const char *text)
{
  strappy_ipc_message message;
  char *result = NULL;
  int fd = strappy_ipc_connect(socket_path, geteuid(), 3000);
  if (fd < 0) return NULL;
  memset(&message, 0, sizeof(message));
  message.kind = STRAPPY_IPC_REQUEST;
  message.operation = STRAPPY_IPC_HELLO;
  message.request_id = 1;
  message.length = 8;
  strappy_ipc_put32(message.payload, STRAPPY_IPC_VERSION);
  strappy_ipc_put32(message.payload + 4, STRAPPY_IPC_VERSION);
  if (strappy_ipc_send(fd, &message, 3000) ||
      strappy_ipc_receive(fd, &message, 3000) || message.status) goto done;
  memset(&message, 0, sizeof(message));
  message.kind = STRAPPY_IPC_REQUEST;
  message.operation = operation;
  message.request_id = 2;
  message.length = (uint32_t)strlen(text);
  if (message.length >= sizeof(message.payload)) goto done;
  memcpy(message.payload, text, message.length);
  if (strappy_ipc_send(fd, &message, 3000) ||
      strappy_ipc_receive(fd, &message, 10000) || message.status ||
      message.kind != STRAPPY_IPC_REPLY || message.operation != operation ||
      message.request_id != 2 || message.length >= sizeof(message.payload))
    goto done;
  if (operation == STRAPPY_IPC_WORK_STATUS && message.length != 12)
    goto done;
  result = malloc(message.length + 1);
  if (result) {
    memcpy(result, message.payload, message.length);
    result[message.length] = '\0';
  }
done:
  close(fd);
  return result;
}


int main(int argc, char **argv)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *stmt = NULL;
  char *account = NULL, *reply = NULL;
  char command[512];
  const char *allow = getenv("STRAPPY_ALLOW_LIVE_PROVIDER_TESTS");
  int ok = 0, attempt;
  unsigned int operation = 0, state = 0;
  if (argc != 3 || !allow || strcmp(allow, "YES")) return 2;
  socket_path = argv[1];
  if (sqlite3_open_v2(argv[2], &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
    goto done;
  sqlite3_busy_timeout(db, 3000);
  if (sqlite3_prepare_v2(db,
      "SELECT id,responses_endpoint IS NULL FROM provider_accounts "
      "WHERE provider_id='openrouter' AND lifecycle_state='active'",
      -1, &stmt, NULL) != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW)
    goto done;
  account = strdup((const char *)sqlite3_column_text(stmt, 0));
  printf("OpenRouter endpoint uses provider default: %s\n",
    sqlite3_column_int(stmt, 1) ? "yes" : "no");
  if (!account || sqlite3_step(stmt) != SQLITE_DONE) goto done;
  sqlite3_finalize(stmt); stmt = NULL;
  snprintf(command, sizeof(command), "{\"id\":\"%s\"}", account);
  reply = exchange(STRAPPY_IPC_REFRESH_CATALOG, command);
  if (!reply) goto done;
  free(reply); reply = NULL;
  for (attempt = 0; attempt < 300; ++attempt) {
    reply = exchange(STRAPPY_IPC_WORK_STATUS, "");
    if (!reply) goto done;
    operation = strappy_ipc_get32((unsigned char *)reply + 4);
    state = strappy_ipc_get32((unsigned char *)reply + 8);
    if (strappy_ipc_get32((unsigned char *)reply) == 0) break;
    free(reply); reply = NULL;
    usleep(100000);
  }
  printf("Catalog refresh terminal operation=%u state=%u\n", operation, state);
  if (attempt == 300 || operation != STRAPPY_IPC_REFRESH_CATALOG || state != 3)
    goto done;
  if (sqlite3_prepare_v2(db,
      "SELECT count(*) FROM models WHERE provider_id='openrouter' AND catalog_active=1",
      -1, &stmt, NULL) != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW)
    goto done;
  printf("Active OpenRouter models: %d\n", sqlite3_column_int(stmt, 0));
  ok = sqlite3_column_int(stmt, 0) > 0;
done:
  free(reply); free(account);
  sqlite3_finalize(stmt);
  if (db) sqlite3_close(db);
  puts(ok ? "PASS: native live catalog refresh" : "FAIL: native live catalog refresh");
  return ok ? 0 : 1;
}
