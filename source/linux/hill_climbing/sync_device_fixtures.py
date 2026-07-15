#!/usr/bin/env python3
"""Sync Strappy-approved SQLite fixtures from a configured device."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import shlex
import shutil
import sqlite3
import subprocess
from pathlib import Path, PurePosixPath


REMOTE_CATALOG = (
    "/private/var/mobile/Library/Application Support/Strappy/strappy.sqlite"
)
MANIFEST_NAME = "databases.json"
SQLITE_SIDECARS = ("-wal", "-shm", "-journal")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture-dir", required=True, type=Path)
    parser.add_argument("--host", default="gomadango")
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--verify-only", action="store_true")
    return parser.parse_args()


def verify_sqlite(path: Path) -> None:
    transient_paths = (Path(f"{path}-wal"), Path(f"{path}-shm"))
    existed = {candidate: candidate.exists() for candidate in transient_paths}
    connection = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    try:
        rows = connection.execute("PRAGMA quick_check").fetchall()
    finally:
        connection.close()
        for candidate in transient_paths:
            if not existed[candidate]:
                candidate.unlink(missing_ok=True)
    messages = [str(row[0]) for row in rows]
    if messages != ["ok"]:
        raise RuntimeError(f"SQLite quick_check failed for {path}: {messages}")


def remote_exists(host: str, path: str) -> bool:
    command = f"test -f {shlex.quote(path)}"
    completed = subprocess.run(
        ["ssh", "-o", "BatchMode=yes", host, command],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if completed.returncode == 0:
        return True
    if completed.returncode == 1:
        return False
    raise RuntimeError(
        f"Could not inspect SQLite companion on {host}: {path}"
    )


def copy_remote_file(host: str, remote_path: str, local_path: Path) -> None:
    local_path.parent.mkdir(parents=True, exist_ok=True)
    remote = f"{host}:{shlex.quote(remote_path)}"
    subprocess.run(
        ["scp", "-T", "-p", "-o", "BatchMode=yes", remote, str(local_path)],
        check=True,
    )


def local_path_for_remote(root: Path, remote_path: str) -> Path:
    remote = PurePosixPath(remote_path)
    if not remote.is_absolute() or ".." in remote.parts:
        raise RuntimeError(f"Unsafe catalog path: {remote_path}")
    return root.joinpath(*remote.parts[1:])


def load_allowed_databases(catalog: Path) -> list[sqlite3.Row]:
    transient_paths = (Path(f"{catalog}-wal"), Path(f"{catalog}-shm"))
    existed = {candidate: candidate.exists() for candidate in transient_paths}
    connection = sqlite3.connect(f"file:{catalog}?mode=ro", uri=True)
    connection.row_factory = sqlite3.Row
    try:
        tables = {
            str(row[0])
            for row in connection.execute(
                "SELECT name FROM sqlite_master WHERE type = 'table'"
            )
        }
        if {
            "databases",
            "database_locations",
            "database_permissions",
        }.issubset(tables):
            rows = connection.execute(
                """
                SELECT d.assistant_database_id, l.path,
                       l.size_bytes AS size,
                       l.modified_at_s AS modified_at,
                       l.validation_state AS scan_status,
                       CASE WHEN l.validation_state = 'valid'
                            THEN 1 ELSE 0 END AS is_valid_sqlite
                FROM databases d
                JOIN database_locations l
                  ON l.database_id = d.id AND l.active = 1
                JOIN database_permissions p ON p.database_id = d.id
                WHERE p.decision = 'allowed'
                ORDER BY d.id, l.id
                """
            ).fetchall()
        else:
            # Device fixtures may come from an older installed app. Reading
            # that catalog remains useful; every runner-created database below
            # is required to use the current semantic schema.
            rows = connection.execute(
                """
                SELECT assistant_database_id, path, size, modified_at,
                       scan_status, is_valid_sqlite
                FROM discovered_databases
                WHERE user_decision = 'allowed'
                ORDER BY id
                """
            ).fetchall()
    finally:
        connection.close()
        for candidate in transient_paths:
            if not existed[candidate]:
                candidate.unlink(missing_ok=True)
    if not rows:
        raise RuntimeError("The Strappy catalog has no approved databases.")
    invalid = [row["path"] for row in rows if not row["is_valid_sqlite"]]
    if invalid:
        raise RuntimeError(f"Approved catalog rows are not valid: {invalid}")
    return rows


def verify_manifest(fixture_dir: Path) -> dict[str, object]:
    manifest_path = fixture_dir / MANIFEST_NAME
    if not manifest_path.is_file():
        raise RuntimeError(f"Missing fixture manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    databases = manifest.get("databases")
    if manifest.get("format_version") != 1 or not isinstance(databases, list):
        raise RuntimeError(f"Invalid fixture manifest: {manifest_path}")
    if not databases:
        raise RuntimeError(f"Fixture manifest is empty: {manifest_path}")

    seen: set[Path] = set()
    for entry in databases:
        if not isinstance(entry, dict) or not isinstance(
            entry.get("local_path"), str
        ):
            raise RuntimeError(f"Invalid database entry in {manifest_path}")
        database = (fixture_dir / entry["local_path"]).resolve()
        if fixture_dir.resolve() not in database.parents:
            raise RuntimeError(f"Fixture escapes private root: {database}")
        if database in seen:
            raise RuntimeError(f"Duplicate fixture path: {database}")
        seen.add(database)
        if not database.is_file():
            raise RuntimeError(f"Missing database fixture: {database}")
        companions = entry.get("companions", [])
        if not isinstance(companions, list):
            raise RuntimeError(f"Invalid companion list for {database}")
        for companion in companions:
            if not isinstance(companion, dict) or not isinstance(
                companion.get("local_path"), str
            ):
                raise RuntimeError(f"Invalid SQLite companion for {database}")
            companion_path = (fixture_dir / companion["local_path"]).resolve()
            if fixture_dir.resolve() not in companion_path.parents:
                raise RuntimeError(
                    f"SQLite companion escapes private root: {companion_path}"
                )
            if companion_path.exists() and not companion_path.is_file():
                raise RuntimeError(f"Invalid SQLite companion: {companion_path}")
        verify_sqlite(database)

    catalog_entry = manifest.get("catalog")
    if not isinstance(catalog_entry, dict):
        raise RuntimeError(f"Missing catalog metadata in {manifest_path}")
    catalog = (
        fixture_dir / str(catalog_entry.get("local_path", ""))
    ).resolve()
    if fixture_dir.resolve() not in catalog.parents:
        raise RuntimeError(f"Catalog escapes private root: {catalog}")
    if not catalog.is_file():
        raise RuntimeError(f"Missing catalog fixture: {catalog}")
    catalog_companions = catalog_entry.get("companions", [])
    if not isinstance(catalog_companions, list):
        raise RuntimeError(f"Invalid catalog companion list: {catalog}")
    for companion in catalog_companions:
        if not isinstance(companion, dict) or not isinstance(
            companion.get("local_path"), str
        ):
            raise RuntimeError(f"Invalid catalog companion: {catalog}")
        companion_path = (fixture_dir / companion["local_path"]).resolve()
        if fixture_dir.resolve() not in companion_path.parents:
            raise RuntimeError(
                f"Catalog companion escapes private root: {companion_path}"
            )
        if companion_path.exists() and not companion_path.is_file():
            raise RuntimeError(f"Invalid catalog companion: {companion_path}")
    verify_sqlite(catalog)
    return manifest


def verify_registration(
    fixture_dir: Path,
    manifest: dict[str, object],
    runner: Path,
) -> None:
    runner = runner.resolve()
    if not runner.is_file():
        raise RuntimeError(f"Missing hill-climbing runner: {runner}")
    entries = manifest["databases"]
    databases = [
        (fixture_dir / entry["local_path"]).resolve() for entry in entries
    ]
    session_db = fixture_dir / ".registration-check.sqlite"
    family = [
        session_db,
        Path(f"{session_db}-wal"),
        Path(f"{session_db}-shm"),
    ]
    for path in family:
        path.unlink(missing_ok=True)
    command = [
        str(runner),
        "--prepare-only",
        "--model",
        "google/gemma-4-31b-it",
        "--session-db",
        str(session_db),
    ]
    for database in databases:
        command.extend(["--database", str(database)])
    try:
        subprocess.run(command, check=True)
        connection = sqlite3.connect(f"file:{session_db}?mode=ro", uri=True)
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
            user_facts = connection.execute(
                """
                SELECT kind, subject, predicate, value,
                       confidence_basis_points / 10000.0,
                       'model_observed'
                FROM user_facts
                ORDER BY id
                """
            ).fetchall()
        finally:
            connection.close()
        registered = {Path(str(row[0])).resolve() for row in rows}
        expected = set(databases)
        if registered != expected:
            raise RuntimeError(
                "Runner registration mismatch: "
                f"expected {len(expected)}, found {len(registered)}"
            )
        expected_user_fact = [
            (
                "fact",
                "user",
                "fact",
                "The user's name is Jeff.",
                0.75,
                "model_observed",
            )
        ]
        if user_facts != expected_user_fact:
            raise RuntimeError(
                "Runner remembered-user-fact mismatch: "
                f"expected {expected_user_fact}, found {user_facts}"
            )
    finally:
        for path in family:
            path.unlink(missing_ok=True)


def sync(host: str, fixture_dir: Path) -> dict[str, object]:
    fixture_dir.mkdir(parents=True, exist_ok=True)
    catalog_dir = fixture_dir / "catalog"
    catalog_dir.mkdir(parents=True, exist_ok=True)
    catalog = catalog_dir / "strappy.sqlite"
    catalog_download = catalog_dir / "strappy.sqlite.download"
    for path in (
        catalog_download,
        Path(f"{catalog_download}-wal"),
        Path(f"{catalog_download}-shm"),
        Path(f"{catalog_download}-journal"),
    ):
        path.unlink(missing_ok=True)

    print(f"Fetching Strappy catalog from {host} ...", flush=True)
    copy_remote_file(host, REMOTE_CATALOG, catalog_download)
    catalog_companion_paths: list[tuple[str, Path]] = []
    for suffix in SQLITE_SIDECARS:
        remote_companion = f"{REMOTE_CATALOG}{suffix}"
        if not remote_exists(host, remote_companion):
            continue
        local_companion = Path(f"{catalog_download}{suffix}")
        copy_remote_file(host, remote_companion, local_companion)
        catalog_companion_paths.append((suffix, local_companion))
    verify_sqlite(catalog_download)
    allowed = load_allowed_databases(catalog_download)

    root = fixture_dir / "root"
    root_download = fixture_dir / "root.download"
    root_previous = fixture_dir / "root.previous"
    if root_download.exists():
        shutil.rmtree(root_download)
    root_download.mkdir(parents=True)

    entries: list[dict[str, object]] = []
    for index, row in enumerate(allowed, start=1):
        remote_path = str(row["path"])
        local_path = local_path_for_remote(root_download, remote_path)
        final_path = local_path_for_remote(root, remote_path)
        print(
            f"[{index}/{len(allowed)}] Fetching {remote_path}",
            flush=True,
        )
        copy_remote_file(host, remote_path, local_path)
        companions: list[dict[str, object]] = []
        for suffix in SQLITE_SIDECARS:
            remote_companion = f"{remote_path}{suffix}"
            if not remote_exists(host, remote_companion):
                continue
            local_companion = Path(f"{local_path}{suffix}")
            copy_remote_file(host, remote_companion, local_companion)
            companions.append(
                {
                    "suffix": suffix,
                    "local_path": str(
                        Path(f"{final_path}{suffix}").relative_to(fixture_dir)
                    ),
                    "size": local_companion.stat().st_size,
                }
            )
        verify_sqlite(local_path)
        entries.append(
            {
                "assistant_database_id": row["assistant_database_id"],
                "remote_path": remote_path,
                "local_path": str(final_path.relative_to(fixture_dir)),
                "catalog_size": row["size"],
                "catalog_modified_at": row["modified_at"],
                "size": local_path.stat().st_size,
                "companions": companions,
            }
        )

    manifest: dict[str, object] = {
        "format_version": 1,
        "synced_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source_host": host,
        "source_catalog_path": REMOTE_CATALOG,
        "catalog": {
            "local_path": str(catalog.relative_to(fixture_dir)),
            "size": catalog_download.stat().st_size,
            "companions": [
                {
                    "suffix": suffix,
                    "local_path": str(
                        Path(f"{catalog}{suffix}").relative_to(fixture_dir)
                    ),
                    "size": path.stat().st_size,
                }
                for suffix, path in catalog_companion_paths
            ],
        },
        "databases": entries,
    }
    if root_previous.exists():
        shutil.rmtree(root_previous)
    if root.exists():
        os.replace(root, root_previous)
    try:
        os.replace(root_download, root)
    except OSError:
        if root_previous.exists():
            os.replace(root_previous, root)
        raise
    shutil.rmtree(root_previous, ignore_errors=True)
    for suffix in SQLITE_SIDECARS:
        Path(f"{catalog}{suffix}").unlink(missing_ok=True)
    os.replace(catalog_download, catalog)
    for suffix, path in catalog_companion_paths:
        os.replace(path, Path(f"{catalog}{suffix}"))
    manifest_path = fixture_dir / MANIFEST_NAME
    manifest_download = fixture_dir / f"{MANIFEST_NAME}.download"
    manifest_download.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    os.replace(manifest_download, manifest_path)
    return verify_manifest(fixture_dir)


def main() -> int:
    args = parse_args()
    fixture_dir = args.fixture_dir.resolve()
    manifest = (
        verify_manifest(fixture_dir)
        if args.verify_only
        else sync(args.host, fixture_dir)
    )
    if args.runner is not None:
        verify_registration(fixture_dir, manifest, args.runner)
    databases = manifest["databases"]
    print(
        f"Verified {len(databases)} approved database fixtures in "
        f"{fixture_dir}; remembered user fact is seeded",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
