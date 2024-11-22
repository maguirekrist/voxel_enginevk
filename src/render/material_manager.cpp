#include "material_manager.h"
#include <vk_util.h>
#include <vk_initializers.h>
#include <vk_pipeline_builder.h>
#include <vk_mesh.h>
#include <vk_engine.h>

#include <scenes/blueprint_builder_scene.h>


Material* MaterialManager::get_material(const std::string &name)
{
    //search for the object, and return nullptr if not found
	auto it = _materials.find(name);
	if (it == _materials.end()) {
		return nullptr;
	}
	else {
		return &(*it).second;
	}
}

void MaterialManager::cleanup()
{
	for(auto& material : _materials) {
		
		vkDestroyPipeline(VulkanEngine::instance()._device, material.second.pipeline, nullptr);
		vkDestroyPipelineLayout(VulkanEngine::instance()._device, material.second.pipelineLayout, nullptr);
	}
}


void MaterialManager::build_graphics_pipeline(
	const std::vector<std::shared_ptr<Resource>>& resources,
	const std::vector<PushConstant>& pConstants,
	const PipelineMetadata&& metadata,
	const std::string& vertex_shader,
	const std::string& fragment_shader, 
	const std::string& name)
{
	if (_materials.contains(name)) {
		return;
	}

	VkShaderModule vertexShader;
	vkutil::load_shader_module(vertex_shader, VulkanEngine::instance()._device, &vertexShader);

	VkShaderModule fragmentShader;
	vkutil::load_shader_module(fragment_shader, VulkanEngine::instance()._device, &fragmentShader);

	std::vector<VkDescriptorSet> descriptorSets;
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;

	for (const auto& resource : resources) {
		if (resource->type == Resource::IMAGE) {
			VkDescriptorSetLayout imageSetLayout;
			VkDescriptorSet imageSet;
			VkDescriptorImageInfo imageInfo{
				.sampler = VK_NULL_HANDLE,
				.imageView = resource->value.image.view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL
			};
			vkutil::DescriptorBuilder::begin(&VulkanEngine::instance()._descriptorLayoutCache, &VulkanEngine::instance()._descriptorAllocator)
				.bind_image(0, &imageInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
				.build(imageSet, imageSetLayout);
			descriptorSets.push_back(imageSet);
			descriptorSetLayouts.push_back(imageSetLayout);
		}

		if (resource->type == Resource::BUFFER) {
			VkDescriptorSetLayout bufferSetLayout;
			VkDescriptorSet bufferSet;
			VkDescriptorBufferInfo bufferInfo{
				.buffer = resource->value.buffer._buffer,
				.offset = 0,
				.range = resource->value.buffer._size
			};
			vkutil::DescriptorBuilder::begin(&VulkanEngine::instance()._descriptorLayoutCache, &VulkanEngine::instance()._descriptorAllocator)
				.bind_buffer(0, &bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
				.build(bufferSet, bufferSetLayout);
			descriptorSets.push_back(bufferSet);
			descriptorSetLayouts.push_back(bufferSetLayout);
		}
	}

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
	pipelineLayoutInfo.setLayoutCount = descriptorSetLayouts.size();
	pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
	
	std::vector<VkPushConstantRange> pushConstantRanges;

	for (const auto& pConstant : pConstants)
	{
		VkPushConstantRange push_constant = vkinit::pushconstrant_range(pConstant.size, pConstant.stageFlags);
		pushConstantRanges.push_back(push_constant);
	}
	
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();
	pipelineLayoutInfo.pushConstantRangeCount = pushConstantRanges.size();

	VkPipelineLayout pipelineLayout;
	VK_CHECK(vkCreatePipelineLayout(VulkanEngine::instance()._device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

	PipelineBuilder pipelineBuilder;

	//vertex input controls how to read vertices from vertex buffers. We aren't using it yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(metadata.topology);

	//build viewport and scissor from the swapchain extents
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = static_cast<float>(VulkanEngine::instance()._windowExtent.width);
	pipelineBuilder._viewport.height = static_cast<float>(VulkanEngine::instance()._windowExtent.height);
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;


	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = VulkanEngine::instance()._windowExtent;

	//configure the rasterizer to draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	//we don't use multisampling, so just run the default one
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//a single blend attachment with no blending and writing to RGBA
	pipelineBuilder._colorBlendAttachment = metadata.enableBlending ? vkinit::color_blend_attachment_state_blending() : vkinit::color_blend_attachment_state();

	//default depthtesting
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(metadata.depthTest, metadata.depthWrite, metadata.compareOp);

	//use the triangle layout we created
	pipelineBuilder._pipelineLayout = pipelineLayout;

	//build the mesh pipeline
	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	//connect the pipeline builder vertex input info to the one we get from Vertex
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();


	//clear the shader stages for the builder
	pipelineBuilder._shaderStages.clear();

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));

	//finally build the pipeline
	VkPipeline meshPipeline;
	VkRenderPass render_pass = metadata.enableBlending ? VulkanEngine::instance()._renderPass : VulkanEngine::instance()._offscreenPass;
	meshPipeline = pipelineBuilder.build_pipeline(VulkanEngine::instance()._device, render_pass);

	Material graphicsMaterial {
		.pipeline = meshPipeline,
		.pipelineLayout = pipelineLayout,
		.descriptorSets = descriptorSets,
		.resources = resources,
		.pushConstants = pConstants
	};

	_materials[name] = graphicsMaterial;

	vkDestroyShaderModule(VulkanEngine::instance()._device, fragmentShader, nullptr);
	vkDestroyShaderModule(VulkanEngine::instance()._device, vertexShader, nullptr);

}

void MaterialManager::build_postprocess_pipeline(std::shared_ptr<Resource> fogUboBuffer)
{
	VkShaderModule computeShaderModule;
	vkutil::load_shader_module("fog.comp.spv", VulkanEngine::instance()._device, &computeShaderModule);

	VkDescriptorSet colorImageSet;
	VkDescriptorSetLayout colorImageSetLayout;
	VkDescriptorImageInfo colorImageInfo{
		.sampler = VK_NULL_HANDLE,
		.imageView = VulkanEngine::instance()._fullscreenImage.view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL
	};
	vkutil::DescriptorBuilder::begin(&VulkanEngine::instance()._descriptorLayoutCache, &VulkanEngine::instance()._descriptorAllocator)
		.bind_image(0, &colorImageInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
		.build(colorImageSet, colorImageSetLayout);

	VkDescriptorSet depthImageSet;
	VkDescriptorSetLayout depthImageSetLayout;
	VkDescriptorImageInfo depthImageInfo{
		.sampler = VulkanEngine::instance()._sampler,
		.imageView = VulkanEngine::instance()._depthImage.view,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
	};
	vkutil::DescriptorBuilder::begin(&VulkanEngine::instance()._descriptorLayoutCache, &VulkanEngine::instance()._descriptorAllocator)
		.bind_image(0, &depthImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
		.build(depthImageSet, depthImageSetLayout);

	VkDescriptorSet fogSet;
	VkDescriptorSetLayout fogSetLayout;
	VkDescriptorBufferInfo fogBufferInfo{
		.buffer = fogUboBuffer->value.buffer._buffer,
		.offset = 0,
		.range = fogUboBuffer->value.buffer._size
	};
	vkutil::DescriptorBuilder::begin(&VulkanEngine::instance()._descriptorLayoutCache, &VulkanEngine::instance()._descriptorAllocator)
		.bind_buffer(0, &fogBufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(fogSet, fogSetLayout);


	std::array<VkDescriptorSetLayout, 3> layouts = { colorImageSetLayout, depthImageSetLayout, fogSetLayout };

	VkPipelineShaderStageCreateInfo computeShaderStageInfo = vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_COMPUTE_BIT, computeShaderModule);


	// Define pipeline layout (descriptor set layouts need to be set up based on your compute shader)
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();

	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size()); // Assuming one descriptor set layout
	pipelineLayoutInfo.pSetLayouts = layouts.data(); // Descriptor set layout containing the image resources

	VkPipelineLayout computePipelineLayout;

	if (vkCreatePipelineLayout(VulkanEngine::instance()._device, &pipelineLayoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create compute pipeline layout!");
	}

	// Create compute pipeline
	VkComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage = computeShaderStageInfo;
	pipelineInfo.layout = computePipelineLayout;

	VkPipeline computePipeline;

	if (vkCreateComputePipelines(VulkanEngine::instance()._device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create compute pipeline!");
	}

	Material computeMaterial {
		.pipeline = computePipeline,
		.pipelineLayout = computePipelineLayout,
		.descriptorSets = { colorImageSet, depthImageSet, fogSet },
		.resources = { fogUboBuffer },
		.pushConstants = {}
	};

	_materials["compute"] = computeMaterial;

	vkDestroyShaderModule(VulkanEngine::instance()._device, computeShaderModule, nullptr);
}

void MaterialManager::build_present_pipeline()
{
	VkShaderModule fragShader;
	vkutil::load_shader_module("present_full.frag.spv", VulkanEngine::instance()._device, &fragShader);

	VkShaderModule vertexShader;
	vkutil::load_shader_module("present_full.vert.spv", VulkanEngine::instance()._device, &vertexShader);

	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	VkDescriptorImageInfo sampleImageInfo{
		.sampler = VulkanEngine::instance()._sampler,
		.imageView = VulkanEngine::instance()._fullscreenImage.view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL
	};
	vkutil::DescriptorBuilder::begin(&VulkanEngine::instance()._descriptorLayoutCache, &VulkanEngine::instance()._descriptorAllocator)
		.bind_image(0, &sampleImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(VulkanEngine::instance()._sampledImageSet, VulkanEngine::instance()._sampledImageSetLayout);

	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &VulkanEngine::instance()._sampledImageSetLayout;

	VkPipelineLayout meshPipelineLayout;

	VK_CHECK(vkCreatePipelineLayout(VulkanEngine::instance()._device, &pipeline_layout_info, nullptr, &meshPipelineLayout));

	//build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;

	//vertex input controls how to read vertices from vertex buffers. We aren't using it yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//build viewport and scissor from the swapchain extents
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)VulkanEngine::instance()._windowExtent.width;
	pipelineBuilder._viewport.height = (float)VulkanEngine::instance()._windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;


	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = VulkanEngine::instance()._windowExtent;

	//configure the rasterizer to draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	//we don't use multisampling, so just run the default one
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//a single blend attachment with no blending and writing to RGBA
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();

	//default depthtesting
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, false, VK_COMPARE_OP_LESS_OR_EQUAL);

	//use the triangle layout we created
	pipelineBuilder._pipelineLayout = meshPipelineLayout;

	//build the mesh pipeline
	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 0;  // No vertex buffers
	vertexInputInfo.pVertexBindingDescriptions = nullptr;
	vertexInputInfo.vertexAttributeDescriptionCount = 0;  // No vertex attributes
	vertexInputInfo.pVertexAttributeDescriptions = nullptr;

	//connect the pipeline builder vertex input info to the one we get from Vertex
	pipelineBuilder._vertexInputInfo = vertexInputInfo;


	//clear the shader stages for the builder
	pipelineBuilder._shaderStages.clear();

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragShader));

	//finally build the pipeline
	VkPipeline meshPipeline;
	meshPipeline = pipelineBuilder.build_pipeline(VulkanEngine::instance()._device, VulkanEngine::instance()._renderPass);



	//create_material(meshPipeline, meshPipelineLayout, "present");
	VkDescriptorSet descriptors[] = { VulkanEngine::instance()._sampledImageSet };

	Material material{
		.pipeline = meshPipeline,
		.pipelineLayout = meshPipelineLayout,
		.descriptorSets = { VulkanEngine::instance()._sampledImageSet },
		.resources = {}
	};

	_materials["present"] = material;

	vkDestroyShaderModule(VulkanEngine::instance()._device, fragShader, nullptr);
	vkDestroyShaderModule(VulkanEngine::instance()._device, vertexShader, nullptr);
}
