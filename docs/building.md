# Building Amrvis2

Amrvis2 requires a C++20 compiler, CMake 3.25 or newer, and Ninja for the
provided presets. Qt 6.4 or newer is required only by the desktop target.
There is no build-time dataset dimension: one executable reads 2-D and 3-D
data, and the same runtime metadata representation accepts 1-D data.

## Presets

```text
cmake --preset default     # Release build → build/
cmake --build --preset default
ctest --preset default

cmake --preset debug       # Debug build → build-debug/
cmake --build --preset debug
ctest --preset debug

cmake --preset headless    # No Qt, core library + tools + tests → build-headless/
cmake --build --preset headless
ctest --preset headless

cmake --preset sanitizers  # Headless + ASan + UBSan → build-sanitizers/
cmake --build --preset sanitizers
ctest --preset sanitizers
```

The sanitizer preset enables AddressSanitizer and UndefinedBehaviorSanitizer.

On macOS, Qt builds produce `build/src/qt/Amrvis2.app` by default. The bundle
contains its executable at `Contents/MacOS/Amrvis2` and can be installed into a
user application directory with:

```bash
cmake --install build --prefix "$HOME/Applications"
```

Configure with `-DAMRVIS_BUILD_MACOS_APP_BUNDLE=OFF` to retain the plain
`build/src/qt/amrvis2` executable layout.

## Compiler matrix

| Platform | Compiler | State |
|---|---|---|
| Ubuntu 24.04 | GCC | GitHub CI (full Qt) |
| Ubuntu 24.04 | Clang | GitHub CI (full Qt) |
| macOS 15 | AppleClang | GitHub CI (full Qt) |
| Windows Server 2022 | MSVC 2022 | GitHub CI (full Qt) |

All CI builds treat compiler warnings as errors. An additional headless
Ubuntu/GCC job runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer.
