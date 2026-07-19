#include "strappy_prompt.h"

#include "strappy_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *strappy_prompt_read_file(const char *path, char **error_out)
{
  FILE *file;
  long file_length;
  size_t length;
  char *buffer;
  size_t bytes_read;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(error_out, "System prompt template path is not configured.");
    return NULL;
  }

  file = fopen(path, "rb");
  if (file == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not open system prompt template: %s",
                                path);
    return NULL;
  }

  if (fseek(file, 0L, SEEK_END) != 0) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not read system prompt template: %s",
                                path);
    return NULL;
  }

  file_length = ftell(file);
  if (file_length < 0L) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not measure system prompt template: %s",
                                path);
    return NULL;
  }

  if ((unsigned long)file_length >= (unsigned long)((size_t)-1)) {
    fclose(file);
    strappy_set_error(error_out, "System prompt template is too large.");
    return NULL;
  }

  length = (size_t)file_length;
  if (fseek(file, 0L, SEEK_SET) != 0) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not rewind system prompt template: %s",
                                path);
    return NULL;
  }

  buffer = (char *)malloc(length + 1U);
  if (buffer == NULL) {
    fclose(file);
    strappy_set_error(error_out, "Could not allocate system prompt template.");
    return NULL;
  }

  bytes_read = fread(buffer, 1U, length, file);
  if ((bytes_read != length) || ferror(file)) {
    free(buffer);
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not read system prompt template: %s",
                                path);
    return NULL;
  }

  buffer[length] = '\0';
  fclose(file);
  return buffer;
}

char *strappy_prompt_resource_directory_from_template_path(
  const char *template_path,
  char **error_out)
{
  const char *slash;
  size_t length;
  char *directory;

  if ((template_path == NULL) || (template_path[0] == '\0')) {
    strappy_set_error(error_out, "System prompt template path is not configured.");
    return NULL;
  }

  slash = strrchr(template_path, '/');
  if (slash == NULL) {
    directory = strappy_string_duplicate(".");
    if (directory == NULL) {
      strappy_set_error(error_out, "Could not allocate guidance resource directory.");
    }
    return directory;
  }

  length = (size_t)(slash - template_path);
  if (length == 0U) {
    directory = strappy_string_duplicate("/");
    if (directory == NULL) {
      strappy_set_error(error_out, "Could not allocate guidance resource directory.");
    }
    return directory;
  }

  directory = (char *)malloc(length + 1U);
  if (directory == NULL) {
    strappy_set_error(error_out, "Could not allocate guidance resource directory.");
    return NULL;
  }

  memcpy(directory, template_path, length);
  directory[length] = '\0';
  return directory;
}

char *strappy_prompt_render_system_prompt(const char *template_path,
                                          char **error_out)
{
  return strappy_prompt_read_file(template_path, error_out);
}

char *strappy_prompt_render_resource(const char *resource_dir,
                                     const char *resource_name,
                                     char **error_out)
{
  size_t directory_length;
  size_t resource_length;
  int needs_slash;
  char *path;
  char *result;

  if ((resource_dir == NULL) || (resource_dir[0] == '\0') ||
      (resource_name == NULL) || (resource_name[0] == '\0') ||
      (strchr(resource_name, '/') != NULL) ||
      (strchr(resource_name, '\\') != NULL) ||
      (strstr(resource_name, "..") != NULL)) {
    strappy_set_error(error_out, "System prompt resource is not configured.");
    return NULL;
  }
  directory_length = strlen(resource_dir);
  resource_length = strlen(resource_name);
  needs_slash = (resource_dir[directory_length - 1U] == '/') ? 0 : 1;
  if (directory_length > ((size_t)-1) - resource_length -
      (size_t)needs_slash - 1U) {
    strappy_set_error(error_out, "System prompt resource path is too large.");
    return NULL;
  }
  path = (char *)malloc(directory_length + (size_t)needs_slash +
                        resource_length + 1U);
  if (path == NULL) {
    strappy_set_error(error_out,
                      "Could not allocate system prompt resource path.");
    return NULL;
  }
  memcpy(path, resource_dir, directory_length);
  if (needs_slash) {
    path[directory_length++] = '/';
  }
  memcpy(path + directory_length, resource_name, resource_length + 1U);
  result = strappy_prompt_read_file(path, error_out);
  free(path);
  return result;
}
