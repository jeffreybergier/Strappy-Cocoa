#!/bin/sh
set -eu

sqlite_version=3530400
sqlite_year=2026
sqlite_source_sha3=b834d474b9b393d85a9e3ee4cc11f1329e007e9376a424ee740796f5c4bda3a8
sqlite_amalgamation_sha3=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
sqlite_build_root=${1:?SQLite build directory is required}
sqlite_source_archive="$sqlite_build_root/sqlite-src-$sqlite_version.zip"
sqlite_amalgamation_archive="$sqlite_build_root/sqlite-amalgamation-$sqlite_version.zip"
sqlite_source_dir="$sqlite_build_root/sqlite-src-$sqlite_version"
sqlite_marker="$sqlite_source_dir/ext/wasm/config.make"
sqlite_tool_dir="$sqlite_build_root/tools"
cjson_version=1.7.19
cjson_sha3=0296865ae876e483634cc56be45417e6fba514dcf2b96602405bc860ddfe4c02
cjson_archive="$sqlite_build_root/cjson-$cjson_version.zip"
cjson_source_dir="$sqlite_build_root/cJSON-$cjson_version"

if [ -f "$sqlite_marker" ] && [ -f "$cjson_source_dir/cJSON.c" ]; then
  exit 0
fi

mkdir -p "$sqlite_build_root"
mkdir -p "$sqlite_tool_dir"
ln -sf /emsdk/upstream/bin/llvm-strip "$sqlite_tool_dir/wasm-strip"
sqlite_tool_path=$(cd "$sqlite_tool_dir" && pwd)

fetch_archive() {
  archive_url=$1
  archive_path=$2
  expected_sha3=$3
  if [ ! -f "$archive_path" ]; then
    curl -fsSL "$archive_url" -o "$archive_path"
  fi
  python3 - "$archive_path" "$expected_sha3" <<'PY'
import hashlib
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
expected = sys.argv[2]
actual = hashlib.sha3_256(path.read_bytes()).hexdigest()
if actual != expected:
    raise SystemExit(f"SHA3-256 mismatch for {path}: {actual}")
PY
}

fetch_archive \
  "https://sqlite.org/$sqlite_year/sqlite-src-$sqlite_version.zip" \
  "$sqlite_source_archive" \
  "$sqlite_source_sha3"
fetch_archive \
  "https://sqlite.org/$sqlite_year/sqlite-amalgamation-$sqlite_version.zip" \
  "$sqlite_amalgamation_archive" \
  "$sqlite_amalgamation_sha3"
fetch_archive \
  "https://github.com/DaveGamble/cJSON/archive/refs/tags/v$cjson_version.zip" \
  "$cjson_archive" \
  "$cjson_sha3"

if [ ! -f "$cjson_source_dir/cJSON.c" ]; then
  unzip -q "$cjson_archive" -d "$sqlite_build_root"
fi

if [ -f "$sqlite_marker" ]; then
  exit 0
fi

rm -rf "$sqlite_source_dir"
unzip -q "$sqlite_source_archive" -d "$sqlite_build_root"
unzip -q "$sqlite_amalgamation_archive" -d "$sqlite_build_root"
cp "$sqlite_build_root/sqlite-amalgamation-$sqlite_version/sqlite3.c" \
  "$sqlite_source_dir/sqlite3.c"
cp "$sqlite_build_root/sqlite-amalgamation-$sqlite_version/sqlite3.h" \
  "$sqlite_source_dir/sqlite3.h"

(cd "$sqlite_source_dir" && \
  PATH="$sqlite_tool_path:$PATH" \
  ./configure --disable-tcl --with-emsdk=/emsdk)
