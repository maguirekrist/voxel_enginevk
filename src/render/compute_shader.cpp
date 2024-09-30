#include "compute_shader.h"

#include <vk_initializers.h>

ComputeShader ComputeShader::create(VkDevice device, const std::string& filePath)
{
    VkShaderModule shader_module;
    bool result = vkinit::load_shader_module(filePath, device, &shader_module);
    if (!result)
    {
        throw std::runtime_error("Unable to create compute shader, see internal error.");
    }

    
}