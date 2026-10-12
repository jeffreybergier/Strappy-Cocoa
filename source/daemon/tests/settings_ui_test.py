#!/usr/bin/env python3
"""Build an isolated UIKit regression IPA inside the Altivec container.

Install only on an authorized TEST phone with appinst, then launch
strappy-settings-test://run. Documents/settings-result.txt must become PASS
within 15 seconds; RUNNING, a crash, or a missing result is a failure.
No production app, database, credentials, or daemon is used.
"""
import pathlib
import plistlib
import re
import subprocess

root = pathlib.Path(__file__).resolve().parents[3]
out = root / "build/tests/settings-ui"
out.mkdir(parents=True, exist_ok=True)
identity = "com.altivecintelligence.StrappySettingsTest"
plist = plistlib.loads((root / "source/iOS/Info.plist").read_bytes())
plist.update(CFBundleIdentifier=identity, CFBundleDisplayName="Settings Test",
             CFBundleURLTypes=[{"CFBundleURLSchemes": ["strappy-settings-test"]}])
(out / "Info.plist").write_bytes(plistlib.dumps(plist))
(out / "Entitlements.plist").write_bytes(plistlib.dumps({
    "application-identifier": identity, "get-task-allow": True}))
# Use the real app's complete UI dependency list, replacing only its entry point.
makefile = (root / "source/iOS/Makefile").read_text().replace("\\\n", " ")
sources = re.search(r"^GROUP_ui_SOURCES = (.*)$", makefile, re.M).group(1).split()
sources.remove("main.m")
sources.append(str(root / "source/daemon/tests/settings_ui_test.m"))
subprocess.run([
    "make", "-C", str(root / "source/iOS"), "release",
    "BUILD_ROOT=" + str(out / "output"), "PRODUCTS=Strappy",
    "Strappy_VERSION=" + plist["CFBundleShortVersionString"],
    "GROUP_ui_SOURCES=" + " ".join(sources),
    "INFO_PLIST=" + str(out / "Info.plist"),
    "Strappy_ENTITLEMENTS=" + str(out / "Entitlements.plist"),
    "Strappy_SANDBOX=container", "Strappy_INSTALL=", "Strappy_PACKAGES=ipa",
], check=True)
print(out / "output/iOS/release/Strappy/Strappy.ipa")
