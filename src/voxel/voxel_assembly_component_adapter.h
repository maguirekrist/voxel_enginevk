#pragma once

#include <string>
#include <vector>

#include "components/voxel_assembly_component.h"
#include "voxel_animation_pose.h"
#include "voxel_assembly_asset_manager.h"
#include "voxel_asset_manager.h"
#include "voxel_render_instance.h"

struct VoxelAssemblyResolvedPart
{
    std::string partId{};
    VoxelRenderInstance renderInstance{};
};

struct VoxelAssemblyLocalBundle
{
    std::string assemblyAssetId{};
    std::vector<VoxelAssemblyResolvedPart> parts{};
    std::string diagnostic{};

    [[nodiscard]] bool has_error() const noexcept
    {
        return !diagnostic.empty();
    }
};

struct VoxelAssemblyRenderBundle
{
    std::string assemblyAssetId{};
    std::vector<VoxelAssemblyResolvedPart> parts{};
    std::string diagnostic{};

    [[nodiscard]] bool has_error() const noexcept
    {
        return !diagnostic.empty();
    }
};

[[nodiscard]] VoxelAssemblyLocalBundle build_voxel_assembly_local_bundle(
    const VoxelAssemblyComponent& component,
    VoxelAssemblyAssetManager& assemblyAssetManager,
    VoxelAssetManager& assetManager,
    const VoxelAssemblyPose* pose = nullptr);

[[nodiscard]] VoxelAssemblyRenderBundle build_voxel_assembly_render_bundle(
    const VoxelAssemblyComponent& component,
    VoxelAssemblyAssetManager& assemblyAssetManager,
    VoxelAssetManager& assetManager,
    const VoxelAssemblyPose* pose = nullptr);
