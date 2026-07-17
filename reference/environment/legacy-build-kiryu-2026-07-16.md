# Legacy 2-D build record

- Result: success
- Elapsed build time: 71 seconds
- Legacy source: `5b13be0563b6333ecf8c5c7267f8b802ac567b07`
- AMReX source: `bb6fcb8b3c7af81c525f097c245f79a2b9ff4b34`
- Build source: clean `git archive` extraction in `/tmp`
- Source archive SHA-256: `89675b52b9b3ea5f7203634eec88a79cc181e075c08b5bc1315f0625eea9318f`
- Executable SHA-256: `22a68d395b51fa1f3cd19fdfe9b37cafee510b6cac6fd39ffa2413484e4cbd3d`

Command:

```text
make -j4 AMREX_HOME=<amrex-source> DIM=2 USE_MPI=FALSE USE_VOLRENDER=FALSE COMP=gnu DEBUG=FALSE
```

The link completed against Motif/X11. Compilation emitted legacy bounded-buffer
`sprintf` warnings in `Dataset.cpp`, `PltApp.cpp`, `PltAppOutput.cpp`, and
`XYPlotParam.cpp`; no warnings were promoted to errors. The build was a
feasibility probe only. Runtime reference outputs and screenshots have not yet
been captured, so Phase 0 remains incomplete.

