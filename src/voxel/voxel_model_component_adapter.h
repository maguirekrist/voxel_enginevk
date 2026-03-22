#pragma once

#include <optional>

#include "components/voxel_model_component.h"
#include "voxel_asset_manager.h"
#include "voxel_render_instance.h"

[[nodiscard]] std::optional<VoxelRenderInstance> build_voxel_render_instance(
    const VoxelModelComponent& component,
    VoxelAssetManager& assetManager);
