// MCLA native renderer, stage 2.1: presenter test command processor.
//
// Derives from the SDK's VulkanCommandProcessor: the full Vulkan submission
// machinery (fences, queues, deferred command buffer, caches) is inherited,
// and IssueSwap presents a solid color to the presenter's guest-output image
// so the presenter connection itself can be verified on device before any
// real rendering exists. Draws are census-only (as in stage 1): the base
// IssueDraw is not used yet.
//
// Layout contract (include/rex/ui/vulkan/presenter.h:71-83): the guest
// output image is VK_FORMAT_A2B10G10R10_UNORM_PACK32; it is acquired in
// SHADER_READ_ONLY_OPTIMAL (or UNDEFINED on first write) and must be
// released in SHADER_READ_ONLY_OPTIMAL. The image supports color-attachment
// and sampled usage (vulkan_presenter.cpp:1596-1597 - no TRANSFER_DST), so
// the clear is done with a render pass + vkCmdClearAttachments, not
// vkCmdClearColorImage.

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "frame_census.h"

#include <rex/graphics/registers.h>
#include <rex/graphics/vulkan/command_processor.h>
#include <rex/graphics/vulkan/graphics_system.h>
#include <rex/graphics/xenos.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

// (clangd diagnostics in the IDE are expected: it has no SDK include path;
// the CMake build compiles this cleanly)
namespace rex::graphics::mcla_native {

class MclaNativeGraphicsSystem;

class MclaNativeCommandProcessor : public vulkan::VulkanCommandProcessor {
public:
  MclaNativeCommandProcessor(vulkan::VulkanGraphicsSystem *graphics_system,
                             system::KernelState *kernel_state);
  ~MclaNativeCommandProcessor() override;

  void InitializeShaderStorage(const std::filesystem::path &cache_root,
                               uint32_t title_id, bool blocking) override;

  void ClearCaches() override;
  void InvalidateGpuMemory() override;

  FrameCensusWriter &census() { return census_; }

protected:
  bool SetupContext() override;
  void ShutdownContext() override;

  void WriteRegister(uint32_t index, uint32_t value) override;

  Shader *LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                     const uint32_t *host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(xenos::PrimitiveType prim_type, uint32_t index_count,
                 IndexBufferInfo *index_buffer_info,
                 bool major_mode_explicit) override;
  bool IssueCopy() override;
  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

private:
  // Microcode hashes of the current pair, updated by LoadShader. The base
  // class owns the Shader objects; the census only needs their identities.
  // Deliberately NOT reset at the swap boundary: the guest re-uploads its
  // microcode only when it changes, so later frames reuse the pair loaded
  // for the first without a new LoadShader call.
  uint64_t current_vs_hash_ = 0;
  uint64_t current_ps_hash_ = 0;

  FrameCensusWriter census_;
};

} // namespace rex::graphics::mcla_native
