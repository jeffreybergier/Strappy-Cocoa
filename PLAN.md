# WebAssembly Proof-of-Concept Plan

## Goal

Build the smallest browser proof of concept that reuses Strappy's C business
logic and can complete a real World Knowledge conversation through OpenRouter.

The PoC supports exactly:

- [ ] the `world_knowledge` assistant set;
- [ ] one OpenRouter account and one API key at a time;
- [ ] an API key held only in volatile Worker/Wasm memory;
- [ ] Strappy's internal SQLite store for sessions, memory, and the Responses
  ledger; and
- [ ] the existing C-generated conversation HTML and JavaScript.

The API key must never be written to SQLite, OPFS, IndexedDB, Web Storage,
URLs, logs, diagnostics, or error messages. A reload requires the user to enter
the key again.

## Non-goals

- [ ] Personal Assistant, Coding Assistant, or Database Study
- [ ] ChatGPT accounts, OAuth, Keychain, token refresh, or multiple accounts
- [ ] Bash, filesystem tools, database discovery, or access to user databases
- [ ] Native application feature parity
- [ ] Model catalog and provider-management UI
- [ ] Multiple tabs, offline operation, or broad browser compatibility
- [ ] Production deployment or production-grade secret isolation

The first PoC targets one current Chromium browser served from localhost.
It requires no application backend: a Docker Compose service serves only the
static HTML, JavaScript, Worker, Wasm, and resource files. SQLite remains in
the browser's local OPFS data, and the browser contacts OpenRouter directly.

## Implementation ground rule

Reuse as much existing portable C code as possible without rewriting it for
WebAssembly. Web-specific code is limited to unavoidable browser platform
adapters such as Fetch, Worker messaging, OPFS connection policy, and DOM
integration. Request construction, provider behavior, response parsing,
limits, errors, persistence, tools, prompts, and rendering remain owned by the
shared C modules. Any JavaScript-only behavior introduced by an early transport
spike must be removed rather than evolved once the corresponding shared C path
is connected.

## Implementation order

### 1. Establish the web build and smoke-test loop

- [x] Add `source/web/Makefile` with explicit Emscripten inputs and outputs.
- [x] Add a minimal `index.html`, application script, and Web Worker.
- [x] Add a `web` service to the existing `compose.yml` that serves the built
  static files on a fixed `http://localhost:8765` origin.
- [x] Bind the Compose port to `127.0.0.1` only, mount the build output
  read-only, and pass no API key or other credential into the container.
- [x] Configure the static server to return the correct Wasm MIME type and
  disable caching of the entry HTML during development.
- [x] Compile `strappy_core.c` and one small exported C function to Wasm.
- [x] Package only the shared resources required by World Knowledge.
- [x] Add a `make -C source/web clean test` smoke test that loads the module and
  calls the exported function.
- [x] Keep the existing native and Linux builds unchanged.

Exit condition: `docker compose up web` serves the application at the fixed
localhost origin, and the browser and a headless test can load the Wasm module
and call shared C code.

### 2. Prove direct OpenRouter browser transport

- [x] Add an API-key form whose input is immediately handed to the Worker.
- [x] Keep the authoritative key only in Worker-owned volatile state.
- [x] Make a minimal JavaScript `fetch()` request to OpenRouter's Responses
  endpoint with cancellation and one non-streaming JSON response, matching the
  native OpenRouter request profile.
- [x] Verify CORS, HTTP errors, invalid-key behavior, and a JSON response.
- [x] Add explicit `clear key` and Worker-shutdown paths.

This step intentionally bypasses the full Strappy Responses runtime. It proves
the external dependency before restructuring the C client.

Exit condition: a user can enter a key, make one bounded test request, observe
the JSON response, clear the key, and confirm that reloading forgets it.

### 3. Split the shared client transport

- [x] Keep `strappy_client.h` as the public client boundary.
- [x] Reduce `strappy_client.c` to common URL, result, limit, SSE, and error logic.
- [x] Move native HTTP mechanics into `strappy_client_curl.c`.
- [x] Add `strappy_client_transport.h` for the internal backend contract.
- [x] Add `strappy_client_web.c` for the Emscripten/JavaScript bridge and
  `source/web/strappy_client_fetch.js` for Fetch.
- [x] Keep provider request construction and JSON response interpretation in
  shared C; the Fetch bridge only transfers request and response bytes plus
  transport metadata.
- [x] Have shared C omit optional provider headers that the browser cannot send
  because of Fetch or CORS restrictions; do not encode provider-header policy
  in the JavaScript bridge.
- [x] Link exactly one backend per target.
- [x] Preserve the current synchronous public API with Asyncify for the PoC; leave
  conversion to an explicit asynchronous state machine until after the PoC.
- [x] Replace or supplement curl-specific result fields with transport-neutral
  status without changing native behavior.

Exit condition: the existing C client API can complete an OpenRouter request in
the Worker using Fetch, and native builds still use libcurl.

### 4. Bring up SQLite in Wasm

- [x] Compile SQLite into the Wasm module and first open a temporary database.
- [x] Separate POSIX connection policy from shared schema and query code.
- [x] Add a web connection policy that does not depend on `stat()` identity,
  pthread concurrency, or mandatory WAL mode.
- [x] Run schema initialization and a minimal create/list session test.
- [x] Add SQLite's `opfs-sahpool` VFS and move `strappy.sqlite` to
  origin-private persistent storage. This single-tab PoC choice avoids
  requiring COOP/COEP response headers.
- [x] Keep all SQLite access in the same Worker.
- [x] Verify that history survives reload while the API key does not.

Exit condition: a session written through the existing C database APIs remains
available after reload, with no credential data in the database.

### 5. Enforce the browser capability profile

- [x] Add a web platform profile that exposes only `world_knowledge` and
  OpenRouter.
- [x] Make World Knowledge the effective default regardless of the native default.
- [x] Reject unsupported assistant-set and provider IDs at the C boundary.
- [x] Exclude database, Bash, file, study, OAuth, ChatGPT, and Keychain modules from
  the web link.
- [x] Split `strappy_tools.c` only as much as required to link the browser-safe
  tools without host-tool dependencies.
- [x] Retain World Knowledge's safe tools: memory, skills, session rename, datetime,
  Font Awesome, and OpenRouter server web tools.
- [x] Confirm that unsupported tools are absent from model-facing tool schemas,
  rather than present as failing stubs.

Exit condition: generated World Knowledge requests contain only the intended
browser-safe local tools and OpenRouter server tools.

### 6. Connect the complete Strappy conversation path

- [x] Export small C entry points for store initialization, session creation,
  prompt submission, cancellation, timeline loading, and string cleanup.
- [x] Install a web credential callback that supplies a temporary OpenRouter key
  copy to the Responses runtime.
- [x] Route Responses events from C to the Worker and then to the main thread.
- [x] Use the existing C webview renderer for the initial page and incremental
  updates.
- [x] Add the minimum UI: key entry, one session, transcript, prompt input, send,
  cancel, processing status, and visible errors.
- [x] Use the existing default OpenRouter model for the PoC; defer model selection
  and catalog refresh UI.

Exit condition: the user can enter a key, create a World Knowledge session,
send a prompt, receive a final answer, and see the normalized persisted
timeline rendered by existing Strappy C code.

### 7. Verify the PoC contract

- [x] Add an offline browser test for Wasm loading, SQLite persistence, resource
  loading, assistant/provider filtering, rendering, and credential clearing.
- [x] Test cancellation and invalid/expired keys with offline mocked transport
  and complete Worker conversation paths.
- [x] Manually test a current-information prompt that exercises OpenRouter web
  search and produces source links.
- [x] Run the existing Linux suite and required clean native builds to detect
  regressions.
- [x] Document the local server command, supported browser, known limitations, and
  how to clear the OPFS database.

## PoC completion checklist

The PoC is complete when all of these are true:

- [x] It builds reproducibly with one web command.
- [x] `docker compose up web` runs the application on the documented, fixed
  localhost origin without an application backend.
- [x] The static web container receives no API key, prompt, response, session,
  memory, or SQLite data.
- [x] Only World Knowledge and OpenRouter are reachable.
- [x] The page never persists the OpenRouter key and requires it again after reload.
- [x] A real prompt completes through the shared Responses runtime.
- [x] The existing C renderer displays the resulting timeline.
- [x] Session history and World Knowledge memory survive reload through SQLite
  OPFS.
- [x] No Bash, file, user-database, OAuth, ChatGPT, or Keychain code is linked.
- [x] Offline browser tests, the Linux shared-core suite, and clean native builds
  pass without warnings.

## Deferred until after the PoC

- [ ] Add an explicitly opted-in live OpenRouter browser test with one bounded
  request and secret-free output. Until then, live validation is manual and the
  web build has no credential environment-variable convention.
- [ ] Replace Asyncify with an explicit asynchronous request state machine.
- [ ] Model catalog refresh and model selection.
- [ ] Multiple sessions and full preferences UI.
- [ ] Firefox and Safari validation.
- [ ] Multi-tab database coordination.
- [ ] Session/database export and import.
- [ ] Accessibility, localization, responsive polish, and production hardening.
