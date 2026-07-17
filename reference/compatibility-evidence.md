# Compatibility requirement evidence

Phase 0 cannot exit while any `Yes` commitment below lacks measured usage
evidence or an explicit human-maintainer scope decision. An agent may add
evidence but may not change the required classification.

| Capability | Required | Evidence or decision | Corpus workflow |
|---|---:|---|---|
| Standalone FArrayBox | Yes | Measured legacy open/display | `legacy-standalone-data` |
| Standalone MultiFab | Yes | Measured legacy open/display with nonzero origin | `legacy-standalone-data` |
| Multilevel plotfile | Yes | Measured two-level open/display and batch slice | `legacy-2d-multilevel` |
| Scalar component selection | Yes | Measured `phi` and `q` selection | `legacy-2d-multilevel`, `legacy-3d-orthogonal` |
| Fine-over-coarse slices | Yes | Measured two-level composed display and finest-level batch slice | `legacy-2d-multilevel` |
| User value range | Yes | Measured explicit `[0,1]` display range | `legacy-2d-multilevel`, `legacy-3d-orthogonal` |
| File/level/region ranges | Yes | Missing | TBD |
| Palette and color bar | Yes | Measured explicit palette and visible color bar | `legacy-2d-multilevel`, `legacy-3d-orthogonal` |
| Grid boxes | Yes | Measured enabled grid overlay | `legacy-2d-multilevel`, `legacy-3d-orthogonal` |
| Point probe | Yes | Missing | TBD |
| Line plot | Yes | Missing | TBD |
| Contours | Yes | Missing | TBD |
| Vector overlay | Yes | Missing | TBD |
| Orthogonal 3-D slices | Yes | Measured simultaneous XY/YZ/XZ views | `legacy-3d-orthogonal` |
| Slice animation | Yes | Missing | TBD |
| Plotfile sequence | Yes | Missing | TBD |
| Image export | Yes | Missing | TBD |
