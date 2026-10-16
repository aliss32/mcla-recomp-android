// MCLA native renderer stage 2.2 debug: PNG dump of resolve targets and the
// presented image for the frame selected by --mcla_native_dump_frame=N.
//
// A readback buffer + vkCmdCopyImageToBuffer per dumped image, then an
// uncompressed-deflate PNG writer (zlib "stored" blocks) - no SDK encoder
// dependency.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <rex/ui/vulkan/api.h>
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/util.h>

#include <vulkan/vulkan.h>

namespace rex::graphics::mcla_native {

class FrameDump {
public:
  FrameDump() = default;
  ~FrameDump();

  FrameDump(const FrameDump &) = delete;
  FrameDump &operator=(const FrameDump &) = delete;

  bool Initialize(const ui::vulkan::VulkanDevice *device, VkCommandPool pool,
                  VkFence fence);
  void Shutdown();

  // Copies src image (must be TRANSFER_SRC-capable, in a layout the caller
  // transitions - the caller must ensure the image is in a transfer source
  // or general layout compatible state when the command executes; a barrier
  // from the given old layout is recorded inside) into an RGBA8 or R32 readback
  // buffer and writes it as PNG to <dir>/<name>.png. Returns false on failure.
  bool DumpImage(VkCommandBuffer command_buffer, VkImage image,
                 VkImageLayout current_layout, VkFormat format, uint32_t width,
                 uint32_t height, const std::filesystem::path &dir,
                 const std::string &name);

  // Blocks until the last queued dump commands have completed, then writes
  // out all the PNGs collected this frame. Returns false on failure.
  bool Flush(const std::filesystem::path &dir);

private:
  struct PendingDump {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    VkDeviceSize size = 0;
  };

  const ui::vulkan::VulkanDevice *device_ = nullptr;
  VkFence fence_ = VK_NULL_HANDLE;
  static constexpr uint32_t kMaxPendingDumps = 16;
  PendingDump pending_[kMaxPendingDumps];
  uint32_t pending_count_ = 0;
  uint32_t warn_count_ = 0;
};

} // namespace rex::graphics::mcla_native
