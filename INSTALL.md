# Installing Amrvis2

Two options: build from source, or download a prebuilt AppImage.
Tested on Ubuntu 24.04 and macOS.

## Option 1: Build from source

### Linux (Ubuntu)

Install the dependencies:

```bash
sudo apt install g++ cmake ninja-build qt6-base-dev
```

### Linux (all distros)

Install the dependencies with [Spack](https://github.com/spack/spack):
```bash
spack install qt-base
spack load qt-base
cmake --preset default
cmake --build --preset default
./build/src/qt/amrvis2 /path/to/plotfile
```

### macOS

Install the dependencies with [Homebrew][]:

```bash
brew install cmake ninja qt6
```

[Homebrew]: https://brew.sh

**Optional:** `ffmpeg` is needed to encode animation exports (MP4).

```bash
# Ubuntu
sudo apt install ffmpeg
# macOS
brew install ffmpeg
```

Build (both platforms):

```bash
git clone https://github.com/WeiqunZhang/Amrvis2.git
cd Amrvis2
cmake --preset default
cmake --build --preset default
```

On Linux, the executable is `build/src/qt/amrvis2`. Run it with a plotfile:

```bash
./build/src/qt/amrvis2 /path/to/plotfile
```

On macOS, the default build is `build/src/qt/Amrvis2.app`. It can be opened
from Finder, launched with `open build/src/qt/Amrvis2.app`, or run with a
plotfile from the command line:

```bash
./build/src/qt/Amrvis2.app/Contents/MacOS/Amrvis2 /path/to/plotfile
```

Install it for the current user with:

```bash
cmake --install build --prefix "$HOME/Applications"
```

Set `-DAMRVIS_BUILD_MACOS_APP_BUNDLE=OFF` when configuring to build the plain
`amrvis2` executable on macOS instead. On Linux, install system-wide with:

```bash
sudo cmake --install build --prefix /usr/local
```

## Option 2: Prebuilt AppImage

> **Not yet available.** Prebuilt AppImages will be published on the
> [GitHub Releases][] page once the first release is tagged.

An AppImage is a self-contained executable that runs on any modern Linux
distribution without installing packages. When available:

1. Download `Amrvis2-*.AppImage` from the [GitHub Releases][] page.
2. Make it executable: `chmod +x Amrvis2-*.AppImage`
3. Run it: `./Amrvis2-*.AppImage /path/to/plotfile`

[GitHub Releases]: https://github.com/WeiqunZhang/Amrvis2/releases

## Next steps

See the **[Amrvis2 User Guide](docs/user-guide.md)** for opening plotfiles,
navigating 2-D and 3-D data, selecting AMR levels, animation, export, and
troubleshooting. The same guide is bundled in the application under **Help >
User Guide...**.
