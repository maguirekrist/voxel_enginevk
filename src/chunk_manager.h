#pragma once

#include <vk_types.h>
#include <chunk.h>

struct WorldUpdateJob {
    int _changeX;
    int _changeZ;
    std::queue<ChunkCoord> _chunksToLoad;
    std::queue<ChunkCoord> _chunksToUnload; 
    std::queue<ChunkCoord> _chunksToMesh;
};

class ChunkManager {
public:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> _loadedChunks;
    std::vector<RenderObject> _renderChunks;
    std::unordered_set<ChunkCoord> _worldChunks;
    std::unordered_set<ChunkCoord> _oldWorldChunks;

    ChunkManager(int viewDistance) 
        : _viewDistance(viewDistance), 
          _maxChunks((2 * viewDistance + 1) * (2 * viewDistance + 1)),
          _maxThreads(std::thread::hardware_concurrency() - 1),
          _activeWorkers(0),
          _running(true) {

        _chunkPool.reserve(_maxChunks);
        for(int i = 0; i < _maxChunks; i++)
        {
            _chunkPool.emplace_back(std::make_unique<Chunk>());
        }
        for(size_t i = 0; i < _maxThreads; i++)
        {
            _workers.emplace_back(&ChunkManager::meshChunk, this);
        }
        _updateThread = std::thread(&ChunkManager::worldUpdate, this);
        _updatingWorldState = false;
    }

    ~ChunkManager() {
        _running = false;
        _cvWorld.notify_one();
        _cvMesh.notify_all();
        for (std::thread &worker : _workers) {
            worker.join();
        }
        _updateThread.join();
    }

    void updatePlayerPosition(int x, int z);

    void printLoadedChunks() {
        std::unique_lock<std::mutex> lock(_mutexWorld);
        std::cout << "Loaded chunks: ";
        for (const auto& chunk : _loadedChunks) {
            std::cout << "(" << chunk.first.x << "," << chunk.first.z << ") ";
        }
        std::cout << std::endl;
    }

    void printWorldUpdateQueue() {
        //std::unique_lock<std::mutex> lock(_mutex);
        fmt::println("World update queue count: {}", _worldUpdateQueue.size());
    }

    Block* getBlockGlobal(int x, int y, int z);

private:
    void updateWorldState();
    void queueWorldUpdate(int changeX, int changeZ);

    void worldUpdate();
    void meshChunk();

    std::vector<Chunk*> get_chunk_neighbors(ChunkCoord coord);

    int _viewDistance;
    size_t _maxChunks;
    size_t _maxThreads;
    bool _initLoad{true};
    ChunkCoord _lastPlayerChunk = {0, 0};

    std::vector<std::unique_ptr<Chunk>> _chunkPool;

    std::queue<WorldUpdateJob> _worldUpdateQueue;
    std::queue<std::tuple<Chunk*, std::vector<Chunk*>>> _chunkUpdateQueue;
    std::queue<Mesh*> _meshUploadQueue;

    bool _updatingWorldState;
    std::vector<std::thread> _workers;
    size_t _activeWorkers;
    std::thread _updateThread;

    std::mutex _mutexWorld;
    std::mutex _mutexMesh;
    std::shared_mutex _loadedMutex;

    std::condition_variable _cvWorld;
    std::condition_variable _cvMesh;
    std::atomic<bool> _running;
};