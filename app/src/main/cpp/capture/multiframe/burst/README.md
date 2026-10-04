# Multiframe Burst (future)

Reserved for Mode 3: burst capture producing **many DNGs / JPEGs / RZSL**
(one output set per frame), as opposed to `../mfsr/` (merge-to-one MFSR).

## Status

Stub only. Nothing here is compiled or linked (`CMakeLists.txt` does not
reference this directory). The stub header reserves names so `mfsr/`
is never misused for burst work.

## Planned surface (when implemented)

- `BurstCaptureService`: `start{N,dng[],jpeg[],rzsl}/pollMany/cancel`
- Reuses `../common/` (`MultiframeFrameRing`, `SharedCompletionMailbox`,
  `MultiframeQueueHelpers`, `MultiframeTuning`) and `../../encode/`
  (`AsyncDngWriter`, `AsyncJpegWriter`, `RzslBundleSink`) plus
  `../../develop/` stages. Must NOT import `../mfsr/` merge path.
- Routed from `session/SessionEngine` alongside single-frame and MFSR,
  sharing its `mu_`, AE plan, and camera arming sequence.
