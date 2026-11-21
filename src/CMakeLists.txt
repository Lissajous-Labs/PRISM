add_library(prism_core INTERFACE)
target_link_libraries(prism_core INTERFACE prism_options)
target_include_directories(prism_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT MSVC)
    target_compile_options(prism_core INTERFACE -ffp-contract=off)
endif()

add_library(prism_math INTERFACE)
target_link_libraries(prism_math INTERFACE prism_core)

add_library(prism_hal STATIC
    hal/DamageRegion.cpp
    hal/DeviceRegistry.cpp
    hal/FrameGraph.cpp
)
target_link_libraries(prism_hal PUBLIC prism_math)

add_library(prism_render STATIC
    render/DrawList.cpp
    render/PrimitiveShading.cpp
)
target_link_libraries(prism_render PUBLIC prism_hal)
