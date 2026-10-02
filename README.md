# LDN Relay

[![Build](https://github.com/veritr1x/ldn-relay/actions/workflows/build.yml/badge.svg)](https://github.com/veritr1x/ldn-relay/actions/workflows/build.yml)

Use a modified Nintendo Switch to connect an iPhone or Mac app to a nearby
Switch's local-wireless session. The other Switch can stay stock.

```text
iPhone / Mac app ← Bluetooth → modified Switch ← LDN → another Switch
Mac app         ← USB ──────→ modified Switch ← LDN → another Switch
```

LDN is Nintendo's local-wireless networking. This relay can find, join and host
LDN sessions, then forward UDP packets between the companion app and the consoles.

**Version 0.5.0 is a preview.** Pokémon trades have been reported working with a
separate [mGBA LDN companion](https://github.com/veritr1x/mgba-ldn-ios-macos).
**That emulator is not included here.** This repository
contains the Switch relay and generic iPhone/Mac tools for building and testing
companions. Each game still needs an app that understands its protocol.
The companion 0.6.0 preview uses the matching Switch-approval transport and
provides iOS and Apple silicon Mac downloads in its
[Releases](https://github.com/veritr1x/mgba-ldn-ios-macos/releases).

## Get started

You need a modified original Switch (OLED included), firmware 20 or later,
Homebrew Menu, and a matching companion app. See the [tested setups](COMPATIBILITY.md).

1. Open [Build NRO](https://github.com/veritr1x/ldn-relay/actions/workflows/build.yml),
   choose a successful **main** run and download its `ldn-relay-nro-…` artifact.
   GitHub requires you to sign in to download artifacts.
2. Unzip it, verify `SHA256SUMS`, and copy `ldn-relay-v0.5.0.nro` to
   `/switch/ldn-relay/` on the SD card.
3. Open your companion app and keep it on screen. Launch the relay through
   **Album / Applet Mode** on the Switch.
4. Press **A** to find the companion. At the prompt, release A and press **A**
   again to approve the connection. **B** rejects; **+** exits.
5. Use the companion to choose or host a game session.

No pairing key is needed. Open only one companion during setup. Approval is
required for each connection; it does not authenticate or encrypt BLE traffic.

[Installation and Apple app builds](INSTALL.md) · [Troubleshooting](TROUBLESHOOTING.md)

## Build and extend it

With Docker installed and running:

```sh
git clone https://github.com/veritr1x/ldn-relay.git
cd ldn-relay
sh build.sh
```

The NRO is written to `build/ldn-relay-v0.5.0.nro`. The build uses a pinned
[devkitPro](https://devkitpro.org/) toolchain.

Start with [CONTRIBUTING.md](CONTRIBUTING.md) for the source map, tests and a
companion implementation guide. The [wire protocol](relay/PROTOCOL.md) describes
the commands. Game logic belongs in the companion; the relay stays generic.

## Limits

The relay supports LDN protocols 1 and 3, four UDP sockets, and datagrams up to
1,400 bytes. USB is available for Mac clients. TCP, raw Wi-Fi access and Internet
forwarding are not implemented. BLE throughput and recovery depend on the devices;
see [test results and known issues](PRODUCTION_READINESS.md) and the [roadmap](TODO.md).

Made by [veritrix](https://github.com/veritr1x). [MIT licence](LICENSE), with
[third-party notices](THIRD_PARTY.md). Not affiliated with Nintendo.
