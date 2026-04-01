#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

#include "camera.h"
#include "orbit_orientation_gizmo.h"

namespace editor
{
    struct OrbitCameraState
    {
        float yawDegrees{40.0f};
        float pitchDegrees{24.0f};
        float distance{5.5f};
        float minDistance{0.75f};
        float maxDistance{64.0f};
    };

    [[nodiscard]] inline glm::vec3 orbit_front(const float yawDegrees, const float pitchDegrees)
    {
        const float yaw = glm::radians(yawDegrees);
        const float pitch = glm::radians(pitchDegrees);
        return glm::normalize(glm::vec3(
            std::cos(pitch) * std::cos(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::sin(yaw)));
    }

    inline void update_orbit_camera(
        Camera& camera,
        const glm::vec3& target,
        OrbitCameraState& state,
        const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f))
    {
        state.pitchDegrees = std::clamp(state.pitchDegrees, -85.0f, 85.0f);
        state.distance = std::clamp(state.distance, state.minDistance, state.maxDistance);

        const glm::vec3 front = orbit_front(state.yawDegrees, state.pitchDegrees);
        camera._front = front;
        camera._up = up;
        camera._position = target - (front * state.distance);
        camera.update(0.0f);
    }

    inline bool sync_orbit_from_view_matrix(
        OrbitCameraState& state,
        const glm::mat4& viewMatrix,
        const glm::vec3& target,
        Camera* const camera = nullptr,
        const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f))
    {
        const glm::mat4 inverseView = glm::inverse(viewMatrix);
        const glm::vec3 position = glm::vec3(inverseView[3]);
        const glm::vec3 front = glm::normalize(target - position);

        if (!std::isfinite(front.x) || !std::isfinite(front.y) || !std::isfinite(front.z) || glm::length(front) <= 0.0001f)
        {
            return false;
        }

        state.distance = glm::distance(position, target);
        state.yawDegrees = glm::degrees(std::atan2(front.z, front.x));
        state.pitchDegrees = glm::degrees(std::asin(std::clamp(front.y, -1.0f, 1.0f)));

        if (camera != nullptr)
        {
            camera->_position = position;
            camera->_front = front;
            camera->_up = up;
            camera->_view = viewMatrix;
        }

        return true;
    }

    inline bool draw_orbit_orientation_gizmo(
        Camera& camera,
        const glm::vec3& target,
        OrbitCameraState& state,
        const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f))
    {
        glm::mat4 gizmoView = camera._view;
        if (!::draw_orbit_orientation_gizmo(gizmoView, state.distance))
        {
            return false;
        }

        return sync_orbit_from_view_matrix(state, gizmoView, target, &camera, up);
    }
}
