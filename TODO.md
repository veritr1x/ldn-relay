# Roadmap

The first release should make the existing UDP relay easy to install and reliable
on a documented set of devices. [Test results](PRODUCTION_READINESS.md) explain
what already works and where the remaining issues came from.

## Before a stable release

- [ ] Resolve disconnect error 143-681 after denied approval; log rejection and
  timeout separately, and test reconnecting afterward.
- [ ] Check held-button prevention and fresh approval on each reconnect on hardware.
- [ ] Improve connection/status/error screens, including version mismatch guidance.
- [ ] Test peer loss, backgrounding, screen lock, suspend/resume and exit during connection.
- [ ] Test USB reconnects and cable removal before promising USB support.
- [ ] Measure game-load recovery at the game companion's actual pacing.
- [ ] Run sustained load qualification when scheduled. The 30-minute run is deferred.
- [ ] Verify both saved parties after reload and a clean return to gameplay after trading.
- [ ] Review and fuzz parsers, queue pressure, session ownership and cleanup paths.
- [ ] Check diagnostics stay off in normal builds and logs remain bounded.
- [ ] Record the running Atmosphere version and finish the supported-device matrix.
- [ ] Test a clean installation using only the public instructions.
- [ ] Distribute a matching game companion separately; the generic tool cannot play games.
- [ ] Publish a release tag, NRO, checksums and notices from a passing CI build.

Installation, signing, rollback, troubleshooting and contributor guides are in
place. CI builds the NRO and generic Mac app and runs transport/privacy checks.
Current approval/setup/scan and a tester-confirmed trade are recorded in the test results.

## Later: more of the LDN API

These additions need capability negotiation, bounded errors and tests on hardware
that supports them. They do not need to hold up the existing UDP relay.

- [ ] Discovery filters and more useful session metadata before joining.
- [ ] Host acceptance policies, player rejection, names and private sessions.
- [ ] Membership events, disconnect reasons and consistent session generations.
- [ ] Socket close, configurable UDP limits/options and multicast investigation.
- [ ] TCP connect/listen/accept/read/write/shutdown/close with backpressure.
- [ ] Separate scheduling for control, time-sensitive UDP and bulk traffic.
- [ ] A versioned companion SDK shared by BLE and USB, with small examples.
- [ ] Multi-peer and multi-socket interoperability tests.
- [ ] A companion picker when more than one nearby app advertises.
- [ ] Adapt the historical Mac BLE test central to the current approval protocol.
- [ ] Investigate additional native LDN operations where firmware permissions allow them.

Pia/RFU and other game protocols remain companion work. Adding relay commands
alone cannot make every game compatible or remove BLE bandwidth limits.
