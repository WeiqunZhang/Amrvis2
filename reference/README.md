# Legacy compatibility corpus

Phase 0 is a completion gate. This directory records the exact legacy source,
build environment, workflows, evidence, outputs, and checksums used to define
compatibility. The corpus is currently incomplete and must not be represented
as a finished reference artifact.

The local legacy checkout is `external/Amrvis/`. It is a read-only input and is
not part of the modern application build.

## Required artifact contents

- `manifest.toml`: corpus identity, source revisions, and artifact state.
- `environment/`: captured build and runtime environments.
- `compatibility-evidence.md`: evidence or human decision for each required capability.
- `workflows/`: reproducible commands and expected outputs.
- `artifacts/`: versioned corpus archives, excluded until deliberately produced.
- `checksums.sha256`: checksums for tracked corpus metadata and archived artifacts.

Capture and verification helpers live under `tools/reference_capture/`.

The current `0.1.0-incomplete` archive has been assembled and clean-restore
verified locally. It remains excluded from source control and has no durable
project-controlled publication location, so Phase 0 remains open. The external
`manifest.toml` records the archive checksum after packaging; an archive cannot
contain its own SHA-256 checksum without changing that checksum.
