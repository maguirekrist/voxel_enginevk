
#include "chunk_manager.h"
#include "chunk.h"
#include "chunk_mesher.h"
#include "vk_engine.h"
#include "tracy/Tracy.hpp"
#include "thread_types.h"
#include <chrono>
#include <mutex>

ChunkManager::ChunkManager(VulkanEngine& renderer) 
        : _viewDistance(DEFAULT_VIEW_DISTANCE),
          _maxChunks((2 * DEFAULT_VIEW_DISTANCE + 1) * (2 * DEFAULT_VIEW_DISTANCE + 1)),
          _maxThreads(4),
          _running(true),
          _terrainGenerator(TerrainGenerator::instance()),
          _renderer(renderer),
          _sync_point(4) {
        
        _renderChunks.reserve(_maxChunks);
        _chunkPool.reserve(_maxChunks);
        for(int i = 0; i < _maxChunks; i++)
        {
            _chunkPool.emplace_back(std::make_unique<Chunk>());
            _renderChunks.emplace_back();
        }
        for(size_t i = 0; i < _maxThreads; i++)
        {
            _workers.emplace_back(&ChunkManager::meshChunk, this, i);
        }
        _updateThread = std::thread(&ChunkManager::worldUpdate, this);
        _updatingWorldState = false;
    }

ChunkManager::~ChunkManager()
{
    _running = false;
    _cvWorld.notify_one();
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
    _cvWorld.notify_one();

    if(_initLoad) {
        std::unique_lock<std::mutex> lock(_mutexWorld);
        _cvWorld.wait(lock, [this]() { return !_initLoad; });
    }
}

// void ChunkManager::updateRender()
// {
//     if(_chunksToRender.empty()) return;

//     //this is where we safely update _renderChunks by handling the queue
//     std::unique_lock<std::mutex> worldLock(_mutexWorld);

//     for(const auto& [coord, chunk] : _chunksToRender)
//     {
//         if(chunk == nullptr)
//         {
//             _chunksToRender.erase(coord);
//         }

//         if(chunk->_isValid)
//         {
//             RenderObject object;
// 			object.material = _renderer.get_material("defaultmesh");
// 			object.mesh = &chunk->_mesh;
// 			glm::mat4 translate = glm::translate(glm::mat4{ 1.0 }, glm::vec3(chunk->_position.x, 0, chunk->_position.y));
// 			object.transformMatrix = translate;
//             object.isValid = true;

//             //get a unique index for the chunk coord
//             int x = coord.x - _lastPlayerChunk.x;
//             int y = coord.z - _lastPlayerChunk.z;

//             // Normalize the relative coordinates to the range [0, 64] by adding viewDistance
//             int normalizedX = std::clamp(x + _viewDistance, 0, (2 * DEFAULT_VIEW_DISTANCE + 1) - 1);
//             int normalizedZ = std::clamp(y + _viewDistance, 0, (2 * DEFAULT_VIEW_DISTANCE + 1) - 1);

//             // Calculate the 1D index in the vector
//             int index = normalizedZ * (2 * DEFAULT_VIEW_DISTANCE + 1) + normalizedX;


//             _renderChunks[index] = object;

//             _chunksToRender.erase(coord);
//         }
//     }
// }

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
    tracy::SetThreadName("World Update Thread");
    while (_running) {
        std::unique_lock<std::mutex> lock(_mutexWorld);
        _cvWorld.wait(lock, [this] { return (!_worldUpdateQueue.empty() && !_updatingWorldState) || !_running; });

        if (!_running) break;

        _updatingWorldState = true;

        fmt::println("Worker thread begin work.");

        WorldUpdateJob updateJob = std::move(_worldUpdateQueue.front());
        _worldUpdateQueue.pop();
        {
            ZoneScopedN("World Update Process");
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
                _chunkGenQueue.enqueue(ptrChunk);
            }

            //Calculate the chunks to mesh now, which are the 
            while(!updateJob._chunksToMesh.empty())
            {
                ChunkCoord coord = updateJob._chunksToMesh.front();
                updateJob._chunksToMesh.pop();

                auto chunk = _loadedChunks[coord].get();
                auto neighbors = get_chunk_neighbors(coord);
                if(chunk != nullptr && neighbors.has_value())
                {
                    _chunkMeshQueue.enqueue({ chunk, neighbors.value() });
                }
            }
        }
        _workComplete = false;

        _cvWork.notify_all();    

        {
            std::unique_lock<std::mutex> workLock(_mutexWork);
            _cvWork.wait(workLock, [this]{ return _workComplete == true;});
        }

        //Look into this
        while (!updateJob._chunksToUnload.empty())
        {
            const auto coord = updateJob._chunksToUnload.front();
            updateJob._chunksToUnload.pop();

            std::unique_ptr<Chunk> unloadChunk = std::move(_loadedChunks[coord]);
            _loadedChunks.erase(coord);

            _renderer._mainMeshUnloadQueue.enqueue(&unloadChunk->_mesh);

            _chunkPool.push_back(std::move(unloadChunk));
        }

        fmt::println("Worker thread end work.");

        _updatingWorldState = false;
        _initLoad = false;
        _cvWorld.notify_one();
    }
}

std::optional<std::array<Chunk*, 8>> ChunkManager::get_chunk_neighbors(ChunkCoord coord)
{
    ZoneScopedN("Get Chunk Neighbors");
    std::array<Chunk*, 8> chunks;
    int count = 0;
    for (auto direction : directionList)
    {
        auto offsetX = directionOffsetX[direction];
        auto offsetZ = directionOffsetZ[direction];
        auto offset_coord = ChunkCoord{ coord.x + offsetX, coord.z + offsetZ };
        auto chunk_it = _loadedChunks.find(offset_coord);
        if(chunk_it != _loadedChunks.end())
        {
            count++;
            chunks[direction] = chunk_it->second.get();
        }
    }

    if(count == 8)
    {
        return chunks;
    } else {
        return std::nullopt;
    }
}

int ChunkManager::get_chunk_index(ChunkCoord coord)
{
    //get a unique index for the chunk coord
    int x = coord.x - _lastPlayerChunk.x;
    int y = coord.z - _lastPlayerChunk.z;

    // Normalize the relative coordinates to the range [0, 64] by adding viewDistance
    int normalizedX = std::clamp(x + _viewDistance, 0, (2 * DEFAULT_VIEW_DISTANCE + 1) - 1);
    int normalizedZ = std::clamp(y + _viewDistance, 0, (2 * DEFAULT_VIEW_DISTANCE + 1) - 1);

    // Calculate the 1D index in the vector
    int index = normalizedZ * (2 * DEFAULT_VIEW_DISTANCE + 1) + normalizedX;
}

void ChunkManager::add_chunk(ChunkCoord coord, std::unique_ptr<Chunk>&& chunk)
{
    int index = get_chunk_index(coord);
    _chunks[index] = std::move(chunk);
}

Chunk* ChunkManager::get_chunk(ChunkCoord coord)
{
    int index = get_chunk_index(coord);
    return _chunks[index].get();
}

void ChunkManager::meshChunk(int threadId)
{
    tracy::SetThreadName(fmt::format("Chunk Update Thread: {}", threadId).c_str());
    while(true)
    { 
        //prevent this area from being done unless there is actual work...
        {
            std::unique_lock<std::mutex> workLock(_mutexWork);
            _cvWork.wait(workLock, [this](){ return _workComplete == false; });
        }

        while(_running)
        {
            // First pass: Chunk generation
            Chunk* chunk;
            if(_chunkGenQueue.wait_dequeue_timed(chunk, std::chrono::milliseconds(5)))
            {
                if (chunk != nullptr) {
                    chunk->generate(_terrainGenerator); 
                }
            } else {
                break;
            }   
        }

        _sync_point.arrive_and_wait();

        while(_running)
        {
            std::pair<Chunk*, std::array<Chunk*, 8>> myPair;
            if(_chunkMeshQueue.wait_dequeue_timed(myPair, std::chrono::milliseconds(5)))
            {
                const auto [chunk, neighbors] = myPair;
                if(chunk != nullptr && neighbors.size() == 8)
                {
                    //Mesh oldMesh = chunk->_mesh;
                    ChunkMesher mesher { *chunk, neighbors };
                    mesher.execute();
                    chunk->_isValid = true;
                }
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
