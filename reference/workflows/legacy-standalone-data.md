# Legacy workflow: standalone FAB and MultiFab

- Workflow ID: `legacy-standalone-data`
- Captured: 2026-07-16 on `kiryu`
- Legacy source: Amrvis `5b13be0563b6333ecf8c5c7267f8b802ac567b07`
- AMReX source: `bb6fcb8b3c7af81c525f097c245f79a2b9ff4b34`
- Executable SHA-256: `22a68d39d9664f61c9e2e580bf86c65b319de5c5049b3ec66d9819ad0ba5bd3d`

## FArrayBox

Input: `legacy-outputs/plt00001.zslice.0.Level_1.fab`, produced by workflow
`legacy-2d-multilevel`, SHA-256
`ed6029f5f879e2e59468023577e37072fae056f90b56a2f934b599f9f15fa72a`.

```text
amrvis2d.gnu.ex -fab -palette legacy-outputs/Palette \
  legacy-outputs/plt00001.zslice.0.Level_1.fab
```

The legacy UI opened the complete `((0,0) (511,511))` FAB as component
`Fab_0`. Reference image
`legacy-outputs/legacy-standalone-fab.png`, SHA-256
`5886d171df972e18b500fbd86b107c17e1bf56ab6bc6a2edea99123238ebb4f9`.

## MultiFab with nonzero origin

Inputs:

- `datasets/standalone-multifab/inmf_H`, SHA-256
  `0347fd92f097fe9aa8e5cbec87475d986ce4636bb2ca34441bfdd7ae2d96499f`;
- `datasets/standalone-multifab/inmf_D_00000`, SHA-256
  `175f16e192e0956f684cc95e3cac1938e72aaebf87d4e335c8ce86bd262c2290`.

```text
amrvis2d.gnu.ex -mf -palette legacy-outputs/Palette \
  datasets/standalone-multifab/inmf
```

The legacy UI opened the region `((88,152) (175,231))`, rendered component
`MultiFab_0`, displayed its 30 grid boxes, and reported a value range from
`1.00` to approximately `1.98`. Reference image
`legacy-outputs/legacy-standalone-multifab-nonzero-origin.png`, SHA-256
`56765de90079cad9ed8dc9e1df67fdbe416fb74a1acf8b4e8810a30ae88c1381`.

This workflow is measured evidence for opening standalone FArrayBox and
MultiFab data, including a nonzero integer domain origin.
