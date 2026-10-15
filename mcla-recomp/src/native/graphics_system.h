// MCLA native renderer graphics system, stage 1.
//
// Owns the census command processor and the host Vulkan presenter. The
// presenter path is the SDK's Vulkan provider (the same one the Xenos plugin
// uses), so what appears on screen while the census runs is the presenter's
// blank guest-output surface; the guest's D3D protocol (swap packets, gamma
// ramps, interrupts) is served identically to the Xenos plugin's.
#pragma once

#include <memory>
#include <string>

#include <rex/graphics/graphics_system.h>

namespace rex::graphics::mcla_native {

class MclaNativeCommandProcessor;

class MclaNativeGraphicsSystem : public GraphicsSystem {
public:
  MclaNativeGraphicsSystem();
  ~MclaNativeGraphicsSystem() override;

  static bool IsAvailable() { return true; }

  std::string name() const override;

protected:
  void CreateProvider(bool with_presentation) override;

private:
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override;
};

} // namespace rex::graphics::mcla_native
