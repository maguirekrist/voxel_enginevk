//
// Created by Maguire Krist on 8/11/25.
//

#include "chunk_queue.h"

void ChunkWorkQueue::enqueue(const ChunkWorkPayload&& work)
{
    switch (work->phase)
    {
    case ChunkWork::Phase::Generate:
        _highPriority.enqueue(work);
        break;
    case ChunkWork::Phase::Mesh:
        _medPriority.enqueue(work);
        break;
    case ChunkWork::Phase::WaitingForNeighbors:
        _lowPriority.enqueue(work);
        break;
    }
}

bool ChunkWorkQueue::try_dequeue(ChunkWorkPayload& work)
{
    return _highPriority.try_dequeue(work) || _medPriority.try_dequeue(work) || _lowPriority.try_dequeue(work);
}

void ChunkWorkQueue::wait_dequeue(ChunkWorkPayload& work)
{
    if (_highPriority.try_dequeue(work)) return;
    if (_medPriority.try_dequeue(work)) return;
    _lowPriority.wait_dequeue(work);
}

bool ChunkWorkQueue::wait_dequeue_timed(ChunkWorkPayload& work, const int timeout_ms)
{
    return _highPriority.try_dequeue(work) || _medPriority.try_dequeue(work) || _lowPriority.wait_dequeue_timed(work, std::chrono::milliseconds(timeout_ms));
}

size_t ChunkWorkQueue::size_approx() const
{
    return _highPriority.size_approx() + _lowPriority.size_approx() + _medPriority.size_approx();
}