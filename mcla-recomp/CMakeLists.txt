# mcla - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(mcla LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(MCLA_SOURCES
    src/main.cpp
    src/mcla_app.cpp
    src/crash_trace.cpp
    src/audio_fallback.cpp
    src/frame_timing.cpp
    src/render_perf.cpp
    src/perf_options.cpp
    src/button_prompts.cpp
    src/settings_menu.cpp
    src/sampling_profiler.cpp
    src/code_pointer_scan.cpp
)

if(ANDROID)
    add_library(mcla SHARED ${MCLA_SOURCES})
    set_target_properties(mcla PROPERTIES OUTPUT_NAME "main")
    target_link_libraries(mcla PRIVATE SDL3::SDL3 android log z)
    target_link_options(mcla PRIVATE
        "-Wl,-z,max-page-size=16384"
        "-Wl,--export-dynamic-symbol=SDL_main"
        "-Wl,--export-dynamic-symbol=JNI_OnLoad"
        "-Wl,-Bsymbolic-functions"
    )
elseif(WIN32)
    add_executable(mcla WIN32 ${MCLA_SOURCES})
    target_link_libraries(mcla PRIVATE winmm)
else()
    add_executable(mcla ${MCLA_SOURCES})
endif()

# Native renderer stage 1 (census) GPU plugin, built next to rexgpu-xenos.
# Selected with --gpu_plugin=mcla-native. See docs/native-renderer/.
if(REXSDK_DIR)
    set(REXGLUE_ROOT "${REXSDK_DIR}")
endif()
set(REXGPU_MCLA_NATIVE_SOURCES
    src/native/plugin_main.cpp
    src/native/graphics_system.cpp
    src/native/command_processor.cpp
    src/native/frame_census.cpp
    # SDK graphics sources the census subclass links against. The base
    # classes live only in the rexgpu-xenos target's source list, and this
    # plugin must not link that plugin (duplicate plugin_main), so the
    # needed translation units are compiled into this target instead.
    # Same list the undefined symbols demanded; add more only when the
    # linker names them.
    # Stage 2.1 derives from VulkanCommandProcessor, so the full Vulkan
    # backend source set (and the SPIR-V translator it pulls in) is
    # compiled here too - the same files rexgpu-xenos compiles
    # (src/graphics/CMakeLists.txt:7-55).
    ${REXSDK_DIR}/src/graphics/command_processor.cpp
    ${REXSDK_DIR}/src/graphics/graphics_system.cpp
    ${REXSDK_DIR}/src/graphics/registers.cpp
    ${REXSDK_DIR}/src/graphics/register_file.cpp
    ${REXSDK_DIR}/src/graphics/packet_disassembler.cpp
    ${REXSDK_DIR}/src/graphics/util/draw.cpp
    ${REXSDK_DIR}/src/graphics/util/draw_extent_estimator.cpp
    ${REXSDK_DIR}/src/graphics/pipeline/shader/shader.cpp
    ${REXSDK_DIR}/src/graphics/pipeline/shader/translator.cpp
    ${REXSDK_DIR}/src/graphics/pipeline/shader/spirv_builder.cpp
    ${REXSDK_DIR}/src/graphics/pipeline/shader/spirv.cpp
    ${REXSDK_DIR}/src/graphics/pipeline/shader/spirv_translator.cpp
    ${REXSDK_DIR}/src/graphics/pipeline/shader/spirv_translator_alu.cpp
    ${REXSDK_DIR}/src/graphics/pipeline/shader/spirv_translator_fetch.cpp
    ${REXSDK_DIR}/src/graphics/pipeline/shader/spirv_translator_memexport.cpp
    ${REXSDK_DIR}/src/graphics/pipeline/shader/spirv_translator_rb.cpp
    ${REXSDK_DIR}/src/graphics/vulkan/graphics_system.cpp
    ${REXSDK_DIR}/src/graphics/vulkan/deferred_command_buffer.cpp
    ${REXSDK_DIR}/src/graphics/vulkan/command_processor.cpp
    ${REXSDK_DIR}/src/graphics/vulkan/pipeline_cache.cpp
    ${REXSDK_DIR}/src/graphics/vulkan/primitive_processor.cpp
    ${REXSDK_DIR}/src/graphics/vulkan/render_target_cache.cpp
    ${REXSDK_DIR}/src/graphics/vulkan/shader.cpp
    ${REXSDK_DIR}/src/graphics/vulkan/shared_memory.cpp
    ${REXSDK_DIR}/src/graphics/vulkan/texture_cache.cpp
)
add_library(rexgpu-mcla-native SHARED ${REXGPU_MCLA_NATIVE_SOURCES})
add_library(rex::gpu-mcla-native ALIAS rexgpu-mcla-native)
set_target_properties(rexgpu-mcla-native PROPERTIES
    OUTPUT_NAME "rexgpu-mcla-native"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
target_include_directories(rexgpu-mcla-native PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${REXSDK_DIR}
    # renderdoc_app.h: included by the SDK's Vulkan presenter headers.
    ${REXSDK_DIR}/thirdparty/renderdoc
    ${REXSDK_DIR}/thirdparty/vulkan-headers/include
)
# xxhash.h comes from the vendored xxHash submodule through the xxHash::xxhash
# target (the same dependency the Xenos plugin links).
target_link_libraries(rexgpu-mcla-native PRIVATE rex::runtime xxHash::xxhash)
if(TARGET Vulkan::Headers)
    target_link_libraries(rexgpu-mcla-native PRIVATE Vulkan::Headers)
endif()
# GLSL->SPIR-V for the host helper shaders the Vulkan backend compiles
# (the same link set rexgpu-xenos gets in the SDK's CMakeLists.txt:141-157).
foreach(_glslang_lib glslang::SPIRV GPUOpen::VulkanMemoryAllocator glslang
                     MachineIndependent GenericCodeGen OSDependent OGLCompiler)
    if(TARGET ${_glslang_lib})
        target_link_libraries(rexgpu-mcla-native PRIVATE ${_glslang_lib})
    endif()
endforeach()
# Show every missing symbol in one link, not one per retry (lld only).
if(ANDROID)
    target_link_options(rexgpu-mcla-native PRIVATE "-Wl,--error-limit=0")
endif()
if(COMMAND rexglue_apply_target_settings)
    rexglue_apply_target_settings(rexgpu-mcla-native)
endif()

rexglue_setup_target(mcla GPU_PLUGINS xenos)
