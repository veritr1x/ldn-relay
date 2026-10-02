# Changelog

## 0.5.0 preview

This preview contains accumulated development changes since the initial public
relay. It does not claim complete LDN API coverage or production qualification.

- Require a fresh physical Switch approval for every BLE connection. No pairing
  key import is needed. Approval does not cryptographically authenticate peers
  or encrypt traffic; older paired companions must be updated together.
- Add bounded stream windows, bidirectional batches, compact packet headers,
  notification readiness handling and same-session serialized read recovery.
- Add native LDN hosting, companion-supplied session configuration and USB framing
  with a Mac helper. Game protocols remain in the companion.
- Add sanitizer regressions, modeled BLE fault tests and explicit local opt-in
  for Switch benchmark/radio commands. Hide benchmarks in normal Apple builds.
- Add privacy checks, author/version validation, checksums, source manifests and
  pinned-toolchain CI packaging. Switch app attribution is veritrix.
- Document installation, developer signing, version compatibility, rollback,
  troubleshooting and privacy-safe reporting.

A key-free trade is user-confirmed. Earlier versions passed a low-rate stall
recovery check and controlled foreground reconnects; these do not qualify all
current configurations. The latest denied-approval capture blocked all three
attempts before relay setup, but its last disconnect returned native error
143-681. See COMPATIBILITY.md and PRODUCTION_READINESS.md for outstanding gates.
The 30-minute load test remains deferred.
