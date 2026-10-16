// MCLA native renderer stage 2.2: fullscreen present pass implementation.
#include "present_pass.h"

#include <mutex>
#include <vector>

#include <rex/logging.h>
#include <rex/ui/vulkan/util.h>

#include <glslang/Include/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

namespace rex::graphics::mcla_native {

namespace {

constexpr char kSwapVertexShader[] = R"(
#version 450
layout(location = 0) out vec2 out_uv;
void main() {
  // Fullscreen triangle: covers the viewport with UVs 0..1.
  vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  out_uv = vec2(uv.x, 1.0 - uv.y);
  gl_Position = vec4(uv * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
)";

constexpr char kSwapFragmentShader[] = R"(
#version 450
layout(binding = 0) uniform sampler2D src_texture;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;
void main() {
  out_color = texture(src_texture, in_uv);
}
)";

constexpr char kSwapFragmentShaderRbSwap[] = R"(
#version 450
layout(binding = 0) uniform sampler2D src_texture;
layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;
void main() {
  vec4 c = texture(src_texture, in_uv);
  out_color = vec4(c.b, c.g, c.r, c.a);
}
)";

// glslang's required (mostly unused) default resource limits. This is the
// DefaultTBuiltInResource table from the pinned glslang revision
// (f4f1d8a352ca1908943aea2ad8c54b39b4879080, StandAlone/ResourceLimits.cpp);
// in that revision the table lives in the StandAlone build target, which the
// mcla-native link set does not pull in, and Public/ResourceLimits.h does not
// exist - so the table is reproduced here.
TBuiltInResource GetGlslangResource() {
  TBuiltInResource resource = {};
  resource.maxLights = 32;
  resource.maxClipPlanes = 6;
  resource.maxTextureUnits = 32;
  resource.maxTextureCoords = 32;
  resource.maxVertexAttribs = 64;
  resource.maxVertexUniformComponents = 4096;
  resource.maxVaryingFloats = 64;
  resource.maxVertexTextureImageUnits = 32;
  resource.maxCombinedTextureImageUnits = 80;
  resource.maxTextureImageUnits = 32;
  resource.maxFragmentUniformComponents = 4096;
  resource.maxDrawBuffers = 32;
  resource.maxVertexUniformVectors = 128;
  resource.maxVaryingVectors = 8;
  resource.maxFragmentUniformVectors = 16;
  resource.maxVertexOutputVectors = 16;
  resource.maxFragmentInputVectors = 15;
  resource.minProgramTexelOffset = -8;
  resource.maxProgramTexelOffset = 7;
  resource.maxClipDistances = 8;
  resource.maxComputeWorkGroupCountX = 65535;
  resource.maxComputeWorkGroupCountY = 65535;
  resource.maxComputeWorkGroupCountZ = 65535;
  resource.maxComputeWorkGroupSizeX = 1024;
  resource.maxComputeWorkGroupSizeY = 1024;
  resource.maxComputeWorkGroupSizeZ = 64;
  resource.maxComputeUniformComponents = 1024;
  resource.maxComputeTextureImageUnits = 16;
  resource.maxComputeImageUniforms = 8;
  resource.maxComputeAtomicCounters = 8;
  resource.maxComputeAtomicCounterBuffers = 1;
  resource.maxVaryingComponents = 60;
  resource.maxVertexOutputComponents = 64;
  resource.maxGeometryInputComponents = 64;
  resource.maxGeometryOutputComponents = 128;
  resource.maxFragmentInputComponents = 128;
  resource.maxImageUnits = 8;
  resource.maxCombinedImageUnitsAndFragmentOutputs = 8;
  resource.maxCombinedShaderOutputResources = 8;
  resource.maxImageSamples = 0;
  resource.maxVertexImageUniforms = 0;
  resource.maxTessControlImageUniforms = 0;
  resource.maxTessEvaluationImageUniforms = 0;
  resource.maxGeometryImageUniforms = 0;
  resource.maxFragmentImageUniforms = 8;
  resource.maxCombinedImageUniforms = 8;
  resource.maxGeometryTextureImageUnits = 16;
  resource.maxGeometryOutputVertices = 256;
  resource.maxGeometryTotalOutputComponents = 1024;
  resource.maxGeometryUniformComponents = 1024;
  resource.maxGeometryVaryingComponents = 64;
  resource.maxTessControlInputComponents = 128;
  resource.maxTessControlOutputComponents = 128;
  resource.maxTessControlTextureImageUnits = 16;
  resource.maxTessControlUniformComponents = 1024;
  resource.maxTessControlTotalOutputComponents = 4096;
  resource.maxTessEvaluationInputComponents = 128;
  resource.maxTessEvaluationOutputComponents = 128;
  resource.maxTessEvaluationTextureImageUnits = 16;
  resource.maxTessEvaluationUniformComponents = 1024;
  resource.maxTessPatchComponents = 120;
  resource.maxPatchVertices = 32;
  resource.maxTessGenLevel = 64;
  resource.maxViewports = 16;
  resource.maxVertexAtomicCounters = 0;
  resource.maxTessControlAtomicCounters = 0;
  resource.maxTessEvaluationAtomicCounters = 0;
  resource.maxGeometryAtomicCounters = 0;
  resource.maxFragmentAtomicCounters = 8;
  resource.maxCombinedAtomicCounters = 8;
  resource.maxAtomicCounterBindings = 1;
  resource.maxVertexAtomicCounterBuffers = 0;
  resource.maxTessControlAtomicCounterBuffers = 0;
  resource.maxTessEvaluationAtomicCounterBuffers = 0;
  resource.maxGeometryAtomicCounterBuffers = 0;
  resource.maxFragmentAtomicCounterBuffers = 1;
  resource.maxCombinedAtomicCounterBuffers = 1;
  resource.maxAtomicCounterBufferSize = 16384;
  resource.maxTransformFeedbackBuffers = 4;
  resource.maxTransformFeedbackInterleavedComponents = 64;
  resource.maxCullDistances = 8;
  resource.maxCombinedClipAndCullDistances = 8;
  resource.maxSamples = 4;
  resource.maxMeshOutputVerticesNV = 256;
  resource.maxMeshOutputPrimitivesNV = 512;
  resource.maxMeshWorkGroupSizeX_NV = 32;
  resource.maxMeshWorkGroupSizeY_NV = 1;
  resource.maxMeshWorkGroupSizeZ_NV = 1;
  resource.maxTaskWorkGroupSizeX_NV = 32;
  resource.maxTaskWorkGroupSizeY_NV = 1;
  resource.maxTaskWorkGroupSizeZ_NV = 1;
  resource.maxMeshViewCountNV = 4;
  resource.maxDualSourceDrawBuffersEXT = 1;
  resource.limits.nonInductiveForLoops = 1;
  resource.limits.whileLoops = 1;
  resource.limits.doWhileLoops = 1;
  resource.limits.generalUniformIndexing = 1;
  resource.limits.generalAttributeMatrixVectorIndexing = 1;
  resource.limits.generalVaryingIndexing = 1;
  resource.limits.generalSamplerIndexing = 1;
  resource.limits.generalVariableIndexing = 1;
  resource.limits.generalConstantMatrixVectorIndexing = 1;
  return resource;
}

bool CompileGlsl(EShLanguage stage, const char *source,
                 std::vector<uint32_t> &spirv_out) {
  static std::once_flag glslang_init_once;
  std::call_once(glslang_init_once, []() { glslang::InitializeProcess(); });
  glslang::TShader shader(stage);
  shader.setStrings(&source, 1);
  shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan,
                     450);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
  const EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
  if (!shader.parse(&GetGlslangResource(), 450, false, messages)) {
    REXLOG_WARN("stage2.2: glslang parse failed: {}",
                shader.getInfoLog() ? shader.getInfoLog() : "");
    return false;
  }
  glslang::TProgram program;
  program.addShader(&shader);
  if (!program.link(messages)) {
    REXLOG_WARN("stage2.2: glslang link failed: {}",
                program.getInfoLog() ? program.getInfoLog() : "");
    return false;
  }
  const glslang::TIntermediate *intermediate = program.getIntermediate(stage);
  if (!intermediate) {
    REXLOG_WARN("stage2.2: glslang produced no intermediate");
    return false;
  }
  glslang::SpvOptions options = {};
  options.disableOptimizer = true;
  glslang::GlslangToSpv(*intermediate, spirv_out, &options);
  return !spirv_out.empty();
}

} // namespace

PresentPass::~PresentPass() { Shutdown(); }

bool PresentPass::Initialize(const ui::vulkan::VulkanDevice *device) {
  device_ = device;
  const VulkanDevice::Functions &dfn = device_->functions();
  const VkDevice dev = device_->device();

  VkDescriptorSetLayoutBinding binding = {};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo layout_info = {};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.bindingCount = 1;
  layout_info.pBindings = &binding;
  if (dfn.vkCreateDescriptorSetLayout(dev, &layout_info, nullptr,
                                      &descriptor_set_layout_) != VK_SUCCESS) {
    REXLOG_WARN("stage2.2: present pass descriptor layout failed");
    return false;
  }

  VkPipelineLayoutCreateInfo pipe_layout_info = {};
  pipe_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipe_layout_info.setLayoutCount = 1;
  pipe_layout_info.pSetLayouts = &descriptor_set_layout_;
  if (dfn.vkCreatePipelineLayout(dev, &pipe_layout_info, nullptr,
                                 &pipeline_layout_) != VK_SUCCESS) {
    REXLOG_WARN("stage2.2: present pass pipeline layout failed");
    return false;
  }

  VkSamplerCreateInfo sampler_info = {};
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (dfn.vkCreateSampler(dev, &sampler_info, nullptr, &sampler_) !=
      VK_SUCCESS) {
    REXLOG_WARN("stage2.2: present pass sampler failed");
    return false;
  }

  if (!CompileSwapShaders(device)) {
    return false;
  }
  // Pipelines are created lazily against the first render pass usage in
  // Draw (dynamic rendering needs VK_KHR_dynamic_rendering, which the
  // device supports on Vulkan 1.3).
  return true;
}

bool PresentPass::CompileSwapShaders(const ui::vulkan::VulkanDevice *device) {
  const VulkanDevice::Functions &dfn = device_->functions();
  const VkDevice dev = device_->device();
  std::vector<uint32_t> vs_spirv, fs_spirv, fs_spirv_rb;
  if (!CompileGlsl(EShLangVertex, kSwapVertexShader, vs_spirv) ||
      !CompileGlsl(EShLangFragment, kSwapFragmentShader, fs_spirv) ||
      !CompileGlsl(EShLangFragment, kSwapFragmentShaderRbSwap, fs_spirv_rb)) {
    return false;
  }
  vertex_shader_ = ui::vulkan::util::CreateShaderModule(
      device, reinterpret_cast<const uint32_t *>(vs_spirv.data()),
      vs_spirv.size() * sizeof(uint32_t));
  fragment_shader_ = ui::vulkan::util::CreateShaderModule(
      device, reinterpret_cast<const uint32_t *>(fs_spirv.data()),
      fs_spirv.size() * sizeof(uint32_t));
  fragment_shader_rb_swap_ = ui::vulkan::util::CreateShaderModule(
      device, reinterpret_cast<const uint32_t *>(fs_spirv_rb.data()),
      fs_spirv_rb.size() * sizeof(uint32_t));
  if (vertex_shader_ == VK_NULL_HANDLE || fragment_shader_ == VK_NULL_HANDLE ||
      fragment_shader_rb_swap_ == VK_NULL_HANDLE) {
    REXLOG_WARN("stage2.2: present pass shader module creation failed");
    return false;
  }
  return true;
}

bool PresentPass::EnsurePipelines(const ui::vulkan::VulkanDevice *device) {
  if (pipeline_ != VK_NULL_HANDLE) {
    return true;
  }
  const VulkanDevice::Functions &dfn = device_->functions();
  const VkDevice dev = device_->device();

  // Dynamic rendering: no VkRenderPass; the caller is inside
  // vkCmdBeginRendering with COLOR_ATTACHMENT_OPTIMAL layout.
  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertex_shader_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo vertex_input = {};
  vertex_input.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
  input_assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport_state = {};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster = {};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample = {};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
  depth_stencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

  VkPipelineColorBlendAttachmentState blend_attachment = {};
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend = {};
  blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;

  VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT,
                                      VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state = {};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount = 2;
  dynamic_state.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo pipeline_info = {};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = stages;
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &raster;
  pipeline_info.pMultisampleState = &multisample;
  pipeline_info.pDepthStencilState = &depth_stencil;
  pipeline_info.pColorBlendState = &blend;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = pipeline_layout_;
  // Dynamic rendering (Vulkan 1.3).
  pipeline_info.renderPass = VK_NULL_HANDLE;
  VkPipelineRenderingCreateInfo rendering_create_info = {};
  rendering_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering_create_info.colorAttachmentCount = 1;
  rendering_create_info.pColorAttachmentFormats =
      &ui::vulkan::VulkanPresenter::kGuestOutputFormat;
  pipeline_info.pNext = &rendering_create_info;

  if (dfn.vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipeline_info,
                                    nullptr, &pipeline_) != VK_SUCCESS) {
    REXLOG_WARN("stage2.2: present pipeline creation failed");
    return false;
  }
  stages[1].module = fragment_shader_rb_swap_;
  if (dfn.vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipeline_info,
                                    nullptr,
                                    &pipeline_rb_swap_) != VK_SUCCESS) {
    REXLOG_WARN("stage2.2: present rb-swap pipeline creation failed");
    dfn.vkDestroyPipeline(dev, pipeline_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

bool PresentPass::Draw(const ui::vulkan::VulkanDevice *device,
                       VkCommandBuffer command_buffer, VkImageView src_view,
                       bool rb_swap, uint32_t width, uint32_t height) {
  if (!EnsurePipelines(device_)) {
    return false;
  }
  const VulkanDevice::Functions &dfn = device_->functions();
  const VkDevice dev = device_->device();

  VkDescriptorPoolSize pool_size = {};
  pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_size.descriptorCount = kDescriptorRingSize;
  if (descriptor_pool_ == VK_NULL_HANDLE) {
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = kDescriptorRingSize;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (dfn.vkCreateDescriptorPool(dev, &pool_info, nullptr,
                                   &descriptor_pool_) != VK_SUCCESS) {
      REXLOG_WARN("stage2.2: present descriptor pool failed");
      return false;
    }
  }

  // Find an existing set for this view or reuse the ring slot.
  VkDescriptorSet set = VK_NULL_HANDLE;
  for (uint32_t i = 0; i < kDescriptorRingSize; ++i) {
    if (descriptor_sets_[i] != VK_NULL_HANDLE && ring_views_[i] == src_view) {
      set = descriptor_sets_[i];
      break;
    }
  }
  if (set == VK_NULL_HANDLE) {
    set = descriptor_sets_[ring_next_];
    ring_views_[ring_next_] = src_view;
    ring_next_ = (ring_next_ + 1) % kDescriptorRingSize;
    if (set == VK_NULL_HANDLE) {
      VkDescriptorSetAllocateInfo alloc_info = {};
      alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      alloc_info.descriptorPool = descriptor_pool_;
      alloc_info.descriptorSetCount = 1;
      alloc_info.pSetLayouts = &descriptor_set_layout_;
      if (dfn.vkAllocateDescriptorSets(dev, &alloc_info, &set) != VK_SUCCESS) {
        REXLOG_WARN("stage2.2: present descriptor allocation failed");
        return false;
      }
      descriptor_sets_[(ring_next_ + kDescriptorRingSize - 1) %
                       kDescriptorRingSize] = set;
    }
    VkDescriptorImageInfo image_info = {};
    image_info.sampler = sampler_;
    image_info.imageView = src_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    dfn.vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
  }

  VkViewport viewport = {0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
  VkRect2D scissor = {{0, 0}, {width, height}};
  dfn.vkCmdSetViewport(command_buffer, 0, 1, &viewport);
  dfn.vkCmdSetScissor(command_buffer, 0, 1, &scissor);
  dfn.vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        rb_swap ? pipeline_rb_swap_ : pipeline_);
  dfn.vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_layout_, 0, 1, &set, 0, nullptr);
  dfn.vkCmdDraw(command_buffer, 3, 1, 0, 0);
  return true;
}

void PresentPass::Shutdown() {
  if (!device_) {
    return;
  }
  const VulkanDevice::Functions &dfn = device_->functions();
  const VkDevice dev = device_->device();
  if (pipeline_ != VK_NULL_HANDLE)
    dfn.vkDestroyPipeline(dev, pipeline_, nullptr);
  if (pipeline_rb_swap_ != VK_NULL_HANDLE)
    dfn.vkDestroyPipeline(dev, pipeline_rb_swap_, nullptr);
  if (vertex_shader_ != VK_NULL_HANDLE)
    dfn.vkDestroyShaderModule(dev, vertex_shader_, nullptr);
  if (fragment_shader_ != VK_NULL_HANDLE)
    dfn.vkDestroyShaderModule(dev, fragment_shader_, nullptr);
  if (fragment_shader_rb_swap_ != VK_NULL_HANDLE)
    dfn.vkDestroyShaderModule(dev, fragment_shader_rb_swap_, nullptr);
  if (pipeline_layout_ != VK_NULL_HANDLE)
    dfn.vkDestroyPipelineLayout(dev, pipeline_layout_, nullptr);
  if (descriptor_set_layout_ != VK_NULL_HANDLE)
    dfn.vkDestroyDescriptorSetLayout(dev, descriptor_set_layout_, nullptr);
  if (sampler_ != VK_NULL_HANDLE)
    dfn.vkDestroySampler(dev, sampler_, nullptr);
  if (descriptor_pool_ != VK_NULL_HANDLE)
    dfn.vkDestroyDescriptorPool(dev, descriptor_pool_, nullptr);
  pipeline_ = VK_NULL_HANDLE;
  pipeline_rb_swap_ = VK_NULL_HANDLE;
  vertex_shader_ = VK_NULL_HANDLE;
  fragment_shader_ = VK_NULL_HANDLE;
  fragment_shader_rb_swap_ = VK_NULL_HANDLE;
  pipeline_layout_ = VK_NULL_HANDLE;
  descriptor_set_layout_ = VK_NULL_HANDLE;
  sampler_ = VK_NULL_HANDLE;
  descriptor_pool_ = VK_NULL_HANDLE;
  for (uint32_t i = 0; i < kDescriptorRingSize; ++i) {
    descriptor_sets_[i] = VK_NULL_HANDLE;
    ring_views_[i] = VK_NULL_HANDLE;
  }
  ring_next_ = 0;
  device_ = nullptr;
}

} // namespace rex::graphics::mcla_native
