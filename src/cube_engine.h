#pragma once

#include <world.h>
#include <chunk_mesher.h>
#include <chunk_manager.h>
#include <player.h>

class VulkanEngine;

struct RaycastResult {
    Block* _block;
    FaceDirection _blockFace;
    Chunk* _chunk;
    glm::ivec3 _worldPos;
    float _distance;
};


class CubeEngine {
public:
    ChunkManager _chunkManager;
    World _world{ &_chunkManager };
    Player _player;

    static std::optional<RaycastResult> get_target_block(World& world, Player& player);

    void cleanup();
    void update();
private:
};