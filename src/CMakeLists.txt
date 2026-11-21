add_library(prism_core INTERFACE)
target_link_libraries(prism_core INTERFACE prism_options)
target_include_directories(prism_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT MSVC)
    target_compile_options(prism_core INTERFACE -ffp-contract=off)
endif()

add_library(prism_math INTERFACE)
target_link_libraries(prism_math INTERFACE prism_core)
