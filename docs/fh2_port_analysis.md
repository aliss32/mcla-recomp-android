# FH2 Recompiled → MCLA port analysis (Phase 0)

Source: [Gabrick75/fh2-recompiled](https://github.com/Gabrick75/fh2-recompiled), cloned at
`%TEMP%\fh2-ref` (read-only reference). Our project: `MclaAndroidPort`
(`com.mcla.recomp`, Midnight Club: LA Recompiled, title `545407F8` per `mcla-recomp/config/mcla.toml`).

## Licensing

- fh2-recompiled `LICENSE`: GPL-3.0 (checked at clone time).
- Our `mcla-recomp/LICENSE`: GPL-3.0 → compatible. Copying/adapting is permitted with attribution.
- fh2 `sdk/`: BSD-3 (per its THIRD_PARTY_NOTICES) — also compatible.
- Required credit: keep an "FH2-derived" note in our launcher UI + THIRD_PARTY_NOTICES.md entry.

## What FH2 does that we port (and to what)

| FH2 component | File(s) | Port decision for MCLA |
|---|---|---|
| Launcher menu pages (settings / graphics / performance / diagnostics, programmatic views, no layout XML) | `android/app/src/main/java/com/forzahorizon2/android/MainActivity.java` (page builders: settings/graphics/performance/diagnostics/game), `Menu.java` (UI widgets), `GameOptions.java` (prefs + `arguments(Context)` arg builder) | Port page structure and widget code into our `LauncherActivity` (which already builds programmatic views), renaming `fh2`→`mcla`, `FH2 Recompiled`→`Midnight Club: LA Recompiled`. Keep FH2-derived credit. FH2-only settings (FSR mode, anisotropic override mapping, FH2 custom shaders) are dropped; MCLA cvars (`--mcla_*`) used instead. |
| Renderer selection | FH2: single renderer (xenos emulation) with optional `nfsmw`-native via `app/src/nativo` (NFSC_RECOMP/FH2_RECOMP branches), launched through `--gpu_plugin` | MCLA gets a 3-way selector: `xenos` (default), `mcla-native` (our existing plugin, experimental/diagnostic — known hang `phase=resolve_drain STALLED>2s` after ~1654 draws), `fh2-native` (listed but disabled: "not available in this build"; plugin name reserved `mcla-fhnative`). Setting persists and maps to `--gpu_plugin` at launch. |
| Native renderer engine | `app/src/nativo/*` (Vulkan PM4 ring renderer, shader library, `nfsp` container, `tools/xenos` translator), from StevensND/nfsmw-nx (GPL-3.0) | **Not ported in Phase 1.** Per nfsmw-nx `docs/porting-another-game.md`, porting requires game-specific `RECOMP` branch work (addresses, shared constant layout, PM4 analysis) — a Phase 2+ effort. `mcla-native` already exists in our tree as the diagnostic plugin. |
| Driver status JSON / Vulkan feature check | FH2 `GpuDrivers.java` + `GpuProbeActivity.java` (isolated process Vulkan probe, `--android_gpu_driver_dir/name` args) | We already have `DriverBridge.java` (Turnip proxy) — extended, not replaced (DO NOT TOUCH list). Renderer availability check (1.3) reuses the same driver-status JSON the bridge produces. |
| Crash-loop guard | FH2: none explicit | New (1.4): heartbeat `STALLED` detection from `MclaRecomp-logs/mcla_heartbeat.log` + game-process death → offer "reset to safe defaults" on next launch. |
| Args editor | FH2 `GameOptions.arguments()` builds fixed arg list from prefs | Extended (1.5): three layers — UI preview (RO) + user extra args (free text) + final command line preview; validation against build-time known-cvar list; extra args win precedence. |
| Diagnostics export | FH2 `Diagnostics.java` (zip of own logs, redaction, size cap, share) | Port (1.8) as `Diagnostics.java` in `com.mcla`, adding our `last_launch.txt`, frame census, heartbeat log, logcat of our PID. |
| Game folder validation | FH2: checks `default.xex` + two .xex DLLs + media (per its game-folder footer text) | MCLA validation derived from our code: `MclaApp::OnConfigurePaths` accepts a folder containing `default.xex`; runtime needs it (XEX loader) — validation checks `default.xex` presence + non-empty and names the missing item. Lookup location unchanged (DO NOT TOUCH). |
| last_launch.txt | FH2: writes device/driver/report info | We already have `MclaActivity.writeLaunchReport` (writes `last_launch.txt`); extend (1.7) with renderer id, final args, plugin path, library/driver status, and the "debug log: could not start" message must state WHICH process (launcher vs game) failed and why. |

## Verified facts used above

- Our two known CI compile errors were already fixed in separate commits: `5c9035b` (mutable
  `std::mutex` in `frame_census.h:190`) and `620c259` (`mcla_debug_clear` declared via
  `REXCVAR_DEFINE_BOOL`). No further fix commits needed.
- `mcla-recomp/src/mcla_app.cpp:118-149`: `OnPreSetup` reads `config.gpu_plugin` (defaults
  `"xenos"`), and for `"mcla-native"` enables the frame census via `REXCVAR_GET/SET(mcla_native_census)`.
  The selector therefore only needs to set `--gpu_plugin=<id>`; no native-side change required.
- `MclaActivity.prepareEnvironment` (MclaActivity.java:219-309) already: copies `default_args.txt`
  from assets to `args.txt` on first launch, applies perf-test extras via intent extras, removes
  `--log_level` for debug-log launches, adds `--diagnostics`, strips `--mcla_surface` for Java-side
  surface sizing, reads `driver.txt`/`env.txt`. New renderer + extra-args layers must integrate here
  without changing data dirs or lookup logic.
- Heartbeat evidence of the mcla-native hang (device pull, 2026-10-16 21:01 session):
  `hb draws=1654 ... phase=resolve_drain STALLED>2s swap_out=0 endframe=0` repeated — matches the
  known experimental-plugin hang. This justifies labelling `mcla-native` "experimental / diagnostic"
  and gating the crash-loop reset offer on it.
- Device pull of `Android/data/com.mcla.recomp/files/` shows: `MclaRecomp/`, `args.txt`, `install/`,
  `last_launch.txt`, `mcla/` — consistent with our `MclaApp` path discovery
  (`XDG_DATA_HOME/MclaRecomp/game`) and `last_launch.txt` location.
- Launch report already logs `Midnight Club: LA Recompiled for Android` title (MclaActivity.java:124),
  so only the renderer/args/plugin lines are new, not a rewrite.

## Phase 1 scope (implemented next)

1.1 Port launcher screens (settings/graphics/performance/diagnostics) adapted to MCLA; remove
    FH2-only/unimplemented settings; renames as above; keep "FH2-derived" credit.
1.2 Renderer setting `renderer ∈ {xenos, mcla-native, fh2-native}`, persisted, mapped to `--gpu_plugin`.
1.3 Availability check before launch (plugin `.so` present, Vulkan features from driver-status JSON);
    UI reason when disabled; fallback to default on failure.
1.4 Crash-loop guard: death or heartbeat STALLED within N seconds on non-default renderer →
    next launch offers "reset to safe defaults".
1.5 Launch-arguments editor: RO preview + user extra args + final command-line preview; validate
    `--name=value` against build-time known list (REXCVAR_* + ours); warn unknown, reject malformed,
    never crash; document precedence (extra args win); Reset / Export / Import (JSON) / per-renderer
    profiles; persist, migrate existing prefs.
1.6 Game folder validation with clear missing-item message; never crash or silently continue; no
    lookup-location change.
1.7 `last_launch.txt` gains renderer id, final args, plugin path, library/driver status; explicit
    which-process "debug log: could not start" reason.
1.8 Diagnostics export zip (UTF-8, redacted, ~5 MB cap, shareable).
1.9 JVM unit tests for arg builder/validator/availability/migration; run in CI.

## NEEDS DEVICE (cannot verify on host)

- Actual renderer switch end-to-end (launch with `--gpu_plugin=mcla-native`, observe census/hang).
- Availability check against real driver-status JSON from `DriverBridge`.
- Crash-loop reset offer after a real hang.
- Diagnostics share-sheet on-device.
- (Blocked at time of writing: `adb: no devices/emulators found` — device not connected.)

## DO NOT TOUCH (compat report will evidence each)

`applicationId`; `:game` process split; `LauncherActivity`/`MclaActivity` names; signing config;
data dirs (`logs` paths); game-folder resolution logic; driver proxy/Turnip loading flow; Vulkan
driver test (extend, not replace); NDK r27c / Clang 18 toolchain / CI order; DEFAULT renderer (`xenos`).
