# raw_demosaic — NOTICE

Part of Rawr and distributed with it under **GPL-3.0-only** (see the repository
root `LICENSE` and `NOTICE`).

This module is a GPU port / derivative of GPL-3.0-or-later upstream code.
Copyright in the original code remains with its authors:

- **RCD** demosaic: derived from **librtprocess**
  (<https://github.com/CarVac/librtprocess>), GPL-3.0-or-later, which
  incorporates code from **RawTherapee**
  (<https://github.com/RawTherapee/RawTherapee>). RCD algorithm by Luis Sanz
  Rodríguez; librtprocess and RawTherapee contributors.
- **VNG4** demosaic: derived from **librtprocess** (unclamped variant),
  GPL-3.0-or-later.
- **Dual** (RCD+VNG4) blend: derived from **RawTherapee**, GPL-3.0-or-later.
- **quadfix** pre-filter: original to Rawr.

Files here are modified ports; individual files carry provenance comments naming
the upstream function or pass they reproduce. Full third-party notices are in the
repository root `THIRD_PARTY_NOTICES.md`.
