#include "strappy_prompt.h"

#include "strappy_core.h"
#include "strappy_quality_policy.h"
#include "strappy_tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct strappy_prompt_buffer {
  char *data;
  size_t length;
} strappy_prompt_buffer;

static void strappy_prompt_buffer_destroy(strappy_prompt_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }
  free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0U;
}

static int strappy_prompt_buffer_append(strappy_prompt_buffer *buffer,
                                        const char *text)
{
  size_t length;
  char *next;

  if ((buffer == NULL) || (text == NULL)) {
    return 0;
  }
  length = strlen(text);
  if (buffer->length > ((size_t)-1) - length - 1U) {
    return 0;
  }
  next = (char *)realloc(buffer->data, buffer->length + length + 1U);
  if (next == NULL) {
    return 0;
  }
  buffer->data = next;
  memcpy(buffer->data + buffer->length, text, length + 1U);
  buffer->length += length;
  return 1;
}

static char *strappy_prompt_read_file(const char *path, char **error_out)
{
  FILE *file;
  long file_length;
  size_t length;
  char *buffer;
  size_t bytes_read;

  if ((path == NULL) || (path[0] == '\0')) {
    strappy_set_error(error_out, "Prompt resource path is not configured.");
    return NULL;
  }

  file = fopen(path, "rb");
  if (file == NULL) {
    strappy_set_formatted_error(error_out,
                                "Could not open prompt resource: %s",
                                path);
    return NULL;
  }

  if (fseek(file, 0L, SEEK_END) != 0) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not read prompt resource: %s",
                                path);
    return NULL;
  }

  file_length = ftell(file);
  if (file_length < 0L) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not measure prompt resource: %s",
                                path);
    return NULL;
  }

  if ((unsigned long)file_length >= (unsigned long)((size_t)-1)) {
    fclose(file);
    strappy_set_error(error_out, "Prompt resource is too large.");
    return NULL;
  }

  length = (size_t)file_length;
  if (fseek(file, 0L, SEEK_SET) != 0) {
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not rewind prompt resource: %s",
                                path);
    return NULL;
  }

  buffer = (char *)malloc(length + 1U);
  if (buffer == NULL) {
    fclose(file);
    strappy_set_error(error_out, "Could not allocate prompt resource.");
    return NULL;
  }

  bytes_read = fread(buffer, 1U, length, file);
  if ((bytes_read != length) || ferror(file)) {
    free(buffer);
    fclose(file);
    strappy_set_formatted_error(error_out,
                                "Could not read prompt resource: %s",
                                path);
    return NULL;
  }

  buffer[length] = '\0';
  fclose(file);
  return buffer;
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

char *strappy_prompt_build(
  const char *resource_dir,
  const strappy_assistant_set_profile *profile,
  int web_search_enabled,
  char **error_out)
{
  strappy_prompt_buffer buffer;
  char *tools_markdown;
  char *invariant;
  size_t index;
  int ok;

  buffer.data = NULL;
  buffer.length = 0U;
  tools_markdown = NULL;
  invariant = NULL;
  if ((resource_dir == NULL) || (resource_dir[0] == '\0') ||
      (profile == NULL) || (profile->goal == NULL) ||
      (profile->goal[0] == '\0') || (profile->tool_names == NULL) ||
      (profile->tool_name_count == 0U) ||
      (profile->quality_check_keys == NULL) ||
      (profile->quality_check_key_count == 0U)) {
    strappy_set_error(error_out,
                      "System prompt configuration is incomplete.");
    return NULL;
  }

  tools_markdown = strappy_tools_prompt_markdown_filtered(
    resource_dir,
    (const char * const *)profile->tool_names,
    profile->tool_name_count,
    web_search_enabled,
    error_out);
  if (tools_markdown == NULL) {
    return NULL;
  }
  invariant = strappy_prompt_render_resource(
    resource_dir,
    STRAPPY_PROMPT_INVARIANT_RESOURCE_NAME,
    error_out);
  if ((invariant == NULL) || (invariant[0] == '\0')) {
    free(tools_markdown);
    free(invariant);
    if ((error_out == NULL) || (*error_out == NULL)) {
      strappy_set_error(error_out, "Invariant prompt resource is empty.");
    }
    return NULL;
  }

  ok = strappy_prompt_buffer_append(
         &buffer,
         "# Tools available\n\n"
         "Only the following tools are available for this round. Use their "
         "arguments exactly as described. Complete tool work in a compact "
         "sequence without progress updates, partial answers, or promises "
         "to continue. After the last tool completes, immediately return one "
         "complete, self-contained final answer containing the requested "
         "result. Merely saying that a result is ready is not an answer.\n\n") &&
    strappy_prompt_buffer_append(&buffer, tools_markdown) &&
    strappy_prompt_buffer_append(
      &buffer,
      "\n# Audit checks for this round\n\n"
      "The app evaluates the following checks once after your final answer. "
      "The report is informational and never causes an automatic retry. Do "
      "not treat that as permission to ignore an applicable requirement: "
      "satisfy every REQUIRED check and every CONDITIONAL check whose "
      "condition applies before you finalize. Never invent data or tool "
      "arguments to pass a check. Skip only an inapplicable CONDITIONAL "
      "action, even though its informational check may then fail. Do not "
      "mention this audit in your final answer.\n\n");

  for (index = 0U; ok && (index < profile->quality_check_key_count); index++) {
    const strappy_quality_check_definition *definition;

    definition = strappy_quality_policy_find(
      profile->quality_check_keys[index]);
    if (definition == NULL) {
      strappy_set_formatted_error(
        error_out,
        "Answer-quality check is not registered: %s",
        profile->quality_check_keys[index]);
      ok = 0;
      break;
    }
    if ((definition->tool_name != NULL) &&
        !strappy_assistant_set_profile_allows_tool(profile,
                                                   definition->tool_name)) {
      strappy_set_formatted_error(
        error_out,
        "Answer-quality tool is not allowed by the assistant set: %s",
        definition->tool_name);
      ok = 0;
      break;
    }
    ok = strappy_prompt_buffer_append(&buffer, "- `") &&
      strappy_prompt_buffer_append(&buffer, definition->check_key) &&
      strappy_prompt_buffer_append(&buffer, "`: ") &&
      strappy_prompt_buffer_append(&buffer, definition->prompt_guidance) &&
      strappy_prompt_buffer_append(&buffer, "\n");
  }

  if (ok) {
    ok = strappy_prompt_buffer_append(
           &buffer,
           "\n# Assistant contract\n\n## Goal\n\n") &&
      strappy_prompt_buffer_append(&buffer, profile->goal) &&
      strappy_prompt_buffer_append(
        &buffer,
        "\n\n## Personality and HARD rules\n\n") &&
      strappy_prompt_buffer_append(&buffer, invariant) &&
      ((buffer.length > 0U) &&
       ((buffer.data[buffer.length - 1U] == '\n') ||
        strappy_prompt_buffer_append(&buffer, "\n")));
  }

  free(invariant);
  free(tools_markdown);
  if (!ok) {
    strappy_prompt_buffer_destroy(&buffer);
    if ((error_out == NULL) || (*error_out == NULL)) {
      strappy_set_error(error_out, "Could not allocate system prompt.");
    }
    return NULL;
  }
  return buffer.data;
}
