#!/bin/bash
# Host Mac only. The fixture records launchctl; no real service is loaded.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
engine=${ALTIVEC_ROOT:-"$repo/../AltivecIntelligence"}
# Guard the policy boundary separately from the recorded controller fixture.
python3 - "$repo" <<'PY'
import pathlib, sys
source = pathlib.Path(sys.argv[1]) / 'source'
bridge = (source / 'daemon/StrappySession.m').read_text()
configure = bridge.split('int strappy_service_credentials_configure(void)', 1)[1]
configure = configure.split('void *strappy_service_worker_enter', 1)[0]
assert 'configureNoninteractiveProbeAccess' not in configure
probe = bridge.split('int strappy_service_credentials_probe(', 1)[1]
assert '[XPKeychain configureNoninteractiveProbeAccess]' in probe
app = (source / 'macOS/AppDelegate.m').read_text()
termination = app.split('- (void)applicationWillTerminate:', 1)[1]
termination = termination.split('- (BOOL)applicationShouldTerminateAfter', 1)[0]
assert 'stopForApplicationWithError:' in termination
print('PASS: normal startup preserves Keychain prompts; probe stays noninteractive; '
      'Mac termination calls service stop')
PY
mkdir -p "$repo/build/tests/mac-service"
work=$(mktemp -d "$repo/build/tests/mac-service/run.XXXXXX")
python3 - "$work" <<'PY'
import pathlib, plistlib, sys
root = pathlib.Path(sys.argv[1])
contents = root / 'ServiceFixture.app/Contents'
(contents / 'MacOS').mkdir(parents=True)
resources = contents / 'Resources'
resources.mkdir()
(contents / 'Library/LaunchServices').mkdir(parents=True)
(contents / 'Library/LaunchServices/strappyd').write_bytes(b'fixture-only')
info = {'CFBundleExecutable': 'ServiceFixture',
        'CFBundleIdentifier': 'org.strappy.tests.service-lifecycle',
        'CFBundlePackageType': 'APPL'}
(contents / 'Info.plist').write_bytes(plistlib.dumps(info))
for name in ('BundledModels.json', 'AssistantSets.json', 'SystemPrompt.json',
             'GuidanceTools.json', 'GuidanceSkills.json', 'GuidanceDatabase.json',
             'FontAwesomeIcons.json', 'cacert.pem', 'Info.plist'):
    (resources / name).write_bytes(b'fixture-only')
PY
clang -fno-objc-arc -Wall -Wextra -Werror -Wno-deprecated-declarations \
  -Wno-semicolon-before-method-body -framework Cocoa \
  -I"$repo/source/macOS" -I"$repo/source/shared" -I"$engine/libs/cocoa" \
  "$repo/source/daemon/tests/mac_service_lifecycle_test.m" \
  -o "$work/ServiceFixture.app/Contents/MacOS/ServiceFixture"
python3 - "$work" <<'PY'
import pathlib, subprocess, sys
work = pathlib.Path(sys.argv[1])
subprocess.run([str(work / 'ServiceFixture.app/Contents/MacOS/ServiceFixture'),
                str(work / 'home')], check=True, timeout=45)
PY
