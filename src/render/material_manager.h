#pragma once
#include "material.h"
#include "vk_util.h"


struct PipelineMetadata {
	bool depthTest = true;
	bool depthWrite = true;
	VkCompareOp compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	bool enableBlending = false;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

struct MaterialBackendContext
{
	VkDevice device{};
	VkExtent2D* windowExtent{};
	VkRenderPass* renderPass{};
	VkRenderPass* offscreenPass{};
	VkSampler* sampler{};
	ImageResource* fullscreenImage{};
	ImageResource* depthImage{};
	vkutil::DescriptorAllocator* descriptorAllocator{};
	vkutil::DescriptorLayoutCache* descriptorLayoutCache{};
};

class MaterialManager {
public:
	void init(const MaterialBackendContext& context);
	std::shared_ptr<Material> get_material(const std::string& name);

	void cleanup();
	
	void build_graphics_pipeline(
		const std::vector<std::shared_ptr<Resource>>& resources,
		const std::vector<PushConstant>& pConstants,
		const PipelineMetadata&& metadata,
		const std::string& vertex_shader,
		const std::string& fragment_shader,
		const std::string& name);

	void build_postprocess_pipeline(std::shared_ptr<Resource> fogUboBuffer);
	void build_present_pipeline();
	
private:
    std::unordered_map<std::string, std::shared_ptr<Material>> m_materials;
	MaterialBackendContext _context{};
	VkDescriptorSetLayout _sampledImageSetLayout{VK_NULL_HANDLE};
	VkDescriptorSet _sampledImageSet{VK_NULL_HANDLE};

	void add_material(const std::string& name, Material&& material);
};
