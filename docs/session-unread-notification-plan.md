# Session unread state, observation and badges

Design, implementation and device experiments, 2026-10-18. The policy below is
implemented for the iOS app and daemon. macOS retains its existing app-owned
notification presentation policy.

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
The device tests do not establish visible banner/sound or app navigation behavior:
the owner is away. End-to-end UI and visual checks on gomadango remain for tonight.
No production installation on gomadango was attempted during implementation.

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
