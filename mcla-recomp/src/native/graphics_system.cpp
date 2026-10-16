// MCLA native renderer graphics system implementation, stage 1.
#include "graphics_system.h"

#include <fmt/format.h>

#include "command_processor.h"

#if REX_HAS_VULKAN
#include <rex/ui/vulkan/provider.h>
#endif

#include <rex/system/kernel_state.h>

namespace rex::graphics::mcla_native {

MclaNativeGraphicsSystem::MclaNativeGraphicsSystem() = default;

MclaNativeGraphicsSystem::~MclaNativeGraphicsSystem() = default;

std::string MclaNativeGraphicsSystem::name() const {
  return "MclaNative (census)";
}

void MclaNativeGraphicsSystem::CreateProvider(bool with_presentation) {
#if REX_HAS_VULKAN
  // The SDK Vulkan provider covers instance/device creation and the
  // presenter; the census renders nothing, but the presenter must exist so
  // the app's window lifecycle and ImGui overlay keep working.
  provider_ = rex::ui::vulkan::VulkanProvider::Create(true, with_presentation);
#else
  (void)with_presentation;
  provider_ = nullptr;
#endif
}

std::unique_ptr<CommandProcessor>
MclaNativeGraphicsSystem::CreateCommandProcessor() {
  return std::make_unique<MclaNativeCommandProcessor>(this, kernel_state_);
}

} // namespace rex::graphics::mcla_native
