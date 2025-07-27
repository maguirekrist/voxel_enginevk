
#include "chunk_manager.h"
#include "chunk.h"
#include "chunk_mesher.h"
#include "tracy/Tracy.hpp"
#include "vk_mesh.h"
#include <memory>
#include "vk_engine.h"

ChunkManager::ChunkManager()
        :
        _viewDistance(DEFAULT_VIEW_DISTANCE),
          _maxChunks((2 * DEFAULT_VIEW_DISTANCE + 1) * (2 * DEFAULT_VIEW_DISTANCE + 1)),
          _maxThreads(4),
          _running(true)
{

    _chunks.reserve(10'000);
    _renderedChunks.reserve(_maxChunks);
    _worldChunks.reserve(_maxChunks);
    _oldWorldChunks.reserve(_maxChunks);

    for(size_t i = 0; i < _maxThreads; i++)
    {
        _workers.emplace_back(&ChunkManager::mesh_chunk, this, i);
    }
}

void ChunkManager::cleanup()
{
    std::println("ChunkManager::cleanup");
    _renderedChunks.clear();
    _transparentObjects.clear();
    for(const auto& chunk : _chunkList)
    {
        VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(chunk->_mesh));
        VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(chunk->_waterMesh));
    }

    _chunkList.clear();
    _chunks.clear();
}

ChunkManager::~ChunkManager()
{
    _running = false;

    _cvWork.notify_all();

    for (std::thread &worker : _workers) {
        worker.join();
    }

    std::println("ChunkManager::~ChunkManager");
}

void ChunkManager::update_player_position(const int x, const int z)
{
    fmt::println("Chunk Work Queue: {}", _chunkWorkQueue.size_approx());
    const ChunkCoord playerChunk = {x / static_cast<int>(CHUNK_SIZE), z / static_cast<int>(CHUNK_SIZE)};  // Assuming 16x16 chunks
    if (playerChunk == _lastPlayerChunk) return;

    const auto changeX = playerChunk.x - _lastPlayerChunk.x;
    const auto changeZ = playerChunk.z - _lastPlayerChunk.z;
    std::println("Player changed chunks, delta: x {},  z {}", changeX, changeZ);
    _lastPlayerChunk = playerChunk;
    _oldWorldChunks.swap(_worldChunks);
    update_world_state();



    _renderedChunks.clear();
    _transparentObjects.clear();

    for(const auto& chunkCoord : _worldChunks)
    {
        if (!_chunks.contains(chunkCoord))
        {
            auto unchunked = std::make_shared<Chunk>(chunkCoord);
            _chunkWorkQueue.enqueue(ChunkWork { .chunk = unchunked, .phase = ChunkWork::Phase::Generate });
            std::unique_lock lock(_mapMutex);
            _chunks.insert({ chunkCoord, unchunked });
            _chunkList.push_back(std::move( unchunked));
        } else
        {
            auto chunkToRender = _chunks.at(chunkCoord);
            auto object = std::make_unique<RenderObject>(RenderObject{
                chunkToRender->_mesh,
               VulkanEngine::instance()._materialManager.get_material("defaultmesh"),
              glm::ivec2(chunkToRender->_position.x, chunkToRender->_position.y),
              RenderLayer::Opaque
            });
            _renderedChunks.push_back(std::move(object));

            auto waterObject = std::make_unique<RenderObject>(RenderObject{
                chunkToRender->_waterMesh,
                VulkanEngine::instance()._materialManager.get_material("watermesh"),
                glm::ivec2(chunkToRender->_position.x, chunkToRender->_position.y),
                RenderLayer::Transparent
            });
            _transparentObjects.push_back(std::move(waterObject));
        }
    }

    _cvWork.notify_all();

    fmt::println("Active chunks: {}", _chunks.size());
    fmt::println("Renderable chunks: {}", _renderedChunks.size());
    fmt::println("(world chunks): {}", _worldChunks.size());
}

void ChunkManager::update_world_state()
{
    _worldChunks.clear();
    for (int dx = -_viewDistance; dx <= _viewDistance; ++dx) {
        for (int dz = -_viewDistance; dz <= _viewDistance; ++dz) {
            ChunkCoord coord = {_lastPlayerChunk.x + dx, _lastPlayerChunk.z + dz};
            _worldChunks.push_back(coord);
        }
    }
}

// void ChunkManager::queueWorldUpdate(const int changeX, const int changeZ)
// {
//     WorldUpdateJob newJob{};
//
//     for (int dx = -_viewDistance; dx <= _viewDistance; ++dx) {
//         for (int dz = -_viewDistance; dz <= _viewDistance; ++dz) {
//             ChunkCoord coord = {_lastPlayerChunk.x + dx, _lastPlayerChunk.z + dz};
//             if (_worldChunks.contains(coord)
//                 && !_oldWorldChunks.contains(coord)) {
//
//                 //I think we generate a new chunk here.... try to see if chunk is already been generated?
//                 // if (auto chunkOpt = get_chunk(coord)) {
//                 //     if (auto chunkShared = chunkOpt->lock()) {
//                 //         chunkShared->reset(coord);
//                 //         _chunkGenQueue.enqueue(std::move(chunkShared));
//                 //     }
//                 // }
//
//                 auto new_chunk = std::make_unique<Chunk>(coord);
//                 _chunkGenQueue.enqueue(std::move(new_chunk));
//
//                 newJob._chunksToMesh.push({ coord.x - changeX, coord.z - changeZ });
//             }
//         }
//     }
//
//     // Identify chunks to unload
//     for (const auto& coord : _oldWorldChunks) {
//         int dx = std::abs(coord.x - _lastPlayerChunk.x);
//         int dz = std::abs(coord.z - _lastPlayerChunk.z);
//         if (dx > _viewDistance || dz > _viewDistance) {
//             newJob._chunksToUnload.push(coord);
//         }
//     }
//
//     _worldUpdateQueue.push(newJob);
//
//     //TIP! Use pendingChunks and chunksToUnload to help
//     //return std::make_pair(pendingChunks, chunksToUnload);
//
//
// }

// void ChunkManager::worldUpdate()
// {
//     tracy::SetThreadName("World Update Thread");
//     while (_running) {
//         std::unique_lock<std::mutex> lock(_mutexWorld);
//         _cvWorld.wait(lock, [this] { return (!_worldUpdateQueue.empty() && !_updatingWorldState) || !_running; });
//
//         if (!_running) break;
//
//         _updatingWorldState = true;
//
//         WorldUpdateJob updateJob = std::move(_worldUpdateQueue.front());
//         _worldUpdateQueue.pop();
//         {
//             ZoneScopedN("World Update Process");
//             //Calculate the chunks to mesh now, which are the
//             while(!updateJob._chunksToMesh.empty())
//             {
//                 ChunkCoord coord = updateJob._chunksToMesh.front();
//                 updateJob._chunksToMesh.pop();
//
//                 auto chunk = get_chunk(coord);
//                 auto neighbors = get_chunk_neighbors(coord);
//                 if(chunk.has_value() && neighbors.has_value())
//                 {
//                     _chunkMeshQueue.enqueue(ChunkMeshJob{ chunk.value(), neighbors.value() });
//                 }
//             }
//         }
//         _workComplete = false;
//
//         _cvWork.notify_all();
//
//         {
//             std::unique_lock<std::mutex> workLock(_mutexWork);
//             _cvWork.wait(workLock, [this]{ return _workComplete == true;});
//         }
//
//
//         // Look into this
//         while (!updateJob._chunksToUnload.empty())
//         {
//             const auto coord = updateJob._chunksToUnload.front();
//             updateJob._chunksToUnload.pop();
//
//             auto chunk = std::move(_chunks.at(coord));
//             _chunks.erase(coord);
//             VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(chunk->_mesh));
//             VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(chunk->_waterMesh));
//         }
//
//         _updatingWorldState = false;
//         _initLoad = false;
//         _cvWorld.notify_one();
//     }
// }

std::optional<std::vector<ChunkView>> ChunkManager::get_chunk_neighbors(const ChunkCoord coord)
{
    ZoneScopedN("Get Chunk Neighbors");
    std::vector<ChunkView> chunks;
    int count = 0;
    for (const auto direction : directionList)
    {
        const auto offsetX = directionOffsetX[direction];
        const auto offsetZ = directionOffsetZ[direction];
        const auto offset_coord = ChunkCoord{ coord.x + offsetX, coord.z + offsetZ };

        if(const auto chunk = get_chunk(offset_coord))
        {
            if (!chunk.has_value()) return std::nullopt;

            count++;
            chunks.push_back(chunk.value());
        }
    }

    if(count == 8)
    {
        return chunks;
    } else {
        return std::nullopt;
    }
}

int ChunkManager::get_chunk_index(const ChunkCoord coord) const
{
    //get a unique index for the chunk coord
    const int x = coord.x - _lastPlayerChunk.x;
    const int y = coord.z - _lastPlayerChunk.z;

    // Normalize the relative coordinates to the range [0, 64] by adding viewDistance
    const int normalizedX = std::clamp(x + _viewDistance, 0, (2 * DEFAULT_VIEW_DISTANCE + 1) - 1);
    const int normalizedZ = std::clamp(y + _viewDistance, 0, (2 * DEFAULT_VIEW_DISTANCE + 1) - 1);

    // Calculate the 1D index in the vector
    const int index = normalizedZ * (2 * DEFAULT_VIEW_DISTANCE + 1) + normalizedX;

    return index;
}

// void ChunkManager::add_chunk(ChunkCoord coord, std::unique_ptr<Chunk>&& chunk)
// {
//     _chunks[coord] = std::move(chunk);
// }

std::optional<ChunkView> ChunkManager::get_chunk(const ChunkCoord coord)
{
    std::shared_lock lock(_mapMutex);
    if (_chunks.contains(coord))
    {

        return Chunk::to_view(*_chunks.at(coord));
    }
    return std::nullopt;
}

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

void ChunkManager::mesh_chunk(int threadId)
{
    tracy::SetThreadName(fmt::format("Chunk Update Thread: {}", threadId).c_str());
    while(_running)
    {
        if (ChunkWork work_item; _chunkWorkQueue.try_dequeue(work_item))
        {
            switch (work_item.phase)
            {
                case ChunkWork::Phase::Generate:
                    work_item.chunk->generate();
                    work_item.phase = ChunkWork::Phase::Mesh;
                    _chunkWorkQueue.enqueue(work_item);
                    break;
                case ChunkWork::Phase::Mesh:
                    auto chunkView = Chunk::to_view(*work_item.chunk);
                    auto neighbors = get_chunk_neighbors(work_item.chunk->_chunkCoord);

                    // if (!neighbors.has_value())
                    // {
                    //     //yield and try again
                    //     //this is causing a failure.
                    //     _chunkWorkQueue.enqueue(work_item);
                    //     continue;
                    // };

                    ChunkMesher mesher { chunkView, neighbors };
                    auto [landMesh, waterMesh] = mesher.generate_mesh();

                    work_item.chunk->_mesh = landMesh;
                    work_item.chunk->_waterMesh = waterMesh;

                    if(!waterMesh->_vertices.empty())
                    {
                        VulkanEngine::instance()._meshManager.UploadQueue.enqueue(waterMesh);
                    }
                    VulkanEngine::instance()._meshManager.UploadQueue.enqueue(landMesh);
                    break;
            }
        } else if (!_running)
        {
            break;
        }
    }
}
