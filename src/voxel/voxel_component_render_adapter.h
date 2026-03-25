#pragma once

#include <string>
#include <vector>

#include "components/game_object.h"
#include "voxel_assembly_asset_manager.h"
#include "voxel_asset_manager.h"
#include "voxel_render_instance.h"

struct VoxelComponentRenderEntry
{
    std::string stableId{};
    VoxelRenderInstance renderInstance{};
};

struct VoxelComponentRenderBundle
{
    std::string assetId{};
    std::vector<VoxelComponentRenderEntry> entries{};
    std::string diagnostic{};

    [[nodiscard]] bool has_error() const noexcept
    {
        return !diagnostic.empty();
    }
};

[[nodiscard]] VoxelComponentRenderBundle build_voxel_component_render_bundle(
    const GameObject& object,
    VoxelAssemblyAssetManager& assemblyAssetManager,
    VoxelAssetManager& assetManager);
