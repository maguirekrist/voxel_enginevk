#pragma once

#include <world.h>
#include <chunk_mesher.h>
#include <chunk_manager.h>
#include <player.h>


//number is in chunks away from player position.
constexpr int DEFAULT_VIEW_DISTANCE = 8;

class CubeEngine {
public:
    Player _player;
    ChunkManager _chunkManager{DEFAULT_VIEW_DISTANCE};
    World _world{ &_chunkManager };
	//ChunkMesher _chunkMesher{ _world };

    //CubeEngine(VulkanEngine* renderer) : _renderer(renderer) {}
    //CubeEngine();
    CubeEngine() {}

    //Main Tick entry
    void update();
private:
    //VulkanEngine* _renderer = nullptr;
    //void load_chunks();
};