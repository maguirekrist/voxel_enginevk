#pragma once

#include <chunk.h>

class VulkanEngine;

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

    ChunkManager(VulkanEngine& renderer);

    ~ChunkManager();

    void updatePlayerPosition(int x, int z);

    Block* getBlockGlobal(int x, int y, int z);

private:
    void updateWorldState();
    void queueWorldUpdate(int changeX, int changeZ);

    void worldUpdate();
    void meshChunk(int threadId);

    std::optional<std::array<Chunk*, 8>> get_chunk_neighbors(ChunkCoord coord);

    int _viewDistance;
    size_t _maxChunks;
    size_t _maxThreads;
    bool _initLoad{true};
    ChunkCoord _lastPlayerChunk = {0, 0};

    TerrainGenerator& _terrainGenerator;
    VulkanEngine& _renderer;

    std::vector<std::unique_ptr<Chunk>> _chunkPool;

    std::queue<WorldUpdateJob> _worldUpdateQueue;
    std::queue<Chunk*> _chunkGenQueue;
    std::queue<std::tuple<Chunk*, std::array<Chunk*, 8>>> _chunkMeshQueue;
    std::queue<Mesh*> _meshUploadQueue;

    bool _updatingWorldState;
    std::vector<std::thread> _workers;
    std::atomic<size_t> _activeWorkers;
    size_t _workerCount = 0;
    std::thread _updateThread;

    std::mutex _mutexWorld;
    std::mutex _mutexPool;
    std::mutex _mutex_barrier;
    std::mutex _mutexWork;
    std::shared_mutex _loadedMutex;

    std::condition_variable _cvWorld;
    std::condition_variable _cvMesh;
    std::condition_variable _cvGen;
    std::condition_variable _cvWork;
    std::atomic<bool> _workComplete;

    std::atomic<bool> _running;
};