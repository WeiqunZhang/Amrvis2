add_executable(amrvis_qt
    AnimationPanel.cpp
    AnimationPanel.hpp
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
    SetContoursDialog.cpp
    SetContoursDialog.hpp
    Theme.hpp
    main.cpp
    ${CMAKE_SOURCE_DIR}/resources/amrvis2.qrc
)
set_target_properties(amrvis_qt PROPERTIES OUTPUT_NAME amrvis2 AUTOMOC ON AUTORCC ON)

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

install(TARGETS amrvis_qt RUNTIME DESTINATION bin)
