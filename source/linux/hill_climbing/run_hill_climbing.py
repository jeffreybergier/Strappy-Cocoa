#!/usr/bin/env python3
"""Manual orchestration for the private, live hill-climbing harness."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import sqlite3
import subprocess
import sys
from pathlib import Path


PROMPT = (
    "Please list out the names, blood types, and personalities of the members "
    "of my top 3 listened to KPOP bands"
)

MODELS = (
    ("z-ai/glm-5.2", "glm-5.2"),
    ("deepseek/deepseek-v4-pro", "deepseek-v4-pro"),
    ("google/gemma-4-31b-it", "gemma-4-31b-it"),
    ("qwen/qwen3.6-27b", "qwen3.6-27b"),
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
    parser.add_argument("--env-file", required=True, type=Path)
    parser.add_argument("--system-prompt", required=True, type=Path)
    parser.add_argument(
        "--model",
        choices=[model for model, _ in MODELS],
        help="Run one model instead of the full four-model comparison.",
    )
    parser.add_argument("--prompt", default=PROMPT)
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


def export_output(session_db: Path, output_file: Path) -> None:
    """Export ordered raw API responses without requests or headers."""
    connection = sqlite3.connect(session_db)
    connection.row_factory = sqlite3.Row
    try:
        session = connection.execute(
            """
            SELECT id, model, prompt, response, http_status, created_at
            FROM sessions ORDER BY id DESC LIMIT 1
            """
        ).fetchone()
        calls = connection.execute(
            """
            SELECT id, prompt_group_key, request_kind, round_index,
                   attempt_index, state, is_error, http_status,
                   response_model, response_id, response_status,
                   response_raw_json
            FROM response_api_calls ORDER BY id
            """
        ).fetchall()
    finally:
        connection.close()

    if session is None:
        raise RuntimeError(f"No session found in {session_db}")

    responses = []
    for call in calls:
        raw = call["response_raw_json"]
        parsed = None
        raw_text = None
        if raw is not None:
            try:
                parsed = json.loads(raw)
            except json.JSONDecodeError:
                raw_text = raw
        entry = {
            "call_id": call["id"],
            "prompt_group_key": call["prompt_group_key"],
            "request_kind": call["request_kind"],
            "round_index": call["round_index"],
            "attempt_index": call["attempt_index"],
            "state": call["state"],
            "is_error": bool(call["is_error"]),
            "http_status": call["http_status"],
            "response_model": call["response_model"],
            "response_id": call["response_id"],
            "response_status": call["response_status"],
            "raw_response": parsed,
        }
        if raw_text is not None:
            entry["raw_response_text"] = raw_text
        responses.append(entry)

    output = {
        "format_version": 1,
        "session": {
            "id": session["id"],
            "model": session["model"],
            "prompt": session["prompt"],
            "http_status": session["http_status"],
            "created_at": session["created_at"],
            "final_answer": session["response"],
        },
        "responses": responses,
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
            SELECT path FROM discovered_databases
            WHERE user_decision = 'allowed' AND is_valid_sqlite = 1
            """
        ).fetchall()
    finally:
        connection.close()
    return {Path(str(row[0])).resolve() for row in rows}


def main() -> int:
    args = parse_args()
    harness_dir = Path(__file__).resolve().parent
    runner = args.runner.resolve()
    database_manifest = args.database_manifest.resolve()
    env_file = args.env_file.resolve()
    system_prompt = args.system_prompt.resolve()

    required = (runner, database_manifest, env_file, system_prompt)
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        print("Missing required file(s):", file=sys.stderr)
        for path in missing:
            print(f"  {path}", file=sys.stderr)
        return 2
    try:
        databases = load_database_fixtures(database_manifest)
    except (json.JSONDecodeError, OSError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 2
    selected = [entry for entry in MODELS if args.model in (None, entry[0])]
    run_dir = unique_run_directory(harness_dir / "runs")
    metadata = {
        "created_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "prompt": args.prompt,
        "models": [model for model, _ in selected],
        "database_manifest": str(database_manifest),
        "database_count": len(databases),
        "database_fixtures": [
            {"path": str(database)} for database in databases
        ],
        "seeded_user_fact": SEEDED_USER_FACT,
        "system_prompt": str(system_prompt),
        "system_prompt_sha256": sha256(system_prompt),
        "resource_sha256": {
            name: sha256(system_prompt.parent / name)
            for name in (
                "PromptSystem.txt",
                "GuidanceTools.json",
                "GuidanceAudit.json",
                "GuidanceDatabase.json",
            )
            if (system_prompt.parent / name).is_file()
        },
        "results": {},
    }
    (run_dir / "run.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )

    any_failed = False
    for model, slug in selected:
        model_dir = run_dir / slug
        model_dir.mkdir()
        session_db = model_dir / "strappy.sqlite"
        answer_file = model_dir / "answer.md"
        output_file = model_dir / "output.json"
        print(f"\n> Model | {model}", flush=True)
        started = dt.datetime.now(dt.timezone.utc)
        command = [
            str(runner),
            "--model",
            model,
            "--session-db",
            str(session_db),
            "--env-file",
            str(env_file),
            "--system-prompt",
            str(system_prompt),
            "--answer-file",
            str(answer_file),
            "--prompt",
            args.prompt,
        ]
        for database in databases:
            command.extend(["--database", str(database)])
        completed = subprocess.run(command, check=False)
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
                    print(registration_error, file=sys.stderr)
            except (OSError, sqlite3.Error) as error:
                registration_error = str(error)
                print(
                    f"Could not verify database registration in {session_db}: "
                    f"{error}",
                    file=sys.stderr,
                )
            try:
                export_output(session_db, output_file)
            except (RuntimeError, sqlite3.Error, OSError) as error:
                export_error = str(error)
                print(f"Could not export {output_file}: {error}", file=sys.stderr)
        metadata["results"][model] = {
            "slug": slug,
            "exit_code": completed.returncode,
            "started_at": started.isoformat(),
            "completed_at": finished.isoformat(),
            "elapsed_seconds": (finished - started).total_seconds(),
            "registered_database_count": registered_database_count,
            "database_registration_error": registration_error,
            "output_file": (
                str(output_file.relative_to(run_dir))
                if output_file.is_file()
                else None
            ),
            "output_export_error": export_error,
        }
        (run_dir / "run.json").write_text(
            json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
        )
        if completed.returncode != 0 or registration_error is not None:
            any_failed = True

    evaluator = harness_dir / "evaluate.py"
    evaluation = subprocess.run(
        [sys.executable, str(evaluator), "--run-dir", str(run_dir)],
        check=False,
    )
    if evaluation.returncode != 0:
        any_failed = True

    print("\n> Run Complete")
    print(f">> Artifacts | {run_dir}")
    print(f">> Evaluation | {run_dir / 'report.md'}")
    return 1 if any_failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
