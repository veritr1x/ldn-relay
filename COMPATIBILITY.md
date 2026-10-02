# Compatibility

This is the setup used during 0.5.0 testing. Other combinations may work, but have
not been checked. Build minimums are not a list of tested devices.

| Component | Version / setup | What was checked |
| --- | --- | --- |
| Relay console | Modified OLED Switch, HOS 22.5.0, Album/Applet Mode | Approval, stream setup, LDN scan and initial clean exit |
| Peer console | Stock Switch 2 | Completed trade reported with the separate mGBA companion |
| Phone | iPhone 15 Pro, iOS 27.0 (24A437) | Approval and control traffic |
| Generic companion | LDN Relay 0.5.0; window 3, pacing 15 ms | Connection and scan; this app has no game implementation |
| Game companion | mGBA LDN 0.5.0; window 3, pacing 5 ms | Completed Pokémon trade reported by a tester |
| Atmosphere | Running version not recorded | Still needs to be added to this matrix |
| Mac BLE / USB | Build checks pass | Current-version hardware testing remains incomplete |

The app requires Switch firmware 20 or later. Apple builds target iOS 17 or later
and Apple Silicon Macs on macOS 14 or later. Other Switch models, iPads and OS
versions have not been qualified for 0.5.0.

The pinned Switch toolchain uses libnx 4.12.0 and devkitA64 r29.2. No firmware,
bootloader or sysmodule update is included in the relay package.

## Match the companion version

0.5.0 uses approval on the Switch. Both BLE endpoints must speak that format;
older keyed companions cannot connect. The [mGBA game companion](https://github.com/veritr1x/mgba-ldn-ios)
is a separate project. Its tested 0.5.0 changes are not published there yet;
the current public source uses an older relay protocol.

The [test results](PRODUCTION_READINESS.md) include the known disconnect error,
older reconnect/recovery measurements and tests still to be completed. A successful
trade does not establish compatibility with other games.
