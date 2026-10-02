# Security

Each BLE connection needs approval on the Switch. This confirms your intent to
connect, but does not cryptographically identify the app or encrypt its traffic.
Keep only the intended companion open during discovery.

Session IDs, counters and CRCs help reject stale or damaged traffic. They are not
protection against a hostile peer. USB trusts the physically connected computer.
UDP forwarding is limited to the active LDN network. See [PAIRING.md](PAIRING.md)
for the connection format and [PRIVACY.md](PRIVACY.md) for logging.

## Report a problem

Use the [bug report form](https://github.com/veritr1x/ldn-relay/issues/new/choose)
for ordinary connection problems. For a sensitive issue, use **Report a vulnerability**
in the repository's Security tab if available. Otherwise, open a short issue asking
for a private contact route. Leave exploit details and private data out of that issue.

Traffic accepted before approval, malformed input crashes, resource exhaustion
and forwarding outside the LDN session are all worth reporting. The project has
not had an independent security audit.
