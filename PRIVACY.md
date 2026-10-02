# Privacy and published identity

The Switch app displays `veritrix`. Git commits intentionally identify the public
GitHub account `veritr1x`, using its GitHub-provided no-reply email. That public
attribution is retained at the author's request; private email addresses and
hardware identifiers are not required to build or use the relay.

The NRO uses the generic LDN display name `LDN Relay`; it does not read the user's
Nintendo profile for that name. Session IP/MAC addresses and network identifiers
are exchanged with the connected companion because routing requires them. No
analytics or cloud upload endpoint is part of the Switch relay.

Persistent Switch logs are off by default. Creating
`/switch/ldn-relay/diagnostics.enabled` opts in for the next launch. The app keeps
`relay.log` and one previous session, each at most 256 KiB. Logs can contain local
network addresses, system version, timing and error codes. They are not anonymous.
Remove the flag to stop logging and delete the logs separately when no longer needed.
Old diagnostic files in earlier probe directories are left untouched.

Apple developer companions and physical benchmark tools can log local identifiers
and deployment output. These tools are not packaged with the Switch NRO. Device
IDs and provisioning profiles are explicit local arguments. Never publish signing
profiles, device logs, packet captures, ROMs, saves or private recovery bundles.

The packaging allowlist includes only the NRO, documentation, licences and build
manifests. Ignored build/results directories may contain private development
artifacts; they are not uploaded. `scripts/privacy_check.py` is a guard against
common accidental identifiers, not a complete personal-data or secret detector.
Third-party licence attributions must remain intact.

Version 0.5.0 uses local Switch approval without pairing keys. Previous key files
and Keychain entries are ignored and remain private rollback material. They must
not be published. Approval does not authenticate or encrypt companion traffic.
See [Switch approval](PAIRING.md).
