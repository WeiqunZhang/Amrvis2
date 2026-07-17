# Legacy workflow: 2-D multilevel plotfile

- Workflow ID: `legacy-2d-multilevel`
- Captured: 2026-07-16 on `kiryu`
- Legacy source: Amrvis `5b13be0563b6333ecf8c5c7267f8b802ac567b07`
- AMReX source: `bb6fcb8b3c7af81c525f097c245f79a2b9ff4b34`
- Executable SHA-256: `22a68d39d9664f61c9e2e580bf86c65b319de5c5049b3ec66d9819ad0ba5bd3d`
- Dataset: `datasets/plotfile-sequence/plt00001`
- Normalized dataset tar SHA-256: `ebdbb363cfff489b1746de9183fdd129e773ebe9a9fce10c87497182f78f5eb0`

## Display capture

```text
amrvis2d.gnu.ex -newplt -initialderived phi -showboxes true \
  -palette legacy-outputs/Palette -useminmax 0 1 \
  datasets/plotfile-sequence/plt00001
```

Observed state:

- the title reported levels `0:1`, finest level `1`, and domain `((0,0) (511,511))`;
- scalar component `phi` was selected;
- the color bar showed the explicit user range `[0, 1]`;
- the configured legacy palette and grid-box overlay were visible;
- the multilevel view displayed the fine patch over the coarse layout.

Reference image: `legacy-outputs/legacy-plt00001-phi-boxes-range-0-1.png`,
SHA-256 `6c0334d9e4c1c3efcb06472cccf000ad8364db0a2c7c5805e5b007044f318b4f`.

## Batch slice capture

```text
amrvis2d.gnu.ex -newplt -zslice 0 -sliceallvars -initialderived phi \
  datasets/plotfile-sequence/plt00001
```

The legacy program reported the finest-level slice box
`((0,0) (511,511) (0,0))` and wrote
`legacy-outputs/plt00001.zslice.0.Level_1.fab` (4,194,388 bytes), SHA-256
`ed6029f5f879e2e59468023577e37072fae056f90b56a2f934b599f9f15fa72a`.

This workflow is measured evidence for multilevel plotfiles, scalar component
selection, finest-over-coarse display, user value range, palette/color bar,
and grid boxes. It is not evidence for Amrvis image export; the PNG was an
external lossless X11 window capture.
