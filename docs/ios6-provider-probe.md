# Isolated iOS 6 native BulletinBoard provider test

Status: prepared offline; not installed or run on gomadango. The production
notification implementation is unchanged. This test asks whether Apple's native
loader accepts a custom provider, then whether that provider can publish one
fixed bulletin. It does not try to establish a complete notification service.

See [investigation history](deb-notifications-experiment.md#provider-investigation-handoff-2026-10-17)
for prior commits, native results, binary provenance and online sources.

## Implementation and evidence

`source/iOS/provider_probe.mk` builds a separately signed native `MH_BUNDLE`
and a tiny foreground sender through the existing Altivec product pipeline.
There is no launchd daemon, Substrate filter, production dependency, installer
script, automatic respring, or automatic notification. Installed files are:

- `/System/Library/BulletinBoardPlugins/StrappyProviderProbe.bundle/Info.plist`
- `/System/Library/BulletinBoardPlugins/StrappyProviderProbe.bundle/StrappyProviderProbe`
- `/usr/libexec/strappy-provider-probe`

Package and section identity: `com.altivecintelligence.strappy.provider-probe`.
The provider adopts the runtime's actual `BBDataProvider` protocol at class load;
its initializer refuses OS versions outside iOS 6. Both armv7 and arm64 build,
but only armv7 on iOS 6 is a runtime target. No claim of iOS 7+ compatibility.

Offline inspection with `llvm-objdump --macho --objc-meta-data` confirms the
copied iOS 6 MailDataProvider adopts `BBDataProvider` and implements
`bulletinsFilteredBy:count:lastCleared:`, `sortDescriptors`, `sectionIdentifier`
and `defaultSectionInfo`. `llvm-nm -u` confirms its imports include
`BBDataProviderAddBulletin` and `BBDataProviderWithdrawBulletinsWithRecordID`.
The copied plist identifies `MFMailDataProvider` as `NSPrincipalClass` and
`BNDL` as the package type. The full loader implementation and threading contract
have not been disassembled; the probe's main-thread publication is experimental.

The receiver logs load, initialization, section queries, publication and
withdrawal. It resolves publication functions at runtime and checks selectors.
One explicit Darwin signal requests a fixed notification with no personal data,
sound or tap action. An exclusive `attempted` file limits publication to one
attempt across restarts. Do not remove it to retry during this test window.
No payload or arbitrary bundle identity is accepted. Darwin signals are not
authenticated; the scope is deliberately one fixed diagnostic message.

The provider returns an empty list when queried, so it does not reconstruct
notification history after clearing/restart. Defaults are supplied by
`BBSectionInfo`; no user settings are overwritten. Settings visibility, banners,
lock screen and Notification Center behavior are observations, not guarantees.

## Build and inspect locally

From the repository root:

```sh
probe_build="$PWD/build/experiments/provider-probe"
make -C source/iOS -f provider_probe.mk BUILD_ROOT="$probe_build" clean
make -C source/iOS -f provider_probe.mk BUILD_ROOT="$probe_build" -j8 release
make -C source/iOS -f provider_probe.mk BUILD_ROOT="$probe_build" validate-package analyze
```

Artifact:
`build/experiments/provider-probe/iOS/release/strappy-provider-probe/strappy-provider-probe-rootful.deb`.
Keep both products' matching dSYMs. Review `dpkg-deb -c` and `dpkg-deb -I`:
exactly the three payload files above; no lifecycle scripts or production files.

## Agreed test window only

Allow approximately 15 minutes with the owner present, no active Strappy prompt,
phone charged, and no expected call. Budget two intentional SpringBoard restarts:
one to load the provider, one after removal. This is one installation test,
not a sequence of exploratory resprings. Unexpected restarts mean stop and remove.

A native provider executes inside SpringBoard. A crash can disrupt the phone,
and a provider in the native system directory may still load in Substrate Safe
Mode. Safe Mode alone is not a rollback plan. Before installation, verify a
second independent root SSH connection and the package-removal command. If SSH
recovery cannot be relied on, defer this experiment on the production phone.

Nothing below has been executed as part of preparation. Confirm the phone is
still gomadango / iPhone5,2 / iOS 6.1.3 and rootful. Review the exact DEB hash with
the owner before proceeding; do not use an IP override from an old investigation.

Stage from this checkout (no production files are overwritten):

```sh
stage=/var/mobile/tmp_altivec/strappy-provider-probe-v1
probe_deb=build/experiments/provider-probe/iOS/release/strappy-provider-probe/strappy-provider-probe-rootful.deb
sha256sum "$probe_deb"
ssh gomadango 'mkdir -p /var/mobile/tmp_altivec && mkdir /var/mobile/tmp_altivec/strappy-provider-probe-v1 && chown mobile:mobile /var/mobile/tmp_altivec/strappy-provider-probe-v1 && chmod 700 /var/mobile/tmp_altivec/strappy-provider-probe-v1'
scp -O "$probe_deb" "gomadango:$stage/package.deb"
scp -O source/daemon/tests/provider_probe/device-test.sh "gomadango:$stage/"
```

If the staging directory exists, stop; preserve old evidence and inspect it.
Do not empty it or overwrite a previous run. Open two SSH terminals. In the
first, run each action separately and inspect its exit status/log before the next:

```sh
cd /var/mobile/tmp_altivec/strappy-provider-probe-v1
dpkg-deb -I package.deb
dpkg-deb -c package.deb
sh device-test.sh baseline
sh device-test.sh install
cat install.log
sh device-test.sh respring
```

Wait for the owner to confirm the Home Screen is usable. Inspect `receiver.log`.
A missing log, failure to initialize, or any crash means collect and roll back;
do not attempt another respring or tweak the package in place. Initialization
alone does not prove registration; section/default queries provide additional
evidence. Observe whether “Strappy Provider Probe” appears in notification
settings. Record its defaults; do not change production Strappy settings.

With the phone awake and unlocked, record DND/mute state and issue just once:

```sh
sh device-test.sh send
cat sender.log
cat receiver.log
sh device-test.sh collect
```

Record whether a banner appears and whether Notification Center contains it.
Do not toggle lock/DND or send again to extend the experiment. “Publication
returned” only establishes that the private call returned. No visible alert is
an informative result, not permission to change notification policy.

## Exact removal and emergency rollback

Normal path, while the receiver is healthy:

```sh
cd /var/mobile/tmp_altivec/strappy-provider-probe-v1
sh device-test.sh clear
cat clear.log
cat receiver.log
sh device-test.sh remove
cat remove.log
sh device-test.sh respring
sh device-test.sh collect
```

Emergency path from the second root SSH terminal (does not call provider APIs):

```sh
cd /var/mobile/tmp_altivec/strappy-provider-probe-v1
dpkg -r com.altivecintelligence.strappy.provider-probe > emergency-remove.log 2>&1
```

Check removal succeeded and the bundle executable is gone, then explicitly run:

```sh
test ! -e /System/Library/BulletinBoardPlugins/StrappyProviderProbe.bundle/StrappyProviderProbe && /usr/bin/killall -TERM SpringBoard
```

Do not repeatedly restart SpringBoard before removing the package. Do not
uninstall production Strappy, disable its receiver, overwrite its plist, restore
an entire BulletinBoard preferences file, or delete other providers. If removal
fails, read the dpkg error and stop; do not force package-manager operations.

After rollback, confirm the Home Screen and production Strappy still work;
compare production hashes/package version using `collect`, inspect new crash
report names, and verify the test section/notification disappeared. Removal
uninstalls all test payload and the restart unloads its code. iOS may retain
preferences for the unique test section: disappearance is not guaranteed, and
this probe deliberately does not edit shared BulletinBoard preference storage.
Any residual entry must be reported and investigated separately. This is code
and package rollback, not a claim of byte-for-byte restoration of OS state.

Retrieve evidence and keep staging until reviewed:

```sh
mkdir -p build/device-runs/gomadango-provider-probe-v1
scp -O 'gomadango:/var/mobile/tmp_altivec/strappy-provider-probe-v1/*.log' build/device-runs/gomadango-provider-probe-v1/
scp -O 'gomadango:/var/mobile/tmp_altivec/strappy-provider-probe-v1/before.*' build/device-runs/gomadango-provider-probe-v1/
scp -O 'gomadango:/var/mobile/tmp_altivec/strappy-provider-probe-v1/after.*' build/device-runs/gomadango-provider-probe-v1/
```

Only copy newly identified SpringBoard crash reports if needed. Logs stay ignored;
record sanitized observations in the investigation document after the test.

## Preparation validation (2026-10-17)

- Clean isolated armv7/arm64 release build passed without compiler warnings.
- Common package validation passed; inspected payload has only the three stated
  files, with root ownership and no lifecycle scripts. Probe static analysis
  (armv7) reported no diagnostics.
- Required clean production builds passed for iOS armv7/arm64 and macOS
  PPC/i386/x86_64/arm64 without compiler warnings. Production code was unchanged.
- Shell syntax and `git diff --check` passed. This is cross-build/inspection
  coverage, not native provider validation. No probe was installed or triggered.
- A read-only SSH preflight confirmed gomadango has `sha256sum`, `dpkg`,
  `dpkg-deb`, `dpkg-query`, `diff`, `su`, and `/usr/bin/killall`, the production
  files to fingerprint, and a CrashReporter directory. Production package is
  1.2.0; the proposed test bundle and staging directory were absent. The device
  lacks the `md5` command, so the runner uses its verified `sha256sum` instead.
- Local logs: `build/provider-probe-logs/`,
  `build/production-clean-provider-review.log`,
  `build/production-build-provider-review.log`, and
  `build/production-package-provider-review.log`. Keep logs/binaries ignored.

Scheduling remains pending: owner to choose a 15-minute window with the phone
available and two intentional resprings acceptable. Preparation does not
schedule an automatic job or authorize unattended installation.

Prepared DEB SHA-256 (recompute after any rebuild):
`76ee305f67630cd0c13a3ffeaccd88df4af01724bd9dfef134a7d46d82688fb7`.
