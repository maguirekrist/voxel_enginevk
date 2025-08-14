
#include "chunk_manager.h"
#include "../game/chunk.h"
#include "chunk_mesher.h"
#include "tracy/Tracy.hpp"
#include "../vk_mesh.h"
#include <memory>

#include "../vk_engine.h"

ChunkManager::ChunkManager() : m_chunkCache(0)
{
    auto max_thread = std::thread::hardware_concurrency();
    _maxThreads = max_thread != 0 ? max_thread : _maxThreads;

    for(size_t i = 1; i < _maxThreads; i++)
    {
        _workers.emplace_back(&ChunkManager::work_chunk, this, i);
    }

}

ChunkManager::~ChunkManager()
{
    _running.store(false, std::memory_order::release);

    for (std::thread &worker : _workers) {
        worker.join();
    }
}

void ChunkManager::update()
{
    Chunk* chunk = nullptr;
    while (_readyChunks.try_dequeue(chunk))
    {
        chunk->_opaqueHandle = VulkanEngine::instance()._opaqueSet.insert(RenderObject{
                            .mesh = chunk->_meshData->mesh,
                            .material = VulkanEngine::instance()._materialManager.get_material("defaultmesh"),
                            .xzPos = glm::ivec2(chunk->_data->position.x, chunk->_data->position.y),
                            .layer = RenderLayer::Opaque
                        });
        chunk->_transparentHandle = VulkanEngine::instance()._transparentSet.insert(RenderObject{
            .mesh = chunk->_meshData->waterMesh,
            .material = VulkanEngine::instance()._materialManager.get_material("watermesh"),
            .xzPos = glm::ivec2(chunk->_data->position.x, chunk->_data->position.y),
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
    auto new_chunks = m_chunkCache.slide({ changeX, changeZ });

    for (const auto& chunk : new_chunks)
    {
        _chunkWorkQueue.enqueue(std::make_shared<const ChunkWork>(
            chunk,
            chunk->_gen.load(std::memory_order::acquire),
            chunk->_data,
            ChunkWork::Phase::Generate,
            mapRange
        ));
    }

    std::println("Work Queue: {}", _chunkWorkQueue.size_approx());
    std::println("Active chunks: {}", m_chunkCache.m_chunks.size());
}

Chunk* ChunkManager::get_chunk(const ChunkCoord coord) const
{
    return m_chunkCache.get_chunk(coord);
}

void ChunkManager::initialize_map(const MapRange mapRange)
{
    m_chunkCache = ChunkCache{GameConfig::DEFAULT_VIEW_DISTANCE};

    for (auto& chunk : m_chunkCache.m_chunks)
    {
        _chunkWorkQueue.enqueue(std::make_shared<const ChunkWork>(
            chunk.get(),
            chunk->_gen.load(std::memory_order::acquire),
            chunk->_data,
            ChunkWork::Phase::Generate,
            mapRange
        ));
    }
}


NeighborStatus ChunkManager::chunk_has_neighbors(const ChunkCoord coord) const
{
    int count = 0;

    auto chunk = m_chunkCache.get_chunk(coord);
    if (chunk == nullptr)
    {
        return NeighborStatus::Missing;
    }

    if (chunk->_state.load() == ChunkState::Border)
    {
        //std::println("{} at border...", coord);
        return NeighborStatus::Border;
    }

    for (const auto direction : directionList)
    {
        const auto offsetX = directionOffsetX[direction];
        const auto offsetZ = directionOffsetZ[direction];
        const auto offset_coord = ChunkCoord{ coord.x + offsetX, coord.z + offsetZ };

        auto neighbor_chunk = m_chunkCache.get_chunk(offset_coord);
        if (neighbor_chunk != nullptr)
        {
            switch (neighbor_chunk->_state.load())
            {
                case ChunkState::Uninitialized:
                    return NeighborStatus::Incomplete;
                case ChunkState::Border:
                case ChunkState::Generated:
                case ChunkState::Rendered:
                    count++;
                    break;
            }
        } else
        {
            std::println("{} neighbor {}, not in map", coord, offset_coord);
            // throw std::runtime_error(std::format("Chunk does not exist: {}", offset_coord));
            return NeighborStatus::Incomplete;
        }
    }

    if(count == 8)
    {
        return NeighborStatus::Ready;
    }

    return NeighborStatus::Incomplete;
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

        const auto chunk = m_chunkCache.get_chunk(offset_coord);
        if (chunk == nullptr)
        {
            return std::nullopt;
        }

        if (chunk->_state.load() == ChunkState::Uninitialized) return std::nullopt;
        chunks[direction] = chunk->_data;
        count++;
    }

    if(count == 8)
    {
        return chunks;
    } else {
        return std::nullopt;
    }
}

void ChunkManager::work_chunk(int threadId)
{
    tracy::SetThreadName(std::format("Chunk Update Thread: {}", threadId).c_str());
    while(_running)
    {
        if (ChunkWorkPayload work_item; _chunkWorkQueue.try_dequeue(work_item))
        {
            switch (work_item->phase)
            {
                case ChunkWork::Phase::Generate:
                    work_item->data->generate();
                    if (work_item->chunk->_gen.load(std::memory_order::acquire) == work_item->gen)
                    {
                        work_item->chunk->_state.store(ChunkState::Generated);
                        work_item->chunk->_data = std::move(work_item->data);
                        if (work_item->mapRange.is_border(work_item->data->coord))
                        {
                            work_item->chunk->_state.store(ChunkState::Border);
                        }
                        _chunkWorkQueue.enqueue(
                            std::make_shared<const ChunkWork>(
                                work_item->chunk,
                                work_item->gen,
                                work_item->chunk->_data, //we remember to move valid chunk data now from the generation step...
                                ChunkWork::Phase::WaitingForNeighbors,
                                work_item->mapRange
                                )
                            );
                    }
                    break;
                case ChunkWork::Phase::WaitingForNeighbors:
                    //Double check this... not sure if this is safe...
                    if (work_item->gen != work_item->chunk->_gen.load(std::memory_order::acquire))
                    {
                        //This is an old job at this point, we just drop it?
                        break;
                    }
                    if (work_item->chunk->_state.load() == ChunkState::Border && !_mapRange.is_border(work_item->data->coord) )
                    {
                        work_item->chunk->_state.store(ChunkState::Generated);
                    }
                    switch(chunk_has_neighbors(work_item->data->coord))
                    {
                        case NeighborStatus::Ready:
                            _chunkWorkQueue.enqueue(
                                std::make_shared<const ChunkWork>(
                                    work_item->chunk,
                                    work_item->gen,
                                    work_item->data,
                                    ChunkWork::Phase::Mesh,
                                    work_item->mapRange)
                            );
                            break;
                        case NeighborStatus::Incomplete:
                        case NeighborStatus::Border:
                            //This sleep seems to effect starvation of the mesh job...
                            //std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            _chunkWorkQueue.enqueue(std::move(work_item));
                            break;
                        case NeighborStatus::Missing:
                            break;
                    }
                    break;
                case ChunkWork::Phase::Mesh:
                    auto neighbors = get_chunk_neighbors(work_item->data->coord);
                    ChunkMesher mesher { work_item->data, neighbors };
                    auto meshData = mesher.generate_mesh();

                    //Commit change if chunk is still valid.
                    if (work_item->chunk->_gen.load(std::memory_order::acquire) == work_item->gen)
                    {
                        work_item->chunk->_state.store(ChunkState::Rendered);
                        work_item->chunk->_meshData = std::move(meshData);

                        _readyChunks.enqueue(work_item->chunk);

                        if (!work_item->chunk->_meshData->waterMesh->_vertices.empty())
                        {
                            VulkanEngine::instance()._meshManager.UploadQueue.enqueue(work_item->chunk->_meshData->waterMesh);
                        }

                        if (work_item->chunk->_meshData->mesh->_vertices.empty())
                        {
                            throw std::runtime_error("Chunk mesh is empty");
                        }

                        VulkanEngine::instance()._meshManager.UploadQueue.enqueue(work_item->chunk->_meshData->mesh);
                    }

                    break;
            }
        } else if (!_running)
        {
            break;
        }
    }
}
