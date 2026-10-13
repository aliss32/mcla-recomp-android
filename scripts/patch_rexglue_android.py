#!/usr/bin/env python3
"""
Applies Android compatibility patches to rexglue-sdk after the upstream
PS5/MCLA patches have been applied.
"""
import os
import re
import sys

def patch_file(filepath, old_text, new_text, description):
    if not os.path.exists(filepath):
        print(f"[WARN] File not found: {filepath}")
        return False
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read().replace('\r\n', '\n')
    if new_text in content:
        print(f"[SKIP] Already patched {os.path.basename(filepath)}: {description}")
        return True
    elif old_text in content:
        content = content.replace(old_text, new_text, 1)
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"[OK] Patched {os.path.basename(filepath)}: {description}")
        return True
    else:
        print(f"[WARN] Target text not found in {os.path.basename(filepath)}: {description}")
        return False

def patch_file_regex(filepath, pattern, replacement, description):
    if not os.path.exists(filepath):
        print(f"[WARN] File not found: {filepath}")
        return False
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read().replace('\r\n', '\n')
    if re.search(pattern, content):
        content = re.sub(pattern, replacement, content, count=1)
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"[OK] Patched (regex) {os.path.basename(filepath)}: {description}")
        return True
    else:
        print(f"[WARN] Regex pattern not found in {os.path.basename(filepath)}: {description}")
        return False

def main():
    sdk_dir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/rexglue-sdk"
    print(f"Applying Android patches to ReXGlue SDK at: {sdk_dir}")

    # 1. UI CMakeLists: Disable X11 / Wayland on Android
    ui_cmake = os.path.join(sdk_dir, "src", "ui", "CMakeLists.txt")
    patch_file(
        ui_cmake,
        'if(UNIX AND NOT APPLE AND NOT PS5)',
        'if(UNIX AND NOT APPLE AND NOT PS5 AND NOT ANDROID)',
        'Disable X11/Wayland dependencies on Android'
    )
    patch_file(
        ui_cmake,
        'pkg_check_modules(X11_XCB REQUIRED x11-xcb)',
        '# pkg_check_modules(X11_XCB REQUIRED x11-xcb) - skipped on Android',
        'Comment out X11-XCB pkg-config check'
    )
    patch_file(
        ui_cmake,
        'elseif(PS5)\n    # No window system surface; presentation is VK_KHR_display.\n    set(REXUI_PLATFORM_SOURCES)',
        'elseif(PS5)\n    # No window system surface; presentation is VK_KHR_display.\n    set(REXUI_PLATFORM_SOURCES)\nelseif(ANDROID)\n    set(REXUI_PLATFORM_SOURCES)',
        'Set empty REXUI_PLATFORM_SOURCES for Android'
    )
    patch_file(
        ui_cmake,
        'elseif(APPLE)\n    target_link_libraries(rexui PRIVATE "-framework CoreFoundation")',
        'elseif(APPLE)\n    target_link_libraries(rexui PRIVATE "-framework CoreFoundation")\nelseif(ANDROID)\n    target_link_libraries(rexui PRIVATE android)',
        'Link libandroid.so to rexui'
    )

    # 2. Chrono: Polyfill clock_cast on Android libc++
    chrono_h = os.path.join(sdk_dir, "include", "rex", "chrono", "chrono.h")
    patch_file(
        chrono_h,
        '#if defined(__APPLE__) || defined(__PROSPERO__) || defined(__FreeBSD__)',
        '#if defined(__APPLE__) || defined(__PROSPERO__) || defined(__FreeBSD__) || defined(__ANDROID__)',
        'Polyfill clock_cast for Android NDK libc++'
    )

    # 3. Core CMakeLists: Link dl log instead of pthread rt dl on Android
    core_cmake = os.path.join(sdk_dir, "src", "core", "CMakeLists.txt")
    patch_file(
        core_cmake,
        'elseif(PS5)\n    # No librt or libdl: threads and timers come from the SDK\'s libc/libkernel.\nelseif(UNIX)',
        'elseif(PS5)\n    # No librt or libdl: threads and timers come from the SDK\'s libc/libkernel.\nelseif(ANDROID)\n    target_link_libraries(rexcore PRIVATE dl log)\nelseif(UNIX)',
        'Link dl log to rexcore on Android (no librt.so)'
    )

    # 4. Numeric: Add Android to portable_float_from_chars preprocessor guard
    #    Android NDK libc++ lacks float std::from_chars. The SDK already has a
    #    portable_float_from_chars fallback used for Mac/PS5; we just add Android
    #    to that same preprocessor condition (4 occurrences in numeric.h).
    numeric_h = os.path.join(sdk_dir, "include", "rex", "string", "numeric.h")
    if os.path.exists(numeric_h):
        with open(numeric_h, 'r', encoding='utf-8') as f:
            nc = f.read().replace('\r\n', '\n')
        old_guard = '#if REX_PLATFORM_MAC || REX_PLATFORM_PS5'
        new_guard = '#if REX_PLATFORM_MAC || REX_PLATFORM_PS5 || defined(__ANDROID__)'
        if new_guard in nc:
            print(f"[SKIP] Already patched numeric.h: portable_float_from_chars guard")
        elif old_guard in nc:
            nc = nc.replace(old_guard, new_guard)  # replaces ALL occurrences
            with open(numeric_h, 'w', encoding='utf-8') as f:
                f.write(nc)
            print(f"[OK] Patched numeric.h: Add Android to portable_float_from_chars guard (all occurrences)")
        else:
            print(f"[WARN] Guard not found in numeric.h")

    # 5. Fiber: Android lacks ucontext.h functions (getcontext/makecontext/swapcontext).
    #    Provide thread-fiber support for Android (same approach as PS5).
    fiber_h = os.path.join(sdk_dir, "include", "rex", "thread", "fiber.h")
    patch_file(
        fiber_h,
        '#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC\n#if REX_PLATFORM_MAC && !defined(_XOPEN_SOURCE)',
        '#if (REX_PLATFORM_LINUX || REX_PLATFORM_MAC) && !defined(__ANDROID__)\n#if REX_PLATFORM_MAC && !defined(_XOPEN_SOURCE)',
        'Skip ucontext.h include on Android'
    )
    patch_file(
        fiber_h,
        '#elif REX_PLATFORM_LINUX || REX_PLATFORM_MAC\n  ucontext_t context_{};',
        '#elif (REX_PLATFORM_LINUX || REX_PLATFORM_MAC) && !defined(__ANDROID__)\n  ucontext_t context_{};',
        'Skip ucontext_t members on Android'
    )
    patch_file(
        fiber_h,
        '#elif REX_PLATFORM_PS5\n  // Thread fibers only; see fiber_ps5.cpp.\n  bool is_thread_fiber_ = false;\n#endif',
        '#elif REX_PLATFORM_PS5 || defined(__ANDROID__)\n  // Thread fibers only; see fiber_ps5.cpp / fiber_posix.cpp.\n  bool is_thread_fiber_ = false;\n#endif',
        'Use thread-fiber member on Android'
    )

    fiber_posix = os.path.join(sdk_dir, "src", "core", "fiber_posix.cpp")
    fiber_android_impl = '''#include <rex/platform.h>
#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC

#if defined(__ANDROID__)

#include <rex/thread/fiber.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace rex::thread {

thread_local Fiber* Fiber::tls_current_ = nullptr;

Fiber* Fiber::ConvertCurrentThread() {
  auto* f = new Fiber();
  f->is_thread_fiber_ = true;
  tls_current_ = f;
  return f;
}

Fiber* Fiber::Create(size_t stack_size, void (*entry)(void*), void* arg) {
  (void)stack_size;
  (void)entry;
  (void)arg;
  std::fputs("rex::thread::Fiber::Create is not implemented on Android\\n", stderr);
  return nullptr;
}

void Fiber::SwitchTo(Fiber* target) {
  (void)target;
  std::fputs("rex::thread::Fiber::SwitchTo is not implemented on Android\\n", stderr);
  std::abort();
}

void Fiber::Destroy() {
  if (is_thread_fiber_) {
    tls_current_ = nullptr;
  } else {
    assert(this != tls_current_ && "Destroy called on the currently running fiber");
  }
  delete this;
}

}  // namespace rex::thread

#else

#include <rex/thread/fiber.h>'''

    patch_file(
        fiber_posix,
        '#include <rex/platform.h>\n#if REX_PLATFORM_LINUX || REX_PLATFORM_MAC\n\n#include <rex/thread/fiber.h>',
        fiber_android_impl,
        'Add Android thread-fiber implementation in fiber_posix.cpp'
    )
    patch_file(
        fiber_posix,
        '}  // namespace rex::thread\n\n#endif  // REX_PLATFORM_LINUX || REX_PLATFORM_MAC',
        '}  // namespace rex::thread\n\n#endif  // !defined(__ANDROID__)\n\n#endif  // REX_PLATFORM_LINUX || REX_PLATFORM_MAC',
        'Close Android #if in fiber_posix.cpp'
    )

    # 6. Android helpers: supply the header required by the Android POSIX
    #    threading backend and expose the runtime API-level query.
    main_android_h = os.path.join(sdk_dir, "include", "rex", "main_android.h")
    main_android_contents = '''#ifndef REX_MAIN_ANDROID_H_
#define REX_MAIN_ANDROID_H_
#include <cstdint>
#if defined(__ANDROID__)
#include <android/api-level.h>
#endif
namespace rex {
inline int32_t GetAndroidApiLevel() {
#if defined(__ANDROID__)
  return android_get_device_api_level();
#else
  return 0;
#endif
}
}  // namespace rex
#endif  // REX_MAIN_ANDROID_H_
'''
    if os.path.exists(main_android_h):
        print("[SKIP] Android helper header already exists: include/rex/main_android.h")
    else:
        os.makedirs(os.path.dirname(main_android_h), exist_ok=True)
        with open(main_android_h, 'w', encoding='utf-8') as f:
            f.write(main_android_contents)
    print("Android patching completed.")

if __name__ == "__main__":
    main()
