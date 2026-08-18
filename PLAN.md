# Provider and Multi-Account Plan

Status: ChatGPT OAuth, ChatGPT Responses, native web search, provider-aware
models, and provider-locked sessions work. The next architectural step is to
support named accounts for OpenRouter, OpenAI, and a minimal custom Responses
provider without changing the current iOS or macOS user interface.

This plan replaces the temporary assumption that a provider ID is also its one
account ID. It keeps the working OAuth implementation and makes the provider,
account, credential, model, and session boundaries explicit.

## Current problem

The database has a `provider_accounts` table, but startup creates exactly two
fixed rows whose IDs are also the provider IDs: `openrouter` and
`openai_chatgpt`. The rest of the implementation still relies on that one-to-one
relationship:

- OpenRouter has one global endpoint/token Keychain record.
- ChatGPT has one global OAuth credential and one global authentication state
  machine.
- The ChatGPT bundled catalog contains a fixed provider-account ID.
- OpenRouter catalog refresh writes only to the fixed OpenRouter account.
- The request credential callback does not receive an account ID.
- Provider endpoints, catalog behavior, request differences, hosted tools, and
  error/billing rules are selected in several different modules.

The existing session and request tables already snapshot a
`provider_account_id`. That is the right boundary, but it must refer to a real
account instance rather than a provider singleton.

## Goals

- Allow any number of OpenRouter accounts and ChatGPT accounts.
- Give every account a required, arbitrary, user-editable display name.
- Give every account an opaque, stable Strappy account ID independent of the
  provider ID and any upstream account identifier.
- Support a minimal `other` provider for manually configured Responses API
  endpoints without model discovery or hosted web tools.
- Centralize each provider's static behavior in shared portable C code.
- Store secrets only in account-keyed Keychain entries, never in SQLite.
- Scope models, preferences, catalog refreshes, OAuth refreshes, requests,
  retries, usage, and sign-out to one exact account.
- Preserve the rule that a conversation cannot change accounts after its first
  request.
- Keep OpenRouter and the working ChatGPT OAuth path behaviorally unchanged
  while the architecture is refactored.
- Keep the current iOS and macOS layouts, controls, and user-visible workflows
  unchanged in this phase.
- Make `SessionOptions` select an exact provider account; resolve the provider
  type from that account instead of storing duplicate provider state.

## Non-goals for this phase

- Designing account-list, add-account, rename-account, or account-switching UI.
- Switching an established conversation to another account.
- Falling back to another account or provider after an authentication,
  transport, model, tool, or plan-limit failure.
- Storing bearer tokens, refresh tokens, API keys, OAuth claims, request bodies,
  response bodies, or HTTP headers in SQLite.
- Treating a ChatGPT OAuth token as an OpenAI Platform API key.
- Adding an `openai_api` provider. It may be added later as a distinct provider
  with API-key billing and behavior.
- Assuming every Responses-compatible endpoint implements OpenRouter or
  ChatGPT extensions.

## Core design

### Provider definitions are code-owned

Create one provider registry in `strappy_provider.[ch]`. A provider definition
is immutable application behavior, not a database row and not user input. Each
definition owns or dispatches:

- stable provider ID and display name;
- credential kind (`api_token` or `oauth_device`);
- default Responses endpoint and any other fixed service endpoints;
- validation of permitted per-account endpoint overrides;
- model catalog source and refresh/import operation;
- request URL, headers, body extensions, and response transport;
- hosted-tool names and provider-specific request encoding;
- provider error classification, rate/plan-limit semantics, and billing kind;
- compile-time and runtime availability checks; and
- credential snapshot, refresh, and sign-out operations.

Callers resolve a provider definition by `provider_id` and invoke a narrow
provider operation. They must not infer provider behavior from an endpoint,
credential shape, account ID, model ID prefix, or scattered provider `if`
statements.

The initial registry contains:

| Provider ID | Credentials | Responses endpoint | Models | Hosted web tool |
| --- | --- | --- | --- | --- |
| `openrouter` | API token | provider default with the existing validated account override | account-authenticated remote catalog | OpenRouter search/fetch extensions |
| `openai_chatgpt` | device OAuth | fixed Codex Responses endpoint | bundled provider catalog, materialized per account | native `web_search` |
| `other` | optional bearer API token | required validated account endpoint | manually registered only | none |

OAuth protocol details may remain in `strappy_openai_oauth.[ch]`, generic HTTP
transport may remain shared, and provider-specific parsing/building may remain
in focused modules. The registry is the single public dispatch point; it does
not require one very large source file.

### Accounts are runtime instances

`provider_accounts.id` becomes an opaque Strappy-generated identifier. It must
not equal the provider ID and must not be derived from an API key, token, email,
or upstream OAuth account ID.

Each account stores only non-secret application state:

- `id`;
- `provider_id`;
- required user-visible display name;
- lifecycle state such as active or archived;
- optional validated non-secret configuration, such as an OpenRouter endpoint
  override;
- creation, update, and last-used timestamps.

Account names are labels, not identities. They do not need to be unique and
changing one cannot change routing, credentials, models, or history. OpenRouter
uses an arbitrary name supplied by the user. ChatGPT OAuth may provide a useful
name or email claim; when present and safe to display, it may be used as the
initial suggested name. The implementation must not require that claim because
the working OAuth flow currently guarantees only the upstream account ID. Use a
stable fallback such as `ChatGPT Account`, allow the label to be edited, and do
not use an email or name for credential lookup or duplicate detection.

The upstream ChatGPT account ID remains inside the account's Keychain
credential. SQLite uses only the opaque Strappy account ID. The credential
layer may compare the upstream ID during refresh or duplicate-account checks,
but it must not expose it as the database primary key. A display name may itself
be personal information, including an email chosen or accepted by the user, so
it must not be emitted in network diagnostics or secret-free test reports.

Accounts with conversation history are archived instead of deleted. Sign-out
removes only that account's credential and prevents new requests; it does not
delete the account row, models, sessions, history, or another account's
credential.

### Credentials are keyed by account

Replace the provider-wide Keychain APIs with account-aware operations:

```text
load(provider_id, provider_account_id)
save(provider_id, provider_account_id, credential)
delete(provider_id, provider_account_id)
snapshot(provider_id, provider_account_id, refresh_policy)
```

- OpenRouter stores one API token per account. Its endpoint override is
  non-secret account configuration; the provider definition supplies the
  default endpoint.
- ChatGPT stores one versioned OAuth credential per account: access token,
  rotating refresh token, expiry, and upstream account ID.
- Other may store one bearer API token per account when its endpoint requires
  authentication. Its required endpoint is non-secret account configuration;
  an endpoint that requires no authentication has no Keychain credential.
- Keychain service names separate providers and Keychain account names use the
  opaque Strappy account ID.
- OAuth refresh is serialized per account, not globally. Requests for different
  accounts may refresh independently.
- Atomic replacement preserves the previous usable credential if a rotated
  credential cannot be saved.
- Environment OpenRouter credentials map to one deterministic ephemeral/default
  account and do not silently override every stored OpenRouter account.

The Responses credential callback must accept `provider_account_id`. Every
request obtains an immutable credential/account snapshot for its selected
account, and a 401 retry refreshes only that same account.

### Models remain account-qualified

Models remain routed through an account because catalog availability, pricing,
eligibility, and credentials can differ between two accounts using the same
provider. A model identity is therefore:

```text
provider_account_id + wire_model_id
```

The internal `model_id` may continue to be generated from those values, but no
code may assume a provider ID is the prefix. Identical wire model IDs under two
accounts must produce separate model rows and separate preference state.

Change `BundledModels.json` from an account catalog into a provider catalog:

- keep `provider_id`, `wire_model_id`, display data, capabilities, reasoning
  mappings, limits, hosted tools, and billing kind;
- remove fixed `provider_account_id` and account-qualified `model_id` values;
  and
- materialize the bundled models for every active ChatGPT account during
  account creation and catalog revision import.

OpenRouter refresh receives the target account ID, endpoint configuration, and
that account's credential. It deactivates and upserts models only for that
account. Refreshing one account must never alter another account's models,
allowed choices, or default selection.

### The `other` provider is intentionally minimal

It makes sense to add `other` as one provider type whose accounts point at
user-supplied Responses-compatible endpoints. This avoids creating a new
provider definition for every compatible server while keeping its behavior
strict and predictable:

- require an absolute, validated Responses endpoint on each account;
- use the account's bearer token when present and the common Responses
  request/response shape;
- require model wire IDs to be registered manually for that account;
- provide no model-fetch operation, bundled catalog, hosted web search/fetch,
  provider-specific headers, plan-limit interpretation, or price claims;
- permit Strappy local function tools only when the manually registered model
  explicitly enables them;
- record monetary cost and provider quotas as unknown unless returned in a
  supported common Responses field; and
- fail clearly when an endpoint uses incompatible extensions rather than
  guessing another provider or falling back.

`other` is still a normal provider registry entry and supports multiple named
accounts. Its configurable endpoint is account data, while its lack of extras
and its generic request policy are static provider behavior.

### Session options select the account

Add `provider_account_id` to both the portable `strappy_session_options` struct
and the Objective-C `StrappySessionOptions` object. Add a corresponding option
mask bit, initializer/accessor/copy support, database loading/saving, summary
mapping, and validation.

Resolution has one direction:

```text
SessionOptions.provider_account_id
        -> provider_accounts.provider_id
        -> provider registry definition
```

Do not add `provider_id` to `SessionOptions`; duplicating it would allow the
selected account and provider type to disagree. `model_id` remains a separate
option, and the database must verify that it belongs to the selected account.
Changing the account on an empty session clears or replaces an incompatible
model with that account's allowed default. Once the first request binds the
session, neither account nor provider may change.

Default session options must persist an exact account ID as well as a model ID.
Existing UI controllers may derive the account when a model row is selected and
populate the new field internally, so this change does not require a new
visible control in the current phase.

### Sessions and requests stay pinned

- A new session's options select an account. A legacy or not-yet-configured
  session may temporarily have no account, but it cannot send a request.
- The first request atomically binds both `model_id` and
  `provider_account_id`.
- Later rounds must use models belonging to that same account.
- Model requests and HTTP attempts continue to snapshot the account ID.
- Credential refresh, retry, hosted tools, billing, and rate-limit state are
  resolved from the snapshotted account and its provider definition.
- Missing credentials or an archived account produces an actionable error
  before network I/O.
- There is no automatic cross-account or cross-provider fallback.

## Database work

The database has not shipped, and production databases may be deleted. Do not
add database migration code. Preserve the existing development-reset policy:
keep `PRAGMA user_version = 1`, change the schema identity, and reject the
previous development schema with an explicit delete-and-relaunch message.

Required schema changes:

- Stop inserting provider IDs as permanent account IDs.
- Keep the existing required non-empty `display_name`, define it as editable
  account metadata, and add account lifecycle state plus validated non-secret
  account configuration.
- Add an index on `(provider_id, lifecycle_state)` and preserve uniqueness of
  `(provider_account_id, wire_model_id)`.
- Make bundled catalog revision state provider-scoped, then track application
  of that revision to each applicable account.
- Keep model preferences account-specific through their account-qualified
  model IDs.
- Make default and per-session options persist an exact account and model;
  validate their composite relationship on every write.
- Preserve composite foreign keys that prevent a session, model request, or
  HTTP attempt from mixing a model with the wrong account.
- Add account create/list/update/archive APIs and generic account-scoped catalog
  APIs; remove fixed-account constants from public call sites.
- Add manual model create/update/archive APIs for `other` accounts without
  exposing a fake catalog refresh operation.

SQLite remains metadata and history storage. A database account row may exist
without a usable credential. Request readiness requires an active row and, when
the resolved provider definition requires authentication, a valid account-keyed
Keychain credential.

## UI compatibility boundary

No new account-management interface is part of this phase.

The current screens continue through compatibility methods that resolve one
designated account per provider:

- the existing OpenRouter endpoint/token form edits the designated OpenRouter
  account;
- the existing ChatGPT Sign In, status, refresh, and Sign Out controls operate
  on the designated ChatGPT account;
- the new account field in `SessionOptions` is populated from the designated
  account or selected model without adding a visible account control;
- existing model pickers and whitelist views receive the same dictionary shape
  and preserve their current layout and behavior; and
- existing notifications remain available while their backing implementation
  becomes account-aware.

The core/database/Keychain APIs must support multiple accounts even though this
phase exposes only the designated account in the UI. A later UI plan can add
account creation and switching without another database or request-routing
redesign.

## Implementation phases

### Phase 1 — Provider registry

Phase 1 intentionally makes no database schema changes and adds no database
migration code.

- [x] Define the immutable provider descriptor and operations interface.
- [x] Move endpoint, catalog, hosted-tool, request, error, billing, and feature
  policy behind provider dispatch.
- [x] Add the minimal `other` provider and prove its operation table advertises
  no catalog or hosted-tool capabilities.
- [x] Retain focused OAuth, transport, SSE, and provider adapter modules.
- [x] Remove fixed provider-account constants from provider behavior.
- [x] Add registry tests for unknown providers and complete definitions.

Exit criterion: request/catalog/tool code selects behavior only through a
resolved provider definition, while existing OpenRouter and ChatGPT request
fixtures remain byte-for-byte compatible where protocol behavior is unchanged.

### Phase 2 — Multi-account database

- [ ] Replace the two seeded singleton accounts with opaque account instances.
- [ ] Make the existing required account names arbitrary and editable, then add
  lifecycle/configuration and CRUD/archive APIs.
- [ ] Make defaults, model lookup, catalog state, and catalog refresh explicitly
  account-scoped.
- [ ] Convert the bundled ChatGPT catalog to provider-level static data and
  materialize it per ChatGPT account.
- [ ] Update schema identity/reset detection and all database harness fixtures.
- [ ] Add manual account-model APIs and fixtures for `other`.

Exit criterion: two accounts for the same provider can share a display name and
contain the same wire model ID without collision; renaming, model mutations,
and catalog refreshes for either account leave the other unchanged; all
foreign-key checks pass.

### Phase 3 — Account-keyed credentials and OAuth

- [ ] Add generic account-keyed Keychain operations and versioned credential
  codecs per credential kind.
- [ ] Convert the existing OpenRouter record into the designated account without
  losing the user's saved token/endpoint during the application refactor.
- [ ] Convert ChatGPT authentication from one global credential/state machine to
  account-keyed authentication contexts.
- [ ] Make device login create or attach one account only after the upstream
  identity is known and the credential is safely stored.
- [ ] Serialize refresh, cancellation, rotation, and sign-out per account.
- [ ] Change request credential snapshots and safe 401 retry to require the
  selected account ID.
- [ ] Store independent optional bearer credentials for multiple `other`
  accounts.

Exit criterion: two OpenRouter tokens and two ChatGPT OAuth credentials can
coexist; refreshing or signing out one account cannot read, replace, delete, or
stall the other.

### Phase 4 — Account-pinned runtime

- [ ] Add `provider_account_id` to the C and Objective-C session option types,
  copy/merge/change masks, defaults, summaries, and persistence.
- [ ] Resolve provider type only by loading the selected account; validate the
  selected model belongs to it.
- [ ] Pass the account ID through model resolution, provider dispatch, request
  construction, tools, transport attempts, and error/usage recording.
- [ ] Reject missing, archived, mismatched, or credential-less accounts before
  network I/O.
- [ ] Keep every retry and multi-round tool loop on the original account.
- [ ] Verify native ChatGPT web search and OpenRouter hosted tools are chosen by
  provider capabilities, never by account naming or endpoint inspection.
- [ ] Preserve subscription versus metered billing semantics per provider.
- [ ] Verify `other` sends generic Responses requests with no model fetch,
  hosted tools, provider-specific extensions, or fabricated billing data.

Exit criterion: concurrent sessions using different accounts cannot cross
credentials, endpoints, models, tools, rate-limit state, retries, or history.

### Phase 5 — Preserve the existing UI

- [ ] Add designated-account compatibility facades to `StrappyKeychain`,
  `StrappyAuthentication`, and `StrappySession`.
- [ ] Keep current iOS/macOS controls, strings, notifications, model-row shapes,
  and preference behavior unchanged.
- [ ] Confirm UI state is still loaded from SQLite/Keychain rather than cached as
  an independent source of truth.
- [ ] Document that adding/switching additional accounts is a separate UI phase.

Exit criterion: screenshots and manual workflows match the current build while
portable tests prove the underlying core supports multiple accounts.

### Phase 6 — Hardening and release validation

- [ ] Add deterministic tests for duplicate upstream login, per-account refresh
  races, failed Keychain replacement, archive/sign-out, restart, and missing
  credential recovery.
- [ ] Add two-account fixtures for both providers, including identical wire
  model IDs and simultaneous requests.
- [ ] Add account-name tests for arbitrary UTF-8 labels, duplicate names,
  renaming, empty/oversized rejection, and safe display/log handling.
- [ ] Add `SessionOptions` tests for account/model mismatch, provider inference,
  default-account selection, empty-session account changes, and established
  session locking.
- [ ] Add `other` fixtures for two endpoints, manual models, local functions,
  authenticated and unauthenticated requests, invalid endpoints, and rejection
  of unsupported extras.
- [ ] Assert secrets and upstream account identifiers never enter SQLite, logs,
  fixtures, notifications, or user-facing errors.
- [ ] Run the complete Linux shared-core suite and clean analyzer/release builds
  for iOS and macOS.
- [ ] Manually verify restart, background/resume, expiry, refresh, and sign-out
  on both Apple platforms.
- [ ] Re-run the live ChatGPT compatibility probe for text, local functions,
  native web search, every enabled bundled model, and a real plan-limit case.

## Production release gate

The working ChatGPT login establishes technical compatibility, not permission
to distribute another application's OAuth identity or rely on an undocumented
backend contract. Before release, Strappy still needs either a supported,
redistributable Codex integration that covers its legacy targets or its own
approved OAuth client identity and explicit backend authorization from OpenAI.

Do not ship the ChatGPT provider until that production path is confirmed. Keep
the ChatGPT kill switch independent of OpenRouter and remove the temporary Pi
reference submodule before the implementation commit, retaining attribution
only if code was materially copied.

## Completion criteria

The multi-account foundation is complete when:

- providers are immutable code-owned definitions and accounts are independent
  runtime/database instances;
- at least two named accounts for each provider coexist in database and Keychain
  harnesses;
- `SessionOptions` carries an account ID, provider type is inferred from that
  account, and mismatched account/model pairs cannot be saved or sent;
- `other` performs only generic Responses requests against explicit endpoints
  and never advertises catalog or hosted-tool support;
- every model, session, request, attempt, credential operation, catalog update,
  tool choice, and retry is scoped to an exact account;
- account archive/sign-out is isolated and preserves historical sessions;
- the existing iOS and macOS interfaces behave as they do today; and
- all Linux, Apple build/analyzer, manual lifecycle, secret-safety, and live
  compatibility gates pass.
