# Hill-climbing harness

This is a manual, live OpenRouter evaluation harness for Strappy. It runs the
same personal-media question against the currently enabled models while exercising
the real shared Responses API, tool executor, database catalog, and ledger code.

The checked-in model list enables GLM, DeepSeek, Gemma, Qwen, Gemini, GPT-OSS,
and GPT-5.6 Luna Pro for the full seven-model comparison.

The default evaluation prompt is:

> Please list out the names, blood types, and personalities of the members of
> my top 5 listened to KPOP bands

Each isolated run uses the default Personal Assistant set. Its shared runtime
resources are intentionally reduced to the minimum baseline:

- `source/shared/Resources/AssistantSets.json` selects the Personal Assistant
  goal and policy while the shared prompt builder combines it with
  `SystemPrompt.json` and the effective tool descriptions;
- all Personal Assistant tool names and JSON argument schemas remain available;
- tool-specific behavioral instructions live in their relevant tool
  descriptions instead of the system prompt;
- display metadata and unrelated parameter guidance are removed;
- ordered answer-quality checks are owned by the shared Responses runtime;
- database descriptions, matching rules, related-database hints, and recovery
  instructions are removed;
- every database selected in the device's Strappy catalog is approved in each
  model's isolated run and described only as `Database.`;
- web search and web fetch use the same explicitly selected OpenRouter engine;
  the harness defaults to `none` so neither tool is sent unless requested.

Before round zero, the Personal Assistant profile supplies fresh
`memory_read` and `database_list` results as application-seeded,
matched
`function_call` / `function_call_output` input pairs. Their call IDs are
created by the application, and those typed conversation items are not counted
as model tool calls or tool executions. The runtime quality policy checks
`database_context`, session naming, Font Awesome shortcode confirmation,
durable user-memory consideration, and database-hint consideration in a fixed
order. It also checks for a linked source when web search or web fetch activity
occurred. Every tool-free final response receives one persisted quality report.
Its first check verifies that a non-empty assistant answer was provided. The
report appears immediately before a non-empty assistant answer; for an empty
response, the failed report is the final timeline item. Failed checks are
informational: the runtime does not append a developer remediation prompt or
make another API request.

Each isolated model database is seeded first by executing the real
`memory_save` tool with the stable identity fact that the user's
first name is Jeff. The prompt does not contain that name. The existing
`memory_read` preflight result is the only way the model receives it.

It is intentionally isolated from `source/linux/Makefile`; neither the normal
`test` target nor a plain invocation of this Makefile performs network calls.

All enabled models launch concurrently in isolated child processes. The live
terminal keeps output intentionally small and updates a single pending counter
as models finish:

```text
Hill climbs pending: 7/7
Hill climbs pending: 6/7
...
Hill climbs pending: 0/7
> Run Complete
```

On an interactive terminal the counter is redrawn in place. Each model's full
runner output is preserved in its own `runner.log`, and evaluator output is
preserved once per run in `evaluation.log`. Failures receive a compact summary
after every child has completed.

## Private inputs and outputs

- `private/gomadango/catalog/strappy.sqlite` is the copied device catalog;
- `private/gomadango/root/` mirrors every catalog row whose
  `user_decision` is `allowed`, including available WAL/SHM/journal sidecars;
- `private/gomadango/databases.json` records the ignored fixture inventory used
  by the runner. It is the sole database-fixture input to a live run;
- `private/gomadango/member_roster.json` is the ignored, prefetched active-member
  snapshot for the database-derived top five. It includes one or more serialized
  regular expressions per member plus a public source and retrieval timestamp;
- `runs/` contains answers, per-model Strappy databases, costs, and reports.
- `.env` files are ignored. The default live run reads the repository-root
  `.env` through the normal Strappy configuration loader.
- All of those paths are ignored by this directory's `.gitignore`. Never force
  add them.

The fixtures retain their original Apple path suffixes under the private mirror
so duplicate filenames do not collide and task-specific ground truth remains
identifiable to the evaluator. The runner does not receive a separate
MediaLibrary path. Each model gets a new session database populated uniformly
with every manifest entry as an approved database, preventing model runs from
sharing remembered facts or other mutable assistant state. The run fails if
the registered set differs from the manifest set.

Fixture validation deliberately does not compare file checksums. SQLite may
checkpoint copied WAL data into a database or recreate transient sidecars while
preserving the same logical contents. The harness instead checks safe manifest
paths, required database files, SQLite integrity, and runner registration.

## Commands

Build without contacting OpenRouter:

```sh
make -C source/linux/hill_climbing
```

Validate that the private manifest and all copied databases exist, are ignored,
use safe paths, and pass SQLite's quick check. This also prepares a temporary
model session and confirms that every manifest entry becomes an approved
database in its catalog:

```sh
make -C source/linux/hill_climbing verify-fixture
```

Fetch the current Strappy catalog from `gomadango`, then refresh every selected
database and its available SQLite sidecars:

```sh
make -C source/linux/hill_climbing refresh-fixture
```

Run every currently enabled model concurrently. The confirmation is
deliberately required because this makes billable network requests:

```sh
make -C source/linux/hill_climbing run CONFIRM_LIVE=yes
```

Select one provider for both web tools with `WEB_PROVIDER=native`,
`WEB_PROVIDER=exa`, or `WEB_PROVIDER=parallel` (the default is `none`):

```sh
make -C source/linux/hill_climbing run CONFIRM_LIVE=yes \
  WEB_PROVIDER=exa
```

Run one model:

```sh
make -C source/linux/hill_climbing run CONFIRM_LIVE=yes \
  MODEL=google/gemma-4-31b-it
```

Every run writes an ignored timestamped directory under `runs/` containing the
per-model semantic ledgers, final answers, `report.json`, and `report.md`. Each
model directory also contains:

- `answer.md`: Strappy's extracted final answer;
- `output.json`: a readable snapshot reconstructed from normalized attempts,
  results, usage, typed response items, and tool records in call order;
- `runner.log`: that model's complete captured runner output;
- `strappy.sqlite`: the complete queryable semantic request, response, item,
  catalog, memory, and tool ledger.

Strappy does not persist wire request bodies, wire response bodies, HTTP
headers, or raw JSON. The exported JSON is generated after the run from typed
columns and structured-node trees; it is not a copy of the provider payload.

## Scoring and iteration

Score version 4 is centered on zero and keeps each scoring rule independent:

- every `failed` or `error` audit check scores -5 points;
- `passed` and `not_applicable` checks score zero;
- a required-tool audit passes only after that tool completes successfully;
- every bonus hit scores +1 independently: one for mentioning the seeded
  remembered name, one for every unique expected active member mentioned, and
  one for every unique valid non-image inline Markdown HTTP(S) link, up to 10
  link points; a miss scores zero;
- no points are withheld when an audit fails;
- total run cost is rounded to the nearest cent using half-up rounding. Every
  cent below the $0.05 target scores +5 and every cent above it scores -5:
  $0.03 is +10, $0.05 is 0, and $0.06 is -5.

The evaluator derives the top five K-pop artists dynamically from aggregate
play counts in the private fixture. Before a live comparison, the roster for
those exact five artists must be prefetched once into
`private/gomadango/member_roster.json`. The runner validates it against the
ranking and embeds the complete snapshot and its SHA-256 in `run.json`, so every
model in that run is scored against the same active roster.

Member matching executes the serialized regular expressions in the frozen
roster. Patterns use case and letter boundaries appropriate to each name.
Ambiguous short stage names such as `K`, `Q`, and `EJ` are case-sensitive and
must be surrounded by non-letter boundaries. Hyphen-connected words are treated
as words as well, so the `K` in `K-pop` does not count as member `K`.

Latency, API attempts, local tool errors, and web activity remain visible
diagnostics but do not affect the score. Cost is the sole efficiency metric in
the score. With the current 35-member roster, one remembered-name hit, a
10-link cap, and a zero-cost run, a fully compliant answer can score at most
+71. The score measures audit
compliance and answer coverage; it does not declare blood types or subjective
personality descriptions factually correct.

A useful hill-climbing cycle is:

1. Preserve a baseline run directory.
2. Make one focused shared prompt, tool-guidance, retrieval, or query change.
3. Run the enabled-model comparison again against the unchanged fixture hash.
4. Compare deterministic scores, costs, tool traces, and human-review notes.
5. Keep the change only if it improves behavior across models without harming
   the normal offline Linux harnesses.
