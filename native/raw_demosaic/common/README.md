# Demosaic input stage

`shaders/sensor_clip_pack.comp` normalizes RAW16, classifies physical
R/G1/G2/B clipping before LSC, applies LSC, and emits both untouched packed CFA
values (RCD/VNG4/Dual input) and an R16_UINT clip-state image. It performs no
reconstruction; the clip state is consumed after demosaic and white balance by
`native/raw_highlight`. DNG writers never consume either resource.

## Embedded shaders

With `RAW_DEMOSAIC_EMBED_SHADERS=ON` (the app sets it), this directory also
builds `raw_demosaic::embedded_shaders`: RCD, VNG4, Dual and quadfix SPIR-V
compiled with the production workgroup sizes and exposed as `ShaderProvider`s
in `raw_demosaic/EmbeddedShaders.hpp`. Standalone validation builds load
SPIR-V from files instead.
