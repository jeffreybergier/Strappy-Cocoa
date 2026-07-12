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
- ordered post-answer tool audits live separately in `GuidanceAudit.json`;
- database descriptions, matching rules, related-database hints, and recovery
  instructions are removed;
- every database selected in the device's Strappy catalog is approved in each
  model's isolated run and described only as `Database.`;
- web search and web fetch remain available as unannotated server tools.

Before round zero, the application supplies fresh `database_list_info` and
`memory_user_fact_read` results as application-seeded, matched
`function_call` / `function_call_output` input pairs. Their call IDs are
created by the application, and their request-direction ledger rows are not
counted as model tool calls or response tool executions. The audit file checks
`database_query`, web search, conditional session naming, optional durable user
facts, and query-conditioned database hints in array order. Session naming
applies only when the session began untitled. The user-fact reminder encourages
useful durable facts learned from the user or their databases while forbidding
secrets and sensitive information. Database-hint memory excludes private row
values and one-off query results. If a
candidate answer omitted an applicable tool, the runtime sends that rule's
action-only `if_not_called` message once. After the ordered audit ends, the
runtime sends the single `after_audit` developer message and makes one
tool-disabled `audit_finalize` call. This happens only if at least one audit
reminder was sent. Audit acknowledgements, developer messages, tool activity,
and the refinalized answer all use the same database ledger and visible
timeline paths as other turns. The remaining generic database labels are
structural runtime requirements, not task guidance.

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
>>>>> Tool Output | {"databases":[...]}
>>>>> Tool Output | {"facts":[...]}
>>>> Response | completed | HTTP 200 | 12.7s
>>>>> Reasoning | 2270 characters
>>>>> Tool Call | database_query
```

## Private inputs and outputs

- `private/gomadango/catalog/strappy.sqlite` is the copied device catalog;
- `private/gomadango/root/` mirrors every catalog row whose
  `user_decision` is `allowed`, including available WAL/SHM/journal sidecars;
- `private/gomadango/databases.json` records the ignored fixture inventory and
  durable-file checksums used by the runner. It is the sole database-fixture
  input to a live run;
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

## Commands

Build without contacting OpenRouter:

```sh
make -C source/linux/hill_climbing
```

Validate that the private manifest and all copied databases exist, are ignored,
match their checksums, and pass SQLite's quick check. This also prepares a
temporary model session and confirms that every manifest entry becomes an
approved database in its catalog:

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
raw per-model ledgers, final answers, `report.json`, and `report.md`. Each model
directory also contains:

- `answer.md`: Strappy's extracted final answer;
- `output.json`: every raw Responses API response object in call order, plus
  the final answer selected from the session;
- `strappy.sqlite`: the complete queryable request, response, item, and tool
  ledger.

`output.json` intentionally excludes request payloads and authorization
headers.

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
- deducts 3 points for every live tool-audit intervention;
- stays within API-call, web-search, latency, and the $0.01 cost budget.

The remaining positive checks are proportionally normalized to 100 points
before audit penalties. This keeps the score ceiling stable without assigning
the removed provenance points arbitrarily to another behavior.

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
