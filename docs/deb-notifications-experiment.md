# DEB local notification experiment

2026-10-17, branch `experiment/ios-sandbox-ipc`, koolphone5: iPhone5,2,
iOS 8.4.1 (12H321), armv7, mobile UID 501.

## Result

An unsandboxed app installed by DEB under `/Applications` displayed a local
notification after adding `SBAppUsesLocalNotifications = true` to its Info.plist.
The owner confirmed seeing the alert. This disproves the earlier working
assumption that notification display requires an IPA or sandboxing on this phone.

The successful probe used Strappy's production entitlements, with only the app
identifier and unused Keychain group changed for isolation. It used the same
`AILocalNotificationCenter` as production. No new notification library, private
notification API, remote push service, daemon change, or shared-store change
was needed. The production iOS plist now contains the tested declaration.

The research lead was the upstream
[libnotifications README](https://github.com/CokePokes/libnotifications#readme),
which documents this plist key for apps in `/Applications`. That library targets
newer iOS versions and was not installed or linked; the iOS 8 result comes from
the native test below, not extrapolation from the README.

## Controls and observations

The notification-only mode reuses `sandbox_probe.m`. It skips all IPC, SQLite,
fixture writes, and sandbox initialization. It requests authorization at launch,
starts a short background task on Home, and submits a local alert after three
seconds. This background task exists to run a deterministic test and is not a
proposed change to the production app's lifecycle policy.

| Variant | Packaging | Plist declaration | Device result |
| --- | --- | --- | --- |
| `deb-control` | `/Applications/StrappyNotificationDebControl.app` | Absent | Types 7, authorization 2, submission 0; owner observed no permission dialog. Banner absence was not independently confirmed. |
| `deb-declared` | `/Applications/StrappyNotificationDebDeclared.app` | True | Types 7, authorization 2, submission 0; owner confirmed visible alert. |
| `ipa-control` | IPA | Absent | Cross-built and validated, not installed in this follow-up; earlier IPA results are in the sandbox experiment. |

Types 7 means alert, sound, and badge. Authorization 2 means Authorized;
submission 0 means Submitted. These numeric API results alone are not visual
confirmation. Both DEBs have fresh identities; neither reuses the production
app's saved notification preferences. SpringBoard logged `ADDING REMOTE` for
`com.altivecintelligence.StrappyNotificationDebDeclared` on first launch. No
equivalent provider-registration line appeared for the control in the captured
interval. Separate identities mean this is supporting evidence rather than a
same-identity reversal test proving the key was the sole cause.

The successful app reported `Home: /var/mobile`. Package inspection verified
that each DEB contains only its distinctly named app; neither contains daemon
files or lifecycle scripts. Installation used `dpkg -i`, followed by `uicache`
as mobile. The existing Strappy app, daemon, and database were not replaced.

## Reproduction

From the checkout root:

```sh
python3 source/daemon/tests/notification_probe.py deb-control
python3 source/daemon/tests/notification_probe.py deb-declared
python3 source/daemon/tests/notification_probe.py ipa-control
python3 source/daemon/tests/notification_probe.py deb-control analyze
python3 source/daemon/tests/notification_probe.py deb-control validate-package
python3 source/daemon/tests/notification_probe.py deb-declared validate-package
python3 source/daemon/tests/notification_probe.py ipa-control validate-package
```

The declarative `notification_probe.mk` uses Altivec's common compile, sign,
bundle and package rules. Outputs are under
`build/experiments/deb-notifications/<variant>/output/iOS/release/<product>/`.
Each build includes armv7 and arm64; matching debug symbols are retained.

Stage the DEBs below `~/tmp_altivec` on the authorized phone and install with
its package manager. Refresh registration with `su mobile -c uicache`. Open
`strappy-notification-deb-control://` or
`strappy-notification-deb-declared://` through `uiopen` as mobile (or tap the
matching Home Screen icon). Unlock the phone, respond to any permission dialog,
press Home, and observe the alert after three seconds.

Reports go to
`~/tmp_altivec/com.altivecintelligence.StrappyNotificationDebControl/notification-result.txt`
and the corresponding `StrappyNotificationDebDeclared` directory. Ensure those
test-owned child directories are writable by mobile if the staging parent is
root-owned. Initially the control could not create its report directory for this
reason; its registration/submission results were recovered from syslog, which
also receives probe reports. This did not prevent notification submission.

Generated logs, reports, packages, and symbols remain under the ignored build
directory. The isolated DEB apps remain installed for inspection; uninstall
through `dpkg -r` with their exact package IDs if desired:
`com.altivecintelligence.strappynotificationdebcontrol` and
`com.altivecintelligence.strappynotificationdebdeclared`.

## Validation and limits

### Production follow-up on gomadango

The rootful 1.2.0 package was installed on gomadango (iPhone5,2, iOS 6.1.3).
Daemon IPC v8 passed, but the owner saw no completion alert. Runtime logs showed
the app entering the background at 12:10:36 and receiving the prompt completion
only after becoming active at 12:12:50. The foreground guard then skipped the
alert. The earlier probe held a background task; production had removed it.

The app now holds one finite UIKit background task while prompts are active,
including submission before the daemon persists the run. Start/finish events
track session identifiers so overlapping prompts share the task. Completion
submits the alert before releasing the task. Expiration releases the assertion
without stopping daemon work and prevents renewal until the app becomes active
again. This does not prevent screen locking or promise alerts after iOS expires
the allotted background time. Submission results are logged without content.

The updated rootful DEB was installed on gomadango; daemon IPC v8 passed again.
Native notification display on iOS 6 remains a manual validation step: start a
prompt, press Home before completion, and verify both the visible alert and the
task begin/submission/end sequence in syslog. Also exercise overlapping prompts
and expiration with a long-running request; no renewal should occur in the
background after expiration.

Clean builds passed for both iOS and all four macOS architectures without
compiler warnings. Both iOS packages validated and the app-boundary check passed.
iOS static analysis reported no AppDelegate diagnostics, but flagged a possible
null source in the unchanged debug-capture `memcpy` at
`source/daemon/strappy_client.c:1230`. That separate finding is not resolved by
this lifecycle change. Logs and symbols are retained under
`build/device-runs/gomadango-background/`.

### Original isolated probe coverage

The three probe variants passed clean armv7/arm64 builds and package validation.
Clang analysis of the shared probe passed. Clean production builds cover all
four macOS and both iOS architectures; no compiler warnings were reported.
Native coverage here is iOS 8.4.1 / armv7 only.

The experiment confirms a visible alert from an unsandboxed DEB app. It does
not yet confirm an actual production prompt completion, foreground cleanup,
notification behavior while suspended, a reboot, existing production-ID
notification preferences, or other iOS versions/rootless jailbreaks. The full
production DEB was not deployed: its v8 daemon would replace the installed v5
daemon, which is outside this isolated notification comparison.
