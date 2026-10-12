# Midnight Club: Los Angeles — Android Port (MCLA Recompiled)

An **ARM64 native Android port** of Midnight Club: Los Angeles, built on top of
[mcla-recomp](https://github.com/holdmysocks/mcla-recomp) (the decompilation engine)
and [LibertyRecompAndroid](https://github.com/vaduur/LibertyRecompAndroid) (the Android
launcher/SDL3 wrapper).

> **You must own a legitimate copy of the game.** This project ships no game assets.

---

## Features

- **Vulkan rendering via SDL3 / ANativeWindow** — runs natively on ARM64, no emulation.
- **Custom Vulkan driver support** — swap in Turnip (Mesa Adreno) `.zip` packages
  or fall back to the system driver. Bundled Turnip 26.3.0-R6 targets Adreno 6xx/7xx.
- **Custom game directory** — tap *Set Game Directory* in the launcher to point the
  engine at game files stored anywhere on your device (SD card, OTG drive, etc.).
- **Gamepad & touch controls** — built-in touch HUD and full Bluetooth/USB controller
  support via SDL3.
- **GitHub Actions CI/CD** — automatic APK builds on every push
  (`.github/workflows/build-android.yml`).

---

## Project Structure

```
MclaAndroidPort/
├── app/                     Android Gradle module (Launcher & MclaActivity)
│   └── src/main/java/com/mcla/
│       ├── LauncherActivity.java   First screen: game status, driver picker, game dir
│       ├── MclaActivity.java       SDL3 game activity (sets env vars, launches engine)
│       ├── DriverBridge.java       Vulkan driver selection via libadrenotools
│       ├── Installer.java          Game file extraction from ISO
│       ├── CrashReports.java       On-device crash report collection
│       └── DebugLog.java           logcat capture to files/debug_logs/
├── native/                  driver_proxy.cpp + libadrenotools (Turnip zip loader)
├── mcla-recomp/             MCLA game logic, ReXGlue bindings, CMakeLists
└── .github/workflows/       GitHub Actions build pipeline
```

---

## Building

### GitHub Actions (recommended)

1. Push to `main` — Actions triggers automatically.
2. Go to **Actions → Build MCLA Android APK** and download the artifact when done.

### Local build (Windows / Linux / macOS)

Requirements: Android Studio or command-line NDK r27+, CMake 3.22+, JDK 17+.

```bash
cd MclaAndroidPort
./gradlew assembleDebug
# APK lands in app/build/outputs/apk/debug/
```

---

## Installing & Running

1. Side-load the APK on an Android device with an **Adreno 6xx or 7xx GPU**.
2. Open the app — the launcher shows the game status.
3. Tap **Set Game Directory** and browse to the folder that contains `default.xex`
   (e.g. your extracted ISO contents).  
   The path is saved permanently; you only need to do this once.
4. Choose a Vulkan driver (*Turnip* is recommended for Adreno devices).
5. Press **Play**.

### Game files

The engine looks for `default.xex` inside the chosen directory.  
If you used *mcla-recomp* to extract the ISO, the layout should be:

```
<game dir>/
└── default.xex
└── (other game data files)
```

---

## Repository

`https://github.com/aliss32/mcla-recomp-android`

---

## Credits

- **mcla-recomp** — game decompilation engine by holdmysocks
- **LibertyRecompAndroid** — Android launcher / SDL3 wrapper by vaduur
- **libadrenotools** — Turnip driver injection by illusionman1212
- **Mesa/Turnip** — open-source Adreno Vulkan driver
