"""Step 4 commands against the real daemon, DB, scanner and loopback provider."""
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
header = struct.Struct('!4sHHIIII')
gate, entered = threading.Event(), threading.Event()
gate.set()
study_database = None

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def answer(self, value):
        entered.set()
        gate.wait(15)
        data = json.dumps(value).encode()
        try:
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError): pass
    def do_GET(self):
        self.answer({'data': [{'id': 'fixture/catalog', 'name': 'Fixture Catalog'}]})
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        if study_database:
            calls = [item.get('name') for item in body.get('input', [])]
            stages = [('database_context', {'database_id': study_database}),
                      ('database_query', {'database_id': study_database, 'sql': 'SELECT value FROM fixture LIMIT 1'}),
                      ('database_study', {'database_id': study_database, 'description': 'Fixture database.',
                                          'context': 'Contains the fixture test table.'})]
            for name, arguments in stages:
                if name not in calls:
                    self.answer({'id': 'fixture-'+name, 'object': 'response', 'status': 'completed', 'output':
                                 [{'type': 'function_call', 'id': name, 'call_id': name,
                                   'name': name, 'arguments': json.dumps(arguments)}]})
                    return
        self.answer({'id': 'fixture-study', 'object': 'response', 'status': 'completed',
                     'output': [{'type': 'message', 'role': 'assistant', 'content':
                                 [{'type': 'output_text', 'text': 'No study progress.', 'annotations': []}]}]})

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()

def exact(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n-len(data))
        if not chunk: raise EOFError()
        data += chunk
    return data

def exchange(sock, op, ident, payload=b''):
    sock.sendall(header.pack(b'STPY', 5, 1, op, ident, len(payload), 0)+payload)
    magic, version, kind, reply_op, reply_id, size, status = header.unpack(exact(sock, 24))
    assert (magic, version, kind, reply_op, reply_id) == (b'STPY', 5, 2, op, ident)
    return status, exact(sock, size)

def command(directory, op, payload=b''):
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(5)
        sock.connect(str(directory/'service.sock'))
        status, _ = exchange(sock, 1, 1, struct.pack('!II', 5, 5))
        assert status == 0
        return exchange(sock, op, 2, payload)

def wait(check, timeout=12):
    end = time.monotonic()+timeout
    while time.monotonic() < end:
        if check(): return
        time.sleep(.03)
    raise AssertionError('timed out')

with tempfile.TemporaryDirectory(prefix='strappy-mutations-', dir='/tmp') as temporary:
    directory = Path(temporary)
    dbpath = directory/'strappy.sqlite'
    def start():
        process = subprocess.Popen([binary, str(directory/'log'), str(dbpath), resources])
        def ready():
            assert process.poll() is None
            try: return command(directory,26)[0] == 0
            except (OSError, EOFError): return False
        wait(ready)
        return process
    def rpc(action=None, op=25, expected=0, **values):
        if action: values['action'] = action
        status, data = command(directory,op,json.dumps({k:str(v) for k,v in values.items()}).encode())
        assert status == expected, (action, status, data)
        return data.decode() if status == 0 else None
    def scalar(sql, args=()):
        with sqlite3.connect(f'file:{dbpath}?mode=ro', uri=True) as db:
            return db.execute(sql,args).fetchone()[0]
    def idle(): return struct.unpack('!III',command(directory,26)[1])[0] == 0
    process = start()
    try:
        account = rpc('create_account', id='other', name='Offline')
        def auth_rpc(op, **values):
            return command(directory,op,json.dumps(values).encode())
        status, payload = auth_rpc(30,action='credential_status',id=account)
        assert status == 0 and json.loads(payload) == dict(present=False,identity='',needs_refresh=False)
        assert auth_rpc(30,action='credential_status',id='missing')[0] == 1
        assert auth_rpc(25,action='save_oauth',id=account,access='should-not-save',
                        refresh='should-not-save',identity='fixture',expiry='1')[0] == 1
        assert auth_rpc(30,action='auth_start',id=account,key='a'*64)[0] == 1
        rpc('update_account',id=account,name='Updated',endpoint=f'http://127.0.0.1:{server.server_port}/responses',output=4096)
        model = rpc('create_model',id='other',wire='fixture',name='Fixture',context=8192,output=4096,
                    reasoning=0,image=0,functions=1,price_input='',price_output='',price_read='',price_write='')
        rpc('approve_model',id=model,value=1)
        rpc('default_model',id=model)
        options = dict(mask=1023,model=model,account=account,assistant='world_knowledge',directory=directory,
                       fallback=directory,rounds=3,web_provider='none',web=0,bash=0,one_tool=0,quality=0)
        rpc('defaults',**options)
        session = int(rpc('create_session',directory=directory))
        # Settings must not invalidate the ledger or catalog. Subscribe after
        # earlier commands drain, then verify the complete notification burst.
        time.sleep(.05)
        with socket.socket(socket.AF_UNIX) as subscriber:
            subscriber.settimeout(2)
            subscriber.connect(str(directory/'service.sock'))
            assert exchange(subscriber, 1, 1, struct.pack('!II', 5, 5))[0] == 0
            assert exchange(subscriber, 3, 2)[0] == 0
            rpc('options',session=session,**options)
            magic, version, kind, topic, ident, size, status = header.unpack(exact(subscriber, 24))
            assert (magic, version, kind, topic, status) == (b'STPY', 5, 3, 32, 0)
            exact(subscriber, size)
            subscriber.settimeout(.15)
            try:
                extra = subscriber.recv(1)
                raise AssertionError(('unexpected settings invalidation', extra))
            except socket.timeout:
                pass
            rpc('approve_model',id=model,value=1)
            subscriber.settimeout(2)
            magic, version, kind, topic, ident, size, status = header.unpack(exact(subscriber, 24))
            assert (kind, topic, status) == (3, 34, 0)
            exact(subscriber, size)
            subscriber.settimeout(.15)
            try:
                extra = subscriber.recv(1)
                raise AssertionError(('unexpected catalog invalidation', extra))
            except socket.timeout:
                pass
        assert scalar('SELECT provider_account_id FROM sessions WHERE id=?',(session,)) == account
        rpc('options',session=session,expected=1,**dict(options,rounds=4294967297))
        rpc('approve_model',id=model,value=4294967297,expected=1)
        for payload in (b'[]', b'{"action":"reset_study","action":"delete_session"}',
                        b'{"action":false}', b'{"action":"reset_study"} trailing',
                        b'{"action":"reset_study\\u0000x"}', b'{"action":{}}'):
            assert command(directory,25,payload)[0] == 1, payload
        assert command(directory,26,b'x')[0] == 1
        assert command(directory,20,b'{"action":"reset_study"}')[0] == 1
        root = directory/'scan'; root.mkdir()
        with sqlite3.connect(root/'fixture.sqlite') as db:
            db.execute('CREATE TABLE fixture(value TEXT)')
        rpc(op=23,path=root,quick=0)
        wait(idle)
        assert struct.unpack('!III',command(directory,26)[1])[1:] == (23,3)
        catalog = scalar('SELECT database_id FROM database_locations WHERE path=?',(str(root/'fixture.sqlite'),))
        rpc('approve_database',id=catalog,value=1)
        rpc('hide_database',id=catalog,value=1)
        rpc('hide_database',id=catalog,value=0)
        # A disconnected submitting client leaves study running; mutations cannot race it.
        gate.clear(); entered.clear()
        study = int(rpc(op=24,directory=directory,key='a'*64))
        cancelled_study = study
        wait(entered.is_set)
        assert scalar('SELECT state FROM prompt_runs WHERE session_id=?',(study,)) == 2
        rpc('delete_session',session=study,expected=7)
        rpc('approve_database',id=catalog,value=1)
        run = scalar('SELECT id FROM prompt_runs WHERE session_id=?',(study,))
        assert command(directory,17,struct.pack('!QQ',study,run))[0] == 0
        wait(idle)
        assert scalar('SELECT state FROM prompt_runs WHERE id=?',(run,)) == 5
        gate.set()
        # No-progress termination preserves a truthful failed run after three batches.
        study = int(rpc(op=24,directory=directory,key='b'*64))
        wait(idle)
        assert scalar('SELECT state FROM prompt_runs WHERE session_id=?',(study,)) == 4
        study_database = scalar('SELECT assistant_database_id FROM databases WHERE id=?',(catalog,))
        study = int(rpc(op=24,directory=directory,key='c'*64))
        wait(idle)
        assert scalar('SELECT state FROM prompt_runs WHERE session_id=?',(study,)) == 3
        rpc('delete_study',id=study_database)
        rpc('reset_study')
        router = rpc('create_account',id='openrouter',name='Offline catalog')
        # OpenRouter endpoint validation normally pins HTTPS. This isolated fixture
        # redirects only its synthetic account to the local test HTTP server.
        with sqlite3.connect(dbpath) as db:
            db.execute('UPDATE provider_accounts SET responses_endpoint=? WHERE id=?',
                       (f'http://127.0.0.1:{server.server_port}/responses',router))
        rpc(op=27,id=router)
        wait(idle)
        assert struct.unpack('!III',command(directory,26)[1])[1:] == (27,3)
        assert scalar("SELECT count(*) FROM models WHERE wire_model_id='fixture/catalog'") == 1
        gate.clear(); entered.clear()
        rpc(op=27,id=router)
        wait(entered.is_set)
        # Durable retries still resolve while a different kind of worker is busy.
        retry = struct.pack('!Q',cancelled_study) + b'a'*64 + b'Database Study'
        status, result = command(directory,16,retry)
        assert status == 0 and struct.unpack('!QI',result)[1] == 5
        assert command(directory,16,struct.pack('!Q',cancelled_study)+b'd'*64+b'New work')[0] == 0
        then = time.monotonic()
        process.send_signal(signal.SIGTERM)
        assert process.wait(timeout=5) == 0
        assert time.monotonic()-then < 5
        gate.set()
        process = start()
        assert struct.unpack('!III',command(directory,26)[1]) == (0,0,0)
        rpc('delete_session',session=session)
        rpc('archive_model',id='other',wire='fixture')
        rpc('archive_account',id=account)
        assert scalar('SELECT lifecycle_state FROM provider_accounts WHERE id=?',(account,)) == 'archived'
        assert scalar('PRAGMA user_version') == 1
        print('mutation_harness: accounts, models, defaults, sessions, malformed commands, scan, study cancellation/no-progress, catalog refresh/shutdown and reconnect passed')
    finally:
        gate.set()
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=8)
server.shutdown()
