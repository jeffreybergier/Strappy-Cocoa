/* Offline native regression. Run only against a fresh isolated daemon store.
 * Uses synthetic credentials, archives its accounts, and makes no HTTP calls. */
#include "strappy_ipc.h"
#include <cJSON.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *socket_path;
static unsigned int next_key;

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
  result = malloc(message.length + 1);
  if (result) {
    memcpy(result, message.payload, message.length);
    result[message.length] = '\0';
  }
done:
  close(fd);
  return result;
}

static int credential(const char *account, const char *action,
                      const char *fields, int expected, int check_present)
{
  char command[2048], poll[512], key[65];
  char *reply;
  int attempt, ok = 0;
  snprintf(key, sizeof(key), "%064x", ++next_key);
  snprintf(command, sizeof(command),
    "{\"action\":\"credential_start\",\"credential_action\":\"%s\","
    "\"id\":\"%s\",\"key\":\"%s\"%s}", action, account, key, fields);
  snprintf(poll, sizeof(poll),
    "{\"action\":\"auth_status\",\"id\":\"%s\",\"key\":\"%s\"}", account, key);
  reply = exchange(STRAPPY_IPC_AUTH, command);
  for (attempt = 0; reply && attempt < 100; ++attempt) {
    cJSON *snapshot = cJSON_Parse(reply);
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(snapshot, "state");
    int completed = cJSON_IsString(state) &&
      !strcmp(state->valuestring, "completed");
    int failed = cJSON_IsString(state) && !strcmp(state->valuestring, "failed");
    free(reply);
    if (completed || failed) {
      ok = expected ? completed : failed;
      if (ok && check_present) {
        const cJSON *result = cJSON_GetObjectItemCaseSensitive(snapshot, "result");
        cJSON *status = cJSON_IsString(result) ? cJSON_Parse(result->valuestring) : NULL;
        ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(status, "present"));
        cJSON_Delete(status);
      }
      if (!ok) fprintf(stderr, "Credential action %s: state=%s, expected=%s\n",
        action, completed ? "completed" : "failed",
        expected ? "completed" : "failed");
      cJSON_Delete(snapshot);
      return ok;
    }
    cJSON_Delete(snapshot);
    usleep(100000);
    reply = exchange(STRAPPY_IPC_AUTH, poll);
  }
  free(reply);
  return 0;
}

static int saved(sqlite3 *db, const char *account, const char *name,
                 const char *endpoint, int output)
{
  sqlite3_stmt *stmt = NULL;
  int ok = sqlite3_prepare_v2(db,
    "SELECT display_name,responses_endpoint,max_output_tokens "
    "FROM provider_accounts WHERE id=?", -1, &stmt, NULL) == SQLITE_OK;
  if (ok) ok = sqlite3_bind_text(stmt, 1, account, -1, SQLITE_STATIC) == SQLITE_OK &&
    sqlite3_step(stmt) == SQLITE_ROW &&
    !strcmp((const char *)sqlite3_column_text(stmt, 0), name) &&
    (endpoint ? sqlite3_column_type(stmt, 1) == SQLITE_TEXT &&
      !strcmp((const char *)sqlite3_column_text(stmt, 1), endpoint) :
      sqlite3_column_type(stmt, 1) == SQLITE_NULL) &&
    sqlite3_column_int(stmt, 2) == output;
  sqlite3_finalize(stmt);
  return ok;
}

int main(int argc, char **argv)
{
  sqlite3 *db = NULL;
  char *router = NULL, *other = NULL;
  int ok = 0, cleaned = 1;
  const char *phase = "open database";
  if (argc != 3) return 2;
  socket_path = argv[1];
  if (sqlite3_open_v2(argv[2], &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
    goto done;
  sqlite3_busy_timeout(db, 3000);
  phase = "create OpenRouter account";
  router = exchange(STRAPPY_IPC_MUTATE,
    "{\"action\":\"create_account\",\"id\":\"openrouter\",\"name\":\"Endpoint fixture\"}");
  if (!router) goto done;
  phase = "save OpenRouter key";
  if (!credential(router, "update_account",
      ",\"name\":\"Saved\",\"endpoint\":\"\",\"output\":\"4096\","
      "\"token\":\"synthetic-offline-endpoint-fixture\"", 1, 0) ||
      !saved(db, router, "Saved", NULL, 4096) ||
      !credential(router, "credential_status", "", 1, 1)) goto done;
  phase = "preserve OpenRouter key";
  /* Omitting the token keeps the saved key, as both UI editors do. */
  if (!credential(router, "update_account",
      ",\"name\":\"Renamed\",\"endpoint\":\"\",\"output\":\"0\"", 1, 0) ||
      !saved(db, router, "Renamed", NULL, 0) ||
      !credential(router, "credential_status", "", 1, 1)) goto done;
  phase = "create Other account";
  other = exchange(STRAPPY_IPC_MUTATE,
    "{\"action\":\"create_account\",\"id\":\"other\",\"name\":\"Required endpoint\"}");
  if (!other) goto done;
  phase = "reject required empty endpoint";
  if (!credential(other, "update_account",
      ",\"name\":\"Rejected\",\"endpoint\":\"\",\"output\":\"0\"", 0, 0) ||
      !saved(db, other, "Required endpoint", NULL, 0)) goto done;
  if (!credential(other, "update_account",
      ",\"name\":\"Explicit\",\"endpoint\":\"http://127.0.0.1:12345/v1/responses\","
      "\"output\":\"1024\"", 1, 0) ||
      !saved(db, other, "Explicit", "http://127.0.0.1:12345/v1/responses", 1024))
    goto done;
  ok = 1;
done:
  if (!ok) fprintf(stderr, "Failed phase: %s\n", phase);
  if (router) cleaned &= credential(router, "archive_account", "", 1, 0);
  if (other) cleaned &= credential(other, "archive_account", "", 1, 0);
  free(router);
  free(other);
  if (db) sqlite3_close(db);
  printf("%s: native account endpoint/credential regression; fixture cleanup %s\n",
    ok ? "PASS" : "FAIL", cleaned ? "passed" : "FAILED");
  return ok && cleaned ? 0 : 1;
}
