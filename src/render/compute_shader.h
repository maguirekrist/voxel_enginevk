#pragma once 

#include <vk_types.h>

struct ComputeShader {
    static ComputeShader create(VkDevice device, const std::string& filePath);

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};