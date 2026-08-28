#include "strappy_sse.h"

#include "strappy_core.h"

#include <stdlib.h>
#include <string.h>

void strappy_sse_parser_init(strappy_sse_parser *parser)
{
  if (parser != NULL) {
    memset(parser, 0, sizeof(*parser));
  }
}

void strappy_sse_parser_destroy(strappy_sse_parser *parser)
{
  if (parser == NULL) {
    return;
  }
  free(parser->event_buffer);
  free(parser->terminal_json);
  strappy_sse_parser_init(parser);
}

int strappy_sse_parser_feed(strappy_sse_parser *parser,
                            const char *bytes,
                            size_t length,
                            char **error_out)
{
  (void)bytes;
  if (parser != NULL) {
    parser->stream_bytes += length;
  }
  strappy_set_error(error_out,
                    "SSE Responses are unavailable in the web PoC.");
  return 0;
}

int strappy_sse_parser_finish(strappy_sse_parser *parser,
                              char **error_out)
{
  (void)parser;
  strappy_set_error(error_out,
                    "SSE Responses are unavailable in the web PoC.");
  return 0;
}

int strappy_sse_parser_is_terminal(const strappy_sse_parser *parser)
{
  return ((parser != NULL) && parser->terminal_received) ? 1 : 0;
}

int strappy_sse_parser_has_events(const strappy_sse_parser *parser)
{
  return ((parser != NULL) && (parser->parsed_event_count > 0U)) ? 1 : 0;
}

char *strappy_sse_parser_take_terminal_json(strappy_sse_parser *parser)
{
  char *json;

  if (parser == NULL) {
    return NULL;
  }
  json = parser->terminal_json;
  parser->terminal_json = NULL;
  return json;
}
