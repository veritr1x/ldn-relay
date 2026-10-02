# Troubleshooting

| What you see | What to do |
| --- | --- |
| No companion discovered | Open one matching companion, enable Bluetooth, keep it foreground/unlocked and nearby; retry A on the Switch. Close competing relay apps. |
| Approval prompt remains | Release A, then press A again. The initial discovery press is not approval. B rejects; + exits. |
| Rejected or timed out | No relay session was opened. Press A to retry; approve within 60 seconds. |
| Companion setup failed / incompatible paired relay | Update both endpoints to matching approval-mode builds. A previous pairing key cannot fix a version mismatch. |
| Phone app cannot launch | Unlock the phone, check Developer Mode/trust prompts and the signing profile's validity. Rebuild with your own valid development identity/profile. |
| Bluetooth unavailable | Check the app's Bluetooth permission and Bluetooth state in system settings. |
| Scan returns no sessions | Start the game's local-wireless host on the other console. Select its protocol in the companion; a game being open is not necessarily hosting. |
| Native join/create failure | Use Album/Applet Mode and compatible game ID/version/settings. Preserve the numeric error for a report. |
| Queue full / stalled / peer timeout | Stop the session safely, keep the companion foreground, then exit/reconnect with approval. A relay reconnect does not promise to resume an interrupted game transaction. |
| Generic app connects but cannot trade | This is a transport tool. Use a compatible game companion implementing the game's protocol. |
| Old app appears in Homebrew Menu | Keep only the intended NRO in `/switch/ldn-relay/`; retain rollback copies off the SD's app folders. |
| USB fails | Exit DBI first. Restart relay USB mode, the local helper and USB-capable companion. The default generic Apple build uses BLE. |

## Minimal diagnostic report

Include relay and companion versions, console model/system version, Album versus
application mode, phone/Mac OS, exact error and reproduction steps. Say whether
the failure happened before approval, during scan/join, during play or after save.
Report observed results separately from assumptions.

For a requested Switch log, create an empty
`/switch/ldn-relay/diagnostics.enabled`, launch once, reproduce, exit with + and
copy `relay.log` locally. Remove the flag after testing. Logs rotate to
`relay.previous.log` and are capped at 256 KiB each. Never publish raw logs without
review: they may contain local network identifiers. Share a small redacted excerpt.

Do not attach ROMs, saves, pairing keys, provisioning profiles, device IDs,
private recovery bundles or full packet captures to public issues. Existing game
saves should be backed up privately, not uploaded for routine troubleshooting.
