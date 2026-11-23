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
    "${PROJECT_SOURCE_DIR}/shaders/metal/formant_curve.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/prism_shading.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/spot_coverage.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/halftone_field.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/spectrogram_fft.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/wavetable_3d.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/uber_primitive.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/present.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/vector_raster.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/post_effects.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/probes.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/telemetry_scope.metal"
    "${PROJECT_SOURCE_DIR}/shaders/metal/telemetry_spectrum.metal"
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
    render/BlueNoise.cpp
    render/HalftoneField.cpp
    render/FormantCurve.cpp
    render/Wavetable3d.cpp
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

add_library(prism_telemetry STATIC
    telemetry/LoudnessMeter.cpp
    telemetry/HalfBand.cpp
    telemetry/SpectrumEstimator.cpp
    telemetry/UmaTelemetry.cpp
)
target_link_libraries(prism_telemetry PUBLIC prism_render)

if(APPLE)
    add_library(prism_backend_metal STATIC
        backends/metal/MetalDevice.mm
        backends/metal/MetalEffects.mm
        backends/metal/MetalFormantCache.mm
        backends/metal/MetalHalftoneCache.mm
        backends/metal/MetalShadowCache.mm
        backends/metal/MetalPathEngine.mm
        backends/metal/MetalSpectrogram.mm
        backends/metal/MetalTelemetry.mm
        backends/metal/MetalWavetable3d.mm
        backends/metal/MetalSurface.mm
        backends/metal/MetalSurfaceState.mm
    )
    target_link_libraries(prism_backend_metal PUBLIC prism_render prism_telemetry "-framework Metal" "-framework Foundation" "-framework QuartzCore")
    target_compile_options(prism_backend_metal PRIVATE -fobjc-arc)
endif()

add_library(prism_text STATIC
    text/Bidi.cpp
    text/FontEngine.cpp
    text/FontFile.cpp
    text/FontVariations.cpp
    text/GlyphAtlas.cpp
    text/GlyphOutline.cpp
    text/GlyphRasterizer.cpp
    text/LineBreak.cpp
    text/MtsdfGenerator.cpp
    text/NativeFace.cpp
    text/SlugGlyph.cpp
    text/OpenTypeLayout.cpp
    text/TextShaper.cpp
    text/UnicodeClasses.cpp
    text/UtfFastPath.cpp
)
target_link_libraries(prism_text PUBLIC prism_render)

if(PRISM_NATIVE_TEXT)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PRISM_FREETYPE REQUIRED IMPORTED_TARGET freetype2)
    find_library(PRISM_FREETYPE_STATIC libfreetype.a HINTS ${PRISM_FREETYPE_LIBRARY_DIRS} REQUIRED)
    find_library(PRISM_PNG_STATIC libpng16.a HINTS /opt/homebrew/opt/libpng/lib REQUIRED)
    find_library(PRISM_BROTLIDEC_STATIC libbrotlidec.a HINTS /opt/homebrew/opt/brotli/lib REQUIRED)
    find_library(PRISM_BROTLICOMMON_STATIC libbrotlicommon.a HINTS /opt/homebrew/opt/brotli/lib
                 REQUIRED)
    if(NOT TARGET prism_harfbuzz)
        set(PRISM_HARFBUZZ_SOURCE_DIR
            "${PROJECT_SOURCE_DIR}/../JUCE/modules/juce_graphics/fonts/harfbuzz"
            CACHE PATH "HarfBuzz amalgamation directory")
        add_library(prism_harfbuzz STATIC "${PRISM_HARFBUZZ_SOURCE_DIR}/harfbuzz.cc")
        target_include_directories(prism_harfbuzz SYSTEM PUBLIC "${PRISM_HARFBUZZ_SOURCE_DIR}")
        target_compile_definitions(prism_harfbuzz PRIVATE HB_NO_PRAGMA_GCC_DIAGNOSTIC_WARNING
                                   HB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR)
        target_compile_options(prism_harfbuzz PRIVATE -w)
        target_compile_features(prism_harfbuzz PRIVATE cxx_std_17)
    endif()
    target_compile_definitions(prism_text PRIVATE PRISM_NATIVE_TEXT=1)
    target_include_directories(prism_text PRIVATE ${PRISM_FREETYPE_INCLUDE_DIRS})
    target_link_libraries(prism_text PRIVATE prism_harfbuzz ${PRISM_FREETYPE_STATIC}
                          ${PRISM_PNG_STATIC} ${PRISM_BROTLIDEC_STATIC}
                          ${PRISM_BROTLICOMMON_STATIC} z bz2)
endif()

add_library(prism_scene STATIC
    scene/SceneGraph.cpp
)
target_link_libraries(prism_scene PUBLIC prism_render)

add_library(prism_dom STATIC
    dom/DomBinary.cpp
    dom/DomDocument.cpp
    dom/DomJson.cpp
    dom/DomReconcile.cpp
    dom/DomXml.cpp
    dom/StyleCompiler.cpp
    dom/StyleResolver.cpp
)
target_link_libraries(prism_dom PUBLIC prism_math)

add_library(prism_layout STATIC
    dom/layout/AnchoredSolver.cpp
    dom/layout/FlexSolver.cpp
    dom/layout/GridSolver.cpp
    dom/layout/LayoutEngine.cpp
    dom/layout/PatchCanvasLayout.cpp
)
target_link_libraries(prism_layout PUBLIC prism_dom)

if(APPLE)
    add_library(prism_host STATIC
        host/AccessibilityBridge.cpp
        host/HostComponent.cpp
        host/PassiveSurfaceHost_mac.mm
        host/ProcessorState.cpp
    )
    target_link_libraries(prism_host PUBLIC prism_backend_metal prism_layout "-framework AppKit" "-framework QuartzCore" "-framework CoreVideo")
    target_compile_options(prism_host PRIVATE -fobjc-arc)
else()
    add_library(prism_host STATIC
        host/AccessibilityBridge.cpp
        host/HostComponent.cpp
        host/PassiveSurfaceHost_portable.cpp
        host/ProcessorState.cpp
    )
    target_link_libraries(prism_host PUBLIC prism_render prism_layout)
endif()

add_library(prism_widgets STATIC
    widgets/GpuFilterCurve.cpp
    widgets/GpuNoiseEnvelope.cpp
    widgets/VowelFormants.cpp
    widgets/GpuHalftoneSpectrogram.cpp
    widgets/GpuSpectralMatrix.cpp
    widgets/GpuWavetable3d.cpp
    widgets/GpuLoudnessMeter.cpp
    widgets/GpuScope.cpp
    widgets/GpuSpectrum.cpp
)
target_link_libraries(prism_widgets PUBLIC prism_render prism_telemetry)

add_library(prism_controls STATIC
    widgets/Button.cpp
    widgets/ComboBox.cpp
    widgets/NodeCard.cpp
    widgets/RotaryKnob.cpp
    widgets/Slider.cpp
    widgets/VerletCableLayer.cpp
    widgets/XYPad.cpp
)
target_link_libraries(prism_controls PUBLIC prism_widgets prism_host)

if(PRISM_ENABLE_STUDIO)
    add_library(prism_studio STATIC
        studio/GpuProfilerHud.cpp
        studio/HotReloader.cpp
        studio/ProvenanceInspector.cpp
        studio/StudioOverlay.cpp
        studio/StudioSession.cpp
    )
    target_link_libraries(prism_studio PUBLIC prism_layout prism_render)
endif()
