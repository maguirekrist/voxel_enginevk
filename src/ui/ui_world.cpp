#include "ui_world.h"

#include <algorithm>

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

namespace ui
{
    std::optional<glm::vec2> project_world_anchor(const WorldAnchor& anchor, const ProjectionParameters& parameters)
    {
        const glm::vec3 delta = anchor.worldPosition - parameters.cameraPosition;
        const float distance = glm::length(delta);
        if (distance > anchor.maxDistance)
        {
            return std::nullopt;
        }

        const glm::vec4 clip = parameters.viewProjection * glm::vec4(anchor.worldPosition, 1.0f);
        if (clip.w <= 0.0f)
        {
            return std::nullopt;
        }

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f)
        {
            return std::nullopt;
        }

        glm::vec2 screen{
            (ndc.x * 0.5f + 0.5f) * static_cast<float>(parameters.viewport.width),
            (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(parameters.viewport.height)
        };

        if (anchor.clampToViewport)
        {
            screen.x = std::clamp(screen.x, 0.0f, static_cast<float>(parameters.viewport.width));
            screen.y = std::clamp(screen.y, 0.0f, static_cast<float>(parameters.viewport.height));
        }
        else if (screen.x < 0.0f ||
            screen.y < 0.0f ||
            screen.x > static_cast<float>(parameters.viewport.width) ||
            screen.y > static_cast<float>(parameters.viewport.height))
        {
            return std::nullopt;
        }

        return screen + anchor.screenOffset;
    }
}
