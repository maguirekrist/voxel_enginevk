#include "voxel_placement.h"

#include <format>
#include <vector>

#include "voxel_spatial_bounds.h"

namespace
{
    [[nodiscard]] glm::vec3 bottom_center_from_bounds(const VoxelSpatialBounds& bounds)
    {
        return glm::vec3(
            (bounds.min.x + bounds.max.x) * 0.5f,
            bounds.min.y,
            (bounds.min.z + bounds.max.z) * 0.5f);
    }
}

glm::vec3 resolve_voxel_model_placement_anchor(
    const VoxelModel& model,
    const VoxelPlacementPolicy placementPolicy,
    const std::string_view attachmentName)
{
    switch (placementPolicy)
    {
    case VoxelPlacementPolicy::BottomCenter:
    {
        const VoxelSpatialBounds bounds = evaluate_voxel_model_local_bounds(model);
        return bounds.valid ? bottom_center_from_bounds(bounds) : glm::vec3(0.0f);
    }

    case VoxelPlacementPolicy::BoundsCenter:
    {
        const VoxelSpatialBounds bounds = evaluate_voxel_model_local_bounds(model);
        return bounds.center();
    }

    case VoxelPlacementPolicy::NamedAttachment:
        if (const VoxelAttachment* const attachment = model.find_attachment(attachmentName); attachment != nullptr)
        {
            return attachment->position - model.pivot;
        }
        return glm::vec3(0.0f);

    case VoxelPlacementPolicy::Pivot:
    default:
        return glm::vec3(0.0f);
    }
}

glm::vec3 resolve_voxel_assembly_placement_anchor(
    const std::unordered_map<std::string, VoxelRenderInstance>& resolvedInstances,
    const std::string_view rootPartId,
    const VoxelPlacementPolicy placementPolicy,
    const std::string_view attachmentName,
    std::string* const diagnostic)
{
    switch (placementPolicy)
    {
    case VoxelPlacementPolicy::BottomCenter:
    {
        std::vector<VoxelRenderInstance> instances{};
        instances.reserve(resolvedInstances.size());
        for (const auto& [partId, instance] : resolvedInstances)
        {
            (void)partId;
            if (instance.is_renderable())
            {
                instances.push_back(instance);
            }
        }

        const VoxelSpatialBounds bounds = evaluate_voxel_render_instances_bounds(instances);
        return bounds.valid ? bottom_center_from_bounds(bounds) : glm::vec3(0.0f);
    }

    case VoxelPlacementPolicy::BoundsCenter:
    {
        std::vector<VoxelRenderInstance> instances{};
        instances.reserve(resolvedInstances.size());
        for (const auto& [partId, instance] : resolvedInstances)
        {
            (void)partId;
            if (instance.is_renderable())
            {
                instances.push_back(instance);
            }
        }

        return evaluate_voxel_render_instances_bounds(instances).center();
    }

    case VoxelPlacementPolicy::NamedAttachment:
    {
        if (resolvedInstances.empty())
        {
            return glm::vec3(0.0f);
        }

        const auto rootIt = !rootPartId.empty() ? resolvedInstances.find(std::string(rootPartId)) : resolvedInstances.end();
        const VoxelRenderInstance* referenceInstance = rootIt != resolvedInstances.end()
            ? &rootIt->second
            : &resolvedInstances.begin()->second;

        if (const std::optional<glm::mat4> attachmentTransform =
            referenceInstance->attachment_world_transform(attachmentName);
            attachmentTransform.has_value())
        {
            return glm::vec3(attachmentTransform.value()[3]);
        }

        if (diagnostic != nullptr && diagnostic->empty() && !attachmentName.empty())
        {
            *diagnostic = std::format(
                "Placement attachment '{}' was not found on assembly root/reference part.",
                attachmentName);
        }
        return glm::vec3(0.0f);
    }

    case VoxelPlacementPolicy::Pivot:
    default:
        return glm::vec3(0.0f);
    }
}
