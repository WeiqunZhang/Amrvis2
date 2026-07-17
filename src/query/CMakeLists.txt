add_library(amrvis_query SliceQuery.cpp)
add_library(Amrvis::query ALIAS amrvis_query)

target_include_directories(amrvis_query
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(amrvis_query
    PUBLIC Amrvis::core Amrvis::io
    PRIVATE Amrvis::warnings
)
target_compile_features(amrvis_query PUBLIC cxx_std_20)

