// MCLA native renderer stage 2.2: host render target manager implementation.
#include "render_target_manager.h"

#include <cinttypes>
#include <cstring>

#include <fmt/format.h>

#include <rex/logging.h>

namespace rex::graphics::mcla_native {

using ui::vulkan::VulkanDevice;

RenderTargetManager::~RenderTargetManager() { Shutdown(); }

bool RenderTargetManager::Initialize(const VulkanDevice *device) {
  device_ = device;
  const VulkanDevice::Functions &dfn = device_->functions();
  const VkDevice dev = device_->device();
  VkFormatProperties props;
  device_->vulkan_instance()->functions().vkGetPhysicalDeviceFormatProperties(
      device_->physical_device(), VK_FORMAT_D24_UNORM_S8_UINT, &props);
  d24_unorm_s8_supported_ =
      (props.optimalTilingFeatures &
       VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
  (void)dfn;
  (void)dev;
  return true;
}

void RenderTargetManager::Shutdown() {
  if (!device_) {
    return;
  }
  const VulkanDevice::Functions &dfn = device_->functions();
  const VkDevice dev = device_->device();
  for (uint32_t i = 0; i < render_target_count_; ++i) {
    RenderTarget &rt = render_targets_[i];
    if (rt.view_color != VK_NULL_HANDLE)
      dfn.vkDestroyImageView(dev, rt.view_color, nullptr);
    if (rt.view_resolved != VK_NULL_HANDLE)
      dfn.vkDestroyImageView(dev, rt.view_resolved, nullptr);
    if (rt.image != VK_NULL_HANDLE)
      dfn.vkDestroyImage(dev, rt.image, nullptr);
    if (rt.memory != VK_NULL_HANDLE)
      dfn.vkFreeMemory(dev, rt.memory, nullptr);
  }
  render_target_count_ = 0;
  for (uint32_t i = 0; i < resolve_texture_count_; ++i) {
    ResolveTexture &tex = resolve_textures_[i];
    if (tex.view != VK_NULL_HANDLE)
      dfn.vkDestroyImageView(dev, tex.view, nullptr);
    if (tex.view_as_attachment != VK_NULL_HANDLE)
      dfn.vkDestroyImageView(dev, tex.view_as_attachment, nullptr);
    if (tex.image != VK_NULL_HANDLE)
      dfn.vkDestroyImage(dev, tex.image, nullptr);
    if (tex.memory != VK_NULL_HANDLE)
      dfn.vkFreeMemory(dev, tex.memory, nullptr);
  }
  resolve_texture_count_ = 0;
  device_ = nullptr;
}

VkFormat
RenderTargetManager::HostFormatFromRT(uint32_t guest_rt_format) {
  // ColorRenderTargetFormat numbers per xenos.h:298-318.
  switch (guest_rt_format) {
  case 0: // k_8_8_8_8
  case 1: // k_8_8_8_8_GAMMA (same storage)
    return VK_FORMAT_R8G8B8A8_UNORM;
  case 3: // k_2_10_10_10_FLOAT (7e3) -> f16x4 storage (SDK parity)
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case 4: // k_16_16 fixed point -> snorm (SDK parity)
  case 5: // k_16_16_16_16
    return VK_FORMAT_R16G16B16A16_SNORM;
  case 7: // k_16_16_FLOAT
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case 14: // k_32_FLOAT
    return VK_FORMAT_R32_SFLOAT;
  default:
    return VK_FORMAT_UNDEFINED;
  }
}

VkFormat RenderTargetManager::HostFormatFromTextureFormat(
    uint32_t guest_color_format) {
  // ColorFormat numbers per xenos.h:537-565 (k_8_8_8_8=6,
  // k_2_10_10_10=7, k_16_16=26, k_16_16_16_16_FLOAT=32,
  // k_32_FLOAT=36). Note 0 is NOT a valid ColorFormat here - the old
  // single-table mapping conflated RT format 0 with ColorFormat 6.
  switch (guest_color_format) {
  case 6: // k_8_8_8_8
  case 14: // k_8_8_8_8_A (same 32bpp RGBA8 storage)
    return VK_FORMAT_R8G8B8A8_UNORM;
  case 7: // k_2_10_10_10 -> f16x4 storage (SDK parity)
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case 26: // k_16_16 -> snorm (SDK parity)
    return VK_FORMAT_R16G16B16A16_SNORM;
  case 32: // k_16_16_FLOAT
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case 36: // k_32_FLOAT
    return VK_FORMAT_R32_SFLOAT;
  default:
    return VK_FORMAT_UNDEFINED;
  }
}

uint32_t
RenderTargetManager::GuestFormatBytesPerPixel(uint32_t guest_color_format) {
  switch (guest_color_format) {
  case 0:
  case 6:
  case 2:
  case 3:
  case 7:
    return 4;
  case 5:
  case 8:
  case 26:
  case 32:
    return 8;
  case 14:
  case 36:
    return 4;
  default:
    return 0;
  }
}

VkFormat
RenderTargetManager::HostDepthFormat(uint32_t guest_depth_format) const {
  // DepthRenderTargetFormat (xenos.h): kD24S8 = 0, kD24FS8 = 1.
  switch (guest_depth_format) {
  case 0:
    return d24_unorm_s8_supported_ ? VK_FORMAT_D24_UNORM_S8_UINT
                                   : VK_FORMAT_D32_SFLOAT_S8_UINT;
  case 1:
    return VK_FORMAT_D32_SFLOAT_S8_UINT; // 20e4 floats have no Vulkan format
  default:
    return VK_FORMAT_UNDEFINED;
  }
}

RenderTargetManager::RenderTarget *RenderTargetManager::GetOrCreateRenderTarget(
    uint32_t edram_base, uint32_t pitch, uint32_t msaa, bool is_depth,
    uint32_t guest_format, uint32_t width, uint32_t height) {
  for (uint32_t i = 0; i < render_target_count_; ++i) {
    RenderTarget &rt = render_targets_[i];
    if (rt.edram_base == edram_base && rt.pitch == pitch && rt.msaa == msaa &&
        rt.is_depth == is_depth && rt.guest_format == guest_format) {
      if (height > rt.height) {
        // Grown resolve region; recreate lazily is not implemented in 2.2 -
        // the RT is oversized at creation instead.
      }
      return &rt;
    }
  }
  if (render_target_count_ >= kMaxRenderTargets) {
    LogOnce("rt_pool_exhausted");
    return nullptr;
  }

  const VkFormat host_format =
      is_depth ? HostDepthFormat(guest_format) : HostFormatFromRT(guest_format);
  if (host_format == VK_FORMAT_UNDEFINED) {
    REXLOG_WARN("stage2.2: unmapped RT guest format {} (depth={})",
                guest_format, is_depth);
    return nullptr;
  }

  RenderTarget &rt = render_targets_[render_target_count_];
  std::memset(&rt, 0, sizeof(rt));
  rt.edram_base = edram_base;
  rt.pitch = pitch;
  rt.msaa = msaa;
  rt.is_depth = is_depth;
  rt.guest_format = guest_format;
  rt.host_format = host_format;
  rt.width = width;
  rt.height = height;

  const VulkanDevice::Functions &dfn = device_->functions();
  const VkDevice dev = device_->device();

  VkImageCreateInfo image_info = {};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = host_format;
  image_info.extent = {width, height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = (msaa >= 3)   ? VK_SAMPLE_COUNT_4_BIT
                       : (msaa >= 2) ? VK_SAMPLE_COUNT_2_BIT
                                     : VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          device_, image_info, ui::vulkan::util::MemoryPurpose::kDeviceLocal,
          rt.image, rt.memory)) {
    REXLOG_WARN("stage2.2: failed to create RT image (base=0x{:08X})",
                edram_base);
    return nullptr;
  }

  VkImageViewCreateInfo view_info = {};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = rt.image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = host_format;
  view_info.subresourceRange.aspectMask =
      is_depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
               : VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.layerCount = 1;
  if (dfn.vkCreateImageView(dev, &view_info, nullptr, &rt.view_color) !=
      VK_SUCCESS) {
    REXLOG_WARN("stage2.2: failed to create RT view (base=0x{:08X})",
                edram_base);
    return nullptr;
  }
  if (msaa >= 2) {
    // A sample-1 view for later resolves.
    view_info.subresourceRange.aspectMask =
        is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    if (dfn.vkCreateImageView(dev, &view_info, nullptr, &rt.view_resolved) !=
        VK_SUCCESS) {
      REXLOG_WARN("stage2.2: failed to create RT resolved view (base=0x{:08X})",
                  edram_base);
      return nullptr;
    }
  }

  REXLOG_INFO("stage2.2: RT created base=0x{:08X} pitch={} {} {}{} width={} "
              "height={} host_format={}",
              edram_base, pitch, is_depth ? "depth" : "color",
              (msaa >= 3)   ? "4x"
              : (msaa >= 2) ? "2x"
                            : "1x",
              "MSAA", width, height, int(host_format));
  ++render_target_count_;
  return &rt;
}

RenderTargetManager::ResolveTexture *
RenderTargetManager::GetOrCreateResolveTexture(uint32_t dest_base,
                                               uint32_t pitch, uint32_t height,
                                               uint32_t guest_format,
                                               uint32_t swap_mode) {
  for (uint32_t i = 0; i < resolve_texture_count_; ++i) {
    ResolveTexture &tex = resolve_textures_[i];
    if (tex.dest_base == dest_base && tex.pitch == pitch &&
        tex.guest_format == guest_format) {
      if (height > tex.height) {
        tex.height = height;
      }
      return &tex;
    }
  }
  if (resolve_texture_count_ >= kMaxResolveTextures) {
    LogOnce("resolve_pool_exhausted");
    return nullptr;
  }

  const VkFormat host_format = HostFormatFromTextureFormat(guest_format);
  if (host_format == VK_FORMAT_UNDEFINED) {
    REXLOG_WARN("stage2.2: unmapped resolve dest guest format {}",
                guest_format);
    return nullptr;
  }

  const uint32_t bpp = GuestFormatBytesPerPixel(guest_format);
  const uint32_t width = bpp ? pitch / bpp : 0;
  if (!width || !height) {
    LogOnce("resolve_bad_size");
    return nullptr;
  }

  ResolveTexture &tex = resolve_textures_[resolve_texture_count_];
  std::memset(&tex, 0, sizeof(tex));
  tex.dest_base = dest_base;
  tex.pitch = pitch;
  tex.height = height;
  tex.width = width;
  tex.guest_format = guest_format;
  tex.host_format = host_format;
  tex.needs_rb_swap = (swap_mode == 1);

  const VulkanDevice::Functions &dfn = device_->functions();
  const VkDevice dev = device_->device();

  VkImageCreateInfo image_info = {};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = host_format;
  image_info.extent = {width, height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  // SAMPLED for the swap pass, COLOR_ATTACHMENT for a possible direct clear,
  // TRANSFER_DST for vkCmdResolveImage/copy destinations.
  image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          device_, image_info, ui::vulkan::util::MemoryPurpose::kDeviceLocal,
          tex.image, tex.memory)) {
    REXLOG_WARN("stage2.2: failed to create resolve texture (dest=0x{:08X})",
                dest_base);
    return nullptr;
  }

  VkImageViewCreateInfo view_info = {};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = tex.image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = host_format;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.layerCount = 1;
  if (dfn.vkCreateImageView(dev, &view_info, nullptr, &tex.view) !=
      VK_SUCCESS) {
    REXLOG_WARN("stage2.2: failed to create resolve view (dest=0x{:08X})",
                dest_base);
    return nullptr;
  }
  tex.view_as_attachment = tex.view; // same view works for both

  REXLOG_INFO("stage2.2: resolve texture created dest=0x{:08X} {}x{} fmt={} "
              "rb_swap={}",
              dest_base, width, height, int(host_format), tex.needs_rb_swap);
  ++resolve_texture_count_;
  return &tex;
}

RenderTargetManager::ResolveTexture *
RenderTargetManager::FindResolveTexture(uint32_t dest_base) {
  for (uint32_t i = 0; i < resolve_texture_count_; ++i) {
    if (resolve_textures_[i].dest_base == dest_base) {
      return &resolve_textures_[i];
    }
  }
  return nullptr;
}

RenderTargetManager::RenderTarget *
RenderTargetManager::FindRenderTarget(uint32_t edram_base) {
  for (uint32_t i = 0; i < render_target_count_; ++i) {
    if (render_targets_[i].edram_base == edram_base) {
      return &render_targets_[i];
    }
  }
  return nullptr;
}

void RenderTargetManager::LogOnce(const char *what) {
  LogThrottledKey(what, fmt::format("stage2.2: {}", what));
}

void RenderTargetManager::LogThrottledKey(const char *key,
                                           const std::string &message) {
  uint32_t slot = kMaxLogKeys;
  for (uint32_t i = 0; i < log_key_count_; ++i) {
    if (std::strcmp(log_keys_[i], key) == 0) {
      slot = i;
      break;
    }
  }
  if (slot == kMaxLogKeys) {
    if (log_key_count_ >= kMaxLogKeys) {
      return;
    }
    slot = log_key_count_++;
    log_keys_[slot] = key;
  }
  if (log_key_counts_[slot] >= kMaxLogsPerKey) {
    return;
  }
  ++log_key_counts_[slot];
  REXLOG_WARN("{}", message);
}

} // namespace rex::graphics::mcla_native
