#pragma once

#include <string_view>

#include "components/voxel_animation_component.h"
#include "components/voxel_assembly_component.h"
#include "voxel_assembly_asset.h"
#include "voxel_animation_clip_asset_manager.h"
#include "voxel_animation_controller_asset_manager.h"

void set_voxel_animation_float_parameter(
    VoxelAnimationComponent& component,
    std::string_view parameterId,
    float value);

void set_voxel_animation_bool_parameter(
    VoxelAnimationComponent& component,
    std::string_view parameterId,
    bool value);

void trigger_voxel_animation_parameter(
    VoxelAnimationComponent& component,
    std::string_view parameterId);

void clear_voxel_animation_events(VoxelAnimationComponent& component);

void consume_voxel_animation_root_motion(VoxelAnimationComponent& component);

[[nodiscard]] VoxelAssemblyPose sample_voxel_animation_clip_pose(
    const VoxelAnimationClipAsset& clip,
    float timeSeconds);

[[nodiscard]] glm::vec3 sample_voxel_animation_clip_motion_source_position(
    const VoxelAnimationClipAsset& clip,
    std::string_view fallbackSourcePartId,
    float timeSeconds);

void tick_voxel_animation_component(
    VoxelAnimationComponent& component,
    const VoxelAssemblyComponent& assemblyComponent,
    const VoxelAssemblyAsset& assemblyAsset,
    VoxelAnimationControllerAssetManager& controllerAssetManager,
    VoxelAnimationClipAssetManager& clipAssetManager,
    float deltaTime);
