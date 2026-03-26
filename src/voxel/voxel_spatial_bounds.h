#pragma once

#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/vec3.hpp>

#include "voxel_model.h"
#include "voxel_render_instance.h"

struct VoxelSpatialBounds
{
    bool valid{false};
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    [[nodiscard]] glm::vec3 center() const
    {
        return valid ? (min + max) * 0.5f : glm::vec3(0.0f);
    }

    [[nodiscard]] glm::vec3 size() const
    {
        return valid ? (max - min) : glm::vec3(0.0f);
    }
};

[[nodiscard]] VoxelSpatialBounds union_bounds(const VoxelSpatialBounds& lhs, const VoxelSpatialBounds& rhs);
[[nodiscard]] VoxelSpatialBounds evaluate_voxel_model_local_bounds(const VoxelModel& model);
[[nodiscard]] VoxelSpatialBounds transform_bounds(const VoxelSpatialBounds& bounds, const glm::mat4& transform);
[[nodiscard]] VoxelSpatialBounds evaluate_voxel_render_instance_bounds(const VoxelRenderInstance& instance);
[[nodiscard]] VoxelSpatialBounds evaluate_voxel_render_instances_bounds(const std::vector<VoxelRenderInstance>& instances);
