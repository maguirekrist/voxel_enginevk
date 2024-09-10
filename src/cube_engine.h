#pragma once

#include <world.h>
#include <chunk_mesher.h>
#include <player.h>

//number is in chunks away from player position.
constexpr int DEFAULT_VIEW_DISTANCE = 4;

class CubeEngine {
public:
    World _world;
    Player _player;
	ChunkMesher _chunkMesher{ _world };

    //Main Tick entry
    void update();
private:
    void load_chunks();
};