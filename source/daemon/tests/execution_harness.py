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
resources = str(Path(os.environ['STRAPPY_TEST_RESOURCE_DIR']).resolve())
reader = str((Path(os.environ['STRAPPY_TEST_BUILD_DIR']) / 'run_reader_harness').resolve())
header = struct.Struct('!4sHHIIII')
gate = threading.Event()
entered = threading.Event()
requests = []
mode = 'answer'
answer_text = 'Daemon fixture completed.'

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
                       'content': [{'type': 'output_text', 'text': answer_text, 'annotations': []}]}]
        answer = json.dumps({'id': f'response-fixture-{number}', 'object': 'response', 'status': 'completed', 'output': output}).encode()
        if mode == 'error':
            answer = json.dumps({'error': {'message': 'private provider diagnostic',
                                          'type': 'invalid_request_error'}}).encode()
        try:
            self.send_response(400 if mode == 'error' else 200)
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
    sock.sendall(header.pack(b'STPY', 8, 1, op, ident, len(payload), 0)+payload)

def receive(sock):
    magic, version, kind, op, ident, size, status = header.unpack(exact(sock, 24))
    assert (magic, version, kind) == (b'STPY', 8, 2)
    return status, exact(sock, size)

def connect(directory):
    sock = socket.socket(socket.AF_UNIX)
    sock.settimeout(12)
    sock.connect(str(directory/'service.sock'))
    send(sock, 1, 1, struct.pack('!II', 8, 8))
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
    notification_path = directory/'notifications.sock'
    notifications = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    notifications.bind(str(notification_path))
    notifications.settimeout(2)
    badges = []
    def receive_alert(timeout):
        end = time.monotonic() + timeout
        while True:
            notifications.settimeout(max(.001,end-time.monotonic()))
            data, address = notifications.recvfrom(4096)
            if data[:4] != b'STB1':
                return data
            assert len(data) == 52
            revision, count = (int(x.split(b'\0')[0]) for x in struct.unpack('24s24s',data[4:]))
            badges.append((revision,count))
            notifications.sendto(b'STA1'+data[4:],address)
            if time.monotonic() >= end:
                raise socket.timeout()
    def notification():
        data = receive_alert(2)
        assert len(data) == 2030
        magic, run_id, title, body = struct.unpack('4s24s401s1601s', data)
        assert magic == b'STN1'
        return (int(run_id.split(b'\0', 1)[0]),
                title.split(b'\0', 1)[0].decode(), body.split(b'\0', 1)[0].decode())
    def no_notification():
        notifications.settimeout(.15)
        try:
            receive_alert(.15)
            raise AssertionError('unexpected completion notification')
        except socket.timeout:
            pass
        finally:
            notifications.settimeout(2)
    def start():
        p = subprocess.Popen([binary, str(directory/'heartbeat.log'), str(dbpath), resources],
                             env=dict({k:v for k,v in os.environ.items() if k not in ('APITOKEN', 'APIENDPOINT')},
                                      STRAPPY_TEST_NOTIFICATION_SOCKET=str(notification_path)))
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
            db.execute("UPDATE app_preferences SET default_model_id='other:fixture',default_provider_account_id='daemon-offline' WHERE id=1")
            for session in (1,2):
                db.execute("INSERT INTO sessions(id,model_id,provider_account_id,created_at_ms,updated_at_ms) VALUES(?,'other:fixture','daemon-offline',1,1)", (session,))
                db.execute("INSERT INTO session_settings(session_id,web_provider,bash_enabled,updated_at_ms) VALUES(?,'none',1,1)", (session,))
                db.execute("INSERT INTO session_working_directories VALUES(?,?,1)", (session, str(directory)))
        settings = {'action':'options','session':'1','mask':'256','rounds':'7',
                    'web':'0','bash':'1','one_tool':'0','quality':'0','web_provider':'none'}
        def combined(key, values=settings, prompt=b'Hello daemon'):
            encoded = json.dumps(values).encode()
            return struct.pack('!Q',1)+key.encode()+struct.pack('!I',len(encoded))+encoded+prompt
        # Credential acceptance is short; waits stay in per-account daemon jobs.
        def auth(**values):
            status, data = command(directory,30,json.dumps(values).encode())
            assert status == 0
            return json.loads(data)
        started = time.monotonic()
        auth(action='credential_start',credential_action='refresh_oauth',id='daemon-offline',identity='slow-fixture',key='a'*64)
        assert time.monotonic()-started < 1
        auth(action='credential_start',credential_action='credential_status',id='daemon-offline',key='b'*64)
        assert auth(action='auth_status',id='daemon-offline',key='b'*64)['state'] == 'queued'
        started = time.monotonic()
        status, other = command(directory,25,b'{"action":"create_account","id":"other","name":"Concurrent control"}')
        assert status == 0 and time.monotonic()-started < 1
        auth(action='credential_start',credential_action='credential_status',id=other.decode(),key='c'*64)
        wait_for(lambda: auth(action='auth_status',id=other.decode(),key='c'*64)['state'] == 'completed')
        assert time.monotonic()-started < 1
        wait_for(lambda: auth(action='auth_status',id='daemon-offline',key='b'*64)['state'] == 'completed')
        assert auth(action='auth_status',id='daemon-offline',key='a'*64)['result'] == 'fixture'
        assert command(directory,29,b'{"action":"refresh_oauth","id":"daemon-offline"}')[0] == 1
        # An acceptance failure rolls the settings back in the same transaction.
        with sqlite3.connect(dbpath) as db:
            previous_rounds = db.execute('SELECT round_limit FROM session_settings WHERE session_id=1').fetchone()[0]
            db.execute("CREATE TRIGGER reject_accept BEFORE INSERT ON prompt_runs BEGIN SELECT RAISE(ABORT,'fixture'); END")
        assert command(directory,28,combined('f'*64))[0] == 9
        with sqlite3.connect(dbpath) as db:
            assert db.execute('SELECT round_limit FROM session_settings WHERE session_id=1').fetchone()[0] == previous_rounds
            assert db.execute('SELECT count(*) FROM prompt_runs').fetchone()[0] == 0
            db.execute('DROP TRIGGER reject_accept')
        invalid = dict(settings,rounds='0')
        assert command(directory,28,combined('f'*64,invalid))[0] == 1
        # Unicode previews must end on a scalar boundary, and the title comes
        # from the committed rename performed while the request is running.
        answer_text = 'Tokyo news ' + '漢😀é' * 200
        # Lost acknowledgment: disconnect before reading; retry while request runs.
        key = 'a'*64
        with connect(directory) as sock:
            send(sock, 28, 2, combined(key))
        wait_for(lambda: state(key) in (1,2))
        wait_for(entered.is_set)
        with sqlite3.connect(dbpath) as db:
            assert db.execute('SELECT round_limit FROM session_settings WHERE session_id=1').fetchone()[0] == 7
        # A retry may not overwrite the settings captured by the accepted run.
        assert command(directory,28,combined(key,dict(settings,rounds='9')))[0] == 0
        with sqlite3.connect(dbpath) as db:
            assert db.execute('SELECT round_limit FROM session_settings WHERE session_id=1').fetchone()[0] == 7
        subprocess.run([reader, str(dbpath), '2'], check=True)
        status, accepted = submit(directory, key)
        assert status == 0, status
        run, phase = struct.unpack('!QI', accepted)
        assert submit(directory, key, prompt=b'different')[0] == 8
        assert submit(directory, 'b'*64)[0] == 7
        assert submit(directory, 'b'*64, session=2)[0] == 0
        wait_for(lambda: len(requests) == 2)
        # A blocked provider must not block committed session mutations.
        start_control = time.monotonic()
        status, created = command(directory,25,json.dumps({'action':'create_session','directory':str(directory)}).encode())
        assert status == 0
        assert command(directory,25,json.dumps({'action':'delete_session','session':created.decode()}).encode())[0] == 0
        assert command(directory,25,b'{"action":"delete_session","session":"1"}')[0] == 7
        assert time.monotonic()-start_control < 2
        # Cancelling one session leaves the other running.
        second_run = struct.unpack('!QI',submit(directory,'b'*64,session=2)[1])[0]
        assert command(directory,17,struct.pack('!QQ',2,second_run))[0] == 0
        wait_for(lambda: state('b'*64) == 5)
        assert state(key) == 2
        no_notification()  # Cancelled prompts do not notify.
        with sqlite3.connect(dbpath) as db:
            db.execute("UPDATE sessions SET name='Tokyo News Summary Report' WHERE id=1")
        # UI lifetime is absent: no client remains while provider completes.
        gate.set()
        wait_for(lambda: state(key) == 3)
        assert struct.unpack('!QI', submit(directory, key)[1]) == (run, 3)
        assert len(requests) == 2
        assert notification() == (run, 'Tokyo News Summary Report', answer_text[:400])
        no_notification()  # Durable submission retries must not redisplay.
        answer_text = 'Daemon fixture completed.'
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
        # Simulate a scan that was durably started before the crash too.
        with sqlite3.connect(dbpath) as db:
            db.execute("INSERT INTO scan_roots(id,path) VALUES(999,'/fixture-recovery')")
            db.execute("INSERT INTO scan_runs(scan_root_id,state,started_at_ms) VALUES(999,'running',1)")
            db.execute("CREATE TRIGGER reject_recovery BEFORE UPDATE ON scan_runs BEGIN SELECT RAISE(ABORT,'fixture'); END")
        failed = subprocess.Popen([binary, str(directory/'heartbeat.log'), str(dbpath), resources])
        assert failed.wait(timeout=5) != 0
        assert state(key) == 2, 'failed reconciliation must roll back and refuse readiness'
        with sqlite3.connect(dbpath) as db:
            assert db.execute("SELECT COUNT(*) FROM http_attempts WHERE state='running'").fetchone()[0] > 0
            db.execute("DROP TRIGGER reject_recovery")
        process = start()
        assert state(key) == 6
        with sqlite3.connect(dbpath) as db:
            assert db.execute("SELECT COUNT(*) FROM http_attempts WHERE state IN ('pending','running')").fetchone()[0] == 0
            assert db.execute("SELECT COUNT(*) FROM model_requests WHERE state IN ('pending','running')").fetchone()[0] == 0
            assert db.execute("SELECT COUNT(*) FROM turns WHERE state='running'").fetchone()[0] == 0
            assert db.execute("SELECT state FROM scan_runs WHERE scan_root_id=999").fetchone()[0] == 'error'
            assert db.execute("SELECT COUNT(*) FROM http_attempts WHERE transport_error LIKE 'Service interrupted%'").fetchone()[0] == 1
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
        # Abrupt owner death: the guardian stops the tool group without replay.
        (directory/'tool.pid').unlink()
        key = '6'*64
        assert submit(directory, key, prompt=b'Crash during fixture tool')[0] == 0
        wait_for(lambda: (directory/'tool.pid').exists())
        child = int((directory/'tool.pid').read_text())
        process.kill(); process.wait(timeout=5)
        def child_stopped():
            try:
                # Containers may have a PID 1 that does not promptly reap orphans.
                return Path(f'/proc/{child}/stat').read_text().split(') ')[1][0] == 'Z'
            except FileNotFoundError:
                return True
        wait_for(child_stopped, timeout=5)
        process = start()
        assert state(key) == 6
        with sqlite3.connect(dbpath) as db:
            assert db.execute("SELECT COUNT(*) FROM tool_executions WHERE state IN ('pending','running')").fetchone()[0] == 0
            assert db.execute("SELECT COUNT(*) FROM tool_executions WHERE error_code='service_interrupted'").fetchone()[0] == 1
        with sqlite3.connect(dbpath) as db:
            assert db.execute("SELECT COUNT(*) FROM function_outputs WHERE error_code='service_interrupted'").fetchone()[0] == 1
            assert db.execute("SELECT COUNT(*) FROM function_calls f WHERE NOT EXISTS(SELECT 1 FROM function_outputs o WHERE o.function_call_item_id=f.item_id)").fetchone()[0] == 0
        count = len(requests)
        assert struct.unpack('!QI', submit(directory, key, prompt=b'Crash during fixture tool')[1])[1] == 6
        assert len(requests) == count
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
        # Large submissions use independent prompt/options byte limits. Exercise
        # real framing, persistence and provider delivery, not just allocation.
        maximum = 1024 * 1024
        text = ('漢' * (maximum // 3) + 'x' * (maximum % 3)).encode()
        assert len(text) == maximum
        sid = int(command(directory,25,json.dumps({'action':'create_session',
            'directory':str(directory)}).encode())[1])
        large_settings = dict(settings,session=str(sid))
        options = json.dumps(large_settings).encode().ljust(4096,b' ')
        large_key = 'b0' * 32
        payload = struct.pack('!Q',sid)+large_key.encode()+struct.pack('!I',len(options))+options+text
        def no_large_run():
            with sqlite3.connect(dbpath) as db:
                return db.execute('SELECT count(*) FROM prompt_runs WHERE session_id=?',(sid,)).fetchone()[0] == 0
        # Neither an incomplete options block nor an incomplete prompt may
        # commit settings or a run. Keep the first peer open while probing.
        for cut in (80, len(payload)-1):
            with connect(directory) as sock:
                sock.sendall(header.pack(b'STPY',8,1,28,2,len(payload),0)+payload[:cut])
                assert command(directory,26,b'')[0] == 0
                assert no_large_run()
                with sqlite3.connect(dbpath) as db:
                    assert db.execute('SELECT round_limit FROM session_settings WHERE session_id=?',(sid,)).fetchone()[0] != 7
        # Independent limits reject oversized options or text even when the
        # combined frame fits. Invalid UTF-8 and NUL near the end also fail.
        prefix = struct.pack('!Q',sid)+large_key.encode()
        for bad in (prefix+struct.pack('!I',4097)+options+b' '+b'x',
                    prefix+struct.pack('!I',0)+text+b'x',
                    prefix+struct.pack('!I',0)+text[:-1]+b'\xff',
                    prefix+struct.pack('!I',0)+text[:-1]+b'\0'):
            assert command(directory,28,bad)[0] == 1
            assert no_large_run()
        # Reject oversized declared frames using the header alone, including
        # ordinary control messages that still have their 4 KiB cap.
        for op, size in ((16,maximum+73),(28,maximum+4096+77),(25,4097),(28,0xffffffff)):
            with connect(directory) as sock:
                sock.sendall(header.pack(b'STPY',8,1,op,2,size,0))
                assert sock.recv(1) == b''
        assert no_large_run()
        # Split a maximum-sized options-bearing submission across many writes,
        # including multibyte characters. Lose its acknowledgment and retry.
        before = len(requests)
        with connect(directory) as sock:
            sock.sendall(header.pack(b'STPY',8,1,28,2,len(payload),0))
            for offset in range(0,len(payload),8191):
                sock.sendall(payload[offset:offset+8191])
        wait_for(lambda: state(large_key) == 3)
        assert command(directory,28,payload)[0] == 0
        assert len(requests) == before+1
        assert any(part.get('text') == text.decode() for item in requests[-1]['input']
                   for part in item.get('content',[]) if isinstance(part,dict))
        with sqlite3.connect(dbpath) as db:
            assert db.execute('SELECT prompt FROM prompt_runs WHERE request_key=?',(large_key,)).fetchone()[0].encode() == text
            assert db.execute('SELECT round_limit FROM session_settings WHERE session_id=?',(sid,)).fetchone()[0] == 7
            assert db.execute('SELECT count(*) FROM prompt_runs WHERE session_id=?',(sid,)).fetchone()[0] == 1
        # The plain submission operation also supports the exact text boundary;
        # reuse a connection afterward to check large-buffer ownership/reset.
        plain_key = 'b1' * 32
        with connect(directory) as sock:
            send(sock,16,2,struct.pack('!Q',sid)+plain_key.encode()+text)
            assert receive(sock)[0] == 0
            send(sock,26,3)
            assert receive(sock)[0] == 0
        wait_for(lambda: state(plain_key) == 3)
        assert len(requests) == before+2
        # Drain the two large-prompt completions, then verify an empty final
        # answer cannot reuse an older reply and errors never echo provider text.
        notification(); notification(); no_notification()
        answer_text = ''
        empty_key = 'b2' * 32
        assert submit(directory, empty_key)[0] == 0
        wait_for(lambda: state(empty_key) == 3)
        assert notification()[1:] == ('Tokyo News Summary Report', '')
        mode = 'error'
        error_key = 'b3' * 32
        assert submit(directory, error_key)[0] == 0
        wait_for(lambda: state(error_key) == 4)
        assert notification()[1:] == ('Tokyo News Summary Report', 'Error')
        assert submit(directory, error_key)[0] == 0
        no_notification()
        # Known credential errors use application-owned wording, not raw text.
        with sqlite3.connect(dbpath) as db:
            db.execute("INSERT INTO provider_accounts(id,provider_id,display_name,responses_endpoint,created_at_ms,updated_at_ms) VALUES('missing-credentials-fixture','openrouter','Missing',?,1,1)", (endpoint,))
            db.execute("INSERT INTO models(id,provider_id,wire_model_id,name,last_seen_at_ms) VALUES('openrouter:notification-fixture','openrouter','notification-fixture','Fixture',1)")
            db.execute("INSERT INTO model_preferences(provider_id,wire_model_id,allowed,updated_at_ms) VALUES('openrouter','notification-fixture',1,1)")
            db.execute("UPDATE sessions SET model_id='openrouter:notification-fixture',provider_account_id='missing-credentials-fixture' WHERE id=1")
        credential_key = 'b5' * 32
        assert submit(directory, credential_key)[0] == 0
        wait_for(lambda: state(credential_key) == 4)
        assert notification()[1:] == ('Tokyo News Summary Report',
            'Error: Add credentials for the selected account and try again.')
        with sqlite3.connect(dbpath) as db:
            db.execute("UPDATE sessions SET model_id='other:fixture',provider_account_id='daemon-offline' WHERE id=1")
        # A missing receiver does not fail, block, or replay a completed prompt.
        notifications.close(); notification_path.unlink()
        mode = 'answer'; answer_text = 'Receiver unavailable fixture.'
        absent_key = 'b4' * 32
        assert submit(directory, absent_key)[0] == 0
        wait_for(lambda: state(absent_key) == 3)
        print('execution_harness: notification previews, errors, cancellation, retries and missing receiver passed')
        print('execution_harness: 1 MiB UTF-8 prompts, independent options limits, partial transfers, durable large retries passed')
        print('execution_harness: durable acceptance, lost reply, reconnect, contention, HTTP/tool cancellation, restart interruption and history passed')
    finally:
        notifications.close()
        gate.set()
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=8)
server.shutdown()
