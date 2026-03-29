#pragma once

#include <limits>
#include <optional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "ui_text.h"

namespace ui
{
    enum class WorldLabelMode : uint8_t
    {
        ProjectedOverlay = 0,
        Billboard3D = 1
    };

    struct WorldAnchor
    {
        glm::vec3 worldPosition{0.0f};
        glm::vec2 screenOffset{0.0f};
        float maxDistance{std::numeric_limits<float>::infinity()};
        bool clampToViewport{false};
    };

    struct WorldLabel
    {
        ElementId id{};
        ScreenId screenId{};
        WorldLabelMode mode{WorldLabelMode::ProjectedOverlay};
        WorldAnchor anchor{};
        TextRun text{};
        Style style{};
    };

    struct ProjectionParameters
    {
        glm::mat4 viewProjection{1.0f};
        Extent2D viewport{};
        glm::vec3 cameraPosition{0.0f};
    };

    [[nodiscard]] std::optional<glm::vec2> project_world_anchor(const WorldAnchor& anchor, const ProjectionParameters& parameters);

    class WorldLabelCollector
    {
    public:
        WorldLabelCollector() = default;
        explicit WorldLabelCollector(std::vector<WorldLabel>* labels) : _labels(labels) {}

        void add_label(const WorldLabel& label) const
        {
            if (_labels != nullptr)
            {
                _labels->push_back(label);
            }
        }

    private:
        std::vector<WorldLabel>* _labels{nullptr};
    };
}
