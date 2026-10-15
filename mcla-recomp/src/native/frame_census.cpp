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
  return format == 0 ? "D24S8" : "D24FS8";
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
  enabled_ = enabled && !path.empty();
  path_ = path;
  if (enabled_ && !file_) {
    OpenFile();
  }
}

void FrameCensusWriter::OpenFile() {
  std::error_code error;
  std::filesystem::create_directories(path_.parent_path(), error);
  file_ = std::fopen(path_.string().c_str(), "wb");
  if (!file_) {
    file_open_failed_ = true;
    REXLOG_ERROR("mcla_native census: cannot open {}", path_.string());
  }
}

void FrameCensusWriter::BeginFrame() {
  std::lock_guard<std::mutex> lock(mutex_);
  current_ = FrameRecord{};
  current_.frame = next_frame_;
  current_.guest_time_us =
      uint64_t(rex::chrono::Clock::QueryHostTickCount() * 1000000ull /
               rex::chrono::Clock::QueryHostTickFrequency());
}

void FrameCensusWriter::RecordSurfaceConfig(
    const reg::RB_SURFACE_INFO &surface, const reg::RB_COLOR_INFO (&color)[4],
    const reg::RB_DEPTH_INFO &depth, uint32_t edram_mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled_)
    return;
  std::string key =
      fmt::format("{},{},{},{},{},{},{},{},{}", surface.surface_pitch,
                  MsaaName(uint32_t(surface.msaa_samples)),
                  ColorFormatName(color[0].color_format),
                  ColorFormatName(color[1].color_format),
                  ColorFormatName(color[2].color_format),
                  ColorFormatName(color[3].color_format),
                  DepthFormatName(uint32_t(depth.depth_format)), edram_mode,
                  depth.depth_base | (depth.depth_base_bit_11 << 11));
  ++current_.surface_configs[key];
}

void FrameCensusWriter::RecordShaderPair(uint64_t vs_hash, uint64_t ps_hash) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled_)
    return;
  std::string key = fmt::format("{:016X}/{:016X}", vs_hash, ps_hash);
  ++current_.shader_pairs[key];
}

void FrameCensusWriter::RecordPrimitive(xenos::PrimitiveType prim) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled_)
    return;
  std::string key = fmt::format("{}", uint32_t(prim));
  ++current_.primitive_types[key];
}

void FrameCensusWriter::RecordResolve(const char *kind, bool depth,
                                      uint32_t format) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled_)
    return;
  ++current_.copies;
  const char *name = depth ? DepthFormatName(format) : ColorFormatName(format);
  std::string key = fmt::format("{}_{}", kind, name);
  ++current_.resolve_kinds[key];
  if (std::string_view(name) == "unknown") {
    RecordSkippedLocked(fmt::format("resolve_format_{}", format));
  }
}

void FrameCensusWriter::RecordClear(bool depth, uint32_t format) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled_)
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
  if (!enabled_)
    return;
  ++current_.skipped_reasons[reason];
}

void FrameCensusWriter::RecordSwap(uint32_t width, uint32_t height) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled_)
    return;
  current_.swap_width = width;
  current_.swap_height = height;
}

void FrameCensusWriter::RecordDrawSample(FrameRecord::DrawSample sample) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled_)
    return;
  ++current_.draws;
  if (current_.samples.size() < FrameRecord::kMaxSamplesPerFrame) {
    current_.samples.push_back(sample);
  }
}

void FrameCensusWriter::EndFrame() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled_ || !file_) {
    ++next_frame_;
    return;
  }
  FrameRecord &r = current_;
  std::string line =
      fmt::format("{{\"frame\":{},\"guest_time_us\":{},\"swap\":"
                  "[{},{}],\"draws\":{},\"copies\":{},\"clears\":{},",
                  r.frame, r.guest_time_us, r.swap_width, r.swap_height,
                  r.draws, r.copies, r.clears);
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
  write_map("surfaces", r.surface_configs);
  write_map("shader_pairs", r.shader_pairs);
  write_map("primitives", r.primitive_types);
  write_map("resolves", r.resolve_kinds);
  write_map("skipped", r.skipped_reasons);
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
        DepthFormatName(s.depth_format), s.depth_base,
        ColorFormatName(s.color_formats[0]), s.color_bases[0],
        ColorFormatName(s.color_formats[1]), s.color_bases[1],
        ColorFormatName(s.color_formats[2]), s.color_bases[2],
        ColorFormatName(s.color_formats[3]), s.color_bases[3]);
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
