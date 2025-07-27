
#include "chunk_manager.h"
#include "chunk.h"
#include "chunk_mesher.h"
#include "tracy/Tracy.hpp"
#include "vk_mesh.h"
#include <memory>
#include "vk_engine.h"

ChunkManager::ChunkManager() 
        : _viewDistance(DEFAULT_VIEW_DISTANCE),
          _maxChunks((2 * DEFAULT_VIEW_DISTANCE + 1) * (2 * DEFAULT_VIEW_DISTANCE + 1)),
          _maxThreads(4),
          _sync_point(4),
          _running(true)
{

    _chunks.reserve(_maxChunks);
    _renderedChunks.reserve(_maxChunks);

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
    for(const auto& chunk : _chunks | std::views::values)
    {
        VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(chunk->_mesh));
        VulkanEngine::instance()._meshManager.UnloadQueue.enqueue(std::move(chunk->_waterMesh));
    }
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
    const ChunkCoord playerChunk = {x / static_cast<int>(CHUNK_SIZE), z / static_cast<int>(CHUNK_SIZE)};  // Assuming 16x16 chunks
    if (playerChunk == _lastPlayerChunk) return;

    const auto changeX = playerChunk.x - _lastPlayerChunk.x;
    const auto changeZ = playerChunk.z - _lastPlayerChunk.z;
    _lastPlayerChunk = playerChunk;
    _oldWorldChunks = _worldChunks;
    update_world_state();
    //queueWorldUpdate(changeX, changeZ);

    _renderedChunks.clear();
    _transparentObjects.clear();
    for(const auto& chunkCoord : _worldChunks)
    {
        auto chunk = get_chunk(chunkCoord);

        if (!chunk) continue;

        // auto object = std::make_shared<RenderObject>(RenderObject{
        //    realChunk->_mesh,
        //     VulkanEngine::instance()._materialManager.get_material("defaultmesh"),
        //    glm::ivec2(realChunk->_position.x, realChunk->_position.y),
        //    RenderLayer::Opaque
        // });
        // _renderedChunks.push_back(object);
        //
        // auto waterObject = std::make_shared<RenderObject>(RenderObject{
        //     realChunk->_waterMesh,
        //     VulkanEngine::instance()._materialManager.get_material("watermesh"),
        //     glm::ivec2(realChunk->_position.x, realChunk->_position.y),
        //     RenderLayer::Transparent
        // });
        // _transparentObjects.push_back(waterObject);
        
    }

    fmt::println("Active Renderable chunks: {}", _chunks.size());
    // fmt::println("Chunks to unload: {}", old_chunks.size());
}

void ChunkManager::update_world_state()
{
    for (int dx = -_viewDistance; dx <= _viewDistance; ++dx) {
        for (int dz = -_viewDistance; dz <= _viewDistance; ++dz) {
            ChunkCoord coord = {_lastPlayerChunk.x + dx, _lastPlayerChunk.z + dz};
            if (!_worldChunks.contains(coord)) {
                _worldChunks.insert(coord);
            }
        }
    }

    for (const auto& coord : _oldWorldChunks) {
        const int dx = std::abs(coord.x - _lastPlayerChunk.x);
        const int dz = std::abs(coord.z - _lastPlayerChunk.z);
        if (dx > _viewDistance || dz > _viewDistance) {
            _worldChunks.erase(coord);
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

std::optional<std::array<ChunkView, 8>> ChunkManager::get_chunk_neighbors(const ChunkCoord coord)
{
    ZoneScopedN("Get Chunk Neighbors");
    //std::vector<ChunkView> chunks = std::vector<ChunkView>(8);
    int count = 0;
    for (const auto direction : directionList)
    {
        const auto offsetX = directionOffsetX[direction];
        const auto offsetZ = directionOffsetZ[direction];
        const auto offset_coord = ChunkCoord{ coord.x + offsetX, coord.z + offsetZ };

        if(const auto chunk = get_chunk(offset_coord))
        {
            count++;
            //chunks.emplace_back(chunk.value());
        }
    }

    if(count == 8)
    {
        //TOOD: FIX THIS!
        return std::nullopt;
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
    if(const auto chunk_it = _chunks.find(coord); chunk_it != _chunks.end())
    {
        const auto temp_chunk = chunk_it->second.get();
        return Chunk::to_view(*temp_chunk);
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
        //prevent this area from being done unless there is actual work...
        {
            std::unique_lock<std::mutex> workLock(_mutexWork);
            _cvWork.wait(workLock, [this](){ return _workComplete == false || !_running; });

            if (!_running) break;
        }

        while(_running)
        {
            // First pass: Chunk generation
            //TODO: THIS IS PROBLEMATIC!
            if(std::unique_ptr<Chunk> chunk; _chunkGenQueue.wait_dequeue_timed(chunk, std::chrono::milliseconds(5)))
            {
                if (chunk) {
                    chunk->generate();
                    _chunkMeshQueue.enqueue(std::move(chunk));
                }
            } else {
                break;
            }   
        }

        _sync_point.arrive_and_wait();

        while(_running)
        {
            if(std::unique_ptr<Chunk> chunk; _chunkMeshQueue.wait_dequeue_timed(chunk, std::chrono::milliseconds(5)))
            {
                auto chunkView = Chunk::to_view(*chunk);
                auto neighbors = get_chunk_neighbors(chunk->_chunkCoord);



                ChunkMesher mesher { chunkView, neighbors.value() };
                auto [landMesh, waterMesh] = mesher.generate_mesh();

                if(!waterMesh->_vertices.empty())
                {
                    //VulkanEngine::instance()._meshManager.SwapQueue.enqueue(std::make_pair(newWaterMesh, chunk->_waterMesh));
                    VulkanEngine::instance()._meshManager.UploadQueue.enqueue(std::move(waterMesh));
                }

                //VulkanEngine::instance()._meshManager.SwapQueue.enqueue(std::make_pair(newMesh, chunk->_mesh));
                VulkanEngine::instance()._meshManager.UploadQueue.enqueue(std::move(landMesh));
            } else {
                break;
            }
        }

        _sync_point.arrive_and_wait();

        if(!_workComplete.exchange(true))
        {
            //First thread to enter this area.
            _cvWork.notify_one();   
        }
    }
}
