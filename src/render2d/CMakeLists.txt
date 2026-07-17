add_library(amrvis_render2d
    ImageBuffer.cpp
    ScalarRenderer.cpp
)
add_library(Amrvis::render2d ALIAS amrvis_render2d)

target_include_directories(amrvis_render2d
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(amrvis_render2d PUBLIC Amrvis::core PRIVATE Amrvis::warnings)
target_compile_features(amrvis_render2d PUBLIC cxx_std_20)
