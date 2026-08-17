# ChatGPT Device OAuth and Subscription Backend Plan

Status: implementation complete for the experimental catalog, OAuth lifecycle,
provider adapter, tools/session behavior, and matched iOS/macOS UI; live backend
compatibility and production authorization remain release gates, 2026-08-17

This plan adds a second provider, `openai_chatgpt`, whose product label is
"ChatGPT (Codex)". It signs in with the device-code flow and sends requests to
the ChatGPT Codex backend so usage is charged against the user's eligible
ChatGPT plan limits rather than an OpenAI Platform API key.

The plan intentionally keeps OpenRouter working. It does not turn a ChatGPT
OAuth access token into an API key, and it does not send that token to the
public `api.openai.com/v1` API.

## Implementation progress — 2026-08-17

The current worktree includes the provider-account database, validated bundled
catalog importer, iOS/macOS credential lifecycle and preferences, provider-
separated ChatGPT request adapter, bounded SSE extraction, capability-aware
tools/session UI, subscription billing semantics, kill switches, and
no-persistence compatibility/report tooling. OpenRouter remains independent and
unchanged in product behavior. Live backend/tool/plan-limit checks and written
production authority are deliberately not represented as complete.

### Completed

- [x] Added `strappy_openai_oauth.[ch]` with the Pi-compatible device start,
  immediate polling, pending/`slow_down` handling, 15-minute timeout,
  cancellation, authorization-code exchange, refresh-token rotation, bounded
  JSON/base64url/JWT parsing, and ChatGPT account-ID extraction.
- [x] Added the iOS `StrappyAuthentication` coordinator with a process-lifetime
  observable state machine, main-thread notifications, serialized login and
  refresh operations, race-safe cancellation/sign-out, and proactive refresh
  within five minutes of expiry when the app becomes active.
- [x] Extended `XPKeychain` and `StrappyKeychain` to store one provider-specific,
  versioned binary-plist credential containing the access token, rotating
  refresh token, absolute expiry, account ID, and format version. iOS uses
  `kSecAttrAccessibleWhenUnlockedThisDeviceOnly`, and rotation updates the
  existing Keychain item in place rather than deleting the last credential.
- [x] Added the iOS **ChatGPT (Experimental)** preferences section with status,
  device code, Copy Code, Open Browser, Cancel, Retry Refresh, and Sign Out,
  plus English and Japanese localization.
- [x] Added a deterministic Linux mock-server harness covering device start,
  pending polling, successful authorization, token exchange, JWT account
  extraction, refresh rotation, and pre-network cancellation.
- [x] Kept the OpenRouter credential and request path unchanged and made no
  `PRAGMA user_version` change.
- [x] Added internal provider accounts for OpenRouter and ChatGPT, stored each
  model with its account plus an exact wire model ID, and changed database model
  keys to stable account-qualified identifiers such as
  `openrouter:z-ai/glm-5.2`.
- [x] Made OpenRouter catalog deactivation account-scoped, added generic model
  APIs, and grouped the iOS whitelist by account; the macOS whitelist has an
  account column with account-first sorting.
- [x] Added adapter-owned endpoint policy derived from a model's stored account
  route. OpenRouter retains its configurable endpoint; ChatGPT resolves to the
  fixed Codex Responses endpoint and runs only through its provider-separated
  request adapter.
- [x] Added nullable session account binding, locked it on the first recorded
  request, snapshotted the account on model requests and HTTP attempts, and
  prevented cross-account model changes or fallback for an established
  conversation.
- [x] Applied the approved development-reset strategy in schema version 1:
  there is no migration, and an older schema is rejected with an explicit
  delete-and-relaunch message instead of being converted or deleted by Strappy.
- [x] Added a strict, transactional importer for the versioned bundled ChatGPT
  catalog. Revision upgrades preserve allowed/default preferences and deactivate
  removed models only inside the ChatGPT account.
- [x] Added the ChatGPT Responses adapter with fixed endpoint/header policy,
  provider-specific request bodies, bounded SSE terminal extraction, one safe
  pre-event 401 refresh/retry, and no cross-provider fallback.
- [x] Kept local function tools capability-gated, omitted OpenRouter hosted
  tools on ChatGPT, locked established sessions to one provider account, and
  represented ChatGPT usage as plan usage with no fabricated monetary cost.
- [x] Added matched macOS authentication/preferences, provider-aware model and
  session options on both platforms, and account-stable atomic refresh handling.
- [x] Added compile/runtime ChatGPT kill switches, user documentation, a
  no-persistence live compatibility probe, and a secret-free evidence report.

### Verified

- [x] iOS Clang analyzer: 0 warnings and 0 errors.
- [x] Clean iOS release build and package inspection: universal armv7/arm64
  app in `Strappy.deb`.
- [x] Clean macOS analyzer and release build: 0 warnings/errors and a
  ppc/i386/x86_64/arm64 app, verifying the shared Keychain changes against all
  supported targets.
- [x] All nine Linux shared-core harnesses and the existing OAuth PoC parser
  self-test passed. AddressSanitizer/UndefinedBehaviorSanitizer runs also pass
  for the database/catalog, OAuth, and ChatGPT adapter/SSE harnesses.
- [x] Re-ran the required clean Linux, iOS, and macOS gates on the completed
  experimental implementation on 2026-08-17. All nine Linux checks passed;
  both Apple analyzers reported 0 warnings and 0 errors; and the universal iOS
  and quad-architecture macOS release packages built successfully.
- [x] Added regression coverage for identical wire IDs under two accounts,
  account-scoped OpenRouter refresh, fixed-versus-configurable endpoint policy,
  session/request/attempt account snapshots, cross-account session rejection,
  pinned-session no-fallback behavior, foreign keys, and explicit legacy-schema
  reset rejection.
- [x] Installed package version 1.0.2 over 1.0.0 on `gomadango` (iPhone5,2),
  verified the transferred package checksum, and received user confirmation
  that the live device login completed successfully.
- [x] Added deterministic catalog fixtures plus a loopback ChatGPT transport
  harness covering exact header separation, arbitrary SSE chunking, terminal
  variants, early close, malformed/truncated streams, and subscription cost
  suppression.
- [x] Expanded the OAuth mock matrix for status/structured pending,
  `slow_down`, disabled/denied/expired login, malformed/wrong-type/oversized
  fields, network loss, `invalid_grant` redaction, rotation, and cancellation.

### Still open

- [ ] Run the checked-in live probe for text, refresh, local functions, every
  enabled bundled model, and a real plan-limit/not-included response. Only the
  live iOS device-login step has been confirmed so far.
- [ ] Manually exercise failed Keychain replacement, concurrent request refresh,
  process restart, background/resume, live expiry, and sign-out on both Apple
  platforms; deterministic portable coverage does not substitute for Keychain
  and lifecycle testing.
- [ ] Obtain a production-permitted integration and dedicated Strappy identity
  from OpenAI (or adopt a supported App Server path) before distribution.
- [ ] Complete the remaining rollout review and remove the temporary Pi
  submodule before the intended implementation commit.

## Feasibility conclusion

The feature is technically feasible at the prototype level.

OpenAI [documents device-code login](https://learn.chatgpt.com/docs/auth) as a
beta login method for Codex on headless systems. OpenAI also documents that
"Sign in with ChatGPT" uses a
ChatGPT subscription, while API-key login uses separately billed API usage.
Device-code login may have to be enabled in the user's ChatGPT security
settings or by a workspace administrator.

## Scope

### In scope

- Device-code login only: show a URL and short code, open the browser on
  request, poll in the app, and finish remotely.
- Secure access-token and refresh-token persistence in Keychain.
- Automatic, serialized token refresh.
- The ChatGPT Codex Responses-like backend and its SSE response transport.
- Explicit provider identity throughout models, sessions, requests, tools,
  billing metadata, and preferences.
- OpenRouter as an optional, independently configured provider.
- iOS and macOS UI, with shared portable C protocol code where practical.
- Deterministic Linux harness coverage and clean/analyze/release Apple builds.

### Out of scope for the first release

- Browser redirect or localhost-callback login.
- OpenAI Platform API-key login.
- Importing credentials from Codex CLI or another application.
- WebSocket transport or live token-by-token rendering.
- Translating OpenRouter's hosted web tools to an assumed OpenAI equivalent.
- Switching an established conversation between providers.
- Organization/workspace administration inside Strappy.

## Product assumptions to confirm

The implementation can start with these conservative defaults unless they are
changed before that phase:

- OpenRouter remains the default for existing installations. ChatGPT is opt-in.
- Pre-release databases are disposable for this phase. Strappy performs no
  conversion; an older schema is rejected and the user deletes it before
  relaunching.
- A session's provider account becomes fixed when its first request is
  submitted. A new session is required to change accounts.
- ChatGPT sessions support Strappy's local function tools in the first release,
  but not the OpenRouter-hosted web search/fetch tools.
- The ChatGPT path buffers SSE internally and commits the final response through
  Strappy's existing round transaction. Streaming UI is a later feature.
- A protocol failure never falls back to OpenRouter or an API key. Such a
  fallback could unexpectedly change billing, privacy, model behavior, and the
  party receiving the prompt.

Database strategy was approved on 2026-08-17: replace the development schema in
place while retaining `PRAGMA user_version = 1`, perform no migration, and let
the user delete old databases. Strappy detects and rejects the former schema;
it does not delete a database itself.

## What must change

Today a single endpoint/token pair implicitly means OpenRouter. Model refresh
deactivates the entire catalog, model IDs have no provider namespace, request
headers are OpenRouter-specific, hosted tools use `openrouter:*` types, and
usage records assume metered API pricing. Those assumptions cannot be selected
by testing the endpoint string; provider identity must be first-class.

| Concern | OpenRouter | ChatGPT (Codex) |
| --- | --- | --- |
| Credential | User API token | OAuth access + rotating refresh token |
| Billing | Metered/API-provider rules | Eligible ChatGPT plan limits |
| Endpoint | User-configurable Responses URL | Fixed, adapter-owned Codex backend URL |
| Catalog | `/api/v1/models/user` | Bundled, versioned `BundledModels.json`; no runtime discovery unless a supported catalog API is documented |
| Response transport | One final JSON response | SSE, terminating on a completed/done/incomplete response event |
| Extra headers | `X-OpenRouter-*` | Account ID, beta/stream headers, and an approved Strappy originator |
| Request extensions | OpenRouter `session_id` and tool types | Codex-compatible request fields only |
| Hosted web tools | `openrouter:web_search` / `openrouter:web_fetch` | Disabled initially; no equivalence assumed |
| Cost record | Provider-reported/token price | Tokens and plan-limit state; monetary cost is not `0` unless known |

## Target architecture

```text
iOS/macOS Preferences
        |
        v
StrappyAuthentication  --->  Keychain (one versioned OAuth credential)
        |                          access, refresh, expiry, account ID
        v
portable device/refresh protocol adapter

Session + provider-qualified model
        |
        v
provider registry ---> OpenRouter adapter ----> generic libcurl transport
        |                                      |
        +-----------> ChatGPT adapter ---------+
                                                    |
                          final JSON <--- SSE terminal response extractor
                                                    |
                                           existing response parser/DB round
```

### Provider identity and models

Introduce stable provider IDs distinct from marketing labels:

- `openrouter`
- `openai_chatgpt`
- reserve `openai_api` for a possible future, separately billed API-key provider

Each model needs:

- an internal, provider-qualified key such as
  `openai_chatgpt:gpt-…`;
- `provider_account_id`, whose account row supplies `provider_id`;
- the exact `wire_model_id` sent to the provider;
- capability fields for reasoning, local functions, hosted tools, input types,
  context/output limits, and supported reasoning settings;
- catalog source/revision and catalog-active state; and
- billing kind (`metered_api` or `chatgpt_plan`) independent of token counts.

Add `source/shared/Resources/BundledModels.json` beside the existing shared JSON
resources. Despite its location, it is application catalog data rather than
prompt guidance. Its top level contains a bounded `schema_version`, a
`catalog_revision`, and a `models` array. Each entry contains the provider ID,
provider-qualified internal key, exact wire model ID, display name,
capabilities, supported reasoning levels, input modalities, context/output
limits, and billing kind. It must not contain credentials, per-user
allowed/default choices, or monetary prices for `chatgpt_plan` models.

The initial candidate set, matching the pinned Pi catalog at
`94373d815d2b4a3a48864d5341afc824b8db45e3`, is
`gpt-5.3-codex-spark`, `gpt-5.4`, `gpt-5.4-mini`, `gpt-5.5`,
`gpt-5.6-luna`, `gpt-5.6-sol`, and `gpt-5.6-terra`. Presence in Pi is not by
itself a Strappy compatibility guarantee: omit or mark unavailable any entry
that has not passed Strappy's request/capability fixtures before release.

A shared catalog loader validates and transactionally imports this resource
into SQLite during database initialization and whenever its revision changes.
The import upserts only the bundled provider rows, marks removed entries
inactive only within `openai_chatgpt`, records the imported revision, and
preserves database-owned allowed/default preferences. Cocoa model screens query
the unified database API and never parse the JSON resource directly.

Catalog refresh and deactivation must be provider-scoped. Refreshing
OpenRouter must no longer mark ChatGPT models inactive. ChatGPT login controls
whether those bundled models are usable, not whether they exist in the local
catalog. Pi likewise supplies its OpenAI Codex provider a release-generated
static catalog and does not register a runtime model fetch. Do not call the
public OpenAI `/v1/models` endpoint with a ChatGPT access token, probe an
undocumented private model route, or make production depend on files in the
temporary Pi submodule.

### Session isolation

Provider-specific reasoning payloads and hosted-tool records should not cross
provider boundaries. For the first release:

- a new session is unbound until its first request;
- the first request records and locks `sessions.provider_account_id`;
- later model choices must have the same provider account; and
- the UI offers "New Session with …" instead of changing provider in place.

This prevents encrypted reasoning generated by one provider from being replayed
to another and makes request history and billing auditable. A future explicit
"fork portable transcript" feature could copy only user/assistant text and
standard function-call records to a new provider.

### OAuth state and credential ownership

Add a shared Objective-C coordinator, tentatively `StrappyAuthentication`, at
the same Cocoa boundary currently used by `StrappySession` and
`StrappyKeychain`. It owns login state, marshals UI notifications to the main
thread, loads/saves Keychain data, and performs a single-flight refresh when
multiple requests arrive near expiry.

The portable C OAuth module owns HTTP request construction, bounded JSON/JWT
parsing, polling rules, timeout/cancellation, and token response validation. It
does not own Apple Keychain APIs or UI.

Persist one versioned Keychain credential containing:

- access token;
- refresh token;
- absolute expiry;
- ChatGPT account ID; and
- credential format version.

No OAuth secret, device authorization ID, code verifier, or raw JWT belongs in
SQLite, `.env`, logs, notifications, crash messages, or request ledgers. The
short user code is displayed in memory only and disappears on completion,
cancellation, timeout, or process exit.

The existing Keychain implementation deletes and re-adds an item. OAuth token
rotation needs an update-in-place/replace operation that does not destroy the
last usable refresh token if the new write fails. The coordinator publishes new
credentials only after the complete rotated credential has been stored.

Refresh should begin before expiry with a small clock-skew allowance. An HTTP
401 may trigger exactly one refresh and retry only if no response event was
received; once an SSE response has begun, automatic replay could duplicate a
request. `invalid_grant`, a missing rotated refresh token, or a second 401 marks
the account as requiring sign-in without silently using another provider.

### Device login state machine

The Pi reference currently demonstrates this sequence:

1. `POST https://auth.openai.com/api/accounts/deviceauth/usercode` to begin
   device authorization.
2. Validate `device_auth_id`, `user_code`, and polling interval.
3. Display `https://auth.openai.com/codex/device` and the code; offer Copy,
   Open Browser, and Cancel.
4. Poll `/api/accounts/deviceauth/token` on a worker, respecting the server
   interval, pending responses, `slow_down`, cancellation, and a bounded
   timeout.
5. Exchange the returned authorization code and PKCE verifier at
   `/oauth/token`, using the device callback URI.
6. Validate access token, refresh token, and expiry. Decode the bounded JWT
   payload to obtain the ChatGPT account ID required by the backend, then store
   the complete credential.

All endpoint paths, field names, and claim paths are undocumented protocol
details and must live in one narrow adapter. The UI and database must never
depend on them. Parsing must reject missing fields, overlong values, invalid
base64url, invalid JSON, arithmetic overflow, and unexpected status
transitions. JWT claim extraction supplies a routing header; it is not a local
authorization decision.

### ChatGPT request adapter

The first implementation should use libcurl SSE rather than add WebSockets.
Based on the pinned Pi reference, the compatibility spike should verify:

- `https://chatgpt.com/backend-api/codex/responses`;
- OAuth bearer authorization and the ChatGPT account header;
- `originator: strappy`, an honest Strappy user agent, and required beta/SSE
  headers;
- `store: false` and `stream: true`;
- the exact supported shapes for instructions, input, reasoning effort/summary,
  encrypted reasoning, local function tools, and tool choice; and
- terminal events named `response.completed`, `response.done`, or
  `response.incomplete`, plus structured error events.

Do not send OpenRouter headers, the OpenRouter `session_id` body extension,
OpenRouter hosted-tool types, or OpenRouter pricing metadata on this path. Do
not import Pi's coding system prompt; Strappy should keep its own assistant-set
instructions unless the spike proves a documented backend requirement.

The SSE reader must handle arbitrary chunk boundaries, CRLF/LF, comments,
multiple `data:` lines, unknown events, error events, size limits, cancellation,
and a server that leaves the connection open after the terminal event. It can
extract the terminal `response` object and pass that object to Strappy's
existing final-response parser, preserving the current atomic database round
and non-streaming UI.

Plan-limit errors such as usage exhausted/not included and rate limiting should
be surfaced as ChatGPT-plan errors, including a safe reset time when supplied.
They are not ordinary transient transport errors and must not be represented as
per-token charges.

### Provider-aware tools

Keep standard local function tools provider-neutral. Move hosted-tool encoding
behind provider capabilities:

- OpenRouter continues to emit `openrouter:web_search` and
  `openrouter:web_fetch`.
- ChatGPT initially omits those tools and disables the corresponding preference
  for ChatGPT sessions with a short explanation.
- Add an OpenAI-native hosted search tool only after a live compatibility test
  confirms that the **subscription backend**, not merely the public Responses
  API, supports it for the target plans.

The database should store semantic tool activity plus provider identity, not
raw provider JSON. Unknown response item types should be ignored or recorded as
unsupported without corrupting the conversation round.

### Audit and billing semantics

Add provider, authentication method, transport, and provider-qualified model to
model requests and HTTP attempts. Keep the current policy of recording a
redacted header manifest rather than raw credentials or bodies.

For ChatGPT requests, retain token counts when returned, but distinguish
"subscription plan usage; monetary cost not supplied" from a real zero-dollar
API call. Store semantic plan-limit/reset information from structured errors.
Never infer remaining quota from token counts.

## Database design decision

The clean design changes `models.id` into a globally unique internal model key
and adds `provider_account_id` plus `wire_model_id`, with a uniqueness
constraint on `(provider_account_id, wire_model_id)`. Each account maps to a
stable provider ID. Foreign keys retain their existing `model_id` column names
but point at the internal key. The following also need provider-aware changes:

- default/allowed model preferences;
- sessions and model requests;
- provider-scoped catalog activation;
- HTTP attempts and usage/billing records; and
- any response/tool record that can contain provider-specific opaque data.

Decision, 2026-08-17: use the development reset. The provider-aware schema
replaces the former version-1 layout while `PRAGMA user_version` remains `1`.
There is intentionally no conversion path. Strappy detects the former schema,
returns an explicit reset-required error, and leaves deletion to the user.

The implemented model key is globally unique and account-qualified. Models
store `provider_account_id` and `wire_model_id`; the account row maps to a
code-owned provider adapter. This supports multiple accounts for one provider
without letting a model row supply an arbitrary endpoint. Sessions,
model requests, and HTTP attempts snapshot the account boundary.

## Implementation phases

### Phase 0 — Compatibility probe (live run pending)

The secret-free manual probe and report are checked in. Device login has been
live-verified; refresh, text, local-function, candidate-model, and plan-limit
rows must still be filled from real eligible accounts before release.

Build a non-shipping, manually invoked Linux probe using the existing curl/cJSON
stack. It must not read or print API keys, persist OAuth credentials, or run in
CI. Use it with a test ChatGPT account to verify:

- device login enabled and disabled behavior;
- an honest Strappy originator and user agent;
- token refresh and account-ID extraction;
- one simple text response and one local function-call round;
- SSE terminal/error shapes and cancellation;
- plan-limit behavior where practical; and
- that usage appears on the ChatGPT/Codex plan rather than OpenAI Platform API
  billing.

Exit criterion: a short checked-in compatibility fixture/report containing no
secrets and recording the tested protocol date and behavior rather than real
identifiers or payloads.

### Phase 1 — Provider domain and database (implemented)

- [x] Add provider IDs/accounts, provider-qualified model keys, and exact wire
  model IDs to shared C types.
- [x] Apply the explicitly approved development-reset strategy without a
  migration or `user_version` change.
- [x] Make OpenRouter catalog activation, whitelist/default lookup, and model
  queries account-safe.
- [x] Add the bounded, versioned `source/shared/Resources/BundledModels.json`
  schema and seed it with the Pi-matched ChatGPT candidate set above.
- [x] Add a shared validator/importer that transactionally upserts the bundled
  catalog, scopes deactivation to `openai_chatgpt`, records the imported
  revision, and preserves database-owned user preferences.
- [x] Bundle the resource on both platforms. Rename the misleading
  `GUIDANCE_RESOURCES` Makefile variable to a general shared-resource name when
  adding the catalog.
- [x] Reject historical schema data rather than convert it, and enforce session
  account locking on the first request.
- [x] Replace OpenRouter-named public model-record APIs with generic APIs while
  retaining short-lived compatibility wrappers.
- [x] Resolve the endpoint through the selected model's account and a
  code-owned provider policy; keep endpoints adapter-owned.

Exit criterion: both providers may contain the same wire model ID without
collision; importing the same bundled revision is idempotent; a catalog upgrade
preserves allowed/default choices; refreshing one provider cannot change the
other; malformed resources roll back without damaging the last-known catalog;
and all DB harness tests and foreign-key checks pass.

### Phase 2 — OAuth protocol and secure credential lifecycle

Status: implemented on iOS and macOS. The expanded portable protocol matrix
passes; Apple Keychain failure injection and lifecycle/manual cases remain.

- [x] Implement bounded device-start, polling, exchange, refresh, JWT claim parsing,
  timeout, and cancellation in portable C.
- [x] Add the Cocoa authentication coordinator and an observable state machine:
  signed out, requesting code, awaiting user, exchanging, signed in, refreshing,
  needs sign-in, and error/cancelled.
- [x] Add atomic Keychain credential replacement and provider-specific delete.
- [x] Serialize concurrent refreshes and return immutable access/account snapshots to
  requests.
- [x] Implement local sign-out. Do not claim remote revocation unless a supported
  revocation endpoint is later documented.

Exit criterion: deterministic mock-server tests cover every state and no secret
appears in diagnostics, SQLite, or fixtures.

### Phase 3 — Provider request adapters and SSE (implemented)

- [x] Separate generic curl transport from OpenRouter header/body policy.
- [x] Preserve OpenRouter behavior with existing response-loop regression tests.
- [x] Add ChatGPT URL/header/body construction isolated behind provider policy.
- [x] Add the bounded SSE terminal-response extractor and structured error mapping.
- [x] Add refresh-before-send and the narrowly safe one-time 401 retry.
- [x] Feed the terminal response through the existing parser and database round.

Exit criterion: scripted local servers prove success, incomplete, quota error,
malformed stream, timeout, cancellation, early terminal close, and header
separation. Existing response-loop tests remain green.

### Phase 4 — Models, tools, and session behavior (implemented; live model matrix pending)

- [x] Establish the release-time review, compatibility-test, and revision-bump
  procedure for `BundledModels.json`; there is no ChatGPT runtime catalog fetch.
- [x] Generate request reasoning fields from model capabilities rather than the
  current OpenRouter assumptions.
- [x] Keep local functions available when verified; gate hosted tools by provider.
- [x] Disable provider changes on non-empty sessions and require a new session.
- [x] Make missing credentials, unsupported models, and plan limits actionable in
  the UI without automatic fallback.
- [x] Preserve token usage while reporting subscription billing semantics honestly.

Exit criterion: request fixtures for every shipped ChatGPT model/capability and
cross-provider history tests pass.

### Phase 5 — iOS and macOS preferences

Status: implemented on iOS and macOS; matched lifecycle behavior still requires
the manual Apple-platform matrix.

Replace the single implicit OpenRouter credentials form with two provider
sections:

- **OpenRouter:** endpoint, API token, save, and current validation behavior.
- **ChatGPT (Codex):** Sign In; verification URL; large selectable code; Copy
  Code; Open Browser; Cancel; signed-in/refresh-needed state; and Sign Out.

The flow must stay responsive, survive preference-view recreation while the app
process remains alive, and marshal all control changes to the main thread. App
termination may cancel the ephemeral device flow; the user simply starts again.
No callback URL scheme is required.

Model whitelisting is now grouped by account and reads the unified database
catalog. Still group model selection by account and explain why ChatGPT hosted
web tools are unavailable in the initial release. OpenRouter retains its
network-backed **Update** action; once the bundled catalog lands, ChatGPT must
show its catalog revision and have no refresh action. Bundled ChatGPT models
remain visible while signed out but are marked unavailable until authentication
succeeds. Add accessibility labels and keyboard navigation for the code and
actions.

Exit criterion: matched behavior and error messages on iOS and macOS, including
disabled device login, cancellation, timeout, background/resume, restart with a
stored credential, refresh, and sign-out.

### Phase 6 — Hardening, documentation, and rollout (in progress)

- [x] Add a runtime/compile-time kill switch for the ChatGPT provider while the
  protocol remains undocumented/beta.
- [x] Document subscription eligibility, workspace controls, plan limits, privacy,
  troubleshooting, and the distinction from API-key billing.
- [x] Review every log/error path for bearer tokens, refresh tokens, JWTs, device
  IDs, codes, and verifier leakage.
- [x] Verify old SDK/architecture compatibility and update all explicit iOS, macOS,
  and Linux Makefile source lists.
- [ ] Remove the temporary Pi submodule before the implementation is committed.
- [x] Keep the implementation independent; Pi remains a pinned behavioral
  reference only. If that changes, retain its MIT copyright/license notice in
  the appropriate third-party file and source comments.

Exit criterion: all automated and manual gates below pass, the feature can be
disabled without affecting OpenRouter.

## Expected file-level work

Names are tentative, but boundaries should remain narrow.

| Area | Expected files |
| --- | --- |
| Provider types/capabilities | new `source/shared/strappy_provider.[ch]` |
| Device OAuth | new `source/shared/strappy_openai_oauth.[ch]` |
| SSE parsing | new `source/shared/strappy_sse.[ch]` or a private ChatGPT adapter module |
| Credential coordination | iOS `source/iOS/StrappyAuthentication.[hm]`; extended shared `StrappyKeychain` and `XPKeychain` |
| HTTP/request adapters | refactor `strappy_client.[ch]` and `strappy_responses.[ch]` |
| Configuration | clarify legacy OpenRouter fields in `strappy_config.[ch]`; do not add OAuth-token environment variables |
| Persistence/catalog | `strappy_db*.c`, `strappy_db*.h`, `strappy_model_catalog.[ch]`, and new `source/shared/Resources/BundledModels.json` |
| Hosted tools | provider-gate `Resources/GuidanceTools.json` and request building |
| Cocoa boundary | `StrappySession.[hm]` plus focused authentication calls |
| iOS UI | `PreferencesTableViewController.m` and model/session option views |
| macOS UI | `StrappyPreferencesAuthenticationView.m` and model/session option views/controllers |
| Builds/tests | all three Makefiles, `responses_harness.c`, `database_query_harness.c`, and focused new harness files if needed |
| Documentation/licensing | README/auth help and third-party notices if Pi-derived code is retained |

## Automated test gates

### OAuth and secrets

- Valid begin/pending/authorized/exchange flow.
- Pending as both HTTP status and structured error.
- `slow_down`, server interval, bounded timeout, cancellation before/during I/O,
  and no busy polling.
- Missing, wrong-type, oversized, and malformed response fields.
- JWT base64url padding, missing claim, malformed payload, and size limits.
- Refresh-token rotation, near-expiry refresh, failed Keychain replacement,
  invalid grant, and concurrent single-flight refresh.
- Assertions that secret values never occur in logs, ledger fields, SQLite, or
  fixture failure messages.

### Providers and requests

- OpenRouter request snapshots retain their endpoint, headers, body, and hosted
  tools.
- ChatGPT request snapshots contain required fields and no OpenRouter headers,
  extensions, hosted tools, or API key.
- Provider/model mismatch fails before network I/O.
- No automatic cross-provider fallback.
- A 401 retries at most once before any stream event and never after one.

### SSE and responses

- Arbitrary byte splits, CRLF/LF, comments, multiple data lines, and unknown
  events.
- Completed, done, incomplete, failed, and top-level error events.
- Terminal event followed by an open connection.
- Malformed/oversized events, HTTP errors, timeout, and cancellation.
- Text, reasoning/encrypted content, local function call/output, usage, and
  existing multi-round transaction behavior.

### Database and catalogs

- Strict bundled-manifest validation: supported schema/revision, bounded field
  sizes and counts, unique provider-qualified keys, valid wire IDs, known
  capabilities, and no monetary prices on `chatgpt_plan` entries.
- First import, repeated same-revision import, revision upgrade, removed-model
  deactivation, missing resource, malformed resource, and transactional
  rollback while retaining the last-known good catalog.
- Identical wire IDs under two providers.
- Provider-scoped refresh/deactivation and whitelist/default behavior.
- Existing-data conversion or explicit reset behavior, foreign-key checks, and
  rollback on migration failure if migration is chosen.
- Provider lock on non-empty sessions and safe handling of historical
  OpenRouter-specific reasoning/tool records.
- Subscription usage represented as unknown monetary cost, not fabricated zero.

## Required build and manual gates

Run from the repository root after each implementation phase that changes code:

```sh
make -C source/linux clean test
make -C source/iOS clean analyze release
make -C source/macOS clean analyze release
```

Before release, manually test at least:

- one eligible personal ChatGPT plan;
- one managed workspace if managed workspaces are in the supported scope;
- device login enabled, disabled, cancelled, expired, and denied;
- app restart, proactive refresh, expired refresh token, and local sign-out;
- successful text and local function-tool rounds;
- an unavailable model and a plan/rate-limit response;
- network loss during polling, exchange, refresh, and SSE; and
- the complete existing OpenRouter login, catalog, model selection, tools, and
  response loop on both iOS and macOS.

Live credentials must never be added to CI or captured in test artifacts.

## Security and privacy checklist

- TLS verification and the repository CA policy stay enabled for every OAuth and
  backend request.
- URLs are adapter constants; no redirect or endpoint supplied by a response is
  followed without validation.
- OAuth responses, JWTs, SSE events, and error bodies have explicit byte/depth
  limits before parsing or display.
- Device polling uses cancellation and monotonic deadlines where supported.
- Secrets are short-lived in memory, copied minimally, and cleared when buffers
  are released where the platform/compiler permits.
- Keychain entries are provider-specific and local to each app/device.
- UI and logs show a safe account state, not access tokens or the full account
  claim.
- Sign-out removes only ChatGPT credentials and never deletes conversations or
  OpenRouter credentials.
- The selected provider is visible before a prompt is sent so users understand
  which service receives prompts, tool schemas, and database-derived results.
- Workspace controls and data handling are described without claiming that
  ChatGPT subscription traffic has the same policy as OpenAI Platform API
  traffic.

## Rollback strategy

OpenRouter remains a complete independent path. The ChatGPT provider should be
feature-gated at registration/UI level, so a backend protocol change can hide
new sign-in and reject new ChatGPT requests with a clear compatibility message
without changing existing OpenRouter configuration or data. Stored ChatGPT
credentials remain in Keychain until explicit sign-out; disabling the feature
must not print, migrate, or expose them.

## Completion criteria

This project is complete only when:

- the approved development-reset database strategy remains enforced;
- device login, secure refresh, and sign-out work on iOS and macOS;
- a ChatGPT-backed session completes text and local function-tool rounds using
  no API key;
- ChatGPT usage is shown with plan-limit semantics and no fabricated price;
- OpenRouter remains fully functional and independently optional;
- provider-specific data cannot cross session, request, catalog, or tool
  boundaries accidentally;
- all automated, analyze, release, manual, security, and regression gates pass;
  and
- the Pi submodule is absent from the intended commit, with attribution retained
  only if its implementation was materially copied.

## References

- OpenAI, [Codex pricing](https://developers.openai.com/codex/pricing/): plan
  inclusion and the distinction from API-key pricing.
- Temporary local Pi reference at commit
  `94373d815d2b4a3a48864d5341afc824b8db45e3`:
  `packages/ai/src/auth/oauth/openai-codex.ts`,
  `packages/ai/src/auth/oauth/device-code.ts`,
  `packages/ai/src/providers/openai-codex.ts`,
  `packages/ai/src/api/openai-codex-responses.ts`, and their focused tests.

## Want to do

### Confirm the supported production integration and Strappy identity

Status: **Want to do**

All OAuth/backend release-authority and client-identity work is tracked here.
The current experimental development build uses the client identity
demonstrated by the pinned Pi reference at the user's explicit direction. The
successful live login establishes development compatibility only; it is not
production authorization. Strappy must never ship while identifying itself as
Pi, Codex, or another client merely to pass an OAuth or backend check.

Official OpenAI documentation reviewed on 2026-08-17 adds a potentially
supported alternative to Strappy's direct private-protocol approach:

- OpenAI now documents Codex App Server as the interface for embedding Codex
  into another product, including authentication, conversation history,
  approvals, and streamed agent events.
- App Server's JSON-RPC account surface supports managed ChatGPT browser and
  device-code login, automatic token persistence and refresh, cancellation,
  logout, plan identification, rate-limit state, and token-usage summaries.
- The documented device-code method lets the client own the sign-in ceremony
  and return the verification URL and one-time code to its UI.
- The App Server command and WebSocket transport are currently described as
  experimental and unsupported for production workloads. The default stdio
  transport is JSONL, but the documentation does not establish support for
  Strappy's old iOS/macOS runtimes and armv7, PPC, x86, x64, and arm64 target
  matrix, nor does it grant an explicit redistribution or porting right for a
  production Strappy build.
- The reviewed official documentation does not publish a self-service
  third-party OAuth client-registration form. It also does not document the raw
  device OAuth endpoints or the
  `chatgpt.com/backend-api/codex/responses` contract as a general third-party
  production API.

Wanted work:

1. Determine whether Codex App Server can be built, bundled, or otherwise used
   on every required Strappy target without weakening the legacy-platform
   promise or moving user prompts through an unintended intermediary.
2. Ask OpenAI whether App Server is the required and permitted integration for
   a distributed third-party Strappy client and whether its production and
   redistribution status covers these targets.
3. If App Server is not a viable supported route, request a dedicated public
   Strappy OAuth client ID plus explicit authorization to use the ChatGPT Codex
   subscription backend directly.
4. Ask OpenAI to confirm eligible plans, workspace controls, permitted end-user
   distribution, billing semantics, supported models/tools, and the production
   compatibility policy.
5. Record the response, date, and approved integration boundaries without
   recording credentials, account identifiers, or private payloads. Do not
   release the direct backend path until written authorization is obtained.

Completion gate: do not release the ChatGPT provider until OpenAI confirms a
production-permitted integration path and Strappy either uses that supported
path or has its own explicitly approved client identity and backend access.

Proposed request:

> We are developing Strappy, a native iOS and macOS client that lets users
> authenticate with their own eligible ChatGPT account and use Codex under
> their ChatGPT plan. We will not ship another application's OAuth identity or
> treat ChatGPT OAuth tokens as OpenAI Platform API keys. Is Codex App Server
> the required and production-supported integration for this distributed
> third-party use case, including our legacy Apple targets? If App Server
> cannot support those targets, can OpenAI issue Strappy a dedicated public
> OAuth client ID and explicitly authorize direct use of the ChatGPT Codex
> subscription backend? Please also confirm permitted distribution, eligible
> plans and workspace controls, billing semantics, supported capabilities, and
> the applicable production compatibility policy.

Official sources:

- OpenAI, [Codex App Server](https://learn.chatgpt.com/docs/app-server): product
  embedding, JSON-RPC transports, managed ChatGPT authentication, device-code
  UI ownership, refresh/logout, and plan-limit/usage surfaces.
- OpenAI, [Codex authentication](https://learn.chatgpt.com/docs/auth): ChatGPT
  subscription versus API-key authentication, token refresh, device-code beta
  status, and personal/workspace enablement.
- OpenAI, [Codex CLI reference](https://developers.openai.com/codex/cli/reference/):
  the `codex login --device-auth` user flow.
