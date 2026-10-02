# Installing the 0.5.0 preview

This is a developer preview of a UDP relay, not a standalone Pokémon emulator.
The NRO requires a modified original Switch; the other console can remain stock.
See [COMPATIBILITY.md](COMPATIBILITY.md) for tested devices and versions.

## Choose the companion

- **Playing/trading:** use a matching game companion that implements the game's
  protocol. The tested mGBA companion is a separate project; its release packaging
  is not included here. Installing this generic relay app alone does not enable trades.
- **Transport development:** the generic Apple companion in this repository can
  discover/join sessions and exercise UDP. Its game-session passphrase field is
  unrelated to BLE pairing. This release uses Switch approval without pairing keys.

Until a supported game companion is distributed, this repository is usable by
companion developers and people building their own matching client. It is not
an install-and-play package for general players.

## Switch

1. Open [Build NRO](https://github.com/veritr1x/ldn-relay/actions/workflows/build.yml),
   select a successful **main** run and download its `ldn-relay-nro-…` artifact.
   Sign in to GitHub first. Artifacts expire after 30 days; use a recent run.
2. Unzip it, inspect `build-info.json`, and run `shasum -a 256 -c SHA256SUMS`
   from the extracted directory on Mac (`sha256sum -c SHA256SUMS` on Linux).
3. Back up an existing relay NRO, then copy `ldn-relay-v0.5.0.nro` into
   `/switch/ldn-relay/` on the SD. Keep one active NRO to avoid version confusion.
   SD copying or DBI's MTP responder can be used; DBI is not the relay itself.
4. Exit DBI and launch the relay through **Album / Applet Mode**. Full application
   mode can apply the host game's communication-ID restrictions.
5. Open the intended companion, foreground and unlocked. Close other relay apps
   that advertise. Press **A** on the Switch to discover it, release A, then press
   **A** again at the approval prompt. **B** rejects, **+** exits, and the prompt
   expires after 60 seconds. Each new connection requires approval.

No `pairing.key` is required. Old paired builds (0.4.4–0.4.7) cannot communicate
with 0.5.0 over BLE. Update both endpoints. See [PAIRING.md](PAIRING.md).

## Build the generic iPhone companion

Requirements: a Mac with full Xcode and its command-line tools selected, Python 3,
a valid Apple development signing identity in Keychain, and a development profile
that allows the bundle ID and target phone. The builder accepts app-specific or
wildcard profiles. It does not create certificates or register devices.
Apple explains the signing model in [Provisioning Profiles](https://developer.apple.com/documentation/technotes/tn3125-inside-code-signing-provisioning-profiles)
and [running on a physical device](https://developer.apple.com/documentation/xcode/running-your-app-on-simulated-or-physical-devices).

Clone the source first (also needed for the Mac instructions):

```sh
git clone https://github.com/veritr1x/ldn-relay.git
cd ldn-relay
```

Then build and install:

```sh
python3 relay/ios/build.py \
  --profile /path/to/your/development.mobileprovision \
  --bundle-id dev.example.ldnrelay
xcrun devicectl list devices
xcrun devicectl device install app --device DEVICE_ID \
  build/relay-ios-v0.5.0/LDNRelay.app
xcrun devicectl device process launch --device DEVICE_ID dev.example.ldnrelay
```

Replace the profile, bundle ID and device placeholder with your own values.
If your profile covers the default `dev.local.ldn-relay`, omit `--bundle-id`.
Use the same bundle ID and signing team for updates that should preserve app data.
Unlock the phone, enable Developer Mode if iOS requests it, and grant Bluetooth
access. Handle developer trust through iOS/Xcode when prompted; never install
another user's provisioning profile as a substitute for your own signing setup.

Development signing expires according to your profile. Renew the profile and
rebuild/reinstall when necessary. No universally installable signed IPA, App Store
or TestFlight distribution is provided by this repository.
Normal builds use a three-frame window, 15 ms notification pacing and no automatic
benchmarks. `--diagnostics` reveals benchmark controls for deliberate testing;
Switch benchmark commands also require its local diagnostic flag.

## Build the generic Mac companion

On Apple Silicon with full Xcode and macOS 14 or later:

```sh
python3 relay/mac/build.py
open 'build/relay-mac-v0.5.0/LDN Relay Mac.app'
```

This is a local ad-hoc-signed development build, not a notarized public download.
Grant Bluetooth access. Keep the companion in the foreground and approve it on
the Switch. The same BLE protocol is used on Mac and iPhone; their hardware
qualification status is separate.

The Switch also offers USB to a Mac companion. The generic app's default builder
is BLE-only; a USB-capable companion and the helper described in
[CONTRIBUTING.md](CONTRIBUTING.md#usb-on-mac) are needed. USB 0.5.0 lifecycle qualification remains pending.

## Updating and rolling back

Back up the NRO and the game companion's saves before updating. Do not delete a
game app to update it: install a compatible signed update in place, preserving
its bundle ID/team. Check the companion's own save export instructions.

Update the relay and companion together. To roll back across the keyed/approval
boundary, restore both matching versions and their original private setup.
Preserve existing keys only if you need a paired-build rollback; approval mode
ignores them. Never publish keys, profiles, saves or raw device captures.

Do not create `diagnostics.enabled` for everyday use. If support requests a log,
follow [TROUBLESHOOTING.md](TROUBLESHOOTING.md) and remove the flag afterward.
