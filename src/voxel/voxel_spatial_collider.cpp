#include "voxel_spatial_collider.h"

#include <format>

#include "voxel_assembly_asset.h"
#include "voxel_assembly_component_adapter.h"
#include "voxel_model_component_adapter.h"
#include "voxel_placement.h"
#include "voxel_spatial_bounds.h"

namespace
{
    [[nodiscard]] VoxelSpatialColliderEvaluation make_eval_from_bounds(
        const VoxelSpatialBounds& bounds,
        const std::string& diagnostic = {})
    {
        VoxelSpatialColliderEvaluation evaluation{};
        evaluation.valid = bounds.valid;
        evaluation.diagnostic = diagnostic;
        if (bounds.valid)
        {
            evaluation.localBounds = AABB{
                .min = bounds.min,
                .max = bounds.max
            };
        }
        return evaluation;
    }

    [[nodiscard]] VoxelSpatialBounds scaled_bounds(
        const VoxelSpatialBounds& bounds,
        const float scale)
    {
        if (!bounds.valid)
        {
            return {};
        }

        return VoxelSpatialBounds{
            .valid = true,
            .min = bounds.min * scale,
            .max = bounds.max * scale
        };
    }
}

VoxelSpatialColliderEvaluation evaluate_voxel_model_local_collider(
    const VoxelModelComponent& component,
    VoxelAssetManager& assetManager)
{
    if (!component.visible || component.assetId.empty())
    {
        return {};
    }

    const std::shared_ptr<const VoxelRuntimeAsset> asset = assetManager.load_or_get(component.assetId);
    if (asset == nullptr)
    {
        return VoxelSpatialColliderEvaluation{
            .diagnostic = std::format("Missing voxel model '{}'.", component.assetId)
        };
    }

    const glm::vec3 placementAnchor = resolve_voxel_model_placement_anchor(
        asset->model,
        component.placementPolicy,
        component.placementAttachmentName);
    VoxelSpatialBounds bounds = evaluate_voxel_model_local_bounds(asset->model);
    if (bounds.valid)
    {
        bounds.min = (bounds.min - placementAnchor) * component.scale;
        bounds.max = (bounds.max - placementAnchor) * component.scale;
    }

    return make_eval_from_bounds(bounds);
}

VoxelSpatialColliderEvaluation evaluate_voxel_assembly_local_collider(
    const VoxelAssemblyComponent& component,
    VoxelAssemblyAssetManager& assemblyAssetManager,
    VoxelAssetManager& assetManager)
{
    if (!component.visible || component.assetId.empty())
    {
        return {};
    }

    const std::shared_ptr<const VoxelAssemblyAsset> assembly = assemblyAssetManager.load_or_get(component.assetId);
    if (assembly == nullptr)
    {
        return VoxelSpatialColliderEvaluation{
            .diagnostic = std::format("Missing voxel assembly '{}'.", component.assetId)
        };
    }

    switch (assembly->collision.mode)
    {
    case VoxelAssemblyCollisionMode::None:
        return {};

    case VoxelAssemblyCollisionMode::CustomBounds:
        return make_eval_from_bounds(scaled_bounds(VoxelSpatialBounds{
            .valid = true,
            .min = assembly->collision.customBoundsMin,
            .max = assembly->collision.customBoundsMax
        }, component.scale));

    case VoxelAssemblyCollisionMode::TaggedParts:
    default:
        break;
    }

    const VoxelAssemblyLocalBundle localBundle =
        build_voxel_assembly_local_bundle(component, assemblyAssetManager, assetManager);
    if (localBundle.has_error())
    {
        return VoxelSpatialColliderEvaluation{
            .diagnostic = localBundle.diagnostic
        };
    }

    VoxelSpatialBounds aggregateBounds{};
    bool includedAnyPart = false;
    for (const VoxelAssemblyResolvedPart& localPart : localBundle.parts)
    {
        const VoxelAssemblyPartDefinition* const part = assembly->find_part(localPart.partId);
        if (part == nullptr || !part->contributesToCollision)
        {
            continue;
        }

        aggregateBounds = union_bounds(aggregateBounds, evaluate_voxel_render_instance_bounds(localPart.renderInstance));
        includedAnyPart = true;
    }

    if (!includedAnyPart)
    {
        return {};
    }

    return make_eval_from_bounds(aggregateBounds);
}

VoxelSpatialColliderEvaluation evaluate_voxel_local_collider(
    const GameObject& object,
    VoxelAssemblyAssetManager& assemblyAssetManager,
    VoxelAssetManager& assetManager)
{
    if (object.Has<VoxelAssemblyComponent>())
    {
        const VoxelAssemblyComponent& component = object.Get<VoxelAssemblyComponent>();
        if (!component.assetId.empty())
        {
            return evaluate_voxel_assembly_local_collider(component, assemblyAssetManager, assetManager);
        }
    }

    if (object.Has<VoxelModelComponent>())
    {
        const VoxelModelComponent& component = object.Get<VoxelModelComponent>();
        if (!component.assetId.empty())
        {
            return evaluate_voxel_model_local_collider(component, assetManager);
        }
    }

    return {};
}
