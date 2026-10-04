# Highlight reconstruction

This module owns both highlight reconstruction methods. They are selected by the
`capture_highlight_method` setting:

| Method | Name | Origin | Domain |
|---|---|---|---|
| 0 | Colour propagation | In-house | White-balanced camera RGB |
| 1 | Inpaint Opposed | RawTherapee 5.13 "Coloropp" | Pre-WB demosaiced camera RGB, plus an SDR-only tone tap |

Clip classification happens before either method, in sensor space and before
LSC/WB: `native/raw_demosaic/common/shaders/sensor_clip_pack.comp` for stills,
`raw_preview_cfa_state.comp` for preview, the video RAW stage for video, and
`highlight_clip_state.comp` when only RGB is available. DNG writers never
consume reconstructed RGB or the clip-state image.

## Colour propagation (method 0)

With reconstruction **off**, every white-balanced channel is hard-limited to
the lowest post-WB channel ceiling. This common clamp prevents unequal WB
headroom from becoming false magenta and leaves highlight rolloff to the
colour transform and tonemapper.

With reconstruction **on**:

1. `highlight_guide_seed.comp` builds a low-resolution colour guide from bright,
   fully valid, locally smooth pixels.
2. `highlight_guide_propagate.comp` fills clipped regions by jump flooding.
3. `highlight_guide_smooth.comp` runs four smoothing passes.
4. The apply pass fits the guide to the channels that still carry detail and
   raises only the unreliable ones (`raw_highlight_post_rgb.glsl`). If every
   channel is lost, the result converges to a bright neutral core.

`GuideChain` (host) records steps 1–3; each caller owns the apply pass:

- Preview: `highlight_apply_inplace.comp`, `RAWR_HL_SCALE=1` (half-resolution
  image, 4 px guide cells).
- Still and video: `highlight_recover.comp`, `RAWR_HL_SCALE=2` (full
  resolution, 8 px guide cells), through `PostDemosaicProcessor`.

Both cover 8 sensor pixels per guide cell and repair the same pixels: any
channel above 85% of its post-WB ceiling blends continuously into repair, so
preview matches the saved image.

Preview requires these shaders: `RawPreview` refuses to start without them
rather than falling back to a different guide. (`raw_preview.comp`, the path
without CFA state used by validation and diagnostics, keeps a local inline
guide.)

## Inpaint Opposed (method 1)

`coloropp.comp` repairs clipped channels from the cube-root mean of the opposed
channels in a 3×3 neighbourhood. It runs on pre-WB RGB with lens shading. The
post-WB stage then runs `highlight_recover.comp` in passthrough mode (2).
`coloropp_tone.comp` applies RawTherapee's analytic highlight compression to
the SDR render only; the HDR gain-map tap is unaffected. Host code:
`raw_highlight/ColoroppProcessor.hpp`, driven by `native/post_demosaic`.

Preview, still and video all run the selected method. Preview compiles
`coloropp.comp` with `RAWR_COLOROPP_POST_WB=1` because its RGB is already white
balanced; the clip test and opposed mean are algebraically the same. It writes
the repaired image to its linear output (read by RAW overlays and scopes) and
the tone-compressed copy to a separate image that tonemap and film read, like
the still HDR and SDR taps. `raw_highlight/Coloropp.hpp` holds the parameter
mapping both hosts use.

## Related controls

The tonemap **Highlights** slider is separate. Negative values compress
scene-linear highlight luminance; positive values lift the localized display
highlight range. It never enables or tunes channel reconstruction.
