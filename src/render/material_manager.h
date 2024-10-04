#pragma once

#include "vk_types.h"

class MaterialManager {
public:

    std::unordered_map<std::string, Material> _materials;
    Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);

	Material* get_material(const std::string& name);
private:
};