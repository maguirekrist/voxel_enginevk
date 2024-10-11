#include "material_manager.h"
#include <vk_util.h>
#include <vk_initializers.h>
#include <vk_pipeline_builder.h>
#include <vk_mesh.h>
#include <vk_engine.h>

Material* MaterialManager::create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name)
{
	Material mat;
	mat.pipeline = pipeline;
	mat.pipelineLayout = layout;
	_materials[name] = mat;
	return &_materials[name];
}

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

void MaterialManager::build_material_default()
{
	VkShaderModule trimeshFragShader;
	vkutil::load_shader_module("tri_mesh.frag.spv", VulkanEngine::instance()._device, &trimeshFragShader);

	VkShaderModule trimeshVertexShader;
	vkutil::load_shader_module("tri_mesh.vert.spv", VulkanEngine::instance()._device, &trimeshVertexShader);

	//build the pipeline layout that controls the inputs/outputs of the shader
	//we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	VkDescriptorSetLayout setLayouts[] = { _uboSetLayout, _chunkSetLayout };

	//EXAMPLE: assigning descriptor set to this pipelien
	pipeline_layout_info.setLayoutCount = 2;
	pipeline_layout_info.pSetLayouts = setLayouts;

	//EXAMPLE: Assigning a push_constrant to this pipeline from vkinit:

	VkPushConstantRange push_constant = vkinit::pushconstrant_range(sizeof(ChunkPushConstants), VK_SHADER_STAGE_VERTEX_BIT);
	pipeline_layout_info.pPushConstantRanges = &push_constant;
	pipeline_layout_info.pushConstantRangeCount = 1;

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
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	//use the triangle layout we created
	pipelineBuilder._pipelineLayout = meshPipelineLayout;

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
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, trimeshVertexShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, trimeshFragShader));

	//finally build the pipeline
	VkPipeline meshPipeline;
	meshPipeline = pipelineBuilder.build_pipeline(VulkanEngine::instance()._device, VulkanEngine::instance()._offscreenPass);

	create_material(meshPipeline, meshPipelineLayout, "defaultmesh");

	vkDestroyShaderModule(VulkanEngine::instance()._device, trimeshFragShader, nullptr);
	vkDestroyShaderModule(VulkanEngine::instance()._device, trimeshVertexShader, nullptr);
}

void MaterialManager::build_material_water()
{
	VkShaderModule waterFragShader;
	vkutil::load_shader_module("water_mesh.frag.spv", VulkanEngine::instance()._device, &waterFragShader);

	VkShaderModule waterVertexShader;
	vkutil::load_shader_module("water_mesh.vert.spv", VulkanEngine::instance()._device, &waterVertexShader);

	//build the pipeline layout that controls the inputs/outputs of the shader
	//we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	VkDescriptorSetLayout setLayouts[] = { _uboSetLayout, _chunkSetLayout, _fogSetLayout };

	//EXAMPLE: assigning descriptor set to this pipelien
	pipeline_layout_info.setLayoutCount = 3;
	pipeline_layout_info.pSetLayouts = setLayouts;

	//EXAMPLE: Assigning a push_constrant to this pipeline from vkinit:

	VkPushConstantRange push_constant = vkinit::pushconstrant_range(sizeof(ChunkPushConstants), VK_SHADER_STAGE_VERTEX_BIT);
	pipeline_layout_info.pPushConstantRanges = &push_constant;
	pipeline_layout_info.pushConstantRangeCount = 1;

	VkPipelineLayout meshPipelineLayout;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &meshPipelineLayout));

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
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state_blending();


	//default depthtesting
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, false, VK_COMPARE_OP_LESS_OR_EQUAL);

	//use the triangle layout we created
	pipelineBuilder._pipelineLayout = meshPipelineLayout;

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
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, waterVertexShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, waterFragShader));

	//finally build the pipeline
	VkPipeline meshPipeline;
	meshPipeline = pipelineBuilder.build_pipeline(VulkanEngine::instance()._device, VulkanEngine::instance()._renderPass);

	create_material(meshPipeline, meshPipelineLayout, "watermesh");

	vkDestroyShaderModule(VulkanEngine::instance()._device, waterFragShader, nullptr);
	vkDestroyShaderModule(VulkanEngine::instance()._device, waterVertexShader, nullptr);
}

void MaterialManager::build_material_wireframe()
{
	VkShaderModule wiremeshFragShader;
	vkutil::load_shader_module("wire_mesh.frag.spv", VulkanEngine::instance()._device, &wiremeshFragShader);

	VkShaderModule wiremeshVertexShader;
	vkutil::load_shader_module("wire_mesh.vert.spv", VulkanEngine::instance()._device, &wiremeshVertexShader);

	//build the pipeline layout that controls the inputs/outputs of the shader
	//we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	//EXAMPLE: Assigning a push_constrant to this pipeline from vkinit:
	// pipeline_layout_info.pPushConstantRanges = &push_constant;
	// pipeline_layout_info.pushConstantRangeCount = 1;

	VkPushConstantRange push_constant = vkinit::pushconstrant_range(sizeof(ChunkPushConstants), VK_SHADER_STAGE_VERTEX_BIT);
	pipeline_layout_info.pPushConstantRanges = &push_constant;
	pipeline_layout_info.pushConstantRangeCount = 1;

	VkPipelineLayout meshPipelineLayout;

	VK_CHECK(vkCreatePipelineLayout(VulkanEngine::instance()._device, &pipeline_layout_info, nullptr, &meshPipelineLayout));

	//build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;

	//vertex input controls how to read vertices from vertex buffers. We aren't using it yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);

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

	//a single blend attachment with blending and writing to RGBA
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();

	//default depthtesting
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	//use the triangle layout we created
	pipelineBuilder._pipelineLayout = meshPipelineLayout;

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
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, wiremeshVertexShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, wiremeshFragShader));

	//finally build the pipeline
	VkPipeline meshPipeline;
	meshPipeline = pipelineBuilder.build_pipeline(VulkanEngine::instance()._device, VulkanEngine::instance()._offscreenPass);

	create_material(meshPipeline, meshPipelineLayout, "wireframe");

	vkDestroyShaderModule(VulkanEngine::instance()._device, wiremeshFragShader, nullptr);
	vkDestroyShaderModule(VulkanEngine::instance()._device, wiremeshVertexShader, nullptr);
}

void MaterialManager::build_postprocess_pipeline()
{
	VkShaderModule computeShaderModule;
	vkutil::load_shader_module("fog.comp.spv", VulkanEngine::instance()._device, &computeShaderModule);

	_fogUboBuffer = vkutil::create_buffer(VulkanEngine::instance()._allocator, sizeof(FogUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	update_fog_ubo();

	_mainDeletionQueue.push_function([=, this]() {
		vmaDestroyBuffer(_allocator, _fogUboBuffer._buffer, _fogUboBuffer._allocation);
	});

	VkDescriptorSet colorImageSet;
	VkDescriptorSetLayout colorImageSetLayout;
	VkDescriptorImageInfo colorImageInfo{
		.sampler = VK_NULL_HANDLE,
		.imageView = _fullscreenImageView,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL
	};
	vkutil::DescriptorBuilder::begin(&_descriptorLayoutCache, &_descriptorAllocator)
		.bind_image(0, &colorImageInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
		.build(colorImageSet, colorImageSetLayout);

	VkDescriptorSet depthImageSet;
	VkDescriptorSetLayout depthImageSetLayout;
	VkDescriptorImageInfo depthImageInfo{
		.sampler = _sampler,
		.imageView = _depthImageView,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
	};
	vkutil::DescriptorBuilder::begin(&_descriptorLayoutCache, &_descriptorAllocator)
		.bind_image(0, &depthImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
		.build(depthImageSet, depthImageSetLayout);

	VkDescriptorBufferInfo fogBufferInfo{
		.buffer = _fogUboBuffer._buffer,
		.offset = 0,
		.range = sizeof(FogUBO)
	};
	vkutil::DescriptorBuilder::begin(&_descriptorLayoutCache, &_descriptorAllocator)
		.bind_buffer(0, &fogBufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(_fogSet, _fogSetLayout);


	_computeDescriptorSets = { colorImageSet, depthImageSet, _fogSet };
	std::array<VkDescriptorSetLayout, 3> layouts = { colorImageSetLayout, depthImageSetLayout, _fogSetLayout };

	VkPipelineShaderStageCreateInfo computeShaderStageInfo = vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_COMPUTE_BIT, computeShaderModule);


	// Define pipeline layout (descriptor set layouts need to be set up based on your compute shader)
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();

	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size()); // Assuming one descriptor set layout
	pipelineLayoutInfo.pSetLayouts = layouts.data(); // Descriptor set layout containing the image resources

	if (vkCreatePipelineLayout(VulkanEngine::instance()._device, &pipelineLayoutInfo, nullptr, &_computeMaterial.pipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create compute pipeline layout!");
	}

	// Create compute pipeline
	VkComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage = computeShaderStageInfo;
	pipelineInfo.layout = _computeMaterial.pipelineLayout;

	if (vkCreateComputePipelines(VulkanEngine::instance()._device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_computeMaterial.pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create compute pipeline!");
	}

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
		.sampler = _sampler,
		.imageView = _fullscreenImageView,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL
	};
	vkutil::DescriptorBuilder::begin(&_descriptorLayoutCache, &_descriptorAllocator)
		.bind_image(0, &sampleImageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(_sampledImageSet, _sampledImageSetLayout);

	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &_sampledImageSetLayout;

	VkPipelineLayout meshPipelineLayout;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &meshPipelineLayout));

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
	pipelineBuilder._depthStencil = std::nullopt;

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

	create_material(meshPipeline, meshPipelineLayout, "present");

	vkDestroyShaderModule(VulkanEngine::instance()._device, fragShader, nullptr);
	vkDestroyShaderModule(VulkanEngine::instance()._device, vertexShader, nullptr);
}
