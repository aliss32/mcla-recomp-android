#!/usr/bin/env python3
"""
Applies Android compatibility patches to rexglue-sdk after the upstream
PS5/MCLA patches have been applied.
"""
import os
import sys

def patch_file(filepath, old_text, new_text, description):
    if not os.path.exists(filepath):
        print(f"[WARN] File not found: {filepath}")
        return False
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    if old_text in content:
        content = content.replace(old_text, new_text, 1)
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"[OK] Patched {os.path.basename(filepath)}: {description}")
        return True
    elif new_text in content:
        print(f"[SKIP] Already patched {os.path.basename(filepath)}: {description}")
        return True
    else:
        print(f"[WARN] Target text not found in {os.path.basename(filepath)}: {description}")
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
        'elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")',
        'elseif(ANDROID)\n    target_link_libraries(rexui PRIVATE android)\nelseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")',
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

    print("Android patching completed.")

if __name__ == "__main__":
    main()
