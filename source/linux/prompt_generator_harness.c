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

static int harness_prompt_has_quality_bullet(
  const char *begin,
  const char *end,
  const strappy_quality_check_definition *definition)
{
  size_t length;
  char *needle;
  int found;

  length = strlen(definition->check_key) +
    strlen(definition->prompt_guidance) + sizeof("- ``: \n");
  needle = (char *)malloc(length);
  if (needle == NULL) {
    return 0;
  }
  (void)snprintf(needle,
                 length,
                 "- `%s`: %s\n",
                 definition->check_key,
                 definition->prompt_guidance);
  found = harness_region_contains(begin, end, needle);
  free(needle);
  return found;
}

static int harness_verify_prompt(
  const strappy_assistant_set_profile *profile,
  int web_search_enabled,
  const char *prompt,
  const char *invariant,
  const char *tools_json)
{
  static const char tools_heading[] = "# Tools available\n";
  static const char audit_heading[] = "# Audit checks for this round\n";
  static const char contract_heading[] = "# Assistant contract\n";
  static const char goal_heading[] = "## Goal\n\n";
  static const char personality_heading[] =
    "## Personality and HARD rules\n\n";
  const char *tools_start;
  const char *audit_start;
  const char *contract_start;
  const char *goal_start;
  const char *personality_start;
  cJSON *tools;
  cJSON *tool;
  size_t index;
  int has_web_search;
  int has_web_fetch;

  tools_start = strstr(prompt, tools_heading);
  audit_start = strstr(prompt, audit_heading);
  contract_start = strstr(prompt, contract_heading);
  goal_start = strstr(prompt, goal_heading);
  personality_start = strstr(prompt, personality_heading);
  if ((tools_start == NULL) || (audit_start == NULL) ||
      (contract_start == NULL) || (goal_start == NULL) ||
      (personality_start == NULL) || !(tools_start < audit_start) ||
      !(audit_start < contract_start) || !(contract_start < goal_start) ||
      !(goal_start < personality_start) ||
      (harness_count_occurrences(prompt, tools_heading) != 1U) ||
      (harness_count_occurrences(prompt, audit_heading) != 1U) ||
      (harness_count_occurrences(prompt, contract_heading) != 1U) ||
      !harness_region_contains(goal_start,
                               personality_start,
                               profile->goal) ||
      (strcmp(personality_start + strlen(personality_heading),
              invariant) != 0) ||
      !harness_region_contains(
        audit_start,
        contract_start,
        "The report is informational and never causes an automatic retry") ||
      !harness_region_contains(
        audit_start,
        contract_start,
        "Skip only an inapplicable CONDITIONAL action") ||
      !harness_region_contains(
        tools_start,
        audit_start,
        "Complete tool work in a compact sequence without progress updates")) {
    return harness_fail("Generated prompt section contract is invalid.");
  }

  tools = cJSON_Parse(tools_json);
  if (!cJSON_IsArray(tools) || (cJSON_GetArraySize(tools) <= 0) ||
      (harness_count_line_prefixes(tools_start,
                                   audit_start,
                                   "- `") !=
       (size_t)cJSON_GetArraySize(tools))) {
    cJSON_Delete(tools);
    return harness_fail(
      "Generated prompt tools do not match the Responses tool count.");
  }
  has_web_search = 0;
  has_web_fetch = 0;
  for (tool = tools->child; tool != NULL; tool = tool->next) {
    const char *name;

    name = harness_response_tool_name(tool);
    if ((name == NULL) ||
        !harness_prompt_has_named_bullet(tools_start, audit_start, name)) {
      cJSON_Delete(tools);
      return harness_fail(
        "Generated prompt is missing a Responses API tool.");
    }
    if (strcmp(name, STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) == 0) {
      has_web_search = 1;
    } else if (strcmp(name, STRAPPY_TOOL_OPENROUTER_WEB_FETCH) == 0) {
      has_web_fetch = 1;
    }
  }
  cJSON_Delete(tools);
  if (web_search_enabled) {
    if (!has_web_search || !has_web_fetch) {
      return harness_fail(
        "Web-enabled prompt is missing its server tools.");
    }
  } else if (has_web_search || has_web_fetch ||
             harness_prompt_has_named_bullet(tools_start,
                                             audit_start,
                                             STRAPPY_TOOL_OPENROUTER_WEB_SEARCH) ||
             harness_prompt_has_named_bullet(tools_start,
                                             audit_start,
                                             STRAPPY_TOOL_OPENROUTER_WEB_FETCH)) {
    return harness_fail(
      "Web-disabled prompt unexpectedly includes server tools.");
  }

  if (harness_count_line_prefixes(audit_start,
                                  contract_start,
                                  "- `") !=
      profile->quality_check_key_count) {
    return harness_fail("Generated prompt audit count is invalid.");
  }
  for (index = 0U; index < profile->quality_check_key_count; index++) {
    const strappy_quality_check_definition *definition;

    definition = strappy_quality_policy_find(
      profile->quality_check_keys[index]);
    if ((definition == NULL) ||
        !harness_prompt_has_quality_bullet(audit_start,
                                           contract_start,
                                           definition)) {
      return harness_fail("Generated prompt audit guidance is invalid.");
    }
  }
  return 1;
}

static int harness_write_prompt(const char *output_dir,
                                const char *assistant_set_id,
                                int web_search_enabled,
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
                 web_search_enabled ? "enabled" : "disabled");
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
  char *invariant;
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
  invariant = strappy_prompt_render_resource(
    resource_dir,
    STRAPPY_PROMPT_INVARIANT_RESOURCE_NAME,
    &error);
  if (invariant == NULL) {
    fprintf(stderr,
            "Could not load invariant prompt: %s\n",
            (error != NULL) ? error : "unknown error");
    free(error);
    return 1;
  }

  for (set_index = 0U;
       set_index < harness_assistant_set_count;
       set_index++) {
    strappy_assistant_set_profile profile;
    char *without_web_prompt;
    int web_search_enabled;

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
      free(invariant);
      return 1;
    }
    without_web_prompt = NULL;
    for (web_search_enabled = 0;
         web_search_enabled <= 1;
         web_search_enabled++) {
      char *prompt;
      char *tools_json;

      error = NULL;
      prompt = strappy_prompt_build(resource_dir,
                                    &profile,
                                    web_search_enabled,
                                    &error);
      tools_json = (prompt != NULL) ?
        strappy_tools_responses_request_json_filtered(
          resource_dir,
          (const char * const *)profile.tool_names,
          profile.tool_name_count,
          web_search_enabled,
          &error) : NULL;
      if ((prompt == NULL) || (tools_json == NULL) ||
          !harness_verify_prompt(&profile,
                                 web_search_enabled,
                                 prompt,
                                 invariant,
                                 tools_json)) {
        fprintf(stderr,
                "Could not generate %s with web search %s: %s\n",
                profile.identifier,
                web_search_enabled ? "enabled" : "disabled",
                (error != NULL) ? error : "prompt validation failed");
        free(tools_json);
        free(prompt);
        free(without_web_prompt);
        free(error);
        strappy_assistant_set_profile_destroy(&profile);
        free(invariant);
        return 1;
      }
      free(tools_json);
      free(error);
      if (!web_search_enabled) {
        without_web_prompt = (char *)malloc(strlen(prompt) + 1U);
        if (without_web_prompt != NULL) {
          memcpy(without_web_prompt, prompt, strlen(prompt) + 1U);
        }
        if (without_web_prompt == NULL) {
          free(prompt);
          strappy_assistant_set_profile_destroy(&profile);
          free(invariant);
          (void)harness_fail("Could not retain generated prompt.");
          return 1;
        }
      } else if (strcmp(without_web_prompt, prompt) == 0) {
        free(prompt);
        free(without_web_prompt);
        strappy_assistant_set_profile_destroy(&profile);
        free(invariant);
        (void)harness_fail(
          "Web-enabled and web-disabled prompts must differ.");
        return 1;
      }

      if (output_dir != NULL) {
        if (!harness_write_prompt(output_dir,
                                  profile.identifier,
                                  web_search_enabled,
                                  prompt)) {
          free(prompt);
          free(without_web_prompt);
          strappy_assistant_set_profile_destroy(&profile);
          free(invariant);
          return 1;
        }
      } else if (!check_only) {
        printf("\n===== %s | web search %s =====\n\n%s",
               profile.identifier,
               web_search_enabled ? "enabled" : "disabled",
               prompt);
      }
      free(prompt);
    }
    free(without_web_prompt);
    strappy_assistant_set_profile_destroy(&profile);
  }
  free(invariant);
  if (check_only) {
    printf("Prompt generator harness passed (6 variants).\n");
  }
  return 0;
}
