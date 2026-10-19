# Session unread state, observation and badges

Design, implementation and device experiments, started 2026-10-18. The policy
below is implemented for the iOS app and daemon. macOS retains its existing
app-owned notification presentation policy.

## Modern daemon integration (2026-10-19)

The daemon now selects direct UserNotifications submission when the dynamically
loaded `UNUserNotificationCenter` class exists; otherwise it keeps the existing
legacy provider transport. There is no OS-version table or delivery-time backend
switch. Private calls use checked signatures and NSInvocation in the iOS-only
`XPDaemonNotifications` bridge. The modern frameworks are not linked at launch.

Initialization repeats the successful probe's bundle-specific center and
alert/sound/badge authorization request. It adds no app permission screen.
Initialization failure is logged without affecting prompt execution. Glass uses
alert type 5 and the dedicated `com.altivecintelligence.Strappy.completion` topic;
missing tone APIs fall back to the default sound. The daemon's entitlement lists
only `com.altivecintelligence.Strappy` as a notification recipient.

The existing database commit, unread, observation, cancellation, and preview
policies are unchanged. Modern alerts use a 32-entry memory queue and stable
run-based identifiers. A worker serializes framework operations with five-second
callback deadlines. Alerts remain best-effort and are not retried on an ambiguous
callback result. Badge snapshots use the existing durable queue, coalesce to the
latest value, and acknowledge only their exact revision after a successful native
callback. Badge values are never embedded in alert content. Late callbacks own
independent reply objects and cannot acknowledge newer revisions or access torn
down daemon state. An OS operation that times out cannot be canceled by this
bridge; callback success remains submission evidence, not proof of presentation.

Validation: the full offline Linux suite and clean armv7/arm64 iOS plus
ppc/i386/x86_64/arm64 macOS release builds passed without compiler warnings.
The recording-only modern bridge fixture passed on x9-vm (macOS 10.9/x86_64),
covering class selection, authorization, missing selectors/signature mismatch,
content, bounded queue, badge revisions/zero, late callbacks, and shutdown.
Run that fixture on a Mac with
`make -C source/daemon/tests modern-notifications-test`.
The fixture never loads notification frameworks or changes system settings.
Rootful/rootless package validation and provider ownership checks passed; both
signed daemon slices contain the narrow notification entitlement. iOS static
analysis found no diagnostics in the new bridge; it reported a possible null
source in the unchanged debug-capture memcpy in `strappy_client.c:1230`.
Production iOS daemon display, sound, permission changes, and reboot behavior
still require native device acceptance; the earlier probe results below remain
separate evidence. No production device was redeployed for this integration.

## Acceptance checklist (reviewed 2026-10-19)

- [x] Confirm observed-session suppression and foreground alerts for another
  session on gomadango.
- [x] Confirm background completion banners/sound and unread-session badge
  transitions 2 → 1 → cleared on gomadango.
- [x] Validate failures, repeated answers, deletion, disconnect/lease expiry,
  lost acknowledgments and restart recovery in automated harnesses.
- [x] Deploy synchronous Send without a prompt background assertion.
- [x] Confirm completion while the UI is suspended or terminated (owner
  confirmation, 2026-10-19; no new instrumented trace collected).
- [x] Confirm processing/unread row icons visually (owner confirmation,
  2026-10-19).

The latest recorded deployments are `56e1ef0` on pinkphone-rsa (notification
receiver not ready), `ad47a44` on koolphone5, and `b881428` on gomadango
(following `f37bf03` and `02b1b34`). Remaining compatibility work is tracked in the
[current notification checklist](ios6-notification-provider.md#current-checklist-2026-10-19).
Dated test records below preserve the scope of each original observation.

## Koolphone5 production acceptance (2026-10-19)

Installed the rootful production package from `ad47a44` on koolphone5,
iPhone5,2 / iOS 8.4.1 (12H321) / armv7, upgrading the installed 1.1.2 package
to 1.2.0. New package SHA-256:

`d1b23b5cdb458b9a9e38341069e5a3ad865238235c303fdaf433b1675a9e03ba`

- [x] Pass the offline Linux suite and rootful/rootless package checks. The
  preceding clean iOS/macOS build passed without compiler warnings.
- [x] Prepare rollback: retained DEBs did not match the installed build, so
  reconstruct a package from its package-owned files and dpkg control scripts.
  All 38 installed regular-file hashes matched the reconstructed payload.
- [x] Install with dpkg, verify all 39 new regular-file hashes and provider
  ownership, and restart SpringBoard once.
- [x] Verify IPC v8 store/execution readiness and a live production receiver
  connection. SpringBoard PID 3076 logged `receiver.sock` readiness at
  19:20:08 device time. No new crash report filenames appeared during these checks.
- [x] Owner confirms a background completion notification, sound and badge.
- [x] Owner confirms opening the app and reading that session clears the badge.
- [x] Owner confirms no banner or sound while staying in the completing session.
- [x] Owner confirms banner and sound for another session's completion while
  the app is foregrounded.
- [x] Owner confirms two unread sessions produce badge 2, then opening each
  session reduces it to 1 and clears it.
- [x] Owner confirms lock-screen presentation and opening Strappy from the
  notification.
- [x] Owner confirms Strappy notification settings registration, Notification
  Center history, dismissal and clearing.
- [x] Owner confirms badge 1 survives a SpringBoard restart, and a new
  completion afterward still produces a notification, sound and badge.

The owner-confirmed results establish actual display and badge behavior, beyond
API submission/readback. All listed koolphone5 production acceptance checks
have passed; this does not establish compatibility on another OS/architecture.

For the final restart check, the owner first confirmed the prompt had finished
and Home Screen showed badge 1. SpringBoard restarted once for this check, from
PID 3076 to 3145; the new process logged production receiver readiness at
19:29:30 device time. The live socket and IPC v8 readiness probes passed again.
The owner then confirmed badge persistence and fresh completion delivery.
The final crash-directory comparison showed no new report filenames during
installation or acceptance testing.

Packages, payload manifests, installer/readiness logs, crash inventories and
matching armv7 symbols are retained under
`build/device-runs/koolphone5-notifications-ad47a44/`. Remote staging is
`/var/mobile/tmp_altivec/koolphone5-notifications-ad47a44/`. The rollback package
is reconstructed from the installed payload, not an original release archive;
its provenance is retained alongside it. No user database or credentials were
included in that package. Older isolated DEB notification probes were left
installed under their separate identities.

## Pinkphone rootless production test (2026-10-19)

Target: `pinkphone-rsa`, iPhone8,4 (iPhone SE), iOS 15.0 (19A346), arm64,
rootless Dopamine layout under `/var/jb`; SSH as mobile with passwordless sudo.
Installed production build `56e1ef0`, upgrading package 1.1.2 to 1.2.0.
Rootless package SHA-256:

`f0552db7cc0ba65153913afc9ca90b0b00d2648561194307971d206371f953d1`

- [x] Verify the old daemon's IPC v5 readiness before installation.
- [x] Reconstruct rollback from package-owned installed regular files and dpkg
  control metadata; all 38 file hashes match. The retained archive did not match.
  Exclude the system `/var` and `/var/jb` directory symlinks from reconstruction.
- [x] Pass rootless notification payload/preflight checks and build/validate the
  existing loader diagnostic for armv7/arm64 without compiler warnings.
- [x] Install the rootless DEB with dpkg and verify all 39 installed regular-file
  hashes and production provider package ownership.
- [x] Verify production IPC v8 store/execution readiness.
- [x] Restart SpringBoard once (PID 279 → 2683) and confirm it is running.
- [x] Load the installed provider in the isolated diagnostic: BulletinBoard loads,
  both publication/withdrawal symbols exist, the principal class conforms to
  `BBDataProvider`, and initialization succeeds (CF version 1854.00).
- [ ] Establish automatic provider discovery/registration. The production
  receiver connection probe returns 1, and the StrappyAlerts directory is absent
  after restart. Manual diagnostic initialization does not register the provider
  or open the receiver and is not evidence of automatic loading.
- [x] Owner configures an account and confirms a foreground prompt completes.
- [x] Owner confirms production notifications do not appear on this phone.
- [ ] Run notification/badge visual acceptance after receiver readiness works.

A separate modern UserNotifications probe subsequently passed owner-observed
banner, default sound, badge set/clear, notification removal, tap-to-app, and
lock-screen tests.
It requests authorization and posts directly under Strappy's bundle identity,
without integrating into strappyd or loading the legacy provider. Glass via an
absolute system sound-file path fell back to the default sound, as did a generated
WAV tested in the helper and app bundles. A subsequent Mail-style tone test
**succeeded with owner-confirmed Glass**: `TLToneManager` selects `texttone:Glass`
for a Strappy-only topic, and a mutable `UNNotificationSound` created with alert
type 5 receives that topic before submission. No separate audio playback is used.
This is a manual probe result, not production integration; the test fixture and
topic cleanup were paused at the owner's request. See the
[probe instructions and acceptance checklist](../source/daemon/tests/user_notification_device/README.md).

Before installation, neither `/System/Library/BulletinBoardPlugins` nor its
`/var/jb` counterpart existed. The package owns the provider under
`/var/jb/System/Library/BulletinBoardPlugins/`. These observations narrow the
current failure to automatic discovery/registration; they do not establish that
iOS 15 removed the provider APIs or that presentation would work after loading.

The ordinary loader diagnostic was killed with exit 137 before producing its
report, both as mobile and root. Thinning to arm64 did not resolve it. Adding
its specific arm64 SHA-256 code-directory hash with `jbctl trustcache add` did
not resolve it either; that diagnostic hash remains in the jailbreak trust cache
(no global cache clearing was attempted). A staging-only copy signed with
platform/no-sandbox/no-container entitlements then ran. These entitlements were
not added to the production provider. Loading the staged provider reported a
sandbox `mmap` denial, including as root. Loading the installed bundle through
an owned staging symlink succeeded. No diagnostic invoked registration, sent a
bulletin or opened a receiver. A separately signed readiness helper only tested
the production socket connection; it did not send a notification.

No usable on-device `log` executable or `/var/log/syslog` was found. The
readiness finding uses the actual socket probe and directory check, not an
absence of log messages. The isolated loader report does not prove behavior
inside SpringBoard or its automatic search paths.

Packages, manifests, rollback provenance, arm64 symbols and diagnostic reports
are retained under `build/device-runs/pinkphone-notifications-56e1ef0/`.
Remote staging is `/var/mobile/tmp_altivec/pinkphone-notifications-56e1ef0/`.
The new production package remains installed; the matching reconstructed rollback
is staged as `rollback.deb`. Restore with `sudo -n dpkg -i ./rollback.deb` from
that directory, then restart SpringBoard once and verify the old daemon. Do not
manually remove installed provider files or change the bootstrap symlinks.

## Agreed simpler behavior

- A session has `hasNewAnswers`; the icon badge is the number of sessions where
  that value is true, not the number of answers or notifications.
- Opening a session acknowledges it immediately. No HTML rendering, JavaScript
  acknowledgment, scroll-position check or answer-render receipt is required.
- A session has `isBeingObserved`: the app is currently looking at that specific
  session. Opening/closing/switching sessions updates this state. Backgrounding
  the app must end observation; returning to the session reestablishes it.
- Alert eligibility depends on observation of the completing session. App-wide
  foreground status must not suppress another session's completion alert.
- The owner confirmed a respring preserves the existing icon badge. Do not add
  badge restoration on every SpringBoard restart based on an assumed loss.
- Completion in an unobserved session sets `hasNewAnswers` and sends an alert.
  Completion in an observed session should not add unread state or send an alert.
  Opening a session clears its flag and updates the badge independently of alerts.

## Implemented policy and ordering

`session_attention.has_new_answers` is durable SQLite state. A missing row means
read, so existing chats begin read. `notification_badge` stores a monotonically
increasing revision and pending flag; the count is derived from unread sessions.
Both tables are created additively; `user_version` stays at 1.

Observation is live, connection-owned daemon state, not a persisted boolean.
The main-thread UI bridge opens immediately, closes on navigation away or
resigning activity/backgrounding, and renews every 5 seconds while active.
Observation expires after 15 seconds without renewal; a disconnected peer is
ineligible immediately when completion checks it. Returning to the session or
reconnecting reacquires observation and acknowledges unread answers. Session and
owner tokens prevent an old controller's close from clearing a newer observation.
A lost close can suppress a completion until disconnect or lease expiry; lifecycle
IPC is best effort, so this is bounded rather than mathematically instantaneous.

The daemon serializes acknowledgment, terminal-state commits and deletion.
Completion and unread changes commit together, before attempting the alert.
Failures count as new answers; cancellations, shutdown interruption and Database
Study do not. Repeated completion processing cannot send a second alert. Clearing
Notification Center does not acknowledge a session. Opening does not remove
previously delivered bulletins.

| Event | Durable unread state | Visible alert | Badge update |
| --- | --- | --- | --- |
| Open session A | Clear A immediately; observe A | None | Always queue current count, even if unchanged |
| A completes while observed | A remains read | None | Queue current count |
| B completes while observing A | Set B unread | Send B alert, even in foreground | Queue current count |
| Unobserved A completes again | A stays unread | Send new completion alert | Queue same absolute count |
| Unobserved prompt fails | Set its session unread | Send failure alert | Queue current count |
| Cancel, interrupt, or Database Study ends | Unchanged | None | None |
| Switch A to B | End A observation; clear/observe B | None | Always queue current count |
| Close session / leave app | End observation | None | None |
| Return to session | Reacquire observation; clear it | None | Always queue current count |
| Ordinary 5-second renewal | Extend observation | None | None |
| App disconnects / lease expires | End observation; existing unread unchanged | None by itself | None |
| Delete session | Remove its unread row and observation | None | Queue current count if it had an attention row |
| Completion wins race before open | Commit unread and attempt alert, then clear on open | May already be sent | Latest count wins |
| Open wins race before completion | Completion sees observation and stays read | None | Latest count wins |
| Provider unavailable or ACK lost | Preserve pending revision | Alerts remain best effort | Retry latest snapshot about once per second |
| Daemon restarts | Preserve unread; discard observation | No replay | Retry pending snapshot; UI reacquires observation |
| SpringBoard resprings after ACK | Unchanged | No replay | No forced restoration; OS preserves badge |
| Dismiss Notification Center item | Unchanged | Dismissed by OS | None |

The dedicated badge worker sends absolute counts using `STB1`; SpringBoard changes
only the icon badge and acknowledges `STA1` after API readback. Older revisions
cannot overwrite newer ones, and an ACK clears only its exact durable revision.
The worker coalesces queued updates to the latest state. There is no comparison
against the last badge count. Alert submission and badge delivery are independent;
a banner can appear before its badge update. A later open cannot retract an alert
already submitted. System settings still control visible presentation and sound.

## Synchronous Send and app suspension

Send now performs one synchronous IPC submission on the main thread. The payload
includes the prompt, changed session options, and a persisted retry key. Success
means the daemon has committed acceptance; model execution remains in strappyd.
Each IPC connect/handshake/send/reply stage has a two-second timeout; a disconnected
service can therefore take multiple stages, rather than a single total two-second
deadline. Submission does not automatically retry while holding the UI thread.
Rejection preserves the editable prompt and option draft. A lost reply also
preserves the saved key, so retrying the same prompt resolves acceptance without
duplicating daemon work. Both platform composers clear text only on acceptance.

The iOS app no longer requests background execution time for prompts. It still
ends observation on deactivation/backgrounding and reacquires it on activation.
The system can suspend the app while strappyd completes work and SpringBoard
presents the notification and badge. On 2026-10-19, the owner confirmed the
follow-up device acceptance checks work, including completion while the UI is
suspended or terminated. This closes the suspended-app completion follow-up
based on owner confirmation; no new instrumentation or device logs were
collected for this confirmation.

The updated native submission fixture passed on koolphone5 (iOS 8.4.1, armv7):
a simulated 300 ms option-save delay held Send until rejection (about 516 ms
including local persistence), rejection preserved the option draft, and a lost
reply retained the retry identity and 1 MiB Unicode payload for acceptance on
retry. The control-client harness verifies no automatic submission replay and
an explicit retry with the same key. Logs are retained under
`build/sync-send-native-final.log` and `build/sync-send-tests-final.log`.
The complete offline suite passed. Final clean builds passed without compiler
warnings for iOS armv7/arm64 and macOS PPC/i386/x86_64/arm64, along with package
validation and rootful/rootless notification payload checks. The synchronous
Send build `02b1b34` was subsequently installed on gomadango with dpkg, followed
by one respring. The previous payload matched the retained `f37bf03` rollback
package, and installed hashes matched the new package. strappyd PID 4993 passed
the store/execution IPC readiness probe. SpringBoard PID 5006 logged production
receiver readiness at 23:36:29 device time on 2026-10-18. No new crash reports
appeared during installation/readiness checks. Packages, manifests, matching
armv7 symbols and logs are retained in
`build/device-runs/gomadango-sync-send-02b1b34/`; remote staging uses
`/var/mobile/tmp_altivec/gomadango-sync-send-02b1b34/`. After installation, the owner reported that the background completion test
worked. The app has no prompt background assertion; the exact OS suspension
state was not independently instrumented during that visual test.

## Unread status in iOS session rows

Session rows use the existing trailing accessory position. An active prompt
shows Font Awesome solid `arrows-spin`; an idle unread session shows solid
`circle`; a read idle session has no accessory. Both glyphs use the same bold
blue (#007AFF), with the existing 14-point canvas and 10-point icon size.
Processing takes precedence when a session is both active and unread. These are
static status glyphs, matching the previous accessory's presentation.

The paged sidebar snapshot now includes `session_attention.has_new_answers`,
defaulting missing attention rows to read. It uses an indexed join inside the
existing bounded page query and exposes the flag through `StrappySessionRows`.
No new persistent state or per-cell database call is needed. Existing committed
session invalidations refresh the snapshot after completion or acknowledgment,
so opening a session clears the row dot and the app badge from the same source
of truth. Reused cells reset their accessory and accessibility status.

The complete offline suite passed, including new checks for default-read rows,
unread state on refreshed snapshots, snapshot isolation, and acknowledgment
clearing. The 20,000-session paging test remains bounded (maximum 1,957 VM steps
per sequential page in this run).

Build `b881428` was installed on gomadango with dpkg and one respring on
2026-10-18. The previous installed payload matched the saved `02b1b34` rollback
package, and all new installed payload hashes matched the staged build. Daemon
PID 5138 passed store/execution IPC readiness; SpringBoard PID 5151 logged
production receiver readiness at 23:55:07 device time. No new crash reports
appeared during these checks. Matching packages, armv7 symbols and logs are under
`build/device-runs/gomadango-unread-row-b881428/`, with remote staging at
`/var/mobile/tmp_altivec/gomadango-unread-row-b881428/`. On 2026-10-19, the owner
confirmed that the processing and unread row icons display correctly, completing
the outstanding visual acceptance check.

## Implementation validation

The offline execution suite and dedicated attention harness cover observed and
unobserved completions, failures, repeated answers, repeated opens, stale closes,
switching, disconnect, expiry/reacquisition, cancellation, deletion, lost ACKs,
idle after ACK, and daemon restart with a pending badge. The attention harness
also runs natively on x4-vm (Mac OS X 10.4.11, PowerPC) and koolphone5 (iOS 8.4.1,
armv7), using isolated databases and sockets. Both passed.

The production provider policy fixture passed natively on koolphone5, including
badge validation, duplicate/stale revisions, clear, and separation from bulletin
publication. A removable candidate provider loaded in iOS 8 SpringBoard and
acknowledged badge 8 and then zero with real icon readback. iOS 8 needs
`applicationIconForBundleIdentifier:`; iOS 6 uses
`applicationIconForDisplayIdentifier:`. Both selectors are runtime checked.
The iOS-6-only upper version guard has been removed as requested. The native
candidate also returned from bulletin publication with sound 1009 attached.
It was removed and SpringBoard restarted afterward; production app and daemon
SHA-256 fingerprints remained unchanged.

Production builds cover iOS armv7/arm64 and macOS PPC/i386/x86_64/arm64;
rootful/rootless package payload checks pass. Provider analysis is clean. Broader
daemon analysis reports an existing possible null `memcpy` source in unchanged
`strappy_client.c:1230` (raw HTTP debug capture), unrelated to badge handling.
No new crash reports appeared during the koolphone5 candidate tests.

Native device logs and matching test artifacts are retained under
`build/device-runs/attention-x4/` and `build/device-runs/attention-koolphone5/`.
Those isolated tests established daemon and provider behavior, not visual
presentation. The subsequent production checks below establish visible behavior
and app navigation on gomadango.

## Production deployment and owner-confirmed results on gomadango

On 2026-10-18, installed the rootful production DEB built from `f37bf03` on
gomadango (iPhone5,2, iOS 6.1.3, armv7), then restarted SpringBoard once.
The installed package version remains 1.2.0; the commit and SHA-256 identify
this particular build:

`ebd24470a250db7d0f00a874cbe18cf94b02a27e9af2cae0c1ede2596c77ca6e`

Before installation, the existing payload matched the retained rollback DEB
from the `6a239a9` deployment. After installation, all installed payload hashes
matched the new package. strappyd PID 4803 passed the same-user IPC probe,
including store/execution readiness. SpringBoard PID 4815 logged production
`receiver.sock` readiness at 23:15:42 device time. No new crash reports appeared
in the installation/readiness check. Existing chats and settings were preserved.

The owner then ran real prompts and confirmed these results interactively:

| Test | Owner-observed result | Result |
| --- | --- | --- |
| Stay in the completing session | No sound and no banner | Pass |
| Send a prompt, return Home before completion | Banner, sound, badge 1 | Pass |
| Open that unread session, return Home | Badge gone | Pass |
| Send in A, switch to B, keep Strappy foregrounded | A's completion produces banner and sound | Pass |
| Leave A unread, send in B, return Home before completion | Banner, sound, badge 2 | Pass |
| Open A, return Home | Badge drops from 2 to 1 | Pass |
| Open B, return Home | Remaining badge clears | Pass |

These results validate per-session alert eligibility, background observation
cleanup, unread-session counting, and acknowledgment on opening a session.
The badge follows unread sessions rather than app-wide activity. Opening always
queues the absolute count, even if unchanged; the provider handles this separately
from visible bulletin delivery. Observation is deliberately ephemeral, while
unread state and pending badge updates survive daemon restarts in SQLite.

The production acknowledgment tests explicitly confirmed badge changes. The
absence of sound/banner for isolated badge-only commands was confirmed in the
earlier experiment below. Repeated answers in one unread session, failures,
deletion, disconnect/expiry, and lost ACK/restart recovery remain covered by the
automated harnesses; they were not separately exercised in this visual session.
The final owner-observed badge state was cleared.

The new/rollback packages, payload manifests, matching armv7 symbols, installer
logs, readiness logs and crash inventories are retained locally under
`build/device-runs/gomadango-attention-f37bf03/`. Remote installation artifacts
are under `/var/mobile/tmp_altivec/gomadango-attention-f37bf03/`. These generated
artifacts are ignored by Git; the production code is committed as `f37bf03`.

## Test 1: foreground notification delivery

Used the already-staged sender targeting production `receiver.sock`, through the
installed native provider, without rebuilding/replacing production. The owner
confirmed Strappy was foregrounded and authorized each test. Test 21 was sent at
16:52:41 and test 22 at 16:53:11 device time, SpringBoard PID 4179. The owner
confirmed **banner and sound for both sends while Strappy remained active**.
The first send's log recorded sound 1009 attachment and publication return.

Conclusion: this iOS 6 provider can display an alert while Strappy is active.
We can base suppression on our per-session observation policy. This does not
bypass silent mode, DND or notification settings. The owner also confirmed the
production migration's real-prompt notification worked before this experiment.

## Test 2: badge update without an alert

Prepared a removable `com.altivecintelligence.strappy.badge-probe` package via
`source/iOS/badge_probe.mk`. Its SpringBoard-only test dylib uses the same
runtime-checked icon-model `setBadge:` route validated previously, but has no
BulletinBoard publication, sound, launchd job or automatic badge change.
It neither replaces the production provider nor changes its packet protocol.

Explicit diagnostic actions are `set8`, `clear` and `restore`. The first `set8`
saves the prior badge; `clear` passes nil; `restore` restores the saved value.
All badge work runs on SpringBoard's main thread. Darwin messages are a fixed
experiment interface, not a proposed production protocol. Readback is logged;
visible changes and absence of a banner/sound require owner observation.

Install with dpkg and restart SpringBoard once. After readiness, ask the owner
to watch Home Screen and trigger `set8`; verify badge 8 without a banner or sound.
Then test clear, restore the original value, remove only this test package with
dpkg and restart SpringBoard again. Preserve production fingerprints and logs.
If SpringBoard restarts unexpectedly before restore, use the recorded baseline
to recover the badge explicitly; the saved object is memory-only.

The package SHA-256 is
`f4170460ad15ffcfb8278826eb45d22db4d5beb33ee6b0853372166237daff4d`.
It was installed in staging `/var/mobile/tmp_altivec/strappy-badge-only-v1/`;
SpringBoard restarted as PID 4260. The helper logged readiness without changing
the badge, and production provider readiness followed normally.

The owner confirmed Home Screen and baseline badge 4. At 16:56:26, `set8` logged
baseline 4 and readback 8. **The owner confirmed badge 8 with no banner or sound.**
This establishes independent badge updates using the icon-model bridge on this
iOS 6 phone. At 16:56:43, `clear` logged readback none; **the owner confirmed
that the badge disappeared without a banner or sound**. At 16:58:07, `restore`
logged readback 4. The helper was then removed with dpkg and SpringBoard restarted
to unload it. The production provider registered in new SpringBoard PID 4355
and passed a live socket connection check; no new crash reports appeared.
Restoration has API readback evidence, not a separate owner visual confirmation. Production files were verified against the original installed
manifest; no production binaries or settings were changed by this experiment.
At the time of that experiment, unread tracking was not yet implemented.

Clean probe builds passed for armv7/arm64, along with static analysis and DEB
validation. Full clean production builds passed for iOS armv7/arm64 and macOS
PPC/i386/x86_64/arm64; package validation passed, with no compiler warnings.
The probe initially had an analyzer lifetime warning; its process-lifetime
receiver now has an explicit static owner, and the final analyzer run was clean.
Logs use `build/badge-only-build.log` and `build/badge-plan-production-*.log`.
Matching test package, armv7 symbols, native logs and production checks are retained
in `build/device-runs/gomadango-badge-only-v1/`. Native visual results apply to
gomadango (iOS 6.1.3), not other OS versions.

These results establish both prerequisites for the simpler design: badge changes
can be separate from alerts, and foreground Strappy does not inherently suppress
the provider's banners. The implementation above uses these results for session observation and the
independent production badge-update protocol.
