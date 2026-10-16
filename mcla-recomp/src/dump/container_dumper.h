// Shader container dumper: walks the game's committed memory for 0x102A
// shader containers and persists them, deduplicated by hash of contents.
//
// Strategy mirrors fh2-recompiled's in-game extraction (their app/src hooks
// wrap the game's own CreateShader calls) but is game-agnostic here: it scans
// the memory map periodically, so it needs no MCLA address table.
#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace rex::graphics::mcla_dump {

class ContainerDumper {
public:
  explicit ContainerDumper(std::string output_dir);
  ~ContainerDumper();

  void Start();
  void Stop();

  uint32_t dumped() const { return dumped_.load(); }

private:
  void ScanLoop();

  std::string output_dir_;
  std::atomic<bool> running_{false};
  std::atomic<uint32_t> dumped_{0};
  std::thread thread_;
};

} // namespace rex::graphics::mcla_dump
