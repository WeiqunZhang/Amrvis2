add_executable(query_benchmark main.cpp)
target_link_libraries(query_benchmark PRIVATE Amrvis::io Amrvis::warnings)
target_compile_features(query_benchmark PRIVATE cxx_std_20)

