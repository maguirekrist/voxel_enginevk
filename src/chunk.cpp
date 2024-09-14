#include "chunk.h"
#include <world.h>

bool Chunk::is_outside_chunk(const glm::ivec3& pos)
{
    if (pos.x < 0 || pos.x >= CHUNK_SIZE || pos.y < 0 || pos.y >= CHUNK_HEIGHT || pos.z < 0 || pos.z >= CHUNK_SIZE) {
        return true;
    }

    return false;
}

void Chunk::reset(ChunkCoord newCoord)
{
    _position = glm::ivec2(newCoord.x * CHUNK_SIZE, newCoord.z * CHUNK_SIZE);
    _isValid = false;
    _mesh = Mesh{};
}

void Chunk::generate()
{
        auto heightMap = World::generate_height_map(_position.x, _position.y);
        for(int x = 0; x < CHUNK_SIZE; x++)
            {
            for(int z = 0; z < CHUNK_SIZE; z++)
            {
                auto height = World::get_normalized_height(heightMap, CHUNK_HEIGHT, CHUNK_SIZE, x, z);
                for (int y = 0; y < CHUNK_HEIGHT; y++)
                {
                    Block& block = _blocks[x][y][z];
                    block._position = glm::vec3(x, y, z);
                    block._color = glm::vec3(1.0f, 1.0f, 1.0f);
                    if (y <= height)
                    {
                        block._solid = true;
                        block._sunlight = 0;
                    }
                    else {
                        block._solid = false;
                        block._sunlight = MAX_LIGHT_LEVEL;
                    }
                }
            }
        }
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
    return { localPos.x + (_position.x), localPos.y, localPos.z + _position.y };
}


