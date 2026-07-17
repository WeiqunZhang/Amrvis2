add_library(amrvis_warnings INTERFACE)
add_library(Amrvis::warnings ALIAS amrvis_warnings)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(amrvis_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
    )
    if(AMRVIS_WARNINGS_AS_ERRORS)
        target_compile_options(amrvis_warnings INTERFACE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(amrvis_warnings INTERFACE /W4)
    if(AMRVIS_WARNINGS_AS_ERRORS)
        target_compile_options(amrvis_warnings INTERFACE /WX)
    endif()
endif()

