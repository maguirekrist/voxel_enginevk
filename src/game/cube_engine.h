#pragma once

#include "world.h"
#include <world/chunk_manager.h>


class GameScene;

class CubeEngine {
public:
    explicit CubeEngine(GameScene& scene);
    ~CubeEngine();

    GameScene& _scene;
    ChunkManager _chunkManager;
    World _world{ _chunkManager };

    const Block* _current_block;
    const Chunk* _current_chunk;

    void update();
};
