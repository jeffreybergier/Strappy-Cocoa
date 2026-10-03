"""Unsupported semantic constraints must fail before daemon readiness."""
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

writer, reader, daemon = (str(Path(p).resolve()) for p in sys.argv[1:])
resources = str(Path('build-test/daemon-resources').resolve())
with tempfile.TemporaryDirectory(prefix='strappy-schema-', dir='/tmp') as directory:
    path = str(Path(directory)/'legacy.sqlite')
    subprocess.run([writer, path, '--legacy'], check=True)
    subprocess.run([reader, path, '--incompatible'], check=True)
    with sqlite3.connect(path) as db:
        before = list(db.iterdump())
    result = subprocess.run([daemon, str(Path(directory)/'heartbeat.log'), path, resources],
                            capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and 'incompatible UNIQUE' in result.stderr, result.stderr
    assert not (Path(directory)/'service.sock').exists()
    with sqlite3.connect(path) as db:
        assert list(db.iterdump()) == before
print('schema_harness: UI rejection, daemon refuses readiness, existing schema and rows preserved')
