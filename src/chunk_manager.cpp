
#include "chunk.h"
#include "chunk_mesher.h"
#include "vk_engine.h"
#include <chunk_manager.h>
#include <vulkan/vulkan_core.h>

void ChunkManager::updatePlayerPosition(int x, int z)
{
    ChunkCoord playerChunk = {x / static_cast<int>(CHUNK_SIZE), z / static_cast<int>(CHUNK_SIZE)};  // Assuming 16x16 chunks
    if (playerChunk == _lastPlayerChunk && !_initLoad) return;

    std::unique_lock<std::mutex> lock(_mutexWorld);

    fmt::println("Main thread begin work!");

    auto changeX = playerChunk.x - _lastPlayerChunk.x;
    auto changeZ = playerChunk.z - _lastPlayerChunk.z;
    _lastPlayerChunk = playerChunk;
    _oldWorldChunks = _worldChunks;
    updateWorldState();
    queueWorldUpdate(changeX, changeZ);
    _initLoad = false;

    fmt::println("Main thread completed work!");
    lock.unlock();
    _cvWorld.notify_one();

 
    lock.lock();
    _cvWorld.wait(lock, [this] { return !_updatingWorldState && _worldUpdateQueue.empty(); });
    fmt::println("World update completed... refresh renderObjects");
    
    _renderChunks.clear();
    for(const auto& coord : _worldChunks)
    {
        auto it_chunk = _loadedChunks.find(coord);
        if(it_chunk != _loadedChunks.end() && it_chunk->second->_isValid)
        {
            RenderObject obj;
            obj.mesh = &it_chunk->second->_mesh;
            obj.material = VulkanEngine::get_material("defaultmesh");
            glm::mat4 translate = glm::translate(glm::mat4{ 1.0 }, glm::vec3(it_chunk->second->_position.x, 0, it_chunk->second->_position.y));
            obj.transformMatrix = translate;
            _renderChunks.push_back(obj);
        }
    }
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

    std::vector<ChunkCoord> pendingChunks;
    for (int dx = -_viewDistance; dx <= _viewDistance; ++dx) {
        for (int dz = -_viewDistance; dz <= _viewDistance; ++dz) {
            ChunkCoord coord = {_lastPlayerChunk.x + dx, _lastPlayerChunk.z + dz};
            if (_worldChunks.find(coord) != _worldChunks.end()
                && _oldWorldChunks.find(coord) == _oldWorldChunks.end()) {
                newJob._chunksToLoad.push(coord);
                newJob._chunksToMesh.push({ coord.x - changeX, coord.z - changeZ });
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
}

void ChunkManager::worldUpdate()
{
    while (_running) {
        std::unique_lock<std::mutex> lock(_mutexWorld);
        _cvWorld.wait(lock, [this] { return (!_worldUpdateQueue.empty() && !_updatingWorldState) || !_running; });

        if (!_running) break;

        _updatingWorldState = true;

        fmt::println("Worker thread begin work.");

        WorldUpdateJob updateJob = _worldUpdateQueue.front();
        _worldUpdateQueue.pop();

        while (!updateJob._chunksToLoad.empty()) {
            ChunkCoord coord = updateJob._chunksToLoad.front();
            updateJob._chunksToLoad.pop();

            std::unique_ptr<Chunk> chunk;
            if (!_chunkPool.empty()) {
                chunk = std::move(_chunkPool.back());
                _chunkPool.pop_back();
                chunk->reset(coord);
                chunk->generate();
            } else {
                chunk = std::make_unique<Chunk>(coord);
            }
    
            _loadedChunks[coord] = std::move(chunk);
        }

        //Calculate the chunks to mesh now, which are the 
        while(!updateJob._chunksToMesh.empty())
        {
            ChunkCoord coord = updateJob._chunksToMesh.front();
            updateJob._chunksToMesh.pop();

            auto chunk = _loadedChunks[coord].get();
            auto neighbors = get_chunk_neighbors(coord);
            if(chunk != nullptr && neighbors.size() == 8)
            {
                _chunkUpdateQueue.push({ chunk, neighbors });
            }
        }

        _cvMesh.notify_all();

        std::unique_lock<std::mutex> meshLock(_mutexMesh);
        _cvMesh.wait(meshLock, [this] { return _chunkUpdateQueue.empty() && _activeWorkers == 0; });
        
        while(!_meshUploadQueue.empty())
        {
            Mesh* mesh = _meshUploadQueue.front();
            _meshUploadQueue.pop();
            VulkanEngine::upload_mesh(*mesh);
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

        _updatingWorldState = false;

        fmt::println("Worker thread completed.");

        lock.unlock();
        _cvWorld.notify_one();
    }
}

std::vector<Chunk*> ChunkManager::get_chunk_neighbors(ChunkCoord coord)
{
    std::vector<Chunk*> chunks;
    for (auto direction : directionList)
    {
        auto offsetX = directionOffsetX[direction];
        auto offsetZ = directionOffsetZ[direction];
        auto offset_coord = ChunkCoord{ coord.x + offsetX, coord.z + offsetZ };
        auto chunk_it = _loadedChunks.find(offset_coord);
        if(chunk_it != _loadedChunks.end())
        {
            chunks.push_back(chunk_it->second.get());
        }
    }
    return chunks;
}

void ChunkManager::meshChunk()
{
    while(_running)
    {
        std::unique_lock lock(_mutexMesh);
        _cvMesh.wait(lock, [this]() { return !_chunkUpdateQueue.empty() || !_running; });

        if (!_running) break;

        //Acquire a mesh
        if(!_chunkUpdateQueue.empty())
        {
            _activeWorkers++;
            const auto [chunk, neighbors] = _chunkUpdateQueue.front();
            _chunkUpdateQueue.pop();

            ChunkCoord cCoord = { chunk->_position.x / static_cast<int>(CHUNK_SIZE), chunk->_position.y / static_cast<int>(CHUNK_SIZE) };    
            std::vector<Chunk*> neighbor_chunks = get_chunk_neighbors(cCoord);
            
            //Unlock as we can do this work async with other meshChunk threads
            lock.unlock();
            if(chunk != nullptr && !chunk->_isValid && neighbor_chunks.size() == 8)
            {
                Mesh oldMesh = chunk->_mesh;

                ChunkMesher mesher { *chunk, neighbor_chunks };
                mesher.execute();
                chunk->_isValid = true;
            }

            lock.lock();
            _activeWorkers --;

            if(chunk->_isValid) {
                _meshUploadQueue.push(&chunk->_mesh);
            }

            if(_chunkUpdateQueue.empty() && _activeWorkers == 0)
            {
                _cvMesh.notify_all();
            }
        }

    }
}

//This is used by the mesher
Block* ChunkManager::getBlockGlobal(int x, int y, int z)
{
    //std::shared_lock lock(_loadedMutex);
    auto chunkCoord = World::get_chunk_coordinates({ x, y, z });
    auto localCoord = World::get_local_coordinates({ x, y, z });

    auto it = _loadedChunks.find({ chunkCoord.x, chunkCoord.y });
    if (it != _loadedChunks.end()) {
        return it->second->get_block({ localCoord.x, localCoord.y, localCoord.z });
    }
    return nullptr; // Or some default/error value
}
