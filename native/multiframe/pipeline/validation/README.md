# Multiframe validation

The merge is implemented directly in Vulkan. The external numerical oracle is upstream release 1.4 at revision `39ce4aa`, with the two corrections recorded in `upstream_v2_reference.json`. A second full CPU implementation is intentionally not maintained.

Persist a live burst with **Debug → Dump RZSL on Shutter**. `rawr::zsl_container::readBundle()` reads that deterministic container and reconstructs decoder-ready `[meta][sizes][offsets][payload]` packets for replay tools. Golden and Vulkan runs should export identically named little-endian float32 checkpoints for flow, covariance, robustness, support, accumulation, and output.

Compare a candidate checkpoint directory with:

```sh
uv run --no-project tools/multiframe_validation/compare_checkpoints.py golden/manifest.json candidate/
```

The manifest owns per-stage absolute and RMSE tolerances. Final acceptance also requires same-burst inspection for ghosting, zipper artifacts, noise texture, detail, and highlight stability on the target device.
