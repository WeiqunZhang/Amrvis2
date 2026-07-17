# Legacy Amrvis 3-D build record

- Result: success
- Elapsed build time: 75 seconds
- Source archive SHA-256: `89675b52b9b3ea5f7203634eec88a79cc181e075c08b5bc1315f0625eea9318f`
- Executable SHA-256: `8ce6cd3d5fc3881986b9cc0706800f031dd8193f8d16f26bcb5a9a7a0c2e8d9e`
- C++ standard selected by AMReX: C++20

```text
tools/reference_capture/build_legacy.sh \
  external/Amrvis /home/wqzhang/git/amrex-codes/amrex \
  /tmp/amrvis2-legacy-3d 3
```

Equivalent make options:

```text
DIM=3 USE_MPI=FALSE USE_VOLRENDER=FALSE COMP=gnu DEBUG=FALSE
```

This dimensional build exists only because legacy Amrvis and AMReX's legacy
integration compile against `AMREX_SPACEDIM`. Amrvis2 itself is built once and
uses dataset dimensionality at runtime.
