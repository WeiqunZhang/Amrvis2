# Amrvis2

Amrvis2 is a C++20 and Qt 6 desktop application for interactive visualization
of AMReX plotfiles. Its data path is designed around explicit AMR requests,
selective reads, and bounded memory use.

Amrvis2 is one dimension-independent build: the same executable opens 2-D and
3-D plotfiles. The metadata and I/O representation also accepts 1-D datasets
so future 1-D visualization will not require a separate build.

## Installation

See **[INSTALL.md](INSTALL.md)** for end-user instructions — build from source
or download a prebuilt AppImage.

Quick start (Ubuntu 24.04):

```bash
sudo apt install g++ cmake ninja-build qt6-base-dev
cmake --preset default
cmake --build --preset default
./build/src/qt/amrvis2 /path/to/plotfile
```

On macOS, the default preset instead produces
`build/src/qt/Amrvis2.app`. Open it in Finder, launch it with `open`, or run its
executable directly:

```bash
open build/src/qt/Amrvis2.app
./build/src/qt/Amrvis2.app/Contents/MacOS/Amrvis2 /path/to/plotfile
```

## Development

Additional CMake presets and test presets:

```bash
cmake --preset debug       # Debug build → build-debug/
cmake --preset headless    # No Qt, core library + tools + tests → build-headless/
cmake --preset sanitizers  # Headless + ASan + UBSan → build-sanitizers/
ctest --preset default     # Run tests
```

See [docs/building.md](docs/building.md) for the current compiler matrix.

### Building an AppImage

```bash
# Build and install into a staging directory
cmake --preset default
cmake --build --preset default
DESTDIR=$(pwd)/appdir cmake --install build --prefix /usr

# Download linuxdeploy and the Qt plugin
wget -O linuxdeploy.AppImage \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget -O linuxdeploy-plugin-qt.AppImage \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x linuxdeploy.AppImage linuxdeploy-plugin-qt.AppImage

# Bundle (skip QML scanning — Amrvis2 uses only Qt Widgets)
export QMAKE=/usr/bin/qmake6
QML_SOURCES_PATHS=. ./linuxdeploy.AppImage --appdir appdir \
  --executable appdir/usr/bin/amrvis2 \
  --desktop-file resources/amrvis2.desktop \
  --icon-file resources/amrvis2.png \
  --output appimage \
  --plugin qt
```

The optional VTK module is deliberately unavailable until a Qt 6-compatible
VTK configuration and the bounded volume-query contract are implemented.

## Features

- Open AMReX plotfile directories (2-D and 3-D) and standalone FAB/MultiFab
  headers
- Read IEEE-32 and IEEE-64 FAB payloads in their native storage precision;
  physical bounds, cell sizes, time, and view selections use double precision
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
- Middle click: horizontal line plot (2-D); in 3-D moves the slice along the
  horizontal in-view axis (hold Shift or Ctrl for a line plot)
- Right click: vertical line plot (2-D); in 3-D moves the slice along the
  vertical in-view axis (hold Shift or Ctrl for a line plot)
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
