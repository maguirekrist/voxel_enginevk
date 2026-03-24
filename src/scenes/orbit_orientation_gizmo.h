#pragma once

#include <glm/ext/matrix_float4x4.hpp>

[[nodiscard]] bool draw_orbit_orientation_gizmo(glm::mat4& viewMatrix, float orbitDistance);
