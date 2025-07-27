#pragma once

#include <vk_mesh.h>
#include <barrier>
#include <chunk.h>
#include <memory>
#include <utils/concurrentqueue.h>
#include <utils/blockingconcurrentqueue.h>

class VulkanEngine;

struct WorldUpdateJob {
    std::queue<ChunkCoord> _chunksToUnload; 
    std::queue<ChunkCoord> _chunksToMesh;
};

// struct ChunkMeshJob
// {
//     ChunkView target;
//     std::array<ChunkView, 8> neighbors;
// };

class ChunkManager {
public:
    //Real chunk data
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> _chunks;

    //Render chunk data
    std::vector<std::unique_ptr<RenderObject>> _renderedChunks;
    std::vector<std::unique_ptr<RenderObject>> _transparentObjects;

    std::unordered_set<ChunkCoord> _worldChunks;
    std::unordered_set<ChunkCoord> _oldWorldChunks;

    ChunkManager();

    void cleanup();

    ~ChunkManager();

    void update_player_position(int x, int z);

    int get_chunk_index(ChunkCoord coord) const;
    std::optional<ChunkView> get_chunk(ChunkCoord coord);
    std::optional<std::array<ChunkView, 8>> get_chunk_neighbors(ChunkCoord coord);

    //TODO: Chunk saving and loading from disk.
    //void save_chunk(const Chunk& chunk, const std::string& filename);
    //std::unique_ptr<Chunk> load_chunk(const std::string& filename);

private:
    void update_world_state();
    void mesh_chunk(int threadId);
    //void queueWorldUpdate(int changeX, int changeZ);
    //void worldUpdate();

    //void add_chunk(ChunkCoord coord, std::unique_ptr<Chunk>&& chunk);

    int _viewDistance;
    size_t _maxChunks;
    size_t _maxThreads;
    ChunkCoord _lastPlayerChunk = {0, 0};

    //std::queue<WorldUpdateJob> _worldUpdateQueue;

    moodycamel::BlockingConcurrentQueue<std::unique_ptr<Chunk>> _chunkGenQueue{_maxChunks};
    moodycamel::BlockingConcurrentQueue<std::unique_ptr<Chunk>> _chunkMeshQueue;

    std::vector<std::thread> _workers;

    //std::mutex _mutexWorld;
    std::mutex _mutexWork;

    //std::condition_variable _cvWorld;
    std::condition_variable _cvWork;
    std::atomic<bool> _workComplete{true};

    std::barrier<> _sync_point;

    std::atomic<bool> _running;
};