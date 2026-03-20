#include "material_manager.h"

#include <expected>
#include <vk_initializers.h>
#include <vk_pipeline_builder.h>
#include <render/shader_program.h>
#include <vk_vertex.h>

namespace
{
    void validate_descriptor_sets_are_contiguous(const std::vector<ReflectedDescriptorSet>& descriptorSets)
    {
        for (uint32_t index = 0; index < descriptorSets.size(); ++index)
        {
            if (descriptorSets[index].set != index)
            {
                throw std::runtime_error("Descriptor sets must be contiguous and start at set 0");
            }
        }
    }

    VkDescriptorSet allocate_descriptor_set(vkutil::DescriptorAllocator& allocator, const VkDescriptorSetLayout layout)
    {
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (!allocator.allocate(&set, layout))
        {
            throw std::runtime_error("Failed to allocate descriptor set");
        }

        return set;
    }

    void update_descriptor_set(const VkDevice device, const VkDescriptorSet set, std::vector<VkWriteDescriptorSet>& writes)
    {
        for (VkWriteDescriptorSet& write : writes)
        {
            write.dstSet = set;
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void validate_push_constants(const std::vector<PushConstant>& runtimePushConstants, const std::vector<VkPushConstantRange>& reflectedPushConstants)
    {
        if (runtimePushConstants.size() != reflectedPushConstants.size())
        {
            throw std::runtime_error("Runtime push constants do not match reflected shader push constant ranges");
        }

        for (size_t index = 0; index < runtimePushConstants.size(); ++index)
        {
            if (runtimePushConstants[index].size != reflectedPushConstants[index].size)
            {
                throw std::runtime_error("Runtime push constant size does not match reflected shader push constant size");
            }
        }
    }
}

void MaterialManager::init(const MaterialBackendContext& context)
{
	_context = context;
}


std::shared_ptr<Material> MaterialManager::get_material(const std::string &name)
{
    //search for the object, and return nullptr if not found
    if (const auto it = m_materials.find(name); it != m_materials.end()) {
		return it->second;
	}

	throw std::runtime_error("Material not found: " + name);
}

void MaterialManager::cleanup()
{
	for(auto it = m_materials.begin(); it != m_materials.end();) {
		auto& material = *it->second;
		vkDestroyPipeline(_context.device, material.pipeline, nullptr);
		vkDestroyPipelineLayout(_context.device, material.pipelineLayout, nullptr);

		it = m_materials.erase(it);
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
	ShaderProgram shaderProgram = ShaderProgram::load_graphics(_context.device, vertex_shader, fragment_shader);
	validate_descriptor_sets_are_contiguous(shaderProgram.descriptor_sets());
	validate_push_constants(pConstants, shaderProgram.push_constant_ranges());

	std::vector<VkDescriptorSet> descriptorSets;
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts = shaderProgram.create_descriptor_set_layouts(*_context.descriptorLayoutCache);

	if (resources.size() != descriptorSetLayouts.size())
	{
		throw std::runtime_error("Material resources do not match reflected descriptor set count");
	}

	for (size_t setIndex = 0; setIndex < resources.size(); ++setIndex)
	{
		const auto& descriptorSetInfo = shaderProgram.descriptor_sets()[setIndex];
		if (descriptorSetInfo.bindings.size() != 1)
		{
			throw std::runtime_error("Graphics material currently expects exactly one binding per descriptor set");
		}

		const auto& binding = descriptorSetInfo.bindings.front();
		const auto& resource = resources[setIndex];
		const VkDescriptorSet descriptorSet = allocate_descriptor_set(*_context.descriptorAllocator, descriptorSetLayouts[setIndex]);
		std::vector<VkWriteDescriptorSet> writes;
		writes.reserve(1);

		if (resource->type == Resource::BUFFER)
		{
			if (binding.descriptorType != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
			{
				throw std::runtime_error("Buffer resource does not match reflected descriptor type");
			}

			auto* bufferInfo = new VkDescriptorBufferInfo{
				.buffer = resource->value.buffer._buffer,
				.offset = 0,
				.range = resource->value.buffer._size
			};

			writes.push_back(VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = VK_NULL_HANDLE,
				.dstBinding = binding.binding,
				.descriptorCount = 1,
				.descriptorType = binding.descriptorType,
				.pBufferInfo = bufferInfo
			});
		}
		else if (resource->type == Resource::IMAGE)
		{
			auto* imageInfo = new VkDescriptorImageInfo{
				.sampler = resource->value.image.sampler,
				.imageView = resource->value.image.view,
				.imageLayout = binding.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
					? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					: VK_IMAGE_LAYOUT_GENERAL
			};

			writes.push_back(VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = VK_NULL_HANDLE,
				.dstBinding = binding.binding,
				.descriptorCount = 1,
				.descriptorType = binding.descriptorType,
				.pImageInfo = imageInfo
			});
		}
		else
		{
			throw std::runtime_error("Unsupported resource type");
		}

		update_descriptor_set(_context.device, descriptorSet, writes);
		descriptorSets.push_back(descriptorSet);

		for (const VkWriteDescriptorSet& write : writes)
		{
			delete write.pBufferInfo;
			delete write.pImageInfo;
		}
	}

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
	pipelineLayoutInfo.pPushConstantRanges = shaderProgram.push_constant_ranges().data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(shaderProgram.push_constant_ranges().size());

	VkPipelineLayout pipelineLayout;
	VK_CHECK(vkCreatePipelineLayout(_context.device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

	PipelineBuilder pipelineBuilder;

	//vertex input controls how to read vertices from vertex buffers. We aren't using it yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(metadata.topology);

	//build viewport and scissor from the swapchain extents
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = static_cast<float>(_context.windowExtent->width);
	pipelineBuilder._viewport.height = static_cast<float>(_context.windowExtent->height);
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;


	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = *_context.windowExtent;

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
	pipelineBuilder._shaderStages = shaderProgram.shader_stages();

	//finally build the pipeline
	VkRenderPass render_pass = metadata.enableBlending ? *_context.renderPass : *_context.offscreenPass;
	VkPipeline meshPipeline = pipelineBuilder.build_pipeline(_context.device, render_pass);


	auto new_material = Material{
		.key = name,
		.pipeline = meshPipeline,
		.pipelineLayout = pipelineLayout,
		.descriptorSets = descriptorSets,
		.resources = resources,
		.pushConstants = pConstants
	};

	add_material(name, std::move(new_material));
}

void MaterialManager::build_postprocess_pipeline(std::shared_ptr<Resource> fogUboBuffer)
{
	ShaderProgram shaderProgram = ShaderProgram::load_compute(_context.device, "fog.comp.spv");
	validate_descriptor_sets_are_contiguous(shaderProgram.descriptor_sets());
	std::vector<VkDescriptorSetLayout> layouts = shaderProgram.create_descriptor_set_layouts(*_context.descriptorLayoutCache);
	if (layouts.size() != 3)
	{
		throw std::runtime_error("Compute pipeline reflection does not match expected descriptor set count");
	}

	VkDescriptorSet colorImageSet = allocate_descriptor_set(*_context.descriptorAllocator, layouts[0]);
	VkDescriptorImageInfo colorImageInfo{
		.sampler = VK_NULL_HANDLE,
		.imageView = _context.fullscreenImage->view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL
	};
	std::vector<VkWriteDescriptorSet> colorWrites{
		VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = VK_NULL_HANDLE,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &colorImageInfo
		}
	};
	update_descriptor_set(_context.device, colorImageSet, colorWrites);

	VkDescriptorSet depthImageSet = allocate_descriptor_set(*_context.descriptorAllocator, layouts[1]);
	VkDescriptorImageInfo depthImageInfo{
		.sampler = *_context.sampler,
		.imageView = _context.depthImage->view,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
	};
	std::vector<VkWriteDescriptorSet> depthWrites{
		VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = VK_NULL_HANDLE,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &depthImageInfo
		}
	};
	update_descriptor_set(_context.device, depthImageSet, depthWrites);

	VkDescriptorSet fogSet = allocate_descriptor_set(*_context.descriptorAllocator, layouts[2]);
	VkDescriptorBufferInfo fogBufferInfo{
		.buffer = fogUboBuffer->value.buffer._buffer,
		.offset = 0,
		.range = fogUboBuffer->value.buffer._size
	};
	std::vector<VkWriteDescriptorSet> fogWrites{
		VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = VK_NULL_HANDLE,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &fogBufferInfo
		}
	};
	update_descriptor_set(_context.device, fogSet, fogWrites);

	// Define pipeline layout (descriptor set layouts need to be set up based on your compute shader)
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();

	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
	pipelineLayoutInfo.pSetLayouts = layouts.data();
	pipelineLayoutInfo.pPushConstantRanges = shaderProgram.push_constant_ranges().data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(shaderProgram.push_constant_ranges().size());

	VkPipelineLayout computePipelineLayout;

	if (vkCreatePipelineLayout(_context.device, &pipelineLayoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create compute pipeline layout!");
	}

	// Create compute pipeline
	VkComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage = shaderProgram.shader_stages().front();
	pipelineInfo.layout = computePipelineLayout;

	VkPipeline computePipeline;

	if (vkCreateComputePipelines(_context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create compute pipeline!");
	}

	auto new_material = Material{
		.pipeline = computePipeline,
		.pipelineLayout = computePipelineLayout,
		.descriptorSets = { colorImageSet, depthImageSet, fogSet },
		.resources = { fogUboBuffer },
		.pushConstants = {} };

	/*m_materials.emplace("compute", std::make_shared<Material>());*/
	add_material("compute", std::move(new_material));
}

void MaterialManager::build_present_pipeline()
{
	ShaderProgram shaderProgram = ShaderProgram::load_graphics(_context.device, "present_full.vert.spv", "present_full.frag.spv");
	validate_descriptor_sets_are_contiguous(shaderProgram.descriptor_sets());
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts = shaderProgram.create_descriptor_set_layouts(*_context.descriptorLayoutCache);
	if (descriptorSetLayouts.size() != 1)
	{
		throw std::runtime_error("Present pipeline reflection does not match expected descriptor set count");
	}

	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	VkDescriptorImageInfo sampleImageInfo{
		.sampler = *_context.sampler,
		.imageView = _context.fullscreenImage->view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL
	};
	_sampledImageSetLayout = descriptorSetLayouts.front();
	_sampledImageSet = allocate_descriptor_set(*_context.descriptorAllocator, _sampledImageSetLayout);
	std::vector<VkWriteDescriptorSet> presentWrites{
		VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = VK_NULL_HANDLE,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &sampleImageInfo
		}
	};
	update_descriptor_set(_context.device, _sampledImageSet, presentWrites);

	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &_sampledImageSetLayout;
	pipeline_layout_info.pPushConstantRanges = shaderProgram.push_constant_ranges().data();
	pipeline_layout_info.pushConstantRangeCount = static_cast<uint32_t>(shaderProgram.push_constant_ranges().size());

	VkPipelineLayout meshPipelineLayout;

	VK_CHECK(vkCreatePipelineLayout(_context.device, &pipeline_layout_info, nullptr, &meshPipelineLayout));

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
	pipelineBuilder._viewport.width = (float)_context.windowExtent->width;
	pipelineBuilder._viewport.height = (float)_context.windowExtent->height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;


	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = *_context.windowExtent;

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
	pipelineBuilder._shaderStages = shaderProgram.shader_stages();

	//finally build the pipeline
	const VkPipeline meshPipeline = pipelineBuilder.build_pipeline(_context.device, *_context.renderPass);



	//create_material(meshPipeline, meshPipelineLayout, "present");
	auto new_material = Material{
		.key = "present",
		.pipeline = meshPipeline,
		.pipelineLayout = meshPipelineLayout,
		.descriptorSets = { _sampledImageSet },
		.resources = {} };
	add_material("present", std::move(new_material));
}

void MaterialManager::add_material(const std::string& name, Material&& material)
{
	if (m_materials.contains(name))
	{
		auto& map = m_materials[name];
		vkDestroyPipeline(_context.device, map->pipeline, nullptr);
		vkDestroyPipelineLayout(_context.device, map->pipelineLayout, nullptr);
		std::swap(*map, material);
	}
	else {
		m_materials.emplace(name, std::make_shared<Material>(material));
	}
}
