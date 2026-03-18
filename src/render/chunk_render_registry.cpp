#include "chunk_render_registry.h"

#include "material_manager.h"
#include "mesh_manager.h"

void ChunkRenderRegistry::sync(
    ChunkManager& chunkManager,
    MeshManager& meshManager,
    MaterialManager& materialManager,
    dev_collections::sparse_set<RenderObject>& opaqueSet,
    dev_collections::sparse_set<RenderObject>& transparentSet)
{
    ChunkManager::ChunkRenderResetEvent resetEvent;
    while (chunkManager.try_dequeue_render_reset(resetEvent))
    {
        remove_chunk(resetEvent.chunk, opaqueSet, transparentSet);
    }

    ChunkManager::ChunkRenderReadyEvent readyEvent;
    while (chunkManager.try_dequeue_render_ready(readyEvent))
    {
        if (readyEvent.chunk == nullptr)
        {
            continue;
        }

        if (readyEvent.chunk->_gen.load(std::memory_order::acquire) != readyEvent.generation)
        {
            continue;
        }

        meshManager.UploadQueue.enqueue(readyEvent.meshData->mesh);
        if (!readyEvent.meshData->waterMesh->_vertices.empty())
        {
            meshManager.UploadQueue.enqueue(readyEvent.meshData->waterMesh);
        }

        remove_chunk(readyEvent.chunk, opaqueSet, transparentSet);

        ChunkRenderHandles handles{};
        handles.opaque = opaqueSet.insert(RenderObject{
            .mesh = readyEvent.meshData->mesh,
            .material = materialManager.get_material("defaultmesh"),
            .xzPos = glm::ivec2(readyEvent.data->position.x, readyEvent.data->position.y),
            .layer = RenderLayer::Opaque
        });
        handles.hasOpaque = true;

        handles.transparent = transparentSet.insert(RenderObject{
            .mesh = readyEvent.meshData->waterMesh,
            .material = materialManager.get_material("watermesh"),
            .xzPos = glm::ivec2(readyEvent.data->position.x, readyEvent.data->position.y),
            .layer = RenderLayer::Transparent
        });
        handles.hasTransparent = true;

        _handlesByChunk[readyEvent.chunk] = handles;
    }
}

void ChunkRenderRegistry::clear(
    dev_collections::sparse_set<RenderObject>& opaqueSet,
    dev_collections::sparse_set<RenderObject>& transparentSet)
{
    std::vector<Chunk*> chunks;
    chunks.reserve(_handlesByChunk.size());
    for (const auto& [chunk, _] : _handlesByChunk)
    {
        chunks.push_back(chunk);
    }

    for (Chunk* chunk : chunks)
    {
        remove_chunk(chunk, opaqueSet, transparentSet);
    }

    _handlesByChunk.clear();
}

void ChunkRenderRegistry::remove_chunk(
    Chunk* chunk,
    dev_collections::sparse_set<RenderObject>& opaqueSet,
    dev_collections::sparse_set<RenderObject>& transparentSet)
{
    if (chunk == nullptr)
    {
        return;
    }

    const auto it = _handlesByChunk.find(chunk);
    if (it == _handlesByChunk.end())
    {
        return;
    }

    if (it->second.hasOpaque)
    {
        opaqueSet.remove(it->second.opaque);
    }

    if (it->second.hasTransparent)
    {
        transparentSet.remove(it->second.transparent);
    }

    _handlesByChunk.erase(it);
}
