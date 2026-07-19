# Amrvis2

Amrvis2 is a C++20 and Qt 6 desktop application for interactive visualization
of AMReX plotfiles. Its data path is designed around explicit AMR requests,
selective reads, and bounded memory use.

Amrvis2 is one dimension-independent build: the same executable opens 2-D and
3-D plotfiles. The metadata and I/O representation also accepts 1-D datasets
so future 1-D visualization will not require a separate build.

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

## Features

- Open AMReX plotfile directories (2-D and 3-D) and standalone FAB/MultiFab
  headers
- Field and AMR level selection (finest available or one exact level)
- Data ranges from the visible region, a level, or the whole file, plus
  user-defined and logarithmic ranges
- Built-in palettes: rainbow (the legacy Amrvis default), turbo, viridis,
  plasma (matplotlib), parula (MATLAB), coolwarm (the Moreland diverging map
  shared by ParaView/VisIt), and blackbody (a black-body radiation thermal
  ramp); custom palette files (`.pal`) can also be loaded
- Color bar, AMR grid box overlays, and a cursor probe readout
- Rubber-band zoom into a subregion
- Contour lines and vector glyph overlays
- XY line plots through the data
- 3-D orthogonal slice views with crosshairs and an isometric wireframe
- Slice sweep and plotfile-sequence animation
- PNG image export and ASCII slice data export
- Preference persistence across sessions

## Usage

Launch with one or more dataset paths:

```bash
amrvis2 plotfile-dir                  # open one dataset
amrvis2 plt00000 plt00010 ...         # open a plotfile sequence (two or more)
```

File → Open Plotfile Directory... opens an AMReX plotfile directory; File →
Open Standalone FAB/MultiFab... opens a single FAB or MultiFab header;
File → Open Plotfile Sequence... opens a multi-select dialog for two or more
plotfile directories (click, Ctrl-click, Shift-click); each selected directory
is verified by structure (a Header file plus Level_0, Level_1, ...
subdirectories). The "...in New Window" variants open a second, independent
Amrvis2 window (its own dataset, cache, and view state) for side-by-side
comparison.

Mouse:

- Left click: probe the value under the cursor
- Left drag: zoom to the rubber-band subregion
- Middle drag: horizontal line plot (2-D); in 3-D moves the slice along the
  vertical in-view axis (hold Shift or Ctrl for a line plot)
- Right drag: vertical line plot (2-D); in 3-D moves the slice along the
  horizontal in-view axis (hold Shift or Ctrl for a line plot)
- Mouse wheel: zoom; double click: refit to the window

Keys: B toggles grid boxes, 0 fits to the window, 1-6 pick fixed scales
(1x-32x), Ctrl+0 composites the finest available level, Ctrl+1-9 show one
exact level. Ctrl+D opens a Dataset window listing the visible region's raw
cell values per AMR level.

View → Number Format... sets the printf-style readout format (default
`%7.5f`).

Headless smoke hooks (used by the test suite under QT_QPA_PLATFORM=offscreen):

```bash
amrvis2 --smoke-test <plotfile>        # metadata only
amrvis2 --slice-smoke-test <plotfile>  # metadata + initial slice
amrvis2 --sequence-smoke-test <pltA> <pltB>
```

Preferences persist through QSettings (organization and application name
Amrvis2), e.g. `~/.config/Amrvis2/Amrvis2.conf` on Linux.
