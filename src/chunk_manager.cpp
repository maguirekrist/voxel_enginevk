
#include "chunk.h"
#include "chunk_mesher.h"
#include "vk_engine.h"
#include <chunk_manager.h>
#include <vulkan/vulkan_core.h>

void ChunkManager::updatePlayerPosition(int x, int z)
{
    ChunkCoord playerChunk = {x / static_cast<int>(CHUNK_SIZE), z / static_cast<int>(CHUNK_SIZE)};  // Assuming 16x16 chunks
    if (playerChunk == _lastPlayerChunk && !_initLoad) return;

    std::unique_lock<std::mutex> lock(_mutex);

    fmt::println("Main thread begin work!");

    auto changeX = playerChunk.x - _lastPlayerChunk.x;
    auto changeZ = playerChunk.z - _lastPlayerChunk.z;
    _lastPlayerChunk = playerChunk;
    _oldWorldChunks = _worldChunks;
    updateWorldState();
    queueWorldUpdate(changeX, changeZ);
    _initLoad = false;

    fmt::println("Main thread completed work!");
    _cv.notify_one();
}

void ChunkManager::updateWorldState()
{
    for (int dx = -_viewDistance; dx <= _viewDistance; ++dx) {
        for (int dz = -_viewDistance; dz <= _viewDistance; ++dz) {
            ChunkCoord coord = {_lastPlayerChunk.x + dx, _lastPlayerChunk.z + dz};
            if (_worldChunks.find(coord) == _worldChunks.end()) {
                _worldChunks.insert(coord);
            }
        }
    }

    for (const auto& coord : _oldWorldChunks) {
        int dx = std::abs(coord.x - _lastPlayerChunk.x);
        int dz = std::abs(coord.z - _lastPlayerChunk.z);
        if (dx > _viewDistance || dz > _viewDistance) {
            _worldChunks.erase(coord);
        }
    }
}

void ChunkManager::queueWorldUpdate(int changeX, int changeZ)
{
    WorldUpdateJob newJob;
    newJob._changeX = changeX;
    newJob._changeZ = changeZ;

    //std::vector<ChunkCoord> pendingChunks;
    for (int dx = -_viewDistance; dx <= _viewDistance; ++dx) {
        for (int dz = -_viewDistance; dz <= _viewDistance; ++dz) {
            ChunkCoord coord = {_lastPlayerChunk.x + dx, _lastPlayerChunk.z + dz};
            if (_worldChunks.find(coord) != _worldChunks.end()
                && _oldWorldChunks.find(coord) == _oldWorldChunks.end()) {
                newJob._chunksToLoad.push(coord);
                //pendingChunks.push_back(coord);
            }
        }
    }

    // Identify chunks to unload
    //std::vector<ChunkCoord> chunksToUnload;
    for (const auto& coord : _oldWorldChunks) {
        int dx = std::abs(coord.x - _lastPlayerChunk.x);
        int dz = std::abs(coord.z - _lastPlayerChunk.z);
        if (dx > _viewDistance || dz > _viewDistance) {
            newJob._chunksToUnload.push(coord);
        }
    }

    _worldUpdateQueue.push(newJob);

    // Invalidate chunks that need remeshing
    // for(const auto& coord : pendingChunks)
    // {
    //     //We just simply need to invalidate chunks that border coord (new chunk to load)


    //     //add pending chunks to queue
    //     _chunksToLoad.push(coord);
    // }



    //Unload chunks
    // for (const auto& coord : chunksToUnload) {
    //     //This is dangerous because me modify the loadedChunks
    //     std::unique_ptr<Chunk> unloadChunk = std::move(loadedChunks[coord]);
    //     loadedChunks.erase(coord);
    //     VulkanEngine::unload_mesh(unloadChunk->_mesh);
    //     _chunkPool.push_back(std::move(unloadChunk));
    // }
}

void ChunkManager::worker()
{
    while (_running) {
        std::unique_lock<std::mutex> lock(_mutex);
        _cv.wait(lock, [this] { return !_worldUpdateQueue.empty() || !_running; });

        if (!_running) break;

        fmt::println("Worker thread begin work.");

        WorldUpdateJob updateJob = _worldUpdateQueue.front();
        _worldUpdateQueue.pop();

        lock.unlock();
        std::queue<Chunk*> meshQueue;
        while (!updateJob._chunksToLoad.empty()) {
            ChunkCoord coord = updateJob._chunksToLoad.front();
            updateJob._chunksToLoad.pop();

            std::unique_ptr<Chunk> chunk;
            //lock.lock();
            if (!_chunkPool.empty()) {
                chunk = std::move(_chunkPool.back());
                _chunkPool.pop_back();
                chunk->reset(coord);
                chunk->generate();
            } else {
                chunk = std::make_unique<Chunk>(coord);
            }
            //lock.unlock();


            std::vector<ChunkCoord> neighbor_chunks;
            //IF I'm heading in the positive z direction ->
            // I need the negative z, then the positive and negative x at the back z too.
            if(updateJob._changeZ != 0)
            {
                neighbor_chunks.push_back({ coord.x - 1, coord.z - updateJob._changeZ  });
                neighbor_chunks.push_back({ coord.x + 1, coord.z - updateJob._changeZ  });
                neighbor_chunks.push_back({ coord.x, coord.z - updateJob._changeZ  });
            }

            if(updateJob._changeX != 0)
            {
                neighbor_chunks.push_back({ coord.x - updateJob._changeX , coord.z });
                neighbor_chunks.push_back({ coord.x - updateJob._changeX , coord.z - 1 });
                neighbor_chunks.push_back({ coord.x - updateJob._changeX , coord.z + 1 });
            }

            //END THREAD_SAFE AREA

            for(const auto& cCoord : neighbor_chunks)
            {
                if(_loadedChunks.contains(cCoord))
                {
                    auto target_chunk = _loadedChunks[cCoord].get();
                    _loadedChunks[cCoord]->_isValid = false;
                    meshQueue.push(_loadedChunks[cCoord].get());
                }
            }

    
            //Dangerous, we modify loaded chunks
            _loadedChunks[coord] = std::move(chunk);
            meshQueue.push(_loadedChunks[coord].get());
        }

        while(!meshQueue.empty())
        {
            auto chunk = meshQueue.front();
            meshQueue.pop();
            if(chunk != nullptr && !chunk->_isValid)
            {
                Mesh oldMesh = chunk->_mesh;
                ChunkMesher mesher { chunk, this };
                mesher.execute();
                VulkanEngine::upload_mesh(chunk->_mesh);

                if(oldMesh._isActive)
                {
                    VulkanEngine::unload_mesh(oldMesh);
                }

                chunk->_isValid = true;
            }
        }

        while (!updateJob._chunksToUnload.empty())
        {
            auto coord = updateJob._chunksToUnload.front();
            updateJob._chunksToUnload.pop();
            std::unique_ptr<Chunk> unloadChunk = std::move(_loadedChunks[coord]);
            _loadedChunks.erase(coord);
            VulkanEngine::unload_mesh(unloadChunk->_mesh);
            _chunkPool.push_back(std::move(unloadChunk));
        }

        //ock.lock();
        fmt::println("Worker thread completed.");
    }
}

//This is used by the mesher
Block* ChunkManager::getBlockGlobal(int x, int y, int z)
{
    int chunkSize = static_cast<int>(CHUNK_SIZE);
    auto chunkCoord = World::get_chunk_coordinates({ x, y , z});
    auto localCoord = World::get_local_coordinates({ x, y, z});

    auto it = _loadedChunks.find({ chunkCoord.x, chunkCoord.y });
    if (it != _loadedChunks.end()) {
        return it->second->get_block({ localCoord.x, localCoord.y, localCoord.z });
    }
    return nullptr; // Or some default/error value
}

Block* ChunkManager::getBlockGlobal(glm::ivec3 pos)
{
    return getBlockGlobal(pos.x, pos.y, pos.z);
}