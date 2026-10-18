# Session unread state, observation and badges

Design, implementation and device experiments, 2026-10-18. The policy below is
implemented for the iOS app and daemon. macOS retains its existing app-owned
notification presentation policy. Production build `f37bf03` is installed on
gomadango, where the owner confirmed the core end-to-end behavior below.

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
presents the notification and badge. The earlier visual tests recorded here validated the
notification policy with the old background task present; actual suspended-app
completion remains a separate follow-up device test after this change.

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
per sequential page in this run). The row appearance still needs visual
confirmation on the phone after installing this build.

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
