#pragma once

#include <cmath>

#include <glm/ext/quaternion_float.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

#include "voxel_model.h"

namespace voxel::orientation
{
    inline constexpr glm::vec3 ForwardAxis{1.0f, 0.0f, 0.0f};
    inline constexpr glm::vec3 UpAxis{0.0f, 1.0f, 0.0f};
    inline constexpr glm::vec3 RightAxis{0.0f, 0.0f, 1.0f};

    [[nodiscard]] inline glm::vec3 normalize_or_fallback(const glm::vec3& value, const glm::vec3& fallback)
    {
        const float length = glm::length(value);
        if (!std::isfinite(length) || length <= 0.0001f)
        {
            return fallback;
        }

        return value / length;
    }

    inline void orthonormalize_basis(glm::vec3& forward, glm::vec3& up)
    {
        forward = normalize_or_fallback(forward, ForwardAxis);
        glm::vec3 upCandidate = normalize_or_fallback(up, UpAxis);
        if (std::abs(glm::dot(forward, upCandidate)) >= 0.999f)
        {
            upCandidate = std::abs(glm::dot(forward, UpAxis)) < 0.999f ? UpAxis : RightAxis;
        }

        const glm::vec3 right = glm::normalize(glm::cross(upCandidate, forward));
        up = glm::normalize(glm::cross(forward, right));
    }

    inline void sanitize_attachment_basis(VoxelAttachment& attachment)
    {
        orthonormalize_basis(attachment.forward, attachment.up);
    }

    [[nodiscard]] inline glm::quat basis_quat_from_attachment(const VoxelAttachment& attachment)
    {
        glm::vec3 forward = attachment.forward;
        glm::vec3 up = attachment.up;
        orthonormalize_basis(forward, up);
        const glm::vec3 right = glm::normalize(glm::cross(up, forward));

        glm::mat3 basis{1.0f};
        basis[0] = forward;
        basis[1] = up;
        basis[2] = right;
        return glm::normalize(glm::quat_cast(basis));
    }
}
