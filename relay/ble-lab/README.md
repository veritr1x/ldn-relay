# Physical Mac–iPhone BLE test

**Historical laboratory:** the central in this directory predates the 0.5.0
Switch-approval wire format. Its procedures and results below require the
matching historical companion. Do not use it to qualify a current 0.5.0 app
until its handshake is adapted; the current generic Mac companion is built by
`relay/mac/build.py`.

Mac is CoreBluetooth central; iPhone runs the existing LRTransport peripheral.
This bypasses Nintendo's driver and does not validate Switch BLE performance.
No UDP, LDN, game state or Switch connection is needed. USB is used only to install
and launch the iPhone app; benchmark records travel through CoreBluetooth.

Build:

```sh
python3 relay/ble-lab/build.py
python3 relay/ios/build.py --profile /path/to/development.mobileprovision
```

Install and open the signed iOS app, allow Bluetooth and keep it foreground. Close
other apps advertising the relay service to avoid connecting to the wrong peer.
The central logs the chosen peripheral identifier. Run one test at a time:

```sh
'build/ble-central/BLE Central Lab.app/Contents/MacOS/BLECentral' 62 "$PWD/build/ble-central/run-62.json" > build/ble-central/run-62.log 2>&1
'build/ble-central/BLE Central Lab.app/Contents/MacOS/BLECentral' 93 "$PWD/build/ble-central/run-93.json" > build/ble-central/run-93.log 2>&1
```

60 s generation plus up to 15 s drain. The central emits 130-byte numbered test
records, packs batches with a 20 ms maximum wait and submits at most one stream
frame each 15 ms. The iPhone echoes using the production LRTransport benchmark
path. Sequence, timestamp and payload checks detect loss, duplication and corruption.
The JSON reports application drops, unreturned accepted records, native transport
retries, queue peak and RTT percentiles. The stream window is eight.

This is a simultaneous outbound/return **echo workload**, not independently generated
asymmetric game traffic. RTT includes central batching and both directions; it is
not a one-way latency measurement. A slowed return stream can reduce delivered
return traffic even while the central continues to offer load. Read counts alongside
latency; `completed` means the run finished, not that it met performance criteria.
A basic target is zero dropped/missing/corrupt/duplicate records and p95 RTT <100 ms.

The Mac/iPhone may negotiate larger link-layer packets or different PHY/intervals
than Switch. Negotiated ATT capacity is logged; PHY, interval and RF packet counts
are not measured here. A test failure can reflect this client's 15 ms pacing,
batching or peripheral software, not necessarily radio capacity.

## Repeatable physical window sweep

The central accepts optional positional `PACE_MS BATCH_WAIT_MS WINDOW` arguments
after its rate and result path. The iPhone build accepts `--ble-pace` and
`--ble-window`. The sender window is independent in each direction and defaults
to eight; the protocol and maximum receive window remain unchanged.

```sh
python3 relay/ble-lab/build.py
python3 relay/ble-lab/sweep.py --profile /path/to/development.mobileprovision --device YOUR_IPHONE_UDID --prefix experiment1 --windows 4,5,6 --rate 93
python3 relay/ble-lab/report.py
```

The sweep installs and launches a configured iPhone companion for each run. It
uses zero submission pacing and zero fixed accumulation delay, forming batches
when stream-window space is available. Keep the iPhone unlocked and connected
for deployment. Output names must be new; existing result JSON is never replaced.
The Mac client refuses to start if another benchmark instance owns its lock.
Each deployment and source/binary hash set is recorded alongside the result.

### Compact endpoint hardware check

Build the iPhone relay with `--compact-loopback --ble-pace 0 --ble-window 6`,
install and foreground it, then run the central with environment variable
`LDN_COMPACT_TEST=1`. The ordinary CLI arguments remain unchanged.
The central advertises compact capability, checks the enable handshake, and
sends synthetic UDP records through the production compact encoder/decoder.
The iPhone diagnostic loopback returns SEND records through the same codec.
All four socket slots are exercised; addresses change every 100 records.
The central verifies reconstructed addresses, ports, sequence and payload.
This is physical BLE with synthetic endpoints, not an LDN or trading test.

Diagnostic loopback must be absent from normal builds: rebuild without
`--compact-loopback` and reinstall after testing. Do not leave this option
active when connecting to a real Switch. Run without `LDN_COMPACT_TEST` for
an ordinary echo control; historical controls are not equivalent RF conditions.

### Control the hardware conditions

Keep the iPhone relay foreground and unlocked. Record its `APP STATE` lines
and the Mac's default audio devices before comparing runs. In the October 1
physical tests, the during-call ordinary echo delivered 1535/3720 at 62/s with
4398 ms p95. After the call, with built-in microphone/speakers selected (AirPods
still connected), an unchanged six-window/zero-pacing run delivered 3720/3720,
zero drops/retries, median 64 ms and p95 127 ms. This supports audio contention
as a contributor; it is not an isolated RF proof or a Switch measurement.
Those captures are private development records and are not included here. Keep
failed runs when comparing changes, and distinguish connection failures from
completed traffic tests.

### Independent bidirectional size-sample test

`make_game_load.py` extracts size-only samples from the archived game logs.
`experiment.py --duplex --ack 5 --name UNIQUE` installs the explicit diagnostic
build and runs independent 62/s generators for 60 seconds in both directions.
This does not echo each data packet: the iPhone generates its own sequence.
It reports per-direction counts, byte totals, drops, duplicates and corruption.
A separate 15-byte probe runs once per second and reports its RTT; that metric
is not one-way game latency. Reports drain for five seconds. Add
`--duplex-out-size 170` to test a larger fixed Apple-to-host record size.

The host size samples came from every ~100th receive in a successful USB trade.
The outgoing samples came from an older failed BLE session; congestion and old
packetization can bias them. The generators assume uniform pacing at the
observed ~62/s host rate in both directions. There is no millisecond-resolution
successful-trade capture, so this is a traffic approximation, not exact replay.
The heavier outbound test guards against trusting the small outgoing samples.
All record sizes include a conservative ten-byte relay header; compact endpoint
encoding is tested separately. No actual Pia payloads, ROMs or saves are embedded.
Always reinstall a build without `--duplex`/`--compact-loopback` afterward.
