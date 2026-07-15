#include "strappy_webview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char harness_tool_display_registry_json[] =
  "{\"database_query\":{\"promoted_argument\":\"database_id\","
  "\"transform\":\"database_filename\"},"
  "\"helper_fontawesome_shortcode_confirm\":{"
  "\"promoted_argument\":\"shortcodes\","
  "\"transform\":\"comma_separated\"},"
  "\"memory_database_hint_forget\":{\"promoted_argument\":\"id\","
  "\"transform\":\"database_hint_filename\"},"
  "\"openrouter:web_search\":{\"label\":\"Web Search\","
  "\"promoted_path\":[\"action\",\"query\"],\"response_item\":true},"
  "\"openrouter:web_fetch\":{\"label\":\"Web Fetch\","
  "\"promoted_path\":[\"url\"],\"transform\":\"url\","
  "\"response_item\":true}}";

static int harness_expect_contains(const char *text, const char *needle)
{
  if ((text != NULL) && (needle != NULL) && (strstr(text, needle) != NULL)) {
    return 1;
  }

  fprintf(stderr, "Expected generated webview output to contain: %s\n",
          (needle != NULL) ? needle : "(null)");
  return 0;
}

static int harness_expect_not_contains(const char *text, const char *needle)
{
  if ((text == NULL) || (needle == NULL) || (strstr(text, needle) == NULL)) {
    return 1;
  }

  fprintf(stderr, "Expected generated webview output not to contain: %s\n",
          needle);
  return 0;
}

static int harness_expect_equal(const char *actual, const char *expected)
{
  if ((actual != NULL) && (expected != NULL) &&
      (strcmp(actual, expected) == 0)) {
    return 1;
  }

  fprintf(stderr,
          "Expected generated webview value '%s', got '%s'.\n",
          (expected != NULL) ? expected : "(null)",
          (actual != NULL) ? actual : "(null)");
  return 0;
}

static int harness_check_localized_labels(void)
{
  const strappy_webview_labels *labels;
  char *error;
  int ok;

  error = NULL;
  if (!strappy_webview_configure_localized_labels(&error)) {
    fprintf(stderr,
            "Could not configure webview localization: %s\n",
            (error != NULL) ? error : "unknown error");
    strappy_webview_free(error);
    return 0;
  }

  labels = strappy_webview_localized_labels();
  ok = (labels != NULL) &&
       harness_expect_equal(labels->agent, "Agent") &&
       harness_expect_equal(labels->you, "You") &&
       harness_expect_equal(labels->harness, "Harness") &&
       harness_expect_equal(labels->developer, "Developer") &&
       harness_expect_equal(labels->thinking, "Thinking") &&
       harness_expect_equal(labels->request_metadata, "Request Metadata") &&
       harness_expect_equal(labels->tool, "Tool") &&
       harness_expect_equal(labels->tool_call, "Tool Call") &&
       harness_expect_equal(labels->tool_result, "Tool Result") &&
       harness_expect_equal(labels->retry, "Retry") &&
       harness_expect_equal(labels->api_call, "API Call") &&
       harness_expect_equal(labels->api_error, "API Error") &&
       harness_expect_equal(labels->response_item, "Response Item") &&
       harness_expect_equal(labels->request, "Request") &&
       harness_expect_equal(labels->response, "Response") &&
       harness_expect_equal(labels->round, "Round") &&
       harness_expect_equal(labels->attempt, "Attempt");
  strappy_webview_free(error);
  return ok;
}

static int harness_check_message_batch(void)
{
  strappy_webview_message messages[2];
  char *first_html;
  char *second_html;
  char *batch_html;
  char *empty_html;
  char *invalid_html;
  char *expected_html;
  size_t first_length;
  size_t second_length;
  int ok;

  memset(messages, 0, sizeof(messages));
  messages[0].element_id = "batch-user-1";
  messages[0].role = "user";
  messages[0].text = "First batch message";
  messages[1].element_id = "batch-assistant-2";
  messages[1].role = "assistant";
  messages[1].text = "Second batch message";

  first_html = strappy_webview_message_html(&messages[0], NULL, NULL, NULL);
  second_html = strappy_webview_message_html(&messages[1], NULL, NULL, NULL);
  batch_html = strappy_webview_messages_html(messages, 2U, NULL);
  empty_html = strappy_webview_messages_html(NULL, 0U, NULL);
  invalid_html = strappy_webview_messages_html(NULL, 1U, NULL);
  expected_html = NULL;

  if ((first_html != NULL) && (second_html != NULL)) {
    first_length = strlen(first_html);
    second_length = strlen(second_html);
    expected_html = (char *)malloc(first_length + second_length + 1U);
    if (expected_html != NULL) {
      memcpy(expected_html, first_html, first_length);
      memcpy(expected_html + first_length, second_html, second_length + 1U);
    }
  }

  ok = (expected_html != NULL) &&
       harness_expect_equal(batch_html, expected_html) &&
       harness_expect_equal(empty_html, "") &&
       (invalid_html == NULL);
  if (invalid_html != NULL) {
    fprintf(stderr,
            "Expected a non-empty WebView batch with no messages to fail.\n");
  }

  free(expected_html);
  strappy_webview_free(invalid_html);
  strappy_webview_free(empty_html);
  strappy_webview_free(batch_html);
  strappy_webview_free(second_html);
  strappy_webview_free(first_html);
  return ok;
}

static int harness_check_page_scripts(void)
{
  strappy_webview_message message;
  char *message_html;
  char *page_html;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "assistant-1";
  message.role = "assistant";
  message.actor = "user";
  message.prompt_group_key = "prompt-group-page";
  message.text = "Done.";

  message_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (message_html == NULL) {
    fprintf(stderr, "Could not generate assistant message HTML.\n");
    return 0;
  }

  page_html = strappy_webview_messages_page_html(
    message_html,
    harness_tool_display_registry_json,
    "");
  strappy_webview_free(message_html);
  if (page_html == NULL) {
    fprintf(stderr, "Could not generate messages page HTML.\n");
    return 0;
  }
  ok = harness_expect_contains(page_html, "@font-face") &&
       harness_expect_contains(page_html,
                               "<meta name=\"viewport\" "
                               "content=\"width=device-width,initial-scale=1.0\">") &&
       harness_expect_contains(page_html,
                               "font:12px/1.38 -apple-system,'Helvetica Neue',"
                               "Helvetica,Arial,sans-serif;") &&
       harness_expect_contains(page_html, "letter-spacing:0;") &&
       harness_expect_contains(page_html,
                               "@-webkit-keyframes strappy-round-fade{"
                               "from{opacity:0;}to{opacity:1;}}") &&
       harness_expect_contains(page_html,
                               ".row-inserting{-webkit-animation:"
                               "strappy-round-fade .3s ease-out both;}") &&
       harness_expect_not_contains(page_html,
                                   "@-webkit-keyframes strappy-row-rise") &&
       harness_expect_not_contains(page_html, "translateY(") &&
       harness_expect_not_contains(page_html, "row-insert-pending") &&
       harness_expect_not_contains(page_html, "function scheduleInsertedRows") &&
       harness_expect_not_contains(page_html, "function insertedRowsActive") &&
       harness_expect_not_contains(page_html, "function layoutHeight") &&
       harness_expect_not_contains(page_html, ".load-more{") &&
       harness_expect_not_contains(page_html, "id=\"load-more\"") &&
       harness_expect_not_contains(page_html, "strappy-action://load-more") &&
       harness_expect_not_contains(page_html, "function prependMessages") &&
       harness_expect_not_contains(page_html, "id=\"timeline-error\"") &&
       harness_expect_contains(page_html,
                               "function clearTimelineError(){"
                               "var e=byId('timeline-error');") &&
       harness_expect_contains(page_html,
                               "function appendMessage(html){"
                               "clearTimelineError();") &&
       harness_expect_contains(page_html,
                               ".api-tool-card .tool-card-body{"
                               "font-family:inherit;") &&
       harness_expect_contains(page_html,
                               "font-family:Menlo,Monaco,'Courier New',"
                               "monospace;") &&
       harness_expect_contains(page_html,
                               "-webkit-text-size-adjust:none;") &&
       harness_expect_contains(page_html,
                               ".tool-panel{line-height:1.3;") &&
       harness_expect_contains(page_html, "function faIconHTML") &&
       harness_expect_contains(page_html, "function disclosureIconHTML") &&
       harness_expect_contains(page_html,
                               "collapsed?'angle-right':'angle-down'") &&
       harness_expect_contains(page_html, "function toolErrorIconHTML") &&
       harness_expect_contains(page_html, "triangle-exclamation") &&
       harness_expect_contains(page_html, ".tool-error-icon{") &&
       harness_expect_not_contains(page_html,
                                   ".tool-column-error{border-top-color:#d99") &&
       harness_expect_not_contains(page_html,
                                   ".tool-error{border-color:#d99") &&
       harness_expect_not_contains(page_html,
                                   ".api-exchange-row .tool-error{") &&
       harness_expect_contains(page_html, "\"heart\":'F004'") &&
       harness_expect_contains(page_html, "[fa(?::(solid|regular|brands))?") &&
       harness_expect_contains(page_html, "function toolJSONHTML") &&
       harness_expect_contains(page_html, "function toolObjectArrayTable") &&
       harness_expect_contains(page_html, "function toolOutputHasError") &&
       harness_expect_contains(page_html, "function renderAPIToolRows") &&
       harness_expect_contains(page_html, "function apiToolLabel(row)") &&
       harness_expect_contains(page_html,
                               "setToolCardSummary(summary,apiToolLabel(row)+"
                               "': '+title,0)") &&
       harness_expect_contains(page_html,
                               "setToolCardSummary(summary,apiToolLabel(row)+"
                               "': '+title,error)") &&
       harness_expect_contains(page_html, "function apiToolRows") &&
       harness_expect_contains(page_html, "function decorateAPIToolGroups") &&
       harness_expect_not_contains(page_html, "function toggleAPIToolGroup") &&
       harness_expect_contains(page_html, "function decorateAPIExchanges") &&
       harness_expect_contains(page_html, "function toggleAPIExchange") &&
       harness_expect_contains(page_html,
                               "h.className='api-exchange-turn-header "
                               "disclosure-title'") &&
       harness_expect_contains(page_html,
                               "h.onclick=function(){return "
                               "toggleAPIExchange(a);};") &&
       harness_expect_not_contains(page_html,
                                   "a.onclick=function(){return "
                                   "toggleAPIExchange(this);") &&
       harness_expect_contains(page_html,
                               "role.onclick=function(){return "
                               "togglePromptGroup(a);};") &&
       harness_expect_contains(page_html,
                               "a=firstByClass(role,'prompt-group-toggle');"
                               "if(!a)return;role.onclick=null;") &&
       harness_expect_not_contains(page_html,
                                   "toggleAPIToolGroup(a)") &&
       harness_expect_contains(page_html,
                               "tool-rail-title disclosure-title") &&
       harness_expect_contains(page_html,
                               "tool-card-toggle disclosure-title") &&
       harness_expect_not_contains(page_html, "function toggleAPIExchangeSection") &&
       harness_expect_contains(page_html, "function toggleAPIReasoning") &&
       harness_expect_contains(page_html, ".api-reasoning-collapsed>.bubble{") &&
       harness_expect_contains(page_html, ".api-exchange-turn-header{") &&
       harness_expect_contains(page_html,
                               ".api-exchange-turn-header{margin:0 -10px;"
                               "padding:7px 10px 6px;") &&
       harness_expect_contains(page_html,
                               ".disclosure-title{cursor:pointer;"
                               "-webkit-tap-highlight-color:rgba(0,0,0,.08);}") &&
       harness_expect_contains(page_html,
                               ".tool-column-disclosure,"
                               ".prompt-group-disclosure,.tool-disclosure,"
                               ".reasoning-disclosure,.metadata-disclosure,"
                               ".api-reasoning-disclosure{display:inline-block;"
                               "box-sizing:border-box;width:12px;"
                               "margin-right:4px;font-size:12px;line-height:1;"
                               "vertical-align:baseline;text-align:center;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-disclosure{display:inline-block;"
                               "box-sizing:border-box;width:16px;"
                               "margin-right:4px;font-size:14px;line-height:1;"
                               "vertical-align:baseline;text-align:center;}") &&
       harness_expect_not_contains(page_html,
                                   ".disclosure-title .fa-angle-right") &&
       harness_expect_contains(page_html,
                               "line-height:1;vertical-align:-.08em;}") &&
       harness_expect_contains(page_html,
                               ".request-metadata-toggle{color:#4e5961;"
                               "text-decoration:none;}") &&
       harness_expect_contains(page_html,
                               ".tool-column-toggle{color:#4e5961;"
                               "text-decoration:none;}") &&
       harness_expect_contains(page_html,
                               ".processing-status-active .disclosure-title{"
                               "cursor:default;") &&
       harness_expect_contains(page_html,
                               ".tool-rail-title{font-size:12px;font-weight:bold;"
                               "line-height:1.3;color:#30363b;margin:0 0 8px;}") &&
       harness_expect_contains(page_html,
                               ".reasoning-label{font-size:12px;font-weight:bold;"
                               "line-height:1.3;color:#30363b;margin:0 0 8px;}") &&
       harness_expect_contains(page_html,
                               ".request-metadata-title{font-family:inherit;"
                               "font-size:12px;line-height:1.3;font-weight:bold;"
                               "color:#30363b;margin:0 0 8px;white-space:nowrap;"
                               "overflow:hidden;text-overflow:ellipsis;}") &&
       harness_expect_not_contains(page_html,
                                   ".request-metadata-summary{") &&
       harness_expect_contains(page_html,
                               ".tool-card-summary{vertical-align:baseline;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-turn-title{"
                               "vertical-align:baseline;}") &&
       harness_expect_contains(page_html,
                               ".user .role{background:#c1c8ce;color:#30363b;"
                               "margin:0 -12px;padding:8px 12px;") &&
       harness_expect_contains(page_html,
                               ".harness .role,.developer .role{background:#c1c8ce;"
                               "color:#30363b;margin:0 -12px;"
                               "padding:8px 12px;") &&
       harness_expect_contains(page_html,
                               ".api-exchange-item .role{margin:0 -10px;"
                               "padding:5px 10px;}") &&
       harness_expect_contains(page_html, ".api-exchange-metadata{") &&
       harness_expect_not_contains(page_html, ".api-exchange-section-toggle{") &&
       harness_expect_not_contains(page_html,
                                   ".api-exchange-section-collapsed-row{") &&
       harness_expect_not_contains(page_html, "api-tool-group-toggle") &&
       harness_expect_not_contains(page_html, "api-tool-group-collapsed") &&
       harness_expect_contains(page_html,
                               ".api-tool-group-secondary>.role{display:none;}") &&
       harness_expect_not_contains(page_html,
                                   ".api-tool-card>.tool-card-body{display:block;}") &&
       harness_expect_contains(page_html,
                               ".tool-card-body{display:none;") &&
       harness_expect_contains(page_html,
                               ".tool-card-open .tool-card-body{display:block;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-row{position:relative;border-top:0;"
                               "background:#b2bbc2;color:#30363b;"
                               "padding:0 10px;}") &&
       harness_expect_not_contains(page_html, "border-left:5px solid") &&
       harness_expect_contains(page_html,
                               ".api-exchange-row:before{content:'';"
                               "position:absolute;left:0;right:0;top:0;"
                               "height:1px;background:#68727a;"
                               "pointer-events:none;display:none;z-index:1;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-start{margin-top:0;"
                               "border-top:0;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-start:before{display:block;}") &&
       harness_expect_not_contains(page_html,
                                   "height:1px;background:#cbd2d8;") &&
       harness_expect_contains(page_html,
                               ".api-exchange-end{margin-bottom:0;"
                               "border-bottom:0;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-row>.request-metadata{"
                               "border-top:0;border-bottom:0;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-metadata>.request-metadata{"
                               "margin:0 -10px;padding:8px 10px;"
                               "border-bottom:0;}") &&
       harness_expect_not_contains(page_html,
                                   "function apiExchangeColorClass") &&
       harness_expect_not_contains(page_html, "api-exchange-color-") &&
       harness_expect_contains(page_html,
                               "border-bottom:1px solid #959fa7;"
                               "background:#a3adb5;color:#30363b;") &&
       harness_expect_contains(page_html,
                               ".api-exchange-section-label{margin:0 -10px;"
                               "padding:4px 10px;font-size:12px;"
                               "line-height:1.2;font-weight:bold;"
                               "color:#30363b;background:#c1c8ce;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-row>.role{background:#c1c8ce;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-row.api_reasoning>.role{"
                               "background:#dfe4e8;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-row>.bubble,"
                               ".api-exchange-row>.reasoning,"
                               ".api-exchange-row>.request-metadata{"
                               "background:#dfe4e8;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-row>.tool-column{"
                               "background:#c1c8ce;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-row .tool-card-body,"
                               ".api-exchange-row .tool-table-wrap,"
                               ".api-exchange-row .tool-pill,"
                               ".api-exchange-row .tool-raw{"
                               "background:#dfe4e8;border-color:#959fa7;}") &&
       harness_expect_not_contains(page_html, "#4f7f9e") &&
       harness_expect_not_contains(page_html, "#7b6995") &&
       harness_expect_not_contains(page_html, "#4e8375") &&
       harness_expect_not_contains(page_html, "#92743d") &&
       harness_expect_not_contains(page_html, "#956875") &&
       harness_expect_not_contains(page_html, "#788552") &&
       harness_expect_not_contains(page_html, "#eef7fc") &&
       harness_expect_not_contains(page_html, "#fffdf2") &&
       harness_expect_not_contains(page_html, "#f7fbff") &&
       harness_expect_not_contains(page_html, "#fffbed") &&
       harness_expect_not_contains(page_html, "#fff9e8") &&
       harness_expect_not_contains(page_html, "#f2faf5") &&
       harness_expect_not_contains(page_html, "#fff2f2") &&
       harness_expect_not_contains(page_html, "#fff8e3") &&
       harness_expect_not_contains(page_html, "#8a2525") &&
       harness_expect_not_contains(page_html, "#a22") &&
       harness_expect_contains(page_html,
                               ".assistant .bubble{background:#fff;"
                               "border-top-color:#959fa7;"
                               "border-bottom-color:#959fa7;"
                               "font-size:16px;line-height:1.45;}") &&
       harness_expect_contains(page_html,
                               ".user .bubble{background:#fff;border-top:0;"
                               "border-bottom:0;padding-top:4px;"
                               "font-size:16px;line-height:1.45;}") &&
       harness_expect_contains(page_html,
                               ".harness .bubble,.developer .bubble{"
                               "background:#fff;border-top:0;"
                               "border-bottom:0;padding-top:4px;"
                               "font-size:16px;line-height:1.45;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-row.user>.bubble,"
                               ".api-exchange-row.harness>.bubble,"
                               ".api-exchange-row.developer>.bubble,"
                               ".api-exchange-row.assistant>.bubble{"
                               "background:#fff;font-size:16px;"
                               "line-height:1.45;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-row.assistant>.bubble *{"
                               "color:#222;}") &&
       harness_expect_contains(page_html,
                               ".api-exchange-row.assistant>.bubble a{"
                               "color:#2468a8;}") &&
       harness_expect_contains(page_html,
                               ".assistant .role{display:block;}") &&
       harness_expect_not_contains(page_html,
                                   ".assistant .role{display:none;}") &&
       harness_expect_contains(page_html,
                               "var strappyAPIExchangeCollapsed={};") &&
       harness_expect_not_contains(page_html,
                                   "strappyAPIExchangeSectionCollapsed") &&
       harness_expect_contains(page_html,
                               "function rowIsAPIExchangeAnswer") &&
       harness_expect_contains(page_html,
                               "function rowIsAPIExchangeConversation") &&
       harness_expect_contains(page_html,
                               "function apiExchangeCollapsed") &&
       harness_expect_contains(page_html,
                               "function setAPIExchangesCollapsedForPrompt") &&
       harness_expect_not_contains(page_html,
                                   "function apiExchangeSectionCollapsed") &&
       harness_expect_contains(page_html,
                               "setRowClass(row,'api-exchange-collapsed-row',"
                               "collapsed&&row!==anchor&&!conversation);") &&
       harness_expect_contains(page_html,
                               "setRowClass(row,'api-exchange-collapsed-"
                               "conversation',collapsed&&conversation);") &&
       harness_expect_contains(page_html,
                               ".api-exchange-collapsed-conversation>"
                               ".api-exchange-section-label{display:none;}") &&
       harness_expect_not_contains(page_html,
                                   "strappyAPIToolGroupCollapsed") &&
       harness_expect_not_contains(page_html,
                                   "function apiToolGroupCollapsed") &&
       harness_expect_contains(page_html,
                               "var kind=isAPIToolCallRow(row)?'calls':"
                               "'outputs'") &&
       harness_expect_contains(page_html,
                               "'api-call-'+id+'-'+kind") &&
       harness_expect_contains(page_html,
                               "groups[key]={rows:[]}") &&
       harness_expect_contains(page_html,
                               "setRowClass(row,'api-tool-group-anchor',j===0)") &&
       harness_expect_contains(page_html,
                               ".api-tool-group-secondary>.role{display:none;}") &&
       harness_expect_not_contains(page_html,
                                   "role.style.display='block'") &&
       harness_expect_contains(page_html,
                               "function rowIsAPIExchangeMetadata") &&
       harness_expect_contains(page_html,
                               "function apiExchangeCumulativeUsageCost") &&
       harness_expect_contains(page_html,
                               "function formatCumulativeUsageCost(value){"
                               "return '$'+(value!==''?value:'-');}") &&
       harness_expect_not_contains(page_html,
                                   "(value!==''?value:'-')+' total'") &&
       harness_expect_contains(page_html,
                               "titleText=roundLabel+' '+roundNumber;") &&
       harness_expect_contains(page_html,
                               "if(parseInt(attemptNumber,10)>1)"
                               "titleText+=' \\u00b7 '+attemptLabel+' '+"
                               "attemptNumber;") &&
       harness_expect_contains(page_html,
                               "titleText+=' \\u00b7 '+"
                               "formatCumulativeUsageCost("
                               "cumulativeUsageCost);") &&
       harness_expect_contains(
         page_html,
         "ensureAPIExchangeTurnHeader(anchor,g.id,collapsed,"
         "apiExchangeCumulativeUsageCost(g.rows));") &&
       harness_expect_contains(page_html,
                               "setNodeText(title,titleText);") &&
       harness_expect_not_contains(page_html,
                                   "setNodeText(title,roundLabel+' '+roundNumber+") &&
       harness_expect_not_contains(page_html, "setNodeText(n,'#'+id)") &&
       harness_expect_contains(page_html, ".api-exchange-section-label{") &&
       harness_expect_contains(page_html,
                               "renderAPIToolRows();"
                               "renderAPIServerToolRows();"
                               "moveToolRows(root)") &&
       harness_expect_contains(page_html,
                               "decorateAPIExchanges(root);"
                               "decorateAPIToolGroups(root);"
                               "decoratePromptGroups(root)") &&
       harness_expect_contains(page_html,
                               ".api-tool-card .tool-card-body") &&
       harness_expect_contains(page_html, "c.error=toolOutputHasError(raw)") &&
       harness_expect_contains(
         page_html,
         "var strappyToolDisplayRegistry={\"database_query\":") &&
       harness_expect_contains(page_html, "function toolDisplaySpec") &&
       harness_expect_contains(page_html, "function toolPathValue") &&
       harness_expect_contains(page_html, "function toolPromotedValue") &&
       harness_expect_contains(page_html, "function toolDisplayTitle") &&
       harness_expect_contains(page_html, "transform=='comma_separated'") &&
       harness_expect_contains(page_html, "parts.join(', ')") &&
       harness_expect_contains(page_html, "transform=='database_filename'") &&
       harness_expect_contains(page_html,
                               "transform=='database_hint_filename'") &&
       harness_expect_contains(page_html, "function responseItemObject") &&
       harness_expect_contains(page_html,
                               "function renderAPIServerToolRows") &&
       harness_expect_contains(page_html,
                               "renderAPIToolRows();"
                               "renderAPIServerToolRows()") &&
       harness_expect_contains(page_html, ".api_server_tool .bubble{") &&
       harness_expect_not_contains(page_html, "response-item-content") &&
       harness_expect_contains(page_html, "calls[id].args") &&
       harness_expect_contains(page_html,
                               "white-space:nowrap;overflow:hidden;"
                               "text-overflow:ellipsis;") &&
       harness_expect_not_contains(page_html, "function toolUsesDatabaseId") &&
       harness_expect_not_contains(page_html, "function toolFilenameForCard") &&
       harness_expect_contains(page_html, "function beginMessageBatch") &&
       harness_expect_contains(page_html, "function scheduleWebViewUpdate") &&
       harness_expect_contains(page_html, "function flushWebViewUpdates") &&
       harness_expect_contains(page_html, "function scheduleStatusTick") &&
       harness_expect_contains(page_html,
                               "var strappyUpdateInterval=300;"
                               "var strappyStatusInterval=1000;") &&
       harness_expect_contains(page_html, "function scrollBottomNow") &&
       harness_expect_contains(page_html,
                               "html,body{margin:0;padding:0;"
                               "background:#fff;") &&
       harness_expect_contains(page_html, ".page{padding:0;}") &&
       harness_expect_not_contains(page_html,
                                   "body.processing-status-active .page") &&
       harness_expect_contains(page_html,
                               ".processing-status{position:fixed;left:50%;"
                               "right:auto;top:auto;bottom:6px;") &&
       harness_expect_contains(page_html,
                               "border-radius:17px;") &&
       harness_expect_contains(page_html,
                               "font-weight:bold;line-height:32px;height:34px;") &&
       harness_expect_contains(page_html,
                               "background:#b2bbc2;color:#30363b;") &&
       harness_expect_contains(page_html,
                               ".processing-autoscroll{position:absolute") &&
       harness_expect_contains(page_html,
                               "width:26px;box-sizing:border-box;display:block;"
                               "border-radius:13px;") &&
       harness_expect_contains(page_html,
                               "font-size:12px;line-height:24px;"
                               "-webkit-appearance:none;appearance:none;") &&
       harness_expect_contains(page_html,
                               ".processing-autoscroll>.fa{position:absolute;"
                               "left:0;top:50%;width:100%;") &&
       harness_expect_contains(page_html,
                               "height:12px;margin-top:-6px;font-size:12px;"
                               "line-height:12px;") &&
       harness_expect_contains(page_html, ".processing-autoscroll-on") &&
       harness_expect_contains(page_html,
                               ".processing-status-active .tool-column-toggle,") &&
       harness_expect_not_contains(page_html,
                                   ".processing-status-active "
                                   ".api-tool-group-toggle") &&
       harness_expect_contains(page_html,
                               ".processing-status-active .api-reasoning-toggle{"
                               "display:none;}") &&
       harness_expect_contains(page_html,
                               ".processing-status-active .tool-disclosure{"
                               "display:none;}") &&
       harness_expect_contains(page_html,
                               ".processing-status-active .tool-card-toggle{"
                               "pointer-events:none;") &&
       harness_expect_contains(page_html,
                               ".processing-status-text{display:block;"
                               "white-space:nowrap;") &&
       harness_expect_contains(page_html,
                               "#tool-sources,.tool-source-bin,.row.tool_call,"
                               ".row.tool{display:none;}") &&
       harness_expect_contains(page_html, ".processing-status{position:fixed") &&
       harness_expect_contains(page_html,
                               ".bubble,.reasoning,.tool-column,.request-metadata{"
                               "box-shadow:none;}") &&
       harness_expect_contains(page_html, ".request-metadata-error") &&
       harness_expect_contains(page_html, "function metadataFinishStatus") &&
       harness_expect_contains(page_html, "native_finish_reason") &&
       harness_expect_contains(page_html, "request-metadata-error") &&
       harness_expect_contains(page_html,
                               "addMetaLine(lines,'Cost',usage.cost);") &&
       harness_expect_contains(page_html,
                               "addMetaLine(lines,'Input tokens',"
                               "usage.input_tokens);") &&
       harness_expect_contains(page_html,
                               "addMetaLine(lines,'Output tokens',"
                               "usage.output_tokens);") &&
       harness_expect_not_contains(page_html, "formatMetadataSummary") &&
       harness_expect_not_contains(page_html, "ERROR '+detail+' | '") &&
       harness_expect_contains(page_html, "function setProcessingStatus") &&
       harness_expect_contains(page_html, "function clearProcessingStatus") &&
       harness_expect_contains(page_html, "function clearProcessingStatusNode") &&
       harness_expect_contains(page_html, "function initProcessingStatusFromRenderState") &&
       harness_expect_contains(page_html,
                               "var strappyProcessingPromptGroupKey='';") &&
       harness_expect_contains(page_html,
                               "function processingNodePromptGroup") &&
       harness_expect_contains(page_html,
                               "if(processingNodePromptGroup(e)!=group)continue;") &&
       harness_expect_contains(page_html,
                               "function setProcessingThinkingCollapsed") &&
       harness_expect_contains(page_html,
                               "function processingInteractionsLocked") &&
       harness_expect_contains(page_html,
                               "syncProcessingInteractionState(1,group)") &&
       harness_expect_contains(page_html,
                               "syncProcessingInteractionState(0,group)") &&
       harness_expect_contains(page_html,
                               "setAPIExchangesCollapsedForPrompt(group,0);") &&
       harness_expect_contains(page_html,
                               "strappyProcessingPromptGroupKey=group;"
                               "syncProcessingInteractionState(1,group);"
                               "decorateAPIExchanges(document);"
                               "decorateAPIToolGroups(document);") &&
       harness_expect_contains(page_html,
                               "setAPIExchangesCollapsedForPrompt(group,1);"
                               "strappyProcessingStatus=null;"
                               "syncProcessingInteractionState(0,group);"
                               "strappyProcessingStatusDirty=1;"
                               "decorateAPIExchanges(document);"
                               "decorateAPIToolGroups(document);") &&
       harness_expect_contains(page_html,
                               "function toggleMetadata(a){"
                               "if(processingInteractionsLocked())return false;") &&
       harness_expect_contains(page_html,
                               "function toggleAPIExchange(a){"
                               "if(processingInteractionsLocked())return false;") &&
       harness_expect_not_contains(page_html,
                                   "function toggleAPIToolGroup(a)") &&
       harness_expect_contains(page_html,
                               "function togglePromptGroup(a){"
                               "if(processingInteractionsLocked())return false;") &&
       harness_expect_contains(page_html,
                               "function toggleReasoning(a){"
                               "if(processingInteractionsLocked())return false;") &&
       harness_expect_contains(page_html,
                               "function toggleAPIReasoning(a){"
                               "if(processingInteractionsLocked())return false;") &&
       harness_expect_contains(page_html,
                               "function toggleToolColumn(a){"
                               "if(processingInteractionsLocked())return false;") &&
       harness_expect_contains(page_html,
                               "function toggleToolCard(a){"
                               "if(processingInteractionsLocked())return false;") &&
       harness_expect_contains(page_html, "processing_status") &&
       harness_expect_contains(page_html, "var strappyAutoScrollEnabled=1") &&
       harness_expect_contains(page_html, "function toggleAutoScroll") &&
       harness_expect_contains(page_html, "function updateAutoScrollButton") &&
       harness_expect_contains(page_html, "aria-pressed") &&
       harness_expect_contains(page_html, "Disable autoscroll") &&
       harness_expect_contains(page_html, "Enable autoscroll") &&
       harness_expect_contains(page_html, "function shouldRenderMarkdownBubble") &&
       harness_expect_contains(page_html, "function shouldRenderMarkdownReasoning") &&
       harness_expect_contains(page_html, "ancestorHasClass(n,'assistant')") &&
       harness_expect_contains(page_html, ".reasoning-body{white-space:normal;}") &&
       harness_expect_contains(page_html,
                               "shouldRenderMarkdownBubble(n[i])||"
                               "shouldRenderMarkdownReasoning(n[i])") &&
       harness_expect_contains(page_html, "_strappyMarkdownRendered") &&
       harness_expect_contains(page_html, "function scheduleStreamingMarkdown") &&
       harness_expect_contains(page_html,
                               "strappyStreamingMarkdownNeedsFlush=1;"
                               "if(strappyBatchDepth===0)"
                               "scheduleWebViewUpdate(strappyUpdateInterval);") &&
       harness_expect_contains(page_html, "function appendStreamingMarkdownNode") &&
       harness_expect_contains(page_html,
                               "q.kind=='reasoning'){if(hasClass(r,'streaming-active')") &&
       harness_expect_contains(page_html, "queueTextAppend(id,t,'content')") &&
       harness_expect_contains(page_html,
                               "if(strappyBatchDepth===0){"
                               "if(strappyTextQueuesHaveEntries())flushTextQueues();") &&
       harness_expect_contains(page_html,
                               "if(strappyTextQueuesHaveEntries())flushTextQueues();"
                               "if(strappyStreamingMarkdownNeedsFlush)"
                               "flushStreamingMarkdown();") &&
       harness_expect_contains(page_html,
                               "strappyBatchShouldScroll=strappyAutoScrollEnabled?1:0") &&
       harness_expect_contains(page_html,
                               "if(strappyAutoScrollEnabled)"
                               "strappyUpdateShouldScroll=1") &&
       harness_expect_contains(page_html,
                               "function scrollBottom(){"
                               "if(!strappyAutoScrollEnabled)return;"
                               "if(strappyBatchDepth>0){"
                               "strappyBatchShouldScroll=1;return;}") &&
       harness_expect_contains(page_html, "function preserveLiveMessageText") &&
       harness_expect_contains(page_html,
                               "function prepareInsertedRow(n)") &&
       harness_expect_contains(page_html,
                               "setClass(n,'row-inserting',1);"
                               "scheduleInsertedRowCleanup(n)") &&
       harness_expect_contains(page_html,
                               "setTimeout(function(){"
                               "clearInsertedRowAnimation(n);},400)") &&
       harness_expect_contains(page_html,
                               "e.animationName!='strappy-round-fade'") &&
       harness_expect_contains(page_html,
                               "addEventListener('webkitAnimationEnd',"
                               "finishInsertedRowAnimation,false)") &&
       harness_expect_contains(page_html,
                               "removeEventListener('webkitAnimationEnd',"
                               "finishInsertedRowAnimation,false)") &&
       harness_expect_contains(page_html,
                               "while(n){prepareInsertedRow(n);"
                               "n=n.nextSibling;}") &&
       harness_expect_contains(page_html,
                               "while(d.firstChild){n=d.firstChild;"
                               "prepareInsertedRow(n);m.insertBefore(n,before);}") &&
       harness_expect_contains(page_html,
                               "if(isAssistantRow(old)&&isAssistantRow(next))"
                               "preserveLiveMessageText(old,next)") &&
       harness_expect_contains(page_html, "oldRaw.indexOf(newRaw)>=0") &&
       harness_expect_contains(page_html, "moveMessageTextToReasoningByMessageKey") &&
       harness_expect_contains(page_html, "appendMessageTextByMessageKey") &&
       harness_expect_contains(page_html, "appendReasoningTextByMessageKey") &&
       harness_expect_not_contains(page_html, "setInterval(") &&
       harness_expect_not_contains(page_html, "strappyTextTimer") &&
       harness_expect_not_contains(page_html, "strappyUpdateLastFlush") &&
       harness_expect_not_contains(page_html, "shouldAutoScroll") &&
       harness_expect_not_contains(page_html, "requestAnimationFrame") &&
       harness_expect_contains(page_html, "function setToolBoxCount") &&
       harness_expect_contains(page_html, "setToolBoxCount(box,cards.length,last") &&
       harness_expect_contains(page_html, "toolCardSummary(last,cards.length-1)") &&
       harness_expect_contains(page_html,
                               "lastError?toolErrorIconHTML():'')") &&
       harness_expect_contains(page_html,
                               "card.error?toolErrorIconHTML():'')") &&
       harness_expect_contains(page_html,
                               "setToolCardSummary(summary,apiToolLabel(row)+"
                               "': '+title,error)") &&
       harness_expect_contains(page_html,
                               "tool-column-collapsed .tool-rail-title") &&
       harness_expect_contains(page_html, "Raw JSON preview") &&
       harness_expect_contains(page_html, "tool-column tool-column-empty") &&
       harness_expect_contains(page_html, "tool-column-toggle") &&
       harness_expect_contains(page_html, "tool-column-collapsed .tool-count") &&
       harness_expect_contains(page_html, "streaming-active .tool-column-toggle") &&
       harness_expect_contains(page_html, "if(!isToolRow(r))r.style.display='block'") &&
       harness_expect_contains(page_html, "tool-column-error") &&
       harness_expect_contains(page_html, "function decoratePromptGroups") &&
       harness_expect_contains(page_html, "function togglePromptGroup") &&
       harness_expect_contains(page_html, "function setMessagePromptGroup") &&
       harness_expect_contains(page_html, "function promptGroupDefaultCollapsed") &&
       harness_expect_contains(page_html, "function promptGroupCollapsed") &&
       harness_expect_contains(page_html, "function rowIsActiveHarness") &&
       harness_expect_contains(page_html,
                               "function promptGroupAnchor(rows){var i;for(i=0;i<rows.length;i++){if(rowIsHarness(rows[i]))return rows[i];}") &&
       harness_expect_contains(page_html,
                               "setRowClass(anchor,'prompt-group-collapsed-anchor'") &&
       harness_expect_contains(page_html,
                               "prompt-group-collapsed-anchor .bubble") &&
       harness_expect_contains(page_html, "collapsed&&rowIsHarness(g[j])&&g[j]!==anchor") &&
       harness_expect_contains(page_html, "prompt-group-harness") &&
       harness_expect_contains(page_html, "data-prompt-group-key=\"prompt-group-page\"") &&
       harness_expect_contains(page_html, "reasoning-collapsed .reasoning-label");

  strappy_webview_free(page_html);
  return ok;
}

static int harness_check_timeline_error_state(void)
{
  char *empty_page_html;
  char *page_html;
  int ok;

  empty_page_html = strappy_webview_messages_page_html("", "{}", "");
  if (empty_page_html == NULL) {
    fprintf(stderr, "Could not generate blank timeline page HTML.\n");
    return 0;
  }
  page_html = strappy_webview_messages_page_html(
    "",
    "{}",
    "Timeline failed <retry> & \"later\"");
  if (page_html == NULL) {
    fprintf(stderr, "Could not generate timeline error page HTML.\n");
    strappy_webview_free(empty_page_html);
    return 0;
  }

  ok = harness_expect_not_contains(empty_page_html,
                                   "<div id=\"timeline-error\"") &&
       harness_expect_not_contains(empty_page_html, "New Session") &&
       harness_expect_not_contains(empty_page_html,
                                   "No session selected.") &&
       harness_expect_contains(
         page_html,
         "<div id=\"timeline-error\" class=\"timeline-error\">"
         "Timeline failed "
         "&lt;retry&gt; &amp; &quot;later&quot;</div>") &&
       harness_expect_not_contains(page_html,
                                   "Timeline failed <retry>");
  strappy_webview_free(empty_page_html);
  strappy_webview_free(page_html);
  return ok;
}

static int harness_check_fontawesome_rendering(void)
{
  strappy_webview_message message;
  char *message_html;
  char *page_html;
  int ok;

  strappy_webview_set_font_dir("/tmp/Strappy Fonts");

  memset(&message, 0, sizeof(message));
  message.element_id = "assistant-icon";
  message.role = "assistant";
  message.text =
    "Ready [fa:heart] [fa:regular:face-smile] [fa:brands:github] "
    "[fa:alarm-clock] [fa:solid:F004].";

  message_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (message_html == NULL) {
    fprintf(stderr, "Could not generate Font Awesome message HTML.\n");
    return 0;
  }

  page_html = strappy_webview_messages_page_html(message_html, "{}", "");
  strappy_webview_free(message_html);
  if (page_html == NULL) {
    fprintf(stderr, "Could not generate Font Awesome page HTML.\n");
    return 0;
  }

  ok = harness_expect_contains(page_html, "file:///tmp/Strappy%20Fonts/FA7-Solid-900.otf") &&
       harness_expect_contains(page_html, "file:///tmp/Strappy%20Fonts/FA7-Regular-400.otf") &&
       harness_expect_contains(page_html, "file:///tmp/Strappy%20Fonts/FA7-Brands-400.otf") &&
       harness_expect_contains(page_html, ".fa-regular{font-family:'FA7R';}") &&
       harness_expect_contains(page_html, "\"alarm-clock\":'F34E'") &&
       harness_expect_contains(page_html, "\"github\":'F09B'") &&
       harness_expect_contains(page_html, "Ready [fa:heart]") &&
       harness_expect_contains(page_html, "faIconHTML(st,n,m)");

  strappy_webview_free(page_html);
  strappy_webview_set_font_dir(NULL);
  return ok;
}

static int harness_check_script_batch(void)
{
  strappy_webview_script_batch *batch;
  char *java_script;
  const char *expected;
  int ok;

  batch = strappy_webview_script_batch_create();
  if (batch == NULL) {
    fprintf(stderr, "Could not create empty script batch.\n");
    return 0;
  }

  java_script = strappy_webview_script_batch_finish_js(batch);
  ok = (java_script == NULL) &&
       !strappy_webview_script_batch_has_js(batch);
  if (java_script != NULL) {
    strappy_webview_free(java_script);
  }
  strappy_webview_script_batch_destroy(batch);
  if (!ok) {
    fprintf(stderr, "Empty script batch should not produce JavaScript.\n");
    return 0;
  }

  batch = strappy_webview_script_batch_create();
  if (batch == NULL) {
    fprintf(stderr, "Could not create script batch.\n");
    return 0;
  }

  ok = strappy_webview_script_batch_append_js(batch, NULL) &&
       strappy_webview_script_batch_append_js(batch, "") &&
       !strappy_webview_script_batch_has_js(batch) &&
       strappy_webview_script_batch_append_js(
         batch,
         "appendMessageTextByMessageKey('m','hi');") &&
       strappy_webview_script_batch_append_js(
         batch,
         "setProcessingStatus('{}');") &&
       strappy_webview_script_batch_has_js(batch);

  java_script = strappy_webview_script_batch_finish_js(batch);
  expected =
    "beginMessageBatch();try{"
    "appendMessageTextByMessageKey('m','hi');"
    "setProcessingStatus('{}');"
    "}finally{endMessageBatch();}";
  ok = ok &&
       (java_script != NULL) &&
       (strcmp(java_script, expected) == 0) &&
       !strappy_webview_script_batch_has_js(batch) &&
       !strappy_webview_script_batch_append_js(batch,
                                               "appendMessageText('x','y');");

  if (!ok) {
    fprintf(stderr, "Script batch did not produce expected JavaScript.\n");
  }

  if (java_script != NULL) {
    strappy_webview_free(java_script);
  }
  strappy_webview_script_batch_destroy(batch);
  return ok;
}

static int harness_check_tool_column_state(void)
{
  strappy_webview_message message;
  char *final_html;
  char *streaming_html;
  char *reloaded_streaming_html;
  char *reloaded_content_html;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "assistant-final";
  message.role = "assistant";
  message.actor = "user";
  message.prompt_group_key = "prompt-group-final";
  message.text = "Done.";

  final_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (final_html == NULL) {
    fprintf(stderr, "Could not generate final assistant message HTML.\n");
    return 0;
  }

  streaming_html = strappy_webview_streaming_assistant_message_html(
    "assistant-streaming",
    "",
    "",
    "pending",
    "Thinking",
    "user",
    "prompt-group-test",
    NULL);
  if (streaming_html == NULL) {
    fprintf(stderr, "Could not generate streaming assistant message HTML.\n");
    strappy_webview_free(final_html);
    return 0;
  }

  message.element_id = "assistant-reloaded-streaming";
  message.text = "";
  message.reasoning = "Thinking through it";
  message.render_state_json =
    "{\"streaming\":true,\"reasoning_render_when_empty\":true,"
    "\"reasoning_collapsed\":false,\"tool_column_collapsed\":true}";
  reloaded_streaming_html =
    strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (reloaded_streaming_html == NULL) {
    fprintf(stderr, "Could not generate reloaded streaming assistant HTML.\n");
    strappy_webview_free(streaming_html);
    strappy_webview_free(final_html);
    return 0;
  }

  message.element_id = "assistant-reloaded-content";
  message.text = "Answer text";
  message.reasoning = "Thinking through it";
  message.render_state_json =
    "{\"streaming\":true,\"reasoning_render_when_empty\":true,"
    "\"reasoning_collapsed\":false,\"tool_column_collapsed\":true,"
    "\"content_started\":true}";
  reloaded_content_html =
    strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (reloaded_content_html == NULL) {
    fprintf(stderr, "Could not generate reloaded content assistant HTML.\n");
    strappy_webview_free(reloaded_streaming_html);
    strappy_webview_free(streaming_html);
    strappy_webview_free(final_html);
    return 0;
  }

  ok = harness_expect_contains(final_html,
                               "tool-column tool-column-empty tool-column-collapsed") &&
       harness_expect_contains(final_html,
                               "tool-rail-title disclosure-title\" "
                               "onclick=\"return toggleToolColumn(this)\"") &&
       harness_expect_contains(final_html,
                               "<div class=\"role\">Agent</div>") &&
       harness_expect_contains(final_html,
                               "tool-column-disclosure\"><i class=\"fa fa-solid "
                               "fa-angle-right\"") &&
       harness_expect_contains(streaming_html,
                               "row assistant streaming-active state-pending") &&
       harness_expect_contains(streaming_html,
                               "tool-column tool-column-empty tool-column-collapsed") &&
       harness_expect_contains(streaming_html,
                               "data-prompt-group-key=\"prompt-group-test\"") &&
       harness_expect_contains(streaming_html,
                               "class=\"bubble\" style=\"display:none;\"") &&
       harness_expect_contains(streaming_html,
                               "tool-column-disclosure\"><i class=\"fa fa-solid "
                               "fa-angle-right\"") &&
       harness_expect_not_contains(streaming_html,
                                   "tool-column-disclosure\"><i class=\"fa fa-solid "
                                   "fa-angle-down\"") &&
       harness_expect_not_contains(streaming_html,
                                   "bubble bubble-status") &&
       harness_expect_contains(reloaded_streaming_html,
                               "row assistant streaming-active") &&
       harness_expect_contains(reloaded_streaming_html,
                               "data-render-state=\"{&quot;streaming&quot;:true") &&
       harness_expect_contains(reloaded_streaming_html,
                               "Thinking through it") &&
       harness_expect_contains(reloaded_streaming_html,
                               "reasoning-label disclosure-title\" "
                               "onclick=\"return toggleReasoning(this)\"") &&
       harness_expect_contains(reloaded_streaming_html,
                               "class=\"bubble\" style=\"display:none;\"") &&
       harness_expect_not_contains(reloaded_streaming_html,
                                   "bubble bubble-status") &&
       harness_expect_not_contains(reloaded_streaming_html,
                                   "reasoning-collapsed") &&
       harness_expect_contains(reloaded_content_html,
                               "row assistant streaming-active") &&
       harness_expect_not_contains(reloaded_content_html,
                                   "reasoning-collapsed") &&
       harness_expect_contains(reloaded_content_html,
                               "Answer text") &&
       harness_expect_contains(reloaded_content_html,
                               "Thinking through it") &&
       harness_expect_not_contains(reloaded_content_html,
                                   "bubble bubble-status");

  strappy_webview_free(reloaded_content_html);
  strappy_webview_free(reloaded_streaming_html);
  strappy_webview_free(streaming_html);
  strappy_webview_free(final_html);
  return ok;
}

static int harness_check_tool_event_text(void)
{
  char *event_text;
  char *script;
  int ok;

  event_text = strappy_webview_tool_event_text(
    "result",
    "call-1",
    "memory_user_fact_read",
    "{\"query\":\"Alice\"}",
    "{\"records\":[{\"kind\":\"person\",\"value\":{\"name\":\"Alice\"}}]}");
  if (event_text == NULL) {
    fprintf(stderr, "Could not generate tool event text.\n");
    return 0;
  }

  script = strappy_webview_append_tool_event_text_js("streaming-tools",
                                                     event_text);
  if (script == NULL) {
    fprintf(stderr, "Could not generate tool event script.\n");
    strappy_webview_free(event_text);
    return 0;
  }

  ok = harness_expect_contains(event_text, "\"tool_name\":\"memory_user_fact_read\"") &&
       harness_expect_contains(event_text, "\\\"records\\\"") &&
       harness_expect_contains(script, "appendToolEventText");

  strappy_webview_free(script);
  strappy_webview_free(event_text);
  return ok;
}

static int harness_check_processing_status_scripts(void)
{
  char *set_script;
  char *clear_script;
  int ok;

  set_script = strappy_webview_set_processing_status_js(
    "{\"active\":true,\"status_kind\":\"retry_wait\","
    "\"retry_after_seconds\":7,\"retry_attempt\":2,"
    "\"retry_max_attempts\":3}");
  if (set_script == NULL) {
    fprintf(stderr, "Could not generate processing status JS.\n");
    return 0;
  }

  clear_script = strappy_webview_clear_processing_status_js();
  if (clear_script == NULL) {
    fprintf(stderr, "Could not generate processing status clear JS.\n");
    strappy_webview_free(set_script);
    return 0;
  }

  ok = harness_expect_contains(set_script, "setProcessingStatus('{\"active\":true") &&
       harness_expect_contains(set_script, "\"status_kind\":\"retry_wait\"") &&
       harness_expect_contains(clear_script, "clearProcessingStatus();");

  strappy_webview_free(clear_script);
  strappy_webview_free(set_script);
  return ok;
}

static int harness_check_tool_activity_target(void)
{
  char *html;
  int ok;

  html = strappy_webview_tool_activity_message_html(
    "streaming-harness-tools",
    "",
    "pending",
    "Running tools...",
    "harness",
    "prompt-group-harness",
    "streaming-harness-assistant",
    NULL);
  if (html == NULL) {
    fprintf(stderr, "Could not generate tool activity HTML.\n");
    return 0;
  }

  ok = harness_expect_contains(html, "class=\"row tool_call tool_activity") &&
       harness_expect_contains(html, "data-actor=\"harness\"") &&
       harness_expect_contains(html,
                               "data-tool-target=\"streaming-harness-assistant\"") &&
       harness_expect_contains(html, "style=\"display:none\"");

  strappy_webview_free(html);
  return ok;
}

static int harness_check_harness_prompt_group_collapse(void)
{
  strappy_webview_message message;
  char *user_html;
  char *assistant_html;
  char *harness_prompt_html;
  char *harness_assistant_html;
  char *messages_html;
  char *page_html;
  char *streaming_harness_html;
  size_t messages_length;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "collapse-user";
  message.role = "user";
  message.actor = "user";
  message.prompt_group_key = "prompt-group-collapse";
  message.text = "What should I remember?";
  user_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (user_html == NULL) {
    fprintf(stderr, "Could not generate prompt group user HTML.\n");
    return 0;
  }

  memset(&message, 0, sizeof(message));
  message.element_id = "collapse-assistant";
  message.role = "assistant";
  message.actor = "user";
  message.prompt_group_key = "prompt-group-collapse";
  message.text = "Done.";
  assistant_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (assistant_html == NULL) {
    fprintf(stderr, "Could not generate prompt group assistant HTML.\n");
    strappy_webview_free(user_html);
    return 0;
  }

  memset(&message, 0, sizeof(message));
  message.element_id = "collapse-harness-prompt";
  message.role = "harness";
  message.actor = "harness";
  message.prompt_group_key = "prompt-group-collapse";
  message.text = "Learning Summary";
  harness_prompt_html =
    strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (harness_prompt_html == NULL) {
    fprintf(stderr, "Could not generate prompt group harness prompt HTML.\n");
    strappy_webview_free(assistant_html);
    strappy_webview_free(user_html);
    return 0;
  }

  memset(&message, 0, sizeof(message));
  message.element_id = "collapse-harness-assistant";
  message.role = "assistant";
  message.actor = "harness";
  message.prompt_group_key = "prompt-group-collapse";
  message.text = "Learning Summary Complete";
  harness_assistant_html =
    strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (harness_assistant_html == NULL) {
    fprintf(stderr, "Could not generate prompt group harness assistant HTML.\n");
    strappy_webview_free(harness_prompt_html);
    strappy_webview_free(assistant_html);
    strappy_webview_free(user_html);
    return 0;
  }

  messages_length = strlen(user_html) +
                    strlen(assistant_html) +
                    strlen(harness_prompt_html) +
                    strlen(harness_assistant_html);
  messages_html = (char *)malloc(messages_length + 1U);
  if (messages_html == NULL) {
    fprintf(stderr, "Could not allocate prompt group collapse fixture HTML.\n");
    strappy_webview_free(harness_assistant_html);
    strappy_webview_free(harness_prompt_html);
    strappy_webview_free(assistant_html);
    strappy_webview_free(user_html);
    return 0;
  }
  snprintf(messages_html,
           messages_length + 1U,
           "%s%s%s%s",
           user_html,
           assistant_html,
           harness_prompt_html,
           harness_assistant_html);

  page_html = strappy_webview_messages_page_html(messages_html, "{}", "");
  free(messages_html);
  if (page_html == NULL) {
    fprintf(stderr, "Could not generate prompt group collapse page HTML.\n");
    strappy_webview_free(harness_assistant_html);
    strappy_webview_free(harness_prompt_html);
    strappy_webview_free(assistant_html);
    strappy_webview_free(user_html);
    return 0;
  }

  streaming_harness_html = strappy_webview_streaming_assistant_message_html(
    "collapse-harness-streaming",
    "",
    "",
    "pending",
    "Thinking",
    "harness",
    "prompt-group-collapse",
    NULL);
  if (streaming_harness_html == NULL) {
    fprintf(stderr, "Could not generate streaming harness group HTML.\n");
    strappy_webview_free(harness_assistant_html);
    strappy_webview_free(harness_prompt_html);
    strappy_webview_free(assistant_html);
    strappy_webview_free(user_html);
    strappy_webview_free(page_html);
    return 0;
  }

  ok = harness_expect_contains(user_html,
                               "data-prompt-group-key=\"prompt-group-collapse\"") &&
       harness_expect_contains(assistant_html,
                               "data-actor=\"user\"") &&
       harness_expect_contains(harness_prompt_html,
                               "class=\"row harness\"") &&
       harness_expect_contains(harness_prompt_html,
                               "data-actor=\"harness\"") &&
       harness_expect_contains(harness_assistant_html,
                               "class=\"row assistant\"") &&
       harness_expect_contains(harness_assistant_html,
                               "data-actor=\"harness\"") &&
       harness_expect_not_contains(harness_assistant_html,
                                   "streaming-active") &&
       harness_expect_contains(page_html,
                               "id=\"collapse-harness-prompt\"") &&
       harness_expect_contains(page_html,
                               "id=\"collapse-harness-assistant\"") &&
       harness_expect_contains(page_html,
                               "renderMessageDecorations(document);") &&
       harness_expect_contains(streaming_harness_html,
                               "row assistant streaming-active state-pending") &&
       harness_expect_contains(streaming_harness_html,
                               "data-actor=\"harness\"");

  strappy_webview_free(streaming_harness_html);
  strappy_webview_free(page_html);
  strappy_webview_free(harness_assistant_html);
  strappy_webview_free(harness_prompt_html);
  strappy_webview_free(assistant_html);
  strappy_webview_free(user_html);
  return ok;
}

static int harness_check_harness_message(void)
{
  strappy_webview_message message;
  char *message_html;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "harness-1";
  message.role = "harness";
  message.text = "Learning Summary";

  message_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (message_html == NULL) {
    fprintf(stderr, "Could not generate harness message HTML.\n");
    return 0;
  }

  ok = harness_expect_contains(message_html, "class=\"row harness\"") &&
       harness_expect_contains(message_html, "<div class=\"role\">Harness</div>") &&
       harness_expect_contains(message_html, "Learning Summary");

  strappy_webview_free(message_html);
  return ok;
}

static int harness_check_harness_assistant_metadata(void)
{
  strappy_webview_message message;
  char *message_html;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "harness-assistant-1";
  message.role = "assistant";
  message.actor = "harness";
  message.text = "Learning Summary Complete";
  message.reasoning = "Harness thinking";
  message.metadata_json =
    "{\"usage\":{\"cost\":\"0.001\",\"prompt_tokens\":1,\"completion_tokens\":2}}";

  message_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (message_html == NULL) {
    fprintf(stderr, "Could not generate harness assistant HTML.\n");
    return 0;
  }

  ok = harness_expect_contains(message_html, "data-actor=\"harness\"") &&
       harness_expect_contains(message_html, "request-metadata") &&
       harness_expect_contains(message_html,
                               "request-metadata-title disclosure-title\" "
                               "onclick=\"return toggleMetadata(this)\"") &&
       harness_expect_not_contains(message_html,
                                   "request-metadata-summary") &&
       harness_expect_contains(message_html, "Harness thinking") &&
       harness_expect_contains(message_html, "Learning Summary Complete");

  strappy_webview_free(message_html);
  return ok;
}

static int harness_check_error_message_state(void)
{
  strappy_webview_message message;
  char *message_html;
  int ok;

  memset(&message, 0, sizeof(message));
  message.element_id = "assistant-error-1";
  message.role = "assistant";
  message.text = "OpenRouter request failed: Timeout was reached";
  message.metadata_json =
    "{\"finish_reason\":\"error\",\"http_status\":0,"
    "\"error\":\"OpenRouter request failed: Timeout was reached\"}";
  message.is_error = 1;

  message_html = strappy_webview_message_html(&message, NULL, NULL, NULL);
  if (message_html == NULL) {
    fprintf(stderr, "Could not generate error assistant HTML.\n");
    return 0;
  }

  ok = harness_expect_contains(message_html,
                               "class=\"row assistant state-error\"") &&
       harness_expect_not_contains(message_html, "streaming-active") &&
       harness_expect_contains(message_html, "request-metadata");

  strappy_webview_free(message_html);
  return ok;
}

static int harness_check_responses_items(void)
{
  strappy_webview_message message;
  strappy_webview_labels labels;
  char *call_html;
  char *reasoning_html;
  char *function_html;
  char *output_html;
  char *search_html;
  char *fetch_html;
  char *developer_html;
  int ok;

  memset(&labels, 0, sizeof(labels));
  labels.tool = "Localized Tool";

  memset(&message, 0, sizeof(message));
  message.element_id = "response-call-1";
  message.api_call_id = 1LL;
  message.round_number = 3L;
  message.attempt_number = 2L;
  message.cumulative_usage_cost = 0.02392002;
  message.has_cumulative_usage_cost = 1;
  message.role = "api_call";
  message.kind = "response_api_call";
  message.text = "POST /responses\ncompleted / HTTP 200";
  message.metadata_json =
    "{\"id\":\"resp-test\",\"status\":\"completed\","
    "\"usage\":{\"input_tokens\":4,\"output_tokens\":8}}";
  call_html = strappy_webview_message_html(&message, NULL, NULL, NULL);

  memset(&message, 0, sizeof(message));
  message.element_id = "response-reasoning-1";
  message.api_call_id = 1LL;
  message.round_number = 3L;
  message.attempt_number = 2L;
  message.direction = "response";
  message.role = "api_reasoning";
  message.kind = "reasoning";
  message.text = "Checked the available evidence.";
  message.created_at = "2026-07-10T12:34:56.000Z";
  reasoning_html = strappy_webview_message_html(&message, NULL, NULL, NULL);

  memset(&message, 0, sizeof(message));
  message.element_id = "response-function-1";
  message.api_call_id = 1LL;
  message.direction = "response";
  message.role = "api_function_call";
  message.kind = "function_call";
  message.tool_call_id = "call-database-query";
  message.tool_name = "database_query";
  message.arguments_json =
    "{\"database_id\":\"database-1\",\"sql\":\"SELECT 1\"}";
  message.text = "database_query\n{\"database_id\":\"database-1\"}";
  function_html = strappy_webview_message_html(&message, &labels, NULL, NULL);

  memset(&message, 0, sizeof(message));
  message.element_id = "response-output-1";
  message.api_call_id = 2LL;
  message.direction = "request";
  message.role = "api_function_output";
  message.kind = "function_call_output";
  message.tool_call_id = "call-database-query";
  message.result_json =
    "{\"columns\":[\"value\"],\"rows\":[[1]],"
    "\"rows_truncated\":false}";
  message.text = "{\"columns\":[\"value\"]}";
  output_html = strappy_webview_message_html(&message, &labels, NULL, NULL);

  memset(&message, 0, sizeof(message));
  message.element_id = "response-web-search-1";
  message.api_call_id = 2LL;
  message.direction = "response";
  message.role = "api_item";
  message.kind = "openrouter:web_search";
  message.response_item_action_json =
    "{\"type\":\"search\",\"query\":\"Strappy Cocoa\","
    "\"sources\":[{\"type\":\"url\","
    "\"url\":\"https://example.com/search\"}]}";
  message.response_item_status = "completed";
  message.text = "openrouter:web_search";
  search_html = strappy_webview_message_html(&message, &labels, NULL, NULL);

  memset(&message, 0, sizeof(message));
  message.element_id = "response-web-fetch-1";
  message.api_call_id = 2LL;
  message.direction = "response";
  message.role = "api_item";
  message.kind = "openrouter:web_fetch";
  message.response_item_url = "https://example.com/article";
  message.response_item_title = "Example Article";
  message.response_item_status = "completed";
  message.response_item_http_status = "200";
  message.text = "Example Article";
  fetch_html = strappy_webview_message_html(&message, &labels, NULL, NULL);

  memset(&message, 0, sizeof(message));
  message.element_id = "response-developer-1";
  message.api_call_id = 2LL;
  message.direction = "request";
  message.role = "developer";
  message.kind = "message";
  message.text = "Audit the available tools.";
  developer_html = strappy_webview_message_html(&message, NULL, NULL, NULL);

  ok = (call_html != NULL) && (reasoning_html != NULL) &&
       (function_html != NULL) && (output_html != NULL) &&
       (search_html != NULL) && (fetch_html != NULL) &&
       (developer_html != NULL) &&
       harness_expect_contains(call_html, "class=\"row api_call\"") &&
       harness_expect_contains(call_html, "data-api-call-id=\"1\"") &&
       harness_expect_contains(call_html, "data-round-number=\"3\"") &&
       harness_expect_contains(call_html, "data-round-label=\"Round\"") &&
       harness_expect_contains(call_html, "data-attempt-number=\"2\"") &&
       harness_expect_contains(call_html, "data-attempt-label=\"Attempt\"") &&
       harness_expect_contains(call_html,
                               "data-cumulative-usage-cost=\"0.02392002\"") &&
       harness_expect_contains(call_html, "<div class=\"role\">API Call</div>") &&
       harness_expect_contains(call_html, "request-metadata") &&
       harness_expect_contains(reasoning_html,
                               "class=\"row api_reasoning "
                               "api-reasoning-collapsed\"") &&
       harness_expect_contains(reasoning_html,
                               "data-direction=\"response\"") &&
       harness_expect_contains(reasoning_html,
                               "data-direction-label=\"Response\"") &&
       harness_expect_contains(reasoning_html,
                               "<div class=\"role disclosure-title\" "
                               "onclick=\"return toggleAPIReasoning(this)\">") &&
       harness_expect_contains(reasoning_html,
                               "class=\"api-reasoning-toggle\"") &&
       harness_expect_contains(reasoning_html,
                               "aria-expanded=\"false\"") &&
       harness_expect_contains(reasoning_html,
                               "api-reasoning-disclosure\"><i class=\"fa fa-solid "
                               "fa-angle-right\"") &&
       harness_expect_contains(reasoning_html,
                               "</span></a>Thinking</div>") &&
       harness_expect_not_contains(reasoning_html,
                                   "2026-07-10T12:34:56.000Z") &&
       harness_expect_not_contains(reasoning_html, "tool-column") &&
       harness_expect_contains(function_html,
                               "class=\"row api_function_call\"") &&
       harness_expect_not_contains(function_html,
                                   "<div class=\"role\">Tool Call</div>") &&
       harness_expect_contains(function_html,
                               "data-tool-call-id=\"call-database-query\"") &&
       harness_expect_contains(function_html,
                               "data-tool-name=\"database_query\"") &&
       harness_expect_contains(function_html,
                               "data-tool-label=\"Localized Tool\"") &&
       harness_expect_contains(function_html,
                               "data-arguments-json=\"{&quot;database_id&quot;:") &&
       harness_expect_contains(function_html,
                               "class=\"bubble api-tool-card tool-card\"") &&
       harness_expect_contains(function_html,
                               "class=\"tool-card-toggle disclosure-title\"") &&
       harness_expect_contains(function_html,
                               "class=\"tool-card-summary\">Localized Tool: "
                               "database_query</span>") &&
       harness_expect_contains(function_html,
                               "aria-expanded=\"false\"") &&
       harness_expect_contains(function_html,
                               "class=\"tool-disclosure\"") &&
       harness_expect_contains(function_html,
                               "fa-angle-right") &&
       harness_expect_contains(function_html,
                               "class=\"tool-card-body\"") &&
       harness_expect_not_contains(function_html,
                                   "tool-card-open") &&
       harness_expect_not_contains(function_html, "class=\"row tool_call\"") &&
       harness_expect_contains(output_html,
                               "class=\"row api_function_output\"") &&
       harness_expect_contains(output_html,
                               "data-api-call-id=\"2\"") &&
       harness_expect_contains(output_html,
                               "data-direction=\"request\"") &&
       harness_expect_contains(output_html,
                               "data-direction-label=\"Request\"") &&
       harness_expect_not_contains(output_html,
                                   "<div class=\"role\">Tool Result</div>") &&
       harness_expect_contains(output_html,
                               "data-tool-call-id=\"call-database-query\"") &&
       harness_expect_contains(output_html,
                               "data-tool-label=\"Localized Tool\"") &&
       harness_expect_contains(output_html,
                               "data-result-json=\"{&quot;columns&quot;:") &&
       harness_expect_contains(output_html,
                               "class=\"bubble api-tool-card tool-card\"") &&
       harness_expect_contains(output_html,
                               "class=\"tool-card-toggle disclosure-title\"") &&
       harness_expect_contains(output_html,
                               "class=\"tool-card-summary\">Localized Tool: "
                               "Tool Result</span>") &&
       harness_expect_contains(output_html,
                               "aria-expanded=\"false\"") &&
       harness_expect_contains(output_html,
                               "class=\"tool-disclosure\"") &&
       harness_expect_contains(output_html,
                               "fa-angle-right") &&
       harness_expect_contains(output_html,
                               "class=\"tool-card-body\"") &&
       harness_expect_not_contains(output_html,
                                   "tool-card-open") &&
       harness_expect_contains(search_html,
                               "class=\"row api_item\"") &&
       harness_expect_contains(search_html,
                               "data-kind=\"openrouter:web_search\"") &&
       harness_expect_contains(search_html,
                               "data-tool-label=\"Localized Tool\"") &&
       harness_expect_contains(
         search_html,
         "data-response-item-action-json=\"{&quot;type&quot;:"
         "&quot;search&quot;,&quot;query&quot;:&quot;Strappy Cocoa&quot;") &&
       harness_expect_contains(search_html,
                               "data-response-item-status=\"completed\"") &&
       harness_expect_contains(fetch_html,
                               "class=\"row api_item\"") &&
       harness_expect_contains(fetch_html,
                               "data-kind=\"openrouter:web_fetch\"") &&
       harness_expect_contains(
         fetch_html,
         "data-response-item-url=\"https://example.com/article\"") &&
       harness_expect_contains(fetch_html,
                               "data-response-item-title=\"Example Article\"") &&
       harness_expect_contains(fetch_html,
                               "data-response-item-http-status=\"200\"") &&
       harness_expect_not_contains(fetch_html, "Fetched page body") &&
       harness_expect_contains(developer_html,
                               "class=\"row developer\"") &&
       harness_expect_contains(developer_html,
                               "<div class=\"role\">Developer</div>");

  strappy_webview_free(developer_html);
  strappy_webview_free(fetch_html);
  strappy_webview_free(search_html);
  strappy_webview_free(output_html);
  strappy_webview_free(function_html);
  strappy_webview_free(reasoning_html);
  strappy_webview_free(call_html);
  return ok;
}

int main(void)
{
  strappy_webview_set_font_dir("/tmp/Strappy Fonts");
  if (!harness_check_localized_labels()) {
    return 1;
  }
  if (!harness_check_message_batch()) {
    return 1;
  }
  if (!harness_check_page_scripts()) {
    return 1;
  }
  if (!harness_check_timeline_error_state()) {
    return 1;
  }
  if (!harness_check_fontawesome_rendering()) {
    return 1;
  }
  if (!harness_check_script_batch()) {
    return 1;
  }
  if (!harness_check_tool_column_state()) {
    return 1;
  }
  if (!harness_check_tool_event_text()) {
    return 1;
  }
  if (!harness_check_processing_status_scripts()) {
    return 1;
  }
  if (!harness_check_tool_activity_target()) {
    return 1;
  }
  if (!harness_check_harness_prompt_group_collapse()) {
    return 1;
  }
  if (!harness_check_harness_message()) {
    return 1;
  }
  if (!harness_check_harness_assistant_metadata()) {
    return 1;
  }
  if (!harness_check_error_message_state()) {
    return 1;
  }
  if (!harness_check_responses_items()) {
    return 1;
  }

  return 0;
}
