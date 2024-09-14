#pragma once

#include <vk_types.h>
#include <chunk.h>

struct WorldUpdateJob {
    int _changeX;
    int _changeZ;
    std::queue<ChunkCoord> _chunksToLoad;
    std::queue<ChunkCoord> _chunksToUnload; 
};

class ChunkManager {
public:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> _loadedChunks;
    std::unordered_set<ChunkCoord> _worldChunks;
    std::unordered_set<ChunkCoord> _oldWorldChunks;

    ChunkManager(int viewDistance) 
        : _viewDistance(viewDistance), 
          _maxChunks((2 * viewDistance + 1) * (2 * viewDistance + 1)),
          _maxThreads(1),
          _running(true) {
        _chunkPool.reserve(_maxChunks);
        for(int i = 0; i < _maxChunks; i++)
        {
            _chunkPool.emplace_back(std::make_unique<Chunk>());
        }
        for(size_t i = 0; i < _maxThreads; i++)
        {
            _workers.emplace_back(&ChunkManager::worker, this);
        }
        //_workerThread = std::thread(&ChunkManager::worker, this);
    }

    ~ChunkManager() {
        _running = false;
        _cv.notify_all();
        for (std::thread &worker : _workers) {
            worker.join();
        }
    }

    void updatePlayerPosition(int x, int z);

    void printLoadedChunks() {
        std::unique_lock<std::mutex> lock(_mutex);
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
    Block* getBlockGlobal(glm::ivec3 pos);

private:
    void updateWorldState();
    void queueWorldUpdate(int changeX, int changeZ);

    void worker();

    int _viewDistance;
    size_t _maxChunks;
    size_t _maxThreads;
    bool _initLoad{true};
    ChunkCoord _lastPlayerChunk = {0, 0};

    std::vector<std::unique_ptr<Chunk>> _chunkPool;

    std::queue<WorldUpdateJob> _worldUpdateQueue;

    std::vector<std::thread> _workers;
    //std::thread _workerThread;
    std::mutex _mutex;

    std::condition_variable _cv;
    std::atomic<bool> _running;
};