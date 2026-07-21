add_executable(line_repro main.cpp)
target_link_libraries(line_repro PRIVATE Amrvis::query Amrvis::warnings)
target_compile_features(line_repro PRIVATE cxx_std_20)
