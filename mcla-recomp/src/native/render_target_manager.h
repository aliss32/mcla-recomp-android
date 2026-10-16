// MCLA native renderer stage 2.2: host render target manager.
//
// Every guest EDRAM render target lives in its own host VkImage, keyed by
// (edram_base, pitch, msaa, format) - no EDRAM emulation. Format mappings
// were verified against the SDK's VulkanRenderTargetCache and xenos.h
// (see docs/stage2_recon.md): 8_8_8_8 -> R8G8B8A8_UNORM, 2_10_10_FLOAT
// and 16_16_16_16_FLOAT -> R16G16B16A16_SFLOAT, 32_FLOAT -> R32_SFLOAT,
// D24S8 -> D24_UNORM_S8_UINT when supported else D32_SFLOAT_S8_UINT,
// D24FS8 -> D32_SFLOAT_S8_UINT.
//
// Resolve destinations are registered as host textures keyed by
// (dest_base, pitch, height, format) so IssueSwap can find the frame's
// last resolve target and present it.

#pragma once

#include <cstdint>
#include <string>

#include <rex/ui/vulkan/api.h>
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/util.h>
#include <rex/graphics/util/draw.h>

#include <vulkan/vulkan.h>

namespace rex::graphics::mcla_native {

class RenderTargetManager {
public:
  RenderTargetManager() = default;
  ~RenderTargetManager();

  RenderTargetManager(const RenderTargetManager &) = delete;
  RenderTargetManager &operator=(const RenderTargetManager &) = delete;

  bool Initialize(const ui::vulkan::VulkanDevice *device);
  void Shutdown();

  // Host format for a guest color RT format (ColorRenderTargetFormat,
  // xenos.h:298-318); VK_FORMAT_UNDEFINED if unmapped.
  static VkFormat HostFormatFromRT(uint32_t guest_rt_format);
  // Host format for a guest resolve destination format (ColorFormat,
  // xenos.h:537-565); VK_FORMAT_UNDEFINED if unmapped. Separate tables: the
  // two enum spaces share numbers with different meanings (RT k_8_8 = 0
  // vs ColorFormat k_8_8_8_8 = 6).
  static VkFormat HostFormatFromTextureFormat(uint32_t guest_color_format);
  // Depth format for a guest depth RT format; VK_FORMAT_UNDEFINED if
  // unmapped.
  VkFormat HostDepthFormat(uint32_t guest_depth_format) const;

  // Mapping of a guest color format to its byte size in the guest (used to
  // size the RT from the pitch).
  static uint32_t GuestFormatBytesPerPixel(uint32_t guest_color_format);

  // A host render target for the guest EDRAM region. The key is
  // (edram_base, pitch_tiles, msaa_samples, is_depth, guest_format).
  struct RenderTarget {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view_color = VK_NULL_HANDLE; // color/depth aspect view
    VkImageView view_resolved =
        VK_NULL_HANDLE;  // sample-1 view for resolve dest
    uint32_t width = 0;  // pixels (derived: pitch / bpp)
    uint32_t height = 0; // pixels (from the resolve/scissor extent)
    uint32_t edram_base = 0;
    uint32_t pitch = 0;
    uint32_t msaa = 0; // xenos::MsaaSamples
    bool is_depth = false;
    uint32_t guest_format = 0;
    VkFormat host_format = VK_FORMAT_UNDEFINED;
  };

  // Returns the RT for the given key, creating it if needed. height comes
  // from the resolve region/scissor; base/pitch/format/msaa from
  // GetResolveInfo's ResolveEdramInfo.
  RenderTarget *GetOrCreateRenderTarget(uint32_t edram_base, uint32_t pitch,
                                        uint32_t msaa, bool is_depth,
                                        uint32_t guest_format, uint32_t width,
                                        uint32_t height);

  // Resolve destination registration (copy targets): keyed by
  // (dest_base, pitch, height, guest_dest_format).
  struct ResolveTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageView view_as_attachment = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t dest_base = 0;
    uint32_t pitch = 0;
    uint32_t guest_format = 0;
    VkFormat host_format = VK_FORMAT_UNDEFINED;
    // Layout the image was last left in (tracked so repeated barriers use the
    // real previous layout instead of UNDEFINED, which discards contents).
    VkImageLayout last_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool needs_rb_swap = false; // guest dest swap == 1
  };

  ResolveTexture *GetOrCreateResolveTexture(uint32_t dest_base, uint32_t pitch,
                                            uint32_t height,
                                            uint32_t guest_format,
                                            uint32_t swap_mode);
  ResolveTexture *FindResolveTexture(uint32_t dest_base);

  // Dump iteration accessors.
  uint32_t resolve_texture_count() const { return resolve_texture_count_; }
  const ResolveTexture *resolve_texture(uint32_t index) const {
    return index < resolve_texture_count_ ? &resolve_textures_[index] : nullptr;
  }

  // Render target lookup by EDRAM base (for clears).
  RenderTarget *FindRenderTarget(uint32_t edram_base);

  // True once Initialize succeeded; ApplyCopy no-ops before it.
  bool initialized() const { return device_ != nullptr; }

  // Per-message-key throttled logging: each distinct key is logged (with the
  // caller's formatted message) at most kMaxLogsPerKey times, and at most
  // kMaxLogKeys distinct keys are ever tracked, so one repeating error can
  // no longer consume the whole log budget.
  void LogOnce(const char *what);
  void LogThrottledKey(const char *key, const std::string &message);

private:
  const ui::vulkan::VulkanDevice *device_ = nullptr;

  // Small fixed pools: the census showed 57 dest addresses and 14 resolve
  // sources per title run, so linear arrays with linear search are fine and
  // avoid per-frame heap allocation (entries persist across frames).
  static constexpr uint32_t kMaxRenderTargets = 32;
  static constexpr uint32_t kMaxResolveTextures = 64;
  RenderTarget render_targets_[kMaxRenderTargets];
  uint32_t render_target_count_ = 0;
  ResolveTexture resolve_textures_[kMaxResolveTextures];
  uint32_t resolve_texture_count_ = 0;

  // D24_UNORM_S8_UINT availability, probed at Initialize.
  bool d24_unorm_s8_supported_ = false;

  // Per-key log throttle state (linear arrays, same rationale as the RT
  // pools: dozens of keys per run at most).
  static constexpr uint32_t kMaxLogKeys = 32;
  static constexpr uint32_t kMaxLogsPerKey = 3;
  const char *log_keys_[kMaxLogKeys] = {};
  uint32_t log_key_counts_[kMaxLogKeys] = {};
  uint32_t log_key_count_ = 0;
};

} // namespace rex::graphics::mcla_native
