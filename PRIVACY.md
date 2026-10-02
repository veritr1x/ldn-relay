# Privacy

The Switch relay has no analytics or cloud upload endpoint. It uses `LDN Relay`
as its network display name and does not read your Nintendo profile for that name.
It shares session IP/MAC addresses and network IDs with the companion for routing.

## Logs

Switch file logging is off by default. Create
`/switch/ldn-relay/diagnostics.enabled` to turn it on for troubleshooting. The app
keeps `relay.log` and `relay.previous.log`, each capped at 256 KiB. Remove the flag
to stop logging; existing files remain until you delete them.

Logs can contain network addresses, system versions, timing and error codes.
Apple developer apps and lab tools also write local logs and may include device
identifiers. Review logs before sharing and post only the relevant redacted lines.

## Files and attribution

Downloads contain the NRO, documentation, licences, checksums and build manifests.
They do not include ROMs, saves, signing profiles, device captures or pairing keys.
Build and results directories are ignored by Git. The privacy checker catches
common accidental identifiers; it is not a complete secret scanner.

The Switch app's author is `veritrix`; commits use the public `veritr1x` GitHub
identity. Third-party licence credits are preserved.

0.5.0 no longer reads pairing keys. Older key files and Keychain entries remain
local for rollback and can be removed when no longer needed. Approval itself does
not authenticate or encrypt BLE traffic; see [PAIRING.md](PAIRING.md).
