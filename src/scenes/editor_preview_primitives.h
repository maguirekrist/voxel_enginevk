#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "ImGuizmo.h"
#include "imgui.h"
#include "render/material_manager.h"
#include "render/mesh.h"
#include "render/mesh_manager.h"
#include "render/mesh_release_queue.h"
#include "render/scene_render_state.h"
#include "scene_services.h"
#include "voxel/voxel_assembly_asset.h"
#include "voxel/voxel_render_instance.h"
#include "voxel/voxel_spatial_bounds.h"

namespace editor
{
    [[nodiscard]] inline glm::mat4 pivot_transform_matrix(
        const glm::vec3& position,
        const glm::quat& rotation,
        const float scale)
    {
        glm::mat4 result = glm::translate(glm::mat4(1.0f), position);
        result *= glm::mat4_cast(rotation);
        result = glm::scale(result, glm::vec3(scale));
        return result;
    }

    [[nodiscard]] inline glm::mat4 pivot_transform_matrix(const VoxelRenderInstance& instance)
    {
        const glm::vec3 pivotWorldPosition = (instance.asset != nullptr)
            ? instance.world_point_from_asset_local(instance.asset->model.pivot)
            : instance.position;
        return pivot_transform_matrix(pivotWorldPosition, instance.rotation, instance.scale);
    }

    [[nodiscard]] inline glm::mat4 parent_basis_matrix(
        const std::unordered_map<std::string, VoxelRenderInstance>& previewInstances,
        const VoxelAssemblyBindingState& bindingState)
    {
        if (bindingState.parentPartId.empty())
        {
            return glm::mat4(1.0f);
        }

        const auto parentIt = previewInstances.find(bindingState.parentPartId);
        if (parentIt == previewInstances.end())
        {
            return glm::mat4(1.0f);
        }

        const VoxelRenderInstance& parentInstance = parentIt->second;
        if (!bindingState.parentAttachmentName.empty())
        {
            if (const std::optional<glm::mat4> attachmentTransform =
                parentInstance.attachment_world_transform(bindingState.parentAttachmentName);
                attachmentTransform.has_value())
            {
                return attachmentTransform.value();
            }
        }

        return pivot_transform_matrix(parentInstance);
    }

    [[nodiscard]] inline ImGuiViewport* begin_main_viewport_gizmo()
    {
        ImGuiViewport* const viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return nullptr;
        }

        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
        ImGuizmo::SetRect(viewport->WorkPos.x, viewport->WorkPos.y, viewport->WorkSize.x, viewport->WorkSize.y);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::Enable(true);
        return viewport;
    }

    inline void clear_bounds_overlay(
        std::shared_ptr<Mesh>& mesh,
        std::optional<dev_collections::sparse_set<RenderObject>::Handle>& handle,
        SceneRenderState& renderState)
    {
        if (handle.has_value())
        {
            renderState.transparentObjects.remove(handle.value());
            handle.reset();
        }

        if (mesh != nullptr)
        {
            render::enqueue_mesh_release(std::move(mesh));
        }
    }

    inline bool sync_bounds_overlay_for_instance(
        std::shared_ptr<Mesh>& mesh,
        std::optional<dev_collections::sparse_set<RenderObject>::Handle>& handle,
        SceneRenderState& renderState,
        const SceneServices& services,
        std::string_view materialScope,
        const VoxelRenderInstance* const instance,
        const bool visible,
        const glm::vec3& color)
    {
        clear_bounds_overlay(mesh, handle, renderState);

        if (!visible || instance == nullptr || !instance->is_renderable() || instance->asset == nullptr)
        {
            return false;
        }

        const VoxelSpatialBounds localBounds = evaluate_voxel_model_local_bounds(instance->asset->model);
        if (!localBounds.valid)
        {
            return false;
        }

        mesh = Mesh::create_box_outline_mesh(localBounds.min, localBounds.max, color);
        services.meshManager->UploadQueue.enqueue(mesh);
        handle = renderState.transparentObjects.insert(RenderObject{
            .mesh = mesh,
            .material = services.materialManager->get_material(materialScope, "chunkboundary"),
            .transform = instance->model_matrix(),
            .layer = RenderLayer::Transparent,
            .lightingMode = LightingMode::Unlit
        });
        return true;
    }
}
