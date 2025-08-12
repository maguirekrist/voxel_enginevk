//
// Created by Maguire Krist on 8/11/25.
//

#ifndef CHUNK_QUEUE_H
#define CHUNK_QUEUE_H
#include "game/chunk.h"
#include "utils/blockingconcurrentqueue.h"

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

    Chunk* const chunk;
    const Phase phase;
    const MapRange mapRange;
};

using ChunkWorkPayload = std::shared_ptr<const ChunkWork>;

class ChunkWorkQueue
{
    moodycamel::BlockingConcurrentQueue<ChunkWorkPayload> _highPriority;
    moodycamel::BlockingConcurrentQueue<ChunkWorkPayload> _lowPriority;
    moodycamel::BlockingConcurrentQueue<ChunkWorkPayload> _medPriority;

public:
    void enqueue(const ChunkWorkPayload&& work);
    bool try_dequeue(ChunkWorkPayload& work);
    void wait_dequeue(ChunkWorkPayload& work);
    bool wait_dequeue_timed(ChunkWorkPayload& work, int timeout_ms);
    size_t size_approx() const;
};



#endif //CHUNK_QUEUE_H
