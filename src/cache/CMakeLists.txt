add_library(amrvis_cache CacheMetrics.cpp)
add_library(Amrvis::cache ALIAS amrvis_cache)

target_include_directories(amrvis_cache
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(amrvis_cache PUBLIC Amrvis::core PRIVATE Amrvis::warnings)
target_compile_features(amrvis_cache PUBLIC cxx_std_20)
