#include "chunk_render_registry.h"

#include <vector>

#include "material_manager.h"
#include "mesh_manager.h"

void ChunkRenderRegistry::sync(
    ChunkManager& chunkManager,
    MeshManager& meshManager,
    MaterialManager& materialManager,
    SceneRenderState& renderState)
{
    ChunkManager::ChunkRenderResetEvent resetEvent;
    while (chunkManager.try_dequeue_render_reset(resetEvent))
    {
        _pendingByChunk.erase(resetEvent.chunk);
        remove_chunk(resetEvent.chunk, renderState);
    }

    ChunkManager::ChunkRenderReadyEvent readyEvent;
    while (chunkManager.try_dequeue_render_ready(readyEvent))
    {
        if (readyEvent.chunk == nullptr)
        {
            continue;
        }

        if (readyEvent.chunk->_gen.load(std::memory_order::acquire) != readyEvent.generationId)
        {
            continue;
        }

        meshManager.UploadQueue.enqueue(readyEvent.meshData->mesh);
        if (!readyEvent.meshData->waterMesh->_vertices.empty())
        {
            meshManager.UploadQueue.enqueue(readyEvent.meshData->waterMesh);
        }
        if (!readyEvent.meshData->glowMesh->_vertices.empty())
        {
            meshManager.UploadQueue.enqueue(readyEvent.meshData->glowMesh);
        }

        _pendingByChunk[readyEvent.chunk] = PendingChunkRender{
            .chunk = readyEvent.chunk,
            .generationId = readyEvent.generationId,
            .neighborhoodSignature = readyEvent.neighborhoodSignature,
            .data = readyEvent.data,
            .meshData = readyEvent.meshData,
            .hasWaterTransparentMesh = readyEvent.meshData != nullptr &&
                readyEvent.meshData->waterMesh != nullptr &&
                !readyEvent.meshData->waterMesh->_vertices.empty(),
            .hasGlowTransparentMesh = readyEvent.meshData != nullptr &&
                readyEvent.meshData->glowMesh != nullptr &&
                !readyEvent.meshData->glowMesh->_vertices.empty()
        };
    }

    finalize_pending_renders(chunkManager, materialManager, renderState);
}

void ChunkRenderRegistry::finalize_pending_renders(
    ChunkManager& chunkManager,
    MaterialManager& materialManager,
    SceneRenderState& renderState)
{
    std::vector<Chunk*> completedChunks{};
    completedChunks.reserve(_pendingByChunk.size());

    for (const auto& [chunk, pending] : _pendingByChunk)
    {
        if (chunk == nullptr)
        {
            completedChunks.push_back(chunk);
            continue;
        }

        if (chunk->_gen.load(std::memory_order::acquire) != pending.generationId)
        {
            completedChunks.push_back(chunk);
            continue;
        }

        const bool opaqueReady = pending.meshData != nullptr &&
            pending.meshData->mesh != nullptr &&
            pending.meshData->mesh->_isActive.load(std::memory_order::acquire);
        const bool waterExpected = pending.hasWaterTransparentMesh;
        const bool glowExpected = pending.hasGlowTransparentMesh;
        const bool transparentReady = !waterExpected ||
            pending.meshData->waterMesh->_isActive.load(std::memory_order::acquire);
        const bool glowReady = !glowExpected ||
            pending.meshData->glowMesh->_isActive.load(std::memory_order::acquire);

        if (!opaqueReady || !transparentReady || !glowReady)
        {
            continue;
        }

        remove_chunk(chunk, renderState);

        ChunkRenderHandles handles{};
        handles.opaque = renderState.opaqueObjects.insert(RenderObject{
            .mesh = pending.meshData->mesh,
            .material = materialManager.get_material("defaultmesh"),
            .xzPos = glm::ivec2(pending.data->position.x, pending.data->position.y),
            .layer = RenderLayer::Opaque
        });
        handles.hasOpaque = true;

        if (waterExpected)
        {
            handles.waterTransparent = renderState.transparentObjects.insert(RenderObject{
                .mesh = pending.meshData->waterMesh,
                .material = materialManager.get_material("watermesh"),
                .xzPos = glm::ivec2(pending.data->position.x, pending.data->position.y),
                .layer = RenderLayer::Transparent
            });
            handles.hasWaterTransparent = true;
        }

        if (glowExpected)
        {
            handles.glowTransparent = renderState.transparentObjects.insert(RenderObject{
                .mesh = pending.meshData->glowMesh,
                .material = materialManager.get_material("glowmesh"),
                .xzPos = glm::ivec2(pending.data->position.x, pending.data->position.y),
                .layer = RenderLayer::Transparent
            });
            handles.hasGlowTransparent = true;
        }

        _handlesByChunk[chunk] = handles;
        chunkManager.notify_chunk_uploaded(chunk, pending.generationId, pending.neighborhoodSignature);
        completedChunks.push_back(chunk);
    }

    for (Chunk* chunk : completedChunks)
    {
        _pendingByChunk.erase(chunk);
    }
}

void ChunkRenderRegistry::clear(SceneRenderState& renderState)
{
    _pendingByChunk.clear();

    std::vector<Chunk*> chunks;
    chunks.reserve(_handlesByChunk.size());
    for (const auto& [chunk, _] : _handlesByChunk)
    {
        chunks.push_back(chunk);
    }

    for (Chunk* chunk : chunks)
    {
        remove_chunk(chunk, renderState);
    }

    _handlesByChunk.clear();
}

void ChunkRenderRegistry::remove_chunk(Chunk* chunk, SceneRenderState& renderState)
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
        renderState.opaqueObjects.remove(it->second.opaque);
    }

    if (it->second.hasWaterTransparent)
    {
        renderState.transparentObjects.remove(it->second.waterTransparent);
    }

    if (it->second.hasGlowTransparent)
    {
        renderState.transparentObjects.remove(it->second.glowTransparent);
    }

    _handlesByChunk.erase(it);
}
