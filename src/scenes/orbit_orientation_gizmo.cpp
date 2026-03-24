#include "orbit_orientation_gizmo.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "third_party/imoguizmo/imoguizmo.hpp"

bool draw_orbit_orientation_gizmo(glm::mat4& viewMatrix, const float orbitDistance)
{
    ImGuiViewport* const viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
    {
        return false;
    }

    constexpr float gizmoSize = 110.0f;
    constexpr float gizmoPadding = 16.0f;

    const glm::mat4 gizmoProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);

    ImOGuizmo::SetRect(
        viewport->WorkPos.x + viewport->WorkSize.x - gizmoSize - gizmoPadding,
        viewport->WorkPos.y + gizmoPadding,
        gizmoSize);
    ImOGuizmo::BeginFrame(false);

    return ImOGuizmo::DrawGizmo(glm::value_ptr(viewMatrix), glm::value_ptr(gizmoProjection), orbitDistance);
}
