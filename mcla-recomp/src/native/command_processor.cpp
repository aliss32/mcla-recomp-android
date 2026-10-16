// MCLA native renderer: census command processor + stage 2.1 presenter test.
// See command_processor.h.
#include "command_processor.h"

#include <fmt/format.h>
#include <xxhash.h>

#include <rex/cvar.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/system/xvideo.h>
#include <rex/ui/presenter.h>
#include <rex/ui/vulkan/presenter.h>
#include <rex/ui/vulkan/provider.h>
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
  // Stage 2.2: host RT/resolve texture manager (provider device). Without a
  // provider (headless) it stays uninitialized and ApplyCopy no-ops.
  if (graphics_system_->provider()) {
    rt_manager_.Initialize(static_cast<const ui::vulkan::VulkanProvider *>(
                               graphics_system_->provider())
                               ->vulkan_device());
  }
  // Stage 2.2 debug dump frame number (host cvar, read by name cross-module).
  dump_frame_number_ =
      uint64_t(::rex::cvar::Query<int32_t>("mcla_native_dump_frame"));
  dump_frame_enabled_ = dump_frame_number_ != 0;
  return true;
}

void MclaNativeCommandProcessor::ShutdownContext() {
  census_.EndFrame();
  DestroyPresentResources();
  rt_manager_.Shutdown();
  frame_dump_.Shutdown();
  frame_dump_ready_ = false;
}

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
        false,
        UnpackColorFormat(
            (*register_file_)[reg::RB_COLOR_INFO::rt_register_indices[rt]]));
  }
  if (copy_control.copy_command != xenos::CopyCommand::kNull) {
    // Raw register reads (Get<> vs union ambiguity avoided) for the full
    // resolve description: source target/format/base in EDRAM and the
    // guest destination region with its texture format fields.
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

  // Stage 2.2: apply the clears and the format-preserving resolves to host
  // render targets. Conversion-required resolves are counted and skipped
  // (logged), never silent. Census counters advance in parallel.
  ++last_frame_resolves_;
  ApplyCopy(last_frame_clears_, last_frame_applied_, last_frame_skipped_conv_);
  return true;
}
// Stage 2.2 clear/resolve application.
//
// draw_util::GetResolveInfo (include/rex/graphics/util/draw.h:584-587)
// produces the full copy description from the live register state: source
// EDRAM region (ResolveEdramInfo: base_tiles/pitch_tiles/msaa/format,
// draw.h:341-358), clear values (rb_depth_clear/rb_color_clear/
// rb_color_clear_lo, draw.h:511-513), the destination region
// (copy_dest_base/pitch/height via copy_dest_coordinate_info, draw.h:387-403)
// and the extent actually written (copy_dest_extent_start/length,
// draw.h:507-508). We register every non-conversion resolve destination as a
// host texture and issue vkCmdResolveImage (MSAA) or vkCmdCopyImage (1x) from
// the matching RT.
void MclaNativeCommandProcessor::ApplyCopy(uint32_t &clears, uint32_t &applied,
                                           uint32_t &skipped_conv) {
  clears = 0;
  applied = 0;
  skipped_conv = 0;
  if (!rt_manager_.initialized()) {
    return;
  }
  draw_util::ResolveInfo info;
  if (!draw_util::GetResolveInfo(*register_file_, *memory_, 1, 1, false, false,
                                 info)) {
    rt_manager_.LogOnce("resolve_info_invalid");
    return;
  }
  if (!info.coordinate_info.width_div_8 || !info.height_div_8) {
    return;
  }

  const uint32_t width_px = info.coordinate_info.width_div_8 * 8;
  const uint32_t height_px = info.height_div_8 * 8;

  // Clear accounting matches the census (depth clear, color clear, or both).
  if (info.IsClearingDepth())
    ++clears;
  if (info.IsClearingColor())
    ++clears;

  if (info.copy_dest_extent_length == 0) {
    return; // nothing to copy (clear-only packet)
  }

  const bool copying_depth = info.IsCopyingDepth();
  const draw_util::ResolveEdramInfo &src_edram =
      copying_depth ? info.depth_edram_info : info.color_edram_info;
  const uint32_t src_base =
      copying_depth ? info.depth_original_base : info.color_original_base;
  const uint32_t src_format = src_edram.format;

  reg::RB_COPY_DEST_INFO dest_info = info.copy_dest_info;
  const uint32_t dest_format = uint32_t(dest_info.copy_dest_format);
  const uint32_t swap_mode = uint32_t(dest_info.copy_dest_swap);
  const uint32_t dest_pitch =
      uint32_t(info.copy_dest_coordinate_info.pitch_aligned_div_32) * 32;

  // Depth clears/copies target color formats in the census; the guest
  // resolves depth to a color-format dest texture in those packets. For 2.2
  // we only implement color->color resolves; depth source conversion is
  // counted and skipped.
  if (copying_depth) {
    ++skipped_conv;
    if (rt_manager_.log_throttle()) {
      REXLOG_WARN("stage2.2: depth resolve skipped (src fmt {} dest fmt {})",
                  src_format, dest_format);
    }
    return;
  }

  // Format-preserving resolve only (census: 8_8_8_8 -> 8_8_8_8 is the
  // dominant case; 2_10_10_10_FLOAT -> 8_8_8_8 etc. need shader conversion).
  // ColorFormat numbers: 6 = k_8_8_8_8 (xenos.h:548).
  const bool same_format =
      (src_format == dest_format) ||
      (src_format == 0 &&
       dest_format == 6); // RT k_8_8 == dest ColorFormat k_8_8
  if (!same_format) {
    ++skipped_conv;
    if (rt_manager_.log_throttle()) {
      REXLOG_WARN("stage2.2: conversion resolve skipped (src fmt {} dest fmt "
                  "{} swap {})",
                  src_format, dest_format, swap_mode);
    }
    return;
  }

  RenderTargetManager::RenderTarget *src_rt =
      rt_manager_.FindRenderTarget(src_base);
  if (!src_rt) {
    // The RT is only known after draws have been applied to it (stage 2.3);
    // without a source RT the resolve produces nothing yet. Logged once per
    // base via the manager's throttle.
    rt_manager_.LogOnce("resolve_without_source_rt");
    return;
  }

  RenderTargetManager::ResolveTexture *dest_tex =
      rt_manager_.GetOrCreateResolveTexture(info.copy_dest_base, dest_pitch,
                                            height_px, dest_format, swap_mode);
  if (!dest_tex) {
    rt_manager_.LogOnce("resolve_dest_unavailable");
    return;
  }

  // Queue the resolve into the present command buffer (single submission per
  // swap). Layout: source RT TRANSFER_SRC_OPTIMAL, dest UNDEFINED ->
  // TRANSFER_DST_OPTIMAL, barriered at drain time in IssueSwap.
  pending_resolve_src_image_ = src_rt->image;
  pending_resolve_dst_image_ = dest_tex->image;
  pending_resolve_src_msaa_ = src_rt->msaa;
  pending_resolve_width_ = width_px;
  pending_resolve_height_ = height_px;
  pending_resolve_src_is_depth_ = src_rt->is_depth;
  ++applied;
}

// Stage 2.1 presenter test: a tiny standalone Vulkan submission path used
// only from IssueSwap's RefreshGuestOutput callback.
//
// Queue access (a): the SDK externalizes queue access with a per-queue
// recursive mutex - VulkanDevice::Queue (include/rex/ui/vulkan/device.h:
// 244-263) and AcquireQueue (device.h:277-280) return a Queue::Acquisition
// that holds the lock for the duration. The Xenos plugin
// (vulkan/command_processor.cpp:5451-5458) and the presenter
// (vulkan_presenter.cpp:1203-1205) both submit through
// vulkan_device->AcquireQueue(queue_family_graphics_compute(), 0); the same
// mechanism is used here - no raw vkQueueSubmit without the acquisition.
bool MclaNativeCommandProcessor::EnsurePresentResources() {
  if (present_resources_ready_) {
    return true;
  }
  const ui::vulkan::VulkanDevice *const vulkan_device =
      static_cast<const ui::vulkan::VulkanProvider *>(
          graphics_system_->provider())
          ->vulkan_device();
  const ui::vulkan::VulkanDevice::Functions &dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  VkCommandPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = vulkan_device->queue_family_graphics_compute();
  if (dfn.vkCreateCommandPool(device, &pool_info, nullptr,
                              &present_command_pool_) != VK_SUCCESS) {
    REXLOG_WARN("stage2.1: failed to create the command pool");
    return false;
  }
  VkCommandBufferAllocateInfo alloc_info = {};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = present_command_pool_;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = 1;
  if (dfn.vkAllocateCommandBuffers(device, &alloc_info,
                                   &present_command_buffer_) != VK_SUCCESS) {
    REXLOG_WARN("stage2.1: failed to allocate the command buffer");
    return false;
  }
  VkFenceCreateInfo fence_info = {};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  if (dfn.vkCreateFence(device, &fence_info, nullptr, &present_fence_) !=
      VK_SUCCESS) {
    REXLOG_WARN("stage2.1: failed to create the fence");
    return false;
  }
  present_resources_ready_ = true;
  return true;
}

void MclaNativeCommandProcessor::DestroyPresentResources() {
  if (!present_resources_ready_) {
    return;
  }
  const ui::vulkan::VulkanDevice *const vulkan_device =
      static_cast<const ui::vulkan::VulkanProvider *>(
          graphics_system_->provider())
          ->vulkan_device();
  const ui::vulkan::VulkanDevice::Functions &dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  dfn.vkDestroyFence(device, present_fence_, nullptr);
  dfn.vkDestroyCommandPool(device, present_command_pool_, nullptr);
  present_fence_ = VK_NULL_HANDLE;
  present_command_buffer_ = VK_NULL_HANDLE;
  present_command_pool_ = VK_NULL_HANDLE;
  present_resources_ready_ = false;
}

void MclaNativeCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                           uint32_t frontbuffer_width,
                                           uint32_t frontbuffer_height) {
  // The frontbuffer address is the core datum of the wrong-region analysis:
  // compare it with the frame's last resolve "dest 0x..." key.
  census_.RecordSwap(frontbuffer_width, frontbuffer_height, frontbuffer_ptr);

  // Stage 2.2: present the resolve texture that wrote the frame's frontbuffer
  // (dest_base == swap_ptr, census-verified in stage 1) through a fullscreen
  // sampling pass. A full pipeline (vertex + fragment SPIR-V) draws a triangle
  // covering the render area sampling the registered texture; without a
  // registered texture the frame presents black (logged, never a fallback to
  // the stage 2.1 color test).
  const uint64_t frame = census_frame_count_;
  RenderTargetManager::ResolveTexture *present_texture =
      rt_manager_.FindResolveTexture(frontbuffer_ptr);
  if (!present_texture) {
    last_swap_dest_base_ = frontbuffer_ptr;
    if (++present_without_texture_count_ <= 3 ||
        (present_without_texture_count_ % 600) == 0) {
      REXLOG_WARN(
          "stage2.2 frame {}: no resolve texture for swap_ptr 0x{:08X} - "
          "presenting black (count {})",
          frame, frontbuffer_ptr, uint64_t(present_without_texture_count_));
    }
  }

  ui::Presenter *presenter = graphics_system_->presenter();
  if (!presenter) {
    if (++present_fail_count_ <= 3 || (present_fail_count_ % 600) == 0) {
      REXLOG_WARN("stage2.2 frame {}: no presenter (count {})", frame,
                  uint64_t(present_fail_count_));
    }
  } else {
    const uint32_t width = frontbuffer_width ? frontbuffer_width : 1280;
    const uint32_t height = frontbuffer_height ? frontbuffer_height : 720;
    system::X_VIDEO_MODE video_mode;
    kernel::xboxkrnl::VdQueryVideoMode(&video_mode);
    const bool refreshed = presenter->RefreshGuestOutput(
        width, height,
        std::max(uint32_t(1), uint32_t(video_mode.display_width)),
        std::max(uint32_t(1), uint32_t(video_mode.display_height)),
        [this, width, height, present_texture](
            ui::Presenter::GuestOutputRefreshContext &context) -> bool {
          auto &vulkan_context = static_cast<
              ui::vulkan::VulkanPresenter::VulkanGuestOutputRefreshContext &>(
              context);
          const ui::vulkan::VulkanDevice *const vulkan_device =
              static_cast<const ui::vulkan::VulkanProvider *>(
                  graphics_system_->provider())
                  ->vulkan_device();
          const ui::vulkan::VulkanDevice::Functions &dfn =
              vulkan_device->functions();
          const VkDevice device = vulkan_device->device();

          if (!EnsurePresentResources()) {
            return false;
          }
          VkImageSubresourceRange subresource_range =
              ui::vulkan::util::InitializeSubresourceRange();

          // Acquire the guest output image for a color attachment write.
          VkImageMemoryBarrier acquire_barrier = {};
          acquire_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          acquire_barrier.srcAccessMask =
              vulkan_context.image_ever_written_previously()
                  ? ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask
                  : 0;
          acquire_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
          acquire_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
          acquire_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          acquire_barrier.image = vulkan_context.image();
          acquire_barrier.subresourceRange = subresource_range;

          // Release barrier back to the presenter's layout.
          VkImageMemoryBarrier release_barrier = {};
          release_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          release_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
          release_barrier.dstAccessMask =
              ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask;
          release_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          release_barrier.newLayout =
              ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout;
          release_barrier.image = vulkan_context.image();
          release_barrier.subresourceRange = subresource_range;

          VkCommandBufferBeginInfo begin_info = {};
          begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
          begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
          if (dfn.vkBeginCommandBuffer(present_command_buffer_, &begin_info) !=
              VK_SUCCESS) {
            REXLOG_WARN("stage2.2: vkBeginCommandBuffer failed");
            return false;
          }
          dfn.vkCmdPipelineBarrier(
              present_command_buffer_,
              vulkan_context.image_ever_written_previously()
                  ? ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask
                  : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
              nullptr, 1, &acquire_barrier);

          if (present_texture) {
            // Barrier the resolve texture to SHADER_READ_ONLY_OPTIMAL, draw
            // the fullscreen triangle sampling it, then leave it in that
            // layout (it will be resolved into again next frame - a
            // subsequent resolve barriers it back). For 2.2 the texture is
            // sampled with its unswizzled content: guest swap mode 1 is R/B
            // swap; the swap pass shader has an rb_swap specialization that
            // flips r/b. A transform image view does the swizzle when the
            // driver does not.
            VkImageMemoryBarrier tex_acquire = {};
            tex_acquire.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            tex_acquire.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            tex_acquire.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            tex_acquire.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            tex_acquire.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            tex_acquire.image = present_texture->image;
            tex_acquire.subresourceRange = subresource_range;
            dfn.vkCmdPipelineBarrier(present_command_buffer_,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &tex_acquire);

            const bool rb_swap_needed = present_texture->needs_rb_swap;
            VkImageView src_view = present_texture->view;
            present_pass_.Draw(vulkan_device, present_command_buffer_, src_view,
                               rb_swap_needed, width, height);
          } else {
            // No texture: clear to black. No fallback to the color test.
            VkRenderingAttachmentInfo attachment_info = {};
            attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            attachment_info.imageView = vulkan_context.image_view();
            attachment_info.imageLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment_info.clearValue.color = {0.0f, 0.0f, 0.0f, 1.0f};

            VkRenderingInfo rendering_info = {};
            rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering_info.renderArea.offset = {0, 0};
            rendering_info.renderArea.extent = {width, height};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &attachment_info;
            dfn.vkCmdBeginRendering(present_command_buffer_, &rendering_info);
            dfn.vkCmdEndRendering(present_command_buffer_);
          }

          // Drain the pending resolve into this submission, before the
          // present sampling so the presented image carries THIS frame's
          // resolved contents (census-verified contract: last resolve dest ==
          // swap_ptr, and the swap samples exactly that texture).
          if (pending_resolve_dst_image_ != VK_NULL_HANDLE) {
            VkImageMemoryBarrier dst_acquire = {};
            dst_acquire.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            dst_acquire.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            dst_acquire.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dst_acquire.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            dst_acquire.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dst_acquire.image = pending_resolve_dst_image_;
            dst_acquire.subresourceRange = subresource_range;
            VkImageMemoryBarrier src_acquire = {};
            src_acquire.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            src_acquire.srcAccessMask =
                pending_resolve_src_is_depth_
                    ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                    : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            src_acquire.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            src_acquire.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            src_acquire.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            src_acquire.image = pending_resolve_src_image_;
            src_acquire.subresourceRange.aspectMask =
                pending_resolve_src_is_depth_ ? VK_IMAGE_ASPECT_DEPTH_BIT
                                              : VK_IMAGE_ASPECT_COLOR_BIT;
            src_acquire.subresourceRange.levelCount = 1;
            src_acquire.subresourceRange.layerCount = 1;
            VkImageMemoryBarrier drain_barriers[2] = {dst_acquire, src_acquire};
            dfn.vkCmdPipelineBarrier(
                present_command_buffer_,
                pending_resolve_src_is_depth_
                    ? VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
                    : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2,
                drain_barriers);

            VkImageResolve resolve_region = {};
            resolve_region.srcSubresource.aspectMask =
                src_acquire.subresourceRange.aspectMask;
            resolve_region.srcSubresource.layerCount = 1;
            resolve_region.dstSubresource.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            resolve_region.dstSubresource.layerCount = 1;
            resolve_region.extent = {pending_resolve_width_,
                                     pending_resolve_height_, 1};
            if (pending_resolve_src_msaa_ >= 2) {
              dfn.vkCmdResolveImage(
                  present_command_buffer_, pending_resolve_src_image_,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  pending_resolve_dst_image_,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &resolve_region);
            } else {
              VkImageCopy copy_region = {};
              copy_region.srcSubresource = resolve_region.srcSubresource;
              copy_region.dstSubresource = resolve_region.dstSubresource;
              copy_region.extent = resolve_region.extent;
              dfn.vkCmdCopyImage(
                  present_command_buffer_, pending_resolve_src_image_,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  pending_resolve_dst_image_,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);
            }
            pending_resolve_dst_image_ = VK_NULL_HANDLE;
            pending_resolve_src_image_ = VK_NULL_HANDLE;
          }

          // Stage 2.2 debug dump: on the configured frame, dump every resolve
          // texture and the presented guest output after the resolve drain.
          if (dump_frame_enabled_ &&
              census_frame_count_ == dump_frame_number_) {
            if (!frame_dump_ready_) {
              frame_dump_ready_ = frame_dump_.Initialize(
                  vulkan_device, present_command_pool_, present_fence_);
            }
            if (frame_dump_ready_) {
              const std::filesystem::path dump_dir =
                  rex::filesystem::GetUserFolder() / "MclaRecomp" / "dumps";
              std::error_code ec;
              std::filesystem::create_directories(dump_dir, ec);
              for (uint32_t i = 0; i < rt_manager_.resolve_texture_count();
                   ++i) {
                const auto *tex = rt_manager_.resolve_texture(i);
                frame_dump_.DumpImage(
                    present_command_buffer_, tex->image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, tex->host_format,
                    tex->width, tex->height, dump_dir,
                    fmt::format("{:02}_dest_0x{:08X}_{}", i, tex->dest_base,
                                uint32_t(tex->host_format)));
              }
              frame_dump_.DumpImage(
                  present_command_buffer_, vulkan_context.image(),
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  ui::vulkan::VulkanPresenter::kGuestOutputFormat, width,
                  height, dump_dir, "99_presented");
            }
          }

          dfn.vkCmdPipelineBarrier(
              present_command_buffer_,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask, 0, 0,
              nullptr, 0, nullptr, 1, &release_barrier);
          dfn.vkEndCommandBuffer(present_command_buffer_);

          if (dfn.vkResetFences(device, 1, &present_fence_) != VK_SUCCESS) {
            REXLOG_WARN("stage2.2: vkResetFences failed");
            return false;
          }
          VkSubmitInfo submit_info = {};
          submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
          submit_info.commandBufferCount = 1;
          submit_info.pCommandBuffers = &present_command_buffer_;
          {
            const ui::vulkan::VulkanDevice::Queue::Acquisition queue_acq =
                vulkan_device->AcquireQueue(
                    vulkan_device->queue_family_graphics_compute(), 0);
            if (dfn.vkQueueSubmit(queue_acq.queue(), 1, &submit_info,
                                  present_fence_) != VK_SUCCESS) {
              REXLOG_WARN("stage2.2: vkQueueSubmit failed");
              return false;
            }
          }
          // 2.2 simplicity: keep waiting on the fence (measured under 30 fps
          // this does not bound the pipeline; revisit if frame time grows).
          if (dfn.vkWaitForFences(device, 1, &present_fence_, VK_TRUE,
                                  UINT64_MAX) != VK_SUCCESS) {
            REXLOG_WARN("stage2.2: vkWaitForFences failed");
            return false;
          }
          return true;
        });
    if (!refreshed) {
      ++present_fail_count_;
      if (present_fail_count_ <= 3 || (present_fail_count_ % 600) == 0) {
        REXLOG_WARN("stage2.2 frame {}: RefreshGuestOutput failed (count {})",
                    frame, uint64_t(present_fail_count_));
      }
    }
    // Dump PNG writes happen after the fence wait inside the callback.
    if (frame_dump_ready_ && dump_frame_enabled_ &&
        census_frame_count_ == dump_frame_number_) {
      frame_dump_.Flush(rex::filesystem::GetUserFolder() / "MclaRecomp" /
                        "dumps");
    }
  }

  if (frame <= 10 || (frame % 60) == 0) {
    REXLOG_INFO("stage2.2 frame {} resolves={} applied={} skipped_conv={} "
                "clears={} swap_key=0x{:08X}",
                frame, last_frame_resolves_, last_frame_applied_,
                last_frame_skipped_conv_, last_frame_clears_,
                last_swap_dest_base_);
  }
  // Reset the per-frame copy counters for the next frame.
  last_frame_resolves_ = 0;
  last_frame_applied_ = 0;
  last_frame_skipped_conv_ = 0;
  last_frame_clears_ = 0;
  census_.EndFrame();
  census_.BeginFrame();
  ++census_frame_count_;
  // The shader pair is deliberately NOT reset: the guest only re-uploads
  // its microcode when it changes, so a second frame may reuse the pair
  // loaded in the first without a new LoadShader call. A reset here would
  // record an all-zero pair that matches no real shader.
}

} // namespace rex::graphics::mcla_native
