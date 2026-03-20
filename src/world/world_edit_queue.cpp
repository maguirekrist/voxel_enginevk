#include "world_edit_queue.h"

void WorldEditQueue::enqueue(const BlockEdit& edit)
{
    std::lock_guard lock(_mutex);
    _pending.push(edit);
}

std::optional<BlockEdit> WorldEditQueue::try_dequeue()
{
    std::lock_guard lock(_mutex);
    if (_pending.empty())
    {
        return std::nullopt;
    }

    BlockEdit edit = _pending.front();
    _pending.pop();
    return edit;
}
