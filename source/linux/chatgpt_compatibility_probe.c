#define _POSIX_C_SOURCE 200809L

#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "../shared/strappy_client.h"
#include "../shared/strappy_core.h"
#include "../shared/strappy_openai_oauth.h"
#include "../shared/strappy_provider.h"

#define PROBE_DEFAULT_MODEL "gpt-5.6-luna"
#define PROBE_DEFAULT_CREDENTIAL_PATH ".env.chatgpt.json"
#define PROBE_CREDENTIAL_PATH_ENV "STRAPPY_CHATGPT_PROBE_CREDENTIAL_PATH"
#define PROBE_REAUTHORIZE_ENV "STRAPPY_CHATGPT_PROBE_REAUTHORIZE"
#define PROBE_MAX_MODEL_BYTES 128U
#define PROBE_MAX_OUTPUT_BYTES (1024U * 1024U)
#define PROBE_MAX_TOKEN_BYTES (512U * 1024U)
#define PROBE_MAX_ACCOUNT_ID_BYTES 4096U
#define PROBE_MAX_CREDENTIAL_FILE_BYTES (2U * 1024U * 1024U)
#define PROBE_REFRESH_LEEWAY_MILLISECONDS (5LL * 60LL * 1000LL)
#define PROBE_MAX_EXACT_JSON_INTEGER 9007199254740991LL

#define PROBE_TEXT_REQUEST_TEMPLATE \
  "{" \
  "\"model\":\"gpt-5.6-luna\"," \
  "\"store\":false," \
  "\"stream\":true," \
  "\"prompt_cache_key\":\"strappy-compatibility-probe\"," \
  "\"instructions\":\"Follow the user's exact response-format request.\"," \
  "\"input\":[{" \
    "\"type\":\"message\",\"role\":\"user\",\"content\":[{" \
      "\"type\":\"input_text\"," \
      "\"text\":\"Reply with exactly: Strappy compatibility probe passed.\"" \
    "}]" \
  "}]," \
  "\"text\":{\"verbosity\":\"low\"}," \
  "\"include\":[\"reasoning.encrypted_content\"]," \
  "\"tool_choice\":\"none\"," \
  "\"parallel_tool_calls\":true," \
  "\"reasoning\":{\"effort\":\"low\",\"summary\":\"auto\"}" \
  "}"

#define PROBE_TOOL_REQUEST_TEMPLATE \
  "{" \
  "\"model\":\"gpt-5.6-luna\"," \
  "\"store\":false," \
  "\"stream\":true," \
  "\"prompt_cache_key\":\"strappy-compatibility-probe\"," \
  "\"instructions\":\"Use the requested function exactly once, then report " \
    "its result after the function output arrives.\"," \
  "\"input\":[{" \
    "\"type\":\"message\",\"role\":\"user\",\"content\":[{" \
      "\"type\":\"input_text\"," \
      "\"text\":\"Call strappy_probe_add with left 19 and right 23.\"" \
    "}]" \
  "}]," \
  "\"text\":{\"verbosity\":\"low\"}," \
  "\"include\":[\"reasoning.encrypted_content\"]," \
  "\"tool_choice\":\"required\"," \
  "\"parallel_tool_calls\":false," \
  "\"reasoning\":{\"effort\":\"low\",\"summary\":\"auto\"}," \
  "\"tools\":[{" \
    "\"type\":\"function\"," \
    "\"name\":\"strappy_probe_add\"," \
    "\"description\":\"Add two integers.\"," \
    "\"strict\":null," \
    "\"parameters\":{" \
      "\"type\":\"object\"," \
      "\"properties\":{" \
        "\"left\":{\"type\":\"integer\"}," \
        "\"right\":{\"type\":\"integer\"}" \
      "}," \
      "\"required\":[\"left\",\"right\"]," \
      "\"additionalProperties\":false" \
    "}" \
  "}]" \
  "}"

#define PROBE_WEB_SEARCH_REQUEST_TEMPLATE \
  "{" \
  "\"model\":\"gpt-5.6-luna\"," \
  "\"store\":false," \
  "\"stream\":true," \
  "\"prompt_cache_key\":\"strappy-web-search-live-probe\"," \
  "\"instructions\":\"You are a concise news assistant. You must use " \
    "web search and cite the sources used in the answer.\"," \
  "\"input\":[{" \
    "\"type\":\"message\",\"role\":\"user\",\"content\":[{" \
      "\"type\":\"input_text\"," \
      "\"text\":\"What's the top news today in Tokyo? Please search the " \
        "web in Japanese to find authentic articles.\"" \
    "}]" \
  "}]," \
  "\"text\":{\"verbosity\":\"low\"}," \
  "\"include\":[\"reasoning.encrypted_content\"," \
    "\"web_search_call.action.sources\"]," \
  "\"tool_choice\":\"auto\"," \
  "\"parallel_tool_calls\":true," \
  "\"reasoning\":{\"effort\":\"low\",\"summary\":\"auto\"}," \
  "\"tools\":[{\"type\":\"web_search\"}]" \
  "}"

static volatile sig_atomic_t probe_cancelled = 0;

typedef enum probe_credential_cache_status {
  PROBE_CREDENTIAL_CACHE_ERROR = -1,
  PROBE_CREDENTIAL_CACHE_MISSING = 0,
  PROBE_CREDENTIAL_CACHE_LOADED = 1
} probe_credential_cache_status;

static void probe_signal_handler(int signal_number)
{
  (void)signal_number;
  probe_cancelled = 1;
}

static int probe_oauth_cancelled(void *user_data)
{
  (void)user_data;
  return probe_cancelled ? 1 : 0;
}

static int probe_responses_callback(const strappy_responses_event *event,
                                    void *user_data)
{
  (void)event;
  (void)user_data;
  return probe_cancelled ? 0 : 1;
}

static void probe_secure_wipe(void *memory, size_t length)
{
  volatile unsigned char *bytes;

  if (memory == NULL) {
    return;
  }
  bytes = (volatile unsigned char *)memory;
  while (length > 0U) {
    *bytes = 0U;
    bytes++;
    length--;
  }
}

static void probe_wipe_json_strings(cJSON *value)
{
  cJSON *child;

  if (value == NULL) {
    return;
  }
  if (cJSON_IsString(value) && (value->valuestring != NULL)) {
    probe_secure_wipe(value->valuestring, strlen(value->valuestring));
  }
  for (child = value->child; child != NULL; child = child->next) {
    probe_wipe_json_strings(child);
  }
}

static char *probe_duplicate_bounded_secret(const char *value, size_t limit)
{
  size_t length;

  if (value == NULL) {
    return NULL;
  }
  length = strlen(value);
  if ((length == 0U) || (length > limit) ||
      !strappy_utf8_validate(value, length) ||
      (strchr(value, '\r') != NULL) || (strchr(value, '\n') != NULL)) {
    return NULL;
  }
  return strappy_string_duplicate_length(value, length);
}

static int probe_now_milliseconds(long long *value_out)
{
  struct timeval now;
  unsigned long long seconds;

  if ((value_out == NULL) || (gettimeofday(&now, NULL) != 0) ||
      (now.tv_sec < 0) || (now.tv_usec < 0) ||
      (now.tv_usec >= 1000000)) {
    return 0;
  }
  seconds = (unsigned long long)now.tv_sec;
  if (seconds > ((unsigned long long)LLONG_MAX / 1000ULL)) {
    return 0;
  }
  *value_out = ((long long)(seconds * 1000ULL)) +
    ((long long)now.tv_usec / 1000LL);
  return 1;
}

static int probe_credentials_are_valid(
  const strappy_openai_oauth_credentials *credentials)
{
  size_t access_length;
  size_t refresh_length;
  size_t account_length;

  if ((credentials == NULL) || (credentials->access_token == NULL) ||
      (credentials->refresh_token == NULL) ||
      (credentials->account_id == NULL) ||
      (credentials->expires_at_milliseconds <= 0LL) ||
      (credentials->expires_at_milliseconds >
       PROBE_MAX_EXACT_JSON_INTEGER)) {
    return 0;
  }
  access_length = strlen(credentials->access_token);
  refresh_length = strlen(credentials->refresh_token);
  account_length = strlen(credentials->account_id);
  return (access_length > 0U) &&
    (access_length <= PROBE_MAX_TOKEN_BYTES) &&
    (refresh_length > 0U) &&
    (refresh_length <= PROBE_MAX_TOKEN_BYTES) &&
    (account_length > 0U) &&
    (account_length <= PROBE_MAX_ACCOUNT_ID_BYTES) &&
    (strchr(credentials->access_token, '\r') == NULL) &&
    (strchr(credentials->access_token, '\n') == NULL) &&
    (strchr(credentials->refresh_token, '\r') == NULL) &&
    (strchr(credentials->refresh_token, '\n') == NULL) &&
    (strchr(credentials->account_id, '\r') == NULL) &&
    (strchr(credentials->account_id, '\n') == NULL);
}

static int probe_cache_object_has_exact_schema(cJSON *root)
{
  cJSON *child;
  int format_count;
  int access_count;
  int refresh_count;
  int account_count;
  int expiry_count;

  if (!cJSON_IsObject(root)) {
    return 0;
  }
  format_count = 0;
  access_count = 0;
  refresh_count = 0;
  account_count = 0;
  expiry_count = 0;
  for (child = root->child; child != NULL; child = child->next) {
    if (child->string == NULL) {
      return 0;
    }
    if (strcmp(child->string, "format_version") == 0) {
      format_count++;
    } else if (strcmp(child->string, "access_token") == 0) {
      access_count++;
    } else if (strcmp(child->string, "refresh_token") == 0) {
      refresh_count++;
    } else if (strcmp(child->string, "account_id") == 0) {
      account_count++;
    } else if (strcmp(child->string, "expires_at_ms") == 0) {
      expiry_count++;
    } else {
      return 0;
    }
  }
  return (format_count == 1) && (access_count == 1) &&
    (refresh_count == 1) && (account_count == 1) &&
    (expiry_count == 1);
}

static int probe_read_exact(int fd,
                            unsigned char *bytes,
                            size_t length,
                            char **error_out)
{
  size_t offset;

  offset = 0U;
  while (offset < length) {
    ssize_t count;

    count = read(fd, bytes + offset, length - offset);
    if (count > 0) {
      offset += (size_t)count;
    } else if ((count < 0) && (errno == EINTR)) {
      continue;
    } else if (count == 0) {
      strappy_set_error(error_out,
                        "The credential cache changed while being read.");
      return 0;
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not read the credential cache: %s.",
                                  strerror(errno));
      return 0;
    }
  }
  return 1;
}

static probe_credential_cache_status probe_load_credential_cache(
  const char *path,
  strappy_openai_oauth_credentials *credentials,
  char **error_out)
{
  struct stat metadata;
  unsigned char *bytes;
  unsigned char extra;
  size_t length;
  cJSON *root;
  cJSON *format;
  cJSON *access;
  cJSON *refresh;
  cJSON *account;
  cJSON *expiry;
  long long expires_at;
  ssize_t extra_count;
  int fd;
  int ok;

  if ((path == NULL) || (path[0] == '\0') || (credentials == NULL)) {
    strappy_set_error(error_out, "The credential-cache path is invalid.");
    return PROBE_CREDENTIAL_CACHE_ERROR;
  }
  fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) {
    if (errno == ENOENT) {
      return PROBE_CREDENTIAL_CACHE_MISSING;
    }
    strappy_set_formatted_error(error_out,
                                "Could not open the credential cache: %s.",
                                strerror(errno));
    return PROBE_CREDENTIAL_CACHE_ERROR;
  }
  if ((fstat(fd, &metadata) != 0) || !S_ISREG(metadata.st_mode) ||
      (metadata.st_uid != geteuid()) || (metadata.st_nlink != 1) ||
      ((metadata.st_mode & (S_IRWXG | S_IRWXO)) != 0) ||
      ((metadata.st_mode & S_IRUSR) == 0) || (metadata.st_size <= 0) ||
      (metadata.st_size > (off_t)PROBE_MAX_CREDENTIAL_FILE_BYTES)) {
    (void)close(fd);
    strappy_set_error(
      error_out,
      "The credential cache is not a bounded owner-only regular file.");
    return PROBE_CREDENTIAL_CACHE_ERROR;
  }

  length = (size_t)metadata.st_size;
  bytes = (unsigned char *)malloc(length + 1U);
  if (bytes == NULL) {
    (void)close(fd);
    strappy_set_error(error_out,
                      "Could not allocate the credential-cache buffer.");
    return PROBE_CREDENTIAL_CACHE_ERROR;
  }
  ok = probe_read_exact(fd, bytes, length, error_out);
  extra_count = ok ? read(fd, &extra, 1U) : 0;
  while (ok && (extra_count < 0) && (errno == EINTR)) {
    extra_count = read(fd, &extra, 1U);
  }
  if (ok && (extra_count != 0)) {
    strappy_set_error(error_out,
                      "The credential cache changed while being read.");
    ok = 0;
  }
  if (close(fd) != 0) {
    if (ok) {
      strappy_set_formatted_error(error_out,
                                  "Could not close the credential cache: %s.",
                                  strerror(errno));
    }
    ok = 0;
  }
  if (!ok) {
    probe_secure_wipe(bytes, length + 1U);
    free(bytes);
    return PROBE_CREDENTIAL_CACHE_ERROR;
  }

  bytes[length] = '\0';
  root = cJSON_ParseWithLengthOpts((const char *)bytes,
                                   length + 1U,
                                   NULL,
                                   1);
  probe_secure_wipe(bytes, length + 1U);
  free(bytes);
  format = cJSON_GetObjectItemCaseSensitive(root, "format_version");
  access = cJSON_GetObjectItemCaseSensitive(root, "access_token");
  refresh = cJSON_GetObjectItemCaseSensitive(root, "refresh_token");
  account = cJSON_GetObjectItemCaseSensitive(root, "account_id");
  expiry = cJSON_GetObjectItemCaseSensitive(root, "expires_at_ms");
  expires_at = 0LL;
  if (cJSON_IsNumber(expiry) && (expiry->valuedouble > 0.0) &&
      (expiry->valuedouble <= (double)PROBE_MAX_EXACT_JSON_INTEGER)) {
    expires_at = (long long)expiry->valuedouble;
    if ((double)expires_at != expiry->valuedouble) {
      expires_at = 0LL;
    }
  }
  ok = probe_cache_object_has_exact_schema(root) &&
    cJSON_IsNumber(format) &&
    (format->valuedouble ==
     (double)STRAPPY_OPENAI_OAUTH_CREDENTIAL_FORMAT_VERSION) &&
    cJSON_IsString(access) && (access->valuestring != NULL) &&
    cJSON_IsString(refresh) && (refresh->valuestring != NULL) &&
    cJSON_IsString(account) && (account->valuestring != NULL) &&
    (expires_at > 0LL);
  if (ok) {
    credentials->access_token = probe_duplicate_bounded_secret(
      access->valuestring,
      PROBE_MAX_TOKEN_BYTES);
    credentials->refresh_token = probe_duplicate_bounded_secret(
      refresh->valuestring,
      PROBE_MAX_TOKEN_BYTES);
    credentials->account_id = probe_duplicate_bounded_secret(
      account->valuestring,
      PROBE_MAX_ACCOUNT_ID_BYTES);
    credentials->expires_at_milliseconds = expires_at;
    ok = probe_credentials_are_valid(credentials);
  }
  probe_wipe_json_strings(root);
  cJSON_Delete(root);
  if (!ok) {
    strappy_openai_oauth_credentials_destroy(credentials);
    strappy_set_error(error_out,
                      "The credential cache has an invalid schema or value.");
    return PROBE_CREDENTIAL_CACHE_ERROR;
  }
  return PROBE_CREDENTIAL_CACHE_LOADED;
}

static int probe_write_all(int fd,
                           const unsigned char *bytes,
                           size_t length,
                           char **error_out)
{
  size_t offset;

  offset = 0U;
  while (offset < length) {
    ssize_t count;

    count = write(fd, bytes + offset, length - offset);
    if (count > 0) {
      offset += (size_t)count;
    } else if ((count < 0) && (errno == EINTR)) {
      continue;
    } else {
      strappy_set_formatted_error(error_out,
                                  "Could not write the credential cache: %s.",
                                  (count == 0) ? "short write" :
                                    strerror(errno));
      return 0;
    }
  }
  return 1;
}

static char *probe_parent_directory(const char *path)
{
  const char *slash;

  slash = strrchr(path, '/');
  if (slash == NULL) {
    return strappy_string_duplicate(".");
  }
  if (slash == path) {
    return strappy_string_duplicate("/");
  }
  return strappy_string_duplicate_length(path, (size_t)(slash - path));
}

static int probe_sync_parent_directory(const char *path, char **error_out)
{
  struct stat metadata;
  char *directory;
  int fd;
  int ok;

  directory = probe_parent_directory(path);
  if (directory == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate the credential-cache directory.");
    return 0;
  }
  fd = open(directory, O_RDONLY | O_CLOEXEC);
  free(directory);
  if (fd < 0) {
    strappy_set_formatted_error(
      error_out,
      "Could not open the credential-cache directory: %s.",
      strerror(errno));
    return 0;
  }
  ok = (fstat(fd, &metadata) == 0) && S_ISDIR(metadata.st_mode) &&
    (fsync(fd) == 0);
  if (!ok) {
    strappy_set_formatted_error(
      error_out,
      "Could not sync the credential-cache directory: %s.",
      strerror(errno));
  }
  if ((close(fd) != 0) && ok) {
    strappy_set_formatted_error(
      error_out,
      "Could not close the credential-cache directory: %s.",
      strerror(errno));
    ok = 0;
  }
  return ok;
}

static int probe_save_credential_cache(
  const char *path,
  const strappy_openai_oauth_credentials *credentials,
  char **error_out)
{
  static const char temporary_suffix[] = ".tmp.XXXXXX";
  cJSON *root;
  char *serialized;
  char *temporary_path;
  size_t path_length;
  size_t serialized_length;
  int fd;
  int ok;
  int renamed;

  if ((path == NULL) || (path[0] == '\0') ||
      !probe_credentials_are_valid(credentials)) {
    strappy_set_error(error_out,
                      "The credentials cannot be written to the cache.");
    return 0;
  }
  root = cJSON_CreateObject();
  ok = (root != NULL) &&
    (cJSON_AddNumberToObject(
       root,
       "format_version",
       (double)STRAPPY_OPENAI_OAUTH_CREDENTIAL_FORMAT_VERSION) != NULL) &&
    (cJSON_AddStringToObject(root,
                             "access_token",
                             credentials->access_token) != NULL) &&
    (cJSON_AddStringToObject(root,
                             "refresh_token",
                             credentials->refresh_token) != NULL) &&
    (cJSON_AddStringToObject(root,
                             "account_id",
                             credentials->account_id) != NULL) &&
    (cJSON_AddNumberToObject(root,
                             "expires_at_ms",
                             (double)credentials->expires_at_milliseconds) !=
     NULL);
  serialized = ok ? cJSON_PrintUnformatted(root) : NULL;
  serialized_length = (serialized != NULL) ? strlen(serialized) : 0U;
  if (!ok || (serialized == NULL) || (serialized_length == 0U) ||
      (serialized_length > PROBE_MAX_CREDENTIAL_FILE_BYTES)) {
    probe_wipe_json_strings(root);
    cJSON_Delete(root);
    if (serialized != NULL) {
      probe_secure_wipe(serialized, serialized_length);
      free(serialized);
    }
    strappy_set_error(error_out,
                      "Could not serialize the bounded credential cache.");
    return 0;
  }

  path_length = strlen(path);
  if (path_length > ((size_t)-1) - sizeof(temporary_suffix)) {
    probe_wipe_json_strings(root);
    cJSON_Delete(root);
    probe_secure_wipe(serialized, serialized_length);
    free(serialized);
    strappy_set_error(error_out, "The credential-cache path is too long.");
    return 0;
  }
  temporary_path = (char *)malloc(path_length + sizeof(temporary_suffix));
  if (temporary_path == NULL) {
    probe_wipe_json_strings(root);
    cJSON_Delete(root);
    probe_secure_wipe(serialized, serialized_length);
    free(serialized);
    strappy_set_error(error_out,
                      "Could not allocate the temporary credential path.");
    return 0;
  }
  memcpy(temporary_path, path, path_length);
  memcpy(temporary_path + path_length,
         temporary_suffix,
         sizeof(temporary_suffix));

  fd = mkstemp(temporary_path);
  renamed = 0;
  ok = (fd >= 0);
  if (!ok) {
    strappy_set_formatted_error(
      error_out,
      "Could not create the temporary credential cache: %s.",
      strerror(errno));
  }
  if (ok && (fchmod(fd, S_IRUSR | S_IWUSR) != 0)) {
    strappy_set_formatted_error(error_out,
                                "Could not secure the credential cache: %s.",
                                strerror(errno));
    ok = 0;
  }
  if (ok) {
    ok = probe_write_all(fd,
                         (const unsigned char *)serialized,
                         serialized_length,
                         error_out);
  }
  if (ok && (fsync(fd) != 0)) {
    strappy_set_formatted_error(error_out,
                                "Could not sync the credential cache: %s.",
                                strerror(errno));
    ok = 0;
  }
  if ((fd >= 0) && (close(fd) != 0) && ok) {
    strappy_set_formatted_error(error_out,
                                "Could not close the credential cache: %s.",
                                strerror(errno));
    ok = 0;
  }
  if (ok && (rename(temporary_path, path) != 0)) {
    strappy_set_formatted_error(error_out,
                                "Could not replace the credential cache: %s.",
                                strerror(errno));
    ok = 0;
  } else if (ok) {
    renamed = 1;
  }
  if (ok) {
    ok = probe_sync_parent_directory(path, error_out);
  }
  if (!renamed) {
    (void)unlink(temporary_path);
  }

  probe_wipe_json_strings(root);
  cJSON_Delete(root);
  probe_secure_wipe(serialized, serialized_length);
  free(serialized);
  free(temporary_path);
  return ok;
}

static int probe_credential_cache_self_test(void)
{
  char path[] = "/tmp/strappy-chatgpt-credentials-XXXXXX";
  struct stat metadata;
  strappy_openai_oauth_credentials credentials;
  strappy_openai_oauth_credentials loaded;
  probe_credential_cache_status status;
  char *error;
  int fd;
  int ok;

  strappy_openai_oauth_credentials_init(&credentials);
  strappy_openai_oauth_credentials_init(&loaded);
  error = NULL;
  fd = mkstemp(path);
  ok = (fd >= 0);
  if (ok) {
    ok = (close(fd) == 0) && (unlink(path) == 0);
  }
  if (ok) {
    status = probe_load_credential_cache(path, &loaded, &error);
    ok = (status == PROBE_CREDENTIAL_CACHE_MISSING) && (error == NULL);
  }
  if (ok) {
    credentials.access_token = strappy_string_duplicate("access-one");
    credentials.refresh_token = strappy_string_duplicate("refresh-one");
    credentials.account_id = strappy_string_duplicate("account-one");
    credentials.expires_at_milliseconds = 2000000000000LL;
    ok = probe_credentials_are_valid(&credentials) &&
      probe_save_credential_cache(path, &credentials, &error);
  }
  if (ok) {
    ok = (stat(path, &metadata) == 0) && S_ISREG(metadata.st_mode) &&
      ((metadata.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) ==
       (S_IRUSR | S_IWUSR));
  }
  if (ok) {
    status = probe_load_credential_cache(path, &loaded, &error);
    ok = (status == PROBE_CREDENTIAL_CACHE_LOADED) &&
      (strcmp(loaded.access_token, "access-one") == 0) &&
      (strcmp(loaded.refresh_token, "refresh-one") == 0) &&
      (strcmp(loaded.account_id, "account-one") == 0) &&
      (loaded.expires_at_milliseconds == 2000000000000LL);
  }
  strappy_openai_oauth_credentials_destroy(&loaded);
  strappy_openai_oauth_credentials_destroy(&credentials);
  strappy_free_string(error);
  error = NULL;

  if (ok) {
    credentials.access_token = strappy_string_duplicate("access-two");
    credentials.refresh_token = strappy_string_duplicate("refresh-two");
    credentials.account_id = strappy_string_duplicate("account-one");
    credentials.expires_at_milliseconds = 2100000000000LL;
    ok = probe_credentials_are_valid(&credentials) &&
      probe_save_credential_cache(path, &credentials, &error);
  }
  if (ok) {
    status = probe_load_credential_cache(path, &loaded, &error);
    ok = (status == PROBE_CREDENTIAL_CACHE_LOADED) &&
      (strcmp(loaded.access_token, "access-two") == 0) &&
      (strcmp(loaded.refresh_token, "refresh-two") == 0) &&
      (strcmp(loaded.account_id, "account-one") == 0) &&
      (loaded.expires_at_milliseconds == 2100000000000LL);
  }
  strappy_openai_oauth_credentials_destroy(&loaded);
  strappy_openai_oauth_credentials_destroy(&credentials);
  strappy_free_string(error);
  error = NULL;

  if (ok) {
    ok = (chmod(path, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) == 0);
  }
  if (ok) {
    status = probe_load_credential_cache(path, &loaded, &error);
    ok = (status == PROBE_CREDENTIAL_CACHE_ERROR) && (error != NULL);
  }
  strappy_openai_oauth_credentials_destroy(&loaded);
  strappy_free_string(error);
  if (unlink(path) != 0) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr, "Credential-cache self-test failed.\n");
    return 0;
  }
  printf("PASS: credential cache is atomic, reusable, and owner-only.\n");
  return 1;
}

static int probe_replace_model(cJSON *request, const char *model)
{
  cJSON *replacement;

  replacement = cJSON_CreateString(model);
  if (replacement == NULL) {
    return 0;
  }
  if (!cJSON_ReplaceItemInObjectCaseSensitive(request,
                                               "model",
                                               replacement)) {
    cJSON_Delete(replacement);
    return 0;
  }
  return 1;
}

static int probe_replace_streaming(cJSON *request, int uses_sse)
{
  cJSON *replacement;

  replacement = cJSON_CreateBool(uses_sse ? 1 : 0);
  if (replacement == NULL) {
    return 0;
  }
  if (!cJSON_ReplaceItemInObjectCaseSensitive(request,
                                               "stream",
                                               replacement)) {
    cJSON_Delete(replacement);
    return 0;
  }
  return 1;
}

static char *probe_request_from_template(const char *template_json,
                                         const char *model,
                                         int uses_sse)
{
  cJSON *request;
  char *json;

  request = cJSON_Parse(template_json);
  if (!cJSON_IsObject(request) || !probe_replace_model(request, model) ||
      !probe_replace_streaming(request, uses_sse)) {
    cJSON_Delete(request);
    return NULL;
  }
  json = cJSON_PrintUnformatted(request);
  cJSON_Delete(request);
  return json;
}

static const char *probe_error_detail(cJSON *root, const char **code_out)
{
  cJSON *error;
  cJSON *value;

  if (code_out != NULL) {
    *code_out = NULL;
  }
  if (!cJSON_IsObject(root)) {
    return NULL;
  }
  error = cJSON_GetObjectItemCaseSensitive(root, "error");
  if (cJSON_IsString(error) && (error->valuestring != NULL)) {
    if (code_out != NULL) {
      *code_out = error->valuestring;
    }
    return error->valuestring;
  }
  if (!cJSON_IsObject(error)) {
    return NULL;
  }
  value = cJSON_GetObjectItemCaseSensitive(error, "code");
  if (!cJSON_IsString(value)) {
    value = cJSON_GetObjectItemCaseSensitive(error, "type");
  }
  if (cJSON_IsString(value) && (value->valuestring != NULL) &&
      (code_out != NULL)) {
    *code_out = value->valuestring;
  }
  value = cJSON_GetObjectItemCaseSensitive(error, "message");
  return (cJSON_IsString(value) && (value->valuestring != NULL)) ?
    value->valuestring : ((code_out != NULL) ? *code_out : NULL);
}

static const char *probe_oauth_error_category(const char *error)
{
  if (error == NULL) {
    return "unspecified";
  }
  if (strstr(error, "disabled") != NULL) {
    return "device-login-disabled";
  }
  if (strstr(error, "access_denied") != NULL) {
    return "access-denied";
  }
  if (strstr(error, "expired_token") != NULL) {
    return "device-code-expired";
  }
  if (strstr(error, "invalid_grant") != NULL) {
    return "sign-in-required";
  }
  if (strstr(error, "unauthorized_client") != NULL) {
    return "unauthorized-client";
  }
  if (strstr(error, "invalid_request") != NULL) {
    return "invalid-request";
  }
  if (strstr(error, "HTTP 401") != NULL) {
    return "authentication";
  }
  if (strstr(error, "HTTP 403") != NULL) {
    return "permission";
  }
  if (strstr(error, "HTTP 429") != NULL) {
    return "rate-limit";
  }
  if ((strstr(error, "server_error") != NULL) ||
      (strstr(error, "temporarily_unavailable") != NULL) ||
      (strstr(error, "HTTP 5") != NULL)) {
    return "provider";
  }
  if (strstr(error, "cancelled") != NULL) {
    return "cancelled";
  }
  if (strstr(error, "network") != NULL) {
    return "network";
  }
  if ((strstr(error, "invalid JSON") != NULL) ||
      (strstr(error, "missing") != NULL)) {
    return "protocol";
  }
  return "oauth";
}

static void probe_print_safe_failure(const char *operation,
                                     const strappy_responses_http_result *http)
{
  cJSON *root;
  const char *code;
  const char *detail;
  const char *category;

  root = cJSON_Parse((http != NULL) ? http->response_json : NULL);
  code = NULL;
  detail = probe_error_detail(root, &code);
  if (strappy_provider_response_is_plan_limit(
        STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT,
        (http != NULL) ? http->http_status : 0L,
        code,
        detail)) {
    category = "plan-limit-or-model-not-included";
  } else if ((http != NULL) && (http->transport_error != NULL)) {
    category = "transport";
  } else if ((http != NULL) && (http->http_status == 401L)) {
    category = "authentication";
  } else if ((http != NULL) && (http->http_status == 403L)) {
    category = "permission";
  } else if ((http != NULL) && (http->http_status >= 500L)) {
    category = "provider";
  } else {
    category = "protocol-or-request";
  }
  fprintf(stderr,
          "%s failed (HTTP %ld, category %s).\n",
          operation,
          (http != NULL) ? http->http_status : 0L,
          category);
  cJSON_Delete(root);
}

static int probe_response_is_completed(const char *response_json)
{
  cJSON *root;
  cJSON *status;
  cJSON *type;
  int ok;

  root = cJSON_Parse(response_json);
  status = cJSON_GetObjectItemCaseSensitive(root, "status");
  type = cJSON_GetObjectItemCaseSensitive(root, "type");
  ok = cJSON_IsObject(root) &&
    (!cJSON_IsString(type) || (type->valuestring == NULL) ||
     (strcmp(type->valuestring, "error") != 0)) &&
    (!cJSON_IsString(status) || (status->valuestring == NULL) ||
     (strcmp(status->valuestring, "completed") == 0));
  cJSON_Delete(root);
  return ok;
}

static int probe_send_request(strappy_config *config,
                              const strappy_openai_oauth_credentials *credentials,
                              const char *request_json,
                              strappy_responses_response_transport transport,
                              const char *operation,
                              char **response_json_out)
{
  strappy_responses_http_result http;
  char *error;
  int ok;

  *response_json_out = NULL;
  strappy_responses_http_result_init(&http);
  error = NULL;
  ok = strappy_client_send_provider_responses_json_with_transport(
    config,
    STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT,
    transport,
    credentials->access_token,
    credentials->account_id,
    "strappy-compatibility-probe",
    request_json,
    &http,
    probe_responses_callback,
    NULL,
    &error);
  if (!ok || http.cancelled || (http.curl_code != 0L) ||
      (http.http_status < 200L) || (http.http_status >= 300L) ||
      (http.transport_error != NULL) || (http.response_json == NULL) ||
      !probe_response_is_completed(http.response_json)) {
    if (error != NULL) {
      fprintf(stderr, "%s failed before a usable response.\n", operation);
    } else {
      probe_print_safe_failure(operation, &http);
    }
    ok = 0;
  } else {
    *response_json_out = http.response_json;
    http.response_json = NULL;
    ok = 1;
  }
  strappy_free_string(error);
  strappy_responses_http_result_destroy(&http);
  return ok;
}

static int probe_append_text(char **text,
                             size_t *length,
                             const char *fragment)
{
  size_t fragment_length;
  char *next;

  fragment_length = strlen(fragment);
  if ((*length > (((size_t)-1) - fragment_length - 1U)) ||
      ((*length + fragment_length) > PROBE_MAX_OUTPUT_BYTES)) {
    return 0;
  }
  next = (char *)realloc(*text, *length + fragment_length + 1U);
  if (next == NULL) {
    return 0;
  }
  *text = next;
  memcpy(*text + *length, fragment, fragment_length);
  *length += fragment_length;
  (*text)[*length] = '\0';
  return 1;
}

static char *probe_copy_output_text(const char *response_json)
{
  cJSON *root;
  cJSON *output;
  cJSON *item;
  char *text;
  size_t length;
  int ok;

  root = cJSON_Parse(response_json);
  output = cJSON_GetObjectItemCaseSensitive(root, "output");
  text = NULL;
  length = 0U;
  ok = cJSON_IsArray(output);
  for (item = ok ? output->child : NULL;
       (item != NULL) && ok;
       item = item->next) {
    cJSON *content;
    cJSON *part;

    content = cJSON_GetObjectItemCaseSensitive(item, "content");
    if (!cJSON_IsArray(content)) {
      continue;
    }
    for (part = content->child;
         (part != NULL) && ok;
         part = part->next) {
      cJSON *type;
      cJSON *value;

      type = cJSON_GetObjectItemCaseSensitive(part, "type");
      value = cJSON_GetObjectItemCaseSensitive(part, "text");
      if (cJSON_IsString(type) && (type->valuestring != NULL) &&
          (strcmp(type->valuestring, "output_text") == 0) &&
          cJSON_IsString(value) && (value->valuestring != NULL)) {
        ok = probe_append_text(&text, &length, value->valuestring);
      }
    }
  }
  if (ok && (length == 0U)) {
    cJSON *value;

    value = cJSON_GetObjectItemCaseSensitive(root, "output_text");
    if (cJSON_IsString(value) && (value->valuestring != NULL)) {
      ok = probe_append_text(&text, &length, value->valuestring);
    }
  }
  cJSON_Delete(root);
  if (!ok || (length == 0U)) {
    free(text);
    return NULL;
  }
  return text;
}

static int probe_http_url_is_valid(const char *url)
{
  return (url != NULL) &&
    ((strncmp(url, "https://", 8U) == 0) ||
     (strncmp(url, "http://", 7U) == 0));
}

static int probe_web_search_action_has_query(cJSON *action)
{
  cJSON *query;
  cJSON *queries;
  cJSON *value;

  query = cJSON_GetObjectItemCaseSensitive(action, "query");
  if (cJSON_IsString(query) && (query->valuestring != NULL) &&
      (query->valuestring[0] != '\0')) {
    return 1;
  }
  queries = cJSON_GetObjectItemCaseSensitive(action, "queries");
  if (!cJSON_IsArray(queries)) {
    return 0;
  }
  cJSON_ArrayForEach(value, queries) {
    if (cJSON_IsString(value) && (value->valuestring != NULL) &&
        (value->valuestring[0] != '\0')) {
      return 1;
    }
  }
  return 0;
}

static int probe_web_search_response_is_expected(const char *response_json)
{
  cJSON *root;
  cJSON *output;
  cJSON *item;
  size_t search_count;
  size_t source_count;
  size_t citation_count;
  int has_output_text;
  int ok;

  root = cJSON_Parse(response_json);
  output = cJSON_GetObjectItemCaseSensitive(root, "output");
  search_count = 0U;
  source_count = 0U;
  citation_count = 0U;
  has_output_text = 0;
  ok = cJSON_IsObject(root) && cJSON_IsArray(output);
  for (item = ok ? output->child : NULL;
       (item != NULL) && ok;
       item = item->next) {
    cJSON *type;

    type = cJSON_GetObjectItemCaseSensitive(item, "type");
    if (!cJSON_IsString(type) || (type->valuestring == NULL)) {
      continue;
    }
    if (strcmp(type->valuestring, "web_search_call") == 0) {
      cJSON *status;
      cJSON *action;
      cJSON *action_type;
      cJSON *sources;
      cJSON *source;

      status = cJSON_GetObjectItemCaseSensitive(item, "status");
      action = cJSON_GetObjectItemCaseSensitive(item, "action");
      action_type = cJSON_GetObjectItemCaseSensitive(action, "type");
      if (!cJSON_IsString(status) || (status->valuestring == NULL) ||
          (strcmp(status->valuestring, "completed") != 0) ||
          !cJSON_IsObject(action) || !cJSON_IsString(action_type) ||
          (action_type->valuestring == NULL)) {
        ok = 0;
        continue;
      }
      if (strcmp(action_type->valuestring, "search") != 0) {
        continue;
      }
      if (!probe_web_search_action_has_query(action)) {
        ok = 0;
        continue;
      }
      search_count++;
      sources = cJSON_GetObjectItemCaseSensitive(action, "sources");
      cJSON_ArrayForEach(source, sources) {
        cJSON *url;

        url = cJSON_IsObject(source) ?
          cJSON_GetObjectItemCaseSensitive(source, "url") : NULL;
        if (cJSON_IsString(url) &&
            probe_http_url_is_valid(url->valuestring)) {
          source_count++;
        }
      }
    } else if (strcmp(type->valuestring, "message") == 0) {
      cJSON *content;
      cJSON *part;

      content = cJSON_GetObjectItemCaseSensitive(item, "content");
      cJSON_ArrayForEach(part, content) {
        cJSON *part_type;
        cJSON *text;
        cJSON *annotations;
        cJSON *annotation;

        part_type = cJSON_IsObject(part) ?
          cJSON_GetObjectItemCaseSensitive(part, "type") : NULL;
        text = cJSON_IsObject(part) ?
          cJSON_GetObjectItemCaseSensitive(part, "text") : NULL;
        if (cJSON_IsString(part_type) &&
            (part_type->valuestring != NULL) &&
            (strcmp(part_type->valuestring, "output_text") == 0) &&
            cJSON_IsString(text) && (text->valuestring != NULL) &&
            (text->valuestring[0] != '\0')) {
          has_output_text = 1;
        }
        annotations = cJSON_IsObject(part) ?
          cJSON_GetObjectItemCaseSensitive(part, "annotations") : NULL;
        cJSON_ArrayForEach(annotation, annotations) {
          cJSON *annotation_type;
          cJSON *url;

          annotation_type = cJSON_IsObject(annotation) ?
            cJSON_GetObjectItemCaseSensitive(annotation, "type") : NULL;
          url = cJSON_IsObject(annotation) ?
            cJSON_GetObjectItemCaseSensitive(annotation, "url") : NULL;
          if (cJSON_IsString(annotation_type) &&
              (annotation_type->valuestring != NULL) &&
              (strcmp(annotation_type->valuestring, "url_citation") == 0) &&
              cJSON_IsString(url) &&
              probe_http_url_is_valid(url->valuestring)) {
            citation_count++;
          }
        }
      }
    }
  }
  cJSON_Delete(root);
  ok = ok && (search_count > 0U) && (source_count > 0U) &&
    (citation_count > 0U) && has_output_text;
  if (ok) {
    printf("PASS: live web search returned %lu search action(s), %lu "
           "consulted source(s), and %lu citation(s).\n",
           (unsigned long)search_count,
           (unsigned long)source_count,
           (unsigned long)citation_count);
  }
  return ok;
}

static int probe_text_matches_fixture(const char *text, const char *expected)
{
  const char *start;
  const char *end;
  size_t expected_length;

  if ((text == NULL) || (expected == NULL)) {
    return 0;
  }
  start = text;
  while ((*start == ' ') || (*start == '\t') || (*start == '\r') ||
         (*start == '\n')) {
    start++;
  }
  end = start + strlen(start);
  while ((end > start) &&
         ((end[-1] == ' ') || (end[-1] == '\t') || (end[-1] == '\r') ||
          (end[-1] == '\n'))) {
    end--;
  }
  expected_length = strlen(expected);
  return ((size_t)(end - start) == expected_length) &&
    (memcmp(start, expected, expected_length) == 0);
}

static cJSON *probe_find_function_call(cJSON *response)
{
  cJSON *output;
  cJSON *item;

  output = cJSON_GetObjectItemCaseSensitive(response, "output");
  if (!cJSON_IsArray(output)) {
    return NULL;
  }
  for (item = output->child; item != NULL; item = item->next) {
    cJSON *type;
    cJSON *name;

    type = cJSON_GetObjectItemCaseSensitive(item, "type");
    name = cJSON_GetObjectItemCaseSensitive(item, "name");
    if (cJSON_IsString(type) && (type->valuestring != NULL) &&
        (strcmp(type->valuestring, "function_call") == 0) &&
        cJSON_IsString(name) && (name->valuestring != NULL) &&
        (strcmp(name->valuestring, "strappy_probe_add") == 0)) {
      return item;
    }
  }
  return NULL;
}

static int probe_function_call_is_expected(cJSON *call)
{
  cJSON *arguments_value;
  cJSON *arguments;
  cJSON *left;
  cJSON *right;
  int ok;

  arguments_value = cJSON_GetObjectItemCaseSensitive(call, "arguments");
  arguments = cJSON_IsString(arguments_value) ?
    cJSON_Parse(arguments_value->valuestring) : NULL;
  left = cJSON_GetObjectItemCaseSensitive(arguments, "left");
  right = cJSON_GetObjectItemCaseSensitive(arguments, "right");
  ok = cJSON_IsNumber(left) && cJSON_IsNumber(right) &&
    (left->valuedouble == 19.0) && (right->valuedouble == 23.0);
  cJSON_Delete(arguments);
  return ok;
}

static char *probe_build_tool_continuation(const char *initial_request_json,
                                           const char *response_json)
{
  cJSON *request;
  cJSON *response;
  cJSON *input;
  cJSON *output;
  cJSON *item;
  cJSON *call;
  cJSON *call_id;
  cJSON *function_output;
  cJSON *tool_choice;
  char *json;
  int ok;

  request = cJSON_Parse(initial_request_json);
  response = cJSON_Parse(response_json);
  input = cJSON_GetObjectItemCaseSensitive(request, "input");
  output = cJSON_GetObjectItemCaseSensitive(response, "output");
  call = probe_find_function_call(response);
  call_id = cJSON_GetObjectItemCaseSensitive(call, "call_id");
  ok = cJSON_IsObject(request) && cJSON_IsArray(input) &&
    cJSON_IsArray(output) && (call != NULL) &&
    probe_function_call_is_expected(call) && cJSON_IsString(call_id) &&
    (call_id->valuestring != NULL);
  for (item = ok ? output->child : NULL;
       (item != NULL) && ok;
       item = item->next) {
    cJSON *copy;

    copy = cJSON_Duplicate(item, 1);
    if ((copy == NULL) || !cJSON_AddItemToArray(input, copy)) {
      cJSON_Delete(copy);
      ok = 0;
    }
  }
  function_output = ok ? cJSON_CreateObject() : NULL;
  if ((function_output == NULL) ||
      (cJSON_AddStringToObject(function_output,
                               "type",
                               "function_call_output") == NULL) ||
      (cJSON_AddStringToObject(function_output,
                               "call_id",
                               call_id->valuestring) == NULL) ||
      (cJSON_AddStringToObject(function_output, "output", "42") == NULL) ||
      !cJSON_AddItemToArray(input, function_output)) {
    cJSON_Delete(function_output);
    ok = 0;
  }
  tool_choice = ok ? cJSON_CreateString("none") : NULL;
  if ((tool_choice == NULL) ||
      !cJSON_ReplaceItemInObjectCaseSensitive(request,
                                               "tool_choice",
                                               tool_choice)) {
    cJSON_Delete(tool_choice);
    ok = 0;
  }
  json = ok ? cJSON_PrintUnformatted(request) : NULL;
  cJSON_Delete(response);
  cJSON_Delete(request);
  return json;
}

static int probe_run_text_round(
  strappy_config *config,
  const strappy_openai_oauth_credentials *credentials,
  const char *model,
  int uses_sse)
{
  static const char expected[] = "Strappy compatibility probe passed.";
  const char *operation;
  char *request_json;
  char *response_json;
  char *text;
  int ok;

  operation = uses_sse ? "Streaming text response" :
    "Non-streaming text response";
  request_json = probe_request_from_template(PROBE_TEXT_REQUEST_TEMPLATE,
                                             model,
                                             uses_sse);
  response_json = NULL;
  text = NULL;
  ok = (request_json != NULL) &&
    probe_send_request(config,
                       credentials,
                       request_json,
                       uses_sse ? STRAPPY_RESPONSES_RESPONSE_TRANSPORT_SSE :
                         STRAPPY_RESPONSES_RESPONSE_TRANSPORT_JSON,
                       operation,
                       &response_json);
  if (ok) {
    text = probe_copy_output_text(response_json);
    ok = probe_text_matches_fixture(text, expected);
  }
  if (!ok && (response_json != NULL)) {
    fprintf(stderr,
            "%s completed but produced %s.\n",
            operation,
            (text == NULL) ? "no output-text fixture" :
              "unexpected output text");
  }
  free(text);
  free(response_json);
  free(request_json);
  return ok;
}

static int probe_run_tool_round(
  strappy_config *config,
  const strappy_openai_oauth_credentials *credentials,
  const char *model)
{
  char *request_json;
  char *first_response_json;
  char *continuation_json;
  char *final_response_json;
  char *text;
  int ok;

  request_json = probe_request_from_template(PROBE_TOOL_REQUEST_TEMPLATE,
                                             model,
                                             1);
  first_response_json = NULL;
  continuation_json = NULL;
  final_response_json = NULL;
  text = NULL;
  ok = (request_json != NULL) &&
    probe_send_request(config,
                       credentials,
                       request_json,
                       STRAPPY_RESPONSES_RESPONSE_TRANSPORT_SSE,
                       "Local function-call response",
                       &first_response_json);
  if (ok) {
    continuation_json = probe_build_tool_continuation(request_json,
                                                       first_response_json);
    ok = (continuation_json != NULL);
  }
  if (ok) {
    ok = probe_send_request(config,
                            credentials,
                            continuation_json,
                            STRAPPY_RESPONSES_RESPONSE_TRANSPORT_SSE,
                            "Local function-output continuation",
                            &final_response_json);
  }
  if (ok) {
    text = probe_copy_output_text(final_response_json);
    ok = (text != NULL) && (strstr(text, "42") != NULL);
  }
  if (!ok && (first_response_json != NULL)) {
    fprintf(stderr, "Local function compatibility fixture failed.\n");
  }
  free(text);
  free(final_response_json);
  free(continuation_json);
  free(first_response_json);
  free(request_json);
  return ok;
}

static int probe_run_web_search_round(
  strappy_config *config,
  const strappy_openai_oauth_credentials *credentials)
{
  char *request_json;
  char *response_json;
  int ok;

  request_json = probe_request_from_template(
    PROBE_WEB_SEARCH_REQUEST_TEMPLATE,
    PROBE_DEFAULT_MODEL,
    1);
  response_json = NULL;
  ok = (request_json != NULL) &&
    probe_send_request(config,
                       credentials,
                       request_json,
                       STRAPPY_RESPONSES_RESPONSE_TRANSPORT_SSE,
                       "Live native web-search response",
                       &response_json);
  if (ok) {
    ok = probe_web_search_response_is_expected(response_json);
  }
  if (!ok && (response_json != NULL)) {
    fprintf(stderr,
            "Live native web search completed with an unexpected response "
            "shape.\n");
  }
  free(response_json);
  free(request_json);
  return ok;
}

static int probe_obtain_credentials(
  const strappy_openai_oauth_configuration *oauth_configuration,
  strappy_openai_oauth_device *device,
  const char *credential_path,
  int reauthorize,
  int allow_device_authorization,
  strappy_openai_oauth_credentials *credentials)
{
  strappy_openai_oauth_credentials refreshed;
  probe_credential_cache_status cache_status;
  long long now_milliseconds;
  char *error;
  int identity_matches;
  int ok;

  strappy_openai_oauth_credentials_init(&refreshed);
  error = NULL;
  cache_status = reauthorize ? PROBE_CREDENTIAL_CACHE_MISSING :
    probe_load_credential_cache(credential_path, credentials, &error);
  if (cache_status == PROBE_CREDENTIAL_CACHE_ERROR) {
    fprintf(stderr,
            "Could not reuse the OAuth credential cache: %s\n",
            (error != NULL) ? error : "no safe diagnostic");
    strappy_free_string(error);
    return 0;
  }
  strappy_free_string(error);
  error = NULL;

  if (cache_status == PROBE_CREDENTIAL_CACHE_LOADED) {
    ok = probe_now_milliseconds(&now_milliseconds);
    if (!ok) {
      fprintf(stderr, "Could not evaluate the cached token expiry.\n");
      return 0;
    }
    if ((credentials->expires_at_milliseconds > now_milliseconds) &&
        ((credentials->expires_at_milliseconds - now_milliseconds) >
         PROBE_REFRESH_LEEWAY_MILLISECONDS)) {
      printf("PASS: reused the cached OAuth credential without an OAuth "
             "request.\n");
      return 1;
    }

    ok = strappy_openai_oauth_refresh_credentials(
      oauth_configuration,
      credentials->refresh_token,
      &refreshed,
      probe_oauth_cancelled,
      NULL,
      &error);
    identity_matches = ok && (refreshed.account_id != NULL) &&
      (strcmp(credentials->account_id, refreshed.account_id) == 0);
    if (!ok || !identity_matches) {
      fprintf(stderr,
              "Cached OAuth credential refresh failed (category %s).\n",
              (error != NULL) ? probe_oauth_error_category(error) :
                "account-identity-changed");
      strappy_free_string(error);
      strappy_openai_oauth_credentials_destroy(&refreshed);
      return 0;
    }
    strappy_free_string(error);
    error = NULL;
    ok = probe_save_credential_cache(credential_path, &refreshed, &error);
    if (!ok) {
      fprintf(stderr,
              "Refreshed credentials could not be persisted: %s\n",
              (error != NULL) ? error : "no safe diagnostic");
      strappy_free_string(error);
      strappy_openai_oauth_credentials_destroy(&refreshed);
      return 0;
    }
    strappy_free_string(error);
    strappy_openai_oauth_credentials_destroy(credentials);
    *credentials = refreshed;
    strappy_openai_oauth_credentials_init(&refreshed);
    printf("PASS: refreshed and atomically replaced the cached OAuth "
           "credential.\n");
    return 1;
  }

  if (!allow_device_authorization) {
    fprintf(stderr,
            "The live web-search credential cache disappeared before it "
            "could be loaded.\n");
    return 0;
  }

  ok = strappy_openai_oauth_start_device_authorization(
    oauth_configuration,
    device,
    probe_oauth_cancelled,
    NULL,
    &error);
  if (ok) {
    printf("Open %s and enter code %s\n",
           oauth_configuration->verification_url,
           device->user_code);
    printf("Waiting for authorization (Ctrl-C cancels)...\n");
    fflush(stdout);
    ok = strappy_openai_oauth_complete_device_authorization(
      oauth_configuration,
      device,
      credentials,
      probe_oauth_cancelled,
      NULL,
      &error);
  }
  if (!ok) {
    fprintf(stderr,
            "Compatibility-probe login failed (category %s).\n",
            probe_oauth_error_category(error));
    strappy_free_string(error);
    return 0;
  }
  strappy_free_string(error);
  error = NULL;

  ok = probe_save_credential_cache(credential_path, credentials, &error);
  if (!ok) {
    fprintf(stderr,
            "OAuth login succeeded, but credentials could not be persisted: "
            "%s\n",
            (error != NULL) ? error : "no safe diagnostic");
    strappy_free_string(error);
    return 0;
  }
  strappy_free_string(error);
  printf("PASS: OAuth login completed and credentials were cached before "
         "API testing.\n");
  return 1;
}

int main(int argc, char **argv)
{
  const char *model;
  const char *credential_path;
  const char *reauthorize_value;
  strappy_openai_oauth_configuration oauth_configuration;
  strappy_openai_oauth_device device;
  strappy_openai_oauth_credentials credentials;
  strappy_config config;
  char *endpoint;
  char *error;
  int reauthorize;
  int nonstreaming_ok;
  int web_search_live;
  int ok;

  if ((argc == 2) &&
      (strcmp(argv[1], "--credential-cache-self-test") == 0)) {
    return probe_credential_cache_self_test() ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  web_search_live = (argc == 2) &&
    (strcmp(argv[1], "--web-search-live-if-configured") == 0);
  if ((argc != 1) && !web_search_live) {
    fprintf(stderr,
            "Usage: %s [--credential-cache-self-test|"
            "--web-search-live-if-configured]\n",
            argv[0]);
    return EXIT_FAILURE;
  }

  model = web_search_live ? PROBE_DEFAULT_MODEL :
    getenv("STRAPPY_CHATGPT_PROBE_MODEL");
  if ((model == NULL) || (model[0] == '\0')) {
    model = PROBE_DEFAULT_MODEL;
  }
  if ((strlen(model) > PROBE_MAX_MODEL_BYTES) ||
      (strchr(model, '\r') != NULL) || (strchr(model, '\n') != NULL)) {
    fprintf(stderr, "The compatibility-probe model identifier is invalid.\n");
    return EXIT_FAILURE;
  }
  credential_path = getenv(PROBE_CREDENTIAL_PATH_ENV);
  if ((credential_path == NULL) || (credential_path[0] == '\0')) {
    credential_path = PROBE_DEFAULT_CREDENTIAL_PATH;
  }
  reauthorize_value = getenv(PROBE_REAUTHORIZE_ENV);
  if ((reauthorize_value != NULL) && (reauthorize_value[0] != '\0') &&
      (strcmp(reauthorize_value, "1") != 0)) {
    fprintf(stderr,
            PROBE_REAUTHORIZE_ENV " must be unset or exactly 1.\n");
    return EXIT_FAILURE;
  }
  reauthorize = (reauthorize_value != NULL) &&
    (strcmp(reauthorize_value, "1") == 0);
  if (web_search_live && reauthorize) {
    fprintf(stderr,
            "The automated live web-search check cannot reauthorize.\n");
    return EXIT_FAILURE;
  }
  if (web_search_live && (access(credential_path, F_OK) != 0)) {
    if (errno == ENOENT) {
      printf("SKIP: live OpenAI Account web search (%s is absent).\n",
             credential_path);
      return EXIT_SUCCESS;
    }
    fprintf(stderr,
            "Could not inspect the live web-search credential cache: %s.\n",
            strerror(errno));
    return EXIT_FAILURE;
  }

  (void)signal(SIGINT, probe_signal_handler);
  (void)signal(SIGTERM, probe_signal_handler);
  strappy_openai_oauth_default_configuration(&oauth_configuration);
  strappy_openai_oauth_device_init(&device);
  strappy_openai_oauth_credentials_init(&credentials);
  strappy_config_init(&config);
  error = NULL;
  nonstreaming_ok = 0;

  printf("Strappy ChatGPT %s (%s)\n",
         web_search_live ? "live web-search probe" :
           "compatibility probe",
         model);
  printf("Responses stay in memory; OAuth credentials use an ignored, "
         "owner-only JSON cache.\n");
  printf("Valid cached tokens avoid OAuth calls; refresh occurs only near "
         "expiry.\n");
  if (web_search_live) {
    printf("It exercises LUNA at low reasoning effort with native web "
           "search, consulted sources, and citations.\n\n");
  } else {
    printf("It exercises non-streaming JSON, SSE termination, and a local "
           "function round.\n\n");
  }
  ok = probe_obtain_credentials(
    &oauth_configuration,
    &device,
    credential_path,
    reauthorize,
    web_search_live ? 0 : 1,
    &credentials);

  endpoint = ok ? strappy_provider_responses_endpoint(
    STRAPPY_PROVIDER_KIND_OPENAI_CHATGPT,
    NULL,
    &error) : NULL;
  if (ok && (endpoint == NULL)) {
    fprintf(stderr,
            "Could not resolve the ChatGPT endpoint: %s\n",
            (error != NULL) ? error : "no safe diagnostic");
    ok = 0;
  }
  config.api_endpoint = endpoint;
  strappy_free_string(error);
  error = NULL;

  if (ok && web_search_live) {
    ok = probe_run_web_search_round(&config, &credentials);
  }
  if (ok && !web_search_live) {
    nonstreaming_ok = probe_run_text_round(&config,
                                            &credentials,
                                            model,
                                            0);
    if (nonstreaming_ok) {
      printf("PASS: non-streaming JSON response.\n");
    }
  }
  if (ok && !web_search_live) {
    ok = probe_run_text_round(&config, &credentials, model, 1);
    if (ok) {
      printf("PASS: streaming text request and terminal SSE response.\n");
    }
  }
  if (ok && !web_search_live) {
    ok = probe_run_tool_round(&config, &credentials, model);
    if (ok) {
      printf("PASS: local function call/output continuation.\n");
    }
  }
  if (ok && !web_search_live && !nonstreaming_ok) {
    fprintf(stderr,
            "FAIL: the ChatGPT backend did not complete the non-streaming "
            "JSON compatibility check.\n");
    ok = 0;
  }
  if (ok && !web_search_live) {
    printf("PASS: all live compatibility checks completed.\n");
    printf("Record only PASS/FAIL, date, plan category, and model in the "
           "compatibility report; never record identifiers or payloads.\n");
  }

  strappy_config_destroy(&config);
  strappy_openai_oauth_credentials_destroy(&credentials);
  strappy_openai_oauth_device_destroy(&device);
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
