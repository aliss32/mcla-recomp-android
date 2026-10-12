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

rexglue_setup_target(mcla)
