#!/usr/bin/env python3
"""Build isolated native session-list tests (macOS or iOS).

First export build/tests/sidebar-fixture.sqlite with sidebar_harness and
STRAPPY_SIDEBAR_FIXTURE. No production database or daemon is used.
The iOS app writes Documents/session-scroll-result.txt; macOS exits with status
zero and prints PASS. A missing marker, assertion or crash is a failure.
"""
import pathlib
import plistlib
import re
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[3]
platform = sys.argv[1]
assert platform in ("macOS", "iOS")
out = root / "build/tests/session-scroll" / platform
out.mkdir(parents=True, exist_ok=True)
fixture = root / "build/tests/sidebar-fixture.sqlite"
assert fixture.is_file(), "Export the offline sidebar fixture first"
makefile = (root / "source" / platform / "Makefile").read_text().replace("\\\n", " ")
def group(name):
    return re.search(r"^GROUP_" + name + r"_SOURCES = (.*)$", makefile, re.M).group(1).split()
ui = group("ui")
ui.remove("main.m")
ui.append(str(root / "source/daemon/tests" / ("session_scroll_ios_test.m" if platform == "iOS" else "session_scroll_mac_test.m")))
shared = group("shared") + [str(root / "source/daemon/tests/session_rows_probe.m")]
args = ["make", "-C", str(root / "source" / platform), "release",
        "BUILD_ROOT=" + str(out / "output"), "PRODUCTS=Strappy",
        "GROUP_ui_SOURCES=" + " ".join(ui),
        "GROUP_shared_SOURCES=" + " ".join(shared),
        "Strappy_RESOURCES=$(SHARED_RESOURCES) " + str(fixture),
        "Strappy_EMBED=", "Strappy_INSTALL="]
if platform == "iOS":
    identity = "com.altivecintelligence.StrappySessionScrollTest"
    plist = plistlib.loads((root / "source/iOS/Info.plist").read_bytes())
    plist.update(CFBundleIdentifier=identity, CFBundleDisplayName="Session Scroll Test",
                 CFBundleURLTypes=[{"CFBundleURLSchemes": ["strappy-session-scroll-test"]}])
    (out / "Info.plist").write_bytes(plistlib.dumps(plist))
    (out / "Entitlements.plist").write_bytes(plistlib.dumps({
        "application-identifier": identity, "get-task-allow": True}))
    args += ["INFO_PLIST=" + str(out / "Info.plist"),
             "Strappy_VERSION=" + plist["CFBundleShortVersionString"],
             "Strappy_ENTITLEMENTS=" + str(out / "Entitlements.plist"),
             "Strappy_SANDBOX=container", "Strappy_PACKAGES=ipa"]
subprocess.run(args, check=True)
