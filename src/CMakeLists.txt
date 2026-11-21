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

set(PRISM_METAL_SHADERS
    "${PROJECT_SOURCE_DIR}/shaders/metal/prism_common.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/prism_sdf.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/prism_shading.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/uber_primitive.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/present.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/vector_raster.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/post_effects.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/probes.metal"
)
string(REPLACE ";" "|" PRISM_SHADER_ARGUMENT "${PRISM_METAL_SHADERS}")
set(PRISM_SHADER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/ShaderSources.cpp")
add_custom_command(OUTPUT "${PRISM_SHADER_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" "-DSHADER_SOURCES=${PRISM_SHADER_ARGUMENT}" "-DOUTPUT=${PRISM_SHADER_SOURCE}"
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    DEPENDS ${PRISM_METAL_SHADERS} "${PROJECT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    VERBATIM
)

add_library(prism_render STATIC
    render/DrawList.cpp
    render/GradientTable.cpp
    render/PrimitiveShading.cpp
    render/UberBatches.cpp
    render/effects/BackdropBlur.cpp
    render/effects/Bloom.cpp
    render/effects/DualKawase.cpp
    render/effects/GaussianBlur.cpp
    render/vector/FineRaster.cpp
    render/vector/PathTiler.cpp
    render/vector/StrokeExpander.cpp
    render/vector/VectorPath.cpp
    "${PRISM_SHADER_SOURCE}"
)
target_link_libraries(prism_render PUBLIC prism_hal)

add_library(prism_backend_cpu STATIC
    backends/cpu/CpuRaster.cpp
    backends/cpu/CpuRefBackend.cpp
)
target_link_libraries(prism_backend_cpu PUBLIC prism_render)

if(APPLE)
    add_library(prism_backend_metal STATIC
        backends/metal/MetalDevice.mm
        backends/metal/MetalEffects.mm
        backends/metal/MetalPathEngine.mm
        backends/metal/MetalSurface.mm
        backends/metal/MetalSurfaceState.mm
    )
    target_link_libraries(prism_backend_metal PUBLIC prism_render "-framework Metal" "-framework Foundation")
    target_compile_options(prism_backend_metal PRIVATE -fobjc-arc)
endif()

add_library(prism_text STATIC
    text/Bidi.cpp
    text/FontFile.cpp
    text/GlyphOutline.cpp
    text/LineBreak.cpp
    text/OpenTypeLayout.cpp
    text/TextShaper.cpp
    text/UnicodeClasses.cpp
    text/UtfFastPath.cpp
)
target_link_libraries(prism_text PUBLIC prism_render)
