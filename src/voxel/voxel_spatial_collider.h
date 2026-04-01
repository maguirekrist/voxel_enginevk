#pragma once

#include <string>

#include "components/game_object.h"
#include "components/voxel_animation_component.h"
#include "components/voxel_assembly_component.h"
#include "components/voxel_model_component.h"
#include "physics/aabb.h"
#include "voxel_assembly_asset_manager.h"
#include "voxel_asset_manager.h"

struct VoxelSpatialColliderEvaluation
{
    bool valid{false};
    AABB localBounds{
        .min = glm::vec3(0.0f),
        .max = glm::vec3(0.0f)
    };
    std::string diagnostic{};
};

[[nodiscard]] VoxelSpatialColliderEvaluation evaluate_voxel_model_local_collider(
    const VoxelModelComponent& component,
    VoxelAssetManager& assetManager);

[[nodiscard]] VoxelSpatialColliderEvaluation evaluate_voxel_assembly_local_collider(
    const VoxelAssemblyComponent& component,
    VoxelAssemblyAssetManager& assemblyAssetManager,
    VoxelAssetManager& assetManager,
    const VoxelAssemblyPose* pose = nullptr);

[[nodiscard]] VoxelSpatialColliderEvaluation evaluate_voxel_local_collider(
    const GameObject& object,
    VoxelAssemblyAssetManager& assemblyAssetManager,
    VoxelAssetManager& assetManager);
