#ifndef STRAPPY_SSE_H
#define STRAPPY_SSE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct strappy_sse_parser {
  char *event_buffer;
  size_t event_length;
  size_t event_capacity;
  size_t stream_bytes;
  size_t parsed_event_count;
  void *completed_output;
  size_t completed_output_count;
  char *terminal_json;
  int terminal_received;
} strappy_sse_parser;

void strappy_sse_parser_init(strappy_sse_parser *parser);
void strappy_sse_parser_destroy(strappy_sse_parser *parser);

/* Feeds an arbitrary transport chunk. The parser accepts LF or CRLF framing,
 * ignores comments and non-data fields, and stops retaining bytes as soon as
 * a terminal Responses event has been extracted. */
int strappy_sse_parser_feed(strappy_sse_parser *parser,
                            const char *bytes,
                            size_t length,
                            char **error_out);
int strappy_sse_parser_finish(strappy_sse_parser *parser,
                              char **error_out);
int strappy_sse_parser_is_terminal(const strappy_sse_parser *parser);
int strappy_sse_parser_has_events(const strappy_sse_parser *parser);
char *strappy_sse_parser_take_terminal_json(strappy_sse_parser *parser);

#ifdef __cplusplus
}
#endif

#endif
