# Session unread state, observation and badges

Design discussion and device experiments, 2026-10-18. This is a plan, not an
implemented database or notification policy.

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

## Reliability details still to settle

The daemon should serialize opening/acknowledgment and completion updates through
its existing database writer, so a later completion is not erased by a stale
read/modify/write. The UI sends session-open intent, not a copied boolean value.
Unread state persists in SQLite. Derived badge updates should send the absolute
session count, including zero; no increment/decrement guessing.

A durable `isBeingObserved=true` can become stale after an app crash, kill or
suspension. Prefer connection-owned observation cleared on disconnect, with a
bounded expiry if needed for suspended connections. These are implementation
options, not yet agreed heartbeat requirements. On daemon restart, observation
must be reestablished by the app rather than trusting a leftover true value.
The main lifecycle distinction is which session is observed; app backgrounding
invalidates that observation, while app activity alone grants no global exemption.

Decide whether failed prompts count as new answers/unread completions. Existing
notifications include failures, and exclude cancellation and Database Study.
Decide whether clearing Notification Center changes unread state (proposal: it
only dismisses the alert; opening the session acknowledges the answer).
Session deletion must remove its contribution to the badge. Existing sessions'
initial unread state and the schema change need an explicit implementation plan;
no schema/user_version change is made by these experiments.

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
No unread tracking or production badge policy is implemented.

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
the provider's banners. Next implement session unread/observation state and the
production badge-update protocol after settling the remaining policy choices.
