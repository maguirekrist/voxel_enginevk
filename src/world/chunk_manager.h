#pragma once

#include "../game/chunk.h"
#include <memory>
#include "../utils/concurrentqueue.h"
#include "../utils/blockingconcurrentqueue.h"
#include <libcuckoo/cuckoohash_map.hh>

struct MapRange
{
    int low_x{0};
    int high_x{0};
    int low_z{0};
    int high_z{0};

    MapRange() = default;

    MapRange(const ChunkCoord center, const int viewDistance) :
    low_x(center.x - viewDistance), high_x(center.x + viewDistance), low_z(center.z - viewDistance), high_z(center.z + viewDistance)
    {
    }

    [[nodiscard]] constexpr bool contains(const ChunkCoord& coord) const
    {
        return (coord.x >= low_x &&
            coord.x <= high_x &&
            coord.z >= low_z &&
            coord.z <= high_z);
    }

    [[nodiscard]] constexpr bool is_border(const ChunkCoord& coord) const
    {
        return (coord.x == low_x ||
            coord.x == high_x ||
            coord.z == low_z ||
            coord.z == high_z);
    }
};

struct ChunkWork
{
    enum class Phase : int
    {
        Generate = 0,
        Mesh = 2,
        WaitingForNeighbors = 1
    };

    std::shared_ptr<Chunk> chunk;
    Phase phase;
    MapRange mapRange;
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

class ChunkManager {
public:
    std::unordered_map<ChunkCoord, std::shared_ptr<Chunk>> _chunks;

    ChunkManager();
    ~ChunkManager();

    void update_player_position(int x, int z);
    //int get_chunk_index(ChunkCoord coord) const;
    std::optional<std::shared_ptr<Chunk>> get_chunk(ChunkCoord coord);
    std::optional<std::array<std::shared_ptr<Chunk>, 8>> get_chunk_neighbors(ChunkCoord coord);

    //TODO: Chunk saving and loading from disk.
    //void save_chunk(const Chunk& chunk, const std::string& filename);
    //std::unique_ptr<Chunk> load_chunk(const std::string& filename);

private:
    void work_chunk(int threadId);
    void update_map(MapRange mapRange, ChunkCoord delta);
    void initialize_map(MapRange mapRange);
    NeighborStatus chunk_has_neighbors(ChunkCoord coord) const;

    bool _initialLoad{true};
    int _viewDistance{GameConfig::DEFAULT_VIEW_DISTANCE};
    size_t _maxChunks{GameConfig::MAXIMUM_CHUNKS};
    size_t _maxThreads{4};
    ChunkCoord _lastPlayerChunk = {0, 0};

    ChunkWorkQueue _chunkWorkQueue;

    std::vector<std::thread> _workers;

    std::atomic<bool> _running{true};
};