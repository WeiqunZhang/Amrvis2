add_library(amrvis_io
    plotfile/PlotfileBlockReader.cpp
    plotfile/PlotfileDataset.cpp
    plotfile/PlotfileMetadataReader.cpp
    plotfile/StandaloneMetadataReader.cpp
)
add_library(Amrvis::io ALIAS amrvis_io)

target_include_directories(amrvis_io
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(amrvis_io
    PUBLIC Amrvis::cache Amrvis::core
    PRIVATE Amrvis::warnings
)
target_compile_definitions(amrvis_io
    PUBLIC AMRVIS_ENABLE_DERIVED_FIELDS=$<BOOL:${AMRVIS_ENABLE_DERIVED_FIELDS}>)
if(AMRVIS_ENABLE_DERIVED_FIELDS)
    target_link_libraries(amrvis_io PRIVATE amrexpr::amrexpr)
    get_target_property(amrvis_amrexpr_include_dirs
        amrexpr::amrexpr INTERFACE_INCLUDE_DIRECTORIES)
    if(amrvis_amrexpr_include_dirs)
        target_include_directories(amrvis_io SYSTEM PRIVATE
            ${amrvis_amrexpr_include_dirs})
    endif()
endif()
target_compile_features(amrvis_io PUBLIC cxx_std_20)
