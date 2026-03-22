#pragma once

#include <unordered_map>
#include <vector>

#include "game/chunk.h"
#include "voxel/voxel_asset_manager.h"
#include "voxel/voxel_render_registry.h"

class ChunkManager;
class MaterialManager;
class MeshManager;

class ChunkDecorationRenderRegistry
{
public:
    struct ChunkDecorationEntryState
    {
        Chunk* chunk{nullptr};
        const ChunkData* data{nullptr};
        uint32_t generationId{0};
    };

    void sync(
        const ChunkManager& chunkManager,
        const ChunkCoord& centerChunk,
        int viewDistance,
        VoxelAssetManager& assetManager,
        MeshManager& meshManager,
        MaterialManager& materialManager,
        SceneRenderState& renderState);

    void clear(SceneRenderState& renderState);
    [[nodiscard]] size_t active_chunk_count() const noexcept;
    [[nodiscard]] size_t active_instance_count() const noexcept;

    [[nodiscard]] static bool requires_rebuild(
        const ChunkDecorationEntryState* entry,
        const Chunk& chunk) noexcept
    {
        if (chunk._data == nullptr)
        {
            return false;
        }

        if (chunk._state.load(std::memory_order::acquire) == ChunkState::Uninitialized)
        {
            return false;
        }

        const uint32_t generationId = chunk._gen.load(std::memory_order::acquire);
        return entry == nullptr ||
            entry->chunk != &chunk ||
            entry->data != chunk._data.get() ||
            entry->generationId != generationId;
    }

private:
    struct ChunkDecorationEntry
    {
        Chunk* chunk{nullptr};
        const ChunkData* data{nullptr};
        uint32_t generationId{0};
        std::vector<VoxelRenderRegistry::InstanceId> instanceIds{};
    };

    void rebuild_chunk(
        const ChunkCoord& coord,
        Chunk& chunk,
        VoxelAssetManager& assetManager,
        SceneRenderState& renderState);
    void remove_chunk(const ChunkCoord& coord, SceneRenderState& renderState);

    std::unordered_map<ChunkCoord, ChunkDecorationEntry> _entriesByChunk{};
    VoxelRenderRegistry _voxelRenderRegistry{};
};
