# Still highlight adapters (app side)

- `LensShadingMapSnapshot.h`: worker-owned copy of Camera2's lens-shading grid,
  exposed to native libraries as `rawr::shading::LensShadingMapView`.
- `replay_tagged_decode.comp`: decodes the legacy tagged replay format (negative
  sign marks a clipped sample) into packed CFA plus clip state for
  `diagnostics/replay/HighlightReplayRunner`. It performs no reconstruction.

Sensor clip classification and CFA packing for demosaic live in
`native/raw_demosaic/common/shaders/sensor_clip_pack.comp`; reconstruction lives
in `native/raw_highlight`.
