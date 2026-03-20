//
// Created by Maguire Krist on 8/23/25.
//

#ifndef MATERIAL_H
#define MATERIAL_H

#include "render_primitives.h"
#include "resource.h"

struct MaterialBinding {
    uint32_t set{0};
    uint32_t binding{0};
    std::shared_ptr<Resource> resource{};
    std::optional<VkDescriptorBufferInfo> bufferInfo{};
    std::optional<VkDescriptorImageInfo> imageInfo{};

    [[nodiscard]] static MaterialBinding from_resource(const uint32_t set, const uint32_t binding, const std::shared_ptr<Resource>& resource)
    {
        return MaterialBinding{
            .set = set,
            .binding = binding,
            .resource = resource
        };
    }

    [[nodiscard]] static MaterialBinding from_buffer_info(const uint32_t set, const uint32_t binding, const VkDescriptorBufferInfo& bufferInfo)
    {
        return MaterialBinding{
            .set = set,
            .binding = binding,
            .bufferInfo = bufferInfo
        };
    }

    [[nodiscard]] static MaterialBinding from_image_info(const uint32_t set, const uint32_t binding, const VkDescriptorImageInfo& imageInfo)
    {
        return MaterialBinding{
            .set = set,
            .binding = binding,
            .imageInfo = imageInfo
        };
    }
};

using MaterialBindings = std::vector<MaterialBinding>;

struct Material {
    std::string key;
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;

    std::vector<VkDescriptorSet> descriptorSets;
    MaterialBindings bindings;

    std::vector<PushConstant> pushConstants;

    //Not sure if having a call back function make sense.
    //std::function<void()> buffer_update;
    //~Material();
};


#endif //MATERIAL_H
