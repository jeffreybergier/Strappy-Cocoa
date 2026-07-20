#include "strappy_assistant_sets.h"
#include "strappy_prompt.h"
#include "strappy_quality_policy.h"
#include "strappy_tools.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *harness_assistant_set_ids[] = {
  STRAPPY_ASSISTANT_SET_WORLD_KNOWLEDGE,
  STRAPPY_ASSISTANT_SET_PERSONAL_ASSISTANT,
  STRAPPY_ASSISTANT_SET_CODING_ASSISTANT
};

static const size_t harness_assistant_set_count =
  sizeof(harness_assistant_set_ids) / sizeof(harness_assistant_set_ids[0]);

static int harness_fail(const char *message)
{
  fprintf(stderr, "%s\n", message);
  return 0;
}

static size_t harness_count_occurrences(const char *text, const char *needle)
{
  size_t count;
  size_t needle_length;
  const char *cursor;

  if ((text == NULL) || (needle == NULL) || (needle[0] == '\0')) {
    return 0U;
  }
  count = 0U;
  needle_length = strlen(needle);
  cursor = text;
  while ((cursor = strstr(cursor, needle)) != NULL) {
    count++;
    cursor += needle_length;
  }
  return count;
}

static size_t harness_count_line_prefixes(const char *begin,
                                          const char *end,
                                          const char *prefix)
{
  size_t count;
  size_t prefix_length;
  const char *cursor;

  if ((begin == NULL) || (end == NULL) || (begin >= end) ||
      (prefix == NULL) || (prefix[0] == '\0')) {
    return 0U;
  }
  count = 0U;
  prefix_length = strlen(prefix);
  cursor = begin;
  while (cursor < end) {
    if (((cursor == begin) || (cursor[-1] == '\n')) &&
        ((size_t)(end - cursor) >= prefix_length) &&
        (strncmp(cursor, prefix, prefix_length) == 0)) {
      count++;
    }
    cursor++;
  }
  return count;
}

static int harness_region_contains(const char *begin,
                                   const char *end,
                                   const char *needle)
{
  const char *match;

  if ((begin == NULL) || (end == NULL) || (needle == NULL)) {
    return 0;
  }
  match = strstr(begin, needle);
  return (match != NULL) && (match < end) &&
    ((size_t)(end - match) >= strlen(needle));
}

static const char *harness_response_tool_name(cJSON *tool)
{
  cJSON *type;
  cJSON *name;

  type = cJSON_IsObject(tool) ?
    cJSON_GetObjectItemCaseSensitive(tool, "type") : NULL;
  if (!cJSON_IsString(type) || (type->valuestring == NULL)) {
    return NULL;
  }
  if (strcmp(type->valuestring, "function") != 0) {
    return type->valuestring;
  }
  name = cJSON_GetObjectItemCaseSensitive(tool, "name");
  return (cJSON_IsString(name) && (name->valuestring != NULL)) ?
    name->valuestring : NULL;
}

static int harness_prompt_has_named_bullet(const char *begin,
                                           const char *end,
                                           const char *name)
{
  size_t length;
  char *needle;
  int found;

  length = strlen(name) + sizeof("- ``: ");
  needle = (char *)malloc(length);
  if (needle == NULL) {
    return 0;
  }
  (void)snprintf(needle, length, "- `%s`: ", name);
  found = harness_region_contains(begin, end, needle);
  free(needle);
  return found;
}

static cJSON *harness_json_object(cJSON *parent, const char *key)
{
  cJSON *value;

  value = cJSON_IsObject(parent) ?
    cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
  return cJSON_IsObject(value) ? value : NULL;
}

static const char *harness_json_string(cJSON *parent, const char *key)
{
  cJSON *value;

  value = cJSON_IsObject(parent) ?
    cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
  return ((value != NULL) && cJSON_IsString(value) &&
          (value->valuestring != NULL) &&
          (value->valuestring[0] != '\0')) ? value->valuestring : NULL;
}

static const char *harness_json_text(cJSON *parent, const char *key)
{
  cJSON *value;

  value = cJSON_IsObject(parent) ?
    cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
  return cJSON_IsString(value) && (value->valuestring != NULL) ?
    value->valuestring : NULL;
}

static const char *harness_section_content_end(cJSON *section,
                                               const char *content_start,
                                               const char *section_end)
{
  const char *footer;
  const char *match;

  footer = harness_json_text(section, "footer");
  if ((footer == NULL) || (content_start == NULL) || (section_end == NULL)) {
    return NULL;
  }
  if (footer[0] == '\0') {
    return section_end;
  }
  match = strstr(content_start, footer);
  return ((match != NULL) && (match < section_end)) ? match : NULL;
}

static int harness_section_copy_surrounds_content(
  cJSON *section,
  const char *section_start,
  const char *content_start,
  const char *content_end,
  const char *section_end)
{
  const char *instruction;
  const char *footer;
  const char *match;

  instruction = harness_json_text(section, "instruction");
  footer = harness_json_text(section, "footer");
  if ((instruction == NULL) || (footer == NULL) ||
      (section_start == NULL) || (content_start == NULL) ||
      (content_end == NULL) || (section_end == NULL) ||
      !(section_start < content_start) || !(content_start <= content_end) ||
      !(content_end <= section_end)) {
    return 0;
  }
  if (instruction[0] != '\0') {
    match = strstr(section_start, instruction);
    if ((match == NULL) || (match >= content_start)) {
      return 0;
    }
  }
  if (footer[0] != '\0') {
    match = strstr(content_end, footer);
    if ((match == NULL) || (match >= section_end)) {
      return 0;
    }
  }
  return 1;
}

static char *harness_uppercase_copy(const char *text)
{
  size_t index;
  size_t length;
  char *result;

  if (text == NULL) {
    return NULL;
  }
  length = strlen(text);
  result = (char *)malloc(length + 1U);
  if (result == NULL) {
    return NULL;
  }
  for (index = 0U; index < length; index++) {
    char value;

    value = text[index];
    result[index] = ((value >= 'a') && (value <= 'z')) ?
      (char)(value - ('a' - 'A')) : value;
  }
  result[length] = '\0';
  return result;
}

static int harness_prompt_has_quality_bullet(
  const char *begin,
  const char *end,
  const char *check_key,
  cJSON *guidance)
{
  const char *requirement;
  const char *instruction;
  char *uppercase;
  size_t length;
  char *needle;
  int found;

  requirement = harness_json_string(guidance, "requirement");
  instruction = harness_json_string(guidance, "instruction");
  uppercase = harness_uppercase_copy(requirement);
  if ((check_key == NULL) || (instruction == NULL) || (uppercase == NULL)) {
    free(uppercase);
    return 0;
  }
  length = strlen(check_key) + strlen(uppercase) + strlen(instruction) +
    sizeof("- ``: : \n");
  needle = (char *)malloc(length);
  if (needle == NULL) {
    free(uppercase);
    return 0;
  }
  (void)snprintf(needle,
                 length,
                 "- `%s`: %s: %s\n",
                 check_key,
                 uppercase,
                 instruction);
  found = harness_region_contains(begin, end, needle);
  free(uppercase);
  free(needle);
  return found;
}

static char *harness_heading_marker(size_t level, const char *heading)
{
  size_t index;
  size_t length;
  char *marker;

  if ((level == 0U) || (heading == NULL)) {
    return NULL;
  }
  length = level + 1U + strlen(heading) + 2U + 1U;
  marker = (char *)malloc(length);
  if (marker == NULL) {
    return NULL;
  }
  for (index = 0U; index < level; index++) {
    marker[index] = '#';
  }
  marker[level] = ' ';
  memcpy(marker + level + 1U, heading, strlen(heading));
  memcpy(marker + level + 1U + strlen(heading), "\n\n", 3U);
  return marker;
}

static int harness_verify_prompt(
  const strappy_assistant_set_profile *profile,
  strappy_web_tool_mode web_tool_mode,
  const char *prompt,
  cJSON *system_prompt,
  const char *tools_json)
{
  cJSON *sections;
  cJSON *tools_section;
  cJSON *audit_section;
  cJSON *goal_section;
  cJSON *invariant_prompt_section;
  cJSON *audit_guidance;
  char *tools_heading;
  char *audit_heading;
  char *goal_heading;
  char *invariant_heading;
  const char *prompt_end;
  const char *tools_start;
  const char *audit_start;
  const char *goal_start;
  const char *invariant_start;
  const char *tools_content_start;
  const char *tools_content_end;
  const char *audit_content_start;
  const char *audit_content_end;
  const char *goal_content_start;
  const char *goal_content_end;
  const char *invariant_content_start;
  const char *invariant_content_end;
  cJSON *tools;
  cJSON *tool;
  size_t index;
  size_t expected_quality_check_count;
  int has_paid_web_search;
  int has_paid_web_fetch;
  int has_custom_web_search;
  int has_custom_web_fetch;
  int result;

  sections = harness_json_object(system_prompt, "sections");
  tools_section = harness_json_object(sections, "tools");
  audit_section = harness_json_object(sections, "audit");
  goal_section = harness_json_object(sections, "goal");
  invariant_prompt_section = harness_json_object(sections, "invariant");
  audit_guidance = harness_json_object(system_prompt, "audit_guidance");
  tools_heading = harness_heading_marker(
    1U,
    harness_json_string(tools_section, "heading"));
  audit_heading = harness_heading_marker(
    1U,
    harness_json_string(audit_section, "heading"));
  goal_heading = harness_heading_marker(
    1U,
    harness_json_string(goal_section, "heading"));
  invariant_heading = harness_heading_marker(
    1U,
    harness_json_string(invariant_prompt_section, "heading"));
  if ((tools_heading == NULL) || (audit_heading == NULL) ||
      (goal_heading == NULL) || (invariant_heading == NULL)) {
    free(invariant_heading);
    free(goal_heading);
    free(audit_heading);
    free(tools_heading);
    return harness_fail("System prompt resource validation failed.");
  }
  result = 0;

  prompt_end = prompt + strlen(prompt);
  tools_start = strstr(prompt, tools_heading);
  audit_start = strstr(prompt, audit_heading);
  goal_start = strstr(prompt, goal_heading);
  invariant_start = strstr(prompt, invariant_heading);
  if ((tools_start == NULL) || (audit_start == NULL) ||
      (goal_start == NULL) || (invariant_start == NULL) ||
      !(tools_start < audit_start) || !(audit_start < goal_start) ||
      !(goal_start < invariant_start) ||
      (harness_count_occurrences(prompt, tools_heading) != 1U) ||
      (harness_count_occurrences(prompt, audit_heading) != 1U) ||
      (harness_count_occurrences(prompt, goal_heading) != 1U) ||
      (harness_count_occurrences(prompt, invariant_heading) != 1U)) {
    (void)harness_fail("Generated prompt section contract is invalid.");
    goto cleanup;
  }

  tools_content_start = strstr(tools_start, "\n- `");
  audit_content_start = strstr(audit_start, "\n- `");
  if (tools_content_start != NULL) {
    tools_content_start++;
  }
  if (audit_content_start != NULL) {
    audit_content_start++;
  }
  tools_content_end = harness_section_content_end(tools_section,
                                                  tools_content_start,
                                                  audit_start);
  audit_content_end = harness_section_content_end(audit_section,
                                                  audit_content_start,
                                                  goal_start);
  goal_content_start = strstr(goal_start, profile->goal);
  goal_content_end = (goal_content_start != NULL) ?
    goal_content_start + strlen(profile->goal) : NULL;
  invariant_content_start = harness_section_content_end(
    invariant_prompt_section,
    invariant_start,
    prompt_end);
  invariant_content_end = invariant_content_start;
  if ((tools_content_end == NULL) || (audit_content_end == NULL) ||
      (goal_content_end == NULL) || (invariant_content_end == NULL) ||
      !harness_section_copy_surrounds_content(tools_section,
                                              tools_start,
                                              tools_content_start,
                                              tools_content_end,
                                              audit_start) ||
      !harness_section_copy_surrounds_content(audit_section,
                                              audit_start,
                                              audit_content_start,
                                              audit_content_end,
                                              goal_start) ||
      !harness_section_copy_surrounds_content(goal_section,
                                              goal_start,
                                              goal_content_start,
                                              goal_content_end,
                                              invariant_start) ||
      !harness_section_copy_surrounds_content(invariant_prompt_section,
                                              invariant_start,
                                              invariant_content_start,
                                              invariant_content_end,
                                              prompt_end)) {
    (void)harness_fail("Generated prompt section copy is misplaced.");
    goto cleanup;
  }

  tools = cJSON_Parse(tools_json);
  if (!cJSON_IsArray(tools) || (cJSON_GetArraySize(tools) <= 0) ||
      (harness_count_line_prefixes(tools_content_start,
                                   tools_content_end,
                                   "- `") !=
       (size_t)cJSON_GetArraySize(tools))) {
    cJSON_Delete(tools);
    (void)harness_fail(
      "Generated prompt tools do not match the Responses tool count.");
    goto cleanup;
  }
  has_paid_web_search = 0;
  has_paid_web_fetch = 0;
  has_custom_web_search = 0;
  has_custom_web_fetch = 0;
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    const char *name;

    name = harness_response_tool_name(tool);
    if ((name == NULL) ||
        !harness_prompt_has_named_bullet(tools_content_start,
                                         tools_content_end,
                                         name)) {
      cJSON_Delete(tools);
      (void)harness_fail(
        "Generated prompt is missing a Responses API tool.");
      goto cleanup;
    }
    if (strcmp(name, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) == 0) {
      has_paid_web_search = 1;
    } else if (strcmp(name, STRAPPY_TOOL_OPENROUTER_WEB_FETCH) == 0) {
      has_paid_web_fetch = 1;
    } else if (strcmp(name, STRAPPY_TOOL_WEB_SEARCH) == 0) {
      has_custom_web_search = 1;
    } else if (strcmp(name, STRAPPY_TOOL_WEB_FETCH) == 0) {
      has_custom_web_fetch = 1;
    }
  }
  cJSON_Delete(tools);
  if (web_tool_mode == STRAPPY_WEB_TOOL_MODE_CUSTOM) {
    if (!has_custom_web_search || !has_custom_web_fetch ||
        has_paid_web_search || has_paid_web_fetch) {
      (void)harness_fail(
        "Custom web mode did not expose only Strappy web tools.");
      goto cleanup;
    }
  } else if (web_tool_mode == STRAPPY_WEB_TOOL_MODE_PAID) {
    if (!has_paid_web_search || !has_paid_web_fetch ||
        has_custom_web_search || has_custom_web_fetch) {
      (void)harness_fail(
        "Paid web mode did not expose only OpenRouter web tools.");
      goto cleanup;
    }
  } else if (has_paid_web_search || has_paid_web_fetch ||
             has_custom_web_search || has_custom_web_fetch ||
             harness_prompt_has_named_bullet(tools_content_start,
                                             tools_content_end,
                                             STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) ||
             harness_prompt_has_named_bullet(tools_content_start,
                                             tools_content_end,
                                             STRAPPY_TOOL_OPENROUTER_WEB_FETCH) ||
             harness_prompt_has_named_bullet(tools_content_start,
                                             tools_content_end,
                                             STRAPPY_TOOL_WEB_SEARCH) ||
             harness_prompt_has_named_bullet(tools_content_start,
                                             tools_content_end,
                                             STRAPPY_TOOL_WEB_FETCH)) {
    (void)harness_fail(
      "Web-disabled prompt unexpectedly includes server tools.");
    goto cleanup;
  }

  expected_quality_check_count = 0U;
  for (index = 0U; index < profile->quality_check_key_count; index++) {
    const strappy_quality_check_definition *definition;
    cJSON *check_guidance;
    int has_quality_bullet;

    definition = strappy_quality_policy_find(
      profile->quality_check_keys[index]);
    check_guidance = cJSON_GetObjectItemCaseSensitive(
      audit_guidance,
      profile->quality_check_keys[index]);
    if ((definition == NULL) || !cJSON_IsObject(check_guidance)) {
      (void)harness_fail("Generated prompt audit guidance is invalid.");
      goto cleanup;
    }
    has_quality_bullet = harness_prompt_has_quality_bullet(
      audit_content_start,
      audit_content_end,
      profile->quality_check_keys[index],
      check_guidance);
    if ((web_tool_mode == STRAPPY_WEB_TOOL_MODE_DISABLED) &&
        (definition->evaluation_kind ==
         STRAPPY_QUALITY_CHECK_WEB_REFERENCE)) {
      if (has_quality_bullet) {
        (void)harness_fail(
          "Web-disabled prompt unexpectedly includes web audit guidance.");
        goto cleanup;
      }
      continue;
    }
    expected_quality_check_count++;
    if (!has_quality_bullet) {
      (void)harness_fail("Generated prompt audit guidance is invalid.");
      goto cleanup;
    }
  }
  if (harness_count_line_prefixes(audit_content_start,
                                  audit_content_end,
                                  "- `") !=
      expected_quality_check_count) {
    (void)harness_fail("Generated prompt audit count is invalid.");
    goto cleanup;
  }
  result = 1;

cleanup:
  free(invariant_heading);
  free(goal_heading);
  free(audit_heading);
  free(tools_heading);
  return result;
}

static const char *harness_web_tool_mode_name(strappy_web_tool_mode mode)
{
  if (mode == STRAPPY_WEB_TOOL_MODE_CUSTOM) {
    return "custom";
  }
  if (mode == STRAPPY_WEB_TOOL_MODE_PAID) {
    return "paid";
  }
  return "disabled";
}

static int harness_write_prompt(const char *output_dir,
                                const char *assistant_set_id,
                                strappy_web_tool_mode web_tool_mode,
                                const char *prompt)
{
  size_t length;
  char *path;
  FILE *file;
  int written;

  length = strlen(output_dir) + strlen(assistant_set_id) +
    sizeof("/-web-disabled.txt");
  path = (char *)malloc(length);
  if (path == NULL) {
    return harness_fail("Could not allocate generated prompt path.");
  }
  (void)snprintf(path,
                 length,
                 "%s/%s-web-%s.txt",
                 output_dir,
                 assistant_set_id,
                 harness_web_tool_mode_name(web_tool_mode));
  file = fopen(path, "wb");
  if (file == NULL) {
    free(path);
    return harness_fail("Could not open generated prompt output file.");
  }
  written = (fwrite(prompt, 1U, strlen(prompt), file) == strlen(prompt));
  if (fclose(file) != 0) {
    written = 0;
  }
  if (written) {
    printf("Generated %s\n", path);
  } else {
    fprintf(stderr, "Could not write generated prompt: %s\n", path);
  }
  free(path);
  return written;
}

int main(int argc, char **argv)
{
  const char *resource_dir;
  const char *output_dir;
  char *system_prompt_text;
  cJSON *system_prompt;
  char *error;
  size_t set_index;
  int check_only;

  if ((argc < 2) || (argc > 3)) {
    fprintf(stderr,
            "Usage: %s RESOURCE_DIR [OUTPUT_DIR|--check]\n",
            argv[0]);
    return 2;
  }
  resource_dir = argv[1];
  output_dir = NULL;
  check_only = 0;
  if (argc == 3) {
    if (strcmp(argv[2], "--check") == 0) {
      check_only = 1;
    } else {
      output_dir = argv[2];
    }
  }

  error = NULL;
  system_prompt_text = strappy_prompt_render_resource(
    resource_dir,
    STRAPPY_SYSTEM_PROMPT_RESOURCE_NAME,
    &error);
  system_prompt = (system_prompt_text != NULL) ?
    cJSON_Parse(system_prompt_text) : NULL;
  free(system_prompt_text);
  if (!cJSON_IsObject(system_prompt)) {
    fprintf(stderr,
            "Could not load system prompt resource: %s\n",
            (error != NULL) ? error : "unknown error");
    cJSON_Delete(system_prompt);
    free(error);
    return 1;
  }

  for (set_index = 0U;
       set_index < harness_assistant_set_count;
       set_index++) {
    strappy_assistant_set_profile profile;
    char *without_web_prompt;
    int web_tool_mode_value;

    strappy_assistant_set_profile_init(&profile);
    error = NULL;
    if (!strappy_assistant_sets_load_profile(
          resource_dir,
          harness_assistant_set_ids[set_index],
          &profile,
          &error)) {
      fprintf(stderr,
              "Could not load assistant set %s: %s\n",
              harness_assistant_set_ids[set_index],
              (error != NULL) ? error : "unknown error");
      free(error);
      cJSON_Delete(system_prompt);
      return 1;
    }
    without_web_prompt = NULL;
    for (web_tool_mode_value = (int)STRAPPY_WEB_TOOL_MODE_DISABLED;
         web_tool_mode_value <= (int)STRAPPY_WEB_TOOL_MODE_PAID;
         web_tool_mode_value++) {
      char *prompt;
      char *tools_json;
      strappy_web_tool_mode web_tool_mode;

      web_tool_mode = (strappy_web_tool_mode)web_tool_mode_value;
      error = NULL;
      prompt = strappy_prompt_build(resource_dir,
                                    &profile,
                                    web_tool_mode,
                                    &error);
      tools_json = (prompt != NULL) ?
        strappy_tools_responses_request_json_filtered(
          resource_dir,
          (const char * const *)profile.tool_names,
          profile.tool_name_count,
          web_tool_mode,
          &error) : NULL;
      if ((prompt == NULL) || (tools_json == NULL) ||
          !harness_verify_prompt(&profile,
                                 web_tool_mode,
                                 prompt,
                                 system_prompt,
                                 tools_json)) {
        fprintf(stderr,
                "Could not generate %s with web search %s: %s\n",
                profile.identifier,
                harness_web_tool_mode_name(web_tool_mode),
                (error != NULL) ? error : "prompt validation failed");
        free(tools_json);
        free(prompt);
        free(without_web_prompt);
        free(error);
        strappy_assistant_set_profile_destroy(&profile);
        cJSON_Delete(system_prompt);
        return 1;
      }
      free(tools_json);
      free(error);
      if (web_tool_mode == STRAPPY_WEB_TOOL_MODE_DISABLED) {
        without_web_prompt = (char *)malloc(strlen(prompt) + 1U);
        if (without_web_prompt != NULL) {
          memcpy(without_web_prompt, prompt, strlen(prompt) + 1U);
        }
        if (without_web_prompt == NULL) {
          free(prompt);
          strappy_assistant_set_profile_destroy(&profile);
          cJSON_Delete(system_prompt);
          (void)harness_fail("Could not retain generated prompt.");
          return 1;
        }
      } else if (strcmp(without_web_prompt, prompt) == 0) {
        free(prompt);
        free(without_web_prompt);
        strappy_assistant_set_profile_destroy(&profile);
        cJSON_Delete(system_prompt);
        (void)harness_fail(
          "Web-enabled and web-disabled prompts must differ.");
        return 1;
      }

      if (output_dir != NULL) {
        if (!harness_write_prompt(output_dir,
                                  profile.identifier,
                                  web_tool_mode,
                                  prompt)) {
          free(prompt);
          free(without_web_prompt);
          strappy_assistant_set_profile_destroy(&profile);
          cJSON_Delete(system_prompt);
          return 1;
        }
      } else if (!check_only) {
        printf("\n===== %s | web search %s =====\n\n%s",
               profile.identifier,
               harness_web_tool_mode_name(web_tool_mode),
               prompt);
      }
      free(prompt);
    }
    free(without_web_prompt);
    strappy_assistant_set_profile_destroy(&profile);
  }
  cJSON_Delete(system_prompt);
  if (check_only) {
    printf("Prompt generator harness passed (9 variants).\n");
  }
  return 0;
}
