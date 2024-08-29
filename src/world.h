
#pragma once
#include <vk_types.h>
#include <random.h>
#include <chunk.h>

class World {
public:
    int _size;
    std::vector<int> _heightMap;
    Chunk chunk;

    World(int size);

    void generate_height_map(int dim);
    void update_chunk();
};