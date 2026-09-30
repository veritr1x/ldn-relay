# LDN Relay wire protocol, version 1

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
