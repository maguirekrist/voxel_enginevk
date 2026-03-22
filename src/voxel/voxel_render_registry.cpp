#include "voxel_render_registry.h"

#include <unordered_set>

#include "render/material_manager.h"
#include "render/mesh_manager.h"
#include "world/world_light_sampler.h"

VoxelRenderRegistry::InstanceId VoxelRenderRegistry::add_instance(const VoxelRenderInstance& instance)
{
    const InstanceId id = _nextId++;
    _entries.insert_or_assign(id, Entry{
        .instance = instance,
        .uploadRequested = false
    });
    return id;
}

bool VoxelRenderRegistry::update_instance(const InstanceId id, const VoxelRenderInstance& instance)
{
    const auto it = _entries.find(id);
    if (it == _entries.end())
    {
        return false;
    }

    const std::shared_ptr<Mesh> previousMesh = it->second.instance.asset != nullptr ? it->second.instance.asset->mesh : nullptr;
    const std::shared_ptr<Mesh> nextMesh = instance.asset != nullptr ? instance.asset->mesh : nullptr;
    if (previousMesh != nextMesh)
    {
        it->second.uploadRequested = false;
    }

    it->second.instance = instance;
    return true;
}

VoxelRenderInstance* VoxelRenderRegistry::get_instance(const InstanceId id)
{
    const auto it = _entries.find(id);
    return it != _entries.end() ? &it->second.instance : nullptr;
}

const VoxelRenderInstance* VoxelRenderRegistry::get_instance(const InstanceId id) const
{
    const auto it = _entries.find(id);
    return it != _entries.end() ? &it->second.instance : nullptr;
}

bool VoxelRenderRegistry::remove_instance(const InstanceId id, SceneRenderState& renderState)
{
    const auto it = _entries.find(id);
    if (it == _entries.end())
    {
        return false;
    }

    remove_render_object(it->second, renderState);
    _entries.erase(it);
    return true;
}

void VoxelRenderRegistry::clear(SceneRenderState& renderState)
{
    for (auto& [id, entry] : _entries)
    {
        (void)id;
        remove_render_object(entry, renderState);
    }

    _entries.clear();
}

void VoxelRenderRegistry::sync(
    MeshManager& meshManager,
    MaterialManager& materialManager,
    SceneRenderState& renderState,
    const world_lighting::WorldLightSampler* const worldLightSampler)
{
    const auto defaultMaterial = materialManager.get_material("defaultmesh");
    std::unordered_set<Mesh*> queuedMeshes{};
    queuedMeshes.reserve(_entries.size());

    for (auto& [id, entry] : _entries)
    {
        (void)id;
        if (!entry.instance.is_renderable())
        {
            remove_render_object(entry, renderState);
            continue;
        }

        if (worldLightSampler != nullptr && entry.instance.lightingMode == LightingMode::SampledRuntime)
        {
            const world_lighting::SampledWorldLight sampledLight = worldLightSampler->sample(
                entry.instance.light_sample_world_position(),
                entry.instance.lightAffectMask);
            entry.instance.sampledLight = SampledLightPayload{
                .localLight = sampledLight.bakedLocalLight,
                .sunlight = sampledLight.bakedSunlight,
                .dynamicLight = sampledLight.dynamicLight
            };
        }

        Mesh* const meshPtr = entry.instance.asset->mesh.get();
        if (!entry.uploadRequested && meshManager.accepts_uploads() && meshPtr != nullptr && !queuedMeshes.contains(meshPtr))
        {
            meshManager.UploadQueue.enqueue(entry.instance.asset->mesh);
            queuedMeshes.insert(meshPtr);
            entry.uploadRequested = true;
        }

        if (entry.renderHandle.has_value() && entry.submittedLayer.has_value() && entry.submittedLayer.value() != entry.instance.layer)
        {
            remove_render_object(entry, renderState);
        }

        if (!entry.renderHandle.has_value())
        {
            entry.renderHandle = render_bucket(renderState, entry.instance.layer).insert(RenderObject{
                .mesh = entry.instance.asset->mesh,
                .material = defaultMaterial,
                .transform = entry.instance.model_matrix(),
                .layer = entry.instance.layer,
                .lightingMode = entry.instance.lightingMode,
                .sampledLight = entry.instance.sampledLight
            });
            entry.submittedLayer = entry.instance.layer;
            continue;
        }

        RenderObject* const renderObject = render_bucket(renderState, entry.instance.layer).get(entry.renderHandle.value());
        if (renderObject == nullptr)
        {
            entry.renderHandle = std::nullopt;
            entry.submittedLayer = std::nullopt;
            continue;
        }

        renderObject->mesh = entry.instance.asset->mesh;
        renderObject->material = defaultMaterial;
        renderObject->transform = entry.instance.model_matrix();
        renderObject->layer = entry.instance.layer;
        renderObject->lightingMode = entry.instance.lightingMode;
        renderObject->sampledLight = entry.instance.sampledLight;
    }
}

size_t VoxelRenderRegistry::instance_count() const noexcept
{
    return _entries.size();
}

void VoxelRenderRegistry::remove_render_object(Entry& entry, SceneRenderState& renderState)
{
    if (!entry.renderHandle.has_value() || !entry.submittedLayer.has_value())
    {
        entry.renderHandle = std::nullopt;
        entry.submittedLayer = std::nullopt;
        return;
    }

    render_bucket(renderState, entry.submittedLayer.value()).remove(entry.renderHandle.value());
    entry.renderHandle = std::nullopt;
    entry.submittedLayer = std::nullopt;
}

dev_collections::sparse_set<RenderObject>& VoxelRenderRegistry::render_bucket(SceneRenderState& renderState, const RenderLayer layer) const
{
    return layer == RenderLayer::Transparent ? renderState.transparentObjects : renderState.opaqueObjects;
}

const dev_collections::sparse_set<RenderObject>& VoxelRenderRegistry::render_bucket(const SceneRenderState& renderState, const RenderLayer layer) const
{
    return layer == RenderLayer::Transparent ? renderState.transparentObjects : renderState.opaqueObjects;
}
