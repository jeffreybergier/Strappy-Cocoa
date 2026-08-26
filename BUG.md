# ChatGPT Terra exposes internal web-citation markers

## Summary

When Strappy uses a ChatGPT account with `gpt-5.6-terra` and native web
search, the final answer can contain internal citation markup such as:

```text
… high-value watches. citeturn0search0turn0search3
```

The characters surrounding `cite` and `turn0search…` are private-use Unicode
characters (`U+E200`, `U+E202`, and `U+E201`). They appear as strange glyphs in
the WebView because Strappy persists and renders the provider text verbatim.

The same prompt does not exhibit this formatting problem with ChatGPT
`gpt-5.6-luna` or with `gpt-5.6-terra` through OpenRouter.

## Database evidence

A read-only snapshot of `strappy.sqlite`, including its WAL, was copied from
`gomadango` on August 25, 2026. The three newest sessions were:

| Session | Provider and model | Internal cite tokens | Citation rows | Literal HTTP links |
|---:|---|---:|---:|---:|
| 17 | ChatGPT `gpt-5.6-terra` | 3 | 0 | 0 |
| 18 | ChatGPT `gpt-5.6-luna` | 0 | 4 | 4 |
| 19 | OpenRouter `openai/gpt-5.6-terra` | 0 | 15 | 4 |

Session 17's final answer contains three literal `cite…` spans in
`item_text_parts.text`. It has no associated `item_citations` rows. Therefore,
this is not primarily a Markdown or WebView rendering bug: the malformed text
has already reached normalized persistence.

Session 18 contains ordinary inline Markdown links and four URL-citation rows.
Session 19 also contains ordinary links and persisted citation annotations.
The database citation-normalization path works when it receives annotations.

## Likely cause

The ChatGPT SSE adapter does not fully reconcile streamed citation metadata.

`strappy_sse_process_event()` in `source/shared/strappy_sse.c` retains
`response.output_item.done` and terminal response events, but ignores
`response.output_text.annotation.added`. The Responses streaming schema defines
that event as the event emitted when an annotation is added to output text.

There is a second possible loss point in `strappy_sse_store_terminal()`. A
retained `response.output_item.done` collection is used only if the terminal
response's `output` array is absent or empty. If the terminal response contains
a nonempty but annotation-poor copy of an item, it wins over a richer completed
item.

Downstream code in `source/shared/strappy_db_responses.c` correctly reads an
output text part's `annotations`, inserts them into `item_citations`, and
generates a Markdown source list for display. In the failing Terra session,
that code received no annotations and preserved the internal marker text.

Because Strappy intentionally does not persist raw provider responses, the
database cannot establish whether this particular response carried its missing
metadata in standalone annotation events, in a richer output-item event, or in
another ChatGPT-backend-specific field. A bounded live capture or fixture based
on an observed SSE stream is needed to distinguish those cases. The normalized
database evidence nevertheless establishes that the data is lost before or at
response normalization, not in the WebView.

## Why Luna and OpenRouter mask the problem

ChatGPT Luna emitted normal inline Markdown URLs as part of the answer and also
returned standard URL annotations. It therefore did not depend on Strappy
understanding the internal citation markers.

OpenRouter uses a different response transport and returned normalized links
and annotations for Terra. It does not exercise the problematic ChatGPT SSE
adapter behavior.

## Proposed fix

1. Retain `response.output_text.annotation.added` events, keyed by item ID and
   content index, and merge them into the corresponding output text part.
2. Reconcile `response.output_item.done` items with terminal response output
   instead of discarding the completed-item copy whenever terminal output is
   merely nonempty.
3. Convert citation annotations into ordinary Markdown links or the existing
   generated source list.
4. Defensively remove unresolved `cite…` spans from user-visible text
   when no safe citation URL can be resolved. Do not invent links or guess a
   source mapping.
5. Add a ChatGPT adapter/SSE regression fixture containing Terra-style marker
   text, annotation events, and a terminal response with incomplete annotation
   data. Assert that citation rows are persisted and no internal markers reach
   the displayed answer.

## Acceptance criteria

- ChatGPT Terra answers that use native web search contain no visible
  `citeturn…` tokens.
- URL annotations survive streaming and are stored in `item_citations`.
- Valid citations render as clickable HTTP or HTTPS Markdown links or in the
  generated source list.
- Unresolvable internal citation markers are removed without fabricating URLs.
- Existing ChatGPT Luna and OpenRouter citation behavior remains unchanged.
- The Linux ChatGPT adapter, Responses, and WebView harnesses cover the
  regression.

## Diagnostic capture

Raw transport capture is disabled by default through
`STRAPPY_RAW_JSON_DEBUG_CAPTURE` in
`source/shared/strappy_debug_capture.h`. Set that compile-time flag to `1` to
compile the capture path in temporarily.

Captures are written beside `strappy.sqlite` under `debug/sessions`. Each
request/response attempt is identified by its existing `sessions.id`,
`turns.id`, `model_requests.id`, and `http_attempts.id`. The complete response
body is written byte-for-byte to `response-body.json` before the transport
buffer is released. SSE framing, line endings, event fields, and data fields
are preserved exactly as received; the capture does not parse events or write
Strappy's normalized interpretation to disk. This makes it possible to compare
the server response with the normalized data already stored in SQLite.

## Separate observation

The OpenRouter Terra comparison session formatted citations correctly but
answered for June 6 rather than the requested August 25 date. That appears to
be a separate freshness or answer-quality issue, not part of this citation
transport bug.
