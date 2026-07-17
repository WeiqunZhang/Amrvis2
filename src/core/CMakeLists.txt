add_library(amrvis_core
    Metadata.cpp
    Request.cpp
    Statistics.cpp
)
add_library(Amrvis::core ALIAS amrvis_core)

target_include_directories(amrvis_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(amrvis_core PRIVATE Amrvis::warnings)
target_compile_features(amrvis_core PUBLIC cxx_std_20)
