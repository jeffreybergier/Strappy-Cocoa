# Hill-climbing harness

This is a manual, live OpenRouter evaluation harness for Strappy. It runs the
same personal-media question against the currently enabled models while exercising
the real shared Responses API, tool executor, database catalog, and ledger code.

The checked-in model list enables GLM, DeepSeek, Gemma, and Qwen for the full
four-model comparison.

The default evaluation prompt is:

> Please list out the names, blood types, and personalities of the members of
> my top 3 listened to KPOP bands

The shared runtime resources themselves are intentionally reduced to the
minimum baseline:

- `source/shared/Resources/PromptSystem.txt` is used directly;
- all normal tool names and JSON argument schemas remain available;
- tool-specific behavioral instructions live in their relevant tool
  descriptions instead of the system prompt;
- display metadata and unrelated parameter guidance are removed;
- ordered answer-quality checks are owned by the shared Responses runtime;
- database descriptions, matching rules, related-database hints, and recovery
  instructions are removed;
- every database selected in the device's Strappy catalog is approved in each
  model's isolated run and described only as `Database.`;
- web search and web fetch remain available as unannotated server tools.

Before round zero, the application supplies fresh `database_list_info` and
`memory_user_fact_read` results as application-seeded, matched
`function_call` / `function_call_output` input pairs. Their call IDs are
created by the application, and those typed conversation items are not counted
as model tool calls or tool executions. The runtime quality policy checks
`database_context_read`, session naming, Font Awesome shortcode confirmation,
durable user-memory consideration, and database-hint consideration in a fixed
order. It also checks for a linked source when web search or web fetch activity
occurred. Each non-empty candidate final answer receives one persisted quality
report in the visible timeline immediately before the assistant answer. Failed
checks are informational: the runtime does not append a developer remediation
prompt or make another API request. If a tool-free candidate answer is empty,
the runtime sends its empty-answer instruction once as a tool-disabled
`audit_finalize` recovery; a second empty response fails explicitly. That
active recovery and all normal tool and assistant items use the same database
ledger and visible timeline paths as other turns.

Each isolated model database is seeded first by executing the real
`memory_user_fact_remember` tool with the stable identity fact that the user's
first name is Jeff. The prompt does not contain that name. The existing
`memory_user_fact_read` preflight result is the only way the model receives it.

It is intentionally isolated from `source/linux/Makefile`; neither the normal
`test` target nor a plain invocation of this Makefile performs network calls.

Live output follows the same nested shape as the iOS response timeline. Each
additional `>` marks another level, while reasoning and large tool results
use compact previews like their collapsed webview sections:

```text
> Model | google/gemma-4-31b-it
>> Session 1 | google/gemma-4-31b-it | Started
>>> API Turn 1 | User | Round 1 | Attempt 1
>>>> Request
>>>>> User Prompt
>>>>>> Please list out ...
>>>>> Tool Call | database_list_info
>>>>> Tool Call | memory_user_fact_read
>>>>> Tool Output | {"databases":[{"database_id":"...","app_name":"...","path":"...","size_bytes":4096,"modified_at":...}]}
>>>>> Tool Output | [{"id":1,"fact":"The user's name is Jeff.","date_saved":"..."}]
>>>> Response | completed | HTTP 200 | 12.7s
>>>>> Reasoning | 2270 characters
>>>>> Tool Call | database_query
```

## Private inputs and outputs

- `private/gomadango/catalog/strappy.sqlite` is the copied device catalog;
- `private/gomadango/root/` mirrors every catalog row whose
  `user_decision` is `allowed`, including available WAL/SHM/journal sidecars;
- `private/gomadango/databases.json` records the ignored fixture inventory used
  by the runner. It is the sole database-fixture input to a live run;
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

Run every currently enabled model. The confirmation is deliberately required
because this makes billable network requests:

```sh
make -C source/linux/hill_climbing run CONFIRM_LIVE=yes
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
- `strappy.sqlite`: the complete queryable semantic request, response, item,
  catalog, memory, and tool ledger.

Strappy does not persist wire request bodies, wire response bodies, HTTP
headers, or raw JSON. The exported JSON is generated after the run from typed
columns and structured-node trees; it is not a copy of the provider payload.

## Scoring and iteration

The evaluator derives the top three K-pop artists dynamically from aggregate
play counts in the private fixture. It scores whether the runtime and model:

- preload the approved inventory and query MediaLibrary;
- receive the seeded user fact through the memory preflight and mention Jeff in
  the final answer;
- filters the ranking to KPOP records and aggregates total play counts rather
  than ranking groups by one song;
- covers the dynamically calculated top three in descending order;
- provides the requested public details with links;
- stores the dynamically calculated favorite bands as durable user memory;
- stays within API-call, web-search, latency, and the $0.01 cost budget.

The positive checks are proportionally normalized to 100 points so the score
ceiling remains stable as the checklist evolves.

The score deliberately does not declare public biographical facts correct.
Roster freshness, blood types, and subjective personality descriptions remain
part of the human review checklist in each report.

A useful hill-climbing cycle is:

1. Preserve a baseline run directory.
2. Make one focused shared prompt, tool-guidance, retrieval, or query change.
3. Run the enabled-model comparison again against the unchanged fixture hash.
4. Compare deterministic scores, costs, tool traces, and human-review notes.
5. Keep the change only if it improves behavior across models without harming
   the normal offline Linux harnesses.
