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
    enum class Phase : int
    {
        Generate = 0,
        Mesh = 2,
        WaitingForNeighbors = 1
    };
    Phase phase;
};

class ChunkWorkQueue
{
    moodycamel::BlockingConcurrentQueue<ChunkWork> _highPriority;
    moodycamel::BlockingConcurrentQueue<ChunkWork> _lowPriority;
    moodycamel::BlockingConcurrentQueue<ChunkWork> _medPriority;

public:
    void enqueue(const ChunkWork& work);
    bool try_dequeue(ChunkWork& work);
    void wait_dequeue(ChunkWork& work);
    bool wait_dequeue_timed(ChunkWork& work, int timeout_ms);
    size_t size_approx() const;
};

enum class NeighborStatus
{
    Missing,
    Incomplete,
    Ready,
    Border
};

struct MapRange
{
    std::atomic_int low_x;
    std::atomic_int high_x;
    std::atomic_int low_z;
    std::atomic_int high_z;
};

class ChunkManager {
public:
    //Real chunk data
    std::unordered_map<ChunkCoord, std::shared_ptr<Chunk>> _chunks;

    //Render chunk data
    std::vector<std::unique_ptr<RenderObject>> _renderedChunks;
    std::vector<std::unique_ptr<RenderObject>> _transparentObjects;

    std::vector<ChunkCoord> _worldChunks;

    ChunkManager();

    void cleanup();

    ~ChunkManager();

    void update_player_position(int x, int z);

    int get_chunk_index(ChunkCoord coord) const;
    std::optional<std::shared_ptr<Chunk>> get_chunk(ChunkCoord coord);
    std::optional<std::array<std::shared_ptr<Chunk>, 8>> get_chunk_neighbors(ChunkCoord coord);

    //TODO: Chunk saving and loading from disk.
    //void save_chunk(const Chunk& chunk, const std::string& filename);
    //std::unique_ptr<Chunk> load_chunk(const std::string& filename);

private:
    void update_world_state();
    void work_chunk(int threadId);
    bool chunk_at_border(ChunkCoord coord) const;
    NeighborStatus chunk_has_neighbors(ChunkCoord coord);
    //void queueWorldUpdate(int changeX, int changeZ);
    //void worldUpdate();

    //void add_chunk(ChunkCoord coord, std::unique_ptr<Chunk>&& chunk);
    bool _initialLoad{true};
    int _viewDistance;
    size_t _maxChunks;
    size_t _maxThreads;
    ChunkCoord _lastPlayerChunk = {0, 0};
    MapRange _mapRange{};

    ChunkWorkQueue _chunkWorkQueue;

    std::vector<std::thread> _workers;

    std::shared_mutex _mapMutex;
    std::atomic<bool> _running;
};