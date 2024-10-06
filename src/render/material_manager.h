#pragma once

#include "vk_types.h"

class MaterialManager {
public:

    std::unordered_map<std::string, Material> _materials;
    Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);

	Material* get_material(const std::string& name);
private:


	void build_material_default();
	void build_material_water();
	void build_material_wireframe();
	void build_postprocess_pipeline();
	void build_present_pipeline();
};