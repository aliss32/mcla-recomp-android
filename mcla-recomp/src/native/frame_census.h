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

namespace rex::graphics::mcla_native {

class MclaNativeCommandProcessor;
class MclaNativeGraphicsSystem;
// ---------------------------------------------------------------------------
// Frame census writer
// ---------------------------------------------------------------------------

// One frame's aggregated census. Records are kept small: a frame has
// thousands of draws, so per-draw lines are grouped into bounded tables.
struct FrameRecord {
  uint64_t frame = 0;
  uint64_t guest_time_us = 0;
  uint32_t swap_width = 0, swap_height = 0;
  uint32_t draws = 0, copies = 0, clears = 0;
  // Reason a draw was skipped or a resolve was dropped, for anything the
  // census could not classify - surfaced in EndFrame, never silent.
  std::unordered_map<std::string, uint32_t> skipped_reasons;
  // Distinct surface configurations this frame drew into, as
  // "pitch,msaa,rt0,rt1,rt2,rt3,depth,edram_mode,depth_base" keys with a
  // count each. All four render target formats are in the key: MRT passes
  // with different formats per target are distinct configurations.
  std::unordered_map<std::string, uint32_t> surface_configs;
  // Distinct shader pair hashes, with draw counts.
  std::unordered_map<std::string, uint32_t> shader_pairs;
  // Primitive types used.
  std::unordered_map<std::string, uint32_t> primitive_types;
  // Resolve kinds this frame performed: "copy" or "clear" x color/depth x
  // format, with counts.
  std::unordered_map<std::string, uint32_t> resolve_kinds;
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
  void RecordDrawSample(FrameRecord::DrawSample sample);
  void RecordSurfaceConfig(const reg::RB_SURFACE_INFO &surface,
                           const reg::RB_COLOR_INFO (&color)[4],
                           const reg::RB_DEPTH_INFO &depth,
                           uint32_t edram_mode);
  void RecordShaderPair(uint64_t vs_hash, uint64_t ps_hash);
  void RecordPrimitive(xenos::PrimitiveType prim);
  void RecordResolve(const char *kind, bool depth, uint32_t format);
  void RecordClear(bool depth, uint32_t format);
  void RecordSwap(uint32_t width, uint32_t height);
  void RecordSkipped(const std::string &reason);
  void SetEnabled(bool enabled, const std::filesystem::path &path);

  // Convenience: DrawSample lives in FrameRecord.
  using DrawSample = FrameRecord::DrawSample;

private:
  FrameCensusWriter(const FrameCensusWriter &) = delete;
  FrameCensusWriter &operator=(const FrameCensusWriter &) = delete;
  void OpenFile();
  bool enabled() const { return enabled_ && file_ != nullptr; }
  // Census runs on the GPU thread, but Shutdown flushes from any thread.
  std::mutex mutex_;
  void RecordSkippedLocked(const std::string &reason);
  FrameRecord current_;
  uint64_t next_frame_ = 1;
  bool enabled_ = false;
  bool file_open_failed_ = false;
  std::filesystem::path path_;
  std::FILE *file_ = nullptr;
};

} // namespace rex::graphics::mcla_native
