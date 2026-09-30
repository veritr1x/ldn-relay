# Credits and third-party notices

LDN Relay's source is licensed under the [MIT licence](LICENSE). Dependencies
and toolchain runtime components retain their respective licences; the project
licence does not replace those notices.

- [libnx 4.12.0](https://github.com/switchbrew/libnx/tree/v4.12.0) provides the
  native Switch APIs and the default Homebrew Menu icon. Its permission notice
  is included in [licenses/libnx.txt](licenses/libnx.txt).
- [devkitPro](https://devkitpro.org/) provides devkitA64 and the Switch build
  tools. The pinned container includes libnx 4.12.0-1, devkitA64 r29.2-1 and
  switch-tools 1.13.1-1. Newlib and GCC runtime notices are included under
  [licenses/](licenses/); these are not a claim that every listed component
  is linked into this small executable.
- [Decryptu/pokeldn](https://github.com/Decryptu/pokeldn/tree/8e6977d14b5edc4797f054c75c1de6f069a5ae6a)
  informed the investigation. The companion's FRLG connection profile was
  cross-checked against its published protocol settings. PokeLDN is licensed
  under AGPL-3.0; its implementation is not vendored or linked here. This relay
  does not contain its Pia/game implementation.
- The Apple companions use [CoreBluetooth](https://developer.apple.com/documentation/corebluetooth)
  and UIKit supplied by the Apple SDKs. No Apple SDK or signed application is
  included in this repository or its NRO artifact.

Notice sources are recorded by Git blob ID:

| File | Upstream source | Blob |
| --- | --- | --- |
| `licenses/libnx.txt` | `switchbrew/libnx`, `v4.12.0`, `LICENSE.md` | `b8327e3f77c35baf0840797b542c67e5918367fe` |
| `licenses/newlib.txt` | `devkitPro/newlib`, `COPYING.NEWLIB` | `2bf6f0bed40635a9398b5a4d9b0828d59bf1213f` |
| `licenses/gcc-runtime-exception.txt` | `gcc-mirror/gcc`, `COPYING.RUNTIME` | `e1b3c69c179dfaff5744ea4bf853e6b0d90c9ef9` |
| `licenses/gcc-gpl-3.0.txt` | `gcc-mirror/gcc`, `COPYING3` | `94a9ed024d3859793618152ea559a168bbcbb5e2` |
