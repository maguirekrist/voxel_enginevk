#pragma once

#include <mutex>
#include <optional>
#include <queue>

#include "game/block.h"

enum class EditSource : uint8_t
{
    LocalPlayer = 0,
    RemotePlayer = 1,
    Structure = 2,
    Simulation = 3
};

struct BlockEdit
{
    glm::ivec3 worldPos{};
    Block newBlock{};
    EditSource source{EditSource::LocalPlayer};
};

class WorldEditQueue
{
public:
    void enqueue(const BlockEdit& edit);
    [[nodiscard]] std::optional<BlockEdit> try_dequeue();

private:
    std::mutex _mutex{};
    std::queue<BlockEdit> _pending{};
};
