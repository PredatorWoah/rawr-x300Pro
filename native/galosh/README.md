# Galosh NR (native module)

Vulkan port of [GALOSH](https://github.com/luxgrain/GALOSH) (luxgrain, Apache-2.0;
`UPSTREAM_LICENSE`): blind, training-free denoisers for the still JPEG branch.
Two engines, one lib, both live behind the Denoise setting:

- **RAW** (`GaloshRawPipeline`): pre-demosaic Bayer in/out, single-frame
  stills only (skipped for multiframe and the renderer). Grain-safe by
  construction (film grain is injected stages later).
- **YUV** (`GaloshYuvPipeline`): post-tonemap **pre-film-sim** linear-RGB
  in/out, all still paths. Never after grain — the estimator would read grain
  as noise.

DNG save path bypasses both. The profiled wavelet denoiser in
`native/raw_denoise` is the alternative engine; the two are mutually exclusive
per capture (`DenoiseConfig.resolve()`).

## Layout

- `include/galosh/` — `GaloshCommon.hpp` (shader blob map), engine headers
  with full param docs incl. bypass semantics + P1 open items.
  `GaloshVkUtils.hpp` holds the shared host helpers (buffers, images,
  barriers) reused by both engine ports.
- `src/` — `GaloshRawPipeline.cpp` (full o32 port) and
  `GaloshYuvPipeline.cpp`, both with a synchronous `process()`.
- `shaders/` — 43 vendored RAW `.comp` (o32_* + app-authored
  `galosh_bridge_{norm,quant}`), the `yuv_*` set + app-authored
  `galosh_yuv_bridge_*` (the sRGB-wrap `yuv_srgb2ycc`/`yuv_ycc2srgb` are
  superseded by the bridges and not built), shared `.glsl`, and
  `SHADER_LIST` (single source of truth; app CMake filters it).
- `tools/verify_galosh_parity.py` — offline MoltenVK validation using the CMake-built tools.
- `validation/galosh_smoke.cpp` — construction smoke (builds every
  compute pipeline on MoltenVK; catches SPV/DSL/layout errors).
- `tests/golden_metrics.json` — gates + lab reference values.
- `UPSTREAM_PATCHES.patch` — local fixes against upstream @ depth-1 clone:
  Wiener-NaN lane bypasses (reimplemented natively in the port) and
  MoltenVK portability flags (**lab-only**, Adreno lacks the subset ext).

## Validation

CMake builds the module and its optional reference parity tools. The parity tool requires external GALOSH reference executables and DNG
inputs; it is not part of the portable host gate. See the tool's arguments for
input paths. Keep downloaded references and outputs under repository `tmp/`.

## Porting constraints

- Apple Silicon has no correctly-rounded float division (~1 ulp off CPU at
  ~46% of pixels). The R16U normalize bridge therefore uses a host-built
  exact 65536-entry LUT instead of on-device division; without it the
  1-ulp input noise cascades through thresholds to ~66 dB. The quantize
  bridge (mul/add only) is unaffected.
- Buffer sizes must mirror upstream TRI lines one-per-line: merging Ceq
  (cq_f) with C_e (ce_f) once caused a 4x heap under-alloc, caught by
  phase dumps + self-determinism runs, not by reviews.
- `GALOSH_PORT_DUMP=dir` downloads named phase planes for bisection
  against upstream `GALOSH_DUMP_DIR` (names match); zero-cost when unset.
