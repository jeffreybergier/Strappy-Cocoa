# Shared Keychain migration

The native credential facade now uses AltivecCocoa's `AIKeychainStore`.
Build against an Altivec engine containing this class and its public header;
the earlier published image alone does not contain this new implementation.
Existing public app APIs and credential names remain unchanged. No saved
credentials are renamed or rewritten just by launching the app.

`XPKeychain` remains an app policy facade. Internet credentials retain the
legacy account-only discovery and URL attributes. Same-scope edits update in
place; URL changes retain the existing delete-and-replace policy. Mac deletion
removes the first match; iOS iterates account matches with a 128-attempt race
bound. Unknown native protocols/authentication types fail without rewriting
credentials. Environment overrides, caching and login behavior remain app-owned.

The daemon explicitly links static AltivecCocoa. Its process-wide Mac
no-dialog configuration and iOS generic-password device-only creation policy
are preserved. OAuth serialization and provider/account schemas are unchanged.

## Credential diagnostics

The daemon logs Keychain operation failures with the `[StrappyKeychain]` prefix
using `NSLog`. Each native failure includes a fixed operation name, numeric
OSStatus and whether NSError was present; no NSError description, account,
service, URL, credential bytes or token is logged. Missing-item failures are
included: they can be expected during first-time saves and idempotent deletes,
so a logged failure alone does not establish a regression.

Successful generic-password writes perform one diagnostic read-back and log
only readability and byte equality. Read-back failure does not change the
write result, retry, delete or rewrite the item. Credential decoding separately
logs invalid size, format or bearer schema without including stored contents.
The existing noninteractive daemon policy remains unchanged.

On Leopard, inspect the daemon's system log from Terminal:

```sh
syslog -k Sender strappyd | grep '\[StrappyKeychain\]'
```

Use the GUI login session to reproduce Save. An SSH credential probe may have
a different authorization context and is not proof of GUI-session behavior.
Launch the diagnostic app after quitting the old app so its normal service
controller can deploy the matching bundled daemon; copying the app alone does
not replace the running service. No Keychain reset or ACL changes are required
for collecting these diagnostics.

Validation on 2026-10-15: clean Mac/iOS debug and release builds, the app's
complete offline suite, and isolated consumer-facade tests on the host Mac
and `x4-vm` (Tiger 10.4.11/PPC) passed. Shared tests are in the sibling engine's
`libs/cocoa/tests/test_keychain_consumers{,_cross}.sh`; they exercise production
facades using disposable Keychains. rCloud's existing-item ACL-edit call is
recorded to avoid an interactive native authorization prompt. Its actual ACL
edit authorization is still unverified; its storage operations are native.
`koolphone5` was unreachable, so phone runtime tests were skipped with user
approval. Cross-build success is not proof of iPhone runtime behavior.

No installed app or existing user credentials were changed by validation.
The engine's `docs/keychain.md` records API choice, source reduction and full
coverage limits. No image was rebuilt or published for this migration.
