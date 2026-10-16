// rexgpu-mcla-dump plugin entry points.
//
// Stage 0 (dump): same presenter/protocol serving as the census plugin, plus a
// memory scanner that persists every 0x102A shader container it can find in the
// game's committed memory into out/contenedores. Runs until the game reaches
// the main menu (plenty of D3D-created shaders by then), then stops.
#include <string_view>

#include "dump/dump_graphics_system.h"

#include <rex/logging.h>
#include <rex/system/gpu_plugin.h>

extern "C" REX_GPU_PLUGIN_EXPORT uint32_t rex_gpu_abi_version(void) {
  return rex::system::kGpuPluginAbiVersion;
}

extern "C" REX_GPU_PLUGIN_EXPORT rex::system::IGraphicsSystem *
rex_gpu_create(uint32_t abi_version, const rex::system::GpuCreateInfo *info) {
  if (abi_version != rex::system::kGpuPluginAbiVersion) {
    REXLOG_ERROR("rexgpu-mcla-dump: host requested ABI {}, plugin is ABI {}",
                 abi_version, rex::system::kGpuPluginAbiVersion);
    return nullptr;
  }
  if (!info || info->struct_size < sizeof(rex::system::GpuCreateInfo)) {
    REXLOG_ERROR("rexgpu-mcla-dump: invalid GpuCreateInfo");
    return nullptr;
  }
  REXLOG_INFO("rexgpu-mcla-dump: container dump plugin created");
  return new rex::graphics::mcla_dump::DumpGraphicsSystem();
}
