#!/usr/bin/env python3
"""Build offline macOS production-delegate tests.

Run the Mac bundle executable directly. iOS daemon notifications are covered
by execution_harness.py; visible SpringBoard delivery requires a native test.
"""
import pathlib
import plistlib
import re
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[3]
platform = sys.argv[1] if len(sys.argv) > 1 else 'macOS'
if platform != 'macOS':
    raise SystemExit('Use make -C source/daemon/tests execution-test for iOS daemon notification coverage')
out = root / 'build/tests/notifications' / platform
out.mkdir(parents=True, exist_ok=True)
identity = 'com.altivecintelligence.StrappyNotificationsTest'
plist = plistlib.loads((root / 'source' / platform / 'Info.plist').read_bytes())
plist.update(CFBundleIdentifier=identity, CFBundleDisplayName='Notifications Test')
(out / 'Info.plist').write_bytes(plistlib.dumps(plist))
makefile = (root / 'source' / platform / 'Makefile').read_text().replace('\\\n', ' ')
sources = re.search(r'^GROUP_ui_SOURCES = (.*)$', makefile, re.M).group(1).split()
sources.remove('main.m')
sources.append(str(root / 'source/daemon/tests/notifications_test.m'))
args = [
    'make', '-C', str(root / 'source' / platform), 'release',
    'PRODUCTS=Strappy', 'Strappy_EMBED=', 'BUILD_ROOT=' + str(out / 'output'),
    'GROUP_ui_SOURCES=' + ' '.join(sources),
    'INFO_PLIST=' + str(out / 'Info.plist'),
]
subprocess.run(args, check=True)
