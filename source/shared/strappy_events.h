#ifndef STRAPPY_EVENTS_H
#define STRAPPY_EVENTS_H
typedef enum strappy_responses_event_type {
  STRAPPY_RESPONSES_EVENT_CANCELLATION_POLL = 1,
  STRAPPY_RESPONSES_EVENT_PROCESSING_STATUS = 2,
  /* Ledger events invalidate a DB-backed timeline range. One event may make
   * multiple committed changes visible when adjacent round transitions are
   * deliberately coalesced. */
  STRAPPY_RESPONSES_EVENT_LEDGER_CHANGED = 3,
  STRAPPY_RESPONSES_EVENT_LEDGER_UPDATED = 4,
  /* A durable session field changed outside the conversation ledger.
   * Consumers should reload the session summary from SQLite. */
  STRAPPY_RESPONSES_EVENT_SESSION_UPDATED = 5
} strappy_responses_event_type;

typedef struct strappy_responses_event {
  strappy_responses_event_type type;
  const char *prompt_group_key;
  const char *actor;
  const char *kind;
  const char *message_key;
  const char *render_role;
  const char *status_json;
  const char *status_kind;
  const char *status_reason;
  long long status_started_ms;
  long long status_updated_ms;
  long long retry_until_ms;
  unsigned int retry_after_seconds;
  unsigned int retry_attempt;
  unsigned int retry_max_attempts;
  /* The committed ledger range may be held briefly so a fast continuation
   * can share one WebView mutation without hiding a long-running tool call. */
  int coalesce_with_next_ledger_change;
  /* A completed, tool-free response that ends the prompt loop. */
  int is_terminal;
} strappy_responses_event;

typedef int (*strappy_responses_event_callback)(
  const strappy_responses_event *event,
  void *user_data);

#endif
