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
| `send-custom` | Request `StrappyManualChime.wav`; both tested bundle locations fell back to default sound. |
| `send-tone-glass` | Select Glass for the isolated test topic and submit using Mail's tone-selection API; owner confirmed Glass. |
| `tone-reset` | Remove the test topic's tone override; does not change Mail's account or global tone settings. |
| `badge-one` | Set Strappy's badge to 1, independently of unread sessions. |
| `badge-clear` | Set Strappy's badge to 0, independently of unread sessions. |
| `clear` | Request removal of only this diagnostic's pending/delivered notification. |
| `inspect-sounds` | Print Strappy's data-container URL and runtime sound factory signatures. |
| `inspect-tones` | Print ToneLibrary method signatures and the name/path resolved for `texttone:Glass`. |

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
- [x] Test an original three-note PCM WAV in the sending helper's bundle:
  helper resolves the resource, submission succeeds, owner hears default sound.
- [x] Test the WAV in the recipient Strappy app bundle, installed by a separate
  removable fixture package: submission succeeds, owner hears default sound.
  These failures do not establish that all bundled custom sounds are unsupported.
- [x] Prove Glass using Mail's alert-type/topic mechanism: owner explicitly
  confirms "it worked! it was glass!" See the exact sequence below.
- [ ] Optional: resolve arbitrary sound-file delivery. Apple's
  [soundNamed: documentation](https://developer.apple.com/documentation/usernotifications/unnotificationsound/init(named:)?language=objc)
  specifies app/group `Library/Sounds` or the app bundle. The successful Glass
  path selects a built-in tone rather than providing a file to `soundNamed:`.
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

## Successful Glass technique, 2026-10-19

Native confirmation is on pinkphone's iOS 15.0 arm64/rootless installation only.
The probe posts under Strappy's app identity using the already authorized
`UNUserNotificationCenter`; strappyd has not been changed or redeployed.

Disassembly of `maild`'s
`MFUserNotificationSoundController addSoundIfNecessaryForNotificationContent:suppressionContexts:`
(entry `0x1000bb74c`) shows a call to `soundWithAlertType:5`, followed by
`mutableCopy`, `setAlertTopic:` with the selected mail account ID, and `setSound:`
on the notification content. The tone-setting call found in
`MFAccountsRemovalService _deleteAccount:` (`0x10005a1c0`) passes nil to
`setCurrentToneIdentifier:forAlertType:topic:` for type 5 and that account's
topic, clearing its override. This is cleanup evidence, not a discovered Mail
sound-picker implementation.

The working probe uses this equivalent sequence (illustrative Objective-C;
the actual probe checks private selectors and invokes them through NSInvocation):

```objectivec
NSString *topic = @"com.altivecintelligence.Strappy.manual-probe";
TLToneManager *manager = [TLToneManager sharedToneManager];
[manager setCurrentToneIdentifier:@"texttone:Glass"
                    forAlertType:5
                           topic:topic];
id sound = [[UNNotificationSound soundWithAlertType:5] mutableCopy];
[sound setAlertTopic:topic];
[content setSound:sound];
[sound release];
// Submit content in a UNNotificationRequest through Strappy's center.
```

Identifier case matters: ToneLibrary's own `TL.strings` contains
`texttone:Glass`. Runtime `nameForToneIdentifier:` returns `Glass`, and
`filePathForToneIdentifier:` returns
`/System/Library/Audio/UISounds/sms-received3.caf`. The earlier lowercase candidate
did not resolve to Glass and was not used to send a tone-selected notification.
The successful run reads back `texttone:Glass` for the diagnostic topic and
receives a successful submission callback; owner listening confirms playback.

This uses a Strappy-only topic, not a real mail account or the unscoped global
tone setter. It does not change Mail's chosen sound, bypass Silent Mode, or play
audio separately. No sound file is supplied by this successful code path. The
failed WAV fixture was still installed during the successful test, so repeat
after fixture removal to independently confirm that installation is irrelevant.

This is a promising mechanism for user-selectable built-in tones. Only Glass
has been audibly verified; arbitrary imported tones, a picker UI, persistence
across restart, and the minimal required entitlements remain untested.

Evidence under `build/experiments/pinkphone-un-sound/`:
`sender-bundle.log`, `recipient-bundle.log`, `tone-glass-correct-info.log`, and
`tone-glass-send.log`. Mail pseudocode is in
`build/research/pinkphone-mail-ios15/maild-sound-selection-pseudocode.txt` and
`maild-tone-reset-pseudocode.txt`.

## Paused device state and cleanup

At the owner's pause after the successful test, the following remain pending.
This documentation update does not resume device tests or perform cleanup.

- [ ] Clear the diagnostic notification with `probe-tones-v3 clear`.
- [ ] Clear the diagnostic topic override with `probe-tones-v3 tone-reset`.
  The staged executable is under `/var/mobile/tmp_altivec/strappy-un-probe/`.
- [ ] Remove `com.altivecintelligence.strappy.sound-probe` through dpkg.
  That fixture owns the uniquely named
  `/var/jb/Applications/Strappy.app/StrappyManualChime.wav` and
  `/var/jb/usr/libexec/strappy-un-probe`; it does not replace Strappy's executable.
- [ ] Verify production package-owned files against
  `build/experiments/pinkphone-un-sound/production-before.sha256`.
- [ ] Remove obsolete staged sound-test bundles/binaries when no longer needed.
- [ ] Finish validation and commit the currently uncommitted probe extensions
  and WAV generator separately from this documentation update.

Normal Strappy notification authorization remains enabled. Integration into
strappyd and the production sound-selection policy remain future work.
