#pragma once

#include <string>
#include <unordered_map>

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>

#include "game_object.h"
#include "render/render_primitives.h"
#include "voxel/voxel_placement.h"

struct VoxelAssemblyComponent final : Component
{
    std::string assetId{};
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float scale{1.0f};
    VoxelPlacementPolicy placementPolicy{VoxelPlacementPolicy::Pivot};
    std::string placementAttachmentName{};
    glm::vec3 renderAnchorOffset{0.0f};
    LightingMode lightingMode{LightingMode::SampledRuntime};
    glm::vec3 lightSampleOffset{0.0f};
    uint32_t lightAffectMask{0xFFFFFFFFu};
    bool visible{true};
    std::unordered_map<std::string, std::string> partBindingStateOverrides{};
    std::unordered_map<std::string, std::string> slotModelAssetOverrides{};
};
