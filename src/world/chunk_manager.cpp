
#include "chunk_manager.h"
#include "../game/chunk.h"
#include "chunk_mesher.h"
#include "tracy/Tracy.hpp"
#include <memory>

#include "../vk_engine.h"

ChunkManager::ChunkManager() : m_chunkCache(nullptr)
{
}

ChunkManager::~ChunkManager() = default;

void ChunkManager::update()
{
    ChunkReadyPayload payload{};
    while (_readyChunks.try_dequeue(payload))
    {
        payload.chunk->_opaqueHandle = VulkanEngine::instance()._opaqueSet.insert(RenderObject{
            .mesh = payload.opaqueMesh,
            .material = VulkanEngine::instance()._materialManager.get_material("defaultmesh"),
            .xzPos = glm::ivec2(payload.chunk->_data->position.x, payload.chunk->_data->position.y),
            .layer = RenderLayer::Opaque
        });
        payload.chunk->_transparentHandle = VulkanEngine::instance()._transparentSet.insert(RenderObject{
            .mesh = payload.transparentMesh,
            .material = VulkanEngine::instance()._materialManager.get_material("watermesh"),
            .xzPos = glm::ivec2(payload.chunk->_data->position.x, payload.chunk->_data->position.y),
            .layer = RenderLayer::Transparent
        });
    }
}

void ChunkManager::update_player_position(const int x, const int z)
{
    const ChunkCoord playerChunk = {x / static_cast<int>(CHUNK_SIZE), z / static_cast<int>(CHUNK_SIZE)};

    if (playerChunk == _lastPlayerChunk && !_initialLoad) return;

    const auto changeX = playerChunk.x - _lastPlayerChunk.x;
    const auto changeZ = playerChunk.z - _lastPlayerChunk.z;
    std::println("Player changed chunks, delta: x {},  z {}", changeX, changeZ);
    std::println("Player position: x {},  z {}", playerChunk.x, playerChunk.z);
    _lastPlayerChunk = playerChunk;
    const MapRange mapRange(playerChunk, _viewDistance);
    _mapRange = mapRange;

    if (_initialLoad)
    {
        _initialLoad = false;
        return initialize_map(mapRange);
    }
    //calculate old chunks and remove.
    auto new_chunks = m_chunkCache->slide({ changeX, changeZ });

    for (const auto& chunk : new_chunks)
    {
        schedule_generate(chunk, chunk->_gen.load(std::memory_order::acquire));
    }

    // std::println("Work Queue: {}", _chunkWorkQueue.size_approx());
    std::println("Active chunks: {}", m_chunkCache->m_chunks.size());
}

Chunk* ChunkManager::get_chunk(const ChunkCoord coord) const
{
    return m_chunkCache->get_chunk(coord);
}

void ChunkManager::initialize_map(const MapRange mapRange)
{
    m_chunkCache = std::make_unique<ChunkCache>(GameConfig::DEFAULT_VIEW_DISTANCE, _neighborBarrier);

    for (auto& chunk : m_chunkCache->m_chunks)
    {
        schedule_generate(chunk.get(), chunk->_gen.load(std::memory_order::acquire));
    }
}

void ChunkManager::schedule_generate(Chunk* const chunk, const uint32_t gen)
{
    //the expected value needs to be calculated.
    const ChunkCoord cc = chunk->_data->coord;
    std::array<ChunkCoord, 8> required = neighbors_of(cc);
    _neighborBarrier.init(cc, required.begin(), required.end());

    _threadPool.post([cc, gen, chunk, this]() noexcept
    {
        if (chunk->_gen.load(std::memory_order::acquire) != gen) return;
        chunk->_data->generate();
        chunk->_state.store(ChunkState::Generated, std::memory_order::release);

        _neighborBarrier.mark_present(cc);

        //signals neighbors.
        for (const ChunkCoord c : neighbors_of(cc))
        {
            if (_neighborBarrier.signal(c))
            {
                if (auto* neighbor = m_chunkCache->get_chunk(c))
                {
                    schedule_mesh(neighbor, neighbor->_gen);
                }
            }
        }

        //signals itself.
        if (_neighborBarrier.try_consume_ready(cc))
        {
            schedule_mesh(chunk, gen);
        }
    });
}

void ChunkManager::schedule_mesh(Chunk* const chunk, const uint32_t gen)
{
    _threadPool.post([=, this]() noexcept {
        if (chunk->_gen.load(std::memory_order_acquire) != gen) return;

        auto neighbors = get_chunk_neighbors(chunk->_data->coord);
        if (neighbors.has_value() == false)
        {
            std::println("Scheduled mesh with no neighbors: {}", chunk->_data->coord);
            //throw std::runtime_error("No neighbors");
            return;
        }

        ChunkMesher mesher{chunk->_data, neighbors};
        auto meshData = mesher.generate_mesh();

        if (chunk->_gen.load(std::memory_order_acquire) != gen) return;

        if (meshData->opaqueMesh._vertices.empty()) {
            // log instead of throw from worker
            std::println("Chunk mesh empty at {}", chunk->_data->coord);
            return;
        }

        //chunk->_meshData = std::move(meshData);
        chunk->_state.store(ChunkState::Rendered, std::memory_order_release);

        // Uploads can be posted to a dedicated thread if needed
        // VulkanEngine::instance()._meshManager.UploadQueue.enqueue(chunk->_meshData->mesh);
        auto opaqueRef = VulkanEngine::instance()._meshManager.enqueue_upload(std::move(meshData->opaqueMesh));
        std::shared_ptr<MeshRef> transparentRef = nullptr;
        if (!meshData->transparentMesh._vertices.empty())
            transparentRef = VulkanEngine::instance()._meshManager.enqueue_upload(std::move(meshData->transparentMesh));

        _readyChunks.enqueue(ChunkReadyPayload{ .chunk = chunk, .opaqueMesh = opaqueRef, .transparentMesh = transparentRef});
    });
}

std::optional<std::array<std::shared_ptr<const ChunkData>, 8>> ChunkManager::get_chunk_neighbors(const ChunkCoord coord) const
{
    ZoneScopedN("Get Chunk Neighbors");
    std::array<std::shared_ptr<const ChunkData>, 8> chunks{};
    int count = 0;
    for (const auto direction : directionList)
    {
        const auto offsetX = directionOffsetX[direction];
        const auto offsetZ = directionOffsetZ[direction];
        const auto offset_coord = ChunkCoord{ coord.x + offsetX, coord.z + offsetZ };

        const auto neighbor_chunk = m_chunkCache->get_chunk(offset_coord);
        if (neighbor_chunk == nullptr)
        {
            std::println("{} Neighbor does not exist: {}", coord, offset_coord);
            return std::nullopt;
        }

        if (neighbor_chunk->_state.load(std::memory_order::acquire) == ChunkState::Uninitialized)
        {
            // const auto& init_expected = _neighborBarrier[coord];
            // std::println("Chunk expected {} neighbors", init_expected._initial);
            std::println("{} Neighbor is uninitialized: {}", coord, offset_coord);
            return std::nullopt;
        }
        chunks[direction] = neighbor_chunk->_data;
        count++;
    }

    if(count == 8)
    {
        return chunks;
    } else {
        return std::nullopt;
    }
}