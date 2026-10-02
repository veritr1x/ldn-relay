# Switch approval (0.5.0)

No pairing key, import file, PIN or Keychain entry is required. Approve each BLE connection on the Switch. Install matching approval-mode versions of
the relay and companion; the older PSK builds are incompatible over BLE.

1. Open the intended companion app, with Bluetooth enabled and the screen unlocked.
   Keep other relay companion apps closed during discovery.
2. Launch LDN Relay through Album and press **A** to find the companion.
3. When the Switch asks, release A and press **A** again to approve the connection.
   **B** rejects it; **+** exits. The prompt expires after 60 seconds.
4. The companion can now use relay commands. Disconnecting ends that approval;
   every new connection requires another local approval, including diagnostic
   reconnects. The app does not remember a trusted device or approve automatically.

The Switch connects to the first matching BLE service and displays the approval
prompt before exchanging relay setup or commands. It does not identify the person
or app cryptographically. Physical approval authorizes that connection only.

## What approval means

This mode provides **no cryptographic authentication, payload confidentiality or
cryptographic tamper protection**. Session identifiers and counters prevent stale
frames from a previous connection being accepted accidentally; they are public
and forgeable. Stream CRC and sequencing handle transport errors and duplicates,
not hostile peers. Use only a companion you intend to authorize. USB mode remains
controlled by the local cable connection and does not use the BLE approval prompt.

There is no downgrade from the previous keyed protocol: 0.5.0 uses a distinct
setup/envelope format and both endpoints must be updated. Earlier key files and
Keychain entries are ignored, not imported, transmitted or automatically deleted.
They can be removed manually if rollback to a paired build is no longer needed.
The old authentication helper/tests remain historical source; current builds do
not link or invoke the PSK implementation.

## Connection framing

Integers are little-endian. `LRO5` setup and `LOA5` reply are 24 bytes:
magic(4), inner capacity(2), zero reserved(2), random non-secret session ID(16).
Capacity is 64–472 bytes. The reply echoes capacity and session ID. Setup on the
Switch runs only after local approval. The Apple app scopes the session to one
CoreBluetooth central. An identical pending setup may be repeated without
resetting counters; a different session cannot replace an active one.

Envelopes are marker(4), session ID(16), counter(8), payload(1–472):
`LRC5` for Switch writes, `LRD5` for companion reads and `LRN5` for notifications.
The maximum envelope remains 500 bytes. Counters increase independently in each
direction and cannot wrap. These are connection bookkeeping, not security tokens.
The stream codec retains its CRC, cumulative acknowledgements and retransmission.
The 28-byte envelope preserves the previous inner capacity and batching budget.

LRH3/LRA3 stream negotiation, notification probing and same-session read recovery
run inside these envelopes. A notification marker resumes notification mode after
read recovery. No v4 authentication proof or key derivation occurs.
