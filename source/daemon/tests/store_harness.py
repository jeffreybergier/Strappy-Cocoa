"""Run independent writer and UI-capability executables on a disposable store."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="strappy-store-", dir="/tmp") as directory:
    path = str(Path(directory) / "strappy.sqlite")
    for binary in sys.argv[1:]:
        subprocess.run([str(Path(binary).resolve()), path], check=True)
