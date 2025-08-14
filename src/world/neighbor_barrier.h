//
// Created by Maguire Krist on 8/13/25.
//

#ifndef NEIGHBOR_BARRIER_H
#define NEIGHBOR_BARRIER_H
#include <unordered_map>
#include "game/chunk.h"


struct BarrierEntry
{
    int expected;
    int remaining; // neighbors we still need
};

class NeighborBarrier {
    std::mutex m_mutex;
    std::unordered_map<ChunkCoord, BarrierEntry> m_entries;
    std::unordered_set<ChunkCoord> m_present;

public:

    template <typename It>
    void init(const ChunkCoord c, It neighborsBegin, It neighborsEnd)
    {
        std::lock_guard lock(m_mutex);
        const int expected = static_cast<int>(std::distance(neighborsBegin, neighborsEnd));
        int presentNow = 0;
        for (auto it = neighborsBegin; it != neighborsEnd; ++it)
        {
            if (m_present.contains(*it)) ++presentNow;
        }
        auto& e = m_entries[c];
        e.expected = expected;
        e.remaining = std::max(0, expected - presentNow);
    }


    void mark_present(ChunkCoord c);
    bool signal(ChunkCoord c) noexcept;
    bool try_consume_ready(ChunkCoord c);
    void cancel(ChunkCoord c);
};



#endif //NEIGHBOR_BARRIER_H
