#include "chunk.h"

bool Chunk::is_outside_chunk(const glm::ivec3& pos)
{
    if (pos.x < 0 || pos.x >= CHUNK_SIZE || pos.y < 0 || pos.y >= CHUNK_HEIGHT || pos.z < 0 || pos.z >= CHUNK_SIZE) {
        return true;
    }

    return false;
}


//This returns the block at the world coordinates of pos.
//This is why we normalize the position value passed in.
Block* Chunk::get_block(const glm::ivec3& localPos)
{
    if (Chunk::is_outside_chunk(localPos))
    {
        return nullptr;
    }

    return &_blocks[localPos.x][localPos.y][localPos.z];
}

glm::ivec3 Chunk::get_world_pos(const glm::ivec3& localPos)
{
    return { localPos.x + _position.x, localPos.y, localPos.z + _position.y };
}


