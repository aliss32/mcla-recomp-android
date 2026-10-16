// MCLA native renderer, stage 1: census command processor.
//
// Derives from the SDK's Xenos CommandProcessor base, which decodes the PM4
// ring and maintains the register file, and records what each frame would
// draw instead of drawing it. Every draw, resolve, clear and swap is either
// counted in the frame census or, when something unexpected appears that the
// census cannot classify, logged explicitly. Nothing is dropped silently.
//
// The base class handles the whole guest protocol (fences, interrupts,
// WAIT_REG_MEM, ME_INIT, read-pointer writeback), so the guest runs at its
// normal pace while the census is recorded.

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "frame_census.h"

// Vulkan types for the present-test submission come from the SDK's
// rex/ui/vulkan/api.h chain (it defines VK_NO_PROTOTYPES and
// VK_ENABLE_BETA_EXTENSIONS BEFORE including <vulkan/vulkan.h>, and
// vulkan.h:94 gates vulkan_beta.h on that macro). Included first, before
// any other Vulkan-touching header, so every later definition sees the
// same consistent set.
#include <rex/ui/vulkan/api.h>

#include "frame_dump.h"
#include "present_pass.h"
#include "render_target_manager.h"

#include <rex/graphics/command_processor.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/xenos.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

// (clangd diagnostics in the IDE are expected: it has no SDK include path;
// the CMake build compiles this cleanly)
namespace rex::graphics::mcla_native {

class MclaNativeGraphicsSystem;

class MclaNativeCommandProcessor : public CommandProcessor {
public:
  MclaNativeCommandProcessor(MclaNativeGraphicsSystem *graphics_system,
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
  // Stage 2.2: render target/resolve texture management and the present pass.
  bool EnsurePresentResources();
  void DestroyPresentResources();

  // Ensures RTs exist and applies the current copy packet's clears and
  // format-preserving resolves (conversion-required ones counted + logged).
  void ApplyCopy(uint32_t &clears, uint32_t &applied, uint32_t &skipped_conv);

  // The pending resolve recorded by ApplyCopy (one per swap at most in 2.2),
  // drained into the swap submission.
  VkImage pending_resolve_src_image_ = VK_NULL_HANDLE;
  VkImage pending_resolve_dst_image_ = VK_NULL_HANDLE;
  uint32_t pending_resolve_src_msaa_ = 0;
  uint32_t pending_resolve_width_ = 0;
  uint32_t pending_resolve_height_ = 0;
  bool pending_resolve_src_is_depth_ = false;

  VkCommandPool present_command_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer present_command_buffer_ = VK_NULL_HANDLE;
  VkFence present_fence_ = VK_NULL_HANDLE;
  bool present_resources_ready_ = false;
  uint64_t present_fail_count_ = 0;

  // The resolve texture matching the last frame's frontbuffer resolve, by
  // dest_base == swap_ptr (census-verified correspondence).
  uint32_t last_swap_dest_base_ = 0;
  uint32_t present_without_texture_count_ = 0;

  // Per-frame copy/resolve counters reported in the stage2.2 log line; reset
  // in ApplyCopy at the first copy of a frame.
  uint32_t last_frame_resolves_ = 0;
  uint32_t last_frame_applied_ = 0;
  uint32_t last_frame_skipped_conv_ = 0;
  uint32_t last_frame_clears_ = 0;
  bool frame_copies_started_ = false;

  // Own frame counter for the color cadence and log throttling (the census
  // keeps its own bookkeeping; this counts presented swaps).
  uint64_t census_frame_count_ = 0;

  // Microcode hashes of the current pair, updated by LoadShader. The base
  // class owns the Shader objects; the census only needs their identities.
  // Deliberately NOT reset at the swap boundary: the guest re-uploads its
  // microcode only when it changes, so later frames reuse the pair loaded
  // for the first without a new LoadShader call.
  uint64_t current_vs_hash_ = 0;
  uint64_t current_ps_hash_ = 0;

  FrameCensusWriter census_;

  // Stage 2.2 render target/resolve texture registry (fixed pools, no
  // per-frame heap allocation).
  RenderTargetManager rt_manager_;

  // Stage 2.2 fullscreen sampling pass for the swap.
  PresentPass present_pass_;

  // Stage 2.2 debug dump (--mcla_native_dump_frame).
  FrameDump frame_dump_;
  bool frame_dump_ready_ = false;
  bool dump_frame_enabled_ = false;
  uint64_t dump_frame_number_ = 0;
};

} // namespace rex::graphics::mcla_native
