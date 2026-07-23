add_executable(amrvis_qt
    AnimationPanel.cpp
    AnimationPanel.hpp
    CacheConfig.hpp
    ColorBarWidget.cpp
    ColorBarWidget.hpp
    DatasetExtract.hpp
    DatasetWindow.cpp
    DatasetWindow.hpp
    ImageView.cpp
    ImageView.hpp
    IsoWidget.cpp
    IsoWidget.hpp
    LinePlotRequest.hpp
    LinePlotWindow.cpp
    LinePlotWindow.hpp
    MainWindow.cpp
    MainWindow.hpp
    NumberFormat.cpp
    NumberFormat.hpp
    ScientificDoubleSpinBox.cpp
    ScientificDoubleSpinBox.hpp
    SetContoursDialog.cpp
    SetContoursDialog.hpp
    Theme.hpp
    UserGuideDialog.cpp
    UserGuideDialog.hpp
    main.cpp
    ${CMAKE_SOURCE_DIR}/resources/amrvis2.qrc
    ${CMAKE_SOURCE_DIR}/resources/user-guide.qrc
)
set_target_properties(amrvis_qt PROPERTIES OUTPUT_NAME amrvis2 AUTOMOC ON AUTORCC ON)

if(APPLE AND AMRVIS_BUILD_MACOS_APP_BUNDLE)
    set(amrvis_macos_icon "${CMAKE_SOURCE_DIR}/resources/amrvis2.icns")
    set_source_files_properties("${amrvis_macos_icon}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    target_sources(amrvis_qt PRIVATE "${amrvis_macos_icon}")
    set_target_properties(amrvis_qt PROPERTIES
        OUTPUT_NAME Amrvis2
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME Amrvis2
        MACOSX_BUNDLE_GUI_IDENTIFIER org.amrex-codes.Amrvis2
        MACOSX_BUNDLE_ICON_FILE amrvis2.icns
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

target_link_libraries(amrvis_qt
    PRIVATE
        Amrvis::core
        Amrvis::io
        Amrvis::query
        Amrvis::render2d
        Amrvis::warnings
        Qt6::Concurrent
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)
target_compile_features(amrvis_qt PRIVATE cxx_std_20)
target_compile_definitions(amrvis_qt PRIVATE AMRVIS_VERSION="${PROJECT_VERSION}")

install(TARGETS amrvis_qt
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
