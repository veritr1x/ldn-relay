# LDN Relay wire protocol

Current 0.5.0 peers require [local Switch approval](#current-approval-transport-050)
and the LRO5/LOA5 session setup. The older negotiation formats below are historical
references, not fallback paths available to the current BLE runtime.

## Streaming transport (historical relay 0.2.3)

New peers first negotiate `LRH2` / `LRA2`, using the same 12-byte handshake layout below with version 2 and a minimum frame limit of 64 bytes. A nonce-matched `LRN1` / `LRNA` notification probe is required. Failure during negotiation retries legacy v1 explicitly; an active v2 stream never silently changes to the read/write path.

`common/relay_stream.c` uses an independent eight-frame window in each direction, cumulative acknowledgements, go-back-N retries after 150 ms without acknowledgement progress, and receiver backpressure. Frame offsets 0–15 retain the layout below, except version is 2. Bytes 16–19 carry the session nonce, byte 20 advertises a window limit of 0 or 8, bytes 21–23 are zero, and payload starts at byte 24. CRC includes the extended header, excluding only bytes 14–15. A frame is committed only after the platform accepts it; blocked notifications are rebuilt with current acknowledgements. Zero credit prevents new frames but permits retries and acknowledgement heartbeats.

CAPS feature bits are BATCH=1, NOTIFY=2, STREAM=4, SEND_BATCH=8 and QUIET_SEND=16. The CAPS schema version remains 1. V2 batches both directions: Apple-to-Switch batches contain SEND or benchmark-data messages only and are validated completely before application. Successful SEND replies are suppressed; errors remain. Switch UDP batching flushes within 10 ms, or when full. Control and UDP messages retain FIFO order.

The Switch runs BLE on a worker thread, separate from LDN/UDP. It reads one last-event snapshot per GATT signal; the API is not treated as a dequeue operation. That version paced writes and notifications at least 15 ms apart. Current defaults are documented in the 0.5.0 section below. Apple uses a dedicated CoreBluetooth queue and bounded main-thread delivery. Logging is buffered on a separate queue. Stream telemetry records queue depth, retries, event draining and acknowledgement latency; these are not radio connection-interval measurements.

BENCH (0x0a) body is `rate:u16, seconds:u16`, valid for 1–1000 packets/s and 1–120 seconds. It requires stream-v2. The Switch generates 130-byte messages, representing a 120-byte payload plus relay envelope budget; the Apple BLE queue echoes them. BENCH_DATA (0x8b) carries sequence:u32 at offset 3 and Switch timestamp:u64 at offset 7. BENCH_DONE (0x8c) contains ten u32 values after the envelope: rate, generated, enqueued, echoed, dropped, corrupt, mean RTT ms, p95 RTT ms, maximum RTT ms, stream-mode flag. The p95 histogram saturates at 2000 ms: a value of 2000 means at least 2000 ms. The offered duration is followed by up to five seconds of drain time. This measures paced outbound traffic and its echo, not independent traffic generators or game processing.

Local ASan/UBSan tests cover fragmentation, loss, duplicates, corruption, backpressure, nonce isolation and wraparound. The 150/300 packets/s simulations assume three transfers per direction every 15 ms with 15 ms delivery delay; they establish software behavior under that model, not hardware capability. Physical BLE and gameplay acceptance remain separate tests.

## Legacy v1 reference

The following describes the compatibility transport and message schema. Its stop-and-wait scheduling does not apply to stream-v2.

The modified Switch is the BLE central and native LDN station. The companion (iPhone or Mac) is a CoreBluetooth peripheral. All use the framing codec in `common/relay_codec.c`. References to the phone below also apply to the Mac companion; it uses the identical wire protocol. All multi-byte integers are little-endian **except IPv4 addresses and UDP ports**, which are network order. Limits: 500-byte frame, 2,048-byte complete message, 1,400-byte UDP payload, 24 queued messages per endpoint, four UDP sockets, 24 scan records.

## BLE connection and reset

Service UUID: `7B61238D-028A-4F65-99D8-7C8F22A11D4E`.
Read/write-with-response characteristic: `7B61238D-028A-4F65-99D8-7C8F22A11D4F`.
These differ from the historical probe UUIDs, so the old app cannot accidentally accept relay frames.

The Switch requests ATT MTU 512, reads the negotiated value and caps the frame limit at `min(MTU - 3, 500)`. If MTU lookup fails, it uses 20 bytes. The 12-byte initial handshake fits the default ATT MTU. Each exchange is one characteristic write with response followed by one characteristic read; notifications are not used in v1.

Handshake request: ASCII `LRH1`, nonzero random `nonce:u32`, `frame_limit:u16` (20–500), `version:u16` (1).
Response: ASCII `LRA1`, same nonce, accepted frame limit (no larger than requested), version 1.

A new nonce resets all phone codec state and application session state. Repeating the same nonce and limit from the same BLE central returns the same acknowledgement without resetting queues. A different active central is rejected until the existing link has been idle for 30 seconds. The nonce separates sessions; **it is not cryptographic peer authentication**. CRC detects frame damage and does not authenticate peers. This prototype uses an unencrypted custom GATT characteristic as in the validated probe.

After the Switch validates the handshake it initializes its codec and queues CAPS. The phone enables controls after CAPS. The phone freezes a read snapshot for nonzero ATT offsets. No commands are accepted before a valid handshake.

## Fragment frame

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | ASCII `LR` |
| 2 | 1 | Version 1 |
| 3 | 1 | Flag: 0 = acknowledgement only, 1 = data fragment |
| 4 | 2 | Sequence number, nonzero for data; zero for acknowledgement only |
| 6 | 2 | Last accepted peer sequence, initially zero |
| 8 | 2 | Offset in complete message |
| 10 | 2 | Complete message size |
| 12 | 2 | Fragment payload size |
| 14 | 2 | CRC-16/CCITT, initial 0xffff, polynomial 0x1021, no final XOR |
| 16 | variable | Fragment bytes |

CRC covers the entire frame in order, excluding bytes 14 and 15. Acknowledgement-only frames have zero sequence, offset, total size and payload size. Data sequence numbers start at 1 and wrap from 65535 to 1. Each direction has an independent sequence space. A sender retains and repeats a fragment until its sequence is acknowledged. Piggybacked acknowledgements may change between retransmissions, so CRC is recomputed. Only an actually emitted fragment can be retired by an acknowledgement.

The receiver rejects bad magic, version, lengths, CRC, offsets or sequence order. A duplicate of the last accepted sequence is acknowledged without redelivery. Complete-message callbacks run synchronously; returning false applies backpressure by withholding the final acknowledgement. Messages are never partially delivered. Codec tests simulate bidirectional fragmentation, loss, corruption, duplicates, backpressure and sequence wrap.

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

SCAN requires no current joined session. It closes previous station state, increments the scan generation and requests up to three scans, stopping at the first nonempty valid result set. Non-game entries with zero communication ID or zero nodes are omitted. It returns a cached snapshot; JOIN must use the matching generation and index. Generations are connection-local and wrap as u16.

JOIN requires a cached, accepting session with a free slot. `version=-1` uses the host's advertised local communication version; other values must be nonnegative. Key length is 16–64 bytes. Native product security mode is used. Success requires native StationConnected state and a network ID matching the selection, then successful IPv4 lookup. This creates no Pia/application session.

BIND requires a joined session, slot 0–3 and nonzero port. Ports must be unique across slots. Rebinding closes the old slot first, so a bind error leaves that slot closed. Sockets are nonblocking UDP with broadcast enabled. SEND requires a bound slot and nonzero destination port; zero-length UDP payload is allowed. Destinations are restricted to the relay itself, currently connected LDN nodes or the active LDN subnet broadcast address. No general Internet or LAN proxy is provided. IPv4 node addresses are refreshed about once a second.

LEAVE closes sockets and the LDN station. Bluetooth remains connected. INFO returns the same metadata schema as a successful JOIN. PING is an opaque exact echo.

### Responses (Switch → phone)

| Opcode | Name | Body |
| --- | --- | --- |
| 0x80 | CAPS | `version:u8, protocols_bitmap:u8, max_udp:u16, socket_count:u8, reserved:u8` |
| 0x81 | NETWORKS | `generation:u16, protocol:u8, count:u8`, followed by 18-byte records |
| 0x82 | CONNECTED | Variable session metadata below |
| 0x83 | LEFT | Empty |
| 0x84 | BOUND | `slot:u8, port:u16 network-order` |
| 0x85 | SENT | `slot:u8, payload_size:u16` |
| 0x86 | UDP | `slot:u8, source:4 bytes, port:u16 network-order, payload:bytes` |
| 0x87 | PONG | Exact PING body |
| 0x88 | COUNTERS | `joined:u8, queue_depth:u8, rx_packets:u64, tx_packets:u64, dropped:u64, rx_bytes:u64, tx_bytes:u64` |
| 0xff | ERROR | `failed_opcode:u8, category:u8, detail:u32` |

CAPS protocol bitmap bit N indicates protocol N; current value 0x0a means 1 and 3. Reserved byte is zero. A SENT response establishes only that `sendto` accepted the payload, not peer receipt.

Each NETWORKS record is `index:u8, communication_id:u64, scene:u16, host_version:i16, node_count:u8, node_max:u8, channel:i16, acceptance_policy:u8`. Only policy 0 (always accept) is joinable in v1.

CONNECTED body, in order:

1. `protocol:u8`, local IPv4 (4 network-order bytes), subnet mask (4 network-order bytes).
2. Communication ID `u64`, scene `u16`.
3. SSID length `u8` (0–32), that many raw SSID bytes (no added NUL).
4. Raw 16-byte native session ID.
5. Advertisement length `u16` (0–384), advertisement bytes.
6. Connected node count `u8` (0–8), then 15 bytes per connected node: IPv4 (4 bytes), MAC (6 bytes), node ID `u8`, is-connected `u8`, local version `i16`, platform `u8`.

The payload is built field by field; it is not a serialization of padded libnx structs. Player names are not included. Phone adapters should request INFO again if they need updated node metadata.

ERROR categories: 1 format, 2 state, 3 unsupported, 4 native LDN, 5 socket, 6 queue, 7 stale selection, 8 destination. Detail is context-specific, usually raw native Result or errno; decode it according to category. Control queue exhaustion is currently logged on the Switch and may require retry/reconnect rather than producing another queued error.

## Flow control, cleanup and measurements

The Switch polls at most four datagrams per socket per iteration. Incoming datagrams larger than 1400 bytes or from addresses outside the current LDN peers/self are dropped. At most 20 of the 24 outgoing queue slots can be occupied before new UDP events are dropped, reserving space for control replies. A datagram is dropped whole, never truncated and forwarded. Control and UDP messages otherwise share FIFO order, so heavy traffic can delay control replies.

The Switch disconnects after three failed GATT exchanges or ten seconds without acknowledgement progress on a nonempty outgoing queue. Each GATT read/write has a 1.5-second wait bound; native scan/connect calls may block separately. It checks native LDN state about once a second, emits LEFT on loss, and closes its sockets. B leaves LDN; + cleans up both transports. Counters are cumulative for the current Switch app process.

RX counters count accepted source/size datagrams before the BLE queue decision; dropped includes invalid/oversize datagrams, queue overflow and receive errors. TX counts successful socket sends. Phone receive counts independently measure completed BLE delivery. A 1024-byte PING round trip and a 512-byte local UDP loopback measure these isolated paths, including scheduling and fragmentation; neither is a sustained throughput or gameplay benchmark.

## Optional event batching (relay 0.1.1)

The existing wire/codec version remains 1. CAPS byte 8 now advertises optional
features; bit 0 (`RL_FEATURE_BATCH`) allows batched Switch-to-companion events.
All other bits are reserved. Batching starts disabled for every BLE handshake.
A new companion sends CONFIG (opcode 9) with a one-byte feature mask before
SCAN; CONFIGURED (0x89) echoes the applied mask. An old companion never sends
CONFIG and continues receiving the original individual events.

When enabled, BATCH (0x8a, request ID zero) contains repeated
`length:u16le | complete_message:length` records. Each message retains its
original opcode, request ID and payload. Nested BATCH messages are invalid.
Receivers must validate the entire envelope before delivering any records;
codec-level sequence handling suppresses retransmitted envelopes. The envelope
normally fits `negotiated_frame_limit - 16` bytes, so it takes one BLE frame.
Messages that cannot fit are sent individually using normal fragmentation.
CONFIGURED and other control replies may themselves arrive inside a batch.

The Switch fills batches while draining UDP sockets and flushes at the next
BLE exchange, without waiting to fill a timer-based batch. No UDP packet is
split across batch records. UDP size and queue/drop limits remain unchanged.


## Experimental USB transport (local 0.3.0)

Switch X starts libnx usbComms, independent of BLE. The Mac helper claims its
vendor interface and forwards fixed 4096-byte pages to one loopback TCP client
on port 42387. Each page starts with `LDU1`, then message length:u16le,
two zero reserved bytes, and one complete relay message (3–2048 bytes).
Remaining bytes are zero padding. Receivers reject malformed headers and lengths;
TCP reads must accumulate a full page. USB buffers remain page-aligned and alive
until completion or usbCommsExit. CAPS advertises USB bit 32 plus bidirectional
batching and quiet-send support. There is no BLE stream header or relay retry
layer on this reliable ordered transport. USB event batches flush within 1 ms.

This is a single-session transport: restart both endpoints after a disconnect.
A successful build or page-codec test does not establish USB hardware operation,
LDN compatibility or a completed trade.

### Negotiated compact endpoints (0.3.2)

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

## Historical paired transport and recovery (0.4.4–0.4.7)

Versions 0.4.4–0.4.7 required an authenticated envelope. Version 0.5.0
replaces it with [local Switch approval](../PAIRING.md). The preceding
legacy transport sections describe historical formats; they are not an insecure
fallback in paired builds. LRH3/LRA3 retains stream-v2 and negotiates GATT read
recovery after 500 ms receive silence. Reads and writes are serialized during
recovery. Sequence numbers, partial messages and compact caches are preserved.
The read path is slower and has not passed a 62 packets/s load qualification.
Version 0.4.5 periodically retries notifications; an authenticated LRN4 frame
returns the Switch to notification mode without resetting the stream.

### Explicit diagnostic opt-in (0.4.6)

BENCH, one-way/radio parameter commands and diagnostic reconnect require the
local `/switch/ldn-relay/diagnostics.enabled` file. Without it, the Switch
returns unsupported. Connection-scoped opcode 0x7b has an empty body and requests
a transport disconnect plus one reconnect three seconds later. It is allowed
only with no LDN session and is limited to 20 requests per launch. This is a
qualification tool, not automatic reconnection during a game. Version 0.5.0
also requires a fresh local approval on every diagnostic reconnect; there is no
automatic approval bypass.


## Current approval transport (0.5.0)

See [Switch approval and wire format](../PAIRING.md). LRO5/LOA5 and
LRC5/LRD5/LRN5 replace v4 authentication and envelopes. No key is loaded.
A fresh local A press is required before the Switch exchanges setup or commands;
B, exit or the 60-second deadline rejects the attempt. The session is cleared on
cleanup and is never reused on reconnect. Existing stream framing, probe gate,
batching and read recovery remain. The outer notification marker is no longer
authenticated; do not describe session identifiers or counters as security.

After approval setup, LRH3/LRA3 negotiates the stream and read-recovery capability;
there is no fallback to LRH1/LRH2 in the current BLE path. Switch writes are paced
at least 5 ms apart. The generic Apple companion defaults to a three-frame window
and 15 ms notification pacing; the separately tested game companion uses 5 ms.
These application timers do not establish the negotiated radio connection interval.
The stream implementation permits an eight-frame maximum, with the active window
limited by configuration and peer credit. Older eight-frame lab configurations
are not the generic companion's current defaults.
