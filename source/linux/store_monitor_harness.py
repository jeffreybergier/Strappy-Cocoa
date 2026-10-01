"""Readiness gates startup, disconnects, incompatible peers, and restarts."""
from pathlib import Path
import select
import signal
import sqlite3
import subprocess
import sys
import tempfile

daemon, monitor = [str(Path(p).resolve()) for p in sys.argv[1:]]
resources = str(Path('../shared/Resources').resolve())

def expect(process, line):
    assert select.select([process.stdout], [], [], 8)[0], 'monitor timeout'
    assert process.stdout.readline().strip() == line

with tempfile.TemporaryDirectory(prefix='strappy-monitor-', dir='/tmp') as tmp:
    directory = Path(tmp)
    database = directory / 'strappy.sqlite'
    arguments = [daemon, str(directory / 'heartbeat.log'), str(database), resources]
    client = subprocess.Popen([monitor, str(directory / 'service.sock'), str(database)],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    server = None
    try:
        expect(client, 'unavailable')
        assert not database.exists()
        for _ in range(2):
            server = subprocess.Popen(arguments)
            expect(client, 'ready')
            server.send_signal(signal.SIGTERM)
            assert server.wait(timeout=3) == 0
            server = None
            expect(client, 'unavailable')
        # Refuse an incompatible on-disk version without migration or repair.
        with sqlite3.connect(database) as db:
            db.execute('PRAGMA journal_mode=DELETE')
            db.execute('PRAGMA user_version=2')
        assert subprocess.run(arguments, capture_output=True).returncode != 0
        with sqlite3.connect(database) as db:
            assert db.execute('PRAGMA user_version').fetchone()[0] == 2
            assert db.execute('PRAGMA journal_mode').fetchone()[0] == 'delete'
        print('store_monitor_harness: startup race, disconnect, restart, incompatible schema passed')
    finally:
        client.stdin.close()
        client.wait(timeout=3)
        if server is not None:
            server.terminate()
            server.wait(timeout=3)
