# Patches to the ReXGlue SDK

Apply to ReXGlue v0.10.0 (`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`), from the SDK checkout:

```
git apply ../../patches/rexglue-v0.10.0-mcla.patch
git apply ../../patches/rexglue-minmax-blend-factor.patch
git -C thirdparty/FFmpeg apply ../../../../patches/rexglue-ffmpeg-ps5-config.patch
```

The first was several files at first; they are combined now because several touch the same build files and would not apply independently. The second goes on top of the first (both change `src/graphics/vulkan/command_processor.cpp`). The build scripts apply all three, and add the second to an SDK checkout that was patched before it existed.

## rexglue-v0.10.0-android-ui.patch

Android's windowing and presentation changes are kept separate from the PS5 patch above, so a PS5 build is unaffected. Apply it on top of `rexglue-v0.10.0-mcla.patch` in the Android SDK checkout:

```
git apply ../../patches/rexglue-v0.10.0-android-ui.patch
```

| Change | Files | Affects |
|---|---|---|
| `AndroidNativeWindowSurface` (holding an `ANativeWindow` reference) and its `kTypeIndex_AndroidNativeWindow`/`kTypeFlag_AndroidNativeWindow` type | `include/rex/ui/surface_android.h`, `src/ui/surface_android.cpp` | Android only |
| SDL `CreateSurfaceImpl` and `GetNativeWindowHandle` select the Android native window instead of falling through to X11 | `src/ui/window_sdl.cpp` | Android only |
| UI CMake builds `surface_android.cpp`, links `libandroid`, and keeps Android out of the X11/Wayland dependency branch | `src/ui/CMakeLists.txt` | Android only |
| Vulkan presenter and instance keep the existing `VK_KHR_android_surface` path while excluding the XCB/Wayland surface code (`REX_PLATFORM_LINUX` is also set on Android) | `src/ui/vulkan/vulkan_presenter.cpp`, `src/ui/vulkan/vulkan_instance.cpp`, `include/rex/ui/vulkan/instance.h`, `include/rex/ui/vulkan/api.h` | Android only |

Requires the NDK's Android platform headers; `scripts/patch_rexglue_android.py` applies the same change set to an SDK checkout from the build scripts.

## rexglue-v0.10.0-mcla.patch

| Change | Files | Affects |
|---|---|---|
| Vblank timer resync: the guest tick count can step backwards, which made an unsigned difference wrap and fire vblank interrupts without end | `src/graphics/graphics_system.cpp` | all platforms |
| Per-frame counters kept in Release builds, and their CSV path taken from `REX_PERF_LOG_CSV` (the GPU plugin has its own copy of the logger state, so a path set by the host never reached it) | `CMakeLists.txt`, `src/core/CMakeLists.txt`, `src/system/CMakeLists.txt`, `src/core/perf/counter.cpp` | all platforms |
| Debug-only register lookup removed from every GPU register write in Release builds | `src/graphics/command_processor.cpp` | all platforms |
| PS5 platform: everything else. Platform detection, libc and libc++ differences, anonymous shared memory with a shared-object address reservation at a kernel-chosen base, 16 KiB host pages, signals without realtime signals, no X11/Wayland/RenderDoc, static runtime and GPU libraries, no desktop install rules | the remaining files | PS5 only, behind `REX_PLATFORM_PS5` or `if(PS5)` |

Details of the PS5 changes are in `docs/ps5-port-plan.md`.

## rexglue-minmax-blend-factor.patch

The car reflection fix: cars came out white with a glow around them, worst at night. The Xbox 360's GPU applies the source blend factor in the MIN and MAX blend equations; Direct3D 12 and Vulkan ignore blend factors there. The game draws the light glows in the cars' environment map with `max(source x SrcAlpha, destination)`, so every glow became a solid square, and the paint mirrored those. With the patch the translated pixel shader multiplies by its own alpha in that case. Cause, evidence and limits: `REFLECTION_FIX.md`.

| Change | Files | Affects |
|---|---|---|
| System flags per render target, set when its colour or alpha blend is MIN or MAX with source factor SrcAlpha and destination factor One | `src/graphics/d3d12/command_processor.cpp`, `src/graphics/vulkan/command_processor.cpp` | D3D12; Vulkan (PC, PS5) |
| The multiply by the saturated alpha before the colour is written | `dxbc_translator.h`, `dxbc_translator_om.cpp`, `spirv_translator.h`, `spirv_translator_rb.cpp` (under `include/rex/graphics/pipeline/shader` and `src/graphics/pipeline/shader`) | D3D12; Vulkan (PC, PS5) |

Host render target path only (the default on both backends).

## rexglue-ffmpeg-ps5-config.patch

FFmpeg selects a configuration header per platform. This adds `config_ps5_x86_64.h`, generated from the Linux x86-64 one with `HAVE_MALLOC_H`, `CONFIG_ICONV`, `HAVE_MEMALIGN`, `HAVE_SCHED_GETAFFINITY` and `HAVE_LINUX_PERF_EVENT_H` turned off, and selects it for `__PROSPERO__`.
