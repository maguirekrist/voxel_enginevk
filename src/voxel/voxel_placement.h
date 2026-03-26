#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include <glm/vec3.hpp>

#include "voxel_model.h"
#include "voxel_render_instance.h"

enum class VoxelPlacementPolicy : uint8_t
{
    Pivot = 0,
    BottomCenter = 1,
    BoundsCenter = 2,
    NamedAttachment = 3
};

[[nodiscard]] glm::vec3 resolve_voxel_model_placement_anchor(
    const VoxelModel& model,
    VoxelPlacementPolicy placementPolicy,
    std::string_view attachmentName);

[[nodiscard]] glm::vec3 resolve_voxel_assembly_placement_anchor(
    const std::unordered_map<std::string, VoxelRenderInstance>& resolvedInstances,
    std::string_view rootPartId,
    VoxelPlacementPolicy placementPolicy,
    std::string_view attachmentName,
    std::string* diagnostic = nullptr);
