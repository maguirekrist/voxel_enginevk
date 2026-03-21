#pragma once

#include <unordered_map>

#include "collections/spare_set.h"
#include "game/chunk.h"
#include "render_primitives.h"
#include "scene_render_state.h"
#include "world/chunk_manager.h"

class MaterialManager;
class MeshManager;

class ChunkRenderRegistry
{
public:
    void sync(
        ChunkManager& chunkManager,
        MeshManager& meshManager,
        MaterialManager& materialManager,
        SceneRenderState& renderState);

    void clear(SceneRenderState& renderState);

private:
    struct ChunkRenderHandles
    {
        dev_collections::sparse_set<RenderObject>::Handle opaque{};
        dev_collections::sparse_set<RenderObject>::Handle waterTransparent{};
        dev_collections::sparse_set<RenderObject>::Handle glowTransparent{};
        bool hasOpaque{false};
        bool hasWaterTransparent{false};
        bool hasGlowTransparent{false};
    };

    struct PendingChunkRender
    {
        Chunk* chunk{};
        uint32_t generationId{};
        uint64_t neighborhoodSignature{};
        std::shared_ptr<ChunkData> data{};
        std::shared_ptr<ChunkMeshData> meshData{};
        bool hasWaterTransparentMesh{false};
        bool hasGlowTransparentMesh{false};
        bool uploadRequested{false};
    };

    std::unordered_map<Chunk*, ChunkRenderHandles> _handlesByChunk;
    std::unordered_map<Chunk*, PendingChunkRender> _pendingByChunk;

    void remove_chunk(Chunk* chunk, SceneRenderState& renderState);
    void finalize_pending_renders(
        ChunkManager& chunkManager,
        MeshManager& meshManager,
        MaterialManager& materialManager,
        SceneRenderState& renderState);
};
