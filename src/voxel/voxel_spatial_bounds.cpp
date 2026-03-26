#include "voxel_spatial_bounds.h"

#include <algorithm>
#include <array>
#include <limits>

#include <glm/ext/matrix_transform.hpp>

VoxelSpatialBounds union_bounds(const VoxelSpatialBounds& lhs, const VoxelSpatialBounds& rhs)
{
    if (!lhs.valid)
    {
        return rhs;
    }
    if (!rhs.valid)
    {
        return lhs;
    }

    return VoxelSpatialBounds{
        .valid = true,
        .min = glm::min(lhs.min, rhs.min),
        .max = glm::max(lhs.max, rhs.max)
    };
}

VoxelSpatialBounds evaluate_voxel_model_local_bounds(const VoxelModel& model)
{
    const VoxelBounds bounds = model.bounds();
    if (!bounds.valid)
    {
        return {};
    }

    return VoxelSpatialBounds{
        .valid = true,
        .min = (glm::vec3(
            static_cast<float>(bounds.min.x),
            static_cast<float>(bounds.min.y),
            static_cast<float>(bounds.min.z)) * model.voxelSize) - model.pivot,
        .max = (glm::vec3(
            static_cast<float>(bounds.max.x + 1),
            static_cast<float>(bounds.max.y + 1),
            static_cast<float>(bounds.max.z + 1)) * model.voxelSize) - model.pivot
    };
}

VoxelSpatialBounds transform_bounds(const VoxelSpatialBounds& bounds, const glm::mat4& transform)
{
    if (!bounds.valid)
    {
        return {};
    }

    const std::array<glm::vec3, 8> corners{
        glm::vec3(bounds.min.x, bounds.min.y, bounds.min.z),
        glm::vec3(bounds.min.x, bounds.min.y, bounds.max.z),
        glm::vec3(bounds.min.x, bounds.max.y, bounds.min.z),
        glm::vec3(bounds.min.x, bounds.max.y, bounds.max.z),
        glm::vec3(bounds.max.x, bounds.min.y, bounds.min.z),
        glm::vec3(bounds.max.x, bounds.min.y, bounds.max.z),
        glm::vec3(bounds.max.x, bounds.max.y, bounds.min.z),
        glm::vec3(bounds.max.x, bounds.max.y, bounds.max.z)
    };

    glm::vec3 transformedMin(std::numeric_limits<float>::max());
    glm::vec3 transformedMax(std::numeric_limits<float>::lowest());

    for (const glm::vec3& corner : corners)
    {
        const glm::vec3 transformedCorner = glm::vec3(transform * glm::vec4(corner, 1.0f));
        transformedMin = glm::min(transformedMin, transformedCorner);
        transformedMax = glm::max(transformedMax, transformedCorner);
    }

    return VoxelSpatialBounds{
        .valid = true,
        .min = transformedMin,
        .max = transformedMax
    };
}

VoxelSpatialBounds evaluate_voxel_render_instance_bounds(const VoxelRenderInstance& instance)
{
    if (instance.asset == nullptr)
    {
        return {};
    }

    return transform_bounds(evaluate_voxel_model_local_bounds(instance.asset->model), instance.model_matrix());
}

VoxelSpatialBounds evaluate_voxel_render_instances_bounds(const std::vector<VoxelRenderInstance>& instances)
{
    VoxelSpatialBounds result{};
    for (const VoxelRenderInstance& instance : instances)
    {
        result = union_bounds(result, evaluate_voxel_render_instance_bounds(instance));
    }

    return result;
}
