
#pragma once
#include <vk_types.h>
#include <chunk.h>

class ChunkManager;

class World {
public:
    World(ChunkManager* chunkManager) : _chunkManager(chunkManager) {    }

    Block* get_block(const glm::ivec3& worldPos);
    Chunk* get_chunk(glm::vec3 worldPos); 

    static glm::ivec2 get_chunk_coordinates(const glm::vec3& worldPos);
    static glm::ivec2 get_chunk_origin(const glm::vec3& worldPos);
    static glm::ivec3 get_local_coordinates(const glm::vec3& worldPos);
private:
    ChunkManager* _chunkManager;
};