#!/usr/bin/env python3
"""Manual orchestration for the private, live hill-climbing harness."""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime as dt
import hashlib
import json
import sqlite3
import subprocess
import sys
from pathlib import Path

from hill_climbing_ground_truth import (
    REQUIRED_GROUP_COUNT,
    expected_artists,
    load_member_roster,
    resolve_ranking_database,
)


PROMPT = (
    "Please list out the names, blood types, and personalities of the members "
    "of my top 5 listened to KPOP bands"
)

MODELS = (
    ("z-ai/glm-5.2", "glm-5.2"),
    ("deepseek/deepseek-v4-pro", "deepseek-v4-pro"),
    ("google/gemma-4-31b-it", "gemma-4-31b-it"),
    ("qwen/qwen3.6-27b", "qwen3.6-27b"),
    ("google/gemini-3.1-flash-lite", "gemini-3.1-flash-lite"),
    ("openai/gpt-oss-120b", "gpt-oss-120b"),
    ("openai/gpt-5.6-luna-pro", "gpt-5.6-luna-pro"),
)

SEEDED_USER_FACT = {
    "kind": "fact",
    "subject": "user",
    "predicate": "fact",
    "value": "The user's name is Jeff.",
    "expected_name": "Jeff",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--database-manifest", required=True, type=Path)
    parser.add_argument("--member-roster", required=True, type=Path)
    parser.add_argument("--env-file", required=True, type=Path)
    parser.add_argument("--resource-dir", required=True, type=Path)
    parser.add_argument(
        "--model",
        choices=[model for model, _ in MODELS],
        help="Run one model instead of the full seven-model comparison.",
    )
    parser.add_argument("--prompt", default=PROMPT)
    parser.add_argument(
        "--web-provider",
        choices=("none", "auto", "native", "exa", "parallel"),
        default="none",
    )
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def unique_run_directory(root: Path) -> Path:
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    candidate = root / stamp
    suffix = 1
    while candidate.exists():
        candidate = root / f"{stamp}-{suffix}"
        suffix += 1
    candidate.mkdir(parents=True)
    return candidate


def load_database_fixtures(manifest_path: Path) -> list[Path]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = manifest.get("databases")
    if manifest.get("format_version") != 1 or not isinstance(entries, list):
        raise RuntimeError(f"Invalid database manifest: {manifest_path}")
    fixture_root = manifest_path.parent.resolve()
    databases: list[Path] = []
    seen: set[Path] = set()
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(
            entry.get("local_path"), str
        ):
            raise RuntimeError(f"Invalid database entry in {manifest_path}")
        database = (fixture_root / entry["local_path"]).resolve()
        if fixture_root not in database.parents:
            raise RuntimeError(f"Database fixture escapes private root: {database}")
        if database in seen:
            raise RuntimeError(f"Duplicate database fixture: {database}")
        if not database.is_file():
            raise RuntimeError(f"Missing database fixture: {database}")
        seen.add(database)
        databases.append(database)
    if not databases:
        raise RuntimeError(f"Database manifest is empty: {manifest_path}")
    return databases


def structured_value(
    connection: sqlite3.Connection,
    owner_item_id: int,
    purpose: str,
) -> object | None:
    document = connection.execute(
        """
        SELECT id FROM structured_documents
        WHERE owner_item_id = ? AND purpose = ?
        """,
        (owner_item_id, purpose),
    ).fetchone()
    if document is None:
        return None
    rows = connection.execute(
        """
        SELECT node_id, parent_node_id, ordinal, member_name, value_type,
               text_value, number_value, boolean_value
        FROM structured_nodes
        WHERE document_id = ?
        ORDER BY node_id
        """,
        (document["id"],),
    ).fetchall()
    if not rows:
        return None

    nodes = {int(row["node_id"]): row for row in rows}
    children: dict[int, list[sqlite3.Row]] = {}
    for row in rows:
        parent = row["parent_node_id"]
        if parent is not None:
            children.setdefault(int(parent), []).append(row)
    for values in children.values():
        values.sort(key=lambda row: int(row["ordinal"]))

    def build(node_id: int) -> object | None:
        row = nodes[node_id]
        value_type = str(row["value_type"])
        child_rows = children.get(node_id, [])
        if value_type == "object":
            return {
                str(child["member_name"]): build(int(child["node_id"]))
                for child in child_rows
            }
        if value_type == "array":
            return [build(int(child["node_id"])) for child in child_rows]
        if value_type == "string":
            return str(row["text_value"] or "")
        if value_type == "number":
            number = str(row["number_value"] or "0")
            try:
                return json.loads(number)
            except json.JSONDecodeError:
                return number
        if value_type == "boolean":
            return bool(row["boolean_value"])
        return None

    return build(0)


def text_parts(
    connection: sqlite3.Connection,
    item_id: int,
) -> list[dict[str, object]]:
    parts = []
    rows = connection.execute(
        """
        SELECT id, collection_name, ordinal, part_type, text
        FROM item_text_parts WHERE item_id = ?
        ORDER BY collection_name, ordinal
        """,
        (item_id,),
    ).fetchall()
    for row in rows:
        citations = [
            dict(citation)
            for citation in connection.execute(
                """
                SELECT citation_type, start_offset, end_offset, title, url,
                       excerpt
                FROM item_citations WHERE text_part_id = ? ORDER BY ordinal
                """,
                (row["id"],),
            ).fetchall()
        ]
        part = {
            "collection": row["collection_name"],
            "ordinal": row["ordinal"],
            "type": row["part_type"],
            "text": row["text"],
        }
        if citations:
            part["citations"] = citations
        parts.append(part)
    return parts


def semantic_item(
    connection: sqlite3.Connection,
    row: sqlite3.Row,
) -> dict[str, object]:
    item_id = int(row["id"])
    kind = str(row["kind"])
    item: dict[str, object] = {
        "id": item_id,
        "sequence": row["sequence"],
        "kind": kind,
        "provider_item_id": row["provider_item_id"],
        "provider_status": row["provider_status"],
        "is_error": bool(row["is_error"]),
    }
    if kind == "message":
        message = connection.execute(
            "SELECT role, phase FROM message_items WHERE item_id = ?",
            (item_id,),
        ).fetchone()
        if message is not None:
            item.update({"role": message["role"], "phase": message["phase"]})
        item["parts"] = text_parts(connection, item_id)
    elif kind == "reasoning":
        reasoning = connection.execute(
            """
            SELECT encrypted_content, provider_format, provider_signature
            FROM reasoning_items WHERE item_id = ?
            """,
            (item_id,),
        ).fetchone()
        if reasoning is not None:
            item.update(dict(reasoning))
        item["parts"] = text_parts(connection, item_id)
    elif kind == "function_call":
        call = connection.execute(
            """
            SELECT provider_call_id, tool_name, tool_namespace
            FROM function_calls WHERE item_id = ?
            """,
            (item_id,),
        ).fetchone()
        if call is not None:
            item.update(dict(call))
        item["arguments"] = structured_value(connection, item_id, "arguments")
        execution = connection.execute(
            """
            SELECT state, started_at_ms, completed_at_ms, error_code,
                   error_message
            FROM tool_executions WHERE function_call_item_id = ?
            """,
            (item_id,),
        ).fetchone()
        if execution is not None:
            item["execution"] = dict(execution)
    elif kind == "function_call_output":
        output = connection.execute(
            """
            SELECT function_call_item_id, execution_state, started_at_ms,
                   completed_at_ms, output_format, text_output, error_code,
                   error_message
            FROM function_outputs WHERE item_id = ?
            """,
            (item_id,),
        ).fetchone()
        if output is not None:
            item.update(dict(output))
            if output["output_format"] == "structured":
                item["output"] = structured_value(connection, item_id, "output")
            else:
                item["output"] = output["text_output"]
    elif kind == "openrouter:web_search":
        search = connection.execute(
            "SELECT action_type, query FROM web_searches WHERE item_id = ?",
            (item_id,),
        ).fetchone()
        if search is not None:
            item.update(dict(search))
        item["sources"] = [
            dict(source)
            for source in connection.execute(
                """
                SELECT source_type, url FROM web_search_sources
                WHERE web_search_item_id = ? ORDER BY ordinal
                """,
                (item_id,),
            ).fetchall()
        ]
    elif kind == "openrouter:web_fetch":
        fetch = connection.execute(
            """
            SELECT url, title, content, http_status
            FROM web_fetches WHERE item_id = ?
            """,
            (item_id,),
        ).fetchone()
        if fetch is not None:
            item.update(dict(fetch))
    return item


def message_text(
    connection: sqlite3.Connection,
    session_id: int,
    role: str,
    descending: bool,
) -> str:
    direction = "DESC" if descending else "ASC"
    item = connection.execute(
        f"""
        SELECT i.id FROM conversation_items i
        JOIN message_items m ON m.item_id = i.id
        WHERE i.session_id = ? AND m.role = ?
        ORDER BY i.sequence {direction} LIMIT 1
        """,
        (session_id, role),
    ).fetchone()
    if item is None:
        return ""
    rows = connection.execute(
        """
        SELECT text FROM item_text_parts
        WHERE item_id = ? AND collection_name = 'content'
        ORDER BY ordinal
        """,
        (item["id"],),
    ).fetchall()
    return "\n".join(str(row["text"]) for row in rows)


def export_output(session_db: Path, output_file: Path) -> None:
    """Export a readable snapshot reconstructed from semantic records."""
    connection = sqlite3.connect(session_db)
    connection.row_factory = sqlite3.Row
    try:
        session = connection.execute(
            """
            SELECT id, model_id, created_at_ms
            FROM sessions ORDER BY id DESC LIMIT 1
            """
        ).fetchone()
        if session is None:
            raise RuntimeError(f"No session found in {session_db}")
        session_id = int(session["id"])
        prompt = message_text(connection, session_id, "user", False)
        answer = message_text(connection, session_id, "assistant", True)
        attempts = connection.execute(
            """
            SELECT a.id, t.prompt_group_key, r.request_kind, r.round_index,
                   a.attempt_index, a.state, a.http_status,
                   ar.provider_model_id, ar.provider_response_id,
                   ar.provider_status, ar.incomplete_reason, ar.error_type,
                   ar.error_code, ar.error_message, ar.error_parameter,
                   ar.parse_error, u.input_tokens, u.cached_input_tokens,
                   u.output_tokens, u.reasoning_tokens, u.total_tokens,
                   u.cost_nano_usd
            FROM http_attempts a
            JOIN model_requests r ON r.id = a.request_id
            JOIN turns t ON t.id = r.turn_id
            LEFT JOIN api_results ar ON ar.attempt_id = a.id
            LEFT JOIN api_usage u ON u.attempt_id = a.id
            WHERE t.session_id = ? ORDER BY a.id
            """,
            (session_id,),
        ).fetchall()
        exported_attempts = []
        for attempt in attempts:
            items = connection.execute(
                """
                SELECT id, sequence, kind, provider_item_id, provider_status,
                       is_error
                FROM conversation_items
                WHERE source_attempt_id = ? ORDER BY source_item_index
                """,
                (attempt["id"],),
            ).fetchall()
            exported_attempts.append(
                {
                    "attempt_id": attempt["id"],
                    "prompt_group_key": attempt["prompt_group_key"],
                    "request_kind": attempt["request_kind"],
                    "round_index": attempt["round_index"],
                    "attempt_index": attempt["attempt_index"],
                    "state": attempt["state"],
                    "is_error": (
                        attempt["state"] != "completed"
                        or int(attempt["http_status"] or 0) >= 400
                        or attempt["error_message"] is not None
                        or attempt["parse_error"] is not None
                    ),
                    "http_status": attempt["http_status"],
                    "result": {
                        "model": attempt["provider_model_id"],
                        "id": attempt["provider_response_id"],
                        "status": attempt["provider_status"],
                        "incomplete_reason": attempt["incomplete_reason"],
                        "error_type": attempt["error_type"],
                        "error_code": attempt["error_code"],
                        "error_message": attempt["error_message"],
                        "error_parameter": attempt["error_parameter"],
                        "parse_error": attempt["parse_error"],
                    },
                    "usage": {
                        "input_tokens": attempt["input_tokens"],
                        "cached_input_tokens": attempt["cached_input_tokens"],
                        "output_tokens": attempt["output_tokens"],
                        "reasoning_tokens": attempt["reasoning_tokens"],
                        "total_tokens": attempt["total_tokens"],
                        "cost_nano_usd": attempt["cost_nano_usd"],
                    },
                    "items": [semantic_item(connection, item) for item in items],
                }
            )
        latest_status = attempts[-1]["http_status"] if attempts else 0
    finally:
        connection.close()

    output = {
        "format_version": 2,
        "storage": "semantic",
        "session": {
            "id": session["id"],
            "model": session["model_id"],
            "prompt": prompt,
            "http_status": latest_status,
            "created_at": dt.datetime.fromtimestamp(
                int(session["created_at_ms"]) / 1000,
                tz=dt.timezone.utc,
            ).isoformat(),
            "final_answer": answer,
        },
        "attempts": exported_attempts,
    }
    output_file.write_text(
        json.dumps(output, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def registered_database_paths(session_db: Path) -> set[Path]:
    connection = sqlite3.connect(
        f"file:{session_db.resolve()}?mode=ro",
        uri=True,
    )
    try:
        rows = connection.execute(
            """
            SELECT l.path
            FROM databases d
            JOIN database_locations l
              ON l.database_id = d.id AND l.active = 1
            JOIN database_permissions p ON p.database_id = d.id
            WHERE p.decision = 'allowed'
              AND l.validation_state = 'valid'
            """
        ).fetchall()
    finally:
        connection.close()
    return {Path(str(row[0])).resolve() for row in rows}


def run_model(
    runner: Path,
    model: str,
    slug: str,
    run_dir: Path,
    databases: list[Path],
    env_file: Path,
    resource_dir: Path,
    prompt: str,
    web_provider: str = "none",
) -> dict[str, object]:
    model_dir = run_dir / slug
    model_dir.mkdir()
    session_db = model_dir / "strappy.sqlite"
    answer_file = model_dir / "answer.md"
    output_file = model_dir / "output.json"
    runner_log = model_dir / "runner.log"
    started = dt.datetime.now(dt.timezone.utc)
    command = [
        str(runner),
        "--model",
        model,
        "--session-db",
        str(session_db),
        "--env-file",
        str(env_file),
        "--resource-dir",
        str(resource_dir),
        "--answer-file",
        str(answer_file),
        "--prompt",
        prompt,
        "--web-provider",
        web_provider,
    ]
    for database in databases:
        command.extend(["--database", str(database)])

    launch_error = None
    exit_code = 127
    with runner_log.open("w", encoding="utf-8") as log:
        try:
            completed = subprocess.run(
                command,
                check=False,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
            exit_code = completed.returncode
        except OSError as error:
            launch_error = str(error)
            log.write(f"Could not launch runner: {error}\n")

    finished = dt.datetime.now(dt.timezone.utc)
    export_error = None
    registration_error = None
    registered_database_count = None
    if session_db.is_file():
        try:
            registered = registered_database_paths(session_db)
            registered_database_count = len(registered)
            expected = set(databases)
            if registered != expected:
                registration_error = (
                    "Run database registration mismatch: "
                    f"expected {len(expected)}, found {len(registered)}"
                )
        except (OSError, sqlite3.Error) as error:
            registration_error = str(error)
        try:
            export_output(session_db, output_file)
        except (RuntimeError, sqlite3.Error, OSError) as error:
            export_error = str(error)
    else:
        registration_error = "Session database was not created."

    return {
        "slug": slug,
        "exit_code": exit_code,
        "started_at": started.isoformat(),
        "completed_at": finished.isoformat(),
        "elapsed_seconds": (finished - started).total_seconds(),
        "registered_database_count": registered_database_count,
        "database_registration_error": registration_error,
        "output_file": (
            str(output_file.relative_to(run_dir)) if output_file.is_file() else None
        ),
        "output_export_error": export_error,
        "runner_log": str(runner_log.relative_to(run_dir)),
        "runner_launch_error": launch_error,
    }


def print_pending(pending: int, total: int) -> None:
    message = f"Hill climbs pending: {pending}/{total}"
    if sys.stdout.isatty():
        print(f"\r{message}", end="\n" if pending == 0 else "", flush=True)
    else:
        print(message, flush=True)


def main() -> int:
    args = parse_args()
    harness_dir = Path(__file__).resolve().parent
    runner = args.runner.resolve()
    database_manifest = args.database_manifest.resolve()
    member_roster_path = args.member_roster.resolve()
    env_file = args.env_file.resolve()
    resource_dir = args.resource_dir.resolve()

    required = (
        runner,
        database_manifest,
        member_roster_path,
        env_file,
    )
    missing = [str(path) for path in required if not path.is_file()]
    if not resource_dir.is_dir():
        missing.append(str(resource_dir))
    for name in (
        "AssistantSets.json",
        "SystemPrompt.json",
        "GuidanceTools.json",
        "GuidanceDatabase.json",
    ):
        resource = resource_dir / name
        if not resource.is_file():
            missing.append(str(resource))
    if missing:
        print("Missing required file(s):", file=sys.stderr)
        for path in missing:
            print(f"  {path}", file=sys.stderr)
        return 2
    try:
        databases = load_database_fixtures(database_manifest)
        ranking_db = resolve_ranking_database(
            {"database_manifest": str(database_manifest)}
        )
        top_five = expected_artists(
            ranking_db,
            limit=REQUIRED_GROUP_COUNT,
        )
        member_roster = load_member_roster(member_roster_path, top_five)
    except (json.JSONDecodeError, OSError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 2
    selected = [entry for entry in MODELS if args.model in (None, entry[0])]
    run_dir = unique_run_directory(harness_dir / "runs")
    metadata = {
        "created_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "prompt": args.prompt,
        "web_provider": args.web_provider,
        "models": [model for model, _ in selected],
        "execution_mode": "parallel",
        "parallel_model_count": len(selected),
        "database_manifest": str(database_manifest),
        "database_manifest_sha256": sha256(database_manifest),
        "database_count": len(databases),
        "database_fixtures": [
            {"path": str(database)} for database in databases
        ],
        "expected_top_five": [
            {"name": name, "total_plays": plays} for name, plays in top_five
        ],
        "member_roster_path": str(member_roster_path),
        "member_roster_sha256": sha256(member_roster_path),
        "member_roster": member_roster,
        "seeded_user_fact": SEEDED_USER_FACT,
        "resource_dir": str(resource_dir),
        "resource_sha256": {
            name: sha256(resource_dir / name)
            for name in (
                "AssistantSets.json",
                "SystemPrompt.json",
                "GuidanceTools.json",
                "GuidanceDatabase.json",
            )
            if (resource_dir / name).is_file()
        },
        "results": {},
    }
    (run_dir / "run.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )

    any_failed = False
    completed_results: dict[str, dict[str, object]] = {}
    print_pending(len(selected), len(selected))
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=len(selected),
        thread_name_prefix="hill-climb",
    ) as executor:
        futures = {
            executor.submit(
                run_model,
                runner,
                model,
                slug,
                run_dir,
                databases,
                env_file,
                resource_dir,
                args.prompt,
                args.web_provider,
            ): (model, slug)
            for model, slug in selected
        }
        pending = len(futures)
        for future in concurrent.futures.as_completed(futures):
            model, slug = futures[future]
            try:
                result = future.result()
            except Exception as error:
                now = dt.datetime.now(dt.timezone.utc).isoformat()
                result = {
                    "slug": slug,
                    "exit_code": 1,
                    "started_at": now,
                    "completed_at": now,
                    "elapsed_seconds": 0.0,
                    "registered_database_count": None,
                    "database_registration_error": None,
                    "output_file": None,
                    "output_export_error": None,
                    "runner_log": None,
                    "runner_launch_error": None,
                    "orchestration_error": str(error),
                }
            completed_results[model] = result
            metadata["results"] = {
                selected_model: completed_results[selected_model]
                for selected_model, _ in selected
                if selected_model in completed_results
            }
            pending -= 1
            print_pending(pending, len(selected))
            if (
                int(result["exit_code"]) != 0
                or result.get("database_registration_error") is not None
                or result.get("orchestration_error") is not None
            ):
                any_failed = True
            (run_dir / "run.json").write_text(
                json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
            )

    evaluator = harness_dir / "evaluate.py"
    evaluation_log = run_dir / "evaluation.log"
    evaluation_exit_code = 127
    with evaluation_log.open("w", encoding="utf-8") as log:
        try:
            evaluation = subprocess.run(
                [sys.executable, str(evaluator), "--run-dir", str(run_dir)],
                check=False,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
            evaluation_exit_code = evaluation.returncode
        except OSError as error:
            log.write(f"Could not launch evaluator: {error}\n")
    if evaluation_exit_code != 0:
        any_failed = True

    print("> Run Complete")
    print(f">> Artifacts | {run_dir}")
    print(f">> Evaluation | {run_dir / 'report.md'}")
    failures = [
        (model, result)
        for model, result in completed_results.items()
        if int(result["exit_code"]) != 0
        or result.get("database_registration_error") is not None
        or result.get("orchestration_error") is not None
    ]
    if failures:
        print(f">> Failed Models | {len(failures)}", file=sys.stderr)
        for model, result in failures:
            reason = (
                result.get("orchestration_error")
                or result.get("runner_launch_error")
                or result.get("database_registration_error")
                or f"runner exited {result['exit_code']}"
            )
            print(f">>> {model} | {reason}", file=sys.stderr)
    if evaluation_exit_code != 0:
        print(
            f">> Evaluation Failed | see {evaluation_log}",
            file=sys.stderr,
        )
    return 1 if any_failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
