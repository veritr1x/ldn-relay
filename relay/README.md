# Relay source

See the [project README](../README.md) for installation, build instructions,
hardware validation and current limitations, and [PROTOCOL.md](PROTOCOL.md)
for the companion-to-Switch wire protocol.

The companion's FRLG preset uses the public connection settings documented in
[Decryptu/pokeldn at 8e6977d](https://github.com/Decryptu/pokeldn/blob/8e6977d14b5edc4797f054c75c1de6f069a5ae6a/pokeldn/ldn/transport.py).
It provides no Pia implementation or trading logic. The Switch relay itself
receives its settings from the companion and contains no game-specific keys.
