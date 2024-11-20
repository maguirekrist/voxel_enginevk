#pragma once

#include "vk_mesh.h"

struct PipelineMetadata {
	bool depthTest = true;
	bool depthWrite = true;
	VkCompareOp compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	bool enableBlending = false;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

class MaterialManager {
public:
	Material* get_material(const std::string& name);

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
    std::unordered_map<std::string, Material> _materials;
};