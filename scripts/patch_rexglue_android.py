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

    # 1. Root CMakeLists: clear library config postfixes on Android.
    #     SDLActivity loads libraries by fixed name (libSDL3.so, libmain.so,
    #     librexruntime.so, librexgpu-xenos.so). RelWithDebInfo would name them
    #     librexruntimerd.so / librexgpu-xenosrd.so (same approach as
    #     LibertyRecompAndroid).
    sdk_cmake = os.path.join(sdk_dir, "CMakeLists.txt")
    if not os.path.exists(sdk_cmake):
        print(f"[ERROR] Required CMakeLists is missing: {sdk_cmake}")
        return 1
    with open(sdk_cmake, 'r', encoding='utf-8') as f:
        sdk_cmake_content = f.read().replace('\r\n', '\n')
    if 'if(ANDROID)\n    # SDLActivity loads libraries by fixed name' in sdk_cmake_content:
        print('[SKIP] CMakeLists.txt: Android postfix override already present')
    else:
        postfix_old = ('set(CMAKE_CXX_EXTENSIONS OFF)\n'
                       'set(CMAKE_DEBUG_POSTFIX d)\n'
                       'set(CMAKE_RELWITHDEBINFO_POSTFIX rd)')
        postfix_new = ('set(CMAKE_CXX_EXTENSIONS OFF)\n'
                       'if(ANDROID)\n'
                       '    # SDLActivity loads libraries by fixed name\n'
                       '    # (libSDL3.so, libmain.so, librexruntime.so, librexgpu-xenos.so).\n'
                       '    set(CMAKE_DEBUG_POSTFIX "")\n'
                       '    set(CMAKE_RELWITHDEBINFO_POSTFIX "")\n'
                       'else()\n'
                       '    set(CMAKE_DEBUG_POSTFIX d)\n'
                       '    set(CMAKE_RELWITHDEBINFO_POSTFIX rd)\n'
                       'endif()')
        if postfix_old in sdk_cmake_content:
            sdk_cmake_content = sdk_cmake_content.replace(postfix_old, postfix_new, 1)
            with open(sdk_cmake, 'w', encoding='utf-8', newline='\n') as f:
                f.write(sdk_cmake_content)
            print('[OK] Patched CMakeLists.txt: clear config postfixes on Android')
        else:
            print('[ERROR] CMakeLists.txt: postfix anchor not found')
            return 1

    # 1a. windowed_app.h: keep the single-app creator path on Android.
    #     The pinned SDK enables XE_UI_WINDOWED_APPS_IN_LIBRARY on Android,
    #     which removes rex::ui::GetWindowedAppCreator() that the SDL entry
    #     point (windowed_app_main_sdl.cpp) calls. Android hosts a single SDL
    #     app through SDLActivity exactly like the desktop executables, so
    #     library mode is not used (same approach as LibertyRecompAndroid).
    #     main() on Android is renamed to SDL_main via <SDL3/SDL_main.h>, which
    #     libmain.so exports for SDLActivity.
    windowed_app_h = os.path.join(sdk_dir, "include", "rex", "ui", "windowed_app.h")
    if not os.path.exists(windowed_app_h):
        print(f"[ERROR] Required header is missing: {windowed_app_h}")
        return 1
    with open(windowed_app_h, 'r', encoding='utf-8') as f:
        wah_content = f.read().replace('\r\n', '\n')
    if 'XE_UI_WINDOWED_APPS_IN_LIBRARY 1' in wah_content:
        define_old = '#define XE_UI_WINDOWED_APPS_IN_LIBRARY 1'
        define_new = ('// Android hosts the single SDL application through SDLActivity, exactly\n'
                      '// like the desktop executables; multi-app library mode is not used.\n'
                      '// #define XE_UI_WINDOWED_APPS_IN_LIBRARY 1')
        if define_old in wah_content:
            wah_content = wah_content.replace(define_old, define_new, 1)
            with open(windowed_app_h, 'w', encoding='utf-8', newline='\n') as f:
                f.write(wah_content)
            print('[OK] Patched windowed_app.h: keep GetWindowedAppCreator on Android (disable library mode)')
        else:
            print('[ERROR] windowed_app.h: XE_UI_WINDOWED_APPS_IN_LIBRARY define anchor not found')
            return 1
    else:
        print('[SKIP] windowed_app.h: library mode already disabled on Android')

    # 1b. windowed_app_main_sdl.cpp: rename main to SDL_main on Android so
    #     SDLActivity resolves the entry point in libmain.so.
    app_main_sdl = os.path.join(sdk_dir, "src", "ui", "windowed_app_main_sdl.cpp")
    if os.path.exists(app_main_sdl):
        with open(app_main_sdl, 'r', encoding='utf-8') as f:
            am_content = f.read().replace('\r\n', '\n')
        if 'SDL3/SDL_main.h' in am_content:
            print('[SKIP] windowed_app_main_sdl.cpp: SDL_main rename already present')
        else:
            main_old = 'int main(int argc, char* argv[]) {'
            main_new = ('#if REX_PLATFORM_ANDROID\n'
                        '// Renames main to SDL_main, which SDLActivity resolves in libmain.so.\n'
                        '#include <SDL3/SDL_main.h>\n'
                        '#endif\n'
                        'int main(int argc, char* argv[]) {')
            if main_old in am_content:
                am_content = am_content.replace(main_old, main_new, 1)
                with open(app_main_sdl, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(am_content)
                print('[OK] Patched windowed_app_main_sdl.cpp: main -> SDL_main on Android')
            else:
                print('[ERROR] windowed_app_main_sdl.cpp: main() anchor not found')
                return 1
    else:
        print('[WARN] windowed_app_main_sdl.cpp not found; skipping SDL_main rename')

    # 1b-2. rex_app.cpp: the runtime log directory must be writable on Android.
    #     The pinned SDK already has the right idea (REX_PLATFORM_MAC ||
    #     defined(__ANDROID__) -> user_dir/logs), but the upstream condition in
    #     some revisions only covers REX_PLATFORM_MAC; without the Android arm
    #     the log dir resolves next to the executable (nativeLibraryDir -
    #     read-only), create_directories throws filesystem_error and the game
    #     aborts with "Read-only file system [/system/bin/logs]" right after
    #     SDL_main starts.
    rex_app_cpp = os.path.join(sdk_dir, "src", "ui", "rex_app.cpp")
    if os.path.exists(rex_app_cpp):
        with open(rex_app_cpp, 'r', encoding='utf-8') as f:
            rax_content = f.read().replace('\r\n', '\n')
        # Match the #if guarding the log_dir selection whatever form it takes.
        # The block is recognizable by the exe_dir/logs assignment in its else
        # arm:
        #   #if <cond>
        #     log_config.log_dir = (user_dir / "logs").string();
        #   #else
        #     log_config.log_dir = (exe_dir / "logs").string();
        #   #endif
        block_re = re.compile(
            r'(?m)^(\s*)#if ([^\n]*\n)'                       # group 1: indent, group 2: condition
            r'(\s*//[^\n]*\n)*'                                # comment lines
            r'\s*log_config\.log_dir = \(user_dir / "logs"\)\.string\(\);\n'
            r'\s*#else\n'
            r'(?:\s*//[^\n]*\n)*'
            r'\s*log_config\.log_dir = \(exe_dir / "logs"\)\.string\(\);\n')
        match = block_re.search(rax_content)
        if not match:
            print('[WARN] rex_app.cpp: log dir block not found; the runtime log '
                  'may try the executable folder (crash on Android)')
        elif 'defined(__ANDROID__)' in match.group(2) or 'REX_PLATFORM_ANDROID' in match.group(2):
            print('[SKIP] rex_app.cpp: Android log dir condition already present')
        else:
            new_cond = match.group(2).rstrip()
            if 'REX_PLATFORM_MAC' in new_cond:
                new_cond += ' || defined(__ANDROID__)'
            else:
                new_cond += ' || REX_PLATFORM_ANDROID'
            replacement = (match.group(1) + '#if ' + new_cond + '\n'
                           + match.group(0)[match.end(2) - match.start(0) + len(match.group(2)) + 1:])
            rax_content = rax_content[:match.start()] + replacement + rax_content[match.end():]
            with open(rex_app_cpp, 'w', encoding='utf-8', newline='\n') as f:
                f.write(rax_content)
            print('[OK] Patched rex_app.cpp: runtime logs go to the user dir on Android')
    else:
        print('[WARN] rex_app.cpp not found; skipping log dir patch')

    # 1c. UI CMakeLists: Disable X11 / Wayland on Android
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

    # Android must not fall through to Linux X11/Wayland UI code. Keep the
    # native Android surface when available; guard final desktop #else fallbacks
    # in older pinned SDK revisions explicitly.
    window_sdl = os.path.join(sdk_dir, "src", "ui", "window_sdl.cpp")
    if not os.path.exists(window_sdl):
        print(f"[ERROR] Required SDL window source is missing: {window_sdl}")
        return 1
    with open(window_sdl, 'r', encoding='utf-8') as f:
        window_content = f.read().replace('\r\n', '\n')

    has_android_surface = (
        'AndroidNativeWindowSurface' in window_content
        and os.path.exists(os.path.join(sdk_dir, "include", "rex", "ui", "surface_android.h"))
        and os.path.exists(os.path.join(sdk_dir, "src", "ui", "surface_android.cpp"))
    )
    if has_android_surface:
        include_old = ('#else\n'
                       '#include <X11/Xlib-xcb.h>\n'
                       '#include <rex/ui/surface_gnulinux.h>')
        include_new = ('#elif REX_PLATFORM_ANDROID\n'
                       '#include <rex/ui/surface_android.h>\n'
                       '#else\n'
                       '#include <X11/Xlib-xcb.h>\n'
                       '#include <rex/ui/surface_gnulinux.h>')
        if include_new not in window_content:
            if include_old not in window_content:
                if ('#elif REX_PLATFORM_ANDROID\n#include <rex/ui/surface_android.h>'
                        not in window_content):
                    print('[ERROR] Android surface include anchor is missing')
                    return 1
            else:
                window_content = window_content.replace(include_old, include_new, 1)
        surface_old = '  if (allowed_types & Surface::kTypeFlag_XcbWindow)'
        surface_new = ('#elif REX_PLATFORM_ANDROID\n'
                       '  if (allowed_types & Surface::kTypeFlag_AndroidNativeWindow) {\n'
                       '    auto* native_window = static_cast<ANativeWindow*>(GetNativeWindowHandle());\n'
                       '    if (native_window) return std::make_unique<AndroidNativeWindowSurface>(native_window);\n'
                       '  }\n'
                       '#else\n'
                       '  if (allowed_types & Surface::kTypeFlag_XcbWindow)')
        if surface_new not in window_content:
            if surface_old not in window_content:
                print('[ERROR] Android surface exists but GNU/Linux XCB surface anchor is missing')
                return 1
            window_content = window_content.replace(surface_old, surface_new, 1)
        if '#elif REX_PLATFORM_ANDROID\n#include <rex/ui/surface_android.h>' not in window_content:
            print('[ERROR] Android SDL surface include was not configured')
            return 1
        if '#elif REX_PLATFORM_ANDROID\n  if (allowed_types & Surface::kTypeFlag_AndroidNativeWindow)' not in window_content:
            print('[ERROR] Android SDL surface selection was not configured')
            return 1
        print('[OK] Android SDL surface selection precedes GNU/Linux X11 branches')
    else:
        linux_elif = re.compile(
            r'(?m)^(\s*#\s*elif\s+REX_PLATFORM_GNU_LINUX)(?!\s*&&\s*!defined\(__ANDROID__\))'
        )
        window_content, guarded_arms = linux_elif.subn(
            r'\1 && !defined(__ANDROID__)', window_content
        )
        if guarded_arms:
            print(f"[OK] Added Android exclusion to {guarded_arms} GNU/Linux SDL branches")

        # Older pinned SDKs use a final #else for X11 rather than an explicit
        # GNU/Linux condition. Split desktop fallbacks so Android cannot select them.
        fallback_include = '#else\n#include <X11/Xlib-xcb.h>'
        guarded_include = ('#elif REX_PLATFORM_GNU_LINUX && !defined(__ANDROID__)\n'
                           '#include <X11/Xlib-xcb.h>')
        if fallback_include in window_content:
            window_content = window_content.replace(fallback_include, guarded_include, 1)
            print('[OK] Excluded Android from fallback X11 SDL includes')
        if fallback_include in window_content:
            print('[ERROR] Could not exclude Android from the fallback X11 include')
            return 1

        mac_surface_start = window_content.find(
            '#elif REX_PLATFORM_MAC\n'
            '  if (allowed_types & Surface::kTypeFlag_CAMetalLayer)'
        )
        if mac_surface_start >= 0:
            xcb_start = window_content.find(
                'if (allowed_types & Surface::kTypeFlag_XcbWindow)', mac_surface_start
            )
            surface_else = window_content.find('#else', mac_surface_start, xcb_start)
            if xcb_start >= 0 and surface_else >= 0:
                window_content = (window_content[:surface_else]
                                 + '#elif REX_PLATFORM_GNU_LINUX && !defined(__ANDROID__)'
                                 + window_content[surface_else + len('#else'):])
                print('[OK] Excluded Android from fallback X11/Wayland SDL surface code')

        if re.search(
            r'(?m)^\s*#\s*elif\s+REX_PLATFORM_GNU_LINUX(?!\s*&&\s*!defined\(__ANDROID__\))',
            window_content,
        ):
            print('[ERROR] An unguarded GNU/Linux branch remains in window_sdl.cpp')
            return 1
        if '#include <X11/Xlib-xcb.h>' not in window_content:
            print('[ERROR] Expected X11 include not found; cannot verify Android exclusion')
            return 1
        if 'Surface::kTypeFlag_XcbWindow' not in window_content:
            print('[ERROR] Expected XCB surface branch not found; cannot verify Android exclusion')
            return 1
        if ('#elif REX_PLATFORM_GNU_LINUX && !defined(__ANDROID__)\n'
                '#include <X11/Xlib-xcb.h>' not in window_content):
            print('[ERROR] X11 include is not guarded from Android')
            return 1
        if not re.search(
            r'#elif REX_PLATFORM_GNU_LINUX && !defined\(__ANDROID__\)\s+'
            r'SDL_PropertiesID props[\s\S]*Surface::kTypeFlag_XcbWindow',
            window_content,
        ):
            print('[ERROR] X11 surface is not guarded from Android')
            return 1
        print('[OK] Android is excluded from GNU/Linux/X11 SDL branches; no native Android surface is available')

        # Older pinned SDK revisions use generic UNIX/else CMake fallbacks for
        # Linux UI sources and X11/Wayland dependencies. Keep Android out of
        # those desktop-only paths as well.
        patch_file(
            ui_cmake,
            'elseif(APPLE)\n    set(REXUI_PLATFORM_SOURCES\n        surface_mac.cpp\n    )\nelse()\n    set(REXUI_PLATFORM_SOURCES\n        surface_gnulinux.cpp\n    )',
            'elseif(APPLE)\n    set(REXUI_PLATFORM_SOURCES\n        surface_mac.cpp\n    )\nelseif(ANDROID)\n    set(REXUI_PLATFORM_SOURCES)\nelse()\n    set(REXUI_PLATFORM_SOURCES\n        surface_gnulinux.cpp\n    )',
            'Set empty REXUI_PLATFORM_SOURCES for older Android SDK revisions',
        )
        patch_file(
            ui_cmake,
            'if(UNIX AND NOT APPLE)\n    find_package(PkgConfig REQUIRED)',
            'if(UNIX AND NOT APPLE AND NOT ANDROID)\n    find_package(PkgConfig REQUIRED)',
            'Disable X11/Wayland pkg-config dependencies on Android',
        )

    # 1b. Android native-window surface. The mcla patch's Vulkan presenter
    #     (src/ui/vulkan/vulkan_presenter.cpp) already includes
    #     <rex/ui/surface_android.h> and casts to AndroidNativeWindowSurface
    #     under REX_PLATFORM_ANDROID, so the class and its translation unit
    #     must exist and WindowSDL must be able to create it.
    surface_android_h = os.path.join(sdk_dir, "include", "rex", "ui", "surface_android.h")
    surface_android_cpp = os.path.join(sdk_dir, "src", "ui", "surface_android.cpp")
    if not os.path.exists(surface_android_h):
        os.makedirs(os.path.dirname(surface_android_h), exist_ok=True)
        with open(surface_android_h, 'w', encoding='utf-8', newline='\n') as f:
            f.write('''#pragma once
#include <rex/ui/surface.h>
#include <android/native_window.h>

namespace rex {
namespace ui {

class AndroidNativeWindowSurface final : public Surface {
 public:
  // Holds its own reference: SDL releases the ANativeWindow as soon as the
  // Java surface is destroyed (screen off, home), while the presenter may
  // still be using this surface.
  explicit AndroidNativeWindowSurface(ANativeWindow* window) : window_(window) {
    if (window_) ANativeWindow_acquire(window_);
  }
  ~AndroidNativeWindowSurface() override {
    if (window_) ANativeWindow_release(window_);
  }
  AndroidNativeWindowSurface(const AndroidNativeWindowSurface&) = delete;
  AndroidNativeWindowSurface& operator=(const AndroidNativeWindowSurface&) = delete;

  TypeIndex GetType() const override { return kTypeIndex_AndroidNativeWindow; }
  ANativeWindow* window() const { return window_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override {
    if (!window_) return false;
    width_out = static_cast<uint32_t>(ANativeWindow_getWidth(window_));
    height_out = static_cast<uint32_t>(ANativeWindow_getHeight(window_));
    return true;
  }

 private:
  ANativeWindow* window_;
};

}  // namespace ui
}  // namespace rex
''')
        print("[OK] Wrote include/rex/ui/surface_android.h")
    else:
        print("[SKIP] include/rex/ui/surface_android.h exists")
    if not os.path.exists(surface_android_cpp):
        os.makedirs(os.path.dirname(surface_android_cpp), exist_ok=True)
        with open(surface_android_cpp, 'w', encoding='utf-8', newline='\n') as f:
            f.write('#include <rex/ui/surface_android.h>\n')
        print("[OK] Wrote src/ui/surface_android.cpp")
    else:
        print("[SKIP] src/ui/surface_android.cpp exists")
    patch_file(
        ui_cmake,
        'elseif(ANDROID)\n    set(REXUI_PLATFORM_SOURCES)',
        'elseif(ANDROID)\n    set(REXUI_PLATFORM_SOURCES\n        surface_android.cpp\n    )',
        'Build surface_android.cpp for Android'
    )

    # 1c. WindowSDL must return the ANativeWindow handle and create the
    #     Android surface, and detach it on app background events.
    with open(window_sdl, 'r', encoding='utf-8') as f:
        window_content = f.read().replace('\r\n', '\n')
    if 'SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER' not in window_content:
        old_handle = '''void* WindowSDL::GetNativeWindowHandle() const {
#if REX_PLATFORM_WIN32
  if (!sdl_window_) {
    return nullptr;
  }
  return SDL_GetPointerProperty(SDL_GetWindowProperties(sdl_window_),
                                SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
  return nullptr;
#endif
}'''
        new_handle = '''void* WindowSDL::GetNativeWindowHandle() const {
  if (!sdl_window_) {
    return nullptr;
  }
#if REX_PLATFORM_ANDROID
  return SDL_GetPointerProperty(SDL_GetWindowProperties(sdl_window_),
                                SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#elif REX_PLATFORM_WIN32
  return SDL_GetPointerProperty(SDL_GetWindowProperties(sdl_window_),
                                SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
  return nullptr;
#endif
}'''
        if old_handle in window_content:
            window_content = window_content.replace(old_handle, new_handle, 1)
            print('[OK] Android ANativeWindow handle in GetNativeWindowHandle')
        else:
            print('[ERROR] GetNativeWindowHandle anchor not found')
            return 1
    else:
        print('[SKIP] GetNativeWindowHandle already returns the Android window')
    if ('#elif REX_PLATFORM_ANDROID\n'
            '  if (allowed_types & Surface::kTypeFlag_AndroidNativeWindow)' not in window_content):
        inserted = False
        for anchor in (
            '#elif REX_PLATFORM_GNU_LINUX && !defined(__ANDROID__)\n  SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window_);',
            '#else\n  SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window_);\n  if (allowed_types & Surface::kTypeFlag_WaylandSurface) {',
        ):
            android_surface = ('#elif REX_PLATFORM_ANDROID\n'
                               '  if (allowed_types & Surface::kTypeFlag_AndroidNativeWindow) {\n'
                               '    if (auto* native_window = static_cast<ANativeWindow*>(GetNativeWindowHandle())) {\n'
                               '      return std::make_unique<AndroidNativeWindowSurface>(native_window);\n'
                               '    }\n'
                               '  }\n')
            if anchor in window_content:
                if anchor.startswith('#else'):
                    # Split the desktop #else: Android gets its own arm, and
                    # the remaining Wayland/X11 code stays GNU/Linux-only.
                    android_surface += '#elif REX_PLATFORM_GNU_LINUX && !defined(__ANDROID__)\n'
                    window_content = window_content.replace(anchor, android_surface + anchor[len('#else\n'):], 1)
                else:
                    window_content = window_content.replace(anchor, android_surface + anchor, 1)
                inserted = True
                print('[OK] Android surface creation in CreateSurfaceImpl')
                break
        if not inserted:
            print('[ERROR] CreateSurfaceImpl GNU/Linux anchor not found')
            return 1
    else:
        print('[SKIP] CreateSurfaceImpl already handles Android')
    if '#include <rex/ui/surface_android.h>' not in window_content:
        inserted = False
        for anchor, replacement in (
            ('#elif REX_PLATFORM_GNU_LINUX && !defined(__ANDROID__)\n#include <X11/Xlib-xcb.h>',
             '#elif REX_PLATFORM_ANDROID\n#include <rex/ui/surface_android.h>\n'
             '#elif REX_PLATFORM_GNU_LINUX && !defined(__ANDROID__)\n#include <X11/Xlib-xcb.h>'),
            ('#else\n#include <X11/Xlib-xcb.h>\n#include <rex/ui/surface_gnulinux.h>\n#endif',
             '#elif REX_PLATFORM_ANDROID\n#include <rex/ui/surface_android.h>\n'
             '#elif REX_PLATFORM_GNU_LINUX && !defined(__ANDROID__)\n'
             '#include <X11/Xlib-xcb.h>\n#include <rex/ui/surface_gnulinux.h>\n#endif'),
        ):
            if anchor in window_content:
                window_content = window_content.replace(anchor, replacement, 1)
                inserted = True
                print('[OK] surface_android.h included in window_sdl.cpp')
                break
        if not inserted:
            print('[ERROR] window_sdl.cpp include anchor not found')
            return 1
    else:
        print('[SKIP] surface_android.h already included in window_sdl.cpp')

    # 1d. WindowSDL header: app lifecycle handler and detached-surface flag.
    window_sdl_h = os.path.join(sdk_dir, "include", "rex", "ui", "window_sdl.h")
    if os.path.exists(window_sdl_h):
        with open(window_sdl_h, 'r', encoding='utf-8') as f:
            h_content = f.read().replace('\r\n', '\n')
        if 'HandleAppLifecycleEvent' not in h_content:
            anchor = '  void HandleDropEvent(SDL_Event& event);'
            if anchor in h_content:
                h_content = h_content.replace(
                    anchor,
                    '  // Android destroys the window surface when the app is backgrounded (screen\n'
                    '  // off, home) and creates a new one on return; the presenter must follow.\n'
                    '  void HandleAppLifecycleEvent(SDL_Event& event);\n'
                    + anchor, 1)
                print('[OK] HandleAppLifecycleEvent declared in window_sdl.h')
            else:
                print('[ERROR] window_sdl.h method anchor not found')
                return 1
        if 'surface_detached_' not in h_content:
            anchor = '  SDLWindowedAppContext& sdl_app_context() const {'
            if anchor in h_content:
                h_content = h_content.replace(
                    anchor,
                    '  // Android: the presenter surface was dropped while backgrounded.\n'
                    '  bool surface_detached_ = false;\n\n'
                    + anchor, 1)
                print('[OK] surface_detached_ member added to window_sdl.h')
            else:
                print('[ERROR] window_sdl.h member anchor not found')
                return 1
        with open(window_sdl_h, 'w', encoding='utf-8', newline='\n') as f:
            f.write(h_content)
    else:
        print('[ERROR] window_sdl.h not found')
        return 1

    # 1e. window_sdl.cpp: Android lifecycle handling (detach/reattach the
    #     presenter surface around background/foreground events).
    if 'HandleAppLifecycleEvent' not in window_content:
        lifecycle = '''void WindowSDL::HandleAppLifecycleEvent(SDL_Event& event) {
#if REX_PLATFORM_ANDROID
  // SDL reports backgrounding as window minimize/restore on Android, and
  // only after resume: SDL blocks this thread while paused.
  switch (event.type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_HIDDEN:
      // The ANativeWindow is (or is about to be) released; drop the swapchain
      // and VkSurfaceKHR built on it.
      if (!surface_detached_) {
        surface_detached_ = true;
        OnSurfaceChanged(false);
      }
      break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_SHOWN:
      if (surface_detached_ && GetNativeWindowHandle()) {
        surface_detached_ = false;
        OnSurfaceChanged(true);
      }
      break;
    default:
      break;
  }
#else
  (void)event;
#endif
}

'''
        for anchor in ('void WindowSDL::HandleTouchEvent(SDL_Event& event) {',
                       'void WindowSDL::HandleMouseEvent(SDL_Event& event) {'):
            if anchor in window_content:
                window_content = window_content.replace(anchor, lifecycle + anchor, 1)
                break
        else:
            print('[ERROR] lifecycle insertion anchor not found in window_sdl.cpp')
            return 1
        print('[OK] Android app lifecycle handling added to window_sdl.cpp')
    else:
        print('[SKIP] window_sdl.cpp already handles app lifecycle events')

    # 1f. windowed_app_context_sdl.cpp: dispatch lifecycle events and keep
    #     Android out of the GNU/Linux surface includes.
    app_context_cpp = os.path.join(sdk_dir, "src", "ui", "windowed_app_context_sdl.cpp")
    if os.path.exists(app_context_cpp):
        with open(app_context_cpp, 'r', encoding='utf-8') as f:
            ac_content = f.read().replace('\r\n', '\n')
        if 'HandleAppLifecycleEvent' not in ac_content:
            anchor = ('    case SDL_EVENT_WILL_ENTER_BACKGROUND:\n'
                      '    case SDL_EVENT_DID_ENTER_BACKGROUND:\n'
                      '    case SDL_EVENT_DID_ENTER_FOREGROUND: {\n'
                      '      for (const auto& [window_id, window] : windows_) {\n'
                      '        (void)window_id;\n'
                      '        if (window) {\n'
                      '          window->HandleAppLifecycleEvent(event);\n'
                      '        }\n'
                      '      }\n'
                      '      break;\n'
                      '    }\n')
            # The upstream context has no lifecycle dispatch; add it before
            # the gamepad switch (or the drop/default tail if gamepads differ).
            anchor2 = '    case SDL_EVENT_GAMEPAD_ADDED: {'
            anchor3 = '''    case SDL_EVENT_DROP_FILE: {
      if (WindowSDL* window = GetWindow(event.drop.windowID)) {
        window->HandleDropEvent(event);
      }
      break;
    }
'''
            if anchor2 in ac_content:
                ac_content = ac_content.replace(anchor2, anchor + anchor2, 1)
                print('[OK] Lifecycle event dispatch added to windowed_app_context_sdl.cpp')
            elif anchor3 in ac_content:
                ac_content = ac_content.replace(anchor3, anchor3 + anchor, 1)
                print('[OK] Lifecycle event dispatch added to windowed_app_context_sdl.cpp (before default)')
            else:
                print('[ERROR] windowed_app_context_sdl.cpp dispatch anchor not found')
                return 1
            with open(app_context_cpp, 'w', encoding='utf-8', newline='\n') as f:
                f.write(ac_content)
        else:
            print('[SKIP] windowed_app_context_sdl.cpp already dispatches lifecycle events')
    else:
        print('[WARN] windowed_app_context_sdl.cpp not found; skipping lifecycle dispatch')

    with open(window_sdl, 'w', encoding='utf-8', newline='\n') as f:
        f.write(window_content)

    patch_file(
        ui_cmake,
        'elseif(PS5)\n    # No window system surface; presentation is VK_KHR_display.\n    set(REXUI_PLATFORM_SOURCES)',
        'elseif(PS5)\n    # No window system surface; presentation is VK_KHR_display.\n    set(REXUI_PLATFORM_SOURCES)\nelseif(ANDROID)\n    set(REXUI_PLATFORM_SOURCES\n        surface_android.cpp\n    )',
        'Set REXUI_PLATFORM_SOURCES for Android'
    )
    patch_file(
        ui_cmake,
        'elseif(APPLE)\n    target_link_libraries(rexui PRIVATE "-framework CoreFoundation")',
        'elseif(APPLE)\n    target_link_libraries(rexui PRIVATE "-framework CoreFoundation")\nelseif(ANDROID)\n    target_link_libraries(rexui PRIVATE android)',
        'Link libandroid.so to rexui'
    )

    # 2b. The mcla patch's mapped_memory_posix.cpp OpenForAndroidContentUri
    #     calls rex::filesystem::OpenAndroidContentFileDescriptor; provide a
    #     stub so the Android runtime links (content:// URIs are not used by
    #     the MCLA port, which reads from app-private storage).
    filesystem_posix = os.path.join(sdk_dir, "src", "core", "filesystem_posix.cpp")
    if os.path.exists(filesystem_posix):
        with open(filesystem_posix, 'r', encoding='utf-8') as f:
            fp_content = f.read().replace('\r\n', '\n')
        stub = '''int OpenAndroidContentFileDescriptor(const std::string_view uri, const char*) {
  REXLOG_ERROR("content:// URIs are not supported: {}", uri);
  return -1;
}
#endif  // REX_PLATFORM_ANDROID
'''
        if 'OpenAndroidContentFileDescriptor' not in fp_content:
            anchor = '#endif  // REX_PLATFORM_ANDROID'
            if anchor in fp_content:
                fp_content = fp_content.replace(anchor, stub, 1)
                print('[OK] OpenAndroidContentFileDescriptor stub added to filesystem_posix.cpp')
            else:
                # No Android block yet; add one guarded before the closing namespace.
                anchor = '}  // namespace filesystem'
                guarded = '''#if REX_PLATFORM_ANDROID
bool IsAndroidContentUri(const std::string_view source) {
  return source.starts_with("content://");
}
int OpenAndroidContentFileDescriptor(const std::string_view uri, const char*) {
  REXLOG_ERROR("content:// URIs are not supported: {}", uri);
  return -1;
}
#endif  // REX_PLATFORM_ANDROID

'''
                if anchor not in fp_content:
                    print('[ERROR] filesystem_posix.cpp namespace anchor not found')
                    return 1
                fp_content = fp_content.replace(anchor, guarded + anchor, 1)
                print('[OK] OpenAndroidContentFileDescriptor stub (guarded block) added to filesystem_posix.cpp')
            with open(filesystem_posix, 'w', encoding='utf-8', newline='\n') as f:
                f.write(fp_content)
        else:
            print('[SKIP] filesystem_posix.cpp already has OpenAndroidContentFileDescriptor')
    else:
        print('[ERROR] filesystem_posix.cpp not found')
        return 1

    # 2c. rex::style.h (a PUBLIC SDK header included by the recompiled code's
    #     PCH) includes imgui.h, but imgui is linked PRIVATE to rexruntime, so
    #     consumers of rex::runtime get no imgui include path. Make it PUBLIC.
    kernel_cmake = os.path.join(sdk_dir, "src", "kernel", "CMakeLists.txt")
    patch_file(
        kernel_cmake,
        'target_link_libraries(rexruntime PRIVATE o1heap rexcore rexfilesystem rexui rexaudio rexinput imgui)',
        'target_link_libraries(rexruntime PRIVATE o1heap rexcore rexfilesystem rexui rexaudio rexinput)\n'
        '# rex::style.h is a public header and includes imgui.h; the include path\n'
        '# must reach consumers of rex::runtime.\n'
        'target_link_libraries(rexruntime PUBLIC imgui)',
        'Propagate the imgui include path to rex::runtime consumers'
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

    # Include the helper at the first Android-only section in memory_posix.cpp,
    # before AndroidInitialize() calls GetAndroidApiLevel().
    memory_posix = os.path.join(sdk_dir, "src", "core", "memory_posix.cpp")
    if not os.path.exists(memory_posix):
        print("[ERROR] Required Android source file is missing: memory_posix.cpp")
        return 1
    with open(memory_posix, 'r', encoding='utf-8') as f:
        memory_content = f.read().replace('\r\n', '\n')
    android_block = '#if REX_PLATFORM_ANDROID\n'
    android_helper_include = android_block + '#include <rex/main_android.h>'
    if android_helper_include not in memory_content:
        if android_block not in memory_content:
            print("[ERROR] Could not locate Android section in memory_posix.cpp")
            return 1
        memory_content = memory_content.replace(android_block, android_helper_include + '\n', 1)
        with open(memory_posix, 'w', encoding='utf-8') as f:
            f.write(memory_content)
        print("[OK] Added main_android.h at start of memory_posix.cpp Android section")
    else:
        print("[SKIP] memory_posix.cpp Android section already includes main_android.h")
    call_match = re.search(r'GetAndroidApiLevel\s*\(', memory_content)
    include_pos = memory_content.find('#include <rex/main_android.h>')
    if call_match is None or include_pos < 0 or include_pos > call_match.start():
        print("[ERROR] Android API-level helper is not included before its call in memory_posix.cpp")
        return 1
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

    # 8. Bionic does not consistently expose robust-mutex functions across NDK
    #    versions. Guard only the robust-mutex sections in the constructor,
    #    Wait(), and WaitMultiple(); leave unrelated Linux feature checks alone.
    threading_posix = os.path.join(sdk_dir, "src", "core", "threading_posix.cpp")
    if not os.path.exists(threading_posix):
        print(f"[ERROR] File not found: {threading_posix}")
        return 1
    with open(threading_posix, 'r', encoding='utf-8') as f:
        threading_content = f.read().replace('\r\n', '\n')

    robust_sections = (
        (
            '#if REX_PLATFORM_LINUX\n'
            '    // Use robust mutexes so waits can recover if owner thread terminates.',
            '#if REX_PLATFORM_LINUX && !REX_PLATFORM_ANDROID\n'
            '    // Use robust mutexes so waits can recover if owner thread terminates.',
            'Android guard for robust-mutex initialization',
        ),
        (
            '    auto predicate = [this] { return this->signaled(); };\n'
            '#if REX_PLATFORM_LINUX\n'
            '    auto native_mutex = static_cast<pthread_mutex_t*>(mutex_.native_handle());',
            '    auto predicate = [this] { return this->signaled(); };\n'
            '#if REX_PLATFORM_LINUX && !REX_PLATFORM_ANDROID\n'
            '    auto native_mutex = static_cast<pthread_mutex_t*>(mutex_.native_handle());',
            'Android guard for robust-mutex Wait recovery',
        ),
        (
            '      for (size_t i = 0; i < handles.size(); ++i) {\n'
            '#if REX_PLATFORM_LINUX\n'
            '        auto native_mutex = static_cast<pthread_mutex_t*>(handles[i]->mutex_.native_handle());',
            '      for (size_t i = 0; i < handles.size(); ++i) {\n'
            '#if REX_PLATFORM_LINUX && !REX_PLATFORM_ANDROID\n'
            '        auto native_mutex = static_cast<pthread_mutex_t*>(handles[i]->mutex_.native_handle());',
            'Android guard for robust-mutex WaitMultiple recovery',
        ),
    )
    for old_text, new_text, description in robust_sections:
        if new_text in threading_content:
            print(f"[SKIP] Already applied: {description}")
        elif old_text in threading_content:
            threading_content = threading_content.replace(old_text, new_text, 1)
            print(f"[OK] Applied: {description}")
        else:
            print(f"[ERROR] Could not apply: {description}")
            return 1
    with open(threading_posix, 'w', encoding='utf-8') as f:
        f.write(threading_content)

    print("Android patching completed.")
    return 0
if __name__ == "__main__":
    sys.exit(main())

