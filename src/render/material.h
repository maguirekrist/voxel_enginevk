//
// Created by Maguire Krist on 8/23/25.
//

#ifndef MATERIAL_H
#define MATERIAL_H

#include "render_primitives.h"
#include "resource.h"

struct Material {
    std::string key;
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;

    //TODO: Have a Material own its own resources like descriptor sets, etc.
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<std::shared_ptr<Resource>> resources;

    std::vector<PushConstant> pushConstants;

    //Not sure if having a call back function make sense.
    //std::function<void()> buffer_update;
    //~Material();
};


#endif //MATERIAL_H
