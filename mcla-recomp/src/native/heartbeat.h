// GPU pipeline heartbeat: a lock-free counter block plus a watchdog thread
// that emits one summary line every second (and on every 60th swap), so a
// wedged pipeline (black screen, no swap, device lost) is visible from
// logcat alone - the rotating SDK log file can be killed mid-write, logcat
// is always streamed.
#ifndef MCLA_RECOMP_SRC_NATIVE_HEARTBEAT_H_
#define MCLA_RECOMP_SRC_NATIVE_HEARTBEAT_H_

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace rex::graphics::mcla_native {

// Where the GPU thread is right now, stamped at each pipeline stage by the
// command processor. The watchdog reports the last phase, so a stall shows
// exactly which stage hung (e.g. wait_fence).
enum class GpuPhase : uint32_t {
  kIdle = 0,
  kIssueDraw = 1,
  kIssueCopy = 2,
  kIssueSwap = 3,
  kResolveDrain = 4,
  kPresent = 5,
  kWaitFence = 6,
  kSubmit = 7,
};

const char *GpuPhaseName(GpuPhase phase);

// One line per heartbeat: counters + the phase + the last VkResult. Written
// through __android_log_print (tag "MCLA") and to a sidecar file with
// fflush, so it survives a dead rotating sink and a mid-run crash.
class Heartbeat {
public:
  static constexpr uint32_t kMaxSkippedReasons = 16;
  static constexpr uint32_t kMaxVkErrors = 16;

  // Identifies one skipped resolve reason (short string, hashed into a slot).
  struct SkippedSlot {
    std::atomic<uint64_t> name_hash;
    std::atomic<uint64_t> count;
  };
  // One failing Vk function + VkResult (both hashed/valued) and its count.
  struct VkErrorSlot {
    std::atomic<uint64_t> key; // (function_hash << 24) | VkResult
    std::atomic<uint64_t> count;
  };

  void Initialize(const std::string &sidecar_dir);
  void Shutdown();

  // --- command processor stamps (all relaxed atomics; the watchdog only
  // reads, and a torn read of one more/fewer frame is harmless). ---
  std::atomic<uint64_t> &draws() { return draws_; }
  std::atomic<uint64_t> &copies() { return copies_; }
  std::atomic<uint64_t> &swap_enter() { return swap_enter_; }
  std::atomic<uint64_t> &swap_exit() { return swap_exit_; }
  std::atomic<uint64_t> &endframe_called() { return endframe_called_; }
  std::atomic<uint64_t> &endframe_bytes() { return endframe_bytes_; }
  std::atomic<uint64_t> &resolves_queued() { return resolves_queued_; }
  std::atomic<uint64_t> &resolves_applied() { return resolves_applied_; }
  std::atomic<uint64_t> &distinct_swap_ptrs() { return distinct_swap_ptrs_; }
  std::atomic<uint64_t> &last_swap_ptr() { return last_swap_ptr_; }
  std::atomic<uint64_t> &audio_frames_submitted() {
    return audio_frames_submitted_;
  }
  std::atomic<GpuPhase> &gpu_phase() { return gpu_phase_; }

  void RecordSkip(const char *reason);
  void RecordVkError(const char *function, int vk_result);
  void RecordDistinctSwapPtr(uint64_t ptr);

private:
  void WatchdogLoop();

  std::atomic<uint64_t> draws_{0};
  std::atomic<uint64_t> copies_{0};
  std::atomic<uint64_t> swap_enter_{0};
  std::atomic<uint64_t> swap_exit_{0};
  std::atomic<uint64_t> endframe_called_{0};
  std::atomic<uint64_t> endframe_bytes_{0};
  std::atomic<uint64_t> resolves_queued_{0};
  std::atomic<uint64_t> resolves_applied_{0};
  std::atomic<uint64_t> distinct_swap_ptrs_{0};
  std::atomic<uint64_t> last_swap_ptr_{0};
  std::atomic<uint64_t> audio_frames_submitted_{0};
  std::atomic<GpuPhase> gpu_phase_{GpuPhase::kIdle};

  SkippedSlot skipped_[kMaxSkippedReasons];
  VkErrorSlot vk_errors_[kMaxVkErrors];

  std::string sidecar_path_;
  std::thread watchdog_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> last_swap_time_ns_{0};
};

Heartbeat &heartbeat();

} // namespace rex::graphics::mcla_native

#endif // MCLA_RECOMP_SRC_NATIVE_HEARTBEAT_H_
