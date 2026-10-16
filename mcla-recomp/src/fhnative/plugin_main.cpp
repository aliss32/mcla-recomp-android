// rexgpu-mcla-fhnative plugin entry points.
//
// Stage 2: the fh2-recompiled native renderer carried into MCLA (src/nativo,
// guarded by MCLA_RECOMP). Selected with --gpu_plugin=mcla-fhnative.
//
// The renderer implements IGraphicsSystem directly (mcla_nativo_sistema.cpp,
// SistemaGraficoNativo via CrearSistemaGrafico): its own Vulkan provider,
// presenter, PM4 ring reader and shader library. This file only bridges the
// GPU plugin ABI to that factory.
#include "nativo/mcla_nativo_sistema.h"

#include <rex/logging.h>
#include <rex/system/gpu_plugin.h>

extern "C" REX_GPU_PLUGIN_EXPORT uint32_t rex_gpu_abi_version(void) {
  return rex::system::kGpuPluginAbiVersion;
}

extern "C" REX_GPU_PLUGIN_EXPORT rex::system::IGraphicsSystem *
rex_gpu_create(uint32_t abi_version, const rex::system::GpuCreateInfo *info) {
  if (abi_version != rex::system::kGpuPluginAbiVersion) {
    REXLOG_ERROR(
        "rexgpu-mcla-fhnative: host requested ABI {}, plugin is ABI {}",
        abi_version, rex::system::kGpuPluginAbiVersion);
    return nullptr;
  }
  if (info && info->backend && std::string_view(info->backend) != "any" &&
      std::string_view(info->backend) != "vulkan") {
    REXLOG_ERROR(
        "rexgpu-mcla-fhnative: backend '{}' not supported (vulkan only)",
        info->backend);
    return nullptr;
  }
  REXLOG_INFO("rexgpu-mcla-fhnative: native renderer plugin created");
  return mcla::nativo::CrearSistemaGrafico().release();
}
