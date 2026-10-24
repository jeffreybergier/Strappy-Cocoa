# Mac startup readiness

The Mac app shows a buttonless `NSAlert` with an indeterminate spinner while
`StrappyServiceController` installs/loads the daemon in the background.
`StrappyStartupController` opens the conversation window only after:

1. Installation reports success.
2. The IPC monitor reports its authenticated, compatible handshake is ready.
3. The session-store reader validates the database.

The ordinary main run loop keeps running. A 100 ms timer checks the monitor's
thread-safe readiness flag; it performs no IPC itself. A separate 20-second
startup timer includes both installation and handshake time. Installation or
validation failure, or expiry, stops the spinner and displays one Quit button.
Late completions cannot dismiss that failure. A disconnect during validation
keeps waiting within the original deadline.

Reopen, Show Main Window, and Preferences cannot bypass startup. Teardown
invalidates timers and detaches the borrowed delegate before release. Installer
errors are marked handled so they do not also enter the generic error queue.
The existing service-stop lifecycle still runs when the app quits.

The ordinary alert button's reserved space holds the spinner; this does not
require the newer accessory-view API. `XP_layoutSingleButtonAlert` uses native
layout when available and builds the single-button content with public views
on Tiger, whose alert nib otherwise exposes unused controls outside a modal
presentation. Timers run in explicit default
and event-tracking modes, using `currentRunLoop` on the main thread. Neither
`NSRunLoopCommonModes` nor `+[NSRunLoop mainRunLoop]` is available on Tiger.

## Native regression fixture

Build all four Mac slices inside the project's Altivec environment:

```sh
make -C source/daemon/tests -f native.mk PLATFORM=macOS \
  TEST_PRODUCTS=startup-probe debug
```

Stage `build/tests/macOS/daemon/debug/startup-probe/startup-probe` below
`~/Desktop` on a Mac with a logged-in graphical session, then run it via SSH.
The fixture exercises the production controller and native AppKit alert/timers,
but records service installation, readiness, store validation, and Quit. It
does not load a daemon or access the user's database or Keychain. It takes
about 22 seconds and must print PASS; a crash or nonzero exit is a failure.

Coverage includes:

- Hidden/disabled Quit and indeterminate spinner while waiting; no visible
  placeholder, help, or suppression buttons.
- Installation and handshake completing in either order.
- Exactly one successful completion, with the startup alert hidden.
- Installation and database-validation errors.
- Disconnect during validation and later readiness.
- The actual 20-second timer, plus timeout after installation.
- Quit action, late replies, duplicate starts, and teardown.

The fixture passed natively on x4-vm (10.4.11 PowerPC) and x9-vm
(10.9.5 x86_64), both as an executable and wrapped in a foreground app bundle
for visual checks. The rebuilt app also passed a normal launch on x9-vm.
The older installed service and user data on x4-vm were left untouched.
These are startup tests, not live-provider or notification-delivery tests.
