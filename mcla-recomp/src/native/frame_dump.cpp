// MCLA native renderer stage 2.2 debug: PNG dump implementation.
#include "frame_dump.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include <rex/logging.h>

// VulkanDevice::Functions shorthand for the member definitions below; the
// device type is ui::vulkan::VulkanDevice (via frame_dump.h -> device.h).
using VulkanDevice = ui::vulkan::VulkanDevice;

namespace rex::graphics::mcla_native {

namespace {

// CRC32 for PNG chunks (standard table-driven implementation).
uint32_t Crc32(const uint8_t *data, size_t size) {
  static uint32_t table[256];
  static bool table_ready = false;
  if (!table_ready) {
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (uint32_t k = 0; k < 8; ++k) {
        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      }
      table[n] = c;
    }
    table_ready = true;
  }
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

void PutU32(std::vector<uint8_t> &out, uint32_t value) {
  out.push_back(uint8_t(value >> 24));
  out.push_back(uint8_t(value >> 16));
  out.push_back(uint8_t(value >> 8));
  out.push_back(uint8_t(value));
}

void WriteChunk(std::vector<uint8_t> &out, const char *type,
                const uint8_t *data, size_t size) {
  PutU32(out, uint32_t(size));
  size_t type_start = out.size();
  for (int i = 0; i < 4; ++i) {
    out.push_back(uint8_t(type[i]));
  }
  out.insert(out.end(), data, data + size);
  uint32_t crc = Crc32(out.data() + type_start, size + 4);
  PutU32(out, crc);
}

// Uncompressed-deflate PNG: zlib header (CM=8, no dictionary), stored blocks
// (BFINAL/BTYPE=00), adler32. Simple and dependency-free.
bool WritePng(const std::filesystem::path &path, uint32_t width,
              uint32_t height, bool is_rgba, const uint8_t *pixels,
              size_t row_bytes, size_t row_stride) {
  std::vector<uint8_t> png;
  png.reserve(64 + height * (row_bytes + 1) * 2);
  static const uint8_t signature[8] = {0x89, 'P',  'N',  'G',
                                       0x0D, 0x0A, 0x1A, 0x0A};
  png.insert(png.end(), signature, signature + 8);

  // IHDR
  uint8_t ihdr[13];
  ihdr[0] = uint8_t(width >> 24);
  ihdr[1] = uint8_t(width >> 16);
  ihdr[2] = uint8_t(width >> 8);
  ihdr[3] = uint8_t(width);
  ihdr[4] = uint8_t(height >> 24);
  ihdr[5] = uint8_t(height >> 16);
  ihdr[6] = uint8_t(height >> 8);
  ihdr[7] = uint8_t(height);
  ihdr[8] = 8;               // bit depth
  ihdr[9] = is_rgba ? 6 : 0; // 6 = RGBA, 0 = grayscale
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;
  WriteChunk(png, "IHDR", ihdr, sizeof(ihdr));

  // IDAT: raw data with per-row filter byte 0, zlib-wrapped with stored
  // deflate blocks (max 65535 bytes of payload per block).
  std::vector<uint8_t> raw;
  raw.reserve(height * (row_bytes + 1));
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0); // filter type none
    raw.insert(raw.end(), pixels + y * row_stride,
               pixels + y * row_stride + row_bytes);
  }
  std::vector<uint8_t> zlib;
  zlib.push_back(0x78);
  zlib.push_back(0x01); // CMF/FLG: no dict, fastest
  size_t offset = 0;
  while (offset < raw.size()) {
    size_t block = raw.size() - offset;
    if (block > 65535)
      block = 65535;
    bool last = (offset + block) == raw.size();
    zlib.push_back(last ? 1 : 0);
    zlib.push_back(uint8_t(block & 0xFF));
    zlib.push_back(uint8_t(block >> 8));
    zlib.push_back(uint8_t(~block & 0xFF));
    zlib.push_back(uint8_t((~block >> 8) & 0xFF));
    zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + block);
    offset += block;
  }
  uint32_t adler_a = 1, adler_b = 0;
  for (uint8_t byte : raw) {
    adler_a = (adler_a + byte) % 65521;
    adler_b = (adler_b + adler_a) % 65521;
  }
  PutU32(zlib, (adler_b << 16) | adler_a);
  WriteChunk(png, "IDAT", zlib.data(), zlib.size());
  WriteChunk(png, "IEND", nullptr, 0);

  FILE *file = std::fopen(path.string().c_str(), "wb");
  if (!file) {
    return false;
  }
  size_t written = std::fwrite(png.data(), 1, png.size(), file);
  std::fclose(file);
  return written == png.size();
}

} // namespace

FrameDump::~FrameDump() { Shutdown(); }

bool FrameDump::Initialize(const ui::vulkan::VulkanDevice *device,
                           VkCommandPool pool, VkFence fence) {
  device_ = device;
  fence_ = fence;
  (void)pool;
  return true;
}

void FrameDump::Shutdown() {
  if (!device_) {
    return;
  }
  const VulkanDevice::Functions &dfn = device_->functions();
  const VkDevice dev = device_->device();
  for (uint32_t i = 0; i < pending_count_; ++i) {
    PendingDump &dump = pending_[i];
    if (dump.mapped)
      dfn.vkUnmapMemory(dev, dump.memory);
    if (dump.buffer != VK_NULL_HANDLE)
      dfn.vkDestroyBuffer(dev, dump.buffer, nullptr);
    if (dump.memory != VK_NULL_HANDLE)
      dfn.vkFreeMemory(dev, dump.memory, nullptr);
  }
  pending_count_ = 0;
  device_ = nullptr;
}

bool FrameDump::DumpImage(VkCommandBuffer command_buffer, VkImage image,
                          VkImageLayout current_layout, VkFormat format,
                          uint32_t width, uint32_t height,
                          const std::filesystem::path &dir,
                          const std::string &name) {
  if (!device_ || pending_count_ >= kMaxPendingDumps) {
    if (pending_count_ >= kMaxPendingDumps && warn_count_ < 4) {
      REXLOG_WARN("stage2.2: dump queue full, frame {} ({})", name,
                  warn_count_);
      ++warn_count_;
    }
    return false;
  }

  bool is_rgba = false;
  uint32_t bytes_per_pixel = 0;
  switch (format) {
  case VK_FORMAT_R8G8B8A8_UNORM:
  case VK_FORMAT_B8G8R8A8_UNORM:
  case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
  case VK_FORMAT_R16G16B16A16_SFLOAT:
  case VK_FORMAT_R16G16B16A16_SNORM:
    is_rgba = true;
    bytes_per_pixel = 4;
    break;
  case VK_FORMAT_R32_SFLOAT:
    is_rgba = false; // dumped as grayscale 8-bit (float/255 scaling lost)
    bytes_per_pixel = 4;
    break;
  default:
    if (warn_count_ < 8) {
      REXLOG_WARN("stage2.2: dump unsupported format {} ({})", int(format),
                  name);
      ++warn_count_;
    }
    return false;
  }
  (void)is_rgba;

  const VulkanDevice::Functions &dfn = device_->functions();
  const VkDevice dev = device_->device();

  VkBufferImageCopy region = {};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageExtent = {width, height, 1};

  const VkDeviceSize size = VkDeviceSize(width) * height * bytes_per_pixel;
  PendingDump &dump = pending_[pending_count_];
  std::memset(&dump, 0, sizeof(dump));
  dump.name = name;
  dump.width = width;
  dump.height = height;
  dump.format = format;
  dump.size = size;

  VkBufferCreateInfo buffer_info = {};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  if (dfn.vkCreateBuffer(dev, &buffer_info, nullptr, &dump.buffer) !=
      VK_SUCCESS) {
    REXLOG_WARN("stage2.2: dump buffer creation failed ({})", name);
    return false;
  }
  VkMemoryRequirements requirements = {};
  dfn.vkGetBufferMemoryRequirements(dev, dump.buffer, &requirements);
  VkMemoryAllocateInfo alloc_info = {};
  alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc_info.allocationSize = requirements.size;
  const uint32_t memory_type = ui::vulkan::util::ChooseMemoryType(
      device_->memory_types(), requirements.memoryTypeBits,
      ui::vulkan::util::MemoryPurpose::kReadback);
  if (memory_type == UINT32_MAX) {
    REXLOG_WARN("stage2.2: dump memory type unavailable ({})", name);
    dfn.vkDestroyBuffer(dev, dump.buffer, nullptr);
    return false;
  }
  alloc_info.memoryTypeIndex = memory_type;
  if (dfn.vkAllocateMemory(dev, &alloc_info, nullptr, &dump.memory) !=
      VK_SUCCESS) {
    REXLOG_WARN("stage2.2: dump memory allocation failed ({})", name);
    dfn.vkDestroyBuffer(dev, dump.buffer, nullptr);
    return false;
  }
  dfn.vkBindBufferMemory(dev, dump.buffer, dump.memory, 0);
  if (dfn.vkMapMemory(dev, dump.memory, 0, size, 0, &dump.mapped) !=
      VK_SUCCESS) {
    REXLOG_WARN("stage2.2: dump memory map failed ({})", name);
    dfn.vkDestroyBuffer(dev, dump.buffer, nullptr);
    dfn.vkFreeMemory(dev, dump.memory, nullptr);
    return false;
  }

  // Barrier the image to TRANSFER_SRC_OPTIMAL.
  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barrier.oldLayout = current_layout;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  dfn.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &barrier);
  dfn.vkCmdCopyImageToBuffer(command_buffer, image,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dump.buffer,
                             1, &region);
  ++pending_count_;
  return true;
}

bool FrameDump::Flush(const std::filesystem::path &dir) {
  if (!device_ || !pending_count_) {
    return true;
  }
  const VulkanDevice::Functions &dfn = device_->functions();
  const VkDevice dev = device_->device();
  if (dfn.vkWaitForFences(dev, 1, &fence_, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
    REXLOG_WARN("stage2.2: dump flush fence wait failed");
    return false;
  }
  bool ok = true;
  for (uint32_t i = 0; i < pending_count_; ++i) {
    PendingDump &dump = pending_[i];
    const bool is_rgba = dump.format != VK_FORMAT_R32_SFLOAT &&
                         dump.format != VK_FORMAT_R16G16B16A16_SFLOAT;
    const size_t row_bytes = size_t(dump.width) * (is_rgba ? 4 : 4);
    const size_t row_stride = row_bytes;
    if (!WritePng(dir / (dump.name + ".png"), dump.width, dump.height, is_rgba,
                  static_cast<const uint8_t *>(dump.mapped), row_bytes,
                  row_stride)) {
      REXLOG_WARN("stage2.2: PNG write failed ({})", dump.name);
      ok = false;
    }
    if (dump.mapped)
      dfn.vkUnmapMemory(dev, dump.memory);
    if (dump.buffer != VK_NULL_HANDLE)
      dfn.vkDestroyBuffer(dev, dump.buffer, nullptr);
    if (dump.memory != VK_NULL_HANDLE)
      dfn.vkFreeMemory(dev, dump.memory, nullptr);
  }
  pending_count_ = 0;
  return ok;
}

} // namespace rex::graphics::mcla_native
