// MCLA shader container dumper plugin entry points.
//
// Stage 0 of the fh2-style native renderer bring-up: hooks the running game's
// XArchive/shader-create path and writes every 0x102A shader container to
// storage so the offline XenosRecomp -> DXC -> .nfsp chain can build
// mcla_shaders.nfsp. Selected with --gpu_plugin=mcla-dump.
//
// The dump is data (game files) — never committed; only the plugin source is.
#pragma once

#include <memory>
#include <string>

#include "dump/container_dumper.h"
#include "native/command_processor.h"
#include "native/graphics_system.h"

namespace rex::graphics::mcla_dump {

class DumpGraphicsSystem : public mcla_native::MclaNativeGraphicsSystem {
public:
  DumpGraphicsSystem();
  ~DumpGraphicsSystem() override;

  std::string name() const override;

protected:
  void CreateProvider(bool with_presentation) override;
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;

private:
  std::unique_ptr<ContainerDumper> dumper_;
};

} // namespace rex::graphics::mcla_dump
