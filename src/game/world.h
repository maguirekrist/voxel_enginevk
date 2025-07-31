
#pragma once
#include <vk_types.h>
#include "block.h"
#include "chunk.h"

class ChunkManager;

class World {
public:
    World(ChunkManager& chunkManager) : _chunkManager(chunkManager) {    }

    std::optional<Block> get_block(const glm::ivec3& worldPos) const;
    std::optional<ChunkView> get_chunk(glm::vec3 worldPos) const;

    static glm::ivec2 get_chunk_coordinates(const glm::vec3& worldPos);
    static glm::ivec2 get_chunk_origin(const glm::vec3& worldPos);
    static glm::ivec3 get_local_coordinates(const glm::vec3& worldPos);
private:
    ChunkManager& _chunkManager;
};