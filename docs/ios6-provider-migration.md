# iOS 6 production notification provider migration

Deployed to gomadango on 2026-10-18 from commit `6a239a9`; production socket
readiness passed, and the owner confirmed the real-prompt notification worked. The owner confirmed candidate tap-to-open,
dismissal, clearing, history, repeated notifications and restart behavior.
See [the investigation](ios6-notification-provider.md) for native observations.

## Migration results (2026-10-19)

The candidate package was removed before production installation. Package
ownership, obsolete-receiver removal, new-process receiver readiness and daemon
readiness were verified after SpringBoard restart. Real-prompt delivery, app
identity and notification lifecycle were confirmed. Exact rollback packages,
payload manifests, matching symbols and logs were retained. Subsequent deployments
included unread/badges, synchronous Send and unread-row behavior.

Outstanding acceptance and cleanup are tracked in [TODO.md](../TODO.md).

The latest gomadango deployment is `8a0f436`, owner-confirmed working on
2026-10-19. Its verified rollback, package manifests, symbols and logs are in
`build/device-runs/gomadango-8a0f436/`. See the
[latest three-device deployment record](session-unread-notification-plan.md#latest-production-deployments-2026-10-19), the
[production test record](session-unread-notification-plan.md) and
[merge checklist](../TODO.md).
The original migration procedure and rollback guidance below are historical;
its badge-4 and initial iOS-6-only restrictions describe the initial cutover.

## Package and compatibility

The Strappy DEB now owns the native provider bundle and no longer ships the
Substrate dylib/filter. dpkg handles removal of obsolete owned files on upgrade.
The legacy daemon route uses `receiver.sock`; modern runtimes with
`UNUserNotificationCenter` use direct submission. Diagnostic packages use a
separate socket and must be removed before installing production because they own the
same bundle path. The production provider contains no literal test-badge code.
Existing badge 4 is not cleared by this migration; badge policy is separate work.

This migration originally accepted iOS 6 only. The subsequent
[unread/badge implementation](session-unread-notification-plan.md) removes that
upper version guard and tests candidate loading on iOS 8.4.1. arm64 and rootless
packaging checks alone do not establish native support.
The verified cutover target is gomadango, a rootful armv7 iOS 6.1.3 phone.

## Before the device window

Build and inspect both platform packages with the existing Make targets. Keep
matching symbols. Use a fresh directory beneath `/var/mobile/tmp_altivec/` for
all device artifacts and logs, with an independent root SSH session available.
Stage the following before changing packages:

- The new `build/iOS/release/Strappy/Strappy-rootful.deb`, named `new.deb`.
- The exact previously installed production DEB, named `rollback.deb`.
- Candidate 0.1.5, named `candidate.deb`, only if restoring the experiment.

The retained production package from the title/body restoration is at
`build/device-runs/gomadango-notification-restore-ac50d5f/Strappy-rootful.deb`.
Confirm it matches the installed production payload before selecting it for
rollback; a matching version alone is insufficient. Candidate 0.1.5 is retained
at `build/device-runs/gomadango-provider-candidate-v8/package.deb`. Record SHA-256
hashes, `dpkg-deb -I` metadata and `dpkg-deb -c` file lists for all staged packages.
Do not use force-overwrite or force-architecture flags.

If reconstructing rollback from installed files, archive an explicit list of
package-owned regular files with recursion disabled, plus the required dpkg
control metadata/scripts. Never recursively archive directory entries from
`dpkg -L`: shared application directories can include unrelated files or data.
Compare every payload hash with the installed files before upgrading.

Record package versions, daemon/SpringBoard PIDs and relevant crash reports.
Finish active prompts before the window; the installer stops strappyd. Confirm
the owner is ready for the UI restart. No schema or credentials change is part
of this migration. Keep user databases and preferences intact.

## Cutover (root shell, from the owned staging directory)

1. Remove only the experiment through its package manager:
   `dpkg -r com.altivecintelligence.strappy.notification-provider-test`.
2. Install production: `dpkg -i ./new.deb`. Its preflight rejects an installed
   or unpacked experiment. Do not proceed after an error.
3. Inspect `dpkg -L com.altivecintelligence.strappy`: the provider executable and
   Info.plist must be owned by Strappy, and the old `StrappyBulletin.dylib` and
   `StrappyBulletin.plist` must be absent. Do not remove those files manually if
   package inspection disagrees; stop and investigate ownership.
4. Restart SpringBoard once: `killall SpringBoard`. Removing a loaded bundle or
   dylib from disk does not unload it; this restart completes the receiver swap.
5. Wait for SpringBoard and the provider to load. Require a new-process syslog
   entry `StrappyProvider: receiver ready at` ending in `/receiver.sock`, not
   merely an existing socket pathname or an old log line. Preserve the logs.
   Confirm strappyd is loaded and the owner has returned to the Home Screen.
6. Submit a normal prompt through Strappy and observe its completion after
   returning Home. Confirm one notification, Strappy identity, Glass sound under
   the selected settings, tap-to-open and Notification Center clearing. Check
   for new crashes. An isolated test packet does not prove daemon integration.

Do not send prompts between installation and restart: the old receiver may
still be loaded during that interval. Delivery remains best-effort; completion
alerts are not queued or replayed across receiver downtime. The new provider
refuses an existing live endpoint even if the old receiver holds no lock.

## Rollback

If installation or native validation fails, retain logs and use the independent
root shell. From staging, install `dpkg -i ./rollback.deb`, then restart
SpringBoard once with `killall SpringBoard`. dpkg restores the old owned
receiver/filter and removes the newly owned provider bundle. Verify package
ownership, daemon readiness and a normal completion through the legacy receiver.
A version downgrade may be needed; do not force file ownership conflicts.

To restore the exact previous experimental setup as well, install
`dpkg -i ./candidate.deb` only after the rollback production package no longer
owns the provider path, and before the single rollback restart. Never install
the candidate on top of the new production provider. The experiment then owns
its bundle and isolated socket again. No rollback step deletes chat data,
notification preferences or unrelated system files. Retain staging and logs
until the outcome has been recorded.
