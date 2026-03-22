#include "chunk_decoration_render_registry.h"

#include <unordered_set>

#include "components/voxel_model_component.h"
#include "game/chunk.h"
#include "voxel/voxel_model_component_adapter.h"
#include "world/chunk_manager.h"

void ChunkDecorationRenderRegistry::sync(
    const ChunkManager& chunkManager,
    const ChunkCoord& centerChunk,
    const int viewDistance,
    VoxelAssetManager& assetManager,
    MeshManager& meshManager,
    MaterialManager& materialManager,
    SceneRenderState& renderState)
{
    std::unordered_set<ChunkCoord> visibleChunks{};
    visibleChunks.reserve(static_cast<size_t>((viewDistance * 2 + 1) * (viewDistance * 2 + 1)));

    for (int chunkZ = centerChunk.z - viewDistance; chunkZ <= centerChunk.z + viewDistance; ++chunkZ)
    {
        for (int chunkX = centerChunk.x - viewDistance; chunkX <= centerChunk.x + viewDistance; ++chunkX)
        {
            const ChunkCoord coord{ chunkX, chunkZ };
            visibleChunks.insert(coord);

            Chunk* const chunk = chunkManager.get_chunk(coord);
            if (chunk == nullptr || chunk->_data == nullptr)
            {
                remove_chunk(coord, renderState);
                continue;
            }

            if (chunk->_state.load(std::memory_order::acquire) == ChunkState::Uninitialized)
            {
                remove_chunk(coord, renderState);
                continue;
            }

            const auto it = _entriesByChunk.find(coord);
            const std::optional<ChunkDecorationEntryState> entryState = it != _entriesByChunk.end()
                ? std::optional<ChunkDecorationEntryState>(ChunkDecorationEntryState{
                    .chunk = it->second.chunk,
                    .data = it->second.data,
                    .generationId = it->second.generationId
                })
                : std::nullopt;

            if (requires_rebuild(entryState.has_value() ? &entryState.value() : nullptr, *chunk))
            {
                rebuild_chunk(coord, *chunk, assetManager, renderState);
            }
        }
    }

    std::vector<ChunkCoord> staleChunks{};
    staleChunks.reserve(_entriesByChunk.size());
    for (const auto& [coord, entry] : _entriesByChunk)
    {
        (void)entry;
        if (!visibleChunks.contains(coord))
        {
            staleChunks.push_back(coord);
        }
    }

    for (const ChunkCoord& coord : staleChunks)
    {
        remove_chunk(coord, renderState);
    }

    _voxelRenderRegistry.sync(meshManager, materialManager, renderState);
}

void ChunkDecorationRenderRegistry::clear(SceneRenderState& renderState)
{
    for (const auto& [coord, entry] : _entriesByChunk)
    {
        (void)coord;
        for (const auto instanceId : entry.instanceIds)
        {
            _voxelRenderRegistry.remove_instance(instanceId, renderState);
        }
    }

    _entriesByChunk.clear();
    _voxelRenderRegistry.clear(renderState);
}

size_t ChunkDecorationRenderRegistry::active_chunk_count() const noexcept
{
    return _entriesByChunk.size();
}

size_t ChunkDecorationRenderRegistry::active_instance_count() const noexcept
{
    return _voxelRenderRegistry.instance_count();
}

void ChunkDecorationRenderRegistry::rebuild_chunk(
    const ChunkCoord& coord,
    Chunk& chunk,
    VoxelAssetManager& assetManager,
    SceneRenderState& renderState)
{
    remove_chunk(coord, renderState);

    ChunkDecorationEntry entry{};
    entry.chunk = &chunk;
    entry.data = chunk._data.get();
    entry.generationId = chunk._gen.load(std::memory_order::acquire);
    entry.instanceIds.reserve(chunk._data->voxelDecorations.size());

    for (const VoxelDecorationPlacement& placement : chunk._data->voxelDecorations)
    {
        VoxelModelComponent component{};
        component.assetId = placement.assetId;
        component.position = placement.worldPosition;
        component.rotation = placement.rotation;
        component.scale = placement.scale;
        component.visible = true;

        const std::optional<VoxelRenderInstance> renderInstance = build_voxel_render_instance(component, assetManager);
        if (!renderInstance.has_value())
        {
            continue;
        }

        entry.instanceIds.push_back(_voxelRenderRegistry.add_instance(renderInstance.value()));
    }

    _entriesByChunk.insert_or_assign(coord, std::move(entry));
}

void ChunkDecorationRenderRegistry::remove_chunk(const ChunkCoord& coord, SceneRenderState& renderState)
{
    const auto it = _entriesByChunk.find(coord);
    if (it == _entriesByChunk.end())
    {
        return;
    }

    for (const auto instanceId : it->second.instanceIds)
    {
        _voxelRenderRegistry.remove_instance(instanceId, renderState);
    }

    _entriesByChunk.erase(it);
}
