cmake_minimum_required(VERSION 3.25)

project(
    Amrvis2
    VERSION 0.1.0
    DESCRIPTION "Demand-driven AMR visualization"
    LANGUAGES CXX
)

option(AMRVIS_ENABLE_QT "Build the Qt 6 desktop application" ON)
option(AMRVIS_ENABLE_VTK "Build the optional VTK volume module" OFF)
option(AMRVIS_ENABLE_MPI "Enable MPI-aware backends" OFF)
option(AMRVIS_BUILD_TESTS "Build unit and integration tests" ON)
option(AMRVIS_BUILD_MACOS_APP_BUNDLE
    "Build the Qt application as a macOS .app bundle" ${APPLE})
option(AMRVIS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(AMRVIS_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(AMRVIS_ENABLE_DERIVED_FIELDS
    "Enable parser-backed derived-field expressions" ON)
option(AMRVIS_USE_SYSTEM_AMREXPR
    "Use an installed amrex-parser package instead of the pinned source dependency" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AmrvisWarnings)

if(AMRVIS_ENABLE_DERIVED_FIELDS)
    if(AMRVIS_USE_SYSTEM_AMREXPR)
        find_package(amrexpr 1.0 CONFIG REQUIRED)
    else()
        include(FetchContent)
        set(ENABLE_CUDA OFF)
        set(ENABLE_HIP OFF)
        set(ENABLE_SYCL OFF)
        set(ENABLE_TESTS OFF)
        FetchContent_Declare(amrex_parser
            GIT_REPOSITORY https://github.com/AMReX-Codes/amrex-parser.git
            GIT_TAG 559e0d8e026823af0e0afd7caa0e4cb5d32eab1d
            GIT_SHALLOW TRUE
            EXCLUDE_FROM_ALL
        )
        FetchContent_MakeAvailable(amrex_parser)
        add_library(amrexpr::amrexpr ALIAS amrexpr)
    endif()
endif()

if(AMRVIS_ENABLE_SANITIZERS)
    include(AmrvisSanitizers)
    amrvis_enable_sanitizers()
endif()

if(AMRVIS_ENABLE_MPI)
    find_package(MPI REQUIRED COMPONENTS CXX)
endif()

add_subdirectory(src/core)
add_subdirectory(src/cache)
add_subdirectory(src/io)
add_subdirectory(src/query)
add_subdirectory(src/render2d)

if(AMRVIS_ENABLE_QT)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Concurrent Core Gui Widgets)
    add_subdirectory(src/qt)
endif()

if(AMRVIS_ENABLE_VTK)
    message(FATAL_ERROR
        "AMRVIS_ENABLE_VTK is not yet supported. The system VTK package "
        "is Qt 5-based and must not be linked into this Qt 6 application.")
endif()

add_subdirectory(tools/dataset_inspect)
add_subdirectory(tools/query_benchmark)
add_subdirectory(tools/line_repro)

if(AMRVIS_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
