#include "voxel_render_instance.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace
{
    glm::mat4 basis_from_attachment(const VoxelAttachment& attachment)
    {
        const glm::vec3 forward = glm::length(attachment.forward) > 0.0001f
            ? glm::normalize(attachment.forward)
            : glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 up = glm::length(attachment.up) > 0.0001f
            ? glm::normalize(attachment.up)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::cross(up, forward);

        if (glm::length(right) <= 0.0001f)
        {
            right = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        else
        {
            right = glm::normalize(right);
        }

        up = glm::normalize(glm::cross(forward, right));

        glm::mat4 result{1.0f};
        result[0] = glm::vec4(forward, 0.0f);
        result[1] = glm::vec4(up, 0.0f);
        result[2] = glm::vec4(right, 0.0f);
        return result;
    }
}

bool VoxelRenderInstance::is_renderable() const noexcept
{
    return visible && asset != nullptr && asset->mesh != nullptr;
}

glm::mat4 VoxelRenderInstance::model_matrix() const
{
    glm::mat4 result = glm::translate(glm::mat4(1.0f), position);
    result *= glm::mat4_cast(rotation);
    result = glm::scale(result, glm::vec3(scale));
    result *= glm::translate(glm::mat4(1.0f), -renderAnchorOffset);
    return result;
}

glm::vec3 VoxelRenderInstance::world_point_from_asset_local(const glm::vec3& assetLocalPoint) const
{
    if (asset == nullptr)
    {
        return position;
    }

    const glm::vec3 pivotRelativePoint = assetLocalPoint - asset->model.pivot;
    return glm::vec3(model_matrix() * glm::vec4(pivotRelativePoint, 1.0f));
}

glm::vec3 VoxelRenderInstance::light_sample_world_position() const
{
    const glm::vec3 rotatedOffset = rotation * (lightSampleOffset * scale);
    return position + rotatedOffset;
}

std::optional<glm::mat4> VoxelRenderInstance::attachment_world_transform(const std::string_view attachmentName) const
{
    if (asset == nullptr)
    {
        return std::nullopt;
    }

    const VoxelAttachment* const attachment = asset->model.find_attachment(attachmentName);
    if (attachment == nullptr)
    {
        return std::nullopt;
    }

    glm::mat4 localTransform = glm::translate(glm::mat4(1.0f), attachment->position - asset->model.pivot);
    localTransform *= basis_from_attachment(*attachment);
    return model_matrix() * localTransform;
}
