// Frame census writer implementation. See frame_census.h.
#include "frame_census.h"

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/format.h>
#include <rex/chrono/clock.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>

namespace rex::graphics::mcla_native {

namespace {
const char *ColorFormatName(uint32_t format) {
  switch (static_cast<xenos::ColorRenderTargetFormat>(format)) {
  case xenos::ColorRenderTargetFormat::k_8_8_8_8:
    return "8_8_8_8";
  case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
    return "8_8_8_8_GAMMA";
  case xenos::ColorRenderTargetFormat::k_2_10_10_10:
    return "2_10_10_10";
  case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
    return "2_10_10_10_AS_10_10_10_10";
  case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    return "2_10_10_10_FLOAT";
  case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
    return "2_10_10_10_FLOAT_AS_16_16";
  case xenos::ColorRenderTargetFormat::k_16_16:
    return "16_16";
  case xenos::ColorRenderTargetFormat::k_16_16_16_16:
    return "16_16_16_16";
  case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
    return "16_16_FLOAT";
  case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
    return "16_16_16_16_FLOAT";
  case xenos::ColorRenderTargetFormat::k_32_FLOAT:
    return "32_FLOAT";
  default:
    return "unknown";
  }
}

const char *DepthFormatName(uint32_t format) {
  switch (format) {
  case 0:
    return "D24S8";
  case 1:
    return "D24FS8";
  case 0xFFu:
    return "-"; // normalized: depth/stencil not used by the draw
  default:
    return "unknown";
  }
}

const char *MsaaName(uint32_t samples) {
  switch (samples) {
  case 0:
    return "1x";
  case 1:
    return "2x";
  case 2:
    return "4x";
  default:
    return "?";
  }
}
} // namespace

FrameCensusWriter::FrameCensusWriter() = default;

FrameCensusWriter::~FrameCensusWriter() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (file_) {
    std::fclose(file_);
    file_ = nullptr;
  }
}

void FrameCensusWriter::SetEnabled(bool enabled,
                                   const std::filesystem::path &path) {
  std::lock_guard<std::mutex> lock(mutex_);
  path_ = path;
  if (!enabled || path.empty()) {
    enabled_ = false;
    return;
  }
  enabled_ = true;
  if (!file_) {
    OpenFile();
  }
  // If the file could not be opened the census must not accumulate:
  // Record* checks enabled(), which requires a live file handle.
  if (!file_) {
    enabled_ = false;
    REXLOG_ERROR("mcla_native census: cannot open {}; the census is OFF",
                 path.string());
  }
}

void FrameCensusWriter::OpenFile() {
  std::error_code error;
  std::filesystem::create_directories(path_.parent_path(), error);
  file_ = std::fopen(path_.string().c_str(), "wb");
  if (!file_) {
    REXLOG_ERROR("mcla_native census: cannot open {}", path_.string());
  }
}

void FrameCensusWriter::BeginFrame() {
  std::lock_guard<std::mutex> lock(mutex_);
  current_ = FrameRecord{};
  current_.frame = next_frame_;
  // Microseconds since boot, computed without overflowing the 64-bit
  // intermediate: QueryHostTickCount() * 1e6 would wrap after ~5 hours on
  // a 1 GHz counter, and Android's counter runs since boot.
  const uint64_t ticks = rex::chrono::Clock::QueryHostTickCount();
  const uint64_t freq = rex::chrono::Clock::QueryHostTickFrequency();
  current_.guest_time_us =
      freq ? (ticks / freq) * 1000000ull + (ticks % freq) * 1000000ull / freq
           : 0;
}

void FrameCensusWriter::RecordDraw(
    const reg::RB_SURFACE_INFO &surface, const reg::RB_COLOR_INFO (&color)[4],
    const reg::RB_DEPTH_INFO &depth, uint32_t edram_mode,
    uint32_t color_write_mask, bool depth_stencil_enabled,
    xenos::PrimitiveType prim, uint64_t vs_hash, uint64_t ps_hash,
    uint32_t index_count, bool indexed) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled())
    return;
  ++current_.draws;
  // One struct-keyed lookup per draw; the string form is produced at
  // EndFrame only.
  FrameRecord::SurfaceKey key{};
  key.pitch = surface.surface_pitch;
  key.msaa = uint32_t(surface.msaa_samples);
  key.color_write_mask = color_write_mask;
  key.edram_mode = edram_mode;
  // Same normalization as the color targets: with both depth and stencil
  // tests disabled the depth format/base are not part of what the draw
  // reads or writes, so they are 0xFF/0 rather than splitting otherwise
  // identical configurations over a stale base.
  if (depth_stencil_enabled) {
    key.depth_format = uint32_t(depth.depth_format);
    key.depth_base = depth.depth_base | (depth.depth_base_bit_11 << 11);
  } else {
    key.depth_format = 0xFFu;
    key.depth_base = 0;
  }
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    // Normalize: an unwritten target (mask nibble 0) is 0xFF, so it is
    // never confused with a real format (0 = 8_8_8_8) in the key.
    key.color_formats[i] = ((color_write_mask >> (4 * i)) & 0xF)
                               ? uint32_t(color[i].color_format)
                               : 0xFFu;
  }
  ++current_.surface_configs[key];
  const uint32_t prim_value = uint32_t(prim);
  if (prim_value < FrameRecord::kMaxPrimitiveTypes) {
    ++current_.primitive_counts[prim_value];
  } else {
    RecordSkippedLocked(fmt::format("primitive_out_of_range_{}", prim_value));
  }
  ++current_.shader_pairs[{vs_hash, ps_hash}];
  if (current_.samples.size() < FrameRecord::kMaxSamplesPerFrame) {
    FrameRecord::DrawSample sample{};
    sample.prim_type = uint32_t(prim);
    sample.index_count = index_count;
    sample.indexed = indexed;
    sample.vertex_shader_hash = uint32_t(vs_hash);
    sample.pixel_shader_hash = uint32_t(ps_hash);
    sample.surface_pitch = surface.surface_pitch;
    sample.msaa = uint32_t(surface.msaa_samples);
    sample.edram_mode = edram_mode;
    sample.depth_format = key.depth_format;
    sample.depth_base = key.depth_base;
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      // Same normalization as the key: an unwritten target is 0xFF, not a
      // format of 0 that would read as 8_8_8_8.
      sample.color_formats[i] = key.color_formats[i];
      sample.color_bases[i] =
          ((color_write_mask >> (4 * i)) & 0xF)
              ? color[i].color_base | (color[i].color_base_bit_11 << 11)
              : 0;
    }
    current_.samples.push_back(sample);
  }
}

void FrameCensusWriter::RecordResolve(const ResolveInfo &i) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled())
    return;
  ++current_.copies;
  const char *name =
      i.depth ? DepthFormatName(i.format) : ColorFormatName(i.format);
  std::string key = fmt::format(
      "{}_{};src_sel {}@0x{:05X};dest 0x{:08X},pitch {},h {};"
      "dfmt {},endian {},swap {},num {}",
      i.kind, name, i.src_select, i.src_base, i.dest_base, i.dest_pitch,
      i.dest_height, i.dest_format, i.dest_endian, i.dest_swap, i.dest_number);
  ++current_.resolve_kinds[key];
  current_.last_resolve = key;
  if (current_.resolve_log.size() < FrameRecord::kMaxResolveLog) {
    // Prefixed with the draw count at the time, so the resolve's position
    // within the frame is visible.
    current_.resolve_log.push_back(
        fmt::format("after_draw {};{}", current_.draws, key));
  }
  if (std::string_view(name) == "unknown") {
    RecordSkippedLocked(fmt::format("resolve_format_{}", i.format));
  }
}

void FrameCensusWriter::RecordClear(bool depth, uint32_t format) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled())
    return;
  ++current_.clears;
  const char *name = depth ? DepthFormatName(format) : ColorFormatName(format);
  std::string key = fmt::format("clear_{}", name);
  ++current_.resolve_kinds[key];
  if (std::string_view(name) == "unknown") {
    RecordSkippedLocked(fmt::format("clear_format_{}", format));
  }
}

void FrameCensusWriter::RecordSkipped(const std::string &reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled())
    return;
  ++current_.skipped_reasons[reason];
}

void FrameCensusWriter::RecordSwap(uint32_t width, uint32_t height,
                                   uint32_t frontbuffer_ptr) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled())
    return;
  current_.swap_width = width;
  current_.swap_height = height;
  current_.swap_ptr = frontbuffer_ptr;
}

void FrameCensusWriter::EndFrame() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled()) {
    ++next_frame_;
    return;
  }
  FrameRecord &r = current_;
  std::string line =
      fmt::format("{{\"frame\":{},\"guest_time_us\":{},\"swap\":"
                  "[{},{}],\"swap_ptr\":\"0x{:08X}\",\"draws\":{},"
                  "\"copies\":{},\"clears\":{},",
                  r.frame, r.guest_time_us, r.swap_width, r.swap_height,
                  r.swap_ptr, r.draws, r.copies, r.clears);
  std::fwrite(line.data(), 1, line.size(), file_);
  auto write_map = [this](const char *name,
                          const std::unordered_map<std::string, uint32_t> &m) {
    std::fputs(",\"", file_);
    std::fputs(name, file_);
    std::fputs("\":{", file_);
    bool first = true;
    for (const auto &[key, count] : m) {
      if (!first)
        std::fputc(',', file_);
      first = false;
      fmt::print(file_, "\"{}\":{}", key, count);
    }
    std::fputc('}', file_);
  };
  // Surface configs: convert the struct keys to their string form here, at
  // one place and once per distinct configuration per frame. Color formats
  // are in the string; an unwritten target (normalized to 0xFF) prints as
  // "-" so configurations that differ only in formats stay distinct.
  auto rt = [](uint32_t f) { return f == 0xFFu ? "-" : ColorFormatName(f); };
  std::fputs(",\"surfaces\":{", file_);
  {
    bool first = true;
    for (const auto &[key, count] : r.surface_configs) {
      if (!first)
        std::fputc(',', file_);
      first = false;
      fmt::print(file_,
                 "\"{},{};{},{},{},{};{};mask 0x{:04X};mode {};{}@{}\":{}",
                 key.pitch, MsaaName(key.msaa), rt(key.color_formats[0]),
                 rt(key.color_formats[1]), rt(key.color_formats[2]),
                 rt(key.color_formats[3]), DepthFormatName(key.depth_format),
                 key.color_write_mask, key.edram_mode, key.depth_base, count);
    }
    std::fputc('}', file_);
  }
  // Shader pairs: string form from the struct key.
  std::fputs(",\"shader_pairs\":{", file_);
  {
    bool first = true;
    for (const auto &[key, count] : r.shader_pairs) {
      if (!first)
        std::fputc(',', file_);
      first = false;
      fmt::print(file_, "\"{:016X}/{:016X}\":{}", key.vs, key.ps, count);
    }
    std::fputc('}', file_);
  }
  // Primitives: enum value counters.
  std::fputs(",\"primitives\":{", file_);
  {
    bool first = true;
    for (uint32_t i = 0; i < FrameRecord::kMaxPrimitiveTypes; ++i) {
      if (!r.primitive_counts[i])
        continue;
      if (!first)
        std::fputc(',', file_);
      first = false;
      fmt::print(file_, "\"{}\":{}", i, r.primitive_counts[i]);
    }
    std::fputc('}', file_);
  }
  write_map("resolves", r.resolve_kinds);
  write_map("skipped", r.skipped_reasons);
  fmt::print(file_, ",\"last_resolve\":\"{}\",\"resolve_log\":[",
             r.last_resolve);
  {
    bool first = true;
    for (const auto &entry : r.resolve_log) {
      if (!first)
        std::fputc(',', file_);
      first = false;
      fmt::print(file_, "\"{}\"", entry);
    }
  }
  std::fputc(']', file_);
  std::fputs(",\"samples\":[", file_);
  bool first_sample = true;
  for (const auto &s : r.samples) {
    if (!first_sample)
      std::fputc(',', file_);
    first_sample = false;
    fmt::print(
        file_,
        "{{\"prim\":{},\"idx\":{},\"indexed\":{},\"vs\":\"0x{:08X}\",\"ps\":"
        "\"0x{:08X}\","
        "\"pitch\":{},\"msaa\":\"{}\",\"edram_mode\":{},\"depth\":\"{}\","
        "\"depth_base\":{},"
        "\"color\":\"{}@0x{:05X},{},0x{:05X},{},0x{:05X},{},0x{:05X}\"}}",
        s.prim_type, s.index_count, s.indexed, s.vertex_shader_hash,
        s.pixel_shader_hash, s.surface_pitch, MsaaName(s.msaa), s.edram_mode,
        DepthFormatName(s.depth_format), s.depth_base, rt(s.color_formats[0]),
        s.color_bases[0], rt(s.color_formats[1]), s.color_bases[1],
        rt(s.color_formats[2]), s.color_bases[2], rt(s.color_formats[3]),
        s.color_bases[3]);
  }
  std::fputs("]}\n", file_);
  // Flush every frame so the census survives a crash mid-run.
  std::fflush(file_);
  ++next_frame_;
}

void FrameCensusWriter::RecordSkippedLocked(const std::string &reason) {
  ++current_.skipped_reasons[reason];
}

} // namespace rex::graphics::mcla_native
