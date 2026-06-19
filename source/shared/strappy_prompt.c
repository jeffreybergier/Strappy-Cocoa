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

char *strappy_prompt_tools_fragment(char **error_out)
{
  (void)error_out;
  return strappy_string_duplicate("No tools available");
}

static char *strappy_prompt_webview_user_agent_fragment(
  const char *webview_user_agent,
  char **error_out)
{
  const char *value;

  (void)error_out;
  value = webview_user_agent;
  if ((value == NULL) || (value[0] == '\0')) {
    value = "Unknown WebView user agent";
  }

  return strappy_string_duplicate(value);
}

static int strappy_prompt_count_placeholder_occurrences(const char *template_text,
                                                        const char *placeholder,
                                                        size_t *count_out)
{
  const char *cursor;
  size_t placeholder_length;
  size_t count;

  if ((template_text == NULL) || (placeholder == NULL) ||
      (placeholder[0] == '\0') || (count_out == NULL)) {
    return 0;
  }

  placeholder_length = strlen(placeholder);
  cursor = template_text;
  count = 0U;

  while ((cursor = strstr(cursor, placeholder)) != NULL) {
    if (count == (size_t)-1) {
      return 0;
    }
    count++;
    cursor += placeholder_length;
  }

  *count_out = count;
  return 1;
}

static char *strappy_prompt_replace_placeholder(const char *template_text,
                                                const char *placeholder,
                                                const char *replacement,
                                                char **error_out)
{
  const char *cursor;
  const char *next;
  char *rendered;
  char *write_cursor;
  size_t template_length;
  size_t placeholder_length;
  size_t replacement_length;
  size_t occurrence_count;
  size_t rendered_length;

  if ((template_text == NULL) || (placeholder == NULL) ||
      (placeholder[0] == '\0') || (replacement == NULL)) {
    strappy_set_error(error_out, "System prompt template is incomplete.");
    return NULL;
  }

  if (!strappy_prompt_count_placeholder_occurrences(template_text,
                                                    placeholder,
                                                    &occurrence_count)) {
    strappy_set_error(error_out, "Could not inspect system prompt template.");
    return NULL;
  }

  if (occurrence_count == 0U) {
    strappy_set_formatted_error(error_out,
                                "System prompt template does not contain %s.",
                                placeholder);
    return NULL;
  }

  template_length = strlen(template_text);
  placeholder_length = strlen(placeholder);
  replacement_length = strlen(replacement);

  rendered_length = template_length;
  if (replacement_length >= placeholder_length) {
    size_t added_per_occurrence;

    added_per_occurrence = replacement_length - placeholder_length;
    if ((added_per_occurrence != 0U) &&
        (occurrence_count > (((size_t)-1) - rendered_length) /
                              added_per_occurrence)) {
      strappy_set_error(error_out, "Rendered system prompt is too large.");
      return NULL;
    }
    rendered_length += occurrence_count * added_per_occurrence;
  } else {
    rendered_length -= occurrence_count *
                       (placeholder_length - replacement_length);
  }

  if (rendered_length == (size_t)-1) {
    strappy_set_error(error_out, "Rendered system prompt is too large.");
    return NULL;
  }

  rendered = (char *)malloc(rendered_length + 1U);
  if (rendered == NULL) {
    strappy_set_error(error_out, "Could not allocate rendered system prompt.");
    return NULL;
  }

  cursor = template_text;
  write_cursor = rendered;
  while ((next = strstr(cursor, placeholder)) != NULL) {
    size_t literal_length;

    literal_length = (size_t)(next - cursor);
    if (literal_length > 0U) {
      memcpy(write_cursor, cursor, literal_length);
      write_cursor += literal_length;
    }
    if (replacement_length > 0U) {
      memcpy(write_cursor, replacement, replacement_length);
      write_cursor += replacement_length;
    }
    cursor = next + placeholder_length;
  }

  if (*cursor != '\0') {
    size_t tail_length;

    tail_length = strlen(cursor);
    memcpy(write_cursor, cursor, tail_length);
    write_cursor += tail_length;
  }
  *write_cursor = '\0';

  return rendered;
}

char *strappy_prompt_render_system_prompt(const char *template_path,
                                          const char *webview_user_agent,
                                          char **error_out)
{
  char *template_text;
  char *tools;
  char *user_agent;
  char *with_tools;
  char *rendered;

  template_text = strappy_prompt_read_file(template_path, error_out);
  if (template_text == NULL) {
    return NULL;
  }

  tools = strappy_prompt_tools_fragment(error_out);
  if (tools == NULL) {
    free(template_text);
    strappy_set_error(error_out, "Could not build system prompt tool list.");
    return NULL;
  }

  user_agent =
    strappy_prompt_webview_user_agent_fragment(webview_user_agent, error_out);
  if (user_agent == NULL) {
    free(tools);
    free(template_text);
    strappy_set_error(error_out, "Could not build WebView user agent prompt.");
    return NULL;
  }

  with_tools =
    strappy_prompt_replace_placeholder(template_text,
                                       STRAPPY_PROMPT_TOOLS_PLACEHOLDER,
                                       tools,
                                       error_out);
  if (with_tools == NULL) {
    free(user_agent);
    free(tools);
    free(template_text);
    return NULL;
  }

  rendered = strappy_prompt_replace_placeholder(
    with_tools,
    STRAPPY_PROMPT_WEBVIEW_USER_AGENT_PLACEHOLDER,
    user_agent,
    error_out);

  free(with_tools);
  free(user_agent);
  free(tools);
  free(template_text);
  return rendered;
}
