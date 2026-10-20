# Notification merge checklist

Reviewed 2026-10-20 for `experiment/ios-sandbox-ipc` → `main`.
This is the single task list for the notification work. Investigation documents
retain technical findings, reproduction procedures and dated evidence. Checking
an item requires an observed result or an explicit, documented scope decision;
this consolidation does not establish any new device acceptance.

## Before merge

- [ ] Complete pinkphone-rsa production acceptance on the merge candidate:
  Glass sound with Silent Mode off, unread badges 2 → 1 → cleared, observed-session
  suppression, foreground alerts for another session, lock-screen/tap behavior,
  notification permission changes and recovery, and respring survival with fresh
  delivery afterward. Record actual observations separately from callback success
  and standalone-probe results. Production delivery is already owner-confirmed;
  the individual cases are not all established.
  [Evidence](docs/session-unread-notification-plan.md#modern-daemon-integration-2026-10-19)
- [ ] Confirm fresh koolphone5 production notification, sound and badge clearing
  on `8a0f436` (or the merge candidate if production code changes). Its full
  acceptance passed on `ad47a44`; package/readiness checks alone do not repeat it.
  [Device matrix](docs/session-unread-notification-plan.md#latest-production-deployments-2026-10-19)
- [x] Replace the iOS-version gate with runtime capability selection: attempt
  UserNotifications first, then the legacy BulletinBoard provider (including
  iOS 5 when its required APIs work), otherwise deliver no notifications.
  Validate required signatures/initialization before modern authorization;
  never switch routes after denial, timeout or an ambiguous submission result.
  Remove the provider's iOS 6 loader restriction. Native iOS 5 behavior remains
  unverified; cross-builds and fake-API fixtures do not establish it.
  [Implementation and validation](docs/session-unread-notification-plan.md#runtime-capability-fallback-2026-10-20)
- [ ] Resolve the existing uncommitted diagnostic extensions in
  `source/daemon/tests/user_notification_device/main.m`,
  `source/iOS/user_notification_probe.mk`, and
  `source/daemon/tests/user_notification_device/generate_sound.py`: validate and
  commit them separately, or exclude them and align the probe instructions with
  the retained code. Keep the documented tone/cleanup commands reproducible.
  Follow the repository's clean-build requirements for retained changes.
- [ ] Clear the diagnostic notification using `probe-tones-v3 clear` and clear
  only the diagnostic tone override using `probe-tones-v3 tone-reset` from
  `/var/mobile/tmp_altivec/strappy-un-probe/` on pinkphone.
  Do not reset the production `com.altivecintelligence.Strappy.completion` topic.
- [ ] Remove `com.altivecintelligence.strappy.sound-probe` through dpkg and verify
  fixture removal. It owns `StrappyManualChime.wav` in the recipient app bundle
  and the diagnostic helper; do not manually remove production package files.
- [ ] Remove obsolete owned staged sound-test bundles/binaries when no longer
  needed, retaining useful logs and evidence.
- [ ] Repeat production Glass delivery after sound-fixture removal to establish
  that the successful tone route does not depend on the failed WAV experiment.
  [Fixture state and technical details](source/daemon/tests/user_notification_device/README.md#diagnostic-fixture-state)

## Explicit compatibility deferrals

These are coverage limits, not established failures. Resolve their scope in the
merge notes; do not silently promote them to verified support.

- [ ] Record the supported/tested notification OS matrix and explicitly defer
  untested versions, or perform production visual acceptance on additional
  versions intended to be claimed as verified.
- [ ] Record modern reboot/first-unlock/jailbreak-reactivation behavior as
  unverified, or validate it in a separately authorized device window.
  **pinkphone-rsa remains respring-only; reboot requires explicit owner
  authorization.** A respring result does not establish reboot behavior.
- [ ] Reconcile stale notification-related statements in the older root
  `PLAN.md` with the dated iOS 6/8/15 evidence, preserving unrelated daemon
  lifecycle gaps. Its broader daemon acceptance work predates this branch and
  is not automatically a notification merge blocker.

## Optional research, not merge blockers

- [ ] Investigate pinkphone legacy-provider automatic discovery only if that
  route is needed; the working modern backend supersedes its earlier failure.
- [ ] Resolve arbitrary custom sound-file delivery only if custom sounds are
  wanted. Retest the legacy WAV with Silent Mode off; modern helper/app-bundle
  WAV experiments played the default sound. Built-in Glass is already confirmed.
- [ ] Trace private provider threading/teardown and optional cleared-info
  callback contracts if deeper API evidence is needed. Observable lifecycle
  tests and bounded fixtures do not prove every private contract.
- [ ] If pursuing IPA conversion, establish an enforcing sandbox, narrow shared
  store/socket access, and full app-flow behavior. The original probe did not
  establish sandbox enforcement; DEB notifications do not require this conversion.

## Existing validation evidence

The recorded production implementation passed the offline Linux suite, clean
armv7/arm64 iOS and PPC/i386/x86_64/arm64 macOS builds, and rootful/rootless package
checks. The modern recording fixture passed on macOS 10.9; this is not native iOS
presentation evidence. iOS analysis reported an existing possible null source in
`strappy_client.c:1230`, with no diagnostics in the new bridge. See
[implementation validation](docs/session-unread-notification-plan.md#modern-daemon-integration-2026-10-19).

The production migration, unread/badge integration, iOS 6/8 visual acceptance,
modern production delivery and startup-warning fix are implemented and recorded.
The investigation's old handoff milestones are historical, not additional tasks.
