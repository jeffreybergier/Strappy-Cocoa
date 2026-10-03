"""Exercise real AF_UNIX framing and a synthetic post-commit worker offline."""
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

binary, fixture = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
HEADER = struct.Struct('!4sHHIIII')

def frame(op, ident, payload=b'', version=7, kind=1, status=0):
    return HEADER.pack(b'STPY', version, kind, op, ident, len(payload), status) + payload

def exact(sock, count):
    result = b''
    while len(result) < count:
        chunk = sock.recv(count - len(result))
        if not chunk:
            raise EOFError()
        result += chunk
    return result

def receive(sock):
    magic, version, kind, op, ident, length, status = HEADER.unpack(exact(sock, 24))
    assert magic == b'STPY' and version == 7 and length <= 4096
    return kind, op, ident, status, exact(sock, length)

def connect(path):
    sock = socket.socket(socket.AF_UNIX)
    sock.settimeout(3)
    sock.connect(str(path))
    return sock

def request(sock, op, ident, payload=b'', status=0):
    sock.sendall(frame(op, ident, payload))
    result = receive(sock)
    assert result[:4] == (2, op, ident, status), result[:4]
    return result[4]

def hello(sock):
    return request(sock, 1, 1, struct.pack('!II', 7, 7))

def revision(payload):
    assert len(payload) == 36
    return struct.unpack('!Q', payload[16:24])[0]

def start(directory, worker=False):
    path = directory / 'service.sock'
    process = subprocess.Popen([fixture if worker else binary,
                                str(directory if worker else directory / 'heartbeat.log')] + ([] if worker else [str(directory / 'strappy.sqlite'), str(Path('build-test/daemon-resources').resolve())]),
                               stdin=subprocess.PIPE if worker else subprocess.DEVNULL)
    for _ in range(300):
        if path.exists():
            try:
                with connect(path):
                    return process
            except (ConnectionRefusedError, FileNotFoundError):
                pass
        assert process.poll() is None
        time.sleep(.01)
    raise AssertionError('startup timed out')

def stop(process):
    if process.stdin:
        process.stdin.close()
    process.send_signal(signal.SIGTERM)
    assert process.wait(timeout=3) == 0

with tempfile.TemporaryDirectory(prefix="strappy-ipc-", dir="/tmp") as temporary:
    directory = Path(temporary)
    path = directory / 'service.sock'
    process = start(directory)
    try:
        assert path.stat().st_mode & 0o777 == 0o600
        assert subprocess.run([binary, '--ipc-probe', str(path)], check=True).returncode == 0
        # A competing server cannot remove the live socket.
        assert subprocess.run([binary, str(directory / 'other.log'), str(directory / 'strappy.sqlite'), str(Path('build-test/daemon-resources').resolve())]).returncode != 0
        with connect(path) as sock:
            request(sock, 2, 1, status=3)
            snapshot = request(sock, 1, 2, struct.pack('!II', 7, 7))
            assert revision(snapshot) == 0
            assert struct.unpack('!III', snapshot[24:]) == (7, 4096, 2)
            instance = snapshot[:16]
            request(sock, 3, 3)
            request(sock, 3, 3, status=4)
            request(sock, 2, 2, status=4)
            for op in range(16, 25):
                request(sock, op, op, status=1)
            request(sock, 999, 30, status=6)
            request(sock, 2, 31, b'x', status=1)
            request(sock, 4, 32)
        # Every possible header split; coalesced requests; max payload.
        for split in range(1, 25):
            with connect(path) as sock:
                packet = frame(1, 1, struct.pack('!II', 7, 7))
                sock.sendall(packet[:split]); sock.sendall(packet[split:])
                assert receive(sock)[3] == 0
                sock.sendall(frame(2, 2) + frame(2, 3))
                assert receive(sock)[2] == 2 and receive(sock)[2] == 3
                request(sock, 16, 4, b'x' * 4096, status=1)
        for old_version in (1, 6):
            with connect(path) as sock:
                sock.sendall(frame(1, 1, version=old_version))
                assert receive(sock)[3] == 2
                assert sock.recv(1) == b''
        with connect(path) as sock:
            request(sock, 1, 1, struct.pack('!II', 8, 9), status=2)
            assert sock.recv(1) == b''
        for packet in [b'BAD!' + frame(2, 1)[4:],
                       HEADER.pack(b'STPY', 1, 1, 2, 1, 4097, 0),
                       HEADER.pack(b'STPY', 1, 0, 2, 1, 0, 0)]:
            with connect(path) as sock:
                sock.sendall(packet)
                assert sock.recv(1) == b''
        for kind, ident, status in [(2, 1, 0), (1, 0, 0), (1, 1, 1)]:
            with connect(path) as sock:
                sock.sendall(frame(2, ident, kind=kind, status=status))
                assert receive(sock)[3] == 1
        # Partial payload disconnects never affect a healthy client.
        for cut in [1, 23, 24, 28]:
            with connect(path) as sock:
                sock.sendall(frame(1, 1, struct.pack('!II', 7, 7))[:cut])
        # Fill a non-reading subscriber's output without delaying another client.
        with connect(path) as slow:
            hello(slow); request(slow, 3, 2)
            slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
            slow.setblocking(False)
            packet = b''.join(frame(2, i) for i in range(3, 50000))
            offset = 0
            deadline = time.monotonic() + 8
            dropped = False
            while time.monotonic() < deadline:
                try:
                    offset += slow.send(packet[offset:])
                except BlockingIOError:
                    time.sleep(.005)
                except (BrokenPipeError, ConnectionResetError):
                    dropped = True
                    break
                if offset == len(packet):
                    break
            slow.settimeout(3)
            if not dropped:
                try:
                    while slow.recv(65536):
                        pass
                    dropped = True
                except ConnectionResetError:
                    dropped = True
            assert dropped, 'slow peer must disconnect instead of losing notifications'
            with connect(path) as healthy:
                assert hello(healthy)[:16] == instance
        # Peer credentials, not filesystem permissions alone, reject another uid.
        if os.geteuid() == 0:
            os.chmod(directory, 0o711); os.chmod(path, 0o666)
            code = "import socket; s=socket.socket(socket.AF_UNIX); s.connect(%r); assert s.recv(1)==b''" % str(path)
            subprocess.run([sys.executable, '-c', code], check=True,
                           preexec_fn=lambda: os.setuid(65534), timeout=3)
            os.chmod(directory, 0o700); os.chmod(path, 0o600)
        # Abrupt exit leaves a recoverable socket; identity changes on restart.
        process.kill(); process.wait()
        assert path.exists()
        process = start(directory)
        with connect(path) as sock:
            assert hello(sock)[:16] != instance
    finally:
        stop(process)
    assert not path.exists()
    # Refuse public directories and symlink/socket collisions.
    os.chmod(directory, 0o755)
    assert subprocess.run([binary, str(directory / 'heartbeat.log')]).returncode != 0
    os.chmod(directory, 0o700)
    path.write_text('preserve')
    assert subprocess.run([binary, str(directory / 'heartbeat.log')]).returncode != 0
    assert path.read_text() == 'preserve'
    path.unlink()
    process = start(directory, worker=True)
    try:
        with connect(path) as subscriber:
            hello(subscriber)
            snapshot = request(subscriber, 3, 2)
            assert revision(snapshot) == 0
            for topic in range(4):
                process.stdin.write(bytes([topic])); process.stdin.flush()
                event = receive(subscriber)
                assert event[:4] == (3, 32 + topic, 0, 0)
                assert revision(event[4]) == topic + 1
                assert event[4][:16] == snapshot[:16]
            # An idle subscriber survives; an unfinished handshake expires.
            # The first event after idle gets its own output progress deadline.
            with connect(path) as unfinished:
                unfinished.sendall(b'S')
                time.sleep(31)
                assert unfinished.recv(1) == b''
            process.stdin.write(b'\x00'); process.stdin.flush()
            assert revision(receive(subscriber)[4]) == 5
            # Race a commit with a fresh atomic subscription: covered by either
            # its snapshot watermark or a subsequent event, never neither.
            for expected in range(6, 106):
                with connect(path) as other:
                    hello(other)
                    process.stdin.write(b'\x00'); process.stdin.flush()
                    snap = request(other, 3, 2)
                    if revision(snap) < expected:
                        assert revision(receive(other)[4]) >= expected
                event = receive(subscriber)
                assert revision(event[4]) == expected
            request(subscriber, 4, 3)
            process.stdin.write(b'\x00'); process.stdin.flush()
            time.sleep(.05)
            assert revision(request(subscriber, 2, 4)) == 106
            request(subscriber, 3, 5)
            # A command worker must preserve every event topic on its peer.
            request(subscriber, 16, 6)
            topics = set()
            for _ in range(4):
                event = receive(subscriber)
                assert event[0] == 3 and revision(event[4]) == 110
                topics.add(event[1])
            assert topics == {32, 33, 34, 35}
    finally:
        stop(process)
print('ipc_harness: framing, errors, peers, duplicates, slow readers, restart, worker events, atomic resync passed')
