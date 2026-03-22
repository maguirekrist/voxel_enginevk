#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>

#include "render/render_primitives.h"
#include "voxel_runtime_asset.h"

struct VoxelRenderInstance
{
    std::shared_ptr<VoxelRuntimeAsset> asset{};
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float scale{1.0f};
    RenderLayer layer{RenderLayer::Opaque};
    bool visible{true};

    [[nodiscard]] bool is_renderable() const noexcept;
    [[nodiscard]] glm::mat4 model_matrix() const;
    [[nodiscard]] glm::vec3 world_point_from_asset_local(const glm::vec3& assetLocalPoint) const;
    [[nodiscard]] std::optional<glm::mat4> attachment_world_transform(std::string_view attachmentName) const;
};
