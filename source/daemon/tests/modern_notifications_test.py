#!/usr/bin/env python3
"""Run the recording-only modern daemon bridge fixture on macOS."""
from pathlib import Path
import platform
import subprocess

root = Path(__file__).resolve().parents[3]
if platform.system() != "Darwin":
    raise SystemExit("This fixture requires macOS Foundation and clang.")
output = root / "build/tests/modern-notifications/bridge-test"
output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([
    "clang", "-fblocks", "-fno-objc-arc", "-Wall", "-Wextra", "-Werror",
    "-Wno-deprecated-declarations", "-Wno-semicolon-before-method-body",
    "-framework", "Foundation", str(Path(__file__).with_suffix(".m")),
    "-o", str(output),
], check=True)
subprocess.run([str(output)], check=True)
