#include "voxel_assembly_component_adapter.h"

#include <format>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include <glm/gtc/quaternion.hpp>

namespace
{
    glm::quat basis_from_attachment(const VoxelAttachment& attachment)
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

        glm::mat3 basis{1.0f};
        basis[0] = forward;
        basis[1] = up;
        basis[2] = right;
        return glm::normalize(glm::quat_cast(basis));
    }

    float uniform_scale_from_vec3(const glm::vec3& scale)
    {
        return (scale.x + scale.y + scale.z) / 3.0f;
    }

    const VoxelAssemblyBindingState* resolve_binding_state(
        const VoxelAssemblyAsset& assembly,
        const VoxelAssemblyPartDefinition& part,
        const VoxelAssemblyComponent& component,
        std::string& diagnostic)
    {
        if (const auto overrideIt = component.partBindingStateOverrides.find(part.partId);
            overrideIt != component.partBindingStateOverrides.end() && !overrideIt->second.empty())
        {
            if (const VoxelAssemblyBindingState* const overriddenState =
                assembly.find_binding_state(part.partId, overrideIt->second);
                overriddenState != nullptr)
            {
                return overriddenState;
            }

            if (diagnostic.empty())
            {
                diagnostic = std::format(
                    "Assembly '{}' part '{}' requested missing binding state '{}'.",
                    assembly.assetId,
                    part.partId,
                    overrideIt->second);
            }
        }

        return assembly.default_binding_state(part.partId);
    }

    std::string resolve_model_asset_id(
        const VoxelAssemblyPartDefinition& part,
        const VoxelAssemblyComponent& component)
    {
        if (!part.slotId.empty())
        {
            if (const auto overrideIt = component.slotModelAssetOverrides.find(part.slotId);
                overrideIt != component.slotModelAssetOverrides.end() && !overrideIt->second.empty())
            {
                return overrideIt->second;
            }
        }

        return part.defaultModelAssetId;
    }
}

VoxelAssemblyRenderBundle build_voxel_assembly_render_bundle(
    const VoxelAssemblyComponent& component,
    VoxelAssemblyAssetManager& assemblyAssetManager,
    VoxelAssetManager& assetManager)
{
    VoxelAssemblyRenderBundle bundle{};
    if (!component.visible || component.assetId.empty())
    {
        return bundle;
    }

    const std::shared_ptr<const VoxelAssemblyAsset> assembly = assemblyAssetManager.load_or_get(component.assetId);
    if (assembly == nullptr)
    {
        bundle.diagnostic = std::format("Missing voxel assembly '{}'.", component.assetId);
        return bundle;
    }

    bundle.assemblyAssetId = assembly->assetId;

    std::unordered_map<std::string, VoxelRenderInstance> resolvedInstances{};
    std::unordered_set<std::string> visiting{};

    std::function<bool(const VoxelAssemblyPartDefinition&, VoxelRenderInstance&)> resolvePart =
        [&](const VoxelAssemblyPartDefinition& part, VoxelRenderInstance& outInstance) -> bool
    {
        if (const auto cachedIt = resolvedInstances.find(part.partId); cachedIt != resolvedInstances.end())
        {
            outInstance = cachedIt->second;
            return true;
        }

        if (!visiting.insert(part.partId).second)
        {
            if (bundle.diagnostic.empty())
            {
                bundle.diagnostic = std::format(
                    "Assembly '{}' contains a cycle at part '{}'.",
                    assembly->assetId,
                    part.partId);
            }
            return false;
        }

        VoxelRenderInstance localInstance{};
        localInstance.layer = RenderLayer::Opaque;
        localInstance.lightingMode = component.lightingMode;
        localInstance.lightSampleOffset = component.lightSampleOffset;
        localInstance.lightAffectMask = component.lightAffectMask;
        localInstance.visible = part.visibleByDefault;

        const std::string modelAssetId = resolve_model_asset_id(part, component);
        if (!modelAssetId.empty())
        {
            localInstance.asset = assetManager.load_or_get(modelAssetId);
        }

        if (localInstance.asset == nullptr)
        {
            if (bundle.diagnostic.empty())
            {
                bundle.diagnostic = std::format(
                    "Assembly '{}' part '{}' is missing voxel model '{}'.",
                    assembly->assetId,
                    part.partId,
                    modelAssetId);
            }
            visiting.erase(part.partId);
            return false;
        }

        if (const VoxelAssemblyBindingState* const bindingState = resolve_binding_state(*assembly, part, component, bundle.diagnostic);
            bindingState != nullptr)
        {
            localInstance.visible = localInstance.visible && bindingState->visible;
            const bool isRoot = part.partId == assembly->rootPartId || assembly->rootPartId.empty();
            if (isRoot || bindingState->parentPartId.empty())
            {
                localInstance.scale = uniform_scale_from_vec3(bindingState->localScale);
                localInstance.rotation = bindingState->localRotationOffset;
                localInstance.position = bindingState->localPositionOffset;
            }
            else if (const VoxelAssemblyPartDefinition* const parentPart = assembly->find_part(bindingState->parentPartId);
                parentPart != nullptr)
            {
                VoxelRenderInstance parentInstance{};
                if (resolvePart(*parentPart, parentInstance))
                {
                    localInstance.scale = parentInstance.scale * uniform_scale_from_vec3(bindingState->localScale);

                    if (const VoxelAttachment* const attachment =
                        parentInstance.asset->model.find_attachment(bindingState->parentAttachmentName);
                        attachment != nullptr)
                    {
                        const glm::quat attachmentRotation = basis_from_attachment(*attachment);
                        const glm::quat parentAttachmentRotation = parentInstance.rotation * attachmentRotation;
                        localInstance.rotation = parentAttachmentRotation * bindingState->localRotationOffset;
                        localInstance.position = parentInstance.world_point_from_asset_local(attachment->position) +
                            (parentAttachmentRotation * (bindingState->localPositionOffset * parentInstance.scale));
                    }
                    else
                    {
                        localInstance.rotation = parentInstance.rotation * bindingState->localRotationOffset;
                        localInstance.position = parentInstance.position +
                            (parentInstance.rotation * (bindingState->localPositionOffset * parentInstance.scale));
                    }
                }
            }
            else if (bundle.diagnostic.empty())
            {
                bundle.diagnostic = std::format(
                    "Assembly '{}' part '{}' references missing parent part '{}'.",
                    assembly->assetId,
                    part.partId,
                    bindingState->parentPartId);
            }
        }

        visiting.erase(part.partId);
        resolvedInstances.insert_or_assign(part.partId, localInstance);
        outInstance = localInstance;
        return true;
    };

    for (const VoxelAssemblyPartDefinition& part : assembly->parts)
    {
        VoxelRenderInstance localInstance{};
        if (!resolvePart(part, localInstance) || !localInstance.is_renderable())
        {
            continue;
        }

        VoxelRenderInstance worldInstance = localInstance;
        worldInstance.position = component.position +
            (component.rotation * ((localInstance.position - component.renderAnchorOffset) * component.scale));
        worldInstance.rotation = component.rotation * localInstance.rotation;
        worldInstance.scale = component.scale * localInstance.scale;
        worldInstance.renderAnchorOffset = glm::vec3(0.0f);
        worldInstance.lightingMode = component.lightingMode;
        worldInstance.lightSampleOffset = component.lightSampleOffset;
        worldInstance.lightAffectMask = component.lightAffectMask;
        worldInstance.visible = localInstance.visible;
        bundle.parts.push_back(VoxelAssemblyResolvedPart{
            .partId = part.partId,
            .renderInstance = worldInstance
        });
    }

    return bundle;
}
