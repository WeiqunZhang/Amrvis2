add_executable(amrvis_qt
    ColorBarWidget.cpp
    ColorBarWidget.hpp
    ImageView.cpp
    ImageView.hpp
    MainWindow.cpp
    MainWindow.hpp
    main.cpp
)
set_target_properties(amrvis_qt PROPERTIES AUTOMOC ON)

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
