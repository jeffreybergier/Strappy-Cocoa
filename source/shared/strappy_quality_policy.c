#include "strappy_quality_policy.h"

#include "strappy_tools.h"

#include <stddef.h>
#include <string.h>

typedef struct strappy_unicode_range {
  unsigned long first;
  unsigned long last;
} strappy_unicode_range;

/*
 * Unicode 17.0 Emoji=Yes ranges, excluding the ASCII keycap bases #, *, and
 * 0-9. Generated from:
 * https://www.unicode.org/Public/17.0.0/ucd/emoji/emoji-data.txt
 */
static const strappy_unicode_range strappy_unicode_emoji_ranges[] = {
  { 0xA9UL, 0xA9UL },
  { 0xAEUL, 0xAEUL },
  { 0x203CUL, 0x203CUL },
  { 0x2049UL, 0x2049UL },
  { 0x2122UL, 0x2122UL },
  { 0x2139UL, 0x2139UL },
  { 0x2194UL, 0x2199UL },
  { 0x21A9UL, 0x21AAUL },
  { 0x231AUL, 0x231BUL },
  { 0x2328UL, 0x2328UL },
  { 0x23CFUL, 0x23CFUL },
  { 0x23E9UL, 0x23F3UL },
  { 0x23F8UL, 0x23FAUL },
  { 0x24C2UL, 0x24C2UL },
  { 0x25AAUL, 0x25ABUL },
  { 0x25B6UL, 0x25B6UL },
  { 0x25C0UL, 0x25C0UL },
  { 0x25FBUL, 0x25FEUL },
  { 0x2600UL, 0x2604UL },
  { 0x260EUL, 0x260EUL },
  { 0x2611UL, 0x2611UL },
  { 0x2614UL, 0x2615UL },
  { 0x2618UL, 0x2618UL },
  { 0x261DUL, 0x261DUL },
  { 0x2620UL, 0x2620UL },
  { 0x2622UL, 0x2623UL },
  { 0x2626UL, 0x2626UL },
  { 0x262AUL, 0x262AUL },
  { 0x262EUL, 0x262FUL },
  { 0x2638UL, 0x263AUL },
  { 0x2640UL, 0x2640UL },
  { 0x2642UL, 0x2642UL },
  { 0x2648UL, 0x2653UL },
  { 0x265FUL, 0x2660UL },
  { 0x2663UL, 0x2663UL },
  { 0x2665UL, 0x2666UL },
  { 0x2668UL, 0x2668UL },
  { 0x267BUL, 0x267BUL },
  { 0x267EUL, 0x267FUL },
  { 0x2692UL, 0x2697UL },
  { 0x2699UL, 0x2699UL },
  { 0x269BUL, 0x269CUL },
  { 0x26A0UL, 0x26A1UL },
  { 0x26A7UL, 0x26A7UL },
  { 0x26AAUL, 0x26ABUL },
  { 0x26B0UL, 0x26B1UL },
  { 0x26BDUL, 0x26BEUL },
  { 0x26C4UL, 0x26C5UL },
  { 0x26C8UL, 0x26C8UL },
  { 0x26CEUL, 0x26CFUL },
  { 0x26D1UL, 0x26D1UL },
  { 0x26D3UL, 0x26D4UL },
  { 0x26E9UL, 0x26EAUL },
  { 0x26F0UL, 0x26F5UL },
  { 0x26F7UL, 0x26FAUL },
  { 0x26FDUL, 0x26FDUL },
  { 0x2702UL, 0x2702UL },
  { 0x2705UL, 0x2705UL },
  { 0x2708UL, 0x270DUL },
  { 0x270FUL, 0x270FUL },
  { 0x2712UL, 0x2712UL },
  { 0x2714UL, 0x2714UL },
  { 0x2716UL, 0x2716UL },
  { 0x271DUL, 0x271DUL },
  { 0x2721UL, 0x2721UL },
  { 0x2728UL, 0x2728UL },
  { 0x2733UL, 0x2734UL },
  { 0x2744UL, 0x2744UL },
  { 0x2747UL, 0x2747UL },
  { 0x274CUL, 0x274CUL },
  { 0x274EUL, 0x274EUL },
  { 0x2753UL, 0x2755UL },
  { 0x2757UL, 0x2757UL },
  { 0x2763UL, 0x2764UL },
  { 0x2795UL, 0x2797UL },
  { 0x27A1UL, 0x27A1UL },
  { 0x27B0UL, 0x27B0UL },
  { 0x27BFUL, 0x27BFUL },
  { 0x2934UL, 0x2935UL },
  { 0x2B05UL, 0x2B07UL },
  { 0x2B1BUL, 0x2B1CUL },
  { 0x2B50UL, 0x2B50UL },
  { 0x2B55UL, 0x2B55UL },
  { 0x3030UL, 0x3030UL },
  { 0x303DUL, 0x303DUL },
  { 0x3297UL, 0x3297UL },
  { 0x3299UL, 0x3299UL },
  { 0x1F004UL, 0x1F004UL },
  { 0x1F0CFUL, 0x1F0CFUL },
  { 0x1F170UL, 0x1F171UL },
  { 0x1F17EUL, 0x1F17FUL },
  { 0x1F18EUL, 0x1F18EUL },
  { 0x1F191UL, 0x1F19AUL },
  { 0x1F1E6UL, 0x1F1FFUL },
  { 0x1F201UL, 0x1F202UL },
  { 0x1F21AUL, 0x1F21AUL },
  { 0x1F22FUL, 0x1F22FUL },
  { 0x1F232UL, 0x1F23AUL },
  { 0x1F250UL, 0x1F251UL },
  { 0x1F300UL, 0x1F321UL },
  { 0x1F324UL, 0x1F393UL },
  { 0x1F396UL, 0x1F397UL },
  { 0x1F399UL, 0x1F39BUL },
  { 0x1F39EUL, 0x1F3F0UL },
  { 0x1F3F3UL, 0x1F3F5UL },
  { 0x1F3F7UL, 0x1F4FDUL },
  { 0x1F4FFUL, 0x1F53DUL },
  { 0x1F549UL, 0x1F54EUL },
  { 0x1F550UL, 0x1F567UL },
  { 0x1F56FUL, 0x1F570UL },
  { 0x1F573UL, 0x1F57AUL },
  { 0x1F587UL, 0x1F587UL },
  { 0x1F58AUL, 0x1F58DUL },
  { 0x1F590UL, 0x1F590UL },
  { 0x1F595UL, 0x1F596UL },
  { 0x1F5A4UL, 0x1F5A5UL },
  { 0x1F5A8UL, 0x1F5A8UL },
  { 0x1F5B1UL, 0x1F5B2UL },
  { 0x1F5BCUL, 0x1F5BCUL },
  { 0x1F5C2UL, 0x1F5C4UL },
  { 0x1F5D1UL, 0x1F5D3UL },
  { 0x1F5DCUL, 0x1F5DEUL },
  { 0x1F5E1UL, 0x1F5E1UL },
  { 0x1F5E3UL, 0x1F5E3UL },
  { 0x1F5E8UL, 0x1F5E8UL },
  { 0x1F5EFUL, 0x1F5EFUL },
  { 0x1F5F3UL, 0x1F5F3UL },
  { 0x1F5FAUL, 0x1F64FUL },
  { 0x1F680UL, 0x1F6C5UL },
  { 0x1F6CBUL, 0x1F6D2UL },
  { 0x1F6D5UL, 0x1F6D8UL },
  { 0x1F6DCUL, 0x1F6E5UL },
  { 0x1F6E9UL, 0x1F6E9UL },
  { 0x1F6EBUL, 0x1F6ECUL },
  { 0x1F6F0UL, 0x1F6F0UL },
  { 0x1F6F3UL, 0x1F6FCUL },
  { 0x1F7E0UL, 0x1F7EBUL },
  { 0x1F7F0UL, 0x1F7F0UL },
  { 0x1F90CUL, 0x1F93AUL },
  { 0x1F93CUL, 0x1F945UL },
  { 0x1F947UL, 0x1F9FFUL },
  { 0x1FA70UL, 0x1FA7CUL },
  { 0x1FA80UL, 0x1FA8AUL },
  { 0x1FA8EUL, 0x1FAC6UL },
  { 0x1FAC8UL, 0x1FAC8UL },
  { 0x1FACDUL, 0x1FADCUL },
  { 0x1FADFUL, 0x1FAEAUL },
  { 0x1FAEFUL, 0x1FAF8UL }
};

static const strappy_quality_check_definition
strappy_quality_check_definitions[] = {
  {
    "answer_non_empty",
    "answer_content",
    "Answer provided",
    NULL,
    STRAPPY_QUALITY_CHECK_ANSWER_NON_EMPTY
  },
  {
    "unicode_emoji_absent",
    "answer_content",
    "No emoji",
    NULL,
    STRAPPY_QUALITY_CHECK_UNICODE_EMOJI_ABSENT
  },
  {
    "web_reference",
    "answer_content",
    "Source link included",
    NULL,
    STRAPPY_QUALITY_CHECK_WEB_REFERENCE
  },
  {
    "database_context_read",
    "required_tool",
    "Database context checked",
    STRAPPY_TOOL_DATABASE_CONTEXT_READ,
    STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
  },
  {
    "helper_session_name_write",
    "required_tool",
    "Session named",
    STRAPPY_TOOL_HELPER_SESSION_NAME_WRITE,
    STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
  },
  {
    "helper_fontawesome_shortcode_confirm",
    "required_tool",
    "Font Awesome shortcode confirmed",
    STRAPPY_TOOL_HELPER_FONTAWESOME_SHORTCODE_CONFIRM,
    STRAPPY_QUALITY_CHECK_REQUIRED_TOOL
  }
};

static const size_t strappy_quality_check_definition_count =
  sizeof(strappy_quality_check_definitions) /
  sizeof(strappy_quality_check_definitions[0]);

const strappy_quality_check_definition *strappy_quality_policy_find(
  const char *check_key)
{
  size_t index;

  if ((check_key == NULL) || (check_key[0] == '\0')) {
    return NULL;
  }
  for (index = 0U;
       index < strappy_quality_check_definition_count;
       index++) {
    if (strcmp(strappy_quality_check_definitions[index].check_key,
               check_key) == 0) {
      return &strappy_quality_check_definitions[index];
    }
  }
  return NULL;
}

size_t strappy_quality_policy_count(void)
{
  return strappy_quality_check_definition_count;
}

const strappy_quality_check_definition *strappy_quality_policy_at(
  size_t index)
{
  return (index < strappy_quality_check_definition_count) ?
    &strappy_quality_check_definitions[index] : NULL;
}

static int strappy_quality_policy_is_utf8_continuation(unsigned char value)
{
  return (value & 0xC0U) == 0x80U;
}

static size_t strappy_quality_policy_decode_utf8(
  const unsigned char *text,
  unsigned long *code_point_out)
{
  unsigned char first;
  unsigned char second;
  unsigned char third;
  unsigned char fourth;

  first = text[0];
  *code_point_out = (unsigned long)first;
  if (first < 0x80U) {
    return 1U;
  }
  if (text[1] == '\0') {
    return 1U;
  }
  second = text[1];
  if ((first >= 0xC2U) && (first <= 0xDFU) &&
      strappy_quality_policy_is_utf8_continuation(second)) {
    *code_point_out =
      ((unsigned long)(first & 0x1FU) << 6U) |
      (unsigned long)(second & 0x3FU);
    return 2U;
  }
  if (text[2] == '\0') {
    return 1U;
  }
  third = text[2];
  if ((first >= 0xE0U) && (first <= 0xEFU) &&
      strappy_quality_policy_is_utf8_continuation(second) &&
      strappy_quality_policy_is_utf8_continuation(third) &&
      ((first != 0xE0U) || (second >= 0xA0U)) &&
      ((first != 0xEDU) || (second <= 0x9FU))) {
    *code_point_out =
      ((unsigned long)(first & 0x0FU) << 12U) |
      ((unsigned long)(second & 0x3FU) << 6U) |
      (unsigned long)(third & 0x3FU);
    return 3U;
  }
  if (text[3] == '\0') {
    return 1U;
  }
  fourth = text[3];
  if ((first >= 0xF0U) && (first <= 0xF4U) &&
      strappy_quality_policy_is_utf8_continuation(second) &&
      strappy_quality_policy_is_utf8_continuation(third) &&
      strappy_quality_policy_is_utf8_continuation(fourth) &&
      ((first != 0xF0U) || (second >= 0x90U)) &&
      ((first != 0xF4U) || (second <= 0x8FU))) {
    *code_point_out =
      ((unsigned long)(first & 0x07U) << 18U) |
      ((unsigned long)(second & 0x3FU) << 12U) |
      ((unsigned long)(third & 0x3FU) << 6U) |
      (unsigned long)(fourth & 0x3FU);
    return 4U;
  }
  return 1U;
}

static int strappy_quality_policy_code_point_is_emoji(
  unsigned long code_point)
{
  size_t lower;
  size_t upper;

  /* VS16 and the keycap combining mark make an ASCII base emoji-presented. */
  if ((code_point == 0xFE0FUL) || (code_point == 0x20E3UL)) {
    return 1;
  }
  lower = 0U;
  upper = sizeof(strappy_unicode_emoji_ranges) /
    sizeof(strappy_unicode_emoji_ranges[0]);
  while (lower < upper) {
    size_t middle;
    const strappy_unicode_range *range;

    middle = lower + ((upper - lower) / 2U);
    range = &strappy_unicode_emoji_ranges[middle];
    if (code_point < range->first) {
      upper = middle;
    } else if (code_point > range->last) {
      lower = middle + 1U;
    } else {
      return 1;
    }
  }
  return 0;
}

int strappy_quality_policy_text_has_unicode_emoji(const char *text)
{
  const unsigned char *cursor;

  if (text == NULL) {
    return 0;
  }
  cursor = (const unsigned char *)text;
  while (*cursor != '\0') {
    unsigned long code_point;
    size_t width;

    if (*cursor < 0x80U) {
      cursor++;
      continue;
    }
    width = strappy_quality_policy_decode_utf8(cursor, &code_point);
    if (strappy_quality_policy_code_point_is_emoji(code_point)) {
      return 1;
    }
    cursor += width;
  }
  return 0;
}
