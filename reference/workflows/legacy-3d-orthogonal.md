# Legacy workflow: 3-D orthogonal slices

- Workflow ID: `legacy-3d-orthogonal`
- Captured: 2026-07-16 on `kiryu`
- Legacy source: Amrvis `5b13be0563b6333ecf8c5c7267f8b802ac567b07`
- AMReX source: `bb6fcb8b3c7af81c525f097c245f79a2b9ff4b34`
- Executable SHA-256: `8ce6cd3d5fc3881986b9cc0706800f031dd8193f8d16f26bcb5a9a7a0c2e8d9e`
- Dataset: `datasets/plotfile-3d/plt001`
- Normalized dataset tar SHA-256: `9939a774291dedb2f41a27defce905f99b9731a916b61de4c6025cd371604e0f`

The reference-only legacy executable was compiled with `DIM=3`; this does not
apply to Amrvis2, which is one runtime-dimension-aware build.

```text
amrvis3d.gnu.ex -newplt -initialderived q -initplanes 32 32 32 \
  -showboxes true -palette legacy-outputs/Palette -useminmax 0 1 \
  datasets/plotfile-3d/plt001
```

The legacy UI opened one 3-D plotfile and simultaneously displayed its XY, YZ,
and XZ orthogonal views at indices 32, with scalar component `q`, the explicit
range `[0, 1]`, color bar, and grid boxes. Reference image
`legacy-outputs/legacy-3d-orthogonal-q-range-0-1.png`, SHA-256
`27a540844931c50856cee36237d79a2ecbedcf43a4d450dfdeb97797f25108dd`.

This is measured evidence for the required orthogonal 3-D slice workflow and
for scalar selection/range/palette behavior in that view.
