// MCLA native renderer stage 2.2: fullscreen present pass (swap).
//
// A single triangle covering the render area samples the registered resolve
// texture and writes it to the presenter's guest output image. GLSL -> SPIR-V
// at first use via the SDK's glslang path (linked from the SDK's targets,
// src/graphics/CMakeLists.txt:146-157); the shader is compiled once and kept
// for the process lifetime.
#pragma once

#include <rex/ui/vulkan/api.h>
#include <rex/ui/vulkan/presenter.h>

#include <vulkan/vulkan.h>

namespace rex::graphics::mcla_native {

class PresentPass {
public:
  PresentPass() = default;
  ~PresentPass();

  PresentPass(const PresentPass &) = delete;
  PresentPass &operator=(const PresentPass &) = delete;

  bool Initialize(const ui::vulkan::VulkanDevice *device);
  void Shutdown();

  // Records the fullscreen draw sampling src_view into the open render pass
  // on command_buffer. src_view must be in SHADER_READ_ONLY_OPTIMAL. When
  // rb_swap is set, red and blue are swapped in the fragment shader
  // (specialized pipeline).
  bool Draw(const ui::vulkan::VulkanDevice *device,
            VkCommandBuffer command_buffer, VkImageView src_view, bool rb_swap,
            uint32_t width, uint32_t height);

  // One descriptor set per distinct src view, small fixed ring (no per-frame
  // heap allocation); the presenter mailbox cycles at most a few views.
  static constexpr uint32_t kDescriptorRingSize = 4;
  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_sets_[kDescriptorRingSize] = {};
  VkImageView ring_views_[kDescriptorRingSize] = {};
  uint32_t ring_next_ = 0;

  const ui::vulkan::VulkanDevice *device_ = nullptr;

  VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkPipeline pipeline_rb_swap_ = VK_NULL_HANDLE;
  VkShaderModule vertex_shader_ = VK_NULL_HANDLE;
  VkShaderModule fragment_shader_ = VK_NULL_HANDLE;
  VkShaderModule fragment_shader_rb_swap_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;

private:
  bool EnsurePipelines(const ui::vulkan::VulkanDevice *device);
  bool CompileSwapShaders(const ui::vulkan::VulkanDevice *device);
};

} // namespace rex::graphics::mcla_native
