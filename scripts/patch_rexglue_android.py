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

    # 4. Numeric: Replace std::from_chars for float/double with strtof/strtod on Android (NDK libc++ lacks float from_chars)
    numeric_h = os.path.join(sdk_dir, "include", "rex", "string", "numeric.h")
    patch_file(
        numeric_h,
        '#include <charconv>',
        '#include <charconv>\n#include <cstdlib>',
        'Include cstdlib for strtof/strtod in numeric.h'
    )

    # Patch detail::fpfs float/double parser (entire else block including error check)
    pattern_fpfs = (
        r'(\s+)auto \[p, error\] = std::from_chars\(range\.data\(\), range\.data\(\) \+ range\.size\(\), result,\n'
        r'\s+std::chars_format::general\);\n'
        r'\s+//[^\n]*\n'
        r'\s+if \(error != std::errc\(\)\) \{\n'
        r'\s+assert_always\(\);\n'
        r'\s+return T\(\);\n'
        r'\s+\}\n'
        r'\s+if \(is_negative\) \{\n'
        r'\s+result = -result;\n'
        r'\s+\}'
    )
    replacement_fpfs = (
        r'''\1#if defined(__ANDROID__)
\1char* end_ptr = nullptr;
\1std::string s_val(range);
\1if constexpr (std::is_same_v<T, float>) {
\1  result = std::strtof(s_val.c_str(), &end_ptr);
\1} else {
\1  result = std::strtod(s_val.c_str(), &end_ptr);
\1}
\1if (end_ptr == s_val.c_str()) {
\1  return T();
\1}
\1if (is_negative) {
\1  result = -result;
\1}
#else
\1auto [p, error] = std::from_chars(range.data(), range.data() + range.size(), result,
\1                                  std::chars_format::general);
\1if (error != std::errc()) {
\1  assert_always();
\1  return T();
\1}
\1if (is_negative) {
\1  result = -result;
\1}
#endif'''
    )
    patch_file_regex(numeric_h, pattern_fpfs, replacement_fpfs, 'Use strtof/strtod in detail::fpfs on Android')

    # Patch vec128_t float parser
    pattern_vec128 = (
        r'(\s+)auto result = std::from_chars\(p, end, v\.f32\[i\], std::chars_format::general\);\n'
        r'\s+if \(result\.ec != std::errc\(\)\) \{\n'
        r'\s+assert_always\(\);\n'
        r'\s+return vec128_t\(\);\n'
        r'\s+\}\n'
        r'\s+p = result\.ptr;'
    )
    replacement_vec128 = (
        r'''\1#if defined(__ANDROID__)
\1char* next_p = nullptr;
\1v.f32[i] = std::strtof(p, &next_p);
\1if (next_p == p) {
\1  assert_always();
\1  return vec128_t();
\1}
\1p = next_p;
#else
\1auto result = std::from_chars(p, end, v.f32[i], std::chars_format::general);
\1if (result.ec != std::errc()) {
\1  assert_always();
\1  return vec128_t();
\1}
\1p = result.ptr;
#endif'''
    )
    patch_file_regex(numeric_h, pattern_vec128, replacement_vec128, 'Use strtof in vec128_t on Android')

    print("Android patching completed.")

if __name__ == "__main__":
    main()
