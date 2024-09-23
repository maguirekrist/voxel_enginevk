// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vulkan/vulkan_core.h>

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

	VkCommandPoolCreateInfo command_pool_create_info(uint32_t graphicsQueueFamily, VkCommandPoolCreateFlags createFlags);
	VkCommandBufferAllocateInfo command_buffer_allocate_info(VkCommandPool commandPool, uint32_t bufferCount);

	VkRenderPassBeginInfo render_pass_begin_info(VkRenderPass renderPass, VkExtent2D windowExtent, VkFramebuffer frameBuffer);
	VkCommandBufferBeginInfo init_command_buffer();
	VkCommandBufferInheritanceInfo init_inheritance_info(VkRenderPass renderPass);

	VkWriteDescriptorSet write_descriptor_buffer(VkDescriptorType descriptorType, VkDescriptorSet dSet, VkDescriptorBufferInfo* bufferInfo, uint32_t binding);
	VkDescriptorSetLayoutBinding descriptorset_layout_binding(VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t binding);
	VkPushConstantRange pushconstrant_range(size_t size, VkShaderStageFlags accessFlags);

	VkFenceCreateInfo fence_create_info(VkFenceCreateFlags createFlags);
	VkSemaphoreCreateInfo semaphore_create_info();

	VkBufferCreateInfo buffer_create_info(VkDeviceSize size, VkBufferUsageFlags usageFlags);

	VkImageCreateInfo image_create_info(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);
	VkImageViewCreateInfo imageview_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);
}

