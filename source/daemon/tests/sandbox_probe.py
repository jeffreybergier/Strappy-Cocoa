#!/usr/bin/env python3
"""Build isolated iOS sandbox experiments using the production build rules.

Usage: sandbox_probe.py [baseline|read-only|read-write] [release|analyze|validate-package]
No installation, daemon mutation, or provider requests are performed.
"""
import argparse
import pathlib
import plistlib
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('variant', choices=('baseline', 'read-only', 'read-write'))
parser.add_argument('target', nargs='?', default='release',
                    choices=('release', 'analyze', 'validate-package'))
parser.add_argument('--ipc-version', type=int, choices=(5, 8), default=8,
                    help='5 tests the older daemon on koolphone5')
parser.add_argument('--seatbelt', action='store_true',
                    help='explicitly request the iOS container seatbelt profile')
parser.add_argument('--self-sandbox', action='store_true',
                    help='diagnostic: apply the built-in container profile in process')
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[3]
out = root / 'build/experiments/ios-sandbox' / args.variant
if args.ipc_version != 8:
    out = out / ('ipc' + str(args.ipc_version))
if args.seatbelt:
    out = out / 'seatbelt'
if args.self_sandbox:
    out = out / 'self-sandbox'
out.mkdir(parents=True, exist_ok=True)
identity = 'com.altivecintelligence.StrappySandbox.' + args.variant
label = args.variant + ('-seatbelt' if args.seatbelt else '')
if args.seatbelt:
    identity += '.seatbelt'
if args.self_sandbox:
    identity += '.self'
    label += '-self'
plist = plistlib.loads((root / 'source/iOS/Info.plist').read_bytes())
plist.update(CFBundleIdentifier=identity,
             CFBundleDisplayName='Sandbox ' + label,
             CFBundleURLTypes=[{'CFBundleURLSchemes': [
                 'strappy-sandbox-' + label]}],
             StrappySandboxVariant=label,
             StrappySelfSandbox=args.self_sandbox)
(out / 'Info.plist').write_bytes(plistlib.dumps(plist))
entitlements = {'application-identifier': identity}
if args.seatbelt:
    entitlements['seatbelt-profiles'] = ['container']
if args.variant != 'baseline':
    # Include canonical /private paths as well as the public /var spelling.
    entitlements['com.apple.security.exception.files.absolute-path.read-write'] = [
        '/var/log/strappy/', '/private/var/log/strappy/']
    key = 'com.apple.security.exception.files.absolute-path.' + args.variant
    entitlements.setdefault(key, []).extend([
        '/var/mobile/Library/Application Support/Strappy/',
        '/private/var/mobile/Library/Application Support/Strappy/'])
(out / 'Entitlements.plist').write_bytes(plistlib.dumps(entitlements))
# The three read-only probe operations and their 36-byte replies have the same
# layout in v5 and v8 (compare 88247ad^). Adapt only this fixture's wire version;
# production headers and binaries are never edited or downgraded.
header = (root / 'source/shared/strappy_ipc.h').read_text()
assert '#define STRAPPY_IPC_VERSION 8U' in header
(out / 'strappy_ipc.h').write_text(header.replace(
    '#define STRAPPY_IPC_VERSION 8U',
    '#define STRAPPY_IPC_VERSION ' + str(args.ipc_version) + 'U'))
(out / 'strappy_ipc.c').write_bytes(
    (root / 'source/shared/strappy_ipc.c').read_bytes())
command = [
    'make', '-C', str(root / 'source/iOS'),
    'PRODUCTS=Strappy', 'Strappy_INSTALL=', 'Strappy_PACKAGES=ipa',
    'Strappy_SANDBOX=container',
    'Strappy_ENTITLEMENTS=' + str(out / 'Entitlements.plist'),
    'INFO_PLIST=' + str(out / 'Info.plist'),
    'BUILD_ROOT=' + str(out / 'output'),
    'EXTRA_FLAGS=-DSTRAPPY_DB_UI=1 -I' + str(out) + ' -I../shared',
    'GROUP_ui_SOURCES=' + str(root / 'source/daemon/tests/sandbox_probe.m'),
    'GROUP_shared_SOURCES=' + str(out / 'strappy_ipc.c') +
    ' strappy_ipc_platform.c',
]
if args.target == 'release':
    subprocess.run(command + ['clean'], check=True)
subprocess.run(command + [args.target], check=True)
