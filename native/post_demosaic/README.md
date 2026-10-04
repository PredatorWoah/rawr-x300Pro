# Post-demosaic processing

`rawr::post::PostDemosaicProcessor` owns the GPU stages after demosaic:
optional profiled denoise (`raw_denoise`), white balance, either highlight
method (`raw_highlight`), false-colour correction, optional defringe, and the
SDR Inpaint Opposed tone tap. Still capture, video and the DNG renderer use
separate instances of this implementation. The app owns session, frame and
encoder resources; Kotlin passes processing settings and receives status.

Layout: `include/post_demosaic/`, `src/`, and `shaders/` for the stages
this package owns (white balance, defringe). `CMakeLists.txt` also embeds the
raw_highlight, raw_denoise and false-colour SPIR-V it dispatches.
