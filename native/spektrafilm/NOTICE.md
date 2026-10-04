# spektrafilm — NOTICE

Part of Rawr and distributed with it. Licensed under the GNU General Public
License version 3 (GPL-3.0-only); see the repository root `LICENSE` and
`NOTICE`. Third-party components and attributions are listed in the root
`THIRD_PARTY_NOTICES.md`.

## Upstream source

Film-simulation shaders, parameter structs, profile data, and table-building
math are derived from:

- **spektrafilm-ofx** by Aedan Diez (chaert-s)
  <https://github.com/chaert-s/spektrafilm-ofx>
  Revision: **`86476af` (2026-07-06)**.
  License: **GNU General Public License v3.0, only** (`LICENSE.txt` in that repo;
  the OFX legal notice states "GPL-3.0" with no "or later" option).

Profile data descends from **Andrea Volpato's spektrafilm**
(<https://github.com/andreavolpato/spektrafilm>), licensed **CC BY-SA 4.0**;
derivative profile data remains under CC BY-SA 4.0 (one-way compatible with
GPLv3). The spectral upsampling direction follows Johannes Hanika's Hanatos 2025
models / hanatos/vkdt, and the alternate reconstruction path follows Mallett &
Yuksel 2019.

Vendored under `shaders/` from revision `86476af` (byte-identical unless noted):

- Unmodified: `SpektraCopy.comp`, `SpektraFormatConvert.comp`,
  `SpektraFilmExposure.comp`, `SpektraCurveDevelop.comp`, `SpektraHalation.comp`,
  `SpektraDir.comp`, `SpektraScannerPost.comp`
- Modified by Rawr: `SpektraDiffusion.comp`, `SpektraPrintScan.comp`,
  `SpektraGrain.comp`

Vendored headers under `include/spektrafilm/`:

- `SpektraParameters.h`, `SpektraProfileCurves.h`

Build-time generated data under `generated/` + `assets/` (produced by the
upstream `tools/generate_profile_curves.py`, not hand-written):

- `SpektraGeneratedProfileCurves.cpp`, `SpektraGeneratedProfileCounts.h`
- `SpektraHanatos2025Spectra.f32`, `SpektraOutputGamutCompression.f32`

`src/SpektraTables.cpp` ports the upstream CPU table-building helpers
(`makeLinearSensitivity`, `makePackedCurveExposure`,
`makePackedSpectralDensity`, `makeScanProducts`, `makeHanatosRawResponse*`,
`remapHanatosResponseForInputGamutCompression`,
`filteredEnlargerIlluminantCpu`, `makeProcessNegativePaperWeights`,
film/paper gamma helpers) with logic kept identical to
`src/SpektraVulkanRenderer.cpp`. Function-level provenance is noted in
comments.

`reference/` vendors two upstream files unmodified for parity baselining,
except a two-hunk Apple/MoltenVK portability patch in
`SpektraVulkanRenderer.cpp` (`VK_KHR_portability_enumeration` on instance
create, `VK_KHR_portability_subset` on device create — same as
`vk_common/testing/vk_test_common.hpp`):

- `SpektraVulkanRenderer.{h,cpp}`, `SpektraRenderer.h`,
  `SpektraVulkanCopyHarness.cpp`

`shaders/spektra_input.comp` and `shaders/spektra_output.comp` are original
to this module (RGBA16F image <-> float-buffer bridges the unmodified
simulation has no use for). `shaders/spektra_output_vis.comp` and
`shaders/spektra_passthrough.comp` are temporary debug aids, not built by
CMake.

## License implication

Because this module is GPL-3.0-only and is linked into the shipped
`librawrcam_native.so`, the combined Rawr work is conveyed as GPL-3.0-only; the
entire corresponding source is published with the app. Do not enable the
generated profile counts' Academy Printer Density path or otherwise bundle the
upstream SMPTE ST 2065-2 standards data, which is not redistributable.

Outputs rendered with the module (images/video) are the user's own content and
are not restricted. Exporting LUTs derived from the upstream profiles would be
governed by the upstream `SPEKTRAFILM_OFX_LUT_LICENSE.txt` no-resale terms; Rawr
does not currently export such LUTs.
