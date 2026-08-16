# ChatGPT Device OAuth and Subscription Backend Plan

Status: proposed, 2026-08-16

This plan adds a second provider, `openai_chatgpt`, whose product label is
"ChatGPT (Codex)". It signs in with the device-code flow and sends requests to
the ChatGPT Codex backend so usage is charged against the user's eligible
ChatGPT plan limits rather than an OpenAI Platform API key.

The plan intentionally keeps OpenRouter working. It does not turn a ChatGPT
OAuth access token into an API key, and it does not send that token to the
public `api.openai.com/v1` API.

## Feasibility conclusion

The feature is technically feasible, with one important release condition.

OpenAI documents device-code login as a beta login method for Codex on
headless systems. OpenAI also documents that "Sign in with ChatGPT" uses a
ChatGPT subscription, while API-key login uses separately billed API usage.
Device-code login may have to be enabled in the user's ChatGPT security
settings or by a workspace administrator.

OpenAI's public documentation does **not** publish the raw device OAuth
endpoints, a general third-party client-registration process, or the
`chatgpt.com/backend-api/codex/responses` wire contract. The temporary `pi`
checkout demonstrates a working implementation at commit
`94373d815d2b4a3a48864d5341afc824b8db45e3`, but that implementation is a
reference, not an OpenAI compatibility guarantee.

Therefore Phase 0 must establish both:

1. the flow works with a Strappy identity and an eligible ChatGPT account; and
2. shipping a third-party client on this OAuth client/backend is permitted.

If OpenAI requires client registration, Strappy must use an approved Strappy
client ID. Strappy must not ship by identifying itself as Pi or another OpenAI
client merely to pass a backend check.

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
- Existing sessions and models are assigned to OpenRouter during any database
  conversion.
- A session's provider becomes fixed when its first request is submitted. A new
  session is required to change providers.
- ChatGPT sessions support Strappy's local function tools in the first release,
  but not the OpenRouter-hosted web search/fetch tools.
- The ChatGPT path buffers SSE internally and commits the final response through
  Strappy's existing round transaction. Streaming UI is a later feature.
- A protocol failure never falls back to OpenRouter or an API key. Such a
  fallback could unexpectedly change billing, privacy, model behavior, and the
  party receiving the prompt.

Two decisions require explicit approval before production implementation:

1. **OAuth/backend release authority:** confirm an approved client identity and
   acceptable use of the subscription backend.
2. **Database strategy:** approve either a development-database reset or an
   actual schema migration. Repository instructions currently pin
   `PRAGMA user_version` to `1` and prohibit a migration/version change without
   permission.

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
| Catalog | `/api/v1/models/user` | Bundled, versioned manifest until a supported catalog API exists |
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
- `provider_id`;
- the exact `wire_model_id` sent to the provider;
- capability fields for reasoning, local functions, hosted tools, input types,
  context/output limits, and supported reasoning settings;
- catalog source/revision and active/allowed state; and
- billing kind (`metered_api` or `chatgpt_plan`) independent of token counts.

Catalog refresh must be provider-scoped. Refreshing OpenRouter must no longer
mark ChatGPT models inactive. ChatGPT models should come from a small,
versioned Strappy resource that is updated and tested at release time. Do not
call the public OpenAI `/v1/models` endpoint with a ChatGPT access token and do
not make production depend on files in the temporary Pi submodule.

### Session isolation

Provider-specific reasoning payloads and hosted-tool records should not cross
provider boundaries. For the first release:

- a new session is unbound until its first request;
- the first request records and locks `sessions.provider_id`;
- later model choices must have the same provider; and
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

1. `POST https://auth.openai.com/api/accounts/deviceauth/usercode` with the
   approved client ID.
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

All endpoint paths, field names, client identity, and claim paths are
undocumented protocol details and must live in one narrow adapter. The UI and
database must never depend on them. Parsing must reject missing fields,
overlong values, invalid base64url, invalid JSON, arithmetic overflow, and
unexpected status transitions. JWT claim extraction supplies a routing header;
it is not a local authorization decision.

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

## Database design gate

The clean design changes `models.id` into a globally unique internal model key
and adds `provider_id` plus `wire_model_id`, with a uniqueness constraint on
`(provider_id, wire_model_id)`. Foreign keys may retain their existing
`model_id` column names but point at the internal key. The following also need
provider-aware changes:

- default/allowed model preferences;
- sessions and model requests;
- provider-scoped catalog activation;
- HTTP attempts and usage/billing records; and
- any response/tool record that can contain provider-specific opaque data.

This cannot safely be slipped into schema version 1. Before editing the schema,
choose one path:

1. **Development reset:** explicitly approve deleting/recreating local
   pre-release databases and replace the version-1 schema in place; or
2. **Migration:** explicitly approve a new `user_version`, write a transactional
   migration that prefixes existing model IDs with `openrouter:`, updates every
   dependent foreign key, assigns existing sessions/requests to OpenRouter,
   verifies counts and foreign keys, and preserves rollback on failure.

Until that decision is made, schema work is blocked; OAuth protocol and
transport harnesses can still proceed independently.

## Implementation phases

### Phase 0 — Compatibility and release gate

Build a non-shipping, manually invoked Linux probe using the existing curl/cJSON
stack. It must not read or print API keys, persist OAuth credentials, or run in
CI. Use it with a test ChatGPT account to verify:

- device login enabled and disabled behavior;
- an approved client ID and honest Strappy originator;
- token refresh and account-ID extraction;
- one simple text response and one local function-call round;
- SSE terminal/error shapes and cancellation;
- plan-limit behavior where practical; and
- that usage appears on the ChatGPT/Codex plan rather than OpenAI Platform API
  billing.

Separately resolve the authorization/terms question. Record the tested protocol
date and behavior, not real identifiers or payloads. Stop here if Strappy cannot
obtain an approved identity or the backend rejects an honest Strappy client.

Exit criterion: a short checked-in compatibility fixture/report containing no
secrets, plus explicit approval to proceed.

### Phase 1 — Provider domain and database

- Add provider IDs, provider-qualified model keys, capabilities, and billing
  kinds to shared C types.
- Apply the explicitly approved database reset or migration strategy.
- Make catalog activation, whitelist/default lookup, and model queries
  provider-safe.
- Import an initial versioned ChatGPT model manifest into SQLite.
- Assign converted historical data to OpenRouter and enforce session provider
  locking.
- Replace OpenRouter-named public model-record APIs with generic APIs, retaining
  short-lived compatibility wrappers if they reduce review risk.

Exit criterion: both providers may contain the same wire model ID without
collision; refreshing one catalog cannot change the other; all DB harness tests
and foreign-key checks pass.

### Phase 2 — OAuth protocol and secure credential lifecycle

- Implement bounded device-start, polling, exchange, refresh, JWT claim parsing,
  timeout, and cancellation in portable C.
- Add the Cocoa authentication coordinator and an observable state machine:
  signed out, requesting code, awaiting user, exchanging, signed in, refreshing,
  needs sign-in, and error/cancelled.
- Add atomic Keychain credential replacement and provider-specific delete.
- Coalesce concurrent refreshes and return immutable access/account snapshots to
  requests.
- Implement local sign-out. Do not claim remote revocation unless a supported
  revocation endpoint is later documented.

Exit criterion: deterministic mock-server tests cover every state and no secret
appears in diagnostics, SQLite, or fixtures.

### Phase 3 — Provider request adapters and SSE

- Separate generic curl transport from OpenRouter header/body policy.
- Preserve OpenRouter byte-for-byte behavior with request snapshot tests.
- Add ChatGPT URL/header/body construction using only fields proven in Phase 0.
- Add the bounded SSE terminal-response extractor and structured error mapping.
- Add refresh-before-send and the narrowly safe one-time 401 retry.
- Feed the terminal response through the existing parser and database round.

Exit criterion: scripted local servers prove success, incomplete, quota error,
malformed stream, timeout, cancellation, early terminal close, and header
separation. Existing response-loop tests remain green.

### Phase 4 — Models, tools, and session behavior

- Add the curated ChatGPT model resource and release-time update procedure.
- Generate request reasoning fields from model capabilities rather than the
  current OpenRouter assumptions.
- Keep local functions available when verified; gate hosted tools by provider.
- Disable provider changes on non-empty sessions and provide a new-session path.
- Make missing credentials, unsupported models, and plan limits actionable in
  the UI without automatic fallback.
- Preserve token usage while reporting subscription billing semantics honestly.

Exit criterion: request fixtures for every shipped ChatGPT model/capability and
cross-provider history tests pass.

### Phase 5 — iOS and macOS preferences

Replace the single implicit OpenRouter credentials form with two provider
sections:

- **OpenRouter:** endpoint, API token, save, and current validation behavior.
- **ChatGPT (Codex):** Sign In; verification URL; large selectable code; Copy
  Code; Open Browser; Cancel; signed-in/refresh-needed state; and Sign Out.

The flow must stay responsive, survive preference-view recreation while the app
process remains alive, and marshal all control changes to the main thread. App
termination may cancel the ephemeral device flow; the user simply starts again.
No callback URL scheme is required.

Group model selection/whitelisting by provider and explain why ChatGPT hosted
web tools are unavailable in the initial release. Add accessibility labels and
keyboard navigation for the code and actions.

Exit criterion: matched behavior and error messages on iOS and macOS, including
disabled device login, cancellation, timeout, background/resume, restart with a
stored credential, refresh, and sign-out.

### Phase 6 — Hardening, documentation, and rollout

- Add a runtime/compile-time kill switch for the ChatGPT provider while the
  protocol remains undocumented/beta.
- Document subscription eligibility, workspace controls, plan limits, privacy,
  troubleshooting, and the distinction from API-key billing.
- Review every log/error path for bearer tokens, refresh tokens, JWTs, device
  IDs, codes, and verifier leakage.
- Verify old SDK/architecture compatibility and update all explicit iOS, macOS,
  and Linux Makefile source lists.
- Remove the temporary Pi submodule before the implementation is committed.
- If substantial Pi code is adapted rather than independently implemented,
  retain its MIT copyright/license notice in the appropriate third-party file
  and source comments.

Exit criterion: all automated and manual gates below pass, the feature can be
disabled without affecting OpenRouter, and release authority remains valid.

## Expected file-level work

Names are tentative, but boundaries should remain narrow.

| Area | Expected files |
| --- | --- |
| Provider types/capabilities | new `source/shared/strappy_provider.[ch]` |
| Device OAuth | new `source/shared/strappy_openai_oauth.[ch]` |
| SSE parsing | new `source/shared/strappy_sse.[ch]` or a private ChatGPT adapter module |
| Credential coordination | new `source/shared/StrappyAuthentication.[hm]`; extend `StrappyKeychain` and `XPKeychain` |
| HTTP/request adapters | refactor `strappy_client.[ch]` and `strappy_responses.[ch]` |
| Configuration | clarify legacy OpenRouter fields in `strappy_config.[ch]`; do not add OAuth-token environment variables |
| Persistence/catalog | `strappy_db*.c`, `strappy_db*.h`, and a new versioned ChatGPT model resource |
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

- the release-authority and database gates are explicitly resolved;
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

- OpenAI, [Codex authentication](https://developers.openai.com/codex/auth/):
  ChatGPT subscription vs API-key authentication, token storage/refresh, device
  login beta status, and workspace enablement.
- OpenAI, [Codex CLI reference](https://developers.openai.com/codex/cli/reference/):
  `codex login --device-auth` user flow.
- OpenAI, [Codex pricing](https://developers.openai.com/codex/pricing/): plan
  inclusion and the distinction from API-key pricing.
- Temporary local Pi reference at commit
  `94373d815d2b4a3a48864d5341afc824b8db45e3`:
  `packages/ai/src/auth/oauth/openai-codex.ts`,
  `packages/ai/src/auth/oauth/device-code.ts`,
  `packages/ai/src/providers/openai-codex.ts`,
  `packages/ai/src/api/openai-codex-responses.ts`, and their focused tests.
