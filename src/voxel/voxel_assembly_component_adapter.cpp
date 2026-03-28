#include "voxel_assembly_component_adapter.h"

#include <format>
#include <functional>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include <glm/gtc/quaternion.hpp>

#include "voxel_placement.h"
#include "voxel_orientation.h"

namespace
{
    float uniform_scale_from_vec3(const glm::vec3& scale)
    {
        return (scale.x + scale.y + scale.z) / 3.0f;
    }

    const VoxelAssemblyBindingState* resolve_binding_state(
        const VoxelAssemblyAsset& assembly,
        const VoxelAssemblyPartDefinition& part,
        const VoxelAssemblyComponent& component,
        const VoxelAssemblyPose* pose,
        std::string& diagnostic)
    {
        if (pose != nullptr)
        {
            if (const VoxelAssemblyPosePart* const posePart = pose->find_part(part.partId);
                posePart != nullptr && posePart->bindingStateId.has_value() && !posePart->bindingStateId->empty())
            {
                if (const VoxelAssemblyBindingState* const poseState =
                    assembly.find_binding_state(part.partId, posePart->bindingStateId.value());
                    poseState != nullptr)
                {
                    return poseState;
                }

                if (diagnostic.empty())
                {
                    diagnostic = std::format(
                        "Assembly '{}' part '{}' requested missing animation binding state '{}'.",
                        assembly.assetId,
                        part.partId,
                        posePart->bindingStateId.value());
                }
            }
        }

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

VoxelAssemblyLocalBundle build_voxel_assembly_local_bundle(
    const VoxelAssemblyComponent& component,
    VoxelAssemblyAssetManager& assemblyAssetManager,
    VoxelAssetManager& assetManager,
    const VoxelAssemblyPose* pose)
{
    VoxelAssemblyLocalBundle bundle{};
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

        if (const VoxelAssemblyBindingState* const bindingState = resolve_binding_state(*assembly, part, component, pose, bundle.diagnostic);
            bindingState != nullptr)
        {
            localInstance.visible = localInstance.visible && bindingState->visible;
            const bool isRoot = part.partId == assembly->rootPartId || assembly->rootPartId.empty();
            const VoxelAssemblyPosePart* const posePart = pose != nullptr ? pose->find_part(part.partId) : nullptr;
            const glm::vec3 localPosition = posePart != nullptr && posePart->localPosition.has_value()
                ? posePart->localPosition.value()
                : bindingState->localPositionOffset;
            const glm::quat localRotation = posePart != nullptr && posePart->localRotation.has_value()
                ? posePart->localRotation.value()
                : bindingState->localRotationOffset;
            const glm::vec3 localScale = posePart != nullptr && posePart->localScale.has_value()
                ? posePart->localScale.value()
                : bindingState->localScale;
            if (posePart != nullptr && posePart->visible.has_value())
            {
                localInstance.visible = localInstance.visible && posePart->visible.value();
            }
            if (isRoot || bindingState->parentPartId.empty())
            {
                localInstance.scale = uniform_scale_from_vec3(localScale);
                localInstance.rotation = localRotation;
                localInstance.position = localPosition;
            }
            else if (const VoxelAssemblyPartDefinition* const parentPart = assembly->find_part(bindingState->parentPartId);
                parentPart != nullptr)
            {
                VoxelRenderInstance parentInstance{};
                if (resolvePart(*parentPart, parentInstance))
                {
                    localInstance.scale = parentInstance.scale * uniform_scale_from_vec3(localScale);

                    if (const VoxelAttachment* const attachment =
                        parentInstance.asset->model.find_attachment(bindingState->parentAttachmentName);
                        attachment != nullptr)
                    {
                        const glm::quat attachmentRotation = voxel::orientation::basis_quat_from_attachment(*attachment);
                        const glm::quat parentAttachmentRotation = parentInstance.rotation * attachmentRotation;
                        localInstance.rotation = parentAttachmentRotation * localRotation;
                        localInstance.position = parentInstance.world_point_from_asset_local(attachment->position) +
                            (parentAttachmentRotation * (localPosition * parentInstance.scale));
                    }
                    else
                    {
                        localInstance.rotation = parentInstance.rotation * localRotation;
                        localInstance.position = parentInstance.position +
                            (parentInstance.rotation * (localPosition * parentInstance.scale));
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

    std::vector<VoxelAssemblyResolvedPart> localResolvedParts{};
    localResolvedParts.reserve(assembly->parts.size());

    for (const VoxelAssemblyPartDefinition& part : assembly->parts)
    {
        VoxelRenderInstance localInstance{};
        if (!resolvePart(part, localInstance) || !localInstance.is_renderable())
        {
            continue;
        }

        localResolvedParts.push_back(VoxelAssemblyResolvedPart{
            .partId = part.partId,
            .renderInstance = localInstance
        });
    }

    const glm::vec3 placementAnchor = resolve_voxel_assembly_placement_anchor(
        resolvedInstances,
        assembly->rootPartId,
        component.placementPolicy,
        component.placementAttachmentName,
        &bundle.diagnostic) + component.renderAnchorOffset;

    bundle.parts.reserve(localResolvedParts.size());
    for (const VoxelAssemblyResolvedPart& localPart : localResolvedParts)
    {
        VoxelRenderInstance localInstance = localPart.renderInstance;
        localInstance.position = (localPart.renderInstance.position - placementAnchor) * component.scale;
        localInstance.scale = component.scale * localPart.renderInstance.scale;
        localInstance.renderAnchorOffset = glm::vec3(0.0f);
        localInstance.lightingMode = component.lightingMode;
        localInstance.lightSampleOffset = component.lightSampleOffset;
        localInstance.lightAffectMask = component.lightAffectMask;
        localInstance.visible = localPart.renderInstance.visible;
        bundle.parts.push_back(VoxelAssemblyResolvedPart{
            .partId = localPart.partId,
            .renderInstance = localInstance
        });
    }

    return bundle;
}

VoxelAssemblyRenderBundle build_voxel_assembly_render_bundle(
    const VoxelAssemblyComponent& component,
    VoxelAssemblyAssetManager& assemblyAssetManager,
    VoxelAssetManager& assetManager,
    const VoxelAssemblyPose* pose)
{
    const VoxelAssemblyLocalBundle localBundle =
        build_voxel_assembly_local_bundle(component, assemblyAssetManager, assetManager, pose);

    VoxelAssemblyRenderBundle bundle{};
    bundle.assemblyAssetId = localBundle.assemblyAssetId;
    bundle.diagnostic = localBundle.diagnostic;
    bundle.parts.reserve(localBundle.parts.size());
    for (const VoxelAssemblyResolvedPart& localPart : localBundle.parts)
    {
        VoxelRenderInstance worldInstance = localPart.renderInstance;
        worldInstance.position = component.position + (component.rotation * localPart.renderInstance.position);
        worldInstance.rotation = component.rotation * localPart.renderInstance.rotation;
        bundle.parts.push_back(VoxelAssemblyResolvedPart{
            .partId = localPart.partId,
            .renderInstance = worldInstance
        });
    }

    return bundle;
}
