// MCLA native renderer graphics system implementation, stage 2.1.
#include "graphics_system.h"

#include <fmt/format.h>

#include "command_processor.h"

#include <rex/system/kernel_state.h>

namespace rex::graphics::mcla_native {

MclaNativeGraphicsSystem::MclaNativeGraphicsSystem() = default;

MclaNativeGraphicsSystem::~MclaNativeGraphicsSystem() = default;

std::string MclaNativeGraphicsSystem::name() const {
  return "MclaNative (stage2.1 presenter test)";
}

std::unique_ptr<CommandProcessor>
MclaNativeGraphicsSystem::CreateCommandProcessor() {
  return std::make_unique<MclaNativeCommandProcessor>(
      static_cast<vulkan::VulkanGraphicsSystem *>(this), kernel_state_);
}

} // namespace rex::graphics::mcla_native
