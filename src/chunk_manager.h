#pragma once

#include <vk_mesh.h>
#include <chunk.h>
#include <memory>
#include <utils/concurrentqueue.h>
#include <utils/blockingconcurrentqueue.h>
#include <libcuckoo/cuckoohash_map.hh>

class VulkanEngine;


struct ChunkWork
{
    std::shared_ptr<Chunk> chunk;
    enum class Phase { Generate, Mesh };
    Phase phase;
};

class ChunkManager {
public:
    //Real chunk data
    std::unordered_map<ChunkCoord, std::shared_ptr<Chunk>> _chunks;
    std::vector<std::shared_ptr<Chunk>> _chunkList;

    //Render chunk data
    std::vector<std::unique_ptr<RenderObject>> _renderedChunks;
    std::vector<std::unique_ptr<RenderObject>> _transparentObjects;

    std::vector<ChunkCoord> _worldChunks;
    std::vector<ChunkCoord> _oldWorldChunks;

    ChunkManager();

    void cleanup();

    ~ChunkManager();

    void update_player_position(int x, int z);

    int get_chunk_index(ChunkCoord coord) const;
    std::optional<ChunkView> get_chunk(ChunkCoord coord);
    std::optional<std::vector<ChunkView>> get_chunk_neighbors(ChunkCoord coord);

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

    moodycamel::BlockingConcurrentQueue<ChunkWork> _chunkWorkQueue;

    std::vector<std::thread> _workers;

    std::shared_mutex _mapMutex;
    std::mutex _mutexWork;
    std::condition_variable _cvWork;
    std::atomic<bool> _running;
};