AGENTS.md

This project is an iOS app and macOS app built using Altivec Intelligence. The
iOS app supports iOS 4.3 and higher and the macOS App supports Mac OS X and
higher. The Altivec Intelligence toolchain does the hard work for you so don't
worry.

For this project, the iOS app is always built for arm64 and armv7 and the macOS
app is always built for arm64, x64, x86, and PPC. After changing anything before
you finish, please run a clean build to ensure there are no warnings. Also make
sure you capture all of the build output so you can see all the warnings. In
certain cases, we also want to run Clang static analysis to be extra sure we are
not leaking memory or dereferencing null pointers. Capture build output in the
terminal transcript or a local working log, but do not commit generated build or
analysis logs unless the user explicitly asks for them.

Linux-only shared-core harnesses live under `source/linux`. They are fast
developer smoke tests for portable C code and do not replace the required
Altivec iOS/macOS clean builds. The current harness targets are
`database_query_harness`, `bash_harness`, `webview_harness`,
`responses_harness` and `prompt_generator_harness`, run through
`make -C source/linux clean test`.
Use `make -C source/linux prompts` to write all assistant-set prompts with web
search set to none, native, Exa, and Parallel under
`source/linux/build-linux/system-prompts`, or use
`make -C source/linux review-prompts` to print all twelve variants.
`database_query_harness` also covers
OpenRouter model catalog persistence, catalog search, default model selection,
allowed-model whitelisting, per-session model selection, and stale
session-model fallback.

House style for Strappy source:

1. Objective-C classes use Objective-C naming conventions. The Objective-C class
   that manages assistant sessions is `StrappySession`, implemented in
   `StrappySession.h` and `StrappySession.m`.
2. C source and header files use lowercase snake_case names, for example
   `strappy_client.c` and `strappy_core.h`, so they are easy to distinguish from
   Objective-C classes.
3. C type and function names use lowercase snake_case. Public C functions use
   the `strappy_` prefix.
4. The shared SQLite storage module is named `strappy.sqlite`. Do not name it
   `session_store`; it will hold app-wide database responsibilities over time.
5. C files must not import macOS-only framework headers such as
   `CoreFoundation`. Shared C code must stay portable across Apple and potential
   future targets. The exception is `source/shared/strappy_cocoa.c`, which is
   the designated Cocoa/CoreFoundation import boundary for shared C; keep any
   framework-specific imports and code isolated there with portable fallbacks
   for non-Apple targets.
6. `StrappySession.m` owns the Objective-C/C boundary for AI agent sessions.
   Keep filesystem scanning and database discovery out of `StrappySession`.
7. `FileScanner.m` owns the Objective-C/C boundary for filesystem scanning and
   database discovery. It is a singleton because Strappy scans one host
   filesystem at a time. Objective-C UI code should talk to `FileScanner`, not
   directly to scanner C headers.
8. Except for `StrappySession.m`, `FileScanner.m`, and UI code that calls the C
   webview renderer in `strappy_webview.h`, Objective-C files must not directly
   import Strappy C headers such as `strappy_client.h` or `strappy_core.h`.
   Objective-C view controllers may pass display data to the webview renderer,
   but must not hand-roll webview HTML, CSS, or JavaScript.
9. Platform and SDK compatibility `#if` / `#ifdef` checks must live in
   `XPAppKit`, `XPUIKit`, or `XPFoundation`. Call sites should use XP-prefixed
   macros, helpers, categories, or types instead of embedding compatibility
   conditionals directly in feature code.
10. XP-prefixed compatibility methods are runtime bridges, not simple aliases.
   When the modern API may exist on some target runtimes but not others,
   implement an `XP_` category method on the owning Cocoa class, check the
   modern selector with `respondsToSelector:`, and then fall back to the
   oldest supported selector. Prefer `performSelector:` for object-only
   signatures. Use `NSInvocation` for primitive or struct arguments/returns
   where `performSelector:` is unsafe; for example, `NSNumber` integer factory
   and value helpers must use `NSInvocation`, not `performSelector:`. Do not
   use raw typed function pointers in Strappy XP helpers. Foundation-only
   shims live in
   `source/shared/XPFoundation.{h,m}`; AppKit/UIKit shims live in `XPAppKit` /
   `XPUIKit`. Call sites must use the XP method and must not call newer SDK
   selectors directly.
11. SQLite stores semantic fields, not serialized JSON documents. Normalize
    provider objects and arrays into typed subtype tables or
    `structured_documents` / `structured_nodes`, and reconstruct temporary
    cJSON values only when the runtime or webview compatibility API requires
    them. Do not add JSON, raw payload, or HTTP-header columns.
12. Webview HTML, CSS, and JavaScript strings are generated in C. Keep that
    rendering logic in `strappy_webview.{h,c}` or another C module, not in
    Objective-C view controllers.
13. Prompt, assistant-set, tool, and database guidance are runtime resources
    under `source/shared/Resources`: `AssistantSets.json`,
    `SystemPrompt.json`, `GuidanceTools.json`, and `GuidanceDatabase.json`.
    System prompts are assembled programmatically from the selected tools and
    their descriptions, the selected quality checks and their shared policy
    guidance, the assistant-set goal, and the invariant personality/hard-rule
    text. `SystemPrompt.json` owns section copy, prompt-facing audit guidance,
    invariant personality, and hard rules; executable audit evaluation remains
    code-owned. `AssistantSets.json` owns each set's goal, tool allowlist,
    preflight tools, answer-quality checks, and availability. Keep tool schemas
    in `GuidanceTools.json` in sync with the tool-name constants in
    `strappy_tools.h` and the executor in `strappy_tools.c`; do not duplicate
    prompt or tool guidance in Objective-C UI code. At the model-facing
    boundary, `strappy_tools.c` owns tool JSON
    argument validation, dispatch, and output serialization. Specialized C
    modules accept typed values and return typed results rather than parsing or
    emitting tool JSON. Web search and fetch use OpenRouter server tools with a
    single session-selected provider; `strappy_client.c` remains specific to
    OpenRouter.
    Strict assistant workflow
    rules, timestamp guidance, memory guidance, and database-specific
    instructions belong in these resources or the shared quality-policy table,
    not in scattered C or Objective-C strings.
14. Database tool flow is split by responsibility. `database_list_info` lists
    approved databases in a compact `databases` array containing
    assistant-visible IDs, inferred app names, paths, sizes, and modification
    times. Its result is always an object; when the array is empty, a guidance
    string explains that the user has not approved any databases.
    `database_context_read` returns remembered database hints plus compact,
    bounded lists of table and view names. Use targeted read-only SQL through
    `database_query` when column or other schema metadata is needed.
    `database_query` runs bounded read-only SQL against approved databases and
    returns only ordered column-name and positional-row arrays plus a
    `rows_truncated` boolean. Exceptional text and BLOB cells carry their own
    compact metadata.
    The Responses runtime executes the selected assistant set's preflight tools
    before round zero and seeds each result as a typed `function_call` plus
    matching `function_call_output` input pair. World Knowledge preflights only
    `memory_user_fact_read`; Personal Assistant additionally preflights
    `database_list_info`. These application-created, request-direction items do
    not create response tool-execution rows or count as model-generated calls
    for the tool audit.
    Do not put full schema dumps or learned hint caches into
    `database_list_info`.
15. Memory and session-title tools persist durable assistant state.
    `memory_user_fact_*` stores small stable user facts,
    `memory_database_hint_*` stores reusable evidence-backed database hints, and
    `helper_session_name_write` updates the active session name for every user
    prompt and requires a non-empty string name. Do not store secrets,
    credentials, sensitive identifiers, long copied content, or private row
    contents in memory.
16. Active assistant history uses the normalized Responses API ledger: each
    logical round has one `model_requests` row, each transport attempt has one
    `http_attempts` row, and each input/output item has one ordered
    `conversation_items` row plus exactly one applicable typed subtype. Store
    scalar result and usage metadata in `api_results` and `api_usage`; store
    objects and arrays as `structured_documents` / `structured_nodes`. Never
    persist request bodies, response bodies, headers, or raw JSON. Reconstruct
    provider-shaped JSON transiently only at an API or compatibility boundary.
    At every canonical, successful, tool-free final response, evaluate the
    code-owned ordered quality checks exactly once and persist one
    `answer_quality_audits` report with its `answer_quality_checks`. The first
    check verifies that the response contains a non-whitespace assistant
    answer. The next check scans the answer's UTF-8 text for Unicode emoji and
    fails at the first match; ASCII keycap bases such as digits, `#`, and `*`
    are allowed unless an emoji selector or keycap combining mark follows.
    Render that informational report in the visible timeline
    immediately before its assistant answer when present; an empty response
    leaves the failed report as the final timeline item. Track tool activity
    across the whole logical request. When `openrouter:web_search` or
    `openrouter:web_fetch`
    activity has occurred, scan the answer for a non-image inline Markdown HTTP
    or HTTPS link with a non-empty title and URL. Database inventory is an
    application-seeded preflight tool output rather than a quality rule. The
    report always uses the universal set checks for
    `helper_session_name_write` and `helper_fontawesome_shortcode_confirm`.
    Personal Assistant additionally checks `database_context_read`; World
    Knowledge never runs that database-specific check. `database_context_read`
    requires an approved database id and may be skipped when it is not
    applicable, in which case its informational quality check may fail. The
    memory remember tools are optional and are not quality checks:
    `memory_user_fact_remember` requires a non-empty fact, and
    `memory_database_hint_remember` requires both an approved database id and a
    non-empty hint. They do not accept empty or null no-ops. The session-name
    tool requires a non-empty string and updates the active session name. The
    Font Awesome confirmation tool requires a non-empty `shortcodes` array and
    does not accept a null or empty-array no-op.
    `database_query` is not a quality rule and still requires both an approved
    database id and read-only SQL. User-fact memory may include useful durable
    facts learned from approved databases, while database-hint memory must not
    store private row values or one-off query results. Never append a developer
    remediation message or issue another model request because a quality check
    failed; the report informs the user, who decides whether to ask for
    corrections. Accept
    an empty tool-free response as final after recording the failed non-empty
    answer check alongside every other applicable check. Never append a
    developer message or issue another model request for an empty answer. Every
    quality report, tool, and assistant item uses the normal database and
    timeline paths.
17. OpenRouter model catalog and selection state live in shared SQLite storage.
    `strappy_db` owns `models`, `model_prices`, `model_features`,
    `model_preferences`, `app_preferences.default_model_id`, and
    `sessions.model_id`; `StrappySession` owns the
    Objective-C bridge. `APIMODEL` is not a user configuration key. Load
    endpoint/token from `.env`, process environment, or Keychain, then resolve
    the default or per-session model through the catalog and call
    `strappy_config_set_api_model`. Model pickers and prompt options must use
    the allowed-model catalog APIs and must not bypass the whitelist or allow a
    stale disallowed session model to keep running.
18. The database is the source of truth for UI state. Do not update UI-visible
    state first and persist it later; write the database change first, then
    refresh or render the UI from the stored value.
19. Shared C/database code owns conversation-visible state. Platform UI code
    must not synthesize prompt, response item, tool, API error, or cancellation
    rows before they exist in SQLite; it should request the shared core to
    persist the state, then render the ordered DB-backed API items directly. If
    a WebView needs an incremental update, Objective-C should only pass
    shared-core, DB-derived JavaScript through the controller's single injection
    method; do not assemble conversation DOM state in platform UI code. Keep
    temporary UI-only control state, such as disabled buttons or spinners, out
    of persisted conversation rows.
20. iOS Objective-C code must use bracketed accessor/message syntax instead of
    dot syntax, even when dot syntax would be accepted by the compiler.
21. When changing shared C behavior, especially database, tool, prompt, client,
    or JSON parsing code, run `make -C source/linux clean test` where the host
    Linux environment has the required dependencies. Keep the harnesses updated
    as new shared behavior is added or existing behavior changes, so regressions
    can be caught without waiting for full Apple-target builds.
22. SQLite `PRAGMA user_version` is intentionally pinned at `1`. Do not
    increase it or add migration steps without explicit user permission; this
    database has not shipped yet.

The iOS App is a bit special because it's not sandboxed. It must be installed
via .deb file, not .ipa file so that it can scan the whole filesystem for
SQLite databases. This is known to only work on Jailbroken iPhones and that is
ok.

Strappy is an OpenRouter based AI Assistant that has the following basic
infrastructure:

1. C based non-streaming OpenRouter Responses API client
2. OpenRouter model catalog persistence with searchable/sortable browsing,
   default model selection, allowed-model whitelisting, and per-session model
   selection
3. C based transient API JSON parsing with cJSON and normalized SQLite storage
4. C based networking with libcurl
5. C based storage with sqlite
6. Web based chat interface for showing the response from the model
7. Filesystem search to search the host device for sqlite databases
8. Tools that allow the Agent to discover the schema of a sqlite database found
9. Tools that allow the Agent to answer questions the user asks from the personal context found in the sqlite databases
10. Helper tools for timestamp conversion, remembered user facts, remembered database hints, and session naming
11. Runtime assistant-set, prompt, tool, and database guidance resources that
    select capabilities and steer database selection, SQL workflow, and memory
    behavior
