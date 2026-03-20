#pragma once

#include <string>

#include <vk_types.h>

#include "vk_util.h"

struct ReflectedDescriptorBinding
{
    uint32_t binding{0};
    VkDescriptorType descriptorType{VK_DESCRIPTOR_TYPE_MAX_ENUM};
    uint32_t descriptorCount{0};
    VkShaderStageFlags stageFlags{0};
    std::string name;
};

struct ReflectedDescriptorSet
{
    uint32_t set{0};
    std::vector<ReflectedDescriptorBinding> bindings;
};

class ShaderProgram
{
public:
    static ShaderProgram load_graphics(VkDevice device, const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
    static ShaderProgram load_compute(VkDevice device, const std::string& computeShaderPath);

    ShaderProgram() = default;
    ShaderProgram(ShaderProgram&& other) noexcept = default;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept = default;

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    ~ShaderProgram();

    [[nodiscard]] const std::vector<VkPipelineShaderStageCreateInfo>& shader_stages() const;
    [[nodiscard]] const std::vector<ReflectedDescriptorSet>& descriptor_sets() const;
    [[nodiscard]] const std::vector<VkPushConstantRange>& push_constant_ranges() const;
    [[nodiscard]] std::vector<VkDescriptorSetLayout> create_descriptor_set_layouts(vkutil::DescriptorLayoutCache& layoutCache) const;

private:
    struct ShaderModuleHandle
    {
        VkShaderModule module{VK_NULL_HANDLE};
        VkShaderStageFlagBits stage{VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM};
        std::string path;
    };

    VkDevice _device{VK_NULL_HANDLE};
    std::vector<ShaderModuleHandle> _modules;
    std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
    std::vector<ReflectedDescriptorSet> _descriptorSets;
    std::vector<VkPushConstantRange> _pushConstantRanges;

    static ShaderProgram load(VkDevice device, std::initializer_list<std::pair<std::string, VkShaderStageFlagBits>> shaderStages);
};
