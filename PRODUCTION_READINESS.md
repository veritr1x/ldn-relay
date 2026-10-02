# Test results and known issues

0.5.0 is a preview. The basic connection path works, and a tester has confirmed
a Pokémon trade with the separate mGBA companion. Reconnects, interruptions and
performance still need more testing on the current version.

See [COMPATIBILITY.md](COMPATIBILITY.md) for devices and versions, and
[TODO.md](TODO.md) for the remaining work.

## Current version: 0.5.0

| Check | Result |
| --- | --- |
| Approval, setup and LDN scan | Passed on OLED Switch and iPhone 15 Pro; initial exit was clean, with no recorded read/write errors or retries |
| Denied approval | Three attempts stopped before a relay handshake. The log combines rejection and timeout, so it cannot tell which caused each attempt to end |
| Cleanup after denial | Two clean disconnects. The third returned `0x0005528f (143-681)`; both GATT paths were then unregistered and the app exited. The error is unresolved |
| Trade | Tester confirmed a completed trade. No new packet capture or independent inspection of both saves after reload was collected |
| Save-preserving app update | All 41 existing Documents files, including 19 saves, matched their backups immediately after the update |
| Local and CI checks | Sanitizer tests, simulated BLE faults, Python build guards, Mac build and pinned-toolchain NRO packaging pass |

Approval requires a fresh A press on every connection. Local tests cover held
buttons, rejection, the 60-second timeout, stale frames and framing limits.
Separate hardware results for each of those cases are still needed. The current
protocol uses no pairing key and provides no cryptographic authentication or encryption.

## Earlier transport tests

These results help explain the current design. They used older builds and do
not establish the same behavior on every 0.5.0 companion.

| Build | Test | Result |
| --- | --- | --- |
| 0.4.3 | Permanent notification rejection, 62 packets/s | Failed: read/write contention; 454 of 3,720 planned echoes returned |
| 0.4.4 | Permanent rejection with serialized read recovery, 62/s | 1,771 accepted and echoed; 1,949 rejected at admission. No corruption or Switch write errors, but the offered load was not sustained |
| 0.4.5 | Five-second notification stall, 10/s for 60 seconds | 600/600 echoes, no drops, corruption or duplicates. Notification mode resumed; RTT p95 98 ms, maximum 582 ms |
| 0.4.6 | Foreground reconnects, interrupted by backgrounding | Failed after 14 authenticated sessions; notification setup failed on the next connection |
| 0.4.7 | Controlled foreground reconnects | Passed 20 reconnects and 21 authenticated sessions. Every 1,024-byte echo matched; RTT 503.4–573.5 ms |

The 0.4.5 and 0.4.7 tests had no active LDN game session. Connection-interval
requests succeeded, but the negotiated radio interval was not measured.
The recovery transition in 0.4.5 retried five rejected writes without packet loss.

The generic companion uses a three-frame window and 15 ms notification pacing.
The game companion uses 5 ms pacing. Results from one setting do not establish
throughput or recovery at the other. Earlier working game builds also had multiple
reported completed trades.

## Before a stable release

- Resolve the denial cleanup error and distinguish timeout from rejection in logs.
- Test fresh approval on reconnect, unexpected peer loss, backgrounding, screen
  lock, suspend/resume and exit during connection. Test cable removal for USB.
- Measure simultaneous game traffic, queue age, loss and latency on the supported
  setup, including recovery from notification stalls. The 30-minute load test
  remains deferred and has not passed.
- Verify both saved parties after reload and a clean return to gameplay after trading.
- Review parser bounds, queue pressure, session ownership and cleanup paths.
- Try installation on a clean setup using only the published instructions.
- Publish a matching companion, tagged NRO, checksums and rollback instructions.

Full native LDN API coverage is not required for a supported UDP release.
