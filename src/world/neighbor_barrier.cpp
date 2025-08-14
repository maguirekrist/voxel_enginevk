//
// Created by Maguire Krist on 8/13/25.
//

#include "neighbor_barrier.h"

void NeighborBarrier::mark_present(const ChunkCoord c)
{
    std::lock_guard lock(m_mutex);
    m_present.insert(c);
}

bool NeighborBarrier::signal(const ChunkCoord c) noexcept
{
    std::lock_guard lock(m_mutex);
    if (!m_entries.contains(c))
    {
        return false;
    }
    auto& [initial, remaining] = m_entries[c];
    if (remaining > 0)
    {
        --remaining;
    }
    if (remaining == 0)
    {
        m_entries.erase(c);
        return true;
    }
    return false;
}

bool NeighborBarrier::try_consume_ready(ChunkCoord c)
{
    std::lock_guard lock(m_mutex);
    if (!m_entries.contains(c))
    {
        return false;
    }
    auto& [initial, remaining] = m_entries[c];
    if (remaining == 0)
    {
        m_entries.erase(c);
        return true;
    }
    return false;
}

void NeighborBarrier::cancel(ChunkCoord c)
{
    std::lock_guard lock(m_mutex);
    m_entries.erase(c);
    m_present.erase(c);
}
