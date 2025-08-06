
#include "chunk_manager.h"
#include "game/chunk.h"
#include "chunk_mesher.h"
#include "tracy/Tracy.hpp"
#include "vk_mesh.h"
#include <memory>

#include "vk_engine.h"

void ChunkWorkQueue::enqueue(const ChunkWork& work)
{
    switch (work.phase)
    {
    case ChunkWork::Phase::Generate:
        _highPriority.enqueue(work);
        break;
    case ChunkWork::Phase::Mesh:
        _medPriority.enqueue(work);
        break;
    case ChunkWork::Phase::WaitingForNeighbors:
        _lowPriority.enqueue(work);
        break;
    }
}

bool ChunkWorkQueue::try_dequeue(ChunkWork& work)
{
    return _highPriority.try_dequeue(work) || _medPriority.try_dequeue(work) || _lowPriority.try_dequeue(work);
}

void ChunkWorkQueue::wait_dequeue(ChunkWork& work)
{
    if (_highPriority.try_dequeue(work)) return;
    if (_medPriority.try_dequeue(work)) return;
    _lowPriority.wait_dequeue(work);
}

bool ChunkWorkQueue::wait_dequeue_timed(ChunkWork& work, const int timeout_ms)
{
    return _highPriority.try_dequeue(work) || _medPriority.try_dequeue(work) || _lowPriority.wait_dequeue_timed(work, std::chrono::milliseconds(timeout_ms));
}

size_t ChunkWorkQueue::size_approx() const
{
    return _highPriority.size_approx() + _lowPriority.size_approx() + _medPriority.size_approx();
}

ChunkManager::ChunkManager()
{
    _chunks.reserve(_maxChunks);

    auto max_thread = std::thread::hardware_concurrency();
    _maxThreads = max_thread != 0 ? max_thread : _maxThreads;

    _updateThread = std::thread(&ChunkManager::work_update, this, 0);

    for(size_t i = 1; i < _maxThreads; i++)
    {
        _workers.emplace_back(&ChunkManager::work_chunk, this, i);
    }

}

void ChunkManager::cleanup()
{
    std::println("ChunkManager::cleanup");
    // for(const auto& [key, chunk] : _chunks)
    // {
    //     VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(chunk->_mesh));
    //     VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(chunk->_waterMesh));
    // }

    _running = false;

    for (std::thread &worker : _workers) {
        worker.join();
    }

    _updateThread.join();
    _chunks.clear();
}

void ChunkManager::poll_world_update()
{
    WorldUpdateResult result{};
    //Process world updates.
    while (_worldUpdateResultQueue.try_dequeue(result))
    {
        m_snapshot[result.chunk->_chunkCoord] = result.chunk;
    }
}

ChunkManager::~ChunkManager()
{
    std::println("ChunkManager::~ChunkManager");
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

    if (_initialLoad)
    {
        _initialLoad = false;
        return initialize_map(mapRange);
    }
    //calculate old chunks and remove.
    update_map(mapRange, { changeX, changeZ });
    std::println("Work Queue: {}", _chunkWorkQueue.size_approx());
}

void ChunkManager::update_map(const MapRange mapRange, const ChunkCoord delta)
{
    if (delta.x == 0 && delta.z == 0) return;

    std::vector<ChunkWork> WorkQueue;
    WorkQueue.reserve(_maxChunks);

    auto remove_chunk = [&, this](const ChunkCoord chunkCoord)
    {
        if (_chunks.contains(chunkCoord))
        {
            auto chunk = _chunks.at(chunkCoord);
            _chunks.erase(chunkCoord);
        } else
        {
            std::println("Chunk does not exist: {}", chunkCoord);
        }
    };

    // new chunks will always be at the border.
    auto add_chunk = [&, this](const ChunkCoord chunkCoord)
    {
        if (!_chunks.contains(chunkCoord))
        {
            auto unchunked = std::make_shared<Chunk>(chunkCoord);
            _chunks.insert({ chunkCoord, unchunked });
            WorkQueue.emplace_back(unchunked, ChunkWork::Phase::Generate, mapRange);
        } else
        {
            std::println("Chunk already exists: {}", chunkCoord);
        }
    };

    auto update_chunk = [&, this](const ChunkCoord chunkCoord)
    {
        if (mapRange.is_border(chunkCoord))
        {
            return;
        }

        if (!_chunks.contains(chunkCoord))
        {
            std::println("Chunk does not exist: {}", chunkCoord);
            throw std::runtime_error(std::format("Chunk does not exist: {}", chunkCoord));
        }

        auto chunkToUpdate = _chunks.at(chunkCoord);
        if (chunkToUpdate->_state.load() == ChunkState::Border)
        {
            chunkToUpdate->_state.store(ChunkState::Generated);
            WorkQueue.emplace_back(chunkToUpdate, ChunkWork::Phase::WaitingForNeighbors, mapRange);
        } else
        {
            std::println("The work queue is {}", this->_chunkWorkQueue.size_approx());
            throw std::runtime_error(std::format("Chunk is not border {}", chunkCoord));
        }
    };

    if (delta.x == 1)
    {
        for (auto mapZ = mapRange.low_z; mapZ <= mapRange.high_z; ++mapZ)
        {
            const auto removeCoord = ChunkCoord{mapRange.low_x - 1, mapZ};
            remove_chunk(removeCoord);

            const auto newChunk = ChunkCoord{mapRange.high_x, mapZ};
            add_chunk(newChunk);

            const auto oldBorderCoord = ChunkCoord{mapRange.high_x - 1, mapZ};
            update_chunk(oldBorderCoord);
        }
    }

    if (delta.x == -1)
    {
        for (auto mapZ = mapRange.low_z; mapZ <= mapRange.high_z; ++mapZ)
        {
            const auto removeCoord = ChunkCoord{mapRange.high_x + 1, mapZ};
            remove_chunk(removeCoord);

            const auto newChunk = ChunkCoord{mapRange.low_x, mapZ};
            add_chunk(newChunk);

            const auto oldBorderCoord = ChunkCoord{mapRange.low_x + 1, mapZ};
            update_chunk(oldBorderCoord);
        }
    }

    if (delta.z == 1)
    {
        for (auto mapX = mapRange.low_x; mapX <= mapRange.high_x; ++mapX)
        {
            const auto removeCoord = ChunkCoord{mapX, mapRange.low_z - 1};
            remove_chunk(removeCoord);

            const auto newChunk = ChunkCoord{mapX, mapRange.high_z};
            add_chunk(newChunk);

            const auto oldBorderCoord = ChunkCoord{mapX, mapRange.high_z - 1};
            update_chunk(oldBorderCoord);
        }
    }

    if (delta.z == -1)
    {
        for (auto mapX = mapRange.low_x; mapX <= mapRange.high_x; ++mapX)
        {
            const auto removeCoord = ChunkCoord{mapX, mapRange.high_z + 1};
            remove_chunk(removeCoord);

            const auto newChunk = ChunkCoord{mapX, mapRange.low_z};
            add_chunk(newChunk);

            const auto oldBorderCoord = ChunkCoord{mapX, mapRange.low_z + 1};
            update_chunk(oldBorderCoord);
        }
    }

    for (const auto& chunkWork : WorkQueue)
    {
        _chunkWorkQueue.enqueue(chunkWork);
    }
}

void ChunkManager::initialize_map(const MapRange mapRange)
{
    std::vector<ChunkWork> WorkQueue;
    WorkQueue.reserve(_maxChunks);

    for (auto mapX = mapRange.low_x; mapX <= mapRange.high_x; ++mapX)
    {
        for (auto mapZ = mapRange.low_z; mapZ <= mapRange.high_z; ++mapZ)
        {
            auto chunkCoord = ChunkCoord{mapX, mapZ};
            auto new_chunk = std::make_shared<Chunk>(chunkCoord);
            _chunks.insert({ chunkCoord, new_chunk });
            WorkQueue.emplace_back(new_chunk, ChunkWork::Phase::Generate, mapRange);

            if (mapRange.is_border(chunkCoord)) {  continue; } //do not render chunks that are at the border. Just Generate.
        }
    }

    for (const auto& chunkWork : WorkQueue)
    {
        _chunkWorkQueue.enqueue(chunkWork);
    }
}


NeighborStatus ChunkManager::chunk_has_neighbors(const ChunkCoord coord)
{
    int count = 0;

    if (!_chunks.contains(coord))
    {
        // throw std::runtime_error(std::format("Chunk does not exist: {}", coord));
        return NeighborStatus::Missing;
    }

    auto chunk = _chunks.at(coord);
    if (chunk->_state.load() == ChunkState::Border)
    {
        std::println("{} at border...", coord);
        return NeighborStatus::Border;
    }

    for (const auto direction : directionList)
    {
        const auto offsetX = directionOffsetX[direction];
        const auto offsetZ = directionOffsetZ[direction];
        const auto offset_coord = ChunkCoord{ coord.x + offsetX, coord.z + offsetZ };

        if (_chunks.contains(offset_coord))
        {
            const auto neighbor_chunk = _chunks.at(offset_coord);
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

    //std::println("{} not ready...", coord);
    return NeighborStatus::Incomplete;
}

std::optional<std::array<std::shared_ptr<Chunk>, 8>> ChunkManager::get_chunk_neighbors(const ChunkCoord coord)
{
    ZoneScopedN("Get Chunk Neighbors");
    std::array<std::shared_ptr<Chunk>, 8> chunks;
    int count = 0;
    for (const auto direction : directionList)
    {
        const auto offsetX = directionOffsetX[direction];
        const auto offsetZ = directionOffsetZ[direction];
        const auto offset_coord = ChunkCoord{ coord.x + offsetX, coord.z + offsetZ };

        if(auto it = _chunks.find(offset_coord); it != _chunks.end())
        {
            auto chunk = it->second;
            if (chunk->_state.load() == ChunkState::Uninitialized) return std::nullopt;
            chunks[direction] = chunk;
            count++;
        } else
        {
            return std::nullopt;
        }
    }

    if(count == 8)
    {
        return chunks;
    } else {
        return std::nullopt;
    }
}

// int ChunkManager::get_chunk_index(const ChunkCoord coord) const
// {
//     //get a unique index for the chunk coord
//     const int x = coord.x - _lastPlayerChunk.x;
//     const int y = coord.z - _lastPlayerChunk.z;
//
//     // Normalize the relative coordinates to the range [0, 64] by adding viewDistance
//     const int normalizedX = std::clamp(x + _viewDistance, 0, (2 * GameConfig::DEFAULT_VIEW_DISTANCE + 1) - 1);
//     const int normalizedZ = std::clamp(y + _viewDistance, 0, (1 + 2 * GameConfig::DEFAULT_VIEW_DISTANCE) - 1);
//
//     // Calculate the 1D index in the vector
//     const int index = normalizedZ * (2 * GameConfig::DEFAULT_VIEW_DISTANCE + 1) + normalizedX;
//
//     return index;
// }

// std::optional<std::shared_ptr<Chunk>> ChunkManager::get_chunk(const ChunkCoord coord)
// {
//     if (_chunks.contains(coord))
//     {
//         return _chunks.at(coord);
//     }
//     return std::nullopt;
// }

// void ChunkManager::save_chunk(const Chunk &chunk, const std::string &filename)
// {
//     std::ofstream outFile(filename, std::ios::binary);
//
//     outFile.write(reinterpret_cast<const char*>(&chunk._position), sizeof(chunk._position));
//
//     for (const auto& block : chunk._blocks)
//     {
//         outFile.write(reinterpret_cast<const char*>(&block), sizeof(block));
//     }
//
//     outFile.close();
// }

// std::unique_ptr<Chunk> ChunkManager::load_chunk(const std::string &filename)
// {
//     auto chunk = std::make_unique<Chunk>();
//
//     std::ifstream inFile(filename, std::ios::binary);
//
//     inFile.read(reinterpret_cast<char*>(&chunk->_position), sizeof(chunk->_position));
//
//     for (auto& block : chunk->_blocks)
//     {
//         inFile.read(reinterpret_cast<char *>(&block), sizeof(block));
//     }
//
//     inFile.close();
//     return chunk;
// }



void ChunkManager::work_chunk(int threadId)
{
    tracy::SetThreadName(std::format("Chunk Update Thread: {}", threadId).c_str());
    while(_running)
    {
        if (ChunkWork work_item; _chunkWorkQueue.try_dequeue(work_item))
        {
            switch (work_item.phase)
            {
                case ChunkWork::Phase::Generate:
                    work_item.chunk->generate();
                    if (work_item.mapRange.is_border(work_item.chunk->_chunkCoord))
                    {
                        //std::println("Marking chunk {} as border", work_item.chunk->_chunkCoord);
                        work_item.chunk->_state.store(ChunkState::Border);
                        break;
                    }
                    work_item.phase = ChunkWork::Phase::WaitingForNeighbors;
                    _chunkWorkQueue.enqueue(work_item);
                    break;
                case ChunkWork::Phase::WaitingForNeighbors:
                    switch(chunk_has_neighbors(work_item.chunk->_chunkCoord))
                    {
                        case NeighborStatus::Ready:
                            work_item.phase = ChunkWork::Phase::Mesh;
                            _chunkWorkQueue.enqueue(work_item);
                            break;
                        case NeighborStatus::Incomplete:
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            _chunkWorkQueue.enqueue(work_item);
                            break;
                        case NeighborStatus::Missing:
                        case NeighborStatus::Border:
                            break;
                    }
                    break;
                case ChunkWork::Phase::Mesh:
                    auto neighbors = get_chunk_neighbors(work_item.chunk->_chunkCoord);
                    ChunkMesher mesher { work_item.chunk, neighbors };
                    mesher.generate_mesh();
                    work_item.chunk->_state.store(ChunkState::Rendered);

                    if(!work_item.chunk->_waterMesh->_vertices.empty())
                    {
                        VulkanEngine::instance()._meshManager.UploadQueue.enqueue(work_item.chunk->_waterMesh);
                    }
                    VulkanEngine::instance()._meshManager.UploadQueue.enqueue(work_item.chunk->_mesh);

                    //_worldUpdateResultQueue.enqueue(WorldUpdateResult{ .chunk = work_item.chunk });
                    break;
            }
        } else if (!_running)
        {
            break;
        }
    }
}

void ChunkManager::work_update(int threadId)
{
    tracy::SetThreadName(std::format("Chunk Update Thread: {}", threadId).c_str());
    while(_running)
    {
        MapRange mapRange;
        if (_mapUpdateQueue.try_dequeue(mapRange))
        {

        }
    }
}
