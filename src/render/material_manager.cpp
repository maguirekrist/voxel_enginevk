#include "material_manager.h"

#include <expected>
#include <vk_initializers.h>
#include <vk_pipeline_builder.h>
#include <render/shader_program.h>
#include <vk_vertex.h>

namespace
{
    struct MaterialDescriptorState
    {
        std::vector<VkDescriptorSetLayout> layouts;
        std::vector<VkDescriptorSet> descriptorSets;
    };

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

    const MaterialBinding& find_binding(const MaterialBindings& bindings, const uint32_t set, const uint32_t binding)
    {
        const auto it = std::find_if(bindings.begin(), bindings.end(), [set, binding](const MaterialBinding& candidate)
        {
            return candidate.set == set && candidate.binding == binding;
        });

        if (it == bindings.end())
        {
            throw std::runtime_error(std::format("Missing material binding for set {} binding {}", set, binding));
        }

        return *it;
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

    VkDescriptorBufferInfo resolve_buffer_info(const MaterialBinding& binding)
    {
        if (binding.bufferInfo.has_value())
        {
            return binding.bufferInfo.value();
        }

        if (binding.resource != nullptr && binding.resource->type == Resource::BUFFER)
        {
            return VkDescriptorBufferInfo{
                .buffer = binding.resource->value.buffer._buffer,
                .offset = 0,
                .range = binding.resource->value.buffer._size
            };
        }

        throw std::runtime_error("Material binding does not provide buffer data");
    }

    VkDescriptorImageInfo resolve_image_info(const MaterialBinding& binding)
    {
        if (binding.imageInfo.has_value())
        {
            return binding.imageInfo.value();
        }

        if (binding.resource != nullptr && binding.resource->type == Resource::IMAGE)
        {
            return VkDescriptorImageInfo{
                .sampler = binding.resource->value.image.sampler,
                .imageView = binding.resource->value.image.view,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL
            };
        }

        throw std::runtime_error("Material binding does not provide image data");
    }

    MaterialDescriptorState build_descriptor_state(
        const ShaderProgram& shaderProgram,
        const MaterialBindings& bindings,
        const VkDevice device,
        vkutil::DescriptorLayoutCache& layoutCache,
        vkutil::DescriptorAllocator& allocator)
    {
        MaterialDescriptorState state{
            .layouts = shaderProgram.create_descriptor_set_layouts(layoutCache)
        };

        state.descriptorSets.reserve(state.layouts.size());

        for (size_t setIndex = 0; setIndex < state.layouts.size(); ++setIndex)
        {
            const ReflectedDescriptorSet& reflectedSet = shaderProgram.descriptor_sets()[setIndex];
            const VkDescriptorSet descriptorSet = allocate_descriptor_set(allocator, state.layouts[setIndex]);
            std::vector<VkWriteDescriptorSet> writes;
            std::vector<VkDescriptorBufferInfo> bufferInfos;
            std::vector<VkDescriptorImageInfo> imageInfos;

            writes.reserve(reflectedSet.bindings.size());
            bufferInfos.reserve(reflectedSet.bindings.size());
            imageInfos.reserve(reflectedSet.bindings.size());

            for (const ReflectedDescriptorBinding& reflectedBinding : reflectedSet.bindings)
            {
                const MaterialBinding& binding = find_binding(bindings, reflectedSet.set, reflectedBinding.binding);

                if (reflectedBinding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                    reflectedBinding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                {
                    bufferInfos.push_back(resolve_buffer_info(binding));
                    writes.push_back(VkWriteDescriptorSet{
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = VK_NULL_HANDLE,
                        .dstBinding = reflectedBinding.binding,
                        .descriptorCount = 1,
                        .descriptorType = reflectedBinding.descriptorType,
                        .pBufferInfo = &bufferInfos.back()
                    });
                    continue;
                }

                imageInfos.push_back(resolve_image_info(binding));
                writes.push_back(VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = VK_NULL_HANDLE,
                    .dstBinding = reflectedBinding.binding,
                    .descriptorCount = 1,
                    .descriptorType = reflectedBinding.descriptorType,
                    .pImageInfo = &imageInfos.back()
                });
            }

            update_descriptor_set(device, descriptorSet, writes);
            state.descriptorSets.push_back(descriptorSet);
        }

        return state;
    }

    bool uses_blending(const BlendMode mode)
    {
        return mode != BlendMode::Opaque;
    }
}

void MaterialManager::init(const MaterialBackendContext& context)
{
	_context = context;
}


std::shared_ptr<Material> MaterialManager::get_material_by_key(const std::string &name)
{
    //search for the object, and return nullptr if not found
    if (const auto it = m_materials.find(name); it != m_materials.end()) {
		return it->second;
	}

	throw std::runtime_error("Material not found: " + name);
}
 
std::string MaterialManager::scoped_name(const std::string_view scope, const std::string_view name)
{
    return std::format("{}::{}", scope, name);
}

std::shared_ptr<Material> MaterialManager::get_material(const std::string_view scope, const std::string_view name)
{
    return get_material_by_key(scoped_name(scope, name));
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
    const std::string_view scope,
	const MaterialBindings& bindings,
	const std::vector<PushConstant>& pConstants,
	const PipelineMetadata&& metadata,
	const std::string& vertex_shader,
	const std::string& fragment_shader, 
	const std::string_view name)
{
	ShaderProgram shaderProgram = ShaderProgram::load_graphics(_context.device, vertex_shader, fragment_shader);
	validate_descriptor_sets_are_contiguous(shaderProgram.descriptor_sets());
	validate_push_constants(pConstants, shaderProgram.push_constant_ranges());
    MaterialDescriptorState descriptorState = build_descriptor_state(
        shaderProgram,
        bindings,
        _context.device,
        *_context.descriptorLayoutCache,
        *_context.descriptorAllocator);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorState.layouts.size());
	pipelineLayoutInfo.pSetLayouts = descriptorState.layouts.data();
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
    switch (metadata.blendMode)
    {
    case BlendMode::Alpha:
        pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state_blending();
        break;
    case BlendMode::Additive:
        pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state_additive();
        break;
    case BlendMode::Opaque:
    default:
        pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();
        break;
    }

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
	VkRenderPass render_pass = uses_blending(metadata.blendMode) ? *_context.renderPass : *_context.offscreenPass;
	VkPipeline meshPipeline = pipelineBuilder.build_pipeline(_context.device, render_pass);

    const std::string materialKey = scoped_name(scope, name);

	auto new_material = Material{
		.key = materialKey,
		.pipeline = meshPipeline,
		.pipelineLayout = pipelineLayout,
		.descriptorSets = descriptorState.descriptorSets,
		.bindings = bindings,
		.pushConstants = pConstants
	};

	add_material(materialKey, std::move(new_material));
}

void MaterialManager::build_postprocess_pipeline(const std::string_view scope, std::shared_ptr<Resource> fogUboBuffer)
{
	ShaderProgram shaderProgram = ShaderProgram::load_compute(_context.device, "fog.comp.spv");
	validate_descriptor_sets_are_contiguous(shaderProgram.descriptor_sets());
    MaterialBindings bindings{
        MaterialBinding::from_image_info(0, 0, VkDescriptorImageInfo{
            .sampler = VK_NULL_HANDLE,
            .imageView = _context.fullscreenImage->view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL
        }),
        MaterialBinding::from_image_info(1, 0, VkDescriptorImageInfo{
            .sampler = *_context.sampler,
            .imageView = _context.depthImage->view,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
        }),
        MaterialBinding::from_resource(2, 0, fogUboBuffer)
    };
    MaterialDescriptorState descriptorState = build_descriptor_state(
        shaderProgram,
        bindings,
        _context.device,
        *_context.descriptorLayoutCache,
        *_context.descriptorAllocator);

	// Define pipeline layout (descriptor set layouts need to be set up based on your compute shader)
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();

	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorState.layouts.size());
	pipelineLayoutInfo.pSetLayouts = descriptorState.layouts.data();
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
        .key = scoped_name(scope, "compute"),
		.pipeline = computePipeline,
		.pipelineLayout = computePipelineLayout,
		.descriptorSets = descriptorState.descriptorSets,
		.bindings = bindings,
		.pushConstants = {} };

	add_material(new_material.key, std::move(new_material));
}

void MaterialManager::build_present_pipeline(const std::string_view scope)
{
	ShaderProgram shaderProgram = ShaderProgram::load_graphics(_context.device, "present_full.vert.spv", "present_full.frag.spv");
	validate_descriptor_sets_are_contiguous(shaderProgram.descriptor_sets());
    MaterialBindings bindings{
        MaterialBinding::from_image_info(0, 0, VkDescriptorImageInfo{
            .sampler = *_context.sampler,
            .imageView = _context.fullscreenImage->view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL
        })
    };
    MaterialDescriptorState descriptorState = build_descriptor_state(
        shaderProgram,
        bindings,
        _context.device,
        *_context.descriptorLayoutCache,
        *_context.descriptorAllocator);

	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	const VkDescriptorSetLayout sampledImageSetLayout = descriptorState.layouts.front();
	const VkDescriptorSet sampledImageSet = descriptorState.descriptorSets.front();

	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &sampledImageSetLayout;
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
		.key = scoped_name(scope, "present"),
		.pipeline = meshPipeline,
		.pipelineLayout = meshPipelineLayout,
		.descriptorSets = { sampledImageSet },
		.bindings = bindings };
	add_material(new_material.key, std::move(new_material));
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
