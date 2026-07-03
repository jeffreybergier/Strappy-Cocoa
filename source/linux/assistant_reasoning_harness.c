#include <stdio.h>
#include <string.h>

#include "../shared/strappy_assistant.c"

static int harness_fail(const char *message)
{
  fprintf(stderr, "%s\n", message);
  return 0;
}

static int harness_set_reasoning(strappy_chat_result *result,
                                 const char *reasoning)
{
  if (result == NULL) {
    return 0;
  }

  free(result->reasoning_text);
  result->reasoning_text =
    (reasoning != NULL) ? strappy_string_duplicate(reasoning) : NULL;
  return ((reasoning == NULL) || (result->reasoning_text != NULL)) ? 1 : 0;
}

static int harness_test_final_result_gets_accumulated_reasoning(void)
{
  strappy_assistant_tool_sequence sequence;
  strappy_chat_result first;
  strappy_chat_result second;
  strappy_chat_result final;
  char *error;
  int ok;

  strappy_assistant_tool_sequence_init(&sequence);
  strappy_chat_result_init(&first);
  strappy_chat_result_init(&second);
  strappy_chat_result_init(&final);
  error = NULL;

  ok = harness_set_reasoning(&first, "first tool reasoning") &&
       harness_set_reasoning(&second, "second tool reasoning") &&
       harness_set_reasoning(&final, "final answer reasoning") &&
       strappy_assistant_accumulate_result_reasoning(&sequence,
                                                     &first,
                                                     &error) &&
       strappy_assistant_accumulate_result_reasoning(&sequence,
                                                     &second,
                                                     &error) &&
       strappy_assistant_accumulate_result_reasoning(&sequence,
                                                     &final,
                                                     &error) &&
       strappy_assistant_apply_accumulated_reasoning(&sequence,
                                                    &final,
                                                    &error) &&
       (final.reasoning_text != NULL) &&
       (strcmp(final.reasoning_text,
               "first tool reasoning\n\n"
               "second tool reasoning\n\n"
               "final answer reasoning") == 0) &&
       (first.reasoning_text != NULL) &&
       (strcmp(first.reasoning_text, "first tool reasoning") == 0) &&
       (second.reasoning_text != NULL) &&
       (strcmp(second.reasoning_text, "second tool reasoning") == 0);

  if (error != NULL) {
    strappy_free_string(error);
  }
  strappy_chat_result_destroy(&final);
  strappy_chat_result_destroy(&second);
  strappy_chat_result_destroy(&first);
  strappy_assistant_tool_sequence_destroy(&sequence);

  if (!ok) {
    return harness_fail("Final assistant reasoning was not accumulated.");
  }

  return 1;
}

int main(void)
{
  if (!harness_test_final_result_gets_accumulated_reasoning()) {
    fprintf(stderr, "assistant_reasoning_harness failed.\n");
    return 1;
  }

  printf("assistant_reasoning_harness passed.\n");
  return 0;
}
