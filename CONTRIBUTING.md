# Working on LDN Relay

The relay handles Nintendo's local network and moves packets. Companion apps
handle game protocols, emulation and UI. Keeping those separate lets other games
use the same Switch app.

## Source map

| Path | What it contains |
| --- | --- |
| `relay/switch/main.c` | LDN discovery, hosting, commands and UDP sockets |
| `relay/switch/ble_link.inc` | Bluetooth setup, approval and cleanup |
| `relay/switch/stream_link.inc` | BLE worker and queued transport |
| `relay/switch/usb_link.inc` | Switch USB transport |
| `relay/common/relay_protocol.h` | Command IDs, capabilities and app version |
| `relay/common/relay_approval.h` | Approval state and connection envelopes |
| `relay/common/relay_stream.c` | Stream framing, acknowledgements and retries |
| `relay/common/LRTransport.h` | Apple transport API |
| `relay/ios/main.m` | Generic companion UI and command handling, shared with Mac |
| `relay/usb/bridge.c` | Mac USB-to-loopback helper |
| `relay/tests/` | Host-side C tests |
| `relay/sim/` | Deterministic BLE simulation |
| `scripts/` | Signing, privacy and package checks |

`relay/ble-lab/` contains older physical Mac/iPhone experiments. Its central does
not speak the current approval protocol yet. Historical PSK helpers and tests
also remain for reference; current apps do not use them.

## Build and test

The Switch build needs Docker. Apple builds need full Xcode; see [INSTALL.md](INSTALL.md).
Host tests need Clang and Python 3. On Linux, install the OpenSSL development
package (`libssl-dev` on Ubuntu) for the historical authentication tests.

Run these from the repository root:

```sh
make test HOST_CC=clang
make ble-lab-test HOST_CC=clang
python3 -m unittest discover -s scripts -p 'test_*.py'
python3 scripts/privacy_check.py
sh build.sh
python3 scripts/package.py --output build/package-0.5.0
```

Use an empty output directory for packaging. Packages include the NRO, checksums,
licences and a manifest identifying the source and toolchain. Local packages
record uncommitted changes; CI refuses to package a dirty checkout.

C tests run with AddressSanitizer and UndefinedBehaviorSanitizer. The simulator
uses the real stream codec with modeled queues and timing. It is useful for
regressions, but a simulation cannot tell you the Switch radio's actual speed.
CI also compiles and checks the generic Mac app's ad-hoc signature.

## Write a companion

For an Apple app, start with `LRTransport` and the command handling in
`relay/ios/main.m`. Both the generic Mac and iPhone apps act as BLE peripherals;
the Switch discovers and connects to them.

1. Set `messageHandler`, `statusHandler` and `resetHandler`, then call `start`.
2. Wait for Switch approval and `RL_CAPS`. Check the reported features before
   using optional commands. The transport handles setup, framing and batching.
3. Send `RL_SCAN`, then `RL_JOIN` for a returned session, or `RL_HOST` with your
   game's session settings. Keep the scan generation when joining.
4. After `RL_CONNECTED` or `RL_HOSTED`, use `RL_BIND` to open a UDP slot. Send
   game data with `RL_SEND` and consume incoming `RL_UDP` messages.
5. Handle `RL_ERROR`, `RL_LEFT` and transport resets. Clear game-session state
   after disconnection; reconnecting the relay does not resume a game transaction.
6. Send `RL_LEAVE` when finished.

Messages begin with an opcode and a little-endian 16-bit request ID. IP addresses
and UDP ports use network byte order. See [PROTOCOL.md](relay/PROTOCOL.md) for
layouts, limits and hosting settings.

`enqueue:` returns `NO` when it cannot accept a message. Keep a bounded queue in
your app and retry later; don't spin or discard game traffic silently. Apple
transport callbacks run on the main queue, while CoreBluetooth uses its own serial
queue. Keep callbacks short and move expensive game work off that queue.

Use the generic companion's defaults as a starting point: `LDNRelaySendWindow=3`
and `LDNRelayNotificationPaceMs=15` in Info.plist. Larger windows or shorter timers
need measurement on the target devices. Neither setting is the radio connection interval.

### USB on Mac

Install libusb and pkg-config, then build and run the helper:

```sh
mkdir -p build
clang -std=c17 -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -Irelay/common \
  $(pkg-config --cflags libusb-1.0) relay/usb/bridge.c relay/common/relay_codec.c \
  $(pkg-config --libs libusb-1.0) -o build/ldn-relay-usb-bridge
build/ldn-relay-usb-bridge
```

Close DBI and press **X** in the Switch relay. The helper accepts one local client
at `127.0.0.1:42387`. An Apple companion using `LRTransport` selects this path with
`LDNRelayTransport=usb` in its Info.plist before signing. The generic Mac builder
currently defaults to BLE. Restart the relay, helper and companion after USB loss.

## Change the relay

For a new command, add its ID and capability in `relay_protocol.h`, validate its
complete payload before acting, implement bounded error handling on the Switch,
and document the wire layout. Update both endpoints when changing a wire format;
never silently interpret old frames as a new format.

Add a regression test for transport or parsing fixes. Useful cases include bad
lengths, duplicate messages, queue pressure, disconnects and retry timing. Keep
benchmarks behind the existing diagnostic opt-in. See [TODO.md](TODO.md) for work
that would help; full native LDN coverage is a longer-term goal.

A pull request should explain the problem, the change and how it was tested.
For hardware results, include device/OS versions, packet sizes, offered load,
loss and latency. Keep failed results too. Build success and a successful trade
answer different questions.

Keep ROMs, saves, profiles, keys, device IDs and raw captures out of Git. Share
small redacted logs when needed. See [PRIVACY.md](PRIVACY.md) and
[SECURITY.md](SECURITY.md). Preserve third-party licence notices.

## Publishing a Switch download

The embedded version in `relay/common/relay_protocol.h` must match the release
tag. After the main-branch workflow passes, push a matching tag, for example
`v0.5.0`. The tag workflow reruns tests, builds the NRO, verifies its icon, author
and version, and publishes a GitHub preview release. Existing release assets
are never replaced automatically; use a new version for a changed binary.

The release contains the raw NRO, an install ZIP, `SHA256SUMS` and
`build-info.json`. Packaging checks the artifact against the exact CI commit
and verifies all checksums before publishing. The ZIP preserves the source
manifest, licenses and installation documents.
