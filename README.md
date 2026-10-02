# LDN Relay

[![Build NRO](https://github.com/veritr1x/ldn-relay/actions/workflows/build.yml/badge.svg)](https://github.com/veritr1x/ldn-relay/actions/workflows/build.yml)

A generic homebrew UDP relay for a modified Nintendo Switch. App author: **veritrix**.
Source and releases: [veritr1x/ldn-relay](https://github.com/veritr1x/ldn-relay).

```text
Mac / iPhone companion <-- BLE --> modified Switch <-- native LDN --> stock console
Mac companion          <-- USB --> modified Switch <-- native LDN --> stock console
```

The relay discovers, joins or hosts native LDN sessions and forwards UDP packets.
Game connection settings and application protocols belong to the companion.
No ROM, save, game passphrase or Pia/RFU implementation is embedded in the NRO.

**0.5.0 is a preview, not a production-qualified release.** Local Switch approval and same-session read recovery are implemented.
No pairing-key import is required. An approved connection and LDN scan passed
on hardware; broader qualification remains. The earlier 20-reconnect result used
the previous paired build. The read fallback cannot sustain
62 packets/s in the measured synthetic test; recovery under game load remains
unqualified. The user reports a successful trade on the key-free 0.5.0 setup
and multiple successful trades on the previous game build. See
[production readiness](PRODUCTION_READINESS.md) for the release gates.
The [release checklist and capability backlog](TODO.md) separates first-release
work from later expansion of the native LDN interface.

See the [installation guide](INSTALL.md), [compatibility matrix](COMPATIBILITY.md)
and [troubleshooting](TROUBLESHOOTING.md) for setup and supported evidence.

## Install and run

1. Use a modified original Switch, including OLED, with Homebrew Menu. Firmware
   20 or later is required by the app; other combinations need qualification.
2. Download a successful [Build NRO](https://github.com/veritr1x/ldn-relay/actions/workflows/build.yml)
   artifact for the desired commit. Check `build-info.json` for its actual version
   and source; the latest local work may not yet be on GitHub.
3. Verify `SHA256SUMS`, then copy the NRO into `/switch/ldn-relay/` on the SD card.
   Keep only the intended app version in that folder to avoid launching old builds.
4. Launch through **Album / Applet Mode**. Full application mode can fail native
   communication-ID checks.
5. Open a matching approval-mode companion. No key import is needed; see
   [Switch approval](PAIRING.md). Previous paired companions must be updated.
6. **A** discovers a BLE companion. At the prompt, release A and press **A** again
   to approve this connection, or **B** to reject. **X** starts USB for Mac.
   During a session, **B** leaves LDN; **+** releases the session and exits.

BLE discovery can take about 30 seconds. Keep the Apple companion foreground,
with its screen unlocked. Only one companion should advertise during setup:
discovery selects the first matching service, then waits for your Switch approval.
Approval applies only to that connection. It does not authenticate the companion
cryptographically or encrypt traffic.
Use the companion to select or create a session and configure its sockets.
Native LDN can temporarily replace the Switch's normal Wi-Fi connection.

For USB, close DBI before running the relay, start the Mac USB helper, then the
USB companion. The helper listens on `127.0.0.1:42387`. After a USB disconnect,
restart USB mode, helper and companion. DBI is only needed to copy files.

## Capabilities and limits

- LDN protocol 1 or 3; station joining and access-point hosting.
- Companion-provided communication ID, scene, version, passphrase and advertisement.
- Four nonblocking UDP sockets, up to 1,400 bytes per datagram.
- Destination restriction to the connected LDN network's members/broadcast.
- BLE stream-v2 framing, batching, compact headers, bounded queues and counters;
  per-connection Switch approval and same-session GATT read recovery.
- USB transport for a Mac companion.
- Explicit leave, timeout cleanup and reconnect controls.

No raw Wi-Fi injection, TCP relay or Internet proxy. No generic game compatibility
is implied. Benchmark and radio-tuning commands require local diagnostic opt-in. Normal-use isolation remains part of
release review. No cryptographic authentication is provided in approval mode.

## Validation status

Local sanitizer tests cover framing, bounds, loss/reordering, duplicate delivery,
backpressure, sequence wrap, compact packets, host configuration and USB framing.
The simulated BLE laboratory exercises the real stream codec under modeled
conditions; it cannot establish Switch driver behavior or radio capacity.

Hardware development has exercised native joining and hosting. A Mac USB trade
was reported successful. iPhone-host BLE tests also exchanged a Pokémon, but
post-save synchronization and later notification stalls required further fixes.
The user subsequently confirmed multiple completed trades with the working game
build. That is user-verified evidence for that setup. The game adapter lives in a
separate project; the new approval-mode transport needs separate qualification. No
supported-game matrix is published.

## Build, test and package

```sh
make test HOST_CC=clang
make ble-lab-test HOST_CC=clang
python3 scripts/privacy_check.py
python3 -m unittest discover -s scripts -p 'test_*.py'
sh build.sh
python3 scripts/package.py --output build/release-0.5.0
```

`build.sh` uses a devkitPro container pinned by digest and disables its network
while building. `make` also works with a compatible local devkitPro installation.
Output: `build/ldn-relay-v0.5.0.nro`. Embedded title, author and version are
validated before packaging. The package contains licences, documentation,
checksums, source commit, dirty status and a source-file digest manifest.
An existing output directory is never overwritten. Default package output is `dist/`.

CI tests and packages the checked-out commit. It does not run hardware tests.
Actions artifacts require GitHub login and expire after 30 days; stable public
release downloads and verified release tags remain production work.

## Companions and development tools

Apple companion apps are developer tools requiring a local Xcode build; no signed
app or provisioning profile is distributed here. Mac Catalyst requires Apple Silicon
and macOS 14 or later; iPhone builds target iOS 17 or later.

```sh
python3 relay/mac/build.py
python3 relay/ios/build.py --profile /path/to/development.mobileprovision
```

The build prints its output path. For USB on Mac, install libusb and pkg-config:

```sh
mkdir -p build
clang -std=c17 -D_DARWIN_C_SOURCE -Wall -Wextra -Werror -Irelay/common $(pkg-config --cflags libusb-1.0) relay/usb/bridge.c relay/common/relay_codec.c $(pkg-config --libs libusb-1.0) -o build/ldn-relay-usb-bridge
```

See [wire protocol](relay/PROTOCOL.md), [simulated BLE lab](relay/sim/README.md)
and [physical Mac/iPhone lab](relay/ble-lab/README.md). Those development documents
are in the source checkout, not the NRO download. Game/emulator integration requires
an adapter using the transport API; installing the NRO alone does not enable trading.

## Privacy and diagnostics

Switch persistent logging is **off by default**. For one diagnostic session,
create `/switch/ldn-relay/diagnostics.enabled` before launch. Logs are written to
`relay.log`, with one `relay.previous.log`, in the same directory. Each is capped
at 256 KiB. Remove the flag to disable future file logging; existing logs remain
local. Logging can affect performance. Old probe logs are not deleted automatically.

Apple companions and lab tools still produce local development logs. Review
those before sharing. No logs, saves, device IDs or signing materials are packaged.
See [privacy](PRIVACY.md). This project is not affiliated with Nintendo.

## Licence

[MIT](LICENSE). Preserve [third-party notices](THIRD_PARTY.md) and `licenses/`.
Git attribution uses the public [veritr1x](https://github.com/veritr1x) account's
GitHub no-reply identity; the app's display author is `veritrix`.
