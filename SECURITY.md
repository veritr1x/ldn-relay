# Security model and reporting

Version 0.5.0 requires physical approval on the Switch for each BLE connection.
It does not cryptographically authenticate the companion or encrypt relay traffic.
Session IDs, counters and CRCs handle stale traffic and transport errors, not
hostile-peer authentication. Only run the intended companion during discovery.

USB trusts the physically connected computer. UDP destinations are restricted to
the active LDN session's peers/broadcast; the relay is not an Internet proxy.
Malformed data, resource exhaustion, traffic before approval and data leaving
those bounds are still bugs worth investigating. See PAIRING.md and PRIVACY.md.

For non-sensitive bugs, use the repository's bug-report form. If the repository's
Security tab offers **Report a vulnerability**, use that private channel for a
sensitive report. Otherwise open a minimal issue asking the maintainer for a
private reporting channel without posting exploit details or secrets. Do not
assume private reporting is enabled. No guaranteed response time is advertised.

No independent security audit is claimed. Old PSK code/tests are historical and
are not linked into current approval-mode builds.
