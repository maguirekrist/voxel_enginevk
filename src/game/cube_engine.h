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

    Block* _current_block;
    std::weak_ptr<Chunk> _current_chunk;

    void update();
};
