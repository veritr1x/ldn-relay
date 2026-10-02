# Release and capability backlog

Status: local 0.5.0 preview. This checklist does not claim that the current
working tree is published or that every companion has been qualified.
See [production readiness](PRODUCTION_READINESS.md) for measured results and
[Switch approval](PAIRING.md) for the current security model.

## First supported release: current UDP relay

Full native LDN API coverage is not required to release the documented UDP
subset. Publish precise limits and only claim tested configurations.

### Evidence already collected

- [x] Switch display author is `veritrix`; public Git identity is `veritr1x`.
- [x] Source/package privacy checks, notices, checksums and source manifests exist.
- [x] Earlier keyed transport was implemented and tested; superseded by local
  Switch approval at the user's request. It is not the current security model.
- [x] Low-rate recovery passed a five-second injected notification stall:
  600/600 echoes at 10/s, without drops, corruption or duplicates.
- [x] Foreground BLE reconnect qualification passed: 20 reconnects,
  21 authenticated sessions, exact 1,024-byte echoes with 15 ms companion pacing.
- [x] User reported multiple completed trades on the previous working game build.
- [x] User confirmed a working trade on the current key-free 0.5.0 setup.

### P0: installation and ordinary use

- [ ] Publish a compatible companion alongside the NRO, with an explicit version
  compatibility table. Distinguish the generic relay tool from a game adapter.
- [x] Document a repeatable iPhone installation/signing route and Mac installation
  instructions, including updates, expiry where applicable, and rollback.
  Do not distribute personal provisioning profiles, keys, ROMs or saves.
- [x] Hardware-check initial key-free approval, stream setup, LDN scan and clean exit.
- [x] Capture denial before relay setup: three attempts ended without a handshake.
- [ ] Separately identify B rejection versus timeout in hardware logs; verify
  held-button prevention and fresh approval on reconnect. The final denied
  attempt returned native disconnect error 143-681; investigate and recheck cleanup.
- [ ] Provide clear missing companion, approval timeout and version mismatch errors.
  A companion picker is optional later work; initially document one advertiser.
- [ ] Finish normal connection/status/error screens and recovery instructions.
  Automatic benchmarks and reconnect/fault injection must remain off.
- [ ] Verify clean installation on a setup without this development workspace,
  including permissions, dependency installation, approval and a real session.

### P0: correctness and release qualification

- [x] Complete a trade on the new approval-mode game companion: user reports
  "trade works" after the 0.5.0 test instructions.
- [ ] Separately record both saved parties after reloading and clean return to
  gameplay; the brief trade confirmation does not document these details.
- [ ] Measure supported game traffic and recovery at the actual companion pacing;
  the game currently uses 5 ms, while the reconnect test used 15 ms.
- [ ] Test unexpected peer loss, foreground/background and lock behavior,
  suspend/resume, failed discovery, exit during connection and resource cleanup.
- [ ] Qualify USB reconnect/cable removal if USB is part of the supported release.
- [ ] Review approval/session ownership and cleanup. Document that local approval
  provides neither cryptographic identity verification nor encryption.
- [ ] Review/fuzz command parsing, queue pressure and socket/control error paths;
  retain regression tests for every fixed failure.
- [x] Document the recorded hardware, firmware, launch mode, OS and companion
  versions in COMPATIBILITY.md, with evidence limits stated plainly.
- [ ] Complete the matrix's missing running Atmosphere version and qualify the
  advertised configurations; build targets are not tested configurations.
- [ ] Sustained load qualification remains deferred by user request. The
  30-minute test must not run without renewed authorization and is not passed.

### P0: publication and maintenance

- [x] Review public source scope, attribution and package contents; retain private
  captures, profiles, saves and device artifacts outside the source commit.
- [ ] Complete release review of the preview changes before tagging a stable build.
- [ ] Run CI on the exact release commit, then publish a matching immutable tag,
  public NRO download, checksums, notices, source/toolchain manifest and changelog.
- [ ] Publish companion downloads or reproducible installation instructions,
  version mismatch guidance, save-preserving updates and rollback steps.
- [x] Add troubleshooting, a privacy-safe issue template and a security-reporting
  route. Hide automatic/benchmark controls in normal Apple builds.
- [ ] Finish review of experimental commands/logging in normal builds.

## Later: broader LDN support

These are capability-expansion tasks, not prerequisites for releasing the
documented UDP subset. Each item needs capability negotiation, tests and an
explicit unsupported result where the firmware or service denies access.

- [ ] Complete discovery: scan filters and full useful metadata/advertisements
  available before joining; avoid exposing native structs as an unstable wire ABI.
- [ ] Session controls: acceptance policies, allow/block lists, player rejection,
  configurable names and private-network operations where available.
- [ ] Event reporting: native state changes, membership changes in both roles,
  disconnect reasons and consistent session generations after reconnect.
- [ ] UDP expansion: explicit socket close, configurable resource limits,
  negotiated datagram sizes and supported socket options; separately investigate
  multicast instead of promising support without a native test.
- [ ] TCP forwarding: connect/listen/accept/read/write/shutdown/close, partial I/O,
  bounded buffers, cancellation, error propagation and per-socket backpressure.
- [ ] Independent scheduling for control, latency-sensitive UDP and bulk streams,
  with queue-age budgets so one connection cannot starve another.
- [ ] Versioned asynchronous companion SDK shared by BLE and USB, with documented
  capabilities and examples that do not contain game-specific credentials.
- [ ] Multi-peer/multi-socket interoperability and failure tests. Report API
  implementation separately from hardware-qualified compatibility.
- [ ] Optional advanced LDN action frames and operating-mode controls, subject
  to service/firmware permissions; this is not arbitrary raw Wi-Fi access.

Game protocols such as Pia/RFU remain companion responsibilities. More relay
commands cannot remove BLE capacity limits or establish universal game support.
