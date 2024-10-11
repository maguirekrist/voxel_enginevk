// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vulkan/vulkan_core.h>

// Use enum class for your configuration flags
enum class ClearFlags : uint8_t {
    None      = 0,         // 00
    Color     = 1 << 0,    // 01
    Depth     = 1 << 1     // 10
};

// Allow combining flags using bitwise OR
constexpr ClearFlags operator|(ClearFlags lhs, ClearFlags rhs) {
    return static_cast<ClearFlags>(
        static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

// Allow checking flags using bitwise AND
constexpr bool operator&(ClearFlags lhs, ClearFlags rhs) {
    return (static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs)) != 0;
}

namespace vkinit {

	//vulkan init code goes here
	VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_info(VkShaderStageFlagBits stageBits, VkShaderModule shaderModule);
	VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info();
	VkPipelineInputAssemblyStateCreateInfo input_assembly_create_info(VkPrimitiveTopology topology);
	VkPipelineRasterizationStateCreateInfo rasterization_state_create_info(VkPolygonMode polygonMode);
	VkPipelineMultisampleStateCreateInfo multisampling_state_create_info();
	VkPipelineColorBlendAttachmentState color_blend_attachment_state();
	VkPipelineColorBlendAttachmentState color_blend_attachment_state_blending();
	VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info(bool bDepthTest, bool bDepthWrite, VkCompareOp compareOp);
	VkPipelineLayoutCreateInfo pipeline_layout_create_info();

	VkCommandPoolCreateInfo command_pool_create_info(uint32_t graphicsQueueFamily, VkCommandPoolCreateFlags createFlags = 0);
	VkCommandBufferAllocateInfo command_buffer_allocate_info(VkCommandPool commandPool, uint32_t bufferCount);
	VkCommandBufferBeginInfo command_buffer_begin_info(VkCommandBufferUsageFlags flags = 0);
	VkSubmitInfo submit_info(VkCommandBuffer* cmd);

	VkRenderPassBeginInfo render_pass_begin_info(VkRenderPass renderPass, VkExtent2D windowExtent, VkFramebuffer frameBuffer, ClearFlags clearFlags = ClearFlags::Color | ClearFlags::Depth);
	VkCommandBufferBeginInfo init_command_buffer();
	VkCommandBufferInheritanceInfo init_inheritance_info(VkRenderPass renderPass);

	VkWriteDescriptorSet write_descriptor_buffer(VkDescriptorType descriptorType, VkDescriptorSet dSet, VkDescriptorBufferInfo* bufferInfo, uint32_t binding);
	VkDescriptorSetLayoutBinding descriptorset_layout_binding(VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t binding);
	VkPushConstantRange pushconstrant_range(size_t size, VkShaderStageFlags accessFlags);

	VkSamplerCreateInfo sampler_create_info();

	VkAttachmentDescription attachment_description(VkFormat format, VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp, VkImageLayout initialLayout, VkImageLayout finalLayout);

	VkFenceCreateInfo fence_create_info(VkFenceCreateFlags createFlags = 0);
	VkSemaphoreCreateInfo semaphore_create_info();

	VkBufferCreateInfo buffer_create_info(VkDeviceSize size, VkBufferUsageFlags usageFlags);

	VkImageCreateInfo image_create_info(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);
	VkImageViewCreateInfo imageview_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);
}

