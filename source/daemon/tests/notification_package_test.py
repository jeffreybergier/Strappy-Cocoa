#!/usr/bin/env python3
"""Check provider migration preflight and built DEB ownership boundaries."""
from pathlib import Path
import io
import os
import plistlib
import subprocess
import sys
import tarfile
import tempfile

root = Path(__file__).resolve().parents[3]
lifecycle = root / "source/iOS/daemon/lifecycle.sh.in"
with tempfile.TemporaryDirectory(prefix="strappy-package-") as temporary:
    directory = Path(temporary)
    query = directory / "dpkg-query"
    query.write_text('#!/bin/sh\nprintf "%s" "$TEST_STATUS"\nexit "$TEST_EXIT"\n')
    query.chmod(0o700)
    for status, exit_code, allowed in (
        ("", "1", True),
        ("", "2", False),
        ("deinstall ok config-files", "0", True),
        ("unknown ok not-installed", "0", True),
        ("install ok installed", "0", False),
        ("install ok unpacked", "0", False),
        ("install reinstreq half-installed", "0", False),
        ("deinstall ok installed", "0", False),
    ):
        environment = dict(os.environ, PATH=temporary,
                           TEST_STATUS=status, TEST_EXIT=exit_code)
        result = subprocess.run(
            ["/bin/sh", "-c", '. "$1"; strappy_provider_preflight',
             "test", str(lifecycle)], env=environment, capture_output=True)
        assert (result.returncode == 0) == allowed, (status, result.stderr)
    query.unlink()
    result = subprocess.run(
        ["/bin/sh", "-c", '. "$1"; strappy_provider_preflight',
         "test", str(lifecycle)], env=dict(os.environ, PATH=temporary),
        capture_output=True)
    assert result.returncode != 0
print("provider package preflight: passed")

for filename in sys.argv[1:]:
    archive = Path(filename)
    payload = subprocess.check_output(["dpkg-deb", "--fsys-tarfile", str(archive)])
    with tarfile.open(fileobj=io.BytesIO(payload)) as package:
        names = package.getnames()
        executables = [name for name in names if name.endswith(
            "/StrappyNotificationProvider.bundle/StrappyNotificationProvider")]
        assert len(executables) == 1
        binary = package.extractfile(executables[0]).read()
        assert b"/receiver.sock\0" in binary
        assert b"provider-candidate.sock" not in binary
        assert b"test badge" not in binary
        assert not any("StrappyBulletin" in name for name in names)
        assert not any("strappy-provider-test" in name for name in names)
        info_name = next(name for name in names if name.endswith(
            "/StrappyNotificationProvider.bundle/Info.plist"))
        info = plistlib.loads(package.extractfile(info_name).read())
        assert info["MinimumOSVersion"] == "5.0"
    control = subprocess.check_output(["dpkg-deb", "--ctrl-tarfile", str(archive)])
    with tarfile.open(fileobj=io.BytesIO(control)) as package:
        preinst = next(name for name in package.getnames()
                       if name.removeprefix("./") == "preinst")
        script = package.extractfile(preinst).read().decode()
        installation = script[script.index('case "$1" in\n    install|upgrade)'):]
        assert installation.index("strappy_provider_preflight") < installation.index(
            "strappy_service stop")
    print(f"provider package payload and lifecycle: {archive.name} passed")
