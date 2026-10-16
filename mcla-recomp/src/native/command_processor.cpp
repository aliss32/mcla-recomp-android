// MCLA native renderer, stage 1: census command processor implementation.
// See command_processor.h.
#include "command_processor.h"

#include <array>
#include <cmath>
#include <fmt/format.h>
#include <xxhash.h>

#include <rex/cvar.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/system/xvideo.h>
#include <rex/ui/vulkan/presenter.h>
#include <rex/ui/vulkan/util.h>

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
    vulkan::VulkanGraphicsSystem *graphics_system,
    system::KernelState *kernel_state)
    : vulkan::VulkanCommandProcessor(graphics_system, kernel_state) {}

MclaNativeCommandProcessor::~MclaNativeCommandProcessor() = default;

bool MclaNativeCommandProcessor::SetupContext() {
  // The SDK's Vulkan setup (shared memory, caches, submission machinery) is
  // inherited first.
  if (!vulkan::VulkanCommandProcessor::SetupContext()) {
    return false;
  }
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
  // No translated shader storage of our own yet; the base caches its own
  // translated shaders.
  vulkan::VulkanCommandProcessor::InitializeShaderStorage(cache_root, title_id,
                                                          blocking);
}

void MclaNativeCommandProcessor::ClearCaches() {
  vulkan::VulkanCommandProcessor::ClearCaches();
}

void MclaNativeCommandProcessor::InvalidateGpuMemory() {
  vulkan::VulkanCommandProcessor::InvalidateGpuMemory();
}

void MclaNativeCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  vulkan::VulkanCommandProcessor::WriteRegister(index, value);
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
  // and track the identity of the active pair. The base (Vulkan) pipeline
  // cache still owns the real Shader object and translation.
  uint64_t hash =
      XXH3_64bits(host_address, size_t(dword_count) * sizeof(uint32_t));
  if (shader_type == xenos::ShaderType::kVertex) {
    current_vs_hash_ = hash;
  } else if (shader_type == xenos::ShaderType::kPixel) {
    current_ps_hash_ = hash;
  }
  // Let the base class load and translate the shader normally; the census
  // tracks the pair by hash, the base tracks the object itself.
  return vulkan::VulkanCommandProcessor::LoadShader(shader_type, guest_address,
                                                    host_address, dword_count);
}

bool MclaNativeCommandProcessor::IssueDraw(PrimitiveType prim_type,
                                           uint32_t index_count,
                                           IndexBufferInfo *index_buffer_info,
                                           bool major_mode_explicit) {
  (void)major_mode_explicit;
  // kCopy draws are resolves/clears drawn as primitives; the base
  // CommandProcessor does not route them to IssueCopy, so the backend does
  // (the Xenos plugin does the same).
  if (register_file_->Get<reg::RB_MODECONTROL>().edram_mode ==
      xenos::EdramMode::kCopy) {
    return IssueCopy();
  }
  // One census call per draw, sampling the live register state at draw
  // time (the color/depth info registers are written after
  // RB_SURFACE_INFO, so a write-time record would go stale). The write
  // mask comes from RB_COLOR_MASK: which targets this draw actually
  // writes, so unused targets with a format of 0 are not confused with a
  // real 8_8_8_8 target.
  const auto &surface = register_file_->Get<reg::RB_SURFACE_INFO>();
  const auto &depth = register_file_->Get<reg::RB_DEPTH_INFO>();
  reg::RB_COLOR_INFO color[xenos::kMaxColorRenderTargets];
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    color[i].value =
        (*register_file_)[reg::RB_COLOR_INFO::rt_register_indices[i]];
  }
  const uint32_t color_write_mask =
      register_file_->Get<reg::RB_COLOR_MASK>().value & 0xFFFFu;
  const auto depth_control = register_file_->Get<reg::RB_DEPTHCONTROL>();
  const bool depth_stencil_enabled =
      depth_control.z_enable != 0 || depth_control.stencil_enable != 0;
  census_.RecordDraw(
      surface, color, depth,
      uint32_t(register_file_->Get<reg::RB_MODECONTROL>().edram_mode),
      color_write_mask, depth_stencil_enabled, prim_type, current_vs_hash_,
      current_ps_hash_, index_count, index_buffer_info != nullptr);
  // Stage 1 draws nothing; the census replaces the rasterized output.
  return true;
}

bool MclaNativeCommandProcessor::IssueCopy() {
  // IssueCopy covers both resolves (copies) and clears, per RB_COPY_CONTROL.
  // Census recording is unchanged from stage 1 (the oracle keeps running).
  const auto &copy_control = register_file_->Get<reg::RB_COPY_CONTROL>();
  const bool copying_depth =
      copy_control.copy_src_select >= xenos::kMaxColorRenderTargets;
  if (copy_control.depth_clear_enable) {
    census_.RecordClear(
        true, uint32_t(register_file_->Get<reg::RB_DEPTH_INFO>().depth_format));
  }
  if (copy_control.color_clear_enable) {
    const uint32_t rt = copying_depth ? 0 : copy_control.copy_src_select;
    census_.RecordClear(
        false,
        UnpackColorFormat(
            (*register_file_)[reg::RB_COLOR_INFO::rt_register_indices[rt]]));
  }
  if (copy_control.copy_command != xenos::CopyCommand::kNull) {
    ResolveInfo info;
    info.depth = copying_depth;
    info.src_select = copy_control.copy_src_select;
    if (copying_depth) {
      reg::RB_DEPTH_INFO depth_info;
      depth_info.value = (*register_file_)[XE_GPU_REG_RB_DEPTH_INFO];
      info.format = uint32_t(depth_info.depth_format);
      info.src_base =
          depth_info.depth_base | (depth_info.depth_base_bit_11 << 11);
    } else {
      const uint32_t raw =
          (*register_file_)[reg::RB_COLOR_INFO::rt_register_indices
                                [copy_control.copy_src_select]];
      info.format = UnpackColorFormat(raw);
      info.src_base = UnpackColorBase(raw);
    }
    info.dest_base = (*register_file_)[XE_GPU_REG_RB_COPY_DEST_BASE];
    reg::RB_COPY_DEST_PITCH dest_pitch;
    dest_pitch.value = (*register_file_)[XE_GPU_REG_RB_COPY_DEST_PITCH];
    info.dest_pitch = dest_pitch.copy_dest_pitch;
    info.dest_height = dest_pitch.copy_dest_height;
    reg::RB_COPY_DEST_INFO dest_info;
    dest_info.value = (*register_file_)[XE_GPU_REG_RB_COPY_DEST_INFO];
    info.dest_format = uint32_t(dest_info.copy_dest_format);
    info.dest_endian = uint32_t(dest_info.copy_dest_endian);
    info.dest_swap = uint32_t(dest_info.copy_dest_swap);
    info.dest_number = uint32_t(dest_info.copy_dest_number);
    census_.RecordResolve(info);
  } else if (!copy_control.color_clear_enable &&
             !copy_control.depth_clear_enable) {
    census_.RecordSkipped(fmt::format("copy_noop;cmd {};src {}",
                                      uint32_t(copy_control.copy_command),
                                      uint32_t(copy_control.copy_src_select)));
  }
  // Stage 2.1 has no render targets yet; copies/clears produce nothing to
  // present, and the base's shared-memory resolve machinery is NOT invoked
  // (our stage 2.2 host-texture model replaces it).
  return true;
}

void MclaNativeCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                           uint32_t frontbuffer_width,
                                           uint32_t frontbuffer_height) {
  // The frontbuffer address is the core datum of the wrong-region analysis:
  // compare it with the frame's last resolve "dest 0x..." key.
  census_.RecordSwap(frontbuffer_width, frontbuffer_height, frontbuffer_ptr);

  // Stage 2.1 presenter test: present a solid color cycling by frame number
  // (N % 3 -> red/green/blue) so the presenter connection is verifiable on
  // device with zero real rendering. The path follows the Xenos plugin's
  // IssueSwap (vulkan/command_processor.cpp:2298+): BeginSubmission ->
  // RefreshGuestOutput callback -> EndSubmission(true). The guest output
  // image has NO TRANSFER_DST usage (vulkan_presenter.cpp:1596-1597: only
  // TRANSFER_SRC | SAMPLED | COLOR_ATTACHMENT | STORAGE), so the color is
  // written with vkCmdBeginRendering + a clear load op, not
  // vkCmdClearColorImage. Layout contract (presenter.h:71-83): acquire in
  // UNDEFINED/SHADER_READ_ONLY_OPTIMAL, release in SHADER_READ_ONLY_OPTIMAL.
  const uint64_t frame = GetCurrentFrame();
  static constexpr float kColorTable[3][4] = {{1.0f, 0.0f, 0.0f, 1.0f},
                                              {0.0f, 1.0f, 0.0f, 1.0f},
                                              {0.0f, 0.0f, 1.0f, 1.0f}};
  const float *color = kColorTable[frame % 3];

  ui::Presenter *presenter = graphics_system_->presenter();
  if (!presenter) {
    REXLOG_ERROR("stage2.1 frame {}: no presenter", uint64_t(frame));
  } else {
    const uint32_t width = frontbuffer_width ? frontbuffer_width : 1280;
    const uint32_t height = frontbuffer_height ? frontbuffer_height : 720;
    system::X_VIDEO_MODE video_mode;
    kernel::xboxkrnl::VdQueryVideoMode(&video_mode);
    presenter->RefreshGuestOutput(
        width, height,
        std::max(uint32_t(1), uint32_t(video_mode.display_width)),
        std::max(uint32_t(1), uint32_t(video_mode.display_height)),
        [this, width, height,
         color](ui::Presenter::GuestOutputRefreshContext &context) -> bool {
          auto &vulkan_context = static_cast<
              ui::vulkan::VulkanPresenter::VulkanGuestOutputRefreshContext &>(
              context);

          if (!vulkan::VulkanCommandProcessor::BeginSubmission(true)) {
            return false;
          }
          // In case the swap command is the only one in the frame.
          VkImageSubresourceRange subresource_range =
              ui::vulkan::util::InitializeSubresourceRange();

          // Acquire the guest output image for a color attachment write.
          // UNDEFINED old layout is safe in both cases: the image is fully
          // overwritten by the clear.
          PushImageMemoryBarrier(
              vulkan_context.image(), subresource_range,
              vulkan_context.image_ever_written_previously()
                  ? ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask
                  : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              vulkan_context.image_ever_written_previously()
                  ? ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask
                  : 0,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
          SubmitBarriers(true);

          VkRenderingAttachmentInfo attachment_info = {};
          attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
          attachment_info.imageView = vulkan_context.image_view();
          attachment_info.imageLayout =
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
          attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
          attachment_info.clearValue.color = {color[0], color[1], color[2],
                                              color[3]};

          VkRenderingInfo rendering_info = {};
          rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
          rendering_info.renderArea.offset = {0, 0};
          rendering_info.renderArea.extent = {width, height};
          rendering_info.layerCount = 1;
          rendering_info.colorAttachmentCount = 1;
          rendering_info.pColorAttachments = &attachment_info;

          deferred_command_buffer().CmdVkBeginRendering(&rendering_info);
          deferred_command_buffer().CmdVkEndRendering();

          // Release the image back to the presenter's layout.
          PushImageMemoryBarrier(
              vulkan_context.image(), subresource_range,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout);

          // The commands must be submitted before the image goes back to the
          // presenter (vulkan/command_processor.cpp:2946-2950).
          return vulkan::VulkanCommandProcessor::EndSubmission(true);
        });
  }

  const bool log_frame = frame <= 10 || (frame % 60) == 0;
  if (log_frame) {
    REXLOG_INFO("stage2.1 frame {} color=({},{},{},{})", uint64_t(frame),
                color[0], color[1], color[2], color[3]);
  }
  vulkan::VulkanCommandProcessor::EndSubmission(true);

  census_.EndFrame();
  census_.BeginFrame();
  // The shader pair is deliberately NOT reset: the guest only re-uploads
  // its microcode when it changes, so a second frame may reuse the pair
  // loaded in the first without a new LoadShader call. A reset here would
  // record an all-zero pair that matches no real shader.
}

} // namespace rex::graphics::mcla_native
