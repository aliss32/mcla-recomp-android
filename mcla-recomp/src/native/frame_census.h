// Native renderer stage 1: the frame contract census.
//
// Consumes the same decoded PM4 stream the Xenos plugin sees, and instead of
// drawing, records what each frame actually contains: draws, resolves, clears,
// swaps, surface configurations (pitch / MSAA / color / depth formats), shader
// microcode hashes and their register context. The output is JSON Lines, one
// record per frame, written to the path given with --mcla_native_census.
// Summarizing the records gives the finite list of surface configurations,
// resolve kinds, texture layouts and primitive types the native executor must
// implement (the MCLA analogue of the FH1 frame contract, XR-01 of the Xenos
// retirement plan), and the basis for comparing the census of the same scripted
// route across renderer versions.
//
// Nothing is drawn in this stage: IssueDraw logs the draw with its full
// register context and returns success, so the guest's fence protocol
// (the interrupt the guest waits on at the end of each frame) still runs,
// but the presenter shows a blank frame. This makes the census safe to run
// for the whole game: it can boot, reach the menus and drive the city while
// recording, with the game's own streaming logic fed normally.
//
// Every draw that will need real rendering later is recorded with a reason
// field; a skipped draw is never silent.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/graphics/registers.h>
#include <rex/graphics/xenos.h>
#include <rex/types.h>

// The SurfaceKey hash uses the vendored xxHash directly (header-only use);
// the include dir comes from the xxHash::xxhash CMake target.
#include <xxhash.h>

namespace rex::graphics::mcla_native {

class MclaNativeCommandProcessor;
class MclaNativeGraphicsSystem;

// One resolve (copy): source target/format/base in EDRAM and the guest
// destination region, plus the destination texture format fields. Recorded
// in order; the last one before a swap is what swap_ptr should match.
struct ResolveInfo {
  const char *kind = "copy";
  bool depth = false;
  uint32_t format = 0; // source RT/depth format
  uint32_t src_select = 0;
  uint32_t src_base = 0;  // EDRAM tile base
  uint32_t dest_base = 0; // RB_COPY_DEST_BASE (raw guest address)
  uint32_t dest_pitch = 0;
  uint32_t dest_height = 0;
  uint32_t dest_format = 0; // RB_COPY_DEST_INFO.copy_dest_format
  uint32_t dest_endian = 0;
  uint32_t dest_swap = 0;
  uint32_t dest_number = 0;
};
// ---------------------------------------------------------------------------
// Frame census writer
// ---------------------------------------------------------------------------

// One frame's aggregated census. Records are kept small: a frame has
// thousands of draws, so per-draw lines are grouped into bounded tables.
struct FrameRecord {
  uint64_t frame = 0;
  uint64_t guest_time_us = 0;
  uint32_t swap_width = 0, swap_height = 0;
  // Guest address the swap reads the frontbuffer from. Compared against the
  // last resolve's "dest 0x..." to find the wrong-EDRAM-region reads.
  uint32_t swap_ptr = 0;
  uint32_t draws = 0, copies = 0, clears = 0;
  // Reason a draw was skipped or a resolve was dropped, for anything the
  // census could not classify - surfaced in EndFrame, never silent.
  std::unordered_map<std::string, uint32_t> skipped_reasons;
  // Distinct surface configurations this frame drew into, with draw counts.
  // Struct-keyed (converted to strings only at EndFrame) so a thousand-draw
  // frame costs one hash lookup per draw, not four string builds.
  struct SurfaceKey {
    uint32_t pitch;
    uint32_t msaa;
    uint32_t color_formats[4];
    uint32_t color_write_mask; // RB_COLOR_MASK: which targets are written
    uint32_t depth_format;
    uint32_t depth_base;
    uint32_t edram_mode;
    bool operator==(const SurfaceKey &) const = default;
  };
  struct SurfaceKeyHash {
    size_t operator()(const SurfaceKey &k) const {
      return XXH3_64bits(&k, sizeof(k));
    }
  };
  std::unordered_map<SurfaceKey, uint32_t, SurfaceKeyHash> surface_configs;
  // Distinct shader pair hashes, with draw counts. Struct-keyed, string
  // form produced at EndFrame.
  struct ShaderPairKey {
    uint64_t vs;
    uint64_t ps;
    bool operator==(const ShaderPairKey &) const = default;
  };
  struct ShaderPairKeyHash {
    size_t operator()(const ShaderPairKey &k) const {
      return XXH3_64bits(&k, sizeof(k));
    }
  };
  std::unordered_map<ShaderPairKey, uint32_t, ShaderPairKeyHash> shader_pairs;
  // Primitive types used, counted by enum value (bounded, no strings per
  // draw).
  static constexpr uint32_t kMaxPrimitiveTypes = 64;
  uint32_t primitive_counts[kMaxPrimitiveTypes] = {};
  // Resolve kinds this frame performed: copy/clear x color/depth x format,
  // plus the source and destination region of each copy (the EDRAM tile
  // bases the broken-render analysis compares), keyed as one string.
  std::unordered_map<std::string, uint32_t> resolve_kinds;
  // Resolve order is preserved: the last resolve before the swap is what
  // swap_ptr must match. resolve_log keeps the first N resolves in order,
  // each prefixed with the draw count at the time.
  std::string last_resolve;
  std::vector<std::string> resolve_log;
  static constexpr size_t kMaxResolveLog = 32;
  // The first N draws of the frame with full register context, for shader
  // and state decode work. Enough to seed the first translation work;
  // aggregates cover the rest.
  struct DrawSample {
    uint32_t prim_type;
    uint32_t index_count;
    bool indexed;
    uint32_t vertex_shader_hash; // low 32 bits of the ucode hash
    uint32_t pixel_shader_hash;  // low 32 bits, 0 when none
    uint32_t surface_pitch;
    uint32_t msaa;
    uint32_t color_formats[4]; // ColorRenderTargetFormat, 0xFF unused
    uint32_t color_bases[4];   // EDRAM tile base
    uint32_t depth_format;     // DepthRenderTargetFormat, 0xFF unused
    uint32_t depth_base;
    uint32_t edram_mode;
    uint32_t blend_control[4];
  };
  std::vector<DrawSample> samples;
  static constexpr size_t kMaxSamplesPerFrame = 24;
};

class FrameCensusWriter {
public:
  FrameCensusWriter();
  ~FrameCensusWriter();
  void BeginFrame();
  void EndFrame();
  // One call per draw: records the surface configuration, primitive, shader
  // pair and a bounded sample under a single lock. Thousand-draw frames are
  // common, so the per-draw overhead is one lock and a struct-keyed map
  // lookup; strings are only produced at EndFrame.
  void RecordDraw(const reg::RB_SURFACE_INFO &surface,
                  const reg::RB_COLOR_INFO (&color)[4],
                  const reg::RB_DEPTH_INFO &depth, uint32_t edram_mode,
                  uint32_t color_write_mask, bool depth_stencil_enabled,
                  xenos::PrimitiveType prim, uint64_t vs_hash, uint64_t ps_hash,
                  uint32_t index_count, bool indexed);
  void RecordResolve(const ResolveInfo &info);
  void RecordClear(bool depth, uint32_t format);
  void RecordSwap(uint32_t width, uint32_t height, uint32_t frontbuffer_ptr);
  void RecordSkipped(const std::string &reason);
  void SetEnabled(bool enabled, const std::filesystem::path &path);

  // Whether the census is actually writing (file opened). Called only on
  // the GPU thread, so no lock.
  bool enabled() const { return enabled_ && file_ != nullptr; }

  // Byte size of the census file at the last EndFrame flush (0 until the
  // first flush). The heartbeat reads it to prove the file is growing.
  uint64_t last_flush_bytes() const;

  // Convenience: DrawSample lives in FrameRecord.
  using DrawSample = FrameRecord::DrawSample;

private:
  FrameCensusWriter(const FrameCensusWriter &) = delete;
  FrameCensusWriter &operator=(const FrameCensusWriter &) = delete;
  void OpenFile();
  // Census runs on the GPU thread, but Shutdown flushes from any thread and
  // last_flush_bytes() is const, so the mutex must be mutable.
  mutable std::mutex mutex_;
  void RecordSkippedLocked(const std::string &reason);
  FrameRecord current_;
  uint64_t next_frame_ = 1;
  bool enabled_ = false;
  uint64_t last_flush_bytes_ = 0;
  std::filesystem::path path_;
  std::FILE *file_ = nullptr;
};

} // namespace rex::graphics::mcla_native
