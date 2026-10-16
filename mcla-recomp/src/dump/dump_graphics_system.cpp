// Dump graphics system: the census plugin's plumbing + container dumper.
// Reuses the census graphics system wholesale (it already serves the guest's
// D3D protocol and presents blank frames) and starts/stops the dumper around
// the command processor's lifetime.
#include "dump/dump_graphics_system.h"

#include "dump/container_dumper.h"
#include "native/command_processor.h"

#include <fmt/format.h>

#if REX_HAS_VULKAN
#include <rex/ui/vulkan/provider.h>
#endif

#include <rex/system/filesystem.h>
#include <rex/system/kernel_state.h>

namespace rex::graphics::mcla_dump {

DumpGraphicsSystem::DumpGraphicsSystem() = default;

DumpGraphicsSystem::~DumpGraphicsSystem() {
  if (dumper_)
    dumper_->Stop();
}

std::string DumpGraphicsSystem::name() const { return "MclaDump (stage 0)"; }

void DumpGraphicsSystem::CreateProvider(bool with_presentation) {
#if REX_HAS_VULKAN
  provider_ = rex::ui::vulkan::VulkanProvider::Create(true, with_presentation);
#else
  (void)with_presentation;
  provider_ = nullptr;
#endif
}

std::unique_ptr<CommandProcessor> DumpGraphicsSystem::CreateCommandProcessor() {
  // First use of the kernel state: that is where the guest memory is live,
  // so start the dumper here. Output goes next to the game files.
  if (!dumper_) {
    std::filesystem::path dir =
        rex::filesystem::GetRootContentPath() / "contenedores";
    dumper_ = std::make_unique<ContainerDumper>(dir.string());
    dumper_->Start();
  }
  return std::make_unique<mcla_native::MclaNativeCommandProcessor>(
      this, kernel_state_);
}

} // namespace rex::graphics::mcla_dump
