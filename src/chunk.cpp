#include "chunk.h"
#include <terrain_gen.h>
#include "tracy/Tracy.hpp"

void Chunk::reset(ChunkCoord newCoord)
{
    _position = glm::ivec2(newCoord.x * CHUNK_SIZE, newCoord.z * CHUNK_SIZE);
    //_isValid = false;
    // _mesh = std::make_shared<Mesh>();
}

void Chunk::generate()
{
        ZoneScopedN("Generate chunk");

        //std::vector<float> chunkDensityMap = generator.GenerateDensityMap(_position.x, _position.y);
        // for(int x = 0; x < CHUNK_SIZE; x++)
        // {
        //     for(int z = 0; z < CHUNK_SIZE; z++)
        //     {
        //         for (int y = 0; y < CHUNK_HEIGHT; y++)
        //         {
        //             float density = generator.SampleNoise3D(x + _position.x, y, z + _position.y);
        //             Block& block = _blocks[x][y][z];
        //             if (density > 0)
        //             {
        //                 block._solid = true;
        //                 block._type = BlockType::GROUND;
        //                 block._sunlight = 0;
        //             }
        //             else {
        //                 block._solid = false;
        //                 block._type = BlockType::AIR;
        //                 block._sunlight = MAX_LIGHT_LEVEL;
//             }
        //         }
        //     }
        // }


        std::vector<float> chunkHeightMap = TerrainGenerator::instance().GenerateHeightMap(_position.x, _position.y);
        for(int x = 0; x < CHUNK_SIZE; x++)
        {
            for(int z = 0; z < CHUNK_SIZE; z++)
            {
                // auto height = generator.NormalizeHeight(chunkHeightMap, CHUNK_HEIGHT, CHUNK_SIZE, x, z);
                for (int y = 0; y < CHUNK_HEIGHT; y++)
                {
                    Block& block = _blocks[x][y][z];
                    if (y <= chunkHeightMap[(z * CHUNK_SIZE) + x])
                    {
                        block._solid = true;
                        block._type = BlockType::GROUND;
                        block._sunlight = 0;
                    } 
                    else if(y <= SEA_LEVEL) {
                        block._solid = false;
                        block._type = BlockType::WATER;
                        int sunLight = std::max(-(static_cast<int>(SEA_LEVEL) - y) + static_cast<int>(MAX_LIGHT_LEVEL), 0);
                        block._sunlight = std::clamp(static_cast<uint8_t>(sunLight), static_cast<uint8_t>(1), static_cast<uint8_t>(MAX_LIGHT_LEVEL));
                    }
                    else {
                        block._solid = false;
                        block._type = BlockType::AIR;
                        block._sunlight = MAX_LIGHT_LEVEL;
                    }
                }
            }
        }
}


//This returns the block at the world coordinates of pos.
//This is why we normalize the position value passed in.
std::optional<Block> ChunkView::get_block(const glm::ivec3& localPos) const
{
    if (Chunk::is_outside_chunk(localPos))
    {
        return std::nullopt;
    }

    return blocks[localPos.x][localPos.y][localPos.z];
}

glm::ivec3 ChunkView::get_world_pos(const glm::ivec3& localPos) const
{
    return { localPos.x + position.x, localPos.y, localPos.z + position.y };
}

glm::ivec3 Chunk::get_world_pos(const glm::ivec3& localPos) const
{
    return { localPos.x + _position.x, localPos.y, localPos.z + _position.y };
}


