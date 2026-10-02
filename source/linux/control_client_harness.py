"""Count connections/handshakes, enforce monotonically ordered requests, lose a reply."""
import socket
import struct
import subprocess
import sys
import tempfile
import threading
from pathlib import Path
header = struct.Struct('!4sHHIIII')
errors, mutation_connections = [], []
count = 0
lock = threading.Lock()
refresh_entered, control_seen = threading.Event(), threading.Event()
def exact(s, n):
    data = b''
    while len(data) < n:
        part = s.recv(n-len(data))
        if not part: raise EOFError
        data += part
    return data
with tempfile.TemporaryDirectory(prefix='strappy-control-',dir='/tmp') as tmp:
    path = str(Path(tmp)/'service.sock')
    server = socket.socket(socket.AF_UNIX)
    server.bind(path)
    server.listen()
    def peer(s, identity):
        global count
        previous = 0
        snapshot = bytes(24)+struct.pack('!III',7,4096,2)
        try:
            with s:
                while True:
                    magic,version,kind,op,ident,size,status = header.unpack(exact(s,24))
                    payload = exact(s,size)
                    assert magic == b'STPY' and kind == 1 and status == 0
                    assert ident > previous
                    assert (previous != 0) or op == 1
                    previous = ident
                    if op == 29:
                        refresh_entered.set()
                        assert control_seen.wait(3), 'refresh blocked the control FIFO'
                        response = b'ok'
                    elif op == 25:
                        assert refresh_entered.wait(3), 'missing independent refresh'
                        control_seen.set()
                        with lock:
                            count += 1
                            mutation_connections.append(identity)
                            if count == 33: return
                        response = b'ok'
                    elif op == 26: response = bytes(12)
                    else: response = snapshot
                    s.sendall(header.pack(magic,version,2,op,ident,len(response),0)+response)
        except EOFError: pass
        except Exception as error: errors.append(error)
    def accept():
        identity = 0
        while True:
            try: s,_ = server.accept()
            except OSError: return
            identity += 1
            threading.Thread(target=peer,args=(s,identity),daemon=True).start()
    threading.Thread(target=accept,daemon=True).start()
    subprocess.run([sys.argv[1],path,str(Path(tmp)/'unused.sqlite')],check=True,timeout=15)
    assert not errors, errors
    assert count == 34
    assert len(set(mutation_connections[:33])) == 1
    assert mutation_connections[33] != mutation_connections[32]
    server.close()
