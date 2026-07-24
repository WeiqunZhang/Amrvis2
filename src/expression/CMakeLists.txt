add_library(amrvis_expression Expression.cpp)
add_library(Amrvis::expression ALIAS amrvis_expression)

target_include_directories(amrvis_expression
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(amrvis_expression PRIVATE Amrvis::warnings)
target_compile_features(amrvis_expression PUBLIC cxx_std_20)
