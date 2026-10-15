#!/usr/bin/env python3
"""Build offline production-delegate tests: notifications_test.py [macOS|iOS].

Run the Mac bundle executable directly. Install the isolated iOS IPA only on
an authorized test phone and launch it; Documents/notifications-result.txt must
contain PASS. This recording test does not establish native banner display.
"""
import pathlib
import plistlib
import re
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[3]
platform = sys.argv[1] if len(sys.argv) > 1 else 'macOS'
if platform not in ('macOS', 'iOS'):
    raise SystemExit('Expected macOS or iOS')
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
if platform == 'iOS':
    (out / 'Entitlements.plist').write_bytes(plistlib.dumps({
        'application-identifier': identity, 'get-task-allow': True}))
    args += ['Strappy_ENTITLEMENTS=' + str(out / 'Entitlements.plist'),
             'Strappy_SANDBOX=container', 'Strappy_INSTALL=', 'Strappy_PACKAGES=ipa']
subprocess.run(args, check=True)
