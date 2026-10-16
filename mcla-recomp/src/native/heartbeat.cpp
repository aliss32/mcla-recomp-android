// Heartbeat implementation. The watchdog thread never touches Vulkan or the
// register file - it only formats counters and writes the sidecar, so it is
// safe even when the GPU thread is wedged inside a driver call.
#include "heartbeat.h"

#include <android/log.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

namespace rex::graphics::mcla_native {

namespace {

constexpr const char *kLogTag = "MCLA";
// Every swap/second the watchdog reports; a swap stall longer than this is
// its own line.
constexpr uint64_t kStallNs = 2'000'000'000ull;

uint64_t HashName(const char *name) {
  // FNV-1a; collisions land in the same slot and only mislabel one reason.
  uint64_t h = 1469598103934665603ull;
  for (const char *p = name; *p; ++p) {
    h ^= uint64_t(uint8_t(*p));
    h *= 1099511628211ull;
  }
  return h ? h : 1; // 0 means "free slot".
}

} // namespace

const char *GpuPhaseName(GpuPhase phase) {
  switch (phase) {
  case GpuPhase::kIdle:
    return "idle";
  case GpuPhase::kIssueDraw:
    return "issue_draw";
  case GpuPhase::kIssueCopy:
    return "issue_copy";
  case GpuPhase::kIssueSwap:
    return "issue_swap";
  case GpuPhase::kResolveDrain:
    return "resolve_drain";
  case GpuPhase::kPresent:
    return "present";
  case GpuPhase::kWaitFence:
    return "wait_fence";
  case GpuPhase::kSubmit:
    return "submit";
  }
  return "?";
}

void Heartbeat::Initialize(const std::string &sidecar_dir) {
  std::error_code ec;
  std::filesystem::create_directories(sidecar_dir, ec);
  sidecar_path_ =
      (std::filesystem::path(sidecar_dir) / "mcla_heartbeat.log").string();
  if (!running_.exchange(true)) {
    watchdog_ = std::thread([this] { WatchdogLoop(); });
  }
}

void Heartbeat::Shutdown() {
  if (running_.exchange(false) && watchdog_.joinable()) {
    watchdog_.join();
  }
}

void Heartbeat::RecordSkip(const char *reason) {
  const uint64_t h = HashName(reason);
  for (auto &slot : skipped_) {
    uint64_t expected = 0;
    if (slot.name_hash.compare_exchange_strong(expected, h) ||
        slot.name_hash.load(std::memory_order_relaxed) == h) {
      slot.count.fetch_add(1, std::memory_order_relaxed);
      return;
    }
  }
  // All 16 slots taken: fold into slot 0 so the total is still visible.
  skipped_[0].count.fetch_add(1, std::memory_order_relaxed);
}

void Heartbeat::RecordVkError(const char *function, int vk_result) {
  const uint64_t key =
      (HashName(function) << 16) | uint64_t(uint32_t(vk_result) & 0xFFFF);
  for (auto &slot : vk_errors_) {
    uint64_t expected = 0;
    if (slot.key.compare_exchange_strong(expected, key) ||
        slot.key.load(std::memory_order_relaxed) == key) {
      slot.count.fetch_add(1, std::memory_order_relaxed);
      return;
    }
  }
  vk_errors_[0].count.fetch_add(1, std::memory_order_relaxed);
}

void Heartbeat::RecordDistinctSwapPtr(uint64_t ptr) {
  if (last_swap_ptr_.load(std::memory_order_relaxed) != ptr) {
    distinct_swap_ptrs_.fetch_add(1, std::memory_order_relaxed);
    last_swap_ptr_.store(ptr, std::memory_order_relaxed);
  }
  last_swap_time_ns_.store(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count(),
      std::memory_order_relaxed);
}

void Heartbeat::WatchdogLoop() {
  uint64_t last_swap_seen = 0;
  uint64_t last_swap_logged = 0;
  while (running_.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!running_.load(std::memory_order_relaxed)) {
      break;
    }

    const uint64_t swaps_in = swap_enter_.load(std::memory_order_relaxed);
    const bool stalled =
        last_swap_time_ns_.load(std::memory_order_relaxed) != 0 &&
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
                    .count() -
                last_swap_time_ns_.load(std::memory_order_relaxed) >
            kStallNs;

    char skipped_buf[256] = "";
    {
      std::string s;
      for (auto &slot : skipped_) {
        const uint64_t c = slot.count.load(std::memory_order_relaxed);
        if (c) {
          char part[64];
          std::snprintf(part, sizeof(part), "%s%llu:%llu", s.empty() ? "" : " ",
                        (unsigned long long)slot.name_hash.load(
                            std::memory_order_relaxed),
                        (unsigned long long)c);
          s += part;
        }
      }
      std::strncpy(skipped_buf, s.c_str(), sizeof(skipped_buf) - 1);
    }
    char vk_buf[256] = "";
    {
      std::string s;
      for (auto &slot : vk_errors_) {
        const uint64_t c = slot.count.load(std::memory_order_relaxed);
        if (c) {
          const uint64_t key = slot.key.load(std::memory_order_relaxed);
          char part[64];
          std::snprintf(part, sizeof(part), "%s%llx:%llu", s.empty() ? "" : " ",
                        (unsigned long long)(key >> 16), (unsigned long long)c);
          s += part;
        }
      }
      std::strncpy(vk_buf, s.c_str(), sizeof(vk_buf) - 1);
    }

    char line[512];
    const GpuPhase phase = gpu_phase_.load(std::memory_order_relaxed);
    std::snprintf(
        line, sizeof(line),
        "hb draws=%llu copies=%llu swap_in=%llu swap_out=%llu endframe=%llu "
        "endframe_bytes=%llu rq=%llu ra=%llu swap_ptrs=%llu last_swap=%llx "
        "phase=%s%s audio=%llu skips=[%s] vkerr=[%s]",
        (unsigned long long)draws_.load(std::memory_order_relaxed),
        (unsigned long long)copies_.load(std::memory_order_relaxed),
        (unsigned long long)swaps_in,
        (unsigned long long)swap_exit_.load(std::memory_order_relaxed),
        (unsigned long long)endframe_called_.load(std::memory_order_relaxed),
        (unsigned long long)endframe_bytes_.load(std::memory_order_relaxed),
        (unsigned long long)resolves_queued_.load(std::memory_order_relaxed),
        (unsigned long long)resolves_applied_.load(std::memory_order_relaxed),
        (unsigned long long)distinct_swap_ptrs_.load(std::memory_order_relaxed),
        (unsigned long long)last_swap_ptr_.load(std::memory_order_relaxed),
        GpuPhaseName(phase), stalled ? " STALLED>2s" : "",
        (unsigned long long)audio_frames_submitted_.load(
            std::memory_order_relaxed),
        skipped_buf, vk_buf);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", line);

    if (!sidecar_path_.empty()) {
      std::FILE *f = std::fopen(sidecar_path_.c_str(), "a");
      if (f) {
        std::fputs(line, f);
        std::fputc('\n', f);
        std::fflush(f);
        std::fclose(f);
      }
    }
    if (last_swap_seen != swaps_in && swaps_in >= last_swap_logged + 60) {
      last_swap_logged = swaps_in;
    }
    last_swap_seen = swaps_in;
  }
}

namespace {
Heartbeat g_heartbeat;
}

Heartbeat &heartbeat() { return g_heartbeat; }

} // namespace rex::graphics::mcla_native
