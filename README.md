# Amrvis2

Amrvis2 is a C++20 and Qt 6 modernization of Amrvis. Its data path is designed
around explicit AMR requests, selective reads, and bounded memory use. The
project is being implemented according to [PLAN.md](PLAN.md).

Amrvis2 is one dimension-independent build: the same executable opens 2-D and
3-D plotfiles. The metadata and I/O representation also accepts 1-D datasets so
future 1-D visualization will not require a separate build. Dimension-specific
builds under `tools/reference_capture/` are only for recording legacy behavior.

The legacy Amrvis source is available locally at `external/Amrvis/` as a
read-only behavioral reference. It is intentionally excluded from this
repository's source set and build.

## Build

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

For a build without Qt:

```bash
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
```

For AddressSanitizer and UndefinedBehaviorSanitizer validation:

```bash
cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers
```

See [docs/building.md](docs/building.md) for the current compiler matrix.

The optional VTK module is deliberately unavailable until a Qt 6-compatible
VTK configuration and the bounded volume-query contract are implemented.
