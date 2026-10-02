# Production release gates

Status: **preview; do not tag 1.0 yet**. Source cleanup and CI do not substitute
for a measured console qualification run. These gates apply to the generic relay;
game-specific correctness and save integrity also require companion validation.
The actionable [release checklist and capability backlog](TODO.md) tracks work
remaining. Full native LDN API coverage is a later milestone; the first supported
release can explicitly cover the existing UDP subset.

## Completed in the 0.4.2 preparation

- Switch NACP and startup attribution use `veritrix`.
- Personal device/profile defaults removed from the physical test runner.
- Build path remapping; privacy checks for source and packaged executable.
- Switch SD logging disabled by default, opt-in files bounded to two 256 KiB logs.
- Neutral install/log location `/switch/ldn-relay/` and companion wording.
- Package checks for all localized NACP authors, version/tag consistency and bounds.
- Source digest manifest, dirty-source disclosure and checksums for package files.
- CI privacy checks and deterministic fault simulation alongside sanitizer tests.
- Third-party legal notices preserved.

## Blocking release gates

| Area | Required evidence |
| --- | --- |
| BLE liveness | Reproduce and fix sustained `updateValue` rejection/notification stalls on real Switch↔iPhone. Prove recovery without silent loss, duplicate delivery or manual restart. 0.4.4 serialized GATT read recovery delivered every accepted packet in the injected permanent-stall test with zero write errors; offered 62/s overloaded that fallback. 0.4.5 returned to notifications automatically after a five-second injected stall: 600/600 echoes at 10/s, no drops/corruption, p95 RTT 98 ms, max 582 ms. This is not a game-load qualification. |
| Load and latency | Run independent simultaneous traffic with measured packet sizes for at least 30 minutes, including LDN hosting and joining. Record accepted/delivered/lost bytes, queue age, p50/p95/p99 latency and negotiated transport mode. No growing queue or unexplained loss at the supported rate; demonstrate at least 1.5× headroom or document a lower limit. |
| Lifecycle | At least 20 connect/leave/reconnect cycles for BLE and USB; peer disappearance, app backgrounding, screen lock, link loss, cable removal, suspend/resume, missing companion, full SD and exit during discovery. No leaked handles, stuck Wi-Fi or unrecoverable state. |
| Companion approval | User selected key-free local Switch approval for 0.5.0. Verify fresh-button approval, rejection, timeout, exit, held-button prevention and approval on every reconnect. No cryptographic identity or encryption claim. Earlier PSK tests below are historical. |
| Protocol robustness | Fuzz command parsers and state transitions; verify queue limits, malformed batches, stale membership, invalid lengths, command flooding and cleanup under sanitizer/fault tests. Review socket and control-response failure paths. |
| Diagnostic isolation | Gate benchmark/raw radio tuning behind explicit local opt-in or a separate diagnostic build. Keep normal-use UI and logging bounded. Verify any default interval policy on every supported hardware/firmware combination. |
| End-to-end correctness | For each claimed game/companion pair, repeat a complete session and clean exit. For trading, verify both saved parties and absence of Bad Eggs after returning to normal gameplay. The iPhone adapter's extra-save-barrier fix is separate from this relay. |
| Compatibility | Publish exact tested Switch models, system firmware, Atmosphere/libnx, launch mode, phone/Mac OS and companion versions. Explicitly label untested combinations. |
| Distribution | Clean reviewed source commit, exact-head CI green, immutable release tag matching NACP, stable public NRO asset plus checksums/notices, rollback instructions and an archived source/toolchain manifest. Do not publish a dirty local build as a reproducible stable release. |

## Before calling it 1.0

1. Finish hardware qualification and review of the implemented BLE recovery and
   approval/session design; freeze the supported wire protocol before release.
2. Complete the hardware matrix with logs retained privately and publish aggregate results.
3. Define stable protocol negotiation and compatibility guarantees for companions.
4. Review the accumulated experimental code; retain only supported defaults and APIs.
5. Prepare minimal user-facing connection/error screens, companion distribution,
   troubleshooting guidance and a security-reporting channel.
6. Review dependency licences/provenance for the exact linked build and publish a
   release with checksums, changelog and tested rollback path.

No claim is made that a preview package satisfies the unchecked gates above.

## User-reported game verification

The user reports multiple completed trades with the working game build. This
fulfills repeated trade verification for that tested setup; the new recovery
and pairing builds still need their own transport and reconnect qualification.

The 30-minute load run was explicitly deferred by the user. Do not label it
passed, failed, or completed. Version 0.4.6 prepares automated authenticated
reconnect checks, with load generation disabled in the installed test companion.

## Hardware results collected during hardening

| Build / scenario | Observed result | Meaning |
| --- | --- | --- |
| 0.4.3, forced permanent notification rejection, 62/s | 454 of 3,720 planned echoes returned before report; read/write contention | Failed; superseded by serialized recovery. |
| 0.4.4, paired, forced permanent rejection, 62/s | 1,771 accepted and echoed, 1,949 admission drops, zero corruption; Switch reports zero write errors | Same-session delivery works at accepted rate; offered load fails. |
| 0.4.5, paired, five-second forced rejection, 10/s for 60 s | 600 planned/accepted/echoed, zero drops/corruption/duplicates, RTT mean 69 ms, p95 98 ms, max 582 ms; notification mode resumed | Low-rate transient recovery passed on hardware. No LDN session was active. |

The tests used an OLED modified Switch and iPhone 15 Pro in the foreground.
Connection interval requests succeeded; the actual negotiated interval was not
measured. The 0.4.5 read-to-notification transition recorded five rejected writes,
which were retried without packet loss. Do not describe every platform write as
accepted or the whole stall period as sub-100 ms.

### Reconnect run status

Version 0.4.6 completed 14 authenticated sessions with exact 1,024-byte echoes
(initial connection plus 13 completed reconnects). The run was interrupted by
backgrounding after session 8 and resumed in the foreground. The next connection
authenticated but disconnected during notification setup before session 15 could
complete. That 0.4.6 run did not pass; the corrected 0.4.7 run below passed. Some resumed echoes took seconds
and needed stream recovery; exact payload delivery does not imply low latency. The installed companion has both automatic load and soak disabled.

The matching mGBA companion 0.4.9 is installed and its pairing-key import file
is provisioned. All 38 pre-existing Documents files, including 18 saves, matched
their pre-install hashes. Launch was blocked because the iPhone was locked;
Keychain import and a trade on this paired build are not yet verified. The
working 0.4.8 app and save backup remain available for rollback. The game keeps
its previous 5 ms notification pacing; the 15 ms reconnect qualification below
does not qualify that game's throughput. Older relay NROs are backed up locally.

Version 0.4.7 prevents stream heartbeats before the notification probe has been
observed. A regression test using the actual stream codec and a last-event
snapshot fails with the old unrestricted pump and passes with the negotiation
gate. The Switch now waits for a matching nonce/capacity/version response and
allows up to three probe attempts. This addresses a source-confirmed race;
the captured 0.4.6 failure identified probe setup, but did not log enough to
prove which GATT value displaced its reply. Hardware rerun passed as recorded below.


### 0.4.7 foreground BLE reconnect qualification

The OLED Switch and iPhone 15 Pro completed 20 consecutive controlled reconnects
(21 authenticated sessions) without manual intervention. Every session returned
an exact 1,024-byte echo; RTT ranged from 503.4 to 573.5 ms, median 538.5 ms.
The Switch log confirms all 21 authentications, all 20 reconnect requests and a
clean exit. No probe failure was recorded. Both logs are archived privately.

The companion used a three-frame window and 15 ms notification pacing, changed
from 5 ms in the preceding run. This result qualifies that configuration; it
does not isolate pacing from the probe fix or prove game-load latency. No LDN
session was active. Background/lock, unexpected link loss, USB lifecycle and
sustained load remain unqualified. The declined 30-minute test was not run.
The normal generic companion has automatic reconnect/load/fault tests disabled.


### 0.5.0 approval mode (current, connection verified and trade user-confirmed)

At the user's request, both endpoints no longer load or require a pairing key.
The Switch asks for a fresh A press after discovering/connecting to a companion
and before exchanging relay setup or commands. B rejects, + exits and the prompt
expires after 60 seconds. Approval is not remembered across connections.
Session framing is deliberately non-cryptographic; this is not authenticated BLE.

Local sanitizer tests cover the approval gate, timeout, rejection, stale session
frames, direction/size limits and fragmented bidirectional traffic with loss.
The earlier 0.4.7 paired reconnect result does not qualify this new wire format.
The initial approval/control-path check passed. The denied-approval exercise
below adds hardware evidence; distinct rejection/timeout causes, held-button
prevention and approved reconnects still need separately attributable results. A current-build trade is now
user-confirmed as described below.
No 30-minute load test is authorized. See TODO.md for release work.


The installed 0.5.0 iPhone companion completed setup after user approval and
received a native LDN scan response over stream-v2 (472-byte inner frame).
The matching Switch log confirms the approval, successful handshake and scan,
zero recorded retries/read errors/write errors, and clean LDN/BLE cleanup on exit.
That initial log contains no rejection event; the previous SD log belongs to
0.4.3. The later denied-approval exercise is recorded separately below. This is initial connection/control-path evidence, not a new trade,
throughput result or full lifecycle qualification. The game update preserved all
41 existing Documents files, including 19 saves, verified against fresh backups.


After installing the 0.5.0 relay and game companion and receiving the new-build
trade instructions, the user reported "trade works". Record this as a successful
user-confirmed trade on the current key-free setup. This report is separate from
the earlier connection-only logs; no new trade capture or save inspection was
collected. Both saved parties after reload and clean return to normal gameplay
were not separately described in the short confirmation. It does not qualify
other games, interruption recovery or sustained throughput.


### 0.5.0 denied-approval exercise

The subsequent Switch capture records three connections returning from the local
approval prompt without exchanging a relay handshake. The operator was asked to
reject with B and then let a prompt expire. The current combined message does not
distinguish rejection from timeout, and the capture contains three attempts, so
it cannot independently attribute each outcome to a particular action or duration.

The first two attempts disconnected and unregistered both GATT paths successfully.
The third returned `0x0005528f (143-681)` from `btdevDisconnectFromGattServer`, then
successfully unregistered both paths and reached normal app exit. The reason for
that native error remains unresolved; do not call the complete cleanup exercise
passed. Distinct denial-reason logging and a targeted cleanup/reconnect check
remain before full lifecycle qualification. No load test ran.

### Public installation preparation

Installation, version compatibility, rollback, troubleshooting and privacy-safe
issue/security reporting instructions are included. The generic iPhone builder
accepts a caller-selected bundle ID and compatible app-specific or wildcard
development profile; it never distributes a developer's profile. Normal Apple
builds use window 3, 15 ms pacing and hide benchmark controls. The game companion
is a separate project and is not packaged here. A clean-user installation and
current-version lifecycle qualification are still pending.
