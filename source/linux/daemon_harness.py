"""Exercise the real daemon's rotation and cooperative signal wakeup offline."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="strappy-ipc-", dir="/tmp") as directory:
    log = Path(directory) / "heartbeat.log"
    backup = Path(str(log) + ".1")
    for run in range(2):
        previous = bytes([65 + run]) * 65536
        log.write_bytes(previous)
        process = subprocess.Popen([binary, str(log), str(Path(directory) / 'strappy.sqlite'), str(Path('build-test/daemon-resources').resolve())])
        try:
            deadline = time.monotonic() + 5
            while True:
                try:
                    if f"event=startup pid={process.pid} ".encode() in log.read_bytes():
                        break
                except FileNotFoundError:
                    pass  # Rotation has renamed the old file; creation follows.
                assert process.poll() is None, "daemon exited before startup"
                assert time.monotonic() < deadline, "startup timed out"
                time.sleep(0.01)
            process.send_signal(signal.SIGTERM)
            assert process.wait(timeout=2) == 0, "shutdown failed"
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
        records = log.read_text().splitlines()
        assert len(records) == 2, records
        assert "event=startup" in records[0]
        assert "event=shutdown" in records[1]
        assert all(f"pid={process.pid} euid={os.geteuid()} heartbeat=0" in r for r in records)
        assert backup.read_bytes() == previous, "rotation must replace the sole backup"
        assert log.stat().st_size <= 65536
        assert log.stat().st_mode & 0o777 == 0o600
    assert subprocess.run([binary, "relative.log"]).returncode != 0
    assert subprocess.run([binary, str(Path(directory) / "missing/log")]).returncode != 0
print("daemon_harness: rotation, identity, permissions, SIGTERM wakeup, invalid paths passed")
