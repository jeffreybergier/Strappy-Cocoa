#!/usr/bin/env python3
"""Build notification-only IPA/DEB controls; never installs or starts a daemon."""
import argparse
import pathlib
import plistlib
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('variant', choices=('ipa-control', 'deb-control',
                                       'deb-declared'))
parser.add_argument('target', nargs='?', default='release',
                    choices=('release', 'analyze', 'validate-package'))
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[3]
out = root / 'build/experiments/deb-notifications' / args.variant
out.mkdir(parents=True, exist_ok=True)
product = 'StrappyNotification' + ''.join(
    part.title() for part in args.variant.split('-'))
identity = 'com.altivecintelligence.' + product
plist = plistlib.loads((root / 'source/iOS/Info.plist').read_bytes())
plist.update(CFBundleIdentifier=identity, CFBundleExecutable=product,
             CFBundleDisplayName=args.variant,
             CFBundleURLTypes=[{'CFBundleURLSchemes': [
                 'strappy-notification-' + args.variant]}],
             StrappySandboxVariant=args.variant,
             StrappySkipStorageProbe=True,
             StrappyProbeReportPath='tmp_altivec/' + identity +
                 '/notification-result.txt')
plist.pop('SBAppUsesLocalNotifications', None)
if args.variant == 'deb-declared':
    plist['SBAppUsesLocalNotifications'] = True
(out / 'Info.plist').write_bytes(plistlib.dumps(plist))
entitlements = plistlib.loads(
    (root / 'source/iOS/Entitlements.plist').read_bytes())
entitlements['application-identifier'] = identity
entitlements['keychain-access-groups'] = [identity]
(out / 'Entitlements.plist').write_bytes(plistlib.dumps(entitlements))
command = ['make', '-C', str(root / 'source/daemon/tests'),
           '-f', 'notification_probe.mk',
           'PROBE_PRODUCT=' + product, 'PROBE_ID=' + identity.lower(),
           'PROBE_VERSION=' + plist['CFBundleShortVersionString'],
           'PROBE_INPUT=' + str(out),
           'PROBE_FORMAT=' + ('ipa' if args.variant == 'ipa-control'
                              else 'deb-rootful'),
           'BUILD_ROOT=' + str(out / 'output')]
if args.target == 'release':
    subprocess.run(command + ['clean'], check=True)
subprocess.run(command + [args.target], check=True)
