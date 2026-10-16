// Shader container dumper implementation.
//
// Xenos shader container header (big-endian guest memory): first dword
// 0x102A1100/0x102A1101 (2008 format, e.g. FH2) or 0x102A0E00 (2005 format).
// The container carries its own length at +4 (be uint32, bytes). We validate
// that length against plausible bounds before writing, dedupe by FNV-1a.
#include "dump/container_dumper.h"

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <xxhash/xxhash.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <set>

namespace rex::graphics::mcla_dump {
namespace {

constexpr uint32_t kContainerMagic2008 = 0x102A1100;
constexpr uint32_t kContainerMagic2008Alt = 0x102A1101;
constexpr uint32_t kContainerMagic2005 = 0x102A0E00;
constexpr uint32_t kMinContainerBytes = 0x100;
constexpr uint32_t kMaxContainerBytes = 0x40000;

bool IsContainerMagic(uint32_t v) {
  return v == kContainerMagic2008 || v == kContainerMagic2008Alt ||
         v == kContainerMagic2005;
}

// Guest pointers are big-endian 32-bit; length dword is bytes of the container.
uint32_t ReadContainerLength(const uint8_t *p) {
  return (uint32_t(p[4]) << 24) | (uint32_t(p[5]) << 16) |
         (uint32_t(p[6]) << 8) | uint32_t(p[7]);
}

} // namespace

ContainerDumper::ContainerDumper(std::string output_dir)
    : output_dir_(std::move(output_dir)) {}

ContainerDumper::~ContainerDumper() { Stop(); }

void ContainerDumper::Start() {
  if (running_.exchange(true))
    return;
  std::error_code ec;
  std::filesystem::create_directories(output_dir_, ec);
  thread_ = std::thread([this] { ScanLoop(); });
}

void ContainerDumper::Stop() {
  if (!running_.exchange(false))
    return;
  if (thread_.joinable())
    thread_.join();
}

void ContainerDumper::ScanLoop() {
  std::set<uint64_t> seen;
  const auto interval = std::chrono::seconds(5);
  auto *memory = rex::kernel_state()->memory();
  while (running_.load()) {
    std::this_thread::sleep_for(interval);
    // Walk the guest's user memory (0x80000000..0xA0000000) via the kernel
    // state.
    for (uint32_t base = 0x80000000; base < 0xA0000000; base += 0x1000) {
      const uint8_t *p =
          memory->TranslatePhysical<uint8_t *>(base & 0x1FFFFFFF);
      if (!p)
        continue;
      const auto v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                     (uint32_t(p[2]) << 8) | uint32_t(p[3]);
      if (!IsContainerMagic(v))
        continue;
      const uint32_t len = ReadContainerLength(p);
      if (len < kMinContainerBytes || len > kMaxContainerBytes)
        continue;
      uint64_t h = XXH3_64bits(p, len);
      if (!seen.insert(h).second)
        continue;
      char name[32];
      std::snprintf(name, sizeof(name), "%08llx.bin",
                    (unsigned long long)(h & 0xFFFFFFFF));
      std::filesystem::path out = std::filesystem::path(output_dir_) / name;
      FILE *f = std::fopen(out.string().c_str(), "wb");
      if (!f)
        continue;
      std::fwrite(p, 1, len, f);
      std::fclose(f);
      ++dumped_;
      if (dumped_.load() % 100 == 0)
        REXLOG_INFO("mcla-dump: {} containers dumped", dumped_.load());
    }
  }
  REXLOG_INFO("mcla-dump: finished, {} containers", dumped_.load());
}

} // namespace rex::graphics::mcla_dump
