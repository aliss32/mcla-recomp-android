// MCLA native renderer graphics system, stage 2.1.
//
// Stage 2.1 inherits the SDK's Vulkan provider and presenter wiring: the
// presenter path is the SDK's Vulkan provider (the same one the Xenos plugin
// uses), so IssueSwap's RefreshGuestOutput callback can write the presenter's
// guest-output image the same way the Xenos plugin does. The guest's D3D
// protocol (swap packets, gamma ramps, interrupts) is served by the base
// GraphicsSystem identically to the Xenos plugin's.
#pragma once

#include <memory>
#include <string>

#include <rex/graphics/vulkan/graphics_system.h>

namespace rex::graphics::mcla_native {

class MclaNativeCommandProcessor;

class MclaNativeGraphicsSystem : public vulkan::VulkanGraphicsSystem {
public:
  MclaNativeGraphicsSystem();
  ~MclaNativeGraphicsSystem() override;

  static bool IsAvailable() { return true; }

  std::string name() const override;

private:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;
};

} // namespace rex::graphics::mcla_native
