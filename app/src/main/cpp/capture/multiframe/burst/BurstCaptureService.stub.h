// Reserved interface sketch for multiframe burst (many-output) mode.
// NOT COMPILED: this file uses a .stub.h suffix precisely so no build
// target picks it up. See README.md in this directory.
//
// When burst mode is implemented, rename this to BurstCaptureService.h,
// implement BurstCaptureService.cpp beside it, wire both into
// app/src/main/cpp/CMakeLists.txt, and route it from session/SessionEngine
// alongside single-frame and MFSR (sharing its mu_, AE plan, and camera
// arming sequence).
//
// Planned surface (non-binding):
//   class BurstCaptureService {
//    public:
//     struct BurstRequest { int count; /* per-frame dng/jpeg fds + rzsl sink */ };
//     bool start(const BurstRequest& request);  // single-flight + 1-deep queue,
//                                               // same protocol as single/mfsr
//     bool pollMany();                          // drain per-frame completions
//     void cancel();
//   };
//
// Constraints (frozen pipeline):
//   - Reuse capture/multiframe/common/ (ring, mailbox, queue helpers, tuning).
//   - Reuse encoding/ writers and develop/ stages.
//   - Must NOT depend on capture/multiframe/mfsr/ merge path.
#pragma once
