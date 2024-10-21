#pragma once

#include "vk_types.h"

class MaterialManager {
public:
	Material* get_material(const std::string& name);

	void cleanup();
	
	void build_graphics_pipeline(const std::vector<Resource*>& resources, const std::string& vertex_shader, const std::string& fragment_shader, const std::string& name);


	void build_material_default(AllocatedBuffer cameraBuffer);
	void build_material_water(AllocatedBuffer fogUboBuffer, AllocatedBuffer cameraBuffer);
	void build_material_wireframe();
	void build_postprocess_pipeline(AllocatedBuffer fogUboBuffer);
	void build_present_pipeline();
	
private:
    std::unordered_map<std::string, Material> _materials;
    Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);
};