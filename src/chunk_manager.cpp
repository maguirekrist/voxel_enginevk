
#include "chunk_manager.h"
#include "chunk_mesher.h"
#include "vk_engine.h"


ChunkManager::ChunkManager(VulkanEngine& renderer) 
        : _viewDistance(DEFAULT_VIEW_DISTANCE), 
          _maxChunks((2 * DEFAULT_VIEW_DISTANCE + 1) * (2 * DEFAULT_VIEW_DISTANCE + 1)),
          _maxThreads(4),
          _activeWorkers(0),
          _running(true),
          _terrainGenerator(TerrainGenerator::instance()),
          _renderer(renderer) {
        _chunkPool.reserve(_maxChunks);
        for(int i = 0; i < _maxChunks; i++)
        {
            _chunkPool.emplace_back(std::make_unique<Chunk>());
        }
        for(size_t i = 0; i < _maxThreads; i++)
        {
            _workers.emplace_back(&ChunkManager::meshChunk, this);
            _workerCount++;
        }
        _updateThread = std::thread(&ChunkManager::worldUpdate, this);
        _updatingWorldState = false;
    }

ChunkManager::~ChunkManager()
{
    _running = false;
    _cvWorld.notify_one();
    _cvMesh.notify_all();
    for (std::thread &worker : _workers) {
        worker.join();
    }
    _updateThread.join();
}

void ChunkManager::updatePlayerPosition(int x, int z)
{
    ChunkCoord playerChunk = {x / static_cast<int>(CHUNK_SIZE), z / static_cast<int>(CHUNK_SIZE)};  // Assuming 16x16 chunks
    if (playerChunk == _lastPlayerChunk && !_initLoad) return;

    fmt::println("Main thread begin work!");

    auto changeX = playerChunk.x - _lastPlayerChunk.x;
    auto changeZ = playerChunk.z - _lastPlayerChunk.z;
    _lastPlayerChunk = playerChunk;
    _oldWorldChunks = _worldChunks;
    updateWorldState();
    queueWorldUpdate(changeX, changeZ);
    _initLoad = false;
    _cvWorld.notify_one();

    fmt::println("Main thread completed work!");
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
            } else {
                chunk = std::make_unique<Chunk>(coord);
            }

            auto ptrChunk = chunk.get();
            _loadedChunks[coord] = std::move(chunk);
            _chunkGenQueue.push( ptrChunk);
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
                _chunkMeshQueue.push({ chunk, neighbors });
            }
        }
        _workComplete = false;

        _cvGen.notify_all();    

        {
            std::unique_lock<std::mutex> workLock(_mutexWork);
            _cvWork.wait(workLock, [this]{ return _workComplete == true;});
        }

        while(!_meshUploadQueue.empty())
        {
            Mesh* mesh = _meshUploadQueue.front();
            _meshUploadQueue.pop();
            _renderer._mainMeshUploadQueue.push(mesh);
        }

        // while (!updateJob._chunksToUnload.empty())
        // {
        //     auto coord = updateJob._chunksToUnload.front();
        //     updateJob._chunksToUnload.pop();
        //     std::unique_ptr<Chunk> unloadChunk = std::move(_loadedChunks[coord]);
        //     _loadedChunks.erase(coord);
        //     _renderer._mainMeshUnloadQueue.push(unloadChunk->_mesh);
        //     _chunkPool.push_back(std::move(unloadChunk));
        // }

        _updatingWorldState = false;

        fmt::println("Worker thread completed.");

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
        // First pass: Chunk generation
        {
            std::unique_lock lock(_mutexPool);
            _cvGen.wait(lock, [this]() { return !_chunkGenQueue.empty() || !_running; });

            if (!_running) break;  // If shutdown signal, exit

            _activeWorkers++;

            // Process chunks in the queue (first pass)
            while (!_chunkGenQueue.empty()) {
                auto chunk = _chunkGenQueue.front();
                _chunkGenQueue.pop();

                lock.unlock();  // Unlock to allow other threads to access the queue

                ChunkCoord cCoord = { chunk->_position.x / static_cast<int>(CHUNK_SIZE), chunk->_position.y / static_cast<int>(CHUNK_SIZE) };
                if (chunk != nullptr) {
                    chunk->generate(_terrainGenerator);  // First pass work
                }

                lock.lock();  // Lock again for next queue access
            }
        }

        // Synchronization point or barrier before starting second pass
        {
            std::unique_lock lock(_mutex_barrier);
            _activeWorkers--;
            if (_activeWorkers == 0) {
                // All threads finished the first pass, signal second pass start
                _cvMesh.notify_all();
            } else {
                // Wait for all threads to finish first pass
                _cvMesh.wait(lock, [this] { return _activeWorkers == 0 || !_running; });
            }
        }

        {
            std::unique_lock lock(_mutexPool); 
            _cvMesh.wait(lock, [this] { return !_chunkMeshQueue.empty() || !_running; });

            if(!_running) break;
            _activeWorkers++;

            while(!_chunkMeshQueue.empty())
            {  
                const auto [chunk, neighbors] = _chunkMeshQueue.front();
                _chunkMeshQueue.pop();
                lock.unlock();
                if(chunk != nullptr && neighbors.size() == 8)
                {
                    //Mesh oldMesh = chunk->_mesh;
                    ChunkMesher mesher { *chunk, neighbors };
                    mesher.execute();
                    chunk->_isValid = true;
                }
                lock.lock();
                _meshUploadQueue.push(&chunk->_mesh);
            }
        }

        {
            std::unique_lock lock(_mutexWork);
            _activeWorkers--;
            if (_activeWorkers == 0) {
                // All threads finished the first pass, signal second pass start
                _workComplete = true;
                _cvWork.notify_all();
            } else {
                // Wait for all threads to finish first pass
                _cvWork.wait(lock, [this] { return _activeWorkers == 0 || _workComplete || !_running; });
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
