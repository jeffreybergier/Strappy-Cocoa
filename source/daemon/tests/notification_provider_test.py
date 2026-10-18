#!/usr/bin/env python3
"""Run the native macOS policy/IPC fixture without loading private frameworks."""
from pathlib import Path
import platform
import subprocess
import socket
import tempfile

root = Path(__file__).resolve().parents[3]
if platform.system() != "Darwin":
    raise SystemExit("This fixture requires macOS Foundation and clang.")
output = root / "build/tests/notification-provider/policy-test"
output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([
    "clang", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations",
    "-Wno-semicolon-before-method-body", "-framework", "Foundation",
    "-framework", "CoreFoundation",
    str(Path(__file__).with_suffix(".m")), "-o", str(output),
], check=True)
subprocess.run([str(output)], check=True)

# Readiness must distinguish a live receiver from a stale filesystem entry,
# and must not itself produce a datagram/notification.
with tempfile.TemporaryDirectory(prefix="strappy-ready-", dir="/tmp") as directory:
    endpoint = str(Path(directory) / "receiver.sock")
    sender = output.parent / "readiness-test"
    subprocess.run([
        "clang", "-std=c99", "-Wall", "-Wextra", "-Werror",
        '-DSTRAPPY_PROVIDER_TEST_SOCKET="' + endpoint + '"',
        str(Path(__file__).parent / "notification_provider_device/sender.c"),
        "-o", str(sender),
    ], check=True)
    def ready():
        return subprocess.run([str(sender), "ready"], capture_output=True).returncode
    assert ready() == 1  # absent
    receiver = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    receiver.bind(endpoint)
    receiver.close()
    assert ready() == 1  # stale pathname
    Path(endpoint).unlink()
    receiver = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    receiver.bind(endpoint)
    receiver.settimeout(0.1)
    assert ready() == 0
    try:
        receiver.recv(4096)
        raise AssertionError("Readiness check sent a datagram")
    except socket.timeout:
        pass
    subprocess.run([str(sender), "1"], check=True, capture_output=True)
    packet = receiver.recv(4096)
    assert len(packet) == 2030 and packet[:4] == b"STN1"
    receiver.close()
print("notification provider readiness: absent/stale/live/no-alert passed")
