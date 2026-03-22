#include "voxel_picking.h"

#include <algorithm>
#include <cmath>

namespace
{
    glm::vec3 unproject_point(const glm::mat4& inverseViewProjection, const glm::vec3& ndc)
    {
        const glm::vec4 clip = inverseViewProjection * glm::vec4(ndc, 1.0f);
        return glm::vec3(clip) / clip.w;
    }

    glm::ivec3 select_outward_normal_from_hit_point(
        const glm::vec3& hitPoint,
        const glm::vec3& boxMin,
        const glm::vec3& boxMax,
        const glm::vec3& rayDirection,
        const glm::ivec3& fallback)
    {
        const glm::vec3 extents = glm::abs(boxMax - boxMin);
        const float maxExtent = std::max(extents.x, std::max(extents.y, extents.z));
        const float epsilon = std::max(maxExtent * 0.0005f, 0.0001f);
        glm::ivec3 bestNormal = fallback;
        float bestWeight = -1.0f;
        int bestPriority = 99;

        const auto consider = [&](const glm::ivec3& normal, const float weight, const int priority)
        {
            if (weight > bestWeight || (std::abs(weight - bestWeight) <= 0.00001f && priority < bestPriority))
            {
                bestNormal = normal;
                bestWeight = weight;
                bestPriority = priority;
            }
        };

        if (std::abs(hitPoint.x - boxMin.x) <= epsilon)
        {
            consider(glm::ivec3(-1, 0, 0), std::abs(rayDirection.x), 0);
        }
        if (std::abs(hitPoint.x - boxMax.x) <= epsilon)
        {
            consider(glm::ivec3(1, 0, 0), std::abs(rayDirection.x), 0);
        }
        if (std::abs(hitPoint.y - boxMin.y) <= epsilon)
        {
            consider(glm::ivec3(0, -1, 0), std::abs(rayDirection.y), 1);
        }
        if (std::abs(hitPoint.y - boxMax.y) <= epsilon)
        {
            consider(glm::ivec3(0, 1, 0), std::abs(rayDirection.y), 1);
        }
        if (std::abs(hitPoint.z - boxMin.z) <= epsilon)
        {
            consider(glm::ivec3(0, 0, -1), std::abs(rayDirection.z), 2);
        }
        if (std::abs(hitPoint.z - boxMax.z) <= epsilon)
        {
            consider(glm::ivec3(0, 0, 1), std::abs(rayDirection.z), 2);
        }

        return bestNormal;
    }
}

namespace voxel::picking
{
    std::optional<FaceDirection> face_from_outward_normal(const glm::ivec3& normal)
    {
        if (normal.x > 0)
        {
            return LEFT_FACE;
        }
        if (normal.x < 0)
        {
            return RIGHT_FACE;
        }
        if (normal.y > 0)
        {
            return TOP_FACE;
        }
        if (normal.y < 0)
        {
            return BOTTOM_FACE;
        }
        if (normal.z > 0)
        {
            return FRONT_FACE;
        }
        if (normal.z < 0)
        {
            return BACK_FACE;
        }

        return std::nullopt;
    }

    Ray build_ray_from_cursor(
        const int cursorX,
        const int cursorY,
        const VkExtent2D viewport,
        const glm::vec3& cameraPosition,
        const glm::mat4& inverseViewProjection)
    {
        const float width = std::max(static_cast<float>(viewport.width), 1.0f);
        const float height = std::max(static_cast<float>(viewport.height), 1.0f);
        const float pixelCenterX = static_cast<float>(cursorX) + 0.5f;
        const float pixelCenterY = static_cast<float>(cursorY) + 0.5f;
        const float ndcX = (2.0f * pixelCenterX / width) - 1.0f;
        const float ndcY = (2.0f * pixelCenterY / height) - 1.0f;

        // glm::perspective uses OpenGL-style clip depth (-1..1) unless configured otherwise.
        const glm::vec3 nearPoint = unproject_point(inverseViewProjection, glm::vec3(ndcX, ndcY, -1.0f));
        const glm::vec3 farPoint = unproject_point(inverseViewProjection, glm::vec3(ndcX, ndcY, 1.0f));

        return Ray{
            .origin = cameraPosition,
            .direction = glm::normalize(farPoint - nearPoint)
        };
    }

    std::optional<BoxHit> intersect_ray_box(
        const Ray& ray,
        const glm::vec3& boxMin,
        const glm::vec3& boxMax,
        const float maxDistance)
    {
        float tMin = 0.0f;
        float tMax = maxDistance;
        glm::ivec3 outwardNormal{0};
        glm::ivec3 exitNormal{0};

        for (int axis = 0; axis < 3; ++axis)
        {
            const float origin = ray.origin[axis];
            const float direction = ray.direction[axis];
            const float minValue = boxMin[axis];
            const float maxValue = boxMax[axis];

            if (std::abs(direction) < 0.00001f)
            {
                if (origin < minValue || origin > maxValue)
                {
                    return std::nullopt;
                }
                continue;
            }

            float inverse = 1.0f / direction;
            float t1 = (minValue - origin) * inverse;
            float t2 = (maxValue - origin) * inverse;
            glm::ivec3 nearNormal{0};
            glm::ivec3 farNormal{0};
            nearNormal[axis] = -1;
            farNormal[axis] = 1;

            if (t1 > t2)
            {
                std::swap(t1, t2);
                std::swap(nearNormal, farNormal);
            }

            if (t1 > tMin)
            {
                tMin = t1;
                outwardNormal = nearNormal;
            }

            if (t2 < tMax)
            {
                tMax = t2;
                exitNormal = farNormal;
            }
            if (tMin > tMax)
            {
                return std::nullopt;
            }
        }

        if (tMin < 0.0f || tMin > maxDistance)
        {
            return std::nullopt;
        }

        const float hitDistance = outwardNormal == glm::ivec3(0) ? tMax : tMin;
        const glm::ivec3 fallbackNormal = outwardNormal == glm::ivec3(0) ? exitNormal : outwardNormal;
        const glm::vec3 hitPoint = ray.origin + (ray.direction * hitDistance);
        return BoxHit{
            .distance = hitDistance,
            .outwardNormal = select_outward_normal_from_hit_point(hitPoint, boxMin, boxMax, ray.direction, fallbackNormal)
        };
    }
}
