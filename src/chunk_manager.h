#pragma once

#include <vk_types.h>
#include <chunk_mesher.h>
#include "chunk.h"

class ChunkManager {
public:
    ChunkManager(size_t initialPoolSize);

private:
    size_t poolSize_;
    std::vector<Chunk> pool_;
    //ChunkMesher chunkMesher_{ *this };
};