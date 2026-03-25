#include "voxel_component_render_adapter.h"

#include "components/voxel_assembly_component.h"
#include "components/voxel_model_component.h"
#include "voxel_assembly_component_adapter.h"
#include "voxel_model_component_adapter.h"

VoxelComponentRenderBundle build_voxel_component_render_bundle(
    const GameObject& object,
    VoxelAssemblyAssetManager& assemblyAssetManager,
    VoxelAssetManager& assetManager)
{
    VoxelComponentRenderBundle bundle{};

    if (object.Has<VoxelAssemblyComponent>())
    {
        const VoxelAssemblyComponent& component = object.Get<VoxelAssemblyComponent>();
        if (!component.assetId.empty())
        {
            const VoxelAssemblyRenderBundle assemblyBundle =
                build_voxel_assembly_render_bundle(component, assemblyAssetManager, assetManager);
            bundle.assetId = assemblyBundle.assemblyAssetId;
            bundle.diagnostic = assemblyBundle.diagnostic;
            bundle.entries.reserve(assemblyBundle.parts.size());
            for (const VoxelAssemblyResolvedPart& part : assemblyBundle.parts)
            {
                bundle.entries.push_back(VoxelComponentRenderEntry{
                    .stableId = part.partId,
                    .renderInstance = part.renderInstance
                });
            }
            return bundle;
        }
    }

    if (object.Has<VoxelModelComponent>())
    {
        const VoxelModelComponent& component = object.Get<VoxelModelComponent>();
        bundle.assetId = component.assetId;
        if (const std::optional<VoxelRenderInstance> renderInstance =
            build_voxel_render_instance(component, assetManager);
            renderInstance.has_value())
        {
            bundle.entries.push_back(VoxelComponentRenderEntry{
                .stableId = "root",
                .renderInstance = renderInstance.value()
            });
        }
    }

    return bundle;
}
