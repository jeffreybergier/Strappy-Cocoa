"""Real service/runtime/SQLite/HTTP tests; all providers are local fixtures."""
import concurrent.futures
import http.server
import json
import os
from pathlib import Path
import signal
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import threading
import time

binary = str(Path(sys.argv[1]).resolve())
resources = str(Path('build-test/daemon-resources').resolve())
reader = str(Path('build-linux/run_reader_harness').resolve())
header = struct.Struct('!4sHHIIII')
gate = threading.Event()
entered = threading.Event()
requests = []
mode = 'answer'

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass
    def do_POST(self):
        global mode
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        requests.append(body)
        number = len(requests)
        entered.set()
        gate.wait(15)
        if mode == 'tool':
            output = [{'type': 'function_call', 'id': f'fc{number}', 'call_id': f'call{number}',
                       'name': 'bash', 'arguments': json.dumps({'command': 'echo $$ > tool.pid; sleep 20'})}]
        else:
            output = [{'type': 'message', 'id': f'msg{number}', 'role': 'assistant', 'status': 'completed',
                       'content': [{'type': 'output_text', 'text': 'Daemon fixture completed.', 'annotations': []}]}]
        answer = json.dumps({'id': f'response-fixture-{number}', 'object': 'response', 'status': 'completed', 'output': output}).encode()
        try:
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(answer)))
            self.end_headers()
            self.wfile.write(answer)
        except (BrokenPipeError, ConnectionResetError):
            pass

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()

def exact(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n-len(data))
        if not chunk:
            raise EOFError()
        data += chunk
    return data

def send(sock, op, ident, payload=b''):
    sock.sendall(header.pack(b'STPY', 4, 1, op, ident, len(payload), 0)+payload)

def receive(sock):
    magic, version, kind, op, ident, size, status = header.unpack(exact(sock, 24))
    assert (magic, version, kind) == (b'STPY', 4, 2)
    return status, exact(sock, size)

def connect(directory):
    sock = socket.socket(socket.AF_UNIX)
    sock.settimeout(12)
    sock.connect(str(directory/'service.sock'))
    send(sock, 1, 1, struct.pack('!II', 4, 4))
    status, snapshot = receive(sock)
    assert status == 0 and struct.unpack('!I', snapshot[24:28])[0] == 7
    return sock

def command(directory, op, payload):
    with connect(directory) as sock:
        send(sock, op, 2, payload)
        return receive(sock)

def submit(directory, key, session=1, prompt=b'Hello daemon'):
    return command(directory, 16, struct.pack('!Q', session) + key.encode() + prompt)

def wait_for(check, timeout=12):
    end = time.monotonic()+timeout
    while time.monotonic()<end:
        value = check()
        if value:
            return value
        time.sleep(.03)
    raise AssertionError('condition timed out')

with tempfile.TemporaryDirectory(prefix='strappy-run-', dir='/tmp') as temporary:
    directory = Path(temporary)
    dbpath = directory/'strappy.sqlite'
    def start():
        p = subprocess.Popen([binary, str(directory/'heartbeat.log'), str(dbpath), resources],
                             env={k:v for k,v in os.environ.items() if k not in ('APITOKEN', 'APIENDPOINT')})
        def ready():
            assert p.poll() is None, 'daemon startup failed'
            try:
                with connect(directory): return True
            except (OSError, EOFError, AssertionError): return False
        wait_for(ready)
        return p
    def state(key):
        with sqlite3.connect(dbpath) as db:
            row = db.execute('SELECT state FROM prompt_runs WHERE request_key=?', (key,)).fetchone()
            return row[0] if row else None
    process = start()
    try:
        with sqlite3.connect(dbpath) as db:
            endpoint = f'http://127.0.0.1:{server.server_port}/responses'
            db.execute("INSERT INTO provider_accounts(id,provider_id,display_name,responses_endpoint,created_at_ms,updated_at_ms) VALUES('daemon-offline','other','Fixture',?,1,1)", (endpoint,))
            db.execute("INSERT INTO models(id,provider_id,wire_model_id,name,last_seen_at_ms) VALUES('other:fixture','other','fixture','Fixture',1)")
            db.execute("INSERT INTO model_capabilities VALUES('other:fixture','unknown',0,1,0)")
            db.execute("INSERT INTO model_preferences(provider_id,wire_model_id,allowed,updated_at_ms) VALUES('other','fixture',1,1)")
            for session in (1,2):
                db.execute("INSERT INTO sessions(id,model_id,provider_account_id,created_at_ms,updated_at_ms) VALUES(?,'other:fixture','daemon-offline',1,1)", (session,))
                db.execute("INSERT INTO session_settings(session_id,web_provider,bash_enabled,updated_at_ms) VALUES(?,'none',1,1)", (session,))
                db.execute("INSERT INTO session_working_directories VALUES(?,?,1)", (session, str(directory)))
        # Lost acknowledgment: disconnect before reading; retry while request runs.
        key = 'a'*64
        with connect(directory) as sock:
            send(sock, 16, 2, struct.pack('!Q', 1)+key.encode()+b'Hello daemon')
        wait_for(lambda: state(key) in (1,2))
        wait_for(entered.is_set)
        subprocess.run([reader, str(dbpath), '2'], check=True)
        status, accepted = submit(directory, key)
        assert status == 0, status
        run, phase = struct.unpack('!QI', accepted)
        assert submit(directory, key, prompt=b'different')[0] == 8
        assert submit(directory, 'b'*64)[0] == 7
        assert submit(directory, 'b'*64, session=2)[0] == 7
        # UI lifetime is absent: no client remains while provider completes.
        gate.set()
        wait_for(lambda: state(key) == 3)
        assert struct.unpack('!QI', submit(directory, key)[1]) == (run, 3)
        assert len(requests) == 1
        subprocess.run([reader, str(dbpath), '3'], check=True)
        with sqlite3.connect(dbpath) as db:
            assert db.execute('SELECT COUNT(*) FROM conversation_items').fetchone()[0] > 0
            assert db.execute('PRAGMA user_version').fetchone()[0] == 1
        # Simultaneous clients: one accepted, the other busy, one provider call.
        gate.clear(); entered.clear()
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            outcomes = list(pool.map(lambda k: (k, submit(directory, k)), ['c'*64, 'd'*64]))
        assert sorted(r[0] for k,r in outcomes) == [0,7], outcomes
        key, (_, result) = next((k,r) for k,r in outcomes if r[0] == 0)
        run = struct.unpack('!QI', result)[0]
        wait_for(entered.is_set)
        then = time.monotonic()
        assert command(directory, 17, struct.pack('!QQ', 1, run))[0] == 0
        wait_for(lambda: state(key) == 5)
        assert time.monotonic()-then < 5
        gate.set()
        # Terminal duplicates survive daemon restart. Running work is interrupted.
        gate.clear(); entered.clear()
        key = 'e'*64
        assert submit(directory, key)[0] == 0
        wait_for(entered.is_set)
        process.kill(); process.wait(timeout=5)
        process = start()
        assert state(key) == 6
        subprocess.run([reader, str(dbpath), '6'], check=True)
        assert struct.unpack('!QI', submit(directory, key)[1])[1] == 6
        gate.set()
        # Cancellation while a real Bash child is running, without waiting for its timeout.
        mode = 'tool'; entered.clear(); gate.set()
        key = 'f'*64
        status, result = submit(directory, key, prompt=b'Run the fixture tool')
        assert status == 0
        run = struct.unpack('!QI', result)[0]
        wait_for(lambda: (directory/'tool.pid').exists())
        child = int((directory/'tool.pid').read_text())
        then = time.monotonic()
        assert command(directory, 17, struct.pack('!QQ', 1, run))[0] == 0
        wait_for(lambda: state(key) == 5)
        assert time.monotonic()-then < 5
        try:
            os.kill(child, 0)
            raise AssertionError('cancelled tool child survived')
        except ProcessLookupError:
            pass
        # Cooperative shutdown cancels the owned Bash process before exiting.
        (directory/'tool.pid').unlink()
        key = '7'*64
        assert submit(directory, key, prompt=b'Run another fixture tool')[0] == 0
        wait_for(lambda: (directory/'tool.pid').exists())
        child = int((directory/'tool.pid').read_text())
        process.send_signal(signal.SIGTERM)
        assert process.wait(timeout=5) == 0
        try:
            os.kill(child, 0)
            raise AssertionError('shutdown left a tool child running')
        except ProcessLookupError:
            pass
        process = start()
        assert state(key) == 6
        assert struct.unpack('!QI', submit(directory, key, prompt=b'Run another fixture tool')[1])[1] == 6
        mode = 'answer'
        assert submit(directory, '9'*64, session=999)[0] == 1
        assert submit(directory, 'x'*64)[0] == 1
        assert submit(directory, '9'*64, prompt=b'\xff')[0] == 1
        assert submit(directory, '9'*64, prompt=b'a\x00b')[0] == 1
        assert submit(directory, '9'*64, prompt=b'')[0] == 1
        print('execution_harness: durable acceptance, lost reply, reconnect, contention, HTTP/tool cancellation, restart interruption and history passed')
    finally:
        gate.set()
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=8)
server.shutdown()
