#pragma once

#include "world.h"
#include <world/chunk_manager.h>
#include "player.h"

class VulkanEngine;

class CubeEngine {
public:
    CubeEngine();
    ~CubeEngine();

    ChunkManager _chunkManager;
    World _world{ _chunkManager };
    Player _player;
    //Main Tick entry
    void update();
private:
};