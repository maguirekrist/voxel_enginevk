#pragma once

#include "vk_types.h"

class MaterialManager {
public:
	Material* get_material(const std::string& name);

	void cleanup();

	void build_material_default();
	void build_material_water();
	void build_material_wireframe();
	void build_postprocess_pipeline();
	void build_present_pipeline();
private:
    std::unordered_map<std::string, Material> _materials;
    Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);
};