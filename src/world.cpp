
#include "world.h"
#include "chunk_manager.h"

Block* World::get_block(const glm::ivec3& worldPos)
{
    Chunk* chunk = get_chunk(worldPos);

    if (chunk == nullptr)
    {
        return nullptr;
    }

    auto localPos = get_local_coordinates(worldPos);

    if (Chunk::is_outside_chunk(localPos))
    {
        return nullptr;
    }

    return &chunk->_blocks[localPos.x][localPos.y][localPos.z];
}

Chunk* World::get_chunk(glm::vec3 worldPos)
{
    auto chunkCoord = get_chunk_coordinates(worldPos);
    auto chunkKey = ChunkCoord{chunkCoord.x, chunkCoord.y };

    auto chunk = _chunkManager->get_chunk(chunkKey);
    return chunk;
}

// void World::generate_chunk(int xStart, int yStart)
// {
//     auto chunkKey = get_chunk_key({ xStart, yStart });
//     if(_chunkMap.contains(chunkKey))
//     {
//         return;
//     }


//     fmt::println("Generating chunk ({}, {})", xStart, yStart);

//     std::vector<float> heightMap(CHUNK_SIZE * CHUNK_SIZE);
//     _generator.base->GenUniformGrid2D(heightMap.data(), xStart, yStart, CHUNK_SIZE, CHUNK_SIZE, 0.001f, _seed);
//     //median_filter(heightMap, CHUNK_SIZE, 5);
//     auto chunk = std::make_unique<Chunk>(glm::ivec2(xStart, yStart), chunkKey);
//     update_chunk(*chunk, heightMap);
//     _chunkMap[chunkKey] = std::move(chunk);
//     //_chunks.push_back(std::move(chunk));
//     fmt::println("Completed chunk generation.");
// }

glm::ivec2 World::get_chunk_coordinates(const glm::vec3 &worldPos)
{
    return glm::ivec2(
        static_cast<int>(std::floor(worldPos.x / CHUNK_SIZE)),
        static_cast<int>(std::floor(worldPos.z / CHUNK_SIZE))
    );
}

glm::ivec2 World::get_chunk_origin(const glm::vec3 &worldPos)
{
    auto chunkCords = get_chunk_coordinates(worldPos);
    return glm::ivec2(
        chunkCords.x * CHUNK_SIZE,
        chunkCords.y * CHUNK_SIZE
    );
}

glm::ivec3 World::get_local_coordinates(const glm::vec3 &worldPos)
{
    return glm::ivec3(
        static_cast<int>(std::floor(worldPos.x)) & (CHUNK_SIZE - 1),
        static_cast<int>(std::floor(worldPos.y)) & (CHUNK_HEIGHT - 1),
        static_cast<int>(std::floor(worldPos.z)) & (CHUNK_SIZE - 1)
    );
}

// void init_sunlight(Chunk& chunk)
// {
//     for (int x = 0; x < CHUNK_SIZE; x++) {
//         for (int z = 0; z < CHUNK_SIZE; z++) {
//             int y = CHUNK_HEIGHT - 1;
//             while (y >= 0 && !chunk._blocks[x][y][z]._solid) {
//                 chunk._blocks[x][y][z]._sunlight = MAX_LIGHT_LEVEL;
//                 --y;
//             }

//             while (y >= 0) {
//                 chunk._blocks[x][y][z]._sunlight = 0;
//                 --y;
//             }
//         }
//     }
// }

// void World::update_chunk(Chunk& chunk, std::vector<float>& heightMap)
// {
//     //Given the height map, we're going to update the blocks in our default chunk.
//     for(int x = 0; x < CHUNK_SIZE; x++)
//     {
//         for(int z = 0; z < CHUNK_SIZE; z++)
//         {
//             auto height = get_normalized_height(heightMap, CHUNK_HEIGHT, CHUNK_SIZE, x, z);
//             for (int y = 0; y < CHUNK_HEIGHT; y++)
//             {
//                 Block& block = chunk._blocks[x][y][z];
//                 block._position = glm::vec3(x, y, z);
//                 block._color = glm::vec3(1.0f, 1.0f, 1.0f);
//                 if (y <= height)
//                 {
//                     block._solid = true;
//                     block._sunlight = 0;
//                 }
//                 else {
//                     block._solid = false;
//                     block._sunlight = MAX_LIGHT_LEVEL;
//                 }
//             }
//         }
//     }

//     //this is not needed
//     //init_sunlight(chunk);
// }
