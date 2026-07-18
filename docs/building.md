# Building Amrvis2

Amrvis2 requires a C++20 compiler, CMake 3.25 or newer, and Ninja for the
provided presets. Qt 6.4 or newer is required only by the desktop target.
There is no build-time dataset dimension: one executable reads 2-D and 3-D
data, and the same runtime metadata representation accepts 1-D data.

## Presets

```text
cmake --preset default
cmake --build --preset default
ctest --preset default

cmake --preset headless
cmake --build --preset headless
ctest --preset headless

cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers
```

The sanitizer preset enables AddressSanitizer and UndefinedBehaviorSanitizer.

## Compiler matrix

| Platform | Compiler | State |
|---|---|---|
| Ubuntu 24.04 | GCC 13.3 | Locally built and tested |
| Linux | Clang with C++20 support | CI target; validation pending |
| macOS | AppleClang with C++20 support | CI target; validation pending |

Only the first row is currently verified.
