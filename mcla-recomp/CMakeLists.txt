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
    src/native/heartbeat.cpp
    src/native/render_target_manager.cpp
    src/native/present_pass.cpp
    src/native/frame_dump.cpp
    # SDK graphics sources the census subclass links against. The base
    # classes live only in the rexgpu-xenos target's source list, and this
    # plugin must not link that plugin (duplicate plugin_main), so the
    # needed translation units are compiled into this target instead.
    # Same list the undefined symbols demanded; add more only when the
    # linker names them.
    ${REXSDK_DIR}/src/graphics/command_processor.cpp
    ${REXSDK_DIR}/src/graphics/graphics_system.cpp
    ${REXSDK_DIR}/src/graphics/registers.cpp
    ${REXSDK_DIR}/src/graphics/register_file.cpp
    ${REXSDK_DIR}/src/graphics/packet_disassembler.cpp
    ${REXSDK_DIR}/src/graphics/util/draw.cpp
    ${REXSDK_DIR}/src/graphics/util/draw_extent_estimator.cpp
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
)
# xxhash.h comes from the vendored xxHash submodule through the xxHash::xxhash
# target (the same dependency the Xenos plugin links).
target_link_libraries(rexgpu-mcla-native PRIVATE rex::runtime xxHash::xxhash)
# Heartbeat sidecar + logcat line (stage 0.3). The 'log' lib is Android's
# liblog for __android_log_print.
if(ANDROID)
    target_link_libraries(rexgpu-mcla-native PRIVATE log)
endif()
# glslang for the present pass GLSL->SPIR-V (same link set rexgpu-xenos gets,
# src/graphics/CMakeLists.txt:146-157). A missing target fails the configure
# step - no silent fallback.
if(REXGLUE_USE_VULKAN)
    target_link_libraries(rexgpu-mcla-native PRIVATE
        glslang::SPIRV
        glslang
        MachineIndependent
        GenericCodeGen
        OSDependent
        OGLCompiler
    )
endif()
# Show every missing symbol in one link, not one per retry (lld only).
if(ANDROID)
    target_link_options(rexgpu-mcla-native PRIVATE "-Wl,--error-limit=0")
endif()
if(TARGET Vulkan::Headers)
    target_link_libraries(rexgpu-mcla-native PRIVATE Vulkan::Headers)
endif()
if(COMMAND rexglue_apply_target_settings)
    rexglue_apply_target_settings(rexgpu-mcla-native)
endif()

# fh2-recompiled native renderer (stage 2): full PM4 ring renderer, guarded by
# MCLA_RECOMP. Sources in src/nativo; selected with --gpu_plugin=mcla-fhnative.
file(GLOB REXGPU_MCLA_FHNATIVE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/nativo/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/fhnative/plugin_main.cpp)
list(APPEND REXGPU_MCLA_FHNATIVE_SOURCES
    src/native/graphics_system.cpp
    src/native/command_processor.cpp
    src/native/heartbeat.cpp
    src/native/render_target_manager.cpp
    src/native/present_pass.cpp
    src/native/frame_dump.cpp
    ${REXSDK_DIR}/src/graphics/command_processor.cpp
    ${REXSDK_DIR}/src/graphics/graphics_system.cpp
    ${REXSDK_DIR}/src/graphics/registers.cpp
    ${REXSDK_DIR}/src/graphics/register_file.cpp
    ${REXSDK_DIR}/src/graphics/packet_disassembler.cpp
    ${REXSDK_DIR}/src/graphics/util/draw.cpp
    ${REXSDK_DIR}/src/graphics/util/draw_extent_estimator.cpp
)
add_library(rexgpu-mcla-fhnative SHARED ${REXGPU_MCLA_FHNATIVE_SOURCES})
add_library(rex::gpu-mcla-fhnative ALIAS rexgpu-mcla-fhnative)
set_target_properties(rexgpu-mcla-fhnative PROPERTIES
    OUTPUT_NAME "rexgpu-mcla-fhnative"
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_compile_definitions(rexgpu-mcla-fhnative PRIVATE MCLA_RECOMP=1)
target_include_directories(rexgpu-mcla-fhnative PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${REXSDK_DIR}
    ${REXSDK_DIR}/thirdparty/renderdoc
)
target_link_libraries(rexgpu-mcla-fhnative PRIVATE rex::runtime xxHash::xxhash)
if(ANDROID)
    target_link_libraries(rexgpu-mcla-fhnative PRIVATE log)
endif()
if(REXGLUE_USE_VULKAN)
    target_link_libraries(rexgpu-mcla-fhnative PRIVATE
        glslang::SPIRV
        glslang
        MachineIndependent
    )
endif()
if(TARGET Vulkan::Headers)
    target_link_libraries(rexgpu-mcla-fhnative PRIVATE Vulkan::Headers)
endif()

# Stage 0 (dump) plugin: same census plumbing + container dumper. Selected with
# --gpu_plugin=mcla-dump. Shares the census sources; adds the dumper.
set(REXGPU_MCLA_DUMP_SOURCES
    src/dump/dump_main.cpp
    src/dump/dump_graphics_system.cpp
    src/dump/container_dumper.cpp
    src/native/graphics_system.cpp
    src/native/command_processor.cpp
    src/native/frame_census.cpp
    src/native/heartbeat.cpp
    src/native/render_target_manager.cpp
    src/native/present_pass.cpp
    src/native/frame_dump.cpp
    ${REXSDK_DIR}/src/graphics/command_processor.cpp
    ${REXSDK_DIR}/src/graphics/graphics_system.cpp
    ${REXSDK_DIR}/src/graphics/registers.cpp
    ${REXSDK_DIR}/src/graphics/register_file.cpp
    ${REXSDK_DIR}/src/graphics/packet_disassembler.cpp
    ${REXSDK_DIR}/src/graphics/util/draw.cpp
    ${REXSDK_DIR}/src/graphics/util/draw_extent_estimator.cpp
)
add_library(rexgpu-mcla-dump SHARED ${REXGPU_MCLA_DUMP_SOURCES})
add_library(rex::gpu-mcla-dump ALIAS rexgpu-mcla-dump)
set_target_properties(rexgpu-mcla-dump PROPERTIES
    OUTPUT_NAME "rexgpu-mcla-dump"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
target_include_directories(rexgpu-mcla-dump PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${REXSDK_DIR}
    ${REXSDK_DIR}/thirdparty/renderdoc
)
target_link_libraries(rexgpu-mcla-dump PRIVATE rex::runtime xxHash::xxhash)
if(ANDROID)
    target_link_libraries(rexgpu-mcla-dump PRIVATE log)
    target_link_options(rexgpu-mcla-dump PRIVATE "-Wl,--error-limit=0")
endif()
if(REXGLUE_USE_VULKAN)
    target_link_libraries(rexgpu-mcla-dump PRIVATE
        glslang::SPIRV
        glslang
        MachineIndependent
        GenericCodeGen
        OSDependent
        OGLCompiler
    )
endif()
if(TARGET Vulkan::Headers)
    target_link_libraries(rexgpu-mcla-dump PRIVATE Vulkan::Headers)
endif()
if(COMMAND rexglue_apply_target_settings)
    rexglue_apply_target_settings(rexgpu-mcla-dump)
endif()

rexglue_setup_target(mcla GPU_PLUGINS xenos)
