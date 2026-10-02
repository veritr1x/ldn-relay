# Wire protocol: 0.5.0

This is a reference for companion authors. The Apple `LRTransport` implementation
handles BLE setup and framing; most Apple clients only need the message commands.
See [CONTRIBUTING.md](../CONTRIBUTING.md) for the usual connection sequence.

Unless stated otherwise, integers are little-endian. IPv4 addresses and UDP ports
use network byte order. Limits are 2,048 bytes per complete relay message,
1,400 bytes per UDP payload, four sockets and 24 cached scan records.

## BLE setup

The Switch is the BLE central; the companion is the peripheral.

- Service: `7B61238D-028A-4F65-99D8-7C8F22A11D4E`
- Data characteristic: `7B61238D-028A-4F65-99D8-7C8F22A11D4F`
- The Switch requests ATT MTU 512 and caps each characteristic value at 500 bytes.

After a fresh local approval, `LRO5` / `LOA5` establishes a connection-specific
envelope. Its format and counters are in [PAIRING.md](../PAIRING.md). The 28-byte
envelope leaves at most 472 bytes for an inner frame. This is not authentication
or encryption. Old LRH1/LRH2 and keyed-v4 sessions are not fallback options.

Inside that envelope, the Switch sends a 12-byte stream request:
`LRH3(4), nonce:u32, frame_limit:u16, version:u16=2`.
The companion replies with `LRA3`, the same nonce, an accepted limit no greater
than requested (minimum 64), and version 2. The Switch enables notifications and
sends `LRN1` with the same fields; the companion echoes them as `LRNA` through a
notification. Stream traffic starts only after that probe succeeds, then the
Switch sends CAPS.

## Stream frames

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 2 | ASCII `LR` |
| 2 | 1 | Version 2 |
| 3 | 1 | 0 = acknowledgement only, 1 = data fragment |
| 4 | 2 | Fragment sequence; nonzero for data |
| 6 | 2 | Last accepted peer sequence, initially zero |
| 8 | 2 | Offset within the complete message |
| 10 | 2 | Complete message size |
| 12 | 2 | Fragment payload size |
| 14 | 2 | CRC-16/CCITT |
| 16 | 4 | Nonce from stream negotiation |
| 20 | 1 | Receive credit: current sender advertises 0 or 8 |
| 21 | 3 | Zero reserved bytes |
| 24 | variable | Fragment payload |

CRC uses initial value 0xffff, polynomial 0x1021 and no final XOR. It covers the
whole frame except bytes 14–15. ACK-only frames have zero sequence, offset, total
size and payload size. Data sequences start at 1 and wrap from 65535 to 1.

Each direction has its own sequence space and cumulative acknowledgements. The
codec supports up to eight unacknowledged frames; the current Switch and generic
Apple defaults limit the send window to three. Zero credit blocks new data,
but permits retries and acknowledgements. The receiver accepts fragments in order
and does not deliver a duplicate message twice.

A prepared frame is committed only after the platform accepts it. Blocked sends
are rebuilt with the latest acknowledgement. Retries replay unacknowledged frames
from the gap onward. The timeout adapts from a 150 ms minimum to measured ACK
delay, with additional backoff up to 2 seconds after retries. See `relay_stream.c`
for the timer and sequence rules; its tests cover loss, wraparound and backpressure.

The Switch writes at least 5 ms apart; the generic Apple companion spaces
notifications by 15 ms. These are application timers, not radio intervals. A BLE
worker runs separately from Switch LDN/UDP handling. The Switch reads one GATT
event snapshot per signal, rather than treating that API as a dequeue operation.

### Notification recovery

After 500 ms without a valid incoming frame, the Switch starts serialized GATT
reads. Stream sequences, partially assembled messages and compact caches survive
this transition. A later `LRN5` notification returns it to notification mode.
The worker fails after 10 seconds without a valid stream frame. Read recovery
is slower than notifications; it has not sustained the tested 62 packets/s load.

## Message envelope


Every complete message starts `opcode:u8, request_id:u16`, then the body below. Responses echo the request ID; unsolicited CAPS, UDP and disconnection events use ID 0. The companion allocates nonzero request IDs. Unsupported or malformed commands receive ERROR when enough envelope bytes exist. All fixed-size bodies must match exactly.

### Commands (phone → Switch)

| Opcode | Name | Body |
| --- | --- | --- |
| 0x01 | SCAN | `protocol:u8`, 1 or 3 |
| 0x02 | JOIN | `generation:u16, index:u8, version:i16, key_length:u8, key:bytes` |
| 0x03 | LEAVE | Empty |
| 0x04 | BIND | `slot:u8, port:u16 network-order` |
| 0x05 | SEND | `slot:u8, destination:4 bytes, port:u16 network-order, payload:bytes` |
| 0x06 | INFO | Empty |
| 0x07 | PING | Arbitrary bytes, up to 2045 |
| 0x08 | STATS | Empty |
| 0x09 | CONFIG | `features:u8`; currently only the BATCH bit is accepted |
| 0x0b | COMPACT_ENABLE | Empty; see compact endpoints below |
| 0x0c | SEND_COMPACT | `slot:u8, payload`; uses a cached endpoint |
| 0x0d | HOST | Session settings; see hosting below |
| 0x0e | ADVERTISE | Raw advertisement bytes, 0–384 bytes |

SCAN requires no current joined session. It closes previous station state, increments the scan generation and requests up to three scans, stopping at the first nonempty valid result set. Non-game entries with zero communication ID or zero nodes are omitted. It returns a cached snapshot; JOIN must use the matching generation and index. Generations are connection-local and wrap as u16.

JOIN requires a cached, accepting session with a free slot. `version=-1` uses the host's advertised local communication version; other values must be nonnegative. Key length is 16–64 bytes. Native product security mode is used. Success requires native StationConnected state and a network ID matching the selection, then successful IPv4 lookup. This creates no Pia/application session.

BIND requires an active joined or hosted session, slot 0–3 and nonzero port. Ports must be unique across slots. Rebinding closes the old slot first, so a bind error leaves that slot closed. Sockets are nonblocking UDP with broadcast enabled. SEND requires a bound slot and nonzero destination port; zero-length UDP payload is allowed. Destinations are restricted to the relay itself, currently connected LDN nodes or the active LDN subnet broadcast address. No general Internet or LAN proxy is provided. IPv4 node addresses are refreshed about once a second.

LEAVE closes sockets and the LDN session in either role. Bluetooth remains connected. INFO returns CONNECTED for a station or HOSTED for a host, using the same metadata layout. PING is an opaque exact echo.

### Responses (Switch → phone)

| Opcode | Name | Body |
| --- | --- | --- |
| 0x80 | CAPS | `version:u8, protocols_bitmap:u8, max_udp:u16, socket_count:u8, features:u8` |
| 0x81 | NETWORKS | `generation:u16, protocol:u8, count:u8`, followed by 18-byte records |
| 0x82 | CONNECTED | Variable session metadata below |
| 0x83 | LEFT | Empty |
| 0x84 | BOUND | `slot:u8, port:u16 network-order` |
| 0x85 | SENT | `slot:u8, payload_size:u16` |
| 0x86 | UDP | `slot:u8, source:4 bytes, port:u16 network-order, payload:bytes` |
| 0x87 | PONG | Exact PING body |
| 0x88 | COUNTERS | `joined:u8, queue_depth:u8, rx_packets:u64, tx_packets:u64, dropped:u64, rx_bytes:u64, tx_bytes:u64` |
| 0x89 | CONFIGURED | Applied `features:u8` |
| 0x8a | BATCH | Length-prefixed complete messages; see batching below |
| 0x8d | COMPACT_READY | Empty |
| 0x8e | UDP_COMPACT | `slot:u8, payload`; uses a cached endpoint |
| 0x8f | HOSTED | Same metadata layout as CONNECTED |
| 0x93 | MEMBERS | Same metadata layout as CONNECTED; unsolicited host membership update |
| 0x94 | ADVERTISED | Empty |
| 0xff | ERROR | `failed_opcode:u8, category:u8, detail:u32` |

CAPS protocol bitmap bit N indicates protocol N; current value 0x0a means 1 and 3. The feature bits are listed below. SENT only confirms that `sendto` accepted the payload; it does not confirm peer receipt. Stream/USB quiet-send mode suppresses successful SENT replies.

Each NETWORKS record is `index:u8, communication_id:u64, scene:u16, host_version:i16, node_count:u8, node_max:u8, channel:i16, acceptance_policy:u8`. The current join path accepts only policy 0 (always accept).

CONNECTED body, in order:

1. `protocol:u8`, local IPv4 (4 network-order bytes), subnet mask (4 network-order bytes).
2. Communication ID `u64`, scene `u16`.
3. SSID length `u8` (0–32), that many raw SSID bytes (no added NUL).
4. Raw 16-byte native session ID.
5. Advertisement length `u16` (0–384), advertisement bytes.
6. Connected node count `u8` (0–8), then 15 bytes per connected node: IPv4 (4 bytes), MAC (6 bytes), node ID `u8`, is-connected `u8`, local version `i16`, platform `u8`.

The payload is built field by field; it is not a serialization of padded libnx structs. Player names are not included. Phone adapters should request INFO again if they need updated node metadata.

ERROR categories: 1 format, 2 state, 3 unsupported, 4 native LDN, 5 socket, 6 queue, 7 stale selection, 8 destination. Detail is context-specific, usually raw native Result or errno; decode it according to category. Control queue exhaustion is currently logged on the Switch and may require retry/reconnect rather than producing another queued error.

## Capabilities

CAPS schema version is 1. Its feature byte uses these independent bits:

| Value | Feature |
| --- | --- |
| 1 | BATCH: batched Switch events |
| 2 | NOTIFY: notification probe succeeded |
| 4 | STREAM: stream-v2 transport |
| 8 | SEND_BATCH: batched companion sends |
| 16 | QUIET_SEND: no successful per-datagram SENT reply |
| 32 | USB transport |
| 64 | COMPACT endpoint headers |
| 128 | HOST: native LDN hosting |

## Hosting

HOST requires no active joined/hosted session and has this body, in order:

| Field | Type / limit |
| --- | --- |
| Protocol | u8, 1 or 3 |
| Communication ID | u64, nonzero |
| Scene | u16 |
| Local communication version | u16, 0–32767 |
| Maximum nodes | u8, 2–8 |
| Passphrase length | u8, 16–64 |
| Advertisement length | u16, 0–384 |
| Passphrase | Bytes of the given length |
| Advertisement | Bytes of the given length |

The Switch creates a native access point in product security mode, with native
automatic channel selection and the display name `LDN Relay`. The passphrase is
part of the game's LDN configuration, not a BLE pairing key. Successful creation
returns HOSTED; failures return ERROR and close the attempted native session.

ADVERTISE replaces the advertisement of an active hosted session and returns
ADVERTISED. Empty advertisements are valid. While hosting, the relay checks
membership about every 50 ms and emits MEMBERS when connection/address changes
are detected. These use request ID zero. INFO returns the current cached
session metadata; player names are not included.

## Batching and queue limits

BATCH contains `length:u16 | complete_message:length` records after its ordinary
three-byte envelope. Nested batches are invalid. Validate the complete batch
before processing any record. Companion-to-Switch batches accept SEND,
SEND_COMPACT and supported diagnostic data records, not arbitrary control commands.
Malformed batches are rejected before any UDP send; a later socket error does not
undo earlier successful sends in that batch.

Stream setup enables event batching. CONFIG can change that setting using the BATCH flag (value 1)
only. Switch BLE batches flush within 10 ms or when full; USB batches flush within
1 ms. Larger individual messages use normal fragmentation. Stream ordering and
duplicate suppression prevent transport retries from repeating accepted batches.

The Switch polls up to four datagrams per socket per main-loop iteration. It drops
oversize or out-of-session source packets and drops whole datagrams when the
outgoing transport depth reaches 20, leaving room for control traffic. The stream
has 24-message queues and additional bounded worker queues. Control and UDP
currently share FIFO ordering, so heavy traffic can delay control replies.

RX counters count valid source/size packets before the queue decision. Drops
include receive errors, invalid sizes/sources and queue pressure. TX counts
successful socket sends, not remote delivery. Counters last for the Switch app
process. B leaves LDN; + closes both transports. Native LDN loss emits LEFT.

## Compact endpoints


BLE stream peers may advertise CAPS bit 64. Apple sends opcode 11 with an empty
body and waits for opcode 0x8d before using compact records. The Switch enables
its sender only after enqueueing that acknowledgement. Older peers never enable
this extension. USB is unchanged.

Full SEND/UDP records seed an independent endpoint cache per direction and
socket. Opcodes 12 (SEND) and 0x8e (UDP) retain the request ID and socket slot:
`opcode:u8, request_id:u16le, slot:u8, payload`. Their cached IPv4 address and
UDP port replace six bytes. Any endpoint change requires another full record.
Caches persist across LDN session operations, following the ordered transport,
and clear on transport reset. Never evict or reset one side independently.

Encoding/decoding is transactional: copy cache state, then commit only when
the corresponding record is accepted into the ordered queue. Whole outbound
batches are validated against a temporary cache before any UDP sends occur.
This feature relies on stream-v2 duplicate suppression and ordered delivery.
It does not compress ciphertext or reduce UDP payload size.

## USB

Pressing X starts libnx usbComms. The Mac helper forwards fixed 4,096-byte pages
to one TCP client on `127.0.0.1:42387`. A page contains `LDU1(4)`, message length
u16, two zero reserved bytes, one complete relay message (3–2,048 bytes), then
zero padding. Receivers reject malformed headers and lengths; TCP readers must
accumulate a whole page. No BLE envelope or stream header is used.

USB uses the same commands and advertises its capabilities in CAPS. It does not
use BLE approval. Restart the relay, helper and companion after cable loss;
automatic USB session resumption is not implemented.

## Diagnostics

BENCH (0x0a), radio/one-way controls and diagnostic reconnect require the local
`/switch/ldn-relay/diagnostics.enabled` flag. Normal builds do not start load tests.

BENCH takes `rate:u16, seconds:u16` (1–1,000/s, 1–120 seconds). It generates
130-byte BENCH_DATA (0x8b) messages for the companion to echo. Their sequence u32
starts at message offset 3 and timestamp u64 at offset 7. After generation and up
to five seconds of draining, BENCH_DONE (0x8c) carries ten u32 values: rate,
generated, enqueued, echoed, dropped, corrupt, mean RTT ms, p95 RTT ms, maximum
RTT ms and stream-mode flag. Histogram p95 saturates at 2,000 ms, meaning at least
that delay. This is an echo benchmark, not independent traffic in both directions.

DIAG_RECONNECT (0x7b) has an empty body. It requests a disconnect and a new attempt
three seconds later, only over BLE with no active LDN session, at most 20 times
per launch. Every new connection still needs local approval.

## Older versions

LRH1 used stop-and-wait framing. LRH2 introduced stream-v2, and keyed builds used
v4 authenticated envelopes. Their code/tests remain for regression work, but a
0.5.0 companion must use the current approval setup. Wire changes require updating
both endpoints; CRCs and public session counters are not authentication.
