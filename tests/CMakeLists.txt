add_executable(test_core_metadata unit/test_core_metadata.cpp)
target_link_libraries(test_core_metadata PRIVATE Amrvis::core Amrvis::warnings)
add_test(NAME core_metadata COMMAND test_core_metadata)

add_executable(
    test_expression
    unit/test_expression.cpp
    "${PROJECT_SOURCE_DIR}/src/expression/Expression.cpp"
)
target_include_directories(
    test_expression PRIVATE "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(
    test_expression PRIVATE Amrvis::warnings Threads::Threads
)
add_test(NAME expression COMMAND test_expression)

add_executable(test_geometry unit/test_geometry.cpp)
target_link_libraries(test_geometry PRIVATE Amrvis::core Amrvis::warnings)
add_test(NAME geometry COMMAND test_geometry)

add_executable(test_stop_token unit/test_stop_token.cpp)
target_compile_definitions(
    test_stop_token PRIVATE AMRVIS_TEST_FORCE_FALLBACK_STOP_TOKEN
)
target_link_libraries(test_stop_token PRIVATE Amrvis::core Amrvis::warnings)
add_test(NAME stop_token_fallback COMMAND test_stop_token)

add_executable(test_byte_lru_cache unit/test_byte_lru_cache.cpp)
target_link_libraries(test_byte_lru_cache PRIVATE Amrvis::cache Amrvis::warnings)
add_test(NAME byte_lru_cache COMMAND test_byte_lru_cache)

add_executable(test_cache_config unit/test_cache_config.cpp)
target_include_directories(test_cache_config PRIVATE "${CMAKE_SOURCE_DIR}/src/qt")
target_link_libraries(test_cache_config PRIVATE Amrvis::warnings)
add_test(NAME cache_config COMMAND test_cache_config)
set_tests_properties(cache_config PROPERTIES
    ENVIRONMENT "AMRVIS_CACHE_SIZE_MB=768"
)

add_executable(test_scalar_renderer unit/test_scalar_renderer.cpp)
target_link_libraries(test_scalar_renderer PRIVATE Amrvis::render2d Amrvis::warnings)
add_test(NAME scalar_renderer COMMAND test_scalar_renderer)

add_executable(test_palette unit/test_palette.cpp)
target_link_libraries(test_palette PRIVATE Amrvis::render2d Amrvis::warnings)
add_test(
    NAME palette
    COMMAND test_palette "${CMAKE_CURRENT_SOURCE_DIR}/../palettes/rainbow.pal"
)

add_executable(test_contours unit/test_contours.cpp)
target_link_libraries(test_contours PRIVATE Amrvis::render2d Amrvis::warnings)
add_test(NAME contours COMMAND test_contours)

add_executable(test_vector_glyphs unit/test_vector_glyphs.cpp)
target_link_libraries(test_vector_glyphs PRIVATE Amrvis::render2d Amrvis::warnings)
add_test(NAME vector_glyphs COMMAND test_vector_glyphs)

add_executable(test_vismf_index unit/test_vismf_index.cpp)
target_link_libraries(test_vismf_index PRIVATE Amrvis::io Amrvis::warnings)
add_test(NAME vismf_index COMMAND test_vismf_index)

add_executable(test_plotfile_metadata integration/test_plotfile_metadata.cpp)
target_link_libraries(test_plotfile_metadata PRIVATE Amrvis::io Amrvis::warnings)
add_test(
    NAME plotfile_metadata_only
    COMMAND test_plotfile_metadata
        "${CMAKE_CURRENT_SOURCE_DIR}/data/plotfile_2d"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/plotfile_nonuniform"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/plotfile_3d"
)

add_executable(test_selective_block_read integration/test_selective_block_read.cpp)
target_link_libraries(test_selective_block_read PRIVATE Amrvis::io Amrvis::warnings)
add_test(NAME selective_block_read COMMAND test_selective_block_read)

add_executable(
    test_single_precision_plotfile
    integration/test_single_precision_plotfile.cpp
)
target_link_libraries(
    test_single_precision_plotfile
    PRIVATE Amrvis::query Amrvis::warnings
)
add_test(NAME single_precision_plotfile COMMAND test_single_precision_plotfile)

add_executable(test_slice_query integration/test_slice_query.cpp)
target_link_libraries(test_slice_query PRIVATE Amrvis::query Amrvis::warnings)
add_test(NAME slice_query COMMAND test_slice_query)

add_executable(test_line_query integration/test_line_query.cpp)
target_link_libraries(
    test_line_query
    PRIVATE
        Amrvis::query
        Amrvis::warnings
        Threads::Threads
)
add_test(
    NAME line_query
    COMMAND test_line_query
        "${CMAKE_CURRENT_SOURCE_DIR}/data/plotfile_2d"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/plotfile_3d"
)

# Writes FAB payloads into copies of the metadata-only fixtures; Qt-free so
# the headless build can compile it too.
add_executable(fixture_materializer fixture_materializer/main.cpp)
target_link_libraries(fixture_materializer PRIVATE Amrvis::warnings)

if(TARGET amrvis_qt)
    add_executable(test_user_guide_dialog
        unit/test_user_guide_dialog.cpp
        "${PROJECT_SOURCE_DIR}/src/qt/UserGuideDialog.cpp"
        "${PROJECT_SOURCE_DIR}/resources/user-guide.qrc"
    )
    set_target_properties(test_user_guide_dialog PROPERTIES AUTORCC ON)
    target_include_directories(test_user_guide_dialog
        PRIVATE "${PROJECT_SOURCE_DIR}/src/qt"
    )
    target_link_libraries(test_user_guide_dialog
        PRIVATE Amrvis::warnings Qt6::Widgets
    )
    add_test(
        NAME user_guide_dialog
        COMMAND "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen
            "$<TARGET_FILE:test_user_guide_dialog>"
    )

    add_executable(test_scientific_double_spin_box
        unit/test_scientific_double_spin_box.cpp
        "${PROJECT_SOURCE_DIR}/src/qt/NumberFormat.cpp"
        "${PROJECT_SOURCE_DIR}/src/qt/ScientificDoubleSpinBox.cpp"
    )
    target_include_directories(test_scientific_double_spin_box
        PRIVATE "${PROJECT_SOURCE_DIR}/src/qt"
    )
    target_link_libraries(test_scientific_double_spin_box
        PRIVATE Amrvis::warnings Qt6::Widgets
    )
    add_test(
        NAME scientific_double_spin_box
        COMMAND "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen
            "$<TARGET_FILE:test_scientific_double_spin_box>"
    )

    add_test(
        NAME qt_metadata_smoke
        COMMAND "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen
            "$<TARGET_FILE:amrvis_qt>" --smoke-test
            "${CMAKE_CURRENT_SOURCE_DIR}/data/plotfile_2d"
    )
    set_tests_properties(qt_metadata_smoke PROPERTIES TIMEOUT 10)
    add_test(
        NAME qt_metadata_smoke_3d
        COMMAND "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen
            "$<TARGET_FILE:amrvis_qt>" --smoke-test
            "${CMAKE_CURRENT_SOURCE_DIR}/data/plotfile_3d"
    )
    set_tests_properties(qt_metadata_smoke_3d PROPERTIES TIMEOUT 10)
    # The slice and sequence smoke paths need FAB payloads, so each test
    # materializes a fixture copy first (see qt_smoke_driver.cmake).
    add_test(
        NAME qt_slice_smoke_2d
        COMMAND "${CMAKE_COMMAND}"
            "-DMATERIALIZER=$<TARGET_FILE:fixture_materializer>"
            "-DAMRVIS_QT=$<TARGET_FILE:amrvis_qt>"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/data/plotfile_2d"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/fixtures_2d"
            -DMODE=slice
            -P "${CMAKE_CURRENT_SOURCE_DIR}/qt_smoke_driver.cmake"
    )
    set_tests_properties(qt_slice_smoke_2d PROPERTIES TIMEOUT 30)
    add_test(
        NAME qt_expression_editor_smoke
        COMMAND "${CMAKE_COMMAND}"
            "-DMATERIALIZER=$<TARGET_FILE:fixture_materializer>"
            "-DAMRVIS_QT=$<TARGET_FILE:amrvis_qt>"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/data/plotfile_2d"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/fixtures_expression_editor"
            -DMODE=expression-editor
            -P "${CMAKE_CURRENT_SOURCE_DIR}/qt_smoke_driver.cmake"
    )
    set_tests_properties(qt_expression_editor_smoke PROPERTIES TIMEOUT 30)
    add_test(
        NAME qt_slice_smoke_3d
        COMMAND "${CMAKE_COMMAND}"
            "-DMATERIALIZER=$<TARGET_FILE:fixture_materializer>"
            "-DAMRVIS_QT=$<TARGET_FILE:amrvis_qt>"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/data/plotfile_3d"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/fixtures_3d"
            -DMODE=slice
            -P "${CMAKE_CURRENT_SOURCE_DIR}/qt_smoke_driver.cmake"
    )
    set_tests_properties(qt_slice_smoke_3d PROPERTIES TIMEOUT 30)
    add_test(
        NAME qt_multifab_missing_range
        COMMAND "${CMAKE_COMMAND}"
            "-DMATERIALIZER=$<TARGET_FILE:fixture_materializer>"
            "-DAMRVIS_QT=$<TARGET_FILE:amrvis_qt>"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/data/plotfile_2d"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/fixtures_missing_range"
            -DMODE=missing-range
            -P "${CMAKE_CURRENT_SOURCE_DIR}/qt_smoke_driver.cmake"
    )
    set_tests_properties(qt_multifab_missing_range PROPERTIES TIMEOUT 30)
    add_test(
        NAME qt_sequence_smoke
        COMMAND "${CMAKE_COMMAND}"
            "-DMATERIALIZER=$<TARGET_FILE:fixture_materializer>"
            "-DAMRVIS_QT=$<TARGET_FILE:amrvis_qt>"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/data/plotfile_2d"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/fixtures_sequence"
            -DMODE=sequence
            -P "${CMAKE_CURRENT_SOURCE_DIR}/qt_smoke_driver.cmake"
    )
    set_tests_properties(qt_sequence_smoke PROPERTIES TIMEOUT 30)
endif()
