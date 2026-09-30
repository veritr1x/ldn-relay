# LDN Relay

[![Build NRO](https://github.com/veritr1x/ldn-relay/actions/workflows/build.yml/badge.svg)](https://github.com/veritr1x/ldn-relay/actions/workflows/build.yml)

A homebrew UDP relay for a modified Nintendo Switch, controlled over Bluetooth
LE by a Mac or iPhone companion.

```text
Mac or iPhone  <-- Bluetooth LE -->  modified Switch  <-- native LDN -->  stock console
```

The Switch discovers and joins a selected local-wireless session, then forwards
UDP datagrams. A companion can use this transport to implement a game's protocol
or an emulator adapter. The Switch relay contains no game-specific passphrases.

**This is an experimental transport, not a finished multiplayer or trading app.**
Pia, Pokémon trading and emulator integration are not implemented. Each game
still needs compatible connection settings and a companion adapter that speaks
its application protocol. This is a personal project, not affiliated with Nintendo.

## Download the NRO

1. Open [Build NRO](https://github.com/veritr1x/ldn-relay/actions/workflows/build.yml).
2. Select a successful run for the desired commit.
3. Download the `ldn-relay-nro-<commit>` artifact and extract it.
4. Copy `ldn-relay-v0.1.0.nro` to `/switch/ldn-relay/` on the modified Switch's SD card.

Each artifact includes the NRO, `SHA256SUMS`, a `build-info.json` identifying the
source commit and toolchain, and licence notices. Downloading Actions artifacts
requires a GitHub login; artifacts are retained for 30 days. A new run can be
started with **Run workflow** by someone with repository write access.

The NRO needs a companion app to select sessions and send packets. Companion
source is included; signed Apple applications and provisioning profiles are not.

## Requirements and use

- A modified original Switch with Homebrew Menu and firmware 20 or later.
- **Launch through Album / Applet Mode.** This is the tested context. Launching
  under an unrelated full application did not satisfy its communication-ID check.
- A nearby console hosting a compatible LDN session.
- The Mac or iPhone companion, open in the foreground with Bluetooth enabled.
  Only one companion should advertise at a time; the relay selects the first match.

On the modified Switch, launch **LDN Relay 0.1.0** through Album and press **A**.
Bluetooth discovery and connection can take about 30 seconds. The companion
automatically scans after the handshake. Choose the protocol, session passphrase
and UDP port, then select the intended session. The included FRLG preset only
supplies connection settings; it does not implement trading.

**Test BLE** performs an exact 1,024-byte echo. **Test UDP loopback** sends 512 bytes
to the relay's own LDN address using socket 3, port 49152; it does not establish
that another console received anything. **Read counters** reports accepted
socket traffic, queue depth and drops. **Leave session** or Switch **B** closes
LDN while retaining BLE; Switch **+** closes both transports and exits.

Native LDN can temporarily replace the modified Switch's normal Wi-Fi connection.
The companion uses Bluetooth; the Mac does not need to inject Wi-Fi frames or use
an external USB Wi-Fi adapter.

## What is implemented

- Native station-mode LDN protocol 1 or 3 with session discovery and selection.
- Companion-supplied session passphrase and host communication version.
- Four nonblocking UDP sockets, each supporting payloads up to 1,400 bytes.
- Session metadata, including node addresses, delivered to the companion.
- BLE fragmentation, acknowledgements, CRC, duplicate suppression and bounded queues.
- Explicit leave, link-failure cleanup, queue-stall detection and counters.

This version does not host LDN, expose TCP, tunnel raw Wi-Fi or provide an Internet
proxy. Timing and throughput may limit game compatibility. The custom BLE service
does not implement peer authentication; use it with trusted nearby devices.

## Hardware validation

These results describe a short physical test, not CI or a gameplay compatibility claim.

| Check | Result |
| --- | --- |
| Modified Switch launch | Album / Applet Mode on firmware 20.5.0 |
| Stock peer | Switch 2 hosting FireRed/LeafGreen local wireless |
| Mac companion | Version 0.1.1, native Apple Silicon Mac Catalyst |
| Native session | Protocol 3, verified network identity, two connected nodes |
| BLE | ATT MTU 512, 500-byte frames; 1,024-byte exact echo in 682.0 ms |
| Incoming session UDP | Received by Mac; Switch counted 125 session datagrams over about 67 seconds |
| UDP self-loopback | 512 bytes in 568.6 ms while joined |
| Final relay counters | 126 received including loopback, 1 sent, 0 drops, empty queue |
| Shutdown | Native LDN and BLE released cleanly |

An earlier generic iPhone test passed a 1,024-byte BLE echo in 691.3 ms but did not
join LDN. The updated iPhone 0.1.1 generic UDP path has not had that separate
physical test. The iPhone process was stopped during the Mac test.

Still pending: meaningful two-way traffic with the stock console, sustained load,
disconnect/reconnect recovery, other games, and game/emulator adapters. A passing
build or self-loopback does not establish any of these. The initial publication
preserves the tested relay and companion source; personal device logs are not published.

Known display issue: Switch prompts refer to a phone and the Mac packet counter
says “Delivered to iPhone.” These refer to whichever companion is connected.

## Build locally

The Switch build requires Docker and uses a devkitPro container pinned by digest:

```sh
sh build.sh
```

Output: `build/ldn-relay-v0.1.0.nro`. The pinned image supplies libnx 4.12.0-1,
devkitA64 r29.2-1 and switch-tools 1.13.1-1. The build runs without container
network access after Docker retrieves the image. With an equivalent devkitPro
installation, `make` can also build directly.

Run the existing codec tests with a host C compiler supporting ASan and UBSan:

```sh
make test HOST_CC=clang
```

The tests cover bidirectional fragmentation, loss, corrupted frames, duplicate
delivery, backpressure, queue bounds, sequence wrap and reset.

Package a committed checkout after building:

```sh
python3 scripts/package.py
```

This validates the NRO and embedded assets, title and version before writing
`dist/`. It refuses to mix files into a nonempty `dist/`; move the previous package
first. GitHub Actions runs the codec tests, builds the NRO, validates it and
uploads this directory on main pushes, version tags, pull requests and manual runs.

## Companion builds

For Apple Silicon macOS with Xcode and its Mac Catalyst SDK:

```sh
python3 relay/mac/build.py
open 'build/relay-mac/LDN Relay Mac.app'
```

The Mac build uses a local ad-hoc signature and requires no iOS provisioning
profile. Deployment minimum is macOS 14; the hardware run used macOS 27.0.1.

For an iPhone with Developer Mode and a valid wildcard development profile
covering that device:

```sh
python3 relay/ios/build.py --profile /path/to/development.mobileprovision
xcrun devicectl device install app --device DEVICE_ID build/relay-ios/LDNRelay.app
xcrun devicectl device process launch --device DEVICE_ID dev.local.ldn-relay
```

The iPhone build targets iOS 17 or later; the physical BLE test used iOS 27.
Apple companions are built locally, not by the NRO workflow. Keep signing files
and signed applications out of Git.

## Protocol and development

See [relay/PROTOCOL.md](relay/PROTOCOL.md) for the wire format and flow control.
`relay/switch/main.c` owns LDN/UDP; `relay/switch/ble_link.inc` owns BLE discovery;
`relay/common/relay_codec.c` is shared by both endpoints. `relay/ios/main.m`
provides the iPhone and Mac Catalyst UI.

Companion adapters can call `sendDatagram:slot:address:port:` and receive the
`LDNRelayDatagram` notification (`slot`, source address, port, payload).
`networkInfo` holds the received session metadata. These are in-process hooks,
not an existing inter-app API; an emulator needs explicit integration.

Logs append to `sdmc:/switch/pokeldn-bridge-probe/relay-v0_1.log` on Switch
(the historical path is retained), `Documents/relay.log` in the iPhone app,
and `~/Library/Application Support/LDN Relay/relay.log` on Mac.

## Licence and credits

[MIT](LICENSE). Dependency notices and research references are in
[THIRD_PARTY.md](THIRD_PARTY.md). This project was developed with AI assistance;
the hardware results above are from physical-device tests.
