// rexgpu-mcla-native plugin entry points.
//
// Stage 1 (census): consumes the PM4 stream, records the frame contract,
// presents blank frames. Selected with --gpu_plugin=mcla-native.
#include <string_view>

#include "native/graphics_system.h"

#include <rex/logging.h>
#include <rex/system/gpu_plugin.h>

extern "C" REX_GPU_PLUGIN_EXPORT uint32_t rex_gpu_abi_version(void) {
  return rex::system::kGpuPluginAbiVersion;
}

extern "C" REX_GPU_PLUGIN_EXPORT rex::system::IGraphicsSystem *
rex_gpu_create(uint32_t abi_version, const rex::system::GpuCreateInfo *info) {
  if (abi_version != rex::system::kGpuPluginAbiVersion) {
    REXLOG_ERROR("rexgpu-mcla-native: host requested ABI {}, plugin is ABI {}",
                 abi_version, rex::system::kGpuPluginAbiVersion);
    return nullptr;
  }
  if (!info || info->struct_size < sizeof(rex::system::GpuCreateInfo)) {
    REXLOG_ERROR("rexgpu-mcla-native: invalid GpuCreateInfo");
    return nullptr;
  }
  std::string_view backend = info->backend ? info->backend : "any";
  if (backend != "any" && backend != "vulkan") {
    REXLOG_ERROR("rexgpu-mcla-native: backend '{}' not supported (vulkan only)",
                 backend);
    return nullptr;
  }
  REXLOG_INFO("rexgpu-mcla-native: census stage 1 plugin created (backend {})",
              backend);
  return new rex::graphics::mcla_native::MclaNativeGraphicsSystem();
}
