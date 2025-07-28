#pragma once

#include <game/world.h>
#include <chunk_mesher.h>
#include <chunk_manager.h>
#include <game/player.h>

class VulkanEngine;

class CubeEngine {
public:
    CubeEngine();

    ChunkManager _chunkManager;
    World _world{ &_chunkManager };
    Player _player;

    void cleanup();

    //Main Tick entry
    void update();
private:
};