#include "shader_program.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>

#include <spirv_reflect.h>

#include "vk_initializers.h"

namespace
{
    void check_reflect_result(const SpvReflectResult result, const std::string_view operation)
    {
        if (result != SPV_REFLECT_RESULT_SUCCESS)
        {
            throw std::runtime_error(std::format("SPIR-V reflection failed during {}", operation));
        }
    }

    std::vector<uint32_t> read_spirv_code(const std::string& shaderPath)
    {
        const auto path = std::filesystem::current_path() / "shaders" / shaderPath;
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error(std::format("File {}, does not exist.", shaderPath));
        }

        const auto fileSize = static_cast<size_t>(file.tellg());
        std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));
        return buffer;
    }

    VkShaderModule create_shader_module(VkDevice device, const std::vector<uint32_t>& code, const std::string& shaderPath)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size() * sizeof(uint32_t);
        createInfo.pCode = code.data();

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error(std::format("Could not create shader module for {}", shaderPath));
        }

        return shaderModule;
    }

    std::vector<ReflectedDescriptorSet> reflect_descriptor_sets(const std::vector<uint32_t>& code, const VkShaderStageFlagBits stage)
    {
        SpvReflectShaderModule shaderModule{};
        if (spvReflectCreateShaderModule(code.size() * sizeof(uint32_t), code.data(), &shaderModule) != SPV_REFLECT_RESULT_SUCCESS)
        {
            throw std::runtime_error("Failed to reflect shader module");
        }

        uint32_t bindingCount = 0;
        check_reflect_result(spvReflectEnumerateDescriptorBindings(&shaderModule, &bindingCount, nullptr), "descriptor binding count enumeration");
        std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
        check_reflect_result(spvReflectEnumerateDescriptorBindings(&shaderModule, &bindingCount, bindings.data()), "descriptor binding enumeration");

        std::unordered_map<uint32_t, ReflectedDescriptorSet> sets;
        for (SpvReflectDescriptorBinding* binding : bindings)
        {
            auto& setInfo = sets[binding->set];
            setInfo.set = binding->set;
            setInfo.bindings.push_back(ReflectedDescriptorBinding{
                .binding = binding->binding,
                .descriptorType = static_cast<VkDescriptorType>(binding->descriptor_type),
                .descriptorCount = binding->count,
                .stageFlags = static_cast<VkShaderStageFlags>(stage),
                .name = binding->name != nullptr ? binding->name : ""
            });
        }

        std::vector<ReflectedDescriptorSet> orderedSets;
        orderedSets.reserve(sets.size());
        for (auto& [_, setInfo] : sets)
        {
            std::sort(setInfo.bindings.begin(), setInfo.bindings.end(), [](const ReflectedDescriptorBinding& lhs, const ReflectedDescriptorBinding& rhs)
            {
                return lhs.binding < rhs.binding;
            });
            orderedSets.push_back(std::move(setInfo));
        }

        std::sort(orderedSets.begin(), orderedSets.end(), [](const ReflectedDescriptorSet& lhs, const ReflectedDescriptorSet& rhs)
        {
            return lhs.set < rhs.set;
        });

        spvReflectDestroyShaderModule(&shaderModule);
        return orderedSets;
    }

    std::vector<VkPushConstantRange> reflect_push_constants(const std::vector<uint32_t>& code, const VkShaderStageFlagBits stage)
    {
        SpvReflectShaderModule shaderModule{};
        if (spvReflectCreateShaderModule(code.size() * sizeof(uint32_t), code.data(), &shaderModule) != SPV_REFLECT_RESULT_SUCCESS)
        {
            throw std::runtime_error("Failed to reflect shader module");
        }

        uint32_t blockCount = 0;
        check_reflect_result(spvReflectEnumeratePushConstantBlocks(&shaderModule, &blockCount, nullptr), "push constant count enumeration");
        std::vector<SpvReflectBlockVariable*> blocks(blockCount);
        check_reflect_result(spvReflectEnumeratePushConstantBlocks(&shaderModule, &blockCount, blocks.data()), "push constant enumeration");

        std::vector<VkPushConstantRange> pushConstants;
        pushConstants.reserve(blockCount);
        for (SpvReflectBlockVariable* block : blocks)
        {
            pushConstants.push_back(VkPushConstantRange{
                .stageFlags = static_cast<VkShaderStageFlags>(stage),
                .offset = block->offset,
                .size = block->size
            });
        }

        std::sort(pushConstants.begin(), pushConstants.end(), [](const VkPushConstantRange& lhs, const VkPushConstantRange& rhs)
        {
            return lhs.offset < rhs.offset;
        });

        spvReflectDestroyShaderModule(&shaderModule);
        return pushConstants;
    }

    void merge_descriptor_sets(std::vector<ReflectedDescriptorSet>& mergedSets, const std::vector<ReflectedDescriptorSet>& shaderSets)
    {
        for (const ReflectedDescriptorSet& shaderSet : shaderSets)
        {
            auto existingSet = std::find_if(mergedSets.begin(), mergedSets.end(), [&shaderSet](const ReflectedDescriptorSet& candidate)
            {
                return candidate.set == shaderSet.set;
            });

            if (existingSet == mergedSets.end())
            {
                mergedSets.push_back(shaderSet);
                continue;
            }

            for (const ReflectedDescriptorBinding& binding : shaderSet.bindings)
            {
                auto existingBinding = std::find_if(existingSet->bindings.begin(), existingSet->bindings.end(), [&binding](const ReflectedDescriptorBinding& candidate)
                {
                    return candidate.binding == binding.binding;
                });

                if (existingBinding == existingSet->bindings.end())
                {
                    existingSet->bindings.push_back(binding);
                    continue;
                }

                if (existingBinding->descriptorType != binding.descriptorType || existingBinding->descriptorCount != binding.descriptorCount)
                {
                    throw std::runtime_error(std::format("Mismatched reflected descriptor binding at set {}, binding {}", shaderSet.set, binding.binding));
                }

                existingBinding->stageFlags |= binding.stageFlags;
            }

            std::sort(existingSet->bindings.begin(), existingSet->bindings.end(), [](const ReflectedDescriptorBinding& lhs, const ReflectedDescriptorBinding& rhs)
            {
                return lhs.binding < rhs.binding;
            });
        }

        std::sort(mergedSets.begin(), mergedSets.end(), [](const ReflectedDescriptorSet& lhs, const ReflectedDescriptorSet& rhs)
        {
            return lhs.set < rhs.set;
        });
    }

    void merge_push_constants(std::vector<VkPushConstantRange>& mergedRanges, const std::vector<VkPushConstantRange>& shaderRanges)
    {
        for (const VkPushConstantRange& shaderRange : shaderRanges)
        {
            auto existingRange = std::find_if(mergedRanges.begin(), mergedRanges.end(), [&shaderRange](const VkPushConstantRange& candidate)
            {
                return candidate.offset == shaderRange.offset && candidate.size == shaderRange.size;
            });

            if (existingRange == mergedRanges.end())
            {
                mergedRanges.push_back(shaderRange);
                continue;
            }

            existingRange->stageFlags |= shaderRange.stageFlags;
        }

        std::sort(mergedRanges.begin(), mergedRanges.end(), [](const VkPushConstantRange& lhs, const VkPushConstantRange& rhs)
        {
            return lhs.offset < rhs.offset;
        });
    }
}

ShaderProgram ShaderProgram::load_graphics(const VkDevice device, const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
{
    return load(device, {
        { vertexShaderPath, VK_SHADER_STAGE_VERTEX_BIT },
        { fragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT }
    });
}

ShaderProgram ShaderProgram::load_compute(const VkDevice device, const std::string& computeShaderPath)
{
    return load(device, {
        { computeShaderPath, VK_SHADER_STAGE_COMPUTE_BIT }
    });
}

ShaderProgram::~ShaderProgram()
{
    for (const ShaderModuleHandle& module : _modules)
    {
        if (module.module != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(_device, module.module, nullptr);
        }
    }
}

const std::vector<VkPipelineShaderStageCreateInfo>& ShaderProgram::shader_stages() const
{
    return _shaderStages;
}

const std::vector<ReflectedDescriptorSet>& ShaderProgram::descriptor_sets() const
{
    return _descriptorSets;
}

const std::vector<VkPushConstantRange>& ShaderProgram::push_constant_ranges() const
{
    return _pushConstantRanges;
}

std::vector<VkDescriptorSetLayout> ShaderProgram::create_descriptor_set_layouts(vkutil::DescriptorLayoutCache& layoutCache) const
{
    std::vector<VkDescriptorSetLayout> layouts;
    layouts.reserve(_descriptorSets.size());

    for (const ReflectedDescriptorSet& descriptorSet : _descriptorSets)
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.reserve(descriptorSet.bindings.size());
        for (const ReflectedDescriptorBinding& binding : descriptorSet.bindings)
        {
            bindings.push_back(VkDescriptorSetLayoutBinding{
                .binding = binding.binding,
                .descriptorType = binding.descriptorType,
                .descriptorCount = binding.descriptorCount,
                .stageFlags = binding.stageFlags,
                .pImmutableSamplers = nullptr
            });
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        layouts.push_back(layoutCache.create_descriptor_layout(&layoutInfo));
    }

    return layouts;
}

ShaderProgram ShaderProgram::load(const VkDevice device, const std::initializer_list<std::pair<std::string, VkShaderStageFlagBits>> shaderStages)
{
    ShaderProgram program;
    program._device = device;

    for (const auto& [shaderPath, shaderStage] : shaderStages)
    {
        const std::vector<uint32_t> code = read_spirv_code(shaderPath);
        const VkShaderModule shaderModule = create_shader_module(device, code, shaderPath);

        program._modules.push_back(ShaderModuleHandle{
            .module = shaderModule,
            .stage = shaderStage,
            .path = shaderPath
        });
        program._shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(shaderStage, shaderModule));

        merge_descriptor_sets(program._descriptorSets, reflect_descriptor_sets(code, shaderStage));
        merge_push_constants(program._pushConstantRanges, reflect_push_constants(code, shaderStage));
    }

    return program;
}
