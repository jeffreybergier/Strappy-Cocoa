#!/usr/bin/env python3
"""Frozen ground truth and answer matching for the hill-climbing harness."""

from __future__ import annotations

import json
import re
import sqlite3
from pathlib import Path
from typing import Any


REQUIRED_GROUP_COUNT = 5
ROSTER_FORMAT_VERSION = 1

# These aliases normalize database artist labels. Member aliases and their
# matching expressions belong to the frozen private roster snapshot instead.
ALLOWED_ARTIST_ALIASES: dict[str, tuple[str, ...]] = {
    "the boyz": ("the boyz", "boyz"),
    "zerobaseone": ("zerobaseone", "zerobase", "zb1"),
    "tomorrow x together": ("tomorrow x together", "txt"),
    "&team": ("&team",),
    "enhypen": ("enhypen",),
    "seventeen": ("seventeen",),
    "treasure": ("treasure",),
    "boynextdoor": ("boynextdoor",),
    "tws": ("tws",),
}

# A Python ``re`` character that is a Unicode word character but neither a
# digit nor underscore is treated as a letter for member-name boundaries.
LETTER_CHARACTER = r"[^\W\d_]"


def connect_readonly(path: Path) -> sqlite3.Connection:
    connection = sqlite3.connect(f"file:{path.resolve()}?mode=ro", uri=True)
    connection.row_factory = sqlite3.Row
    return connection


def resolve_database_manifest(metadata: dict[str, Any]) -> Path:
    recorded_value = metadata.get("database_manifest")
    if not isinstance(recorded_value, str):
        raise RuntimeError("Run metadata does not identify its database manifest.")

    recorded = Path(recorded_value)
    harness_dir = Path(__file__).resolve().parent
    candidates = (recorded, harness_dir / "private/gomadango/databases.json")
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError("The run's database manifest is unavailable.")


def resolve_ranking_database(metadata: dict[str, Any]) -> Path:
    if "database_manifest" not in metadata:
        recorded_value = metadata.get("media_db")
        if not isinstance(recorded_value, str):
            raise RuntimeError(
                "Legacy run metadata does not identify its ranking database."
            )
        recorded = Path(recorded_value)
        harness_dir = Path(__file__).resolve().parent
        candidates = (
            recorded,
            harness_dir
            / "private/gomadango/root/var/mobile/Media/iTunes_Control/iTunes/MediaLibrary.sqlitedb",
            harness_dir
            / "private/Media/iTunes_Control/iTunes/MediaLibrary.sqlitedb",
        )
        for candidate in candidates:
            if candidate.is_file():
                return candidate
        raise RuntimeError("The legacy run's ranking database is unavailable.")

    suffix = "/Media/iTunes_Control/iTunes/MediaLibrary.sqlitedb"
    manifest_path = resolve_database_manifest(metadata)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = manifest.get("databases")
    if manifest.get("format_version") != 1 or not isinstance(entries, list):
        raise RuntimeError(f"Invalid database manifest: {manifest_path}")

    matches = [
        entry
        for entry in entries
        if isinstance(entry, dict)
        and isinstance(entry.get("remote_path"), str)
        and entry["remote_path"].endswith(suffix)
    ]
    if len(matches) != 1:
        raise RuntimeError(
            "The run manifest must contain exactly one ranking database; "
            f"found {len(matches)}."
        )
    local_path = matches[0].get("local_path")
    if not isinstance(local_path, str):
        raise RuntimeError(f"Invalid ranking database entry in {manifest_path}")
    fixture_root = manifest_path.parent.resolve()
    database = (fixture_root / local_path).resolve()
    if fixture_root not in database.parents:
        raise RuntimeError(f"Ranking database escapes private root: {database}")
    if not database.is_file():
        raise RuntimeError(f"The ranking database is unavailable: {database}")
    return database


def expected_artists(ranking_db: Path, limit: int = 10) -> list[tuple[str, int]]:
    sql = """
        SELECT ia.item_artist AS artist,
               SUM(COALESCE(s.play_count_user, 0)) AS total_plays
        FROM item i
        JOIN item_artist ia ON ia.item_artist_pid = i.item_artist_pid
        JOIN genre g ON g.genre_id = i.genre_id
        LEFT JOIN item_stats s ON s.item_pid = i.item_pid
        WHERE LOWER(REPLACE(REPLACE(g.genre, '-', ''), ' ', '')) = 'kpop'
        GROUP BY ia.item_artist
        HAVING total_plays > 0
        ORDER BY total_plays DESC, COUNT(*) DESC, ia.item_artist COLLATE NOCASE
    """
    ignored_fragments = (" feat. ", "jonas brothers")
    with connect_readonly(ranking_db) as database:
        rows = database.execute(sql).fetchall()
    primary = []
    for row in rows:
        artist = str(row["artist"])
        if any(fragment in artist.lower() for fragment in ignored_fragments):
            continue
        primary.append((artist, int(row["total_plays"])))
        if len(primary) == limit:
            break
    return primary


def artist_aliases(artist: str) -> tuple[str, ...]:
    lowered = artist.casefold()
    return ALLOWED_ARTIST_ALIASES.get(lowered, (lowered,))


def artists_match(first: str, second: str) -> bool:
    first_aliases = set(artist_aliases(first))
    second_aliases = set(artist_aliases(second))
    return bool(first_aliases.intersection(second_aliases))


def standalone_alias_pattern(alias: str, *, case_sensitive: bool) -> str:
    """Return a serialized regex for a standalone member alias.

    Hyphen-connected words are treated as a single word. This extra rule is
    important for the member named ``K``: a plain non-letter boundary would
    otherwise award that member for the ``K`` in ``K-pop``.
    """

    if not isinstance(alias, str) or not alias:
        raise ValueError("A member alias must be non-empty.")
    literal = re.escape(alias)
    if not case_sensitive:
        literal = f"(?i:{literal})"
    return (
        rf"(?<!{LETTER_CHARACTER})(?<!{LETTER_CHARACTER}-)"
        rf"{literal}(?!{LETTER_CHARACTER}|-{LETTER_CHARACTER})"
    )


def _require_nonempty_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise RuntimeError(f"Member roster {label} must be a non-empty string.")
    return value


def validate_member_roster(
    roster: dict[str, Any],
    expected_top_five: list[tuple[str, int]] | None = None,
) -> dict[str, Any]:
    if not isinstance(roster, dict) or roster.get("format_version") != ROSTER_FORMAT_VERSION:
        raise RuntimeError("Member roster format_version must be 1.")
    if roster.get("required_group_count") != REQUIRED_GROUP_COUNT:
        raise RuntimeError(
            f"Member roster must target exactly {REQUIRED_GROUP_COUNT} groups."
        )
    _require_nonempty_string(roster.get("retrieved_at"), "retrieved_at")
    groups = roster.get("groups")
    if not isinstance(groups, list) or len(groups) != REQUIRED_GROUP_COUNT:
        raise RuntimeError(
            f"Member roster must contain exactly {REQUIRED_GROUP_COUNT} groups."
        )
    if expected_top_five is not None and len(expected_top_five) != REQUIRED_GROUP_COUNT:
        raise RuntimeError(
            f"Ranking fixture must produce {REQUIRED_GROUP_COUNT} expected groups."
        )

    seen_artists: set[str] = set()
    seen_members: set[str] = set()
    for group_index, group in enumerate(groups):
        if not isinstance(group, dict):
            raise RuntimeError(f"Member roster group {group_index + 1} is invalid.")
        artist = _require_nonempty_string(
            group.get("artist"), f"group {group_index + 1} artist"
        )
        artist_key = artist.casefold()
        if artist_key in seen_artists:
            raise RuntimeError(f"Member roster repeats artist {artist}.")
        seen_artists.add(artist_key)
        if expected_top_five is not None and not artists_match(
            artist, expected_top_five[group_index][0]
        ):
            raise RuntimeError(
                f"Member roster group {group_index + 1} is {artist}, expected "
                f"{expected_top_five[group_index][0]}."
            )

        source = group.get("source")
        if not isinstance(source, dict):
            raise RuntimeError(f"Member roster source for {artist} is invalid.")
        _require_nonempty_string(source.get("title"), f"source title for {artist}")
        source_url = _require_nonempty_string(
            source.get("url"), f"source URL for {artist}"
        )
        if not source_url.startswith(("http://", "https://")):
            raise RuntimeError(f"Member roster source URL for {artist} is invalid.")

        members = group.get("members")
        if not isinstance(members, list) or not members:
            raise RuntimeError(f"Member roster for {artist} has no members.")
        for member_index, member in enumerate(members):
            if not isinstance(member, dict):
                raise RuntimeError(
                    f"Member roster entry {member_index + 1} for {artist} is invalid."
                )
            name = _require_nonempty_string(
                member.get("name"), f"member {member_index + 1} for {artist}"
            )
            name_key = name.casefold()
            if name_key in seen_members:
                raise RuntimeError(f"Member roster repeats member name {name}.")
            seen_members.add(name_key)
            patterns = member.get("patterns")
            if not isinstance(patterns, list) or not patterns:
                raise RuntimeError(f"Member roster entry {name} has no regex patterns.")
            for pattern in patterns:
                _require_nonempty_string(pattern, f"regex pattern for {name}")
                try:
                    re.compile(pattern)
                except re.error as error:
                    raise RuntimeError(
                        f"Member roster regex for {name} is invalid: {error}"
                    ) from error
    return roster


def load_member_roster(
    path: Path,
    expected_top_five: list[tuple[str, int]] | None = None,
) -> dict[str, Any]:
    if not path.is_file():
        raise RuntimeError(f"Member roster is unavailable: {path}")
    try:
        parsed = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"Could not read member roster {path}: {error}") from error
    return validate_member_roster(parsed, expected_top_five)


def roster_member_count(roster: dict[str, Any]) -> int:
    return sum(len(group["members"]) for group in roster["groups"])


def matched_roster_members(
    answer: str,
    roster: dict[str, Any],
) -> list[dict[str, str]]:
    matches: list[dict[str, str]] = []
    for group in roster["groups"]:
        for member in group["members"]:
            for pattern in member["patterns"]:
                if re.search(pattern, answer):
                    matches.append(
                        {
                            "artist": str(group["artist"]),
                            "name": str(member["name"]),
                            "pattern": str(pattern),
                        }
                    )
                    break
    return matches
