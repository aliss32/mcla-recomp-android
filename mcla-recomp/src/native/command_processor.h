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
