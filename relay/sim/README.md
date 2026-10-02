# Local BLE laboratory

Run the current relay stream implementation without connecting a Switch, accessing
Bluetooth or launching an emulator. Use this to compare scheduling, batching and
retry changes against repeatable congestion conditions. It is a transport simulator,
not a Nintendo OS/radio emulator. A local pass cannot establish a successful trade.

## Run on this Mac

From the repository root:

```sh
make ble-lab-test HOST_CC=clang
make ble-lab HOST_CC=clang LAB_ARGS='--profile ble41 --output build/ble-lab/run.json'
python3 relay/sim/benchmark.py
```

The benchmark writes `build/ble-lab/sweep/REPORT.md` and per-run JSON. Sixty seconds
of simulated traffic takes much less than sixty seconds of wall-clock time.
Only a C compiler and Python 3 are required; no extra Python packages.
`make test HOST_CC=clang` separately runs the existing native sanitizer tests.
The ctypes library uses warnings-as-errors but is not sanitizer-instrumented.

Example experiments (build the library first):

```sh
# Claude's proposed baseline, then 1.5x load, with asymmetric estimated sizes
python3 relay/sim/ble_lab.py --profile ble41 --set duration_ms=60000
python3 relay/sim/ble_lab.py --profile ble41 --set duration_ms=60000 --set rate_switch=93 --set rate_apple=93
# Suspected Switch last-event snapshot loss vs a FIFO with equally slow polling
python3 relay/sim/ble_lab.py --profile snapshot
python3 relay/sim/ble_lab.py --profile slow-callback
# Fewer usable fragments per event or larger negotiated application frame
python3 relay/sim/ble_lab.py --set shared_fragments=8
python3 relay/sim/ble_lab.py --set frame_bytes=182 --set batch_bytes=158 --set packet_apple_bytes=130
# Change submission pacing independently in either direction
python3 relay/sim/ble_lab.py --set pace_switch_ms=5 --set pace_apple_ms=5
```

All parameters in the output `config` can be overridden with `--set KEY=INTEGER`.
Use 0/1 for boolean settings. Profiles: `control`, `ble41`, `slow-callback`,
`snapshot`, `overload`, `loss`, `stall`. The `control` profile deliberately provides
unrealistic spare capacity to isolate software correctness. The `ble41` name describes
its assumed 27-byte payload, **not a calibrated model of your OLED**.

## What runs for real

`stream_bridge.c` compiles the repository's `relay_stream.c` and `relay_codec.c`.
Both simulated peers use its current eight-frame window, cumulative acknowledgements,
ACK-based 150–1000 ms no-progress retry timer, fragmentation, receiver assembly and 24-message queue.
Prepared frames are committed only after the simulated platform accepts them.
Changing the actual stream code changes this experiment after rebuilding the library.
Every delivered synthetic packet is checked for corruption and duplicate delivery.

The Python harness supplies estimated-size RL_UDP/RL_SEND-shaped records and batch
framing. Payloads contain sequence/timestamp test data, not real Pia commands.
Batch packing/scheduling is model code, not the Objective-C or Switch application.
Stream headers, standalone acknowledgements and retransmitted frames consume the
same shared fragment budget as data. Each ATT value adds 3 ATT and 4 L2CAP bytes
and is split into `ceil((value_bytes + 7) / ll_payload)` data fragments.

## Model assumptions and observability

- Both directions share `shared_fragments` data fragments per connection event;
  budgets are not independent one-way throughput figures. Empty link-layer ACKs,
  inter-frame gaps, radio headers, encryption and coexistence are abstracted into
  that effective budget. The model does **not** calculate a PHY maximum.
- Frames may span events. A fair alternating scheduler allocates fragments; actual
  controller scheduling is unknown. Completions happen at event boundaries, so
  sub-event radio timing is not represented.
- Platform submission queues and receive callback queues are bounded. A full
  submission queue backpressures without committing. Callback overflow drops a
  whole frame. `drop_every` injects post-link application/driver loss, not RF loss.
- `switch_snapshot` replaces unread receive events with the latest value. This is
  a hypothesis based on the last-event API, not proof of actual driver loss.
  `poll_switch_ms` models callback delay independently; `stall_ms` suspends Switch
  receive polling only. The native worker's event scheduling is not executed.
- Batch flushing is 10 ms and submission pacing 15 ms by default. The extra native
  application queues, CoreBluetooth, Nintendo services and OS thread scheduling
  are not emulated. Receiver application consumption is immediate.
- JSON contains generated/accepted/dropped/delivered counts, delivery p50/p95/p99/max,
  native retry/gap counters, queue peaks, callback loss and one-second queue samples.
  Latency includes batch and queue waiting. Endpoint latency describes packets
  **received there**; generation/drop counters describe packets originating there.
  `undelivered_accepted` is measured after the drain period. Dropped packets have no
  latency sample: always read loss and latency together.

## Evidence versus estimates

The successful USB run's sampled receive counters rose from 200 to 10,100 between
21:56:06 and 21:58:45 (about 62.26/s). This is an observed average for that session,
not its peak rate or necessarily unique, retransmission-free game traffic.
See `build/hardware-run-06/USB-RESULT.md` and archived logs when available.

Default 62/s **in both directions**, 120-byte inbound UDP and 160-byte outbound Pia
payloads are a scenario based on the supplied review. Only the incoming average is
supported by the sampled run; neither the outgoing rate nor these size distributions
is measured here. Adding the existing 10-byte relay envelope makes records 130 and
170 bytes; batches add 3 bytes plus 2 per record, and stream headers add 24 per frame.
The current 500-byte stream frame therefore has 476 application bytes, not 484.
There is no complete timestamped packet trace in this lab.

Connection interval, usable data fragments/event, controller queues and callback
behavior remain unmeasured. No profile is fitted to a 609 ms echo: a serialized echo
is not a direct measurement of maximum link capacity.

## How to iterate

1. Keep one profile/config fixed as a regression case; save its JSON.
2. Change stream code or a single scheduling assumption, rebuild, rerun tests and sweep.
3. Require no losses, bounded backlog and p95 <100 ms at 62/s and 93/s for the desired
   hypothetical configuration. Check asymmetric sizes and callback stalls too.
4. Retain failures as regression scenarios; do not increase the assumed radio budget
   just to make a code change pass.
5. Later calibrate with a short hardware capture: actual MTU/interval, both directional
   size/rate distributions, accepted submissions, callback spacing and loss. Then
   validate the winning change on the OLED. Local iteration can remove most device
   cycles, but cannot eliminate that final radio/driver and trading validation.

This lab has no legacy-mode negotiation/fallback, Pia reliable-window behavior,
LDN session, radio capture or UI. Those need separate tests. In particular, the older
review's stop-and-wait and missing outbound batching describe an earlier build;
this lab exercises the current stream implementation.

## Asymmetric window and scheduling comparisons

`python3 relay/sim/tune.py` writes `build/ble-lab/tuning/REPORT.md` and 24 raw runs.
`window_switch` and `window_apple` cap new outstanding data frames independently
(1..8). ACK-only frames and retries remain active: limiting data to one is not a
complete notification-credit protocol and does not eliminate snapshot overwrites.
`batch_switch_ms` and `batch_apple_ms` independently vary accumulation delay; a
value of 10 inherits the older `batch_ms` setting for backwards compatibility.
These knobs affect the model only, not deployed code.
