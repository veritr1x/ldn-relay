# Changelog

## 0.5.0 preview

- Publish CI-built NRO and install ZIP downloads on GitHub Releases, with
  checksums, licenses and exact build provenance.
- Add the cyan-and-white LDN link icon to Homebrew Menu.

- Approve each BLE connection on the Switch. Pairing-key import is no longer
  needed; update both endpoints together. Approval does not encrypt traffic.
- Add stream windows, batching in both directions, compact packet headers and
  GATT read recovery when notifications stall.
- Add LDN hosting and a USB transport with a Mac helper.
- Keep game settings and protocols in the companion.
- Add transport regression tests, BLE simulation, privacy checks and CI packaging.
- Keep Switch diagnostics opt-in and hide benchmarks in normal Apple builds.
- Add installation, signing, rollback, troubleshooting and contributor guides.

A trade has been reported working with the separate mGBA companion. Reconnect,
recovery and wider device testing remain incomplete; see the
[test results and known issues](PRODUCTION_READINESS.md).
