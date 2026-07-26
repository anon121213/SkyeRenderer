#include "skypch.h"

#include "SkyRHI/Types.h"
#include "VulkanDevice.h"
#include "VulkanPipeline.h"
#include "VulkanShaderModule.h"
#include "VulkanTranslate.h"

VulkanPipeline::VulkanPipeline(const VulkanDevice& device,
                               const Sky::RHI::GraphicsPipelineDesc& desc,
                               VkShaderModule vertexShader, VkShaderModule fragmentShader,
                               VkDescriptorSetLayout descriptorSetLayout)
{
  m_Device = device.handle();

  VkPipelineShaderStageCreateInfo vertStage{};
  vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertStage.module = vertexShader;
  vertStage.pName = "main";

  VkPipelineShaderStageCreateInfo fragStage{};
  fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragStage.module = fragmentShader;
  fragStage.pName = "main";

  const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = { vertStage, fragStage };

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = desc.vertexStride;
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::vector<VkVertexInputAttributeDescription> attrs;
  for (const auto& a : desc.vertexAttributes)
  {
    VkVertexInputAttributeDescription va{};
    va.location = a.location;
    va.binding = 0;
    va.format = toVkFormat(a.format);
    va.offset = a.offset;
    attrs.push_back(va);
  }

  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount   = desc.vertexStride > 0 ? 1 : 0;
  vertexInput.pVertexBindingDescriptions      = desc.vertexStride > 0 ? &binding : nullptr;
  vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
  vertexInput.pVertexAttributeDescriptions    = attrs.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizer.lineWidth = 1.0f;
  rasterizer.depthBiasEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.blendEnable = desc.additiveBlend ? VK_TRUE : VK_FALSE;
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                      | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable  = desc.depthTest  ? VK_TRUE : VK_FALSE;
  depthStencil.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable = VK_FALSE;

  const bool hasDepth = desc.depthFormat != Sky::RHI::Format::Undefined;
  const bool hasColor = desc.colorFormat != Sky::RHI::Format::Undefined;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable   = VK_FALSE;
  colorBlending.attachmentCount = hasColor ? 1 : 0;
  colorBlending.pAttachments    = hasColor ? &colorBlendAttachment : nullptr;

  const std::array<VkDynamicState, 2> dynamicStates = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR
  };

  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  VkPushConstantRange pcRange{};
  pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pcRange.offset = 0;
  pcRange.size = desc.pushConstantSize;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = descriptorSetLayout != VK_NULL_HANDLE ? 1 : 0;
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayout != VK_NULL_HANDLE ? &descriptorSetLayout : nullptr;
  pipelineLayoutInfo.pushConstantRangeCount = desc.pushConstantSize > 0 ? 1 : 0;
  pipelineLayoutInfo.pPushConstantRanges = desc.pushConstantSize > 0 ? &pcRange : nullptr;

  SKY_RHI_VK_CHECK(vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_Layout),
               "Failed to create pipeline layout");

  VkFormat colorFmt = toVkFormat(desc.colorFormat);
  VkFormat depthFmt = toVkFormat(desc.depthFormat);

  VkPipelineRenderingCreateInfoKHR renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
  renderingInfo.colorAttachmentCount    = hasColor ? 1 : 0;
  renderingInfo.pColorAttachmentFormats = hasColor ? &colorFmt : nullptr;
  renderingInfo.depthAttachmentFormat   = hasDepth ? depthFmt : VK_FORMAT_UNDEFINED;

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
  pipelineInfo.pStages = shaderStages.data();
  pipelineInfo.pVertexInputState = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = nullptr;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDepthStencilState = hasDepth ? &depthStencil : nullptr;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = m_Layout;
  pipelineInfo.renderPass = VK_NULL_HANDLE;
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineInfo.basePipelineIndex = -1;
  pipelineInfo.pNext = &renderingInfo;

  SKY_RHI_VK_CHECK(vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline),
               "Failed to create graphics pipeline");

  SKY_RHI_INFO("Graphics pipeline created");
}

VulkanPipeline::~VulkanPipeline() noexcept
{
  vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
  vkDestroyPipelineLayout(m_Device, m_Layout, nullptr);
}
