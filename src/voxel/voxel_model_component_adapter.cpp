#include "voxel_model_component_adapter.h"

std::optional<VoxelRenderInstance> build_voxel_render_instance(
    const VoxelModelComponent& component,
    VoxelAssetManager& assetManager)
{
    if (!component.visible)
    {
        return std::nullopt;
    }

    const std::shared_ptr<VoxelRuntimeAsset> asset = assetManager.load_or_get(component.assetId);
    if (asset == nullptr)
    {
        return std::nullopt;
    }

    return VoxelRenderInstance{
        .asset = asset,
        .position = component.position,
        .rotation = component.rotation,
        .scale = component.scale,
        .layer = RenderLayer::Opaque,
        .lightingMode = component.lightingMode,
        .lightSampleOffset = component.lightSampleOffset,
        .lightAffectMask = component.lightAffectMask,
        .visible = component.visible
    };
}
