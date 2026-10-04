# vng4_demosaic 0.6.0 (frozen)

Vulkan VNG4 demosaic for Rawr stills and the DNG renderer. RAW/CFA input and
scene-linear RGBA16F output semantics mirror the RCD demosaic contract. It
follows **unclamped librtprocess VNG4** rather than RawTherapee's `max(0, ...)`
reconstruction clamps, preserving negative scene-linear values and >1
highlight headroom.

## Passes

1. `vng4_linear.comp`: four-colour VNG4 initial interpolation.
2. `vng4_green.comp`: 64-term, 8-direction green reconstruction. 16x16
   workgroups, each invocation emitting two horizontal pixels (dispatch
   coverage 32x16).
3. `vng4_export.comp`: librtprocess-compatible R/B reconstruction with shared
   green and CFA tiles, the exact 3-pixel Bayer border, and output scaling.
   `vng4_export_blend.comp` is the Dual (RCD+VNG4) blend variant.

`vng4_green_diagnostic.comp` is an optional single-pixel diagnostic. It is optional: the app's embedded shader set omits it,
and `recordGreenDiagnostic()` then throws.

## Production status

Frozen after Adreno 840 validation. 4080x3064 medians: linear 4.01 ms, green
7.76 ms, export 5.10 ms, total 16.87 ms. Full real-DNG canonical CPU/GPU oracle:
interior max 9.8e-4, mean 2.2e-5, sampled p99 2.4e-4; border max 1.2e-4.

Freeze policy: do not change VNG4 mathematics, the pair-x mapping, dispatch
coverage, or the export architecture for tuning. Reopen only for a correctness
defect, an integration requirement, or a new supported contract or geometry.

The green and export candidate studies that led here (cache, unrolled,
pattern-static, tiled, quad and pair variants, with their compile, benchmark
and audit scripts) are in git history before their removal in the
native-cleanup branch.

## Validation

Use the module's CMake test targets for CPU/GPU parity. Reference implementations
and fixtures remain alongside the module; one-off shell benchmarks were removed.

Supported 2x2 Bayer patterns: RGGB, GRBG, GBRG, BGGR.
