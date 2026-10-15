// MCLA native renderer, stage 1: census command processor implementation.
// See command_processor.h.
#include "command_processor.h"

#include <fmt/format.h>
#include <xxhash.h>

#include <rex/cvar.h>
#include <rex/graphics/util/draw.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/system/xmemory.h>

#include "graphics_system.h"

namespace rex::graphics::mcla_native {

// The census path cvar is defined by the host (src/mcla_app.cpp, "MCLA"
// category). The plugin is a separate shared library without the host's
// cvar storage on its link line, so the value is read through the cvar
// registry by name (REXCVAR_QUERY), which is the documented cross-module
// path.
// A missing cvar parses to an empty string, which leaves the census off
// (logged).
inline std::string CensusPath() {
  return ::rex::cvar::Query<std::string>("mcla_native_census");
}

using xenos::EdramMode;
using xenos::PrimitiveType;

namespace {

uint32_t UnpackColorFormat(uint32_t value) {
  reg::RB_COLOR_INFO info;
  info.value = value;
  return uint32_t(info.color_format);
}

uint32_t UnpackColorBase(uint32_t value) {
  reg::RB_COLOR_INFO info;
  info.value = value;
  return info.color_base | (info.color_base_bit_11 << 11);
}

} // namespace

MclaNativeCommandProcessor::MclaNativeCommandProcessor(
    MclaNativeGraphicsSystem *graphics_system,
    system::KernelState *kernel_state)
    : CommandProcessor(graphics_system, kernel_state) {}

MclaNativeCommandProcessor::~MclaNativeCommandProcessor() = default;

bool MclaNativeCommandProcessor::SetupContext() {
  // The host sets --mcla_native_census (default under the log dir) when the
  // plugin is selected; enable the census once here so the first frame is
  // also opened and recorded. An empty path leaves the census off - that is
  // logged, not silent.
  const std::string census_path = CensusPath();
  if (census_path.empty()) {
    REXLOG_WARN("mcla_native: census path is empty; the frame census is OFF "
                "(set --mcla_native_census=<file>)");
    return true;
  }
  census_.SetEnabled(true, std::filesystem::path(census_path));
  if (!census_.enabled()) {
    REXLOG_WARN("mcla_native: census could not open {}", census_path);
  } else {
    REXLOG_INFO("mcla_native: census recording to {}", census_path);
  }
  census_.BeginFrame();
  return true;
}

void MclaNativeCommandProcessor::ShutdownContext() { census_.EndFrame(); }

void MclaNativeCommandProcessor::InitializeShaderStorage(
    const std::filesystem::path &cache_root, uint32_t title_id, bool blocking) {
  // No translated shader storage in the census stage. Kept as a no-op with
  // the signature intact so a later stage can persist per-shader decisions.
  (void)cache_root;
  (void)title_id;
  (void)blocking;
}

void MclaNativeCommandProcessor::ClearCaches() {}

void MclaNativeCommandProcessor::InvalidateGpuMemory() {}

void MclaNativeCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  CommandProcessor::WriteRegister(index, value);
  // Surface config is NOT recorded here: color/depth info registers are
  // typically written AFTER RB_SURFACE_INFO, so a record at this point
  // would carry the previous configuration's formats and depth_base -
  // exactly the data the EDRAM analysis needs. Instead every IssueDraw
  // samples the live register state (the "config with draw count" reading).
  (void)index;
  (void)value;
}

Shader *MclaNativeCommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                               uint32_t guest_address,
                                               const uint32_t *host_address,
                                               uint32_t dword_count) {
  // Hash the host-side (byte-swapped) microcode like the pipeline caches do,
  // and track the identity of the active pair. No translation is performed.
  uint64_t hash =
      XXH3_64bits(host_address, size_t(dword_count) * sizeof(uint32_t));
  if (shader_type == xenos::ShaderType::kVertex) {
    current_vs_hash_ = hash;
  } else if (shader_type == xenos::ShaderType::kPixel) {
    current_ps_hash_ = hash;
  }
  // The base class stores whatever LoadShader returns; the census tracks the
  // active pair by hash instead, so return null (the same contract as a
  // failed translation - every consumer in the base checks for null).
  return nullptr;
}

FrameCensusWriter::DrawSample MclaNativeCommandProcessor::SampleFromRegisters(
    const RegisterFile &regs, xenos::PrimitiveType prim_type,
    uint32_t index_count, bool indexed, uint64_t vs_hash, uint64_t ps_hash) {
  FrameCensusWriter::DrawSample sample{};
  sample.prim_type = uint32_t(prim_type);
  sample.index_count = index_count;
  sample.indexed = indexed;
  sample.vertex_shader_hash = uint32_t(vs_hash);
  sample.pixel_shader_hash = uint32_t(ps_hash);
  const auto &surface = regs.Get<reg::RB_SURFACE_INFO>();
  sample.surface_pitch = surface.surface_pitch;
  sample.msaa = uint32_t(surface.msaa_samples);
  sample.edram_mode = uint32_t(regs.Get<reg::RB_MODECONTROL>().edram_mode);
  const auto &depth = regs.Get<reg::RB_DEPTH_INFO>();
  sample.depth_format = uint32_t(depth.depth_format);
  sample.depth_base = depth.depth_base | (depth.depth_base_bit_11 << 11);
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    uint32_t value = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[i]);
    sample.color_formats[i] = UnpackColorFormat(value);
    sample.color_bases[i] = UnpackColorBase(value);
    sample.blend_control[i] = regs.Get<reg::RB_BLENDCONTROL>(
        reg::RB_BLENDCONTROL::rt_register_indices[i]);
  }
  return sample;
}

bool MclaNativeCommandProcessor::IssueDraw(PrimitiveType prim_type,
                                           uint32_t index_count,
                                           IndexBufferInfo *index_buffer_info,
                                           bool major_mode_explicit) {
  (void)major_mode_explicit;
  // Record the surface configuration with the live register state at draw
  // time: the key counts "draws made into this configuration", which is the
  // reading the EDRAM/resolve analysis needs (the color/depth info registers
  // are written after RB_SURFACE_INFO, so a write-time record would go stale).
  const auto &surface = register_file_->Get<reg::RB_SURFACE_INFO>();
  const auto &depth = register_file_->Get<reg::RB_DEPTH_INFO>();
  reg::RB_COLOR_INFO color[xenos::kMaxColorRenderTargets];
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    color[i].value = register_file_->Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[i]);
  }
  census_.RecordSurfaceConfig(
      surface, color, depth,
      uint32_t(register_file_->Get<reg::RB_MODECONTROL>().edram_mode));
  census_.RecordPrimitive(prim_type);
  census_.RecordShaderPair(current_vs_hash_, current_ps_hash_);
  census_.RecordDrawSample(SampleFromRegisters(
      *register_file_, prim_type, index_count, index_buffer_info != nullptr,
      current_vs_hash_, current_ps_hash_));
  // Stage 1 draws nothing; the census replaces the rasterized output.
  return true;
}

bool MclaNativeCommandProcessor::IssueCopy() {
  // IssueCopy covers both resolves (copies) and clears, per RB_COPY_CONTROL.
  // Clears and the copy are independent: clear only, copy only and copy with
  // clear are all legal in one packet. copy_src_select == 0 is a real RT0
  // resolve (the most common one) - never treated as a no-op.
  const auto &copy_control = register_file_->Get<reg::RB_COPY_CONTROL>();
  const bool copying_depth =
      copy_control.copy_src_select >= xenos::kMaxColorRenderTargets;
  if (copy_control.depth_clear_enable) {
    census_.RecordClear(
        true, uint32_t(register_file_->Get<reg::RB_DEPTH_INFO>().depth_format));
  }
  if (copy_control.color_clear_enable) {
    // The color clear covers the copy source's target (the copied region);
    // target 0 when the copy source is depth.
    const uint32_t rt = copying_depth ? 0 : copy_control.copy_src_select;
    census_.RecordClear(
        false, UnpackColorFormat(register_file_->Get<reg::RB_COLOR_INFO>(
                   reg::RB_COLOR_INFO::rt_register_indices[rt])));
  }
  if (copy_control.copy_command != xenos::CopyCommand::kNull) {
    uint32_t format;
    if (copying_depth) {
      format = uint32_t(register_file_->Get<reg::RB_DEPTH_INFO>().depth_format);
    } else {
      format = UnpackColorFormat(register_file_->Get<reg::RB_COLOR_INFO>(
          reg::RB_COLOR_INFO::rt_register_indices[copy_control
                                                      .copy_src_select]));
    }
    census_.RecordResolve("copy", copying_depth, format);
  } else if (!copy_control.color_clear_enable &&
             !copy_control.depth_clear_enable) {
    census_.RecordSkipped("copy_noop");
  }
  return true;
}

void MclaNativeCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                           uint32_t frontbuffer_width,
                                           uint32_t frontbuffer_height) {
  (void)frontbuffer_ptr;
  census_.RecordSwap(frontbuffer_width, frontbuffer_height);
  census_.EndFrame();
  census_.BeginFrame();
  // The shader pair is deliberately NOT reset: the guest only re-uploads
  // its microcode when it changes, so a second frame may reuse the pair
  // loaded in the first without a new LoadShader call. A reset here would
  // record an all-zero pair that matches no real shader.
}

} // namespace rex::graphics::mcla_native
