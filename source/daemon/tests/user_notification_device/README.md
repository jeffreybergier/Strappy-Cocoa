# Standalone UserNotifications probe

Manual iOS 10+ diagnostic, built against the existing iOS 8.4 SDK through
runtime-checked NSInvocation calls. This executable is not part of Strappy's
production packages and does not read accounts, run prompts, or start a daemon.
Both armv7 and arm64 are built; only iOS 15.0 arm64 has been tested natively.

## Build and run

From the project root:

```sh
make -C source/iOS -f user_notification_probe.mk clean release validate-package \
  BUILD_ROOT="$PWD/build/experiments/pinkphone-un-probe"
```

Copy the resulting `iOS/release/strappy-un-probe/Intermediates/signed/strappy-un-probe`
under that build root to `/var/mobile/tmp_altivec/strappy-un-probe/` on
`pinkphone-rsa`, and run it over SSH as mobile. Use a fresh remote filename for
each revised executable to avoid the previously observed on-device executable
replacement problem. No installation, trust-cache update, or respring is needed.

Commands:

| Command | Effect |
| --- | --- |
| `settings` | Print notification settings for Strappy. |
| `authorize` | Request badge, sound, and alert authorization; changes persistent system permission state. |
| `send` | Submit one immediate notification with the default sound. |
| `send-glass` | Experimental absolute system-file sound; played the default sound in the native test. |
| `badge-one` | Set Strappy's badge to 1, independently of unread sessions. |
| `badge-clear` | Set Strappy's badge to 0, independently of unread sessions. |
| `clear` | Request removal of only this diagnostic's pending/delivered notification. |
| `inspect-sounds` | Print Strappy's data-container URL and runtime sound factory signatures. |

Use badge commands only during a coordinated manual test; they temporarily
override the app's badge rather than deriving it from the database. `clear`
does not clear the badge or revoke authorization. Finish with `clear` and
`badge-clear`; then delete only the staged probe binaries if no longer needed.
Notification authorization remains enabled and can be changed in iOS Settings.

The notification identity is fixed to `com.altivecintelligence.Strappy` and the
request ID to `strappy.manual.un-probe`. No other app identity is accepted.
The staging executable has its own application identifier plus the private
`com.apple.private.usernotifications.bundle-identifiers` array containing only
Strappy. Platform/no-sandbox/no-container entitlements permit this diagnostic
to run in the tested jailbreak environment; this is not an App Store technique.
The test does not establish the minimal necessary entitlement set.

## Pinkphone results, 2026-10-19

Target: iPhone8,4, iOS 15.0 (19A346), rootless Dopamine, SSH as mobile.
Production Strappy/strappyd remain at build `56e1ef0`.

- [x] Inspect Mail's actual `maild` binary: `UNUserNotificationCenter`
  `initWithBundleIdentifier:`, `addNotificationRequest:withCompletionHandler:`,
  and `setBadgeNumber:withCompletionHandler:` are used by its notification/badge
  controllers. Its signed entitlement permits `com.apple.mobilemail`.
- [x] Initialize the same API for Strappy from a separate foreground executable.
- [x] Query initial settings: authorization NotDetermined, alert/sound/badge
  settings NotSupported. Submission nevertheless returned success; callback
  success alone is not presentation evidence.
- [x] Request authorization: granted YES, no error. Subsequent settings report
  Authorized, banner style, and enabled alert/sound/badge/lock-screen/NC settings.
- [x] Owner observes a banner. The initial phone was silenced, so that observation
  is not a sound test and cannot distinguish the first two closely spaced sends.
- [x] Tapping the notification opens Strappy (owner confirmed).
- [x] With Silent Mode off and Strappy backgrounded, owner confirms banner,
  default sound, and badge 1. Each command exits after its callback.
- [x] Owner confirms diagnostic notification removal and badge clearing.
- [x] Test `soundNamed:` with
  `/System/Library/Audio/UISounds/sms-received3.caf`: submission succeeds, but
  owner hears the default sound again. This is a failed custom-sound test.
- [x] LaunchServices reports no Strappy data container on this installation.
- [ ] Prove Glass/custom sound from a supported resource location. Apple's
  [soundNamed: documentation](https://developer.apple.com/documentation/usernotifications/unnotificationsound/init(named:)?language=objc)
  specifies app/group `Library/Sounds` or the app bundle. No production app
  files were modified for the absolute-path test.
- [x] Owner confirms lock-screen presentation with default sound using the
  final probe build.
- [ ] Check behavior across respring/reboot with this modern route.
- [ ] Integrate only after review: production permission setup, runtime backend
  selection, unread badge reconciliation, observed-session suppression, and
  failure/retry handling still belong to future work.

Raw local artifacts: `build/experiments/pinkphone-un-probe/` (callback logs and
build products) and `build/research/pinkphone-mail-ios15/` (Mail extraction and
pseudocode). These are ignored, not shipped or committed. The production legacy
provider's receiver remains unavailable on pinkphone; this independent probe
does not repair or use it.
