#include "generated/default/mcla_init.h"

#include "mcla_app.h"

#include <filesystem>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>

REXCVAR_DEFINE_STRING(mcla_gpu_backend, "any", "MCLA",
                      "Graphics backend for the Xenos GPU plugin: any, vulkan or d3d12");

// Native renderer stage 1: the frame contract census is written to this file
// (JSON Lines, one record per frame). Only meaningful with
// --gpu_plugin=mcla-native. Default: the user log directory's census file.
// Defined here because the cvar registry is process-wide; the mcla-native
// GPU plugin only reads it.
REXCVAR_DEFINE_STRING(mcla_native_census, "", "MCLA",
                      "Native renderer stage 1: write the frame contract census to this "
                      "file (JSON Lines). Empty disables the census");

// Stage 2.2 debug: when nonzero, dump every resolve destination and the
// presented image of frame N as PNGs into the app's writable directory.
REXCVAR_DEFINE_INT32(mcla_native_dump_frame, 0, "MCLA",
                   "Native renderer stage 2.2: dump all resolve targets and the "
                   "presented image of this frame number as PNGs. 0 disables");

// The runtime's rotating log must never land beside the executable: on Android
// that is /system/bin (read-only), and create_directories there aborts the
// game right after SDL_main starts. Set the log file explicitly so the SDK's
// exe_dir/logs fallback in rex_app.cpp never runs (the same approach as
// Pinyon Shift's OnConfigurePaths, which pins log_file to its state root).
void ConfigureWritableLogFile(const rex::PathConfig& paths) {
  if (!REXCVAR_GET(log_file).empty()) {
    return;
  }
  std::filesystem::path base = paths.user_data_root;
  if (base.empty()) {
    base = rex::filesystem::GetUserFolder() / "MclaRecomp";
  }
  std::error_code error;
  std::filesystem::create_directories(base / "logs", error);
  if (error) {
    // Nothing writable found; leave the SDK default rather than crash here.
    return;
  }
  REXCVAR_SET(log_file, (base / "logs" / "runtime.log").string());
}


std::unique_ptr<rex::system::IAudioSystem> CreateMclaAudioSystem(
    rex::runtime::FunctionDispatcher* function_dispatcher);

// Started with no options (a double-click on mcla.exe), the game should just
// run: find the extracted game next to the executable or, as the build leaves
// it, in the repository a few folders up, and load the Xenos GPU plugin.
void MclaApp::OnConfigurePaths(rex::PathConfig& paths) {
  if (!paths.game_data_root.empty()) {
    return;
  }

  // 1. Check custom path passed via environment variable (set by Android launcher)
  const char* env_dir = std::getenv("MCLA_GAME_DIR");
  if (env_dir && *env_dir) {
    std::error_code error;
    std::filesystem::path p(env_dir);
    if (std::filesystem::exists(p / "default.xex", error) || std::filesystem::exists(p, error)) {
      paths.game_data_root = p;
      return;
    }
  }

  // 2. Check Android XDG data directory paths
  const char* xdg = std::getenv("XDG_DATA_HOME");
  if (xdg && *xdg) {
    std::error_code error;
    std::filesystem::path xdg_base(xdg);
    if (std::filesystem::exists(xdg_base / "MclaRecomp" / "game" / "default.xex", error)) {
      paths.game_data_root = xdg_base / "MclaRecomp" / "game";
      return;
    }
    if (std::filesystem::exists(xdg_base / "game" / "default.xex", error)) {
      paths.game_data_root = xdg_base / "game";
      return;
    }
  }

  // 3. Fallback to executable folder and parents
  std::filesystem::path folder = rex::filesystem::GetExecutableFolder();
  for (int level = 0; level < 5 && !folder.empty(); ++level) {
    std::error_code error;
    if (std::filesystem::exists(folder / "MclaRecomp" / "game" / "default.xex", error)) {
      paths.game_data_root = folder / "MclaRecomp" / "game";
      return;
    }
    if (std::filesystem::exists(folder / "game" / "default.xex", error)) {
      paths.game_data_root = folder / "game";
      return;
    }
    if (folder == folder.parent_path()) {
      break;
    }
    folder = folder.parent_path();
  }

  // The log file is pinned here, after the game root was resolved, so every
  // platform (Android included) writes it under a directory the process can
  // create files in. user_data_root defaults to GetUserFolder()/GetName()
  // in the SDK (XDG_DATA_HOME, which the Android launcher sets to its
  // external files dir).
  ConfigureWritableLogFile(paths);
}

void MclaApp::OnPreSetup(rex::RuntimeConfig& config) {
  config.audio_factory = &CreateMclaAudioSystem;
  if (config.gpu_plugin.empty()) {
    config.gpu_plugin = "xenos";
  }

  // The census runs in the native plugin; it reads the path itself through
  // the cvar, but default it under the log directory so a run without flags
  // still records somewhere writable.
  if (config.gpu_plugin == "mcla-native" || config.gpu_plugin == "mcla-dump") {
    std::string census = REXCVAR_GET(mcla_native_census);
    if (census.empty()) {
      std::filesystem::path base =
          REXCVAR_GET(log_file).empty()
              ? rex::filesystem::GetUserFolder() / "MclaRecomp" / "logs"
              : std::filesystem::path(REXCVAR_GET(log_file)).parent_path();
      census = (base / "mcla_frame_census.jsonl").string();
    }
    REXCVAR_SET(mcla_native_census, census);
    REXLOG_INFO("mcla_native: census enabled, output {}", census);
  }

  // Left to the SDK when "any"; it then loads the plugin named by --gpu_plugin
  // with its own default backend.
  std::string backend = REXCVAR_GET(mcla_gpu_backend);
  if (backend != "any") {
    config.graphics = rex::system::LoadGpuPlugin("xenos", backend);
    if (!config.graphics) {
      REXLOG_ERROR("could not load the xenos GPU plugin with backend '{}'", backend);
    }
  }
}

void MclaApp::OnPostSetup() {
  ConfigureFrameTiming();


  // The game probes for loose city data on a "t:" drive that retail consoles
  // do not have. Give it a device so the probes fail with "not found" rather
  // than "no such device". LARecomp does the same.
  if (runtime() && runtime()->file_system()) {
    runtime()->file_system()->RegisterSymbolicLink("t:", "\\Device\\Harddisk0\\Partition1");
  }
}
