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

    # ReXGlue v0.10.0's SDL file has a platform-specific include chain: Android
    # already has an ANativeWindow surface, while the X11 include is in the
    # final #else and must not be selected on Android. Guard that fallback.
    window_sdl = os.path.join(sdk_dir, "src", "ui", "window_sdl.cpp")
    if not os.path.exists(window_sdl):
        print(f"[ERROR] Required SDL window source is missing: {window_sdl}")
        return 1
    with open(window_sdl, 'r', encoding='utf-8') as f:
        window_content = f.read().replace('\\r\\n', '\\n')

    x11_include_fallback = '#else\n#include <X11/Xlib-xcb.h>'
    guarded_x11_include = ('#elif REX_PLATFORM_GNU_LINUX && !defined(__ANDROID__)\n'
                           '#include <X11/Xlib-xcb.h>')
    if x11_include_fallback in window_content:
        window_content = window_content.replace(x11_include_fallback, guarded_x11_include, 1)
        print('[OK] Added Android exclusion to fallback X11 SDL headers')
    elif guarded_x11_include in window_content:
        print('[SKIP] Fallback X11 SDL headers are already Android-guarded')
    else:
        print('[ERROR] Could not locate the v0.10.0 fallback X11 include')
        return 1
    # In CreateSurfaceImpl, the #else after macOS would select the XCB branch
    # on Android too. Turn it into an explicit GNU/Linux-only branch.
    surface_anchor = (
        '#elif REX_PLATFORM_MAC\n'
        '  if (allowed_types & Surface::kTypeFlag_CAMetalLayer)'
    )
    surface_start = window_content.find(surface_anchor)
    xcb_start = window_content.find(
        'if (allowed_types & Surface::kTypeFlag_XcbWindow)', surface_start
    )
    xcb_fallback = window_content.find('#else', surface_start, xcb_start)
    guarded_xcb = '#elif REX_PLATFORM_GNU_LINUX && !defined(__ANDROID__)'
    if surface_start >= 0 and xcb_start >= 0:
        if xcb_fallback >= 0:
            window_content = (window_content[:xcb_fallback] + guarded_xcb
                              + window_content[xcb_fallback + len('#else'):])
            print('[OK] Added Android exclusion to fallback XCB surface branch')
        elif guarded_xcb in window_content[surface_start:xcb_start]:
            print('[SKIP] Fallback XCB surface branch is already Android-guarded')
        else:
            print('[ERROR] Could not isolate the v0.10.0 XCB surface fallback')
            return 1
    else:
        print('[ERROR] Could not locate the v0.10.0 CreateSurfaceImpl XCB branch')
        return 1
    if x11_include_fallback in window_content:
        print('[ERROR] An unguarded X11 include remains in window_sdl.cpp')
        return 1
    if not re.search(
        r'#elif REX_PLATFORM_GNU_LINUX && !defined\(__ANDROID__\)\s+'
        r'SDL_PropertiesID props[\s\S]*?Surface::kTypeFlag_XcbWindow',
        window_content,
    ):
        print('[ERROR] XCB surface code is not guarded from Android')
        return 1
    with open(window_sdl, 'w', encoding='utf-8') as f:
        f.write(window_content)

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

    # 6. The pinned SDK may already contain a stub main_android.h. Always replace
    #    it with the Bionic API-level wrapper used by memory_posix.cpp.
    main_android_h = os.path.join(sdk_dir, "include", "rex", "main_android.h")
    platform_h = os.path.join(sdk_dir, "include", "rex", "platform.h")
    main_android_contents = '''#ifndef REX_MAIN_ANDROID_H_
#define REX_MAIN_ANDROID_H_
#ifdef __ANDROID__
#include <android/api-level.h>
namespace rex {
inline int GetAndroidApiLevel() { return android_get_device_api_level(); }
}  // namespace rex
#endif  // __ANDROID__
#endif  // REX_MAIN_ANDROID_H_
'''
    os.makedirs(os.path.dirname(main_android_h), exist_ok=True)
    with open(main_android_h, 'w', encoding='utf-8') as f:
        f.write(main_android_contents)
    print("[OK] Wrote Android API-level wrapper to include/rex/main_android.h")

    memory_posix = os.path.join(sdk_dir, "src", "core", "memory_posix.cpp")
    if not os.path.exists(memory_posix):
        print("[ERROR] Required Android source file is missing: memory_posix.cpp")
        return 1
    with open(memory_posix, 'r', encoding='utf-8') as f:
        memory_content = f.read().replace('\r\n', '\n')
    android_block = '#if REX_PLATFORM_ANDROID\n'
    helper_block = android_block + '#include <rex/main_android.h>'
    if helper_block not in memory_content:
        if android_block not in memory_content:
            print("[ERROR] Could not find Android section in memory_posix.cpp")
            return 1
        memory_content = memory_content.replace(android_block, helper_block + '\n', 1)
        with open(memory_posix, 'w', encoding='utf-8') as f:
            f.write(memory_content)
        print("[OK] Added main_android.h before Android API-level calls")

    # 7. Bionic (Android libc) has no robust mutexes: PTHREAD_MUTEX_ROBUST,
    #    pthread_mutexattr_setrobust() and pthread_mutex_consistent() do not
    #    exist there. The SDK guards those calls behind
    #    'REX_PLATFORM_LINUX && !REX_PLATFORM_ANDROID', so on Android the guard
    #    only works if platform.h actually defines REX_PLATFORM_ANDROID.
    platform_android_guard = '''#ifndef REX_PLATFORM_ANDROID
#define REX_PLATFORM_ANDROID 0
#endif
#if defined(__ANDROID__)
#undef REX_PLATFORM_ANDROID
#define REX_PLATFORM_ANDROID 1
#endif'''
    patch_file(
        platform_h,
        '#ifndef REX_PLATFORM_LINUX\n#define REX_PLATFORM_LINUX 0\n#endif',
        '#ifndef REX_PLATFORM_LINUX\n#define REX_PLATFORM_LINUX 0\n#endif\n' + platform_android_guard,
        'Define REX_PLATFORM_ANDROID in platform.h'
    )

    # 8. Bionic does not consistently expose robust-mutex APIs. Exclude Android
    #    from all Linux robust-mutex paths so no pthread_mutex_consistent symbol
    #    is referenced; Android uses the existing portable std::mutex paths.
    threading_posix = os.path.join(sdk_dir, "src", "core", "threading_posix.cpp")
    if not os.path.exists(threading_posix):
        print(f"[ERROR] File not found: {threading_posix}")
        return 1
    with open(threading_posix, 'r', encoding='utf-8') as f:
        threading_content = f.read().replace('\r\n', '\n')

    replacements = (
        (
            '#if REX_PLATFORM_LINUX\n    // Use robust mutexes so waits can recover if owner thread terminates.',
            '#if REX_PLATFORM_LINUX && !REX_PLATFORM_ANDROID\n    // Use robust mutexes so waits can recover if owner thread terminates.',
            'robust-mutex initialization',
        ),
        (
            '    auto predicate = [this] { return this->signaled(); };\n'
            '#if REX_PLATFORM_LINUX\n'
            '    auto native_mutex = static_cast<pthread_mutex_t*>(mutex_.native_handle());',
            '    auto predicate = [this] { return this->signaled(); };\n'
            '#if REX_PLATFORM_LINUX && !REX_PLATFORM_ANDROID\n'
            '    auto native_mutex = static_cast<pthread_mutex_t*>(mutex_.native_handle());',
            'Wait robust-mutex recovery',
        ),
        (
            '      for (size_t i = 0; i < handles.size(); ++i) {\n'
            '#if REX_PLATFORM_LINUX\n'
            '        auto native_mutex = static_cast<pthread_mutex_t*>(handles[i]->mutex_.native_handle());',
            '      for (size_t i = 0; i < handles.size(); ++i) {\n'
            '#if REX_PLATFORM_LINUX && !REX_PLATFORM_ANDROID\n'
            '        auto native_mutex = static_cast<pthread_mutex_t*>(handles[i]->mutex_.native_handle());',
            'WaitMultiple robust-mutex recovery',
        ),
    )
    for old_text, new_text, description in replacements:
        if new_text in threading_content:
            print(f"[SKIP] Already Android-safe: {description}")
        elif old_text in threading_content:
            threading_content = threading_content.replace(old_text, new_text, 1)
            print(f"[OK] Disabled Android robust-mutex path: {description}")
        else:
            print(f"[ERROR] Could not locate expected {description} block in threading_posix.cpp")
            return 1
    with open(threading_posix, 'w', encoding='utf-8') as f:
        f.write(threading_content)

    print("Android patching completed.")
    return 0
if __name__ == "__main__":
    sys.exit(main())
