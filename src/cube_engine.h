#pragma once

#include <world.h>
#include <chunk_mesher.h>
#include <chunk_manager.h>
#include <player.h>

class VulkanEngine;

class CubeEngine {
public:
    CubeEngine(VulkanEngine& renderer) : _renderer(renderer), _chunkManager(_renderer) {}

    VulkanEngine& _renderer;
    ChunkManager _chunkManager{ _renderer };
    World _world{ &_chunkManager };
    Player _player;

    //Main Tick entry
    void update();
private:
    //void load_chunks();
};