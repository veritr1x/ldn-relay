# Compatibility and measured evidence

Current protocol: 0.5.0 local Switch approval, UDP only. This is a preview,
not a guarantee across every console, game or operating system.

| Component | Recorded setup | Evidence |
| --- | --- | --- |
| Relay console | Modified OLED Switch, HOS 22.5.0, Album/Applet Mode | Approval, stream setup, native scan and clean exit captured on 0.5.0 |
| Peer console | Stock Switch 2 | User reports a working trade with the current 0.5.0 setup |
| Phone | iPhone 15 Pro, iOS 27.0 (24A437) | Device inventory and approval/control-path capture |
| Generic companion | LDN Relay 0.5.0, window 3, pacing 15 ms | Connection and scan verified; no game implementation in this app |
| Game companion | mGBA LDN 0.5.0, window 3, pacing 5 ms | User-confirmed working Pokémon trade; separate game project |
| Switch toolchain | libnx 4.12.0, devkitA64 r29.2, pinned devkitPro container | Build and sanitizer/simulation checks; CI is not radio qualification |
| Atmosphere | Exact running version not captured for this test | Do not infer it from an SD file or an older installation |
| Other Switch models, phones, iPads, OS versions | Not qualified for 0.5.0 | Build minimums are not a tested-device list |
| Mac BLE / USB | Local builds supported | Earlier development evidence exists; current-version full hardware qualification pending |

Only publish configurations actually exercised. Firmware minimum 20, iOS minimum
17 and Mac minimum 14 are build/runtime requirements, not a support claim for
all versions at or above those values. Keep player names, device IDs and serial
numbers out of this matrix.

## Evidence boundaries

- Current 0.5.0: local approval, control traffic and a user-confirmed trade.
- Denied-approval hardware exercise: three attempts returned from the prompt
  without a relay handshake. The user was asked to reject and let the next prompt
  expire; the log uses a combined rejection/timeout message, so it cannot
  independently identify the cause of each attempt. Two disconnected cleanly.
  The last reported a native disconnect error, then unregistered paths and exited.
- Earlier 0.4.7: 20 controlled foreground reconnects using the keyed protocol.
  This does not qualify 0.5.0 reconnects.
- Earlier 0.4.5: a five-second notification stall recovered at 10 packets/s.
  Permanent read recovery did not sustain the offered 62 packets/s workload.
- The 30-minute load test is deferred. No new sustained-throughput claim is made.
- Both parties after save/reload, unexpected loss, screen lock/backgrounding,
  suspend/resume and USB removal still need explicit current-version results.

See [PRODUCTION_READINESS.md](PRODUCTION_READINESS.md) for chronological details.
